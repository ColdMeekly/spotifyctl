#include "impl.h"
#include "position.h"
#include "smtc_source.h"
#include "audio_session.h"
#include "spotify/title_parser.h"

#include <Psapi.h>
#include <shellapi.h>

#include <algorithm>
#include <chrono>
#include <cwchar>
#include <vector>

namespace spotify {

// ------------------------------------------------------------------------
// Free helpers
// ------------------------------------------------------------------------

std::string WideToUtf8(std::wstring_view w) {
    if (w.empty()) return {};
    const int len = static_cast<int>(w.size());
    int needed = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), len,
                                       nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out(static_cast<std::size_t>(needed), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.data(), len,
                          out.data(), needed, nullptr, nullptr);
    return out;
}

std::wstring Utf8ToWide(std::string_view s) {
    if (s.empty()) return {};
    const int len = static_cast<int>(s.size());
    int needed = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), len, nullptr, 0);
    if (needed <= 0) return {};
    std::wstring out(static_cast<std::size_t>(needed), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(), len, out.data(), needed);
    return out;
}

bool IsSpotifyWindow(HWND hWnd) {
    wchar_t cls[MAX_PATH]{};
    ::GetClassNameW(hWnd, cls, static_cast<int>(std::size(cls)));
    if (std::wcscmp(cls, L"Chrome_WidgetWin_0") != 0) return false;

    // The main Spotify chrome has WS_EX_WINDOWEDGE; its many helper /
    // tooltip sub-windows do not. This replaces the older WS_VISIBLE check,
    // which rejected Spotify when it was minimized to the system tray.
    if (!(::GetWindowLongW(hWnd, GWL_EXSTYLE) & WS_EX_WINDOWEDGE)) return false;

    DWORD pid = 0;
    ::GetWindowThreadProcessId(hWnd, &pid);
    HANDLE hProc = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) return false;

    wchar_t path[MAX_PATH]{};
    DWORD sz = static_cast<DWORD>(std::size(path));
    BOOL ok = ::QueryFullProcessImageNameW(hProc, 0, path, &sz);
    ::CloseHandle(hProc);
    if (!ok || sz == 0) return false;

    const wchar_t* base = std::wcsrchr(path, L'\\');
    return base && _wcsicmp(base, L"\\Spotify.exe") == 0;
}

// ------------------------------------------------------------------------
// Registry: WinEventHook callbacks are C function pointers with no user
// data, so we route events through a process-global registry of live Impls.
// ------------------------------------------------------------------------

namespace {

std::mutex& RegistryMu() {
    static std::mutex m;
    return m;
}

std::vector<SpotifyClient::Impl*>& Registry() {
    static std::vector<SpotifyClient::Impl*> v;
    return v;
}

void RegisterImpl(SpotifyClient::Impl* p) {
    std::lock_guard lock(RegistryMu());
    Registry().push_back(p);
}

void UnregisterImpl(SpotifyClient::Impl* p) {
    std::lock_guard lock(RegistryMu());
    auto& v = Registry();
    v.erase(std::remove(v.begin(), v.end(), p), v.end());
}

std::vector<SpotifyClient::Impl*> SnapshotRegistry() {
    std::lock_guard lock(RegistryMu());
    return Registry();
}

}  // namespace

// ------------------------------------------------------------------------
// WinEvent callbacks
// ------------------------------------------------------------------------

void CALLBACK SpotifyClient::Impl::OnWindowEvent(
    HWINEVENTHOOK, DWORD event, HWND hwnd,
    LONG /*idObject*/, LONG /*idChild*/,
    DWORD /*idEventThread*/, DWORD /*dwmsEventTime*/) {

    if (!::IsWindow(hwnd)) return;

    for (auto* impl : SnapshotRegistry()) {
        const bool isOurs = (hwnd == impl->window.load());
        if (!isOurs && !IsSpotifyWindow(hwnd)) continue;

        if (event == EVENT_OBJECT_CREATE) {
            impl->SetWindow(hwnd);
        } else if (event == EVENT_OBJECT_DESTROY && isOurs) {
            impl->SetWindow(nullptr);
        }
    }
}

void CALLBACK SpotifyClient::Impl::OnNameEvent(
    HWINEVENTHOOK, DWORD event, HWND hwnd,
    LONG /*idObject*/, LONG /*idChild*/,
    DWORD /*idEventThread*/, DWORD /*dwmsEventTime*/) {

    if (event != EVENT_OBJECT_NAMECHANGE) return;
    // WS_EX_WINDOWEDGE filters out the many helper / tooltip sub-windows
    // that Spotify's Chromium embed creates.
    const LONG exStyle = ::GetWindowLongW(hwnd, GWL_EXSTYLE);
    if (!(exStyle & WS_EX_WINDOWEDGE)) return;

    wchar_t buf[256]{};
    ::GetWindowTextW(hwnd, buf, static_cast<int>(std::size(buf)) - 1);
    std::string utf8 = WideToUtf8(buf);

    for (auto* impl : SnapshotRegistry()) {
        if (hwnd == impl->window.load()) {
            impl->self->OnRawTitle(utf8);
            impl->ApplyTitle(utf8);
        }
    }
}

// ------------------------------------------------------------------------
// Impl::SetWindow / PublishState
// ------------------------------------------------------------------------

void SpotifyClient::Impl::SetWindow(HWND hWnd) {
    const bool wasOpen = (window.load() != nullptr);
    window.store(hWnd);
    DWORD pid = 0;
    if (hWnd) {
        ::GetWindowThreadProcessId(hWnd, &pid);
    }
    processId.store(pid);
    const bool nowOpen = (hWnd != nullptr);

    if (!wasOpen && nowOpen) {
        hookTitle = ::SetWinEventHook(
            EVENT_OBJECT_NAMECHANGE, EVENT_OBJECT_NAMECHANGE, nullptr,
            &Impl::OnNameEvent, pid, 0,
            WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

        self->OnOpened();

        wchar_t buf[256]{};
        ::GetWindowTextW(hWnd, buf, static_cast<int>(std::size(buf)) - 1);
        const std::string utf8 = WideToUtf8(buf);
        self->OnRawTitle(utf8);
        ApplyTitle(utf8);
    } else if (wasOpen && !nowOpen) {
        if (hookTitle) {
            ::UnhookWinEvent(hookTitle);
            hookTitle = nullptr;
        }
        ResetFragments();
        self->OnClosed();
    }
}

// ------------------------------------------------------------------------
// Aggregator — merges SMTC + Audio + Title fragments into unified state.
// Precedence for artist/title/album/position/duration/art/caps:  SMTC > Title
// Audio layer owns: audible, appVolume, appMuted.
// Title layer owns: isAd detection; provides fallback artist/title + Paused
// hint when SMTC is silent.
// ------------------------------------------------------------------------

void SpotifyClient::Impl::ApplySmtc(const PlaybackState& frag) {
    const auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard lock(fragMu);
        // Advance the smoothing anchor only on real discontinuities. In steady
        // Playing state, each SMTC republish carries the current position with
        // some async delay, and re-anchoring on every update produces a visible
        // step each tick. UpdateAnchor keeps the previous anchor whenever the
        // new SMTC observation agrees with our running extrapolation within
        // kDefaultAnchorSnapThreshold, so OnPositionChanged ticks smoothly.
        smtcAnchor = UpdateAnchor(
            smtcAnchor, smtcFrag.status, frag.status,
            frag.position, frag.duration, now);
        smtcFrag = frag;
    }
    Aggregate();
}

void SpotifyClient::Impl::ApplyAudio(float vol, bool muted, bool audible, bool resolved) {
    {
        std::lock_guard lock(fragMu);
        audioFrag.vol      = resolved ? vol : -1.0f;
        audioFrag.muted    = muted;
        audioFrag.audible  = audible;
        audioFrag.resolved = resolved;
    }
    Aggregate();
}

void SpotifyClient::Impl::ApplyTitle(std::string_view rawTitle) {
    TitleParse p = ParseWindowTitle(rawTitle);
    {
        std::lock_guard lock(fragMu);
        titleFrag.idle   = p.idle;
        titleFrag.isAd   = p.isAd;
        titleFrag.artist = std::move(p.artist);
        titleFrag.title  = std::move(p.title);
        titleFrag.status = p.status;
        titleFrag.any    = true;
    }
    Aggregate();
}

void SpotifyClient::Impl::ResetFragments() {
    std::lock_guard lock(fragMu);
    smtcFrag = PlaybackState{};
    smtcAnchor = {};  // invalidate; LatestPositionSmooth will return 0
    audioFrag = {};
    titleFrag = {};
}

void SpotifyClient::Impl::Aggregate() {
    PlaybackState out;
    PositionAnchor anchor;
    {
        std::lock_guard lock(fragMu);
        out = FuseFragments(smtcFrag, audioFrag, titleFrag);
        anchor = smtcAnchor;
    }

    PlaybackState prev;
    {
        std::lock_guard lock(stateMu);
        if (latest == out) return;
        prev = latest;
        latest = out;
        publishedAnchor = anchor;
    }

    // Edge signals fire BEFORE OnStateChanged, so consumers wiring both don't
    // have to filter OnStateChanged for "what actually changed."
    if (IsTrackChange(prev, out)) self->OnTrackChanged(prev, out);
    if (!prev.isAd &&  out.isAd) self->OnAdStarted();
    if ( prev.isAd && !out.isAd) self->OnAdEnded();

    self->OnStateChanged(out);
}

void SpotifyClient::Impl::MaybeFirePositionTick() {
    if (self->OnPositionChanged.size() == 0) return;

    PlaybackState::Status status;
    std::chrono::milliseconds duration;
    PositionAnchor anchor;
    {
        std::lock_guard lock(stateMu);
        status   = latest.status;
        duration = latest.duration;
        anchor   = publishedAnchor;
    }
    // Gate on Playing AND a valid anchor — a title-derived "Playing" with no
    // SMTC anchor has no reliable position to tick on.
    if (status != PlaybackState::Status::Playing || !anchor.valid) return;

    self->OnPositionChanged(
        SmoothPosition(anchor, status, duration, std::chrono::steady_clock::now()));
}

// ------------------------------------------------------------------------
// Impl ctor/dtor (out-of-line so unique_ptr<SmtcSource> destroys correctly)
// ------------------------------------------------------------------------

SpotifyClient::Impl::Impl() = default;
SpotifyClient::Impl::~Impl() = default;

// ------------------------------------------------------------------------
// SpotifyClient
// ------------------------------------------------------------------------

SpotifyClient::SpotifyClient() : m_(std::make_unique<Impl>()) {
    m_->self = this;
}

SpotifyClient::~SpotifyClient() {
    Stop();
}

void SpotifyClient::Start() {
    if (m_->hooksInstalled.exchange(true)) return;

    RegisterImpl(m_.get());

    m_->hookWindow = ::SetWinEventHook(
        EVENT_OBJECT_CREATE, EVENT_OBJECT_DESTROY, nullptr,
        &Impl::OnWindowEvent, 0, 0,
        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS | WINEVENT_SKIPOWNTHREAD);

    // Seed with any already-running Spotify window.
    HWND hWnd = ::FindWindowW(L"Chrome_WidgetWin_0", nullptr);
    while (hWnd) {
        if (IsSpotifyWindow(hWnd)) {
            m_->SetWindow(hWnd);
            break;
        }
        hWnd = ::FindWindowExW(nullptr, hWnd, L"Chrome_WidgetWin_0", nullptr);
    }

    m_->smtc = std::make_unique<SmtcSource>(m_.get());
    m_->smtc->Start();

    m_->audio = std::make_unique<AudioSession>(m_.get());
    m_->audio->Start();
}

void SpotifyClient::Stop() {
    if (!m_->hooksInstalled.exchange(false)) return;

    if (m_->audio) {
        m_->audio->Stop();
        m_->audio.reset();
    }

    if (m_->smtc) {
        m_->smtc->Stop();
        m_->smtc.reset();
    }

    if (m_->hookTitle)  { ::UnhookWinEvent(m_->hookTitle);  m_->hookTitle = nullptr; }
    if (m_->hookWindow) { ::UnhookWinEvent(m_->hookWindow); m_->hookWindow = nullptr; }

    UnregisterImpl(m_.get());

    m_->window.store(nullptr);
    m_->processId.store(0);
}

bool SpotifyClient::IsRunning() const {
    return m_->window.load() != nullptr;
}

PlaybackState SpotifyClient::LatestState() const {
    std::lock_guard lock(m_->stateMu);
    return m_->latest;
}

std::chrono::milliseconds SpotifyClient::LatestPositionSmooth() const {
    PlaybackState::Status status;
    std::chrono::milliseconds duration;
    std::chrono::milliseconds rawPosition;
    PositionAnchor anchor;
    {
        std::lock_guard lock(m_->stateMu);
        status      = m_->latest.status;
        duration    = m_->latest.duration;
        rawPosition = m_->latest.position;
        anchor      = m_->publishedAnchor;
    }
    // No SMTC anchor yet (fresh client, or state derived from title only).
    // Fall back to the raw latest.position — which is 0 when SMTC is silent —
    // rather than extrapolating from a zero-initialized time_point.
    if (!anchor.valid) return rawPosition;

    return SmoothPosition(anchor, status, duration,
                          std::chrono::steady_clock::now());
}

bool SpotifyClient::SendCommand(AppCommand cmd) {
    static constexpr unsigned kTable[] = {
        APPCOMMAND_MEDIA_STOP,
        APPCOMMAND_MEDIA_PLAY,
        APPCOMMAND_MEDIA_PAUSE,
        APPCOMMAND_MEDIA_PLAY_PAUSE,
        APPCOMMAND_MEDIA_NEXTTRACK,
        APPCOMMAND_MEDIA_PREVIOUSTRACK,
        APPCOMMAND_VOLUME_UP,
        APPCOMMAND_VOLUME_DOWN,
        APPCOMMAND_VOLUME_MUTE,
    };
    const HWND hWnd = m_->window.load();
    if (!hWnd) return false;
    const unsigned code = kTable[static_cast<unsigned>(cmd)];
    return ::PostMessageW(hWnd, WM_APPCOMMAND, FAPPCOMMAND_KEY,
                          MAKELONG(0, code)) != 0;
}

bool SpotifyClient::OpenUri(std::string_view spotifyUri) {
    const std::wstring wide = Utf8ToWide(spotifyUri);
    HINSTANCE r = ::ShellExecuteW(m_->window.load(), L"open", wide.c_str(),
                                  nullptr, nullptr, SW_NORMAL);
    return reinterpret_cast<INT_PTR>(r) > 32;
}

bool SpotifyClient::SendKey(unsigned virtualKey) {
    const HWND hWnd = m_->window.load();
    if (!hWnd) return false;
    return ::PostMessageW(hWnd, WM_KEYDOWN,
                          static_cast<WPARAM>(virtualKey), 0) != 0;
}

// Transport controls prefer SMTC (more reliable, supports Seek) and fall
// back to media keys when no SMTC session is available.

bool SpotifyClient::Play() {
    if (m_->smtc && m_->smtc->HasSession() && m_->smtc->Play()) return true;
    return SendCommand(AppCommand::Play);
}
bool SpotifyClient::Pause() {
    if (m_->smtc && m_->smtc->HasSession() && m_->smtc->Pause()) return true;
    return SendCommand(AppCommand::Pause);
}
bool SpotifyClient::Next() {
    if (m_->smtc && m_->smtc->HasSession() && m_->smtc->Next()) return true;
    return SendCommand(AppCommand::NextTrack);
}
bool SpotifyClient::Previous() {
    if (m_->smtc && m_->smtc->HasSession() && m_->smtc->Previous()) return true;
    return SendCommand(AppCommand::PrevTrack);
}
bool SpotifyClient::Seek(std::chrono::milliseconds position) {
    if (m_->smtc && m_->smtc->HasSession()) return m_->smtc->Seek(position);
    return false;  // media keys can't seek
}

float SpotifyClient::GetAppVolume() const       { return m_->audio ? m_->audio->GetVolume()          : -1.0f; }
bool  SpotifyClient::SetAppVolume(float v)      { return m_->audio && m_->audio->SetVolume(v); }
bool  SpotifyClient::IsAppMuted() const         { return m_->audio && m_->audio->IsMuted(); }
bool  SpotifyClient::SetAppMuted(bool m)        { return m_->audio && m_->audio->SetMuted(m); }
float SpotifyClient::GetPeakAmplitude() const   { return m_->audio ? m_->audio->GetPeakAmplitude()    : -1.0f; }

}  // namespace spotify
