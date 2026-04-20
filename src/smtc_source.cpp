#include "smtc_source.h"

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>

#include <algorithm>
#include <atomic>
#include <cwchar>
#include <mutex>
#include <string>

namespace spotify {

namespace wfc = winrt::Windows::Foundation::Collections;
namespace wmc = winrt::Windows::Media::Control;
namespace wss = winrt::Windows::Storage::Streams;
using winrt::Windows::Foundation::IInspectable;

namespace {

constexpr wchar_t kDesktopAumid[] = L"Spotify.exe";
constexpr wchar_t kUwpAumidPrefix[] = L"SpotifyAB.SpotifyMusic_";

bool AumidIsSpotify(const winrt::hstring& aumid) {
    const std::wstring_view sv{aumid};
    if (sv == kDesktopAumid) return true;
    if (sv.size() >= std::size(kUwpAumidPrefix) - 1 &&
        sv.substr(0, std::size(kUwpAumidPrefix) - 1) == kUwpAumidPrefix) {
        return true;
    }
    return false;
}

std::string HStringToUtf8(const winrt::hstring& h) {
    return WideToUtf8(std::wstring_view{h});
}

PlaybackState::Status MapStatus(wmc::GlobalSystemMediaTransportControlsSessionPlaybackStatus s) {
    using S = wmc::GlobalSystemMediaTransportControlsSessionPlaybackStatus;
    switch (s) {
        case S::Playing:        return PlaybackState::Status::Playing;
        case S::Paused:         return PlaybackState::Status::Paused;
        case S::Stopped:        return PlaybackState::Status::Stopped;
        case S::Changing:       return PlaybackState::Status::ChangingTrack;
        case S::Closed:
        case S::Opened:
        default:                return PlaybackState::Status::Unknown;
    }
}

std::vector<std::byte> ReadStreamBytes(const wss::IRandomAccessStreamReference& ref) {
    if (!ref) return {};
    try {
        wss::IRandomAccessStreamWithContentType s = ref.OpenReadAsync().get();
        const auto size = s.Size();
        if (size == 0 || size > (16u * 1024u * 1024u)) return {};
        wss::Buffer buf{static_cast<uint32_t>(size)};
        s.ReadAsync(buf, static_cast<uint32_t>(size), wss::InputStreamOptions::None).get();
        auto reader = wss::DataReader::FromBuffer(buf);
        std::vector<std::byte> out(static_cast<std::size_t>(buf.Length()));
        reader.ReadBytes(winrt::array_view<uint8_t>(
            reinterpret_cast<uint8_t*>(out.data()),
            reinterpret_cast<uint8_t*>(out.data() + out.size())));
        return out;
    } catch (...) {
        return {};
    }
}

}  // namespace

// ------------------------------------------------------------------------
// Priv
// ------------------------------------------------------------------------

struct SmtcSource::Priv {
    SpotifyClient::Impl* impl = nullptr;

    std::atomic<bool> running{false};
    std::atomic<bool> apartmentInitialized{false};

    wmc::GlobalSystemMediaTransportControlsSessionManager mgr{nullptr};
    wmc::GlobalSystemMediaTransportControlsSession session{nullptr};

    winrt::event_token sessionsChangedTok{};
    winrt::event_token currentChangedTok{};
    winrt::event_token mediaPropsTok{};
    winrt::event_token playbackInfoTok{};
    winrt::event_token timelineTok{};

    mutable std::mutex stateMu;
    PlaybackState smtcState;  // our running view; merged by PublishMerged

    void Setup();
    void Teardown();
    void RebindSession();
    void UnsubscribeSession();
    void SubscribeSession();
    void RefreshAll();

    bool SessionReady() const { return static_cast<bool>(session); }
};

void SmtcSource::Priv::Setup() {
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        apartmentInitialized = true;
    } catch (const winrt::hresult_error& e) {
        // RPC_E_CHANGED_MODE if STA already; coexist without asserting mode.
        if (e.code() != static_cast<winrt::hresult>(0x80010106L)) throw;
    }

    mgr = wmc::GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();

    sessionsChangedTok = mgr.SessionsChanged(
        [this](const auto& /*sender*/, const auto& /*args*/) { RebindSession(); });
    currentChangedTok = mgr.CurrentSessionChanged(
        [this](const auto& /*sender*/, const auto& /*args*/) { RebindSession(); });

    RebindSession();
}

void SmtcSource::Priv::Teardown() {
    UnsubscribeSession();
    if (mgr) {
        if (sessionsChangedTok) mgr.SessionsChanged(sessionsChangedTok);
        if (currentChangedTok)  mgr.CurrentSessionChanged(currentChangedTok);
        sessionsChangedTok = {};
        currentChangedTok = {};
    }
    session = nullptr;
    mgr = nullptr;
}

void SmtcSource::Priv::UnsubscribeSession() {
    if (!session) return;
    if (mediaPropsTok)    session.MediaPropertiesChanged(mediaPropsTok);
    if (playbackInfoTok)  session.PlaybackInfoChanged(playbackInfoTok);
    if (timelineTok)      session.TimelinePropertiesChanged(timelineTok);
    mediaPropsTok = {};
    playbackInfoTok = {};
    timelineTok = {};
}

void SmtcSource::Priv::SubscribeSession() {
    if (!session) return;
    mediaPropsTok = session.MediaPropertiesChanged(
        [this](const auto&, const auto&) { RefreshAll(); });
    playbackInfoTok = session.PlaybackInfoChanged(
        [this](const auto&, const auto&) { RefreshAll(); });
    timelineTok = session.TimelinePropertiesChanged(
        [this](const auto&, const auto&) { RefreshAll(); });
}

void SmtcSource::Priv::RebindSession() {
    if (!mgr) return;

    wmc::GlobalSystemMediaTransportControlsSession found{nullptr};
    try {
        auto sessions = mgr.GetSessions();
        for (auto const& s : sessions) {
            if (AumidIsSpotify(s.SourceAppUserModelId())) {
                found = s;
                break;
            }
        }
    } catch (...) {
        found = nullptr;
    }

    if (found == session) return;

    UnsubscribeSession();
    session = found;
    SubscribeSession();

    if (session) {
        RefreshAll();
    } else {
        {
            std::lock_guard lock(stateMu);
            smtcState = PlaybackState{};
        }
        impl->ApplySmtc(PlaybackState{});
    }
}

void SmtcSource::Priv::RefreshAll() {
    wmc::GlobalSystemMediaTransportControlsSession s = session;  // snapshot
    if (!s) return;

    PlaybackState next;
    try {
        auto props = s.TryGetMediaPropertiesAsync().get();
        if (props) {
            next.title  = HStringToUtf8(props.Title());
            next.artist = HStringToUtf8(props.Artist());
            next.album  = HStringToUtf8(props.AlbumTitle());
            next.albumArt = ReadStreamBytes(props.Thumbnail());
            next.RecomputeAlbumArtHash();
        }
    } catch (...) {}

    try {
        auto info = s.GetPlaybackInfo();
        next.status = MapStatus(info.PlaybackStatus());
        if (auto controls = info.Controls()) {
            next.canSeek     = controls.IsPlaybackPositionEnabled();
            next.canSkipNext = controls.IsNextEnabled();
            next.canSkipPrev = controls.IsPreviousEnabled();
        }
    } catch (...) {}

    try {
        auto tl = s.GetTimelineProperties();
        next.position = std::chrono::duration_cast<std::chrono::milliseconds>(tl.Position());
        next.duration = std::chrono::duration_cast<std::chrono::milliseconds>(tl.EndTime() - tl.StartTime());
    } catch (...) {}

    {
        std::lock_guard lock(stateMu);
        smtcState = next;
    }
    impl->ApplySmtc(next);
}

// ------------------------------------------------------------------------
// SmtcSource
// ------------------------------------------------------------------------

SmtcSource::SmtcSource(SpotifyClient::Impl* impl) : p_(std::make_unique<Priv>()) {
    p_->impl = impl;
}

SmtcSource::~SmtcSource() {
    Stop();
}

void SmtcSource::Start() {
    if (p_->running.exchange(true)) return;
    try {
        p_->Setup();
    } catch (...) {
        p_->running = false;
        // SMTC unavailable (pre-1809, or some other error). Silently disable.
    }
}

void SmtcSource::Stop() {
    if (!p_->running.exchange(false)) return;
    try { p_->Teardown(); } catch (...) {}
}

bool SmtcSource::HasSession() const { return p_->SessionReady(); }

bool SmtcSource::Play()     { auto s = p_->session; if (!s) return false; try { s.TryPlayAsync();            } catch (...) { return false; } return true; }
bool SmtcSource::Pause()    { auto s = p_->session; if (!s) return false; try { s.TryPauseAsync();           } catch (...) { return false; } return true; }
bool SmtcSource::Next()     { auto s = p_->session; if (!s) return false; try { s.TrySkipNextAsync();        } catch (...) { return false; } return true; }
bool SmtcSource::Previous() { auto s = p_->session; if (!s) return false; try { s.TrySkipPreviousAsync();    } catch (...) { return false; } return true; }

bool SmtcSource::Seek(std::chrono::milliseconds pos) {
    auto s = p_->session;
    if (!s) return false;
    try {
        auto ticks = std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(pos);
        s.TryChangePlaybackPositionAsync(ticks.count());
    } catch (...) { return false; }
    return true;
}

}  // namespace spotify
