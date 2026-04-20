// Live console dashboard for spotify::SpotifyClient. Exercises every public
// signal and every control the library exposes.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <conio.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <deque>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

#include "spotify/client.h"
#include "spotify/control.h"

namespace {

constexpr int kProgressWidth = 40;
constexpr int kMeterWidth    = 20;
constexpr int kLogRows       = 6;

const char* StatusName(spotify::PlaybackState::Status s) {
    using S = spotify::PlaybackState::Status;
    switch (s) {
        case S::Playing:       return "playing";
        case S::Paused:        return "paused";
        case S::Stopped:       return "stopped";
        case S::ChangingTrack: return "changing";
        default:               return "unknown";
    }
}

std::string FormatMs(std::chrono::milliseconds ms) {
    long long total = ms.count() / 1000;
    if (total < 0) total = 0;
    long long m = total / 60;
    long long s = total % 60;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%lld:%02lld", m, s);
    return buf;
}

std::string Bar(float ratio, int width) {
    if (ratio < 0.0f) return std::string(width, '.');  // "not resolved yet"
    ratio = std::clamp(ratio, 0.0f, 1.0f);
    int filled = static_cast<int>(ratio * width + 0.5f);
    std::string out;
    out.reserve(width);
    for (int i = 0; i < width; ++i) out += (i < filled) ? '#' : '-';
    return out;
}

std::string Truncate(std::string s, std::size_t width) {
    if (s.size() > width) { s.resize(width - 1); s += '~'; }
    return s;
}

void EnableVTMode() {
    HANDLE h = ::GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (::GetConsoleMode(h, &mode)) {
        ::SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
    ::SetConsoleOutputCP(65001);
}

void PumpMessages() {
    MSG msg;
    while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
}

class LogRing {
 public:
    void push(std::string msg) {
        std::lock_guard<std::mutex> lk(mu_);
        if (msgs_.size() == kCap) msgs_.pop_front();
        msgs_.push_back(std::move(msg));
    }
    std::vector<std::string> snapshot() const {
        std::lock_guard<std::mutex> lk(mu_);
        return {msgs_.begin(), msgs_.end()};
    }
 private:
    static constexpr std::size_t kCap = 64;
    mutable std::mutex mu_;
    std::deque<std::string> msgs_;
};

void ModalSearch(spotify::SpotifyClient& client, LogRing& log) {
    std::fputs("\x1b[?25h\x1b[2J\x1b[H", stdout);
    std::fputs("Enter Spotify search query (empty = cancel):\n> ", stdout);
    std::fflush(stdout);
    std::string q;
    std::getline(std::cin, q);
    std::fputs("\x1b[?25l\x1b[2J\x1b[H", stdout);
    if (q.empty()) { log.push("[ctrl] search cancelled"); return; }
    bool ok = client.OpenUri(spotify::uri::Search(q));
    log.push(std::string("[ctrl] search '") + q + "' " + (ok ? "ok" : "fail"));
}

}  // namespace

int main() {
    EnableVTMode();

    spotify::SpotifyClient client;
    LogRing log;

    std::mutex artMu;
    std::vector<std::byte> artBuf;
    std::atomic<std::size_t> artBytes{0};

    client.OnOpened.connect([&] { log.push("[spotify] window opened"); });
    client.OnClosed.connect([&] { log.push("[spotify] window closed"); });
    client.OnAudibleChanged.connect([&](bool audible) {
        log.push(std::string("[audio] audible=") + (audible ? "yes" : "no"));
    });
    client.OnRawTitle.connect([&](const std::string& t) {
        log.push("[title] " + t);
    });
    client.OnStateChanged.connect([&](const spotify::PlaybackState& s) {
        if (!s.albumArt.empty()) {
            std::lock_guard<std::mutex> lk(artMu);
            artBuf = s.albumArt;
            artBytes = s.albumArt.size();
        }
    });
    client.OnTrackChanged.connect([&](const spotify::PlaybackState& /*prev*/,
                                      const spotify::PlaybackState& curr) {
        if (!curr.title.empty()) {
            log.push("[track] " + curr.artist + " \xe2\x80\x94 " + curr.title);
        }
    });
    client.OnAdStarted.connect([&] { log.push("[ad] started"); });
    client.OnAdEnded  .connect([&] { log.push("[ad] ended"); });

    client.Start();
    log.push("[app] started; press ? for help, Q to quit");

    std::fputs("\x1b[?25l\x1b[2J", stdout);

    bool running = true;
    auto nextTick = std::chrono::steady_clock::now();

    auto bumpVolume = [&](float delta) {
        float v = client.GetAppVolume();
        if (v < 0) v = 0.5f;
        v = std::clamp(v + delta, 0.0f, 1.0f);
        bool ok = client.SetAppVolume(v);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "[ctrl] volume=%.0f%% %s",
                      v * 100.0f, ok ? "ok" : "fail");
        log.push(buf);
    };

    auto seekBy = [&](std::chrono::milliseconds delta) {
        auto st = client.LatestState();
        auto tgt = st.position + delta;
        if (tgt < std::chrono::milliseconds(0)) tgt = std::chrono::milliseconds(0);
        if (st.duration.count() > 0 && tgt > st.duration) tgt = st.duration;
        bool ok = client.Seek(tgt);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "[ctrl] seek %+llds -> %s %s",
                      static_cast<long long>(delta.count() / 1000),
                      FormatMs(tgt).c_str(), ok ? "ok" : "fail");
        log.push(buf);
    };

    while (running) {
        PumpMessages();

        while (_kbhit()) {
            int c = _getch();
            if (c == 0 || c == 0xE0) {
                int ext = _getch();
                switch (ext) {
                    case 72: bumpVolume(+0.05f); break;   // up
                    case 80: bumpVolume(-0.05f); break;   // down
                    case 75: seekBy(std::chrono::seconds(-10)); break;  // left
                    case 77: seekBy(std::chrono::seconds(+10)); break;  // right
                    default: break;
                }
                continue;
            }

            switch (c) {
                case 'q': case 'Q': case 27: running = false; break;
                case ' ': case 'p': case 'P': {
                    auto st = client.LatestState();
                    bool playing = st.status == spotify::PlaybackState::Status::Playing;
                    bool ok = playing ? client.Pause() : client.Play();
                    log.push(std::string("[ctrl] ") + (playing ? "pause " : "play ")
                             + (ok ? "ok" : "fail"));
                } break;
                case 'n': case 'N':
                    log.push(std::string("[ctrl] next ") + (client.Next() ? "ok" : "fail"));
                    break;
                case 'b': case 'B':
                    log.push(std::string("[ctrl] prev ") + (client.Previous() ? "ok" : "fail"));
                    break;
                case 'm': case 'M': {
                    bool muted = client.IsAppMuted();
                    bool ok = client.SetAppMuted(!muted);
                    log.push(std::string("[ctrl] per-app mute ")
                             + (ok ? (muted ? "off" : "on") : "fail"));
                } break;
                case 'k': case 'K': {
                    bool ok = client.SendCommand(spotify::AppCommand::MuteUnmute);
                    log.push(std::string("[ctrl] media-key mute toggle ")
                             + (ok ? "ok" : "fail"));
                } break;
                case '+': case '=': bumpVolume(+0.01f); break;
                case '-': case '_': bumpVolume(-0.01f); break;
                case '0': case '1': case '2': case '3': case '4':
                case '5': case '6': case '7': case '8': case '9': {
                    int pct = (c - '0') * 10;
                    auto st = client.LatestState();
                    if (st.duration.count() > 0) {
                        auto tgt = std::chrono::milliseconds(
                            st.duration.count() * pct / 100);
                        bool ok = client.Seek(tgt);
                        char buf[64];
                        std::snprintf(buf, sizeof(buf), "[ctrl] seek %d%% -> %s %s",
                                      pct, FormatMs(tgt).c_str(), ok ? "ok" : "fail");
                        log.push(buf);
                    } else {
                        log.push("[ctrl] seek: no duration yet");
                    }
                } break;
                case 'i': case 'I': {
                    std::lock_guard<std::mutex> lk(artMu);
                    if (artBuf.empty()) { log.push("[art] no art received yet"); break; }
                    std::ofstream f("cover.jpg", std::ios::binary);
                    f.write(reinterpret_cast<const char*>(artBuf.data()),
                            static_cast<std::streamsize>(artBuf.size()));
                    char buf[96];
                    std::snprintf(buf, sizeof(buf), "[art] wrote cover.jpg (%zu bytes)",
                                  artBuf.size());
                    log.push(buf);
                } break;
                case 's': case 'S':
                    ModalSearch(client, log);
                    break;
                case 'r': case 'R': {
                    // Launch Spotify's own Discover Weekly via URI as a URI demo.
                    bool ok = client.OpenUri("spotify:playlist:37i9dQZEVXcJZyENOWUFo7");
                    log.push(std::string("[ctrl] open featured playlist ")
                             + (ok ? "ok" : "fail"));
                } break;
                default: break;
            }
        }

        // ----- render -----
        auto st = client.LatestState();
        float peak = client.GetPeakAmplitude();

        st.position = client.LatestPositionSmooth();

        std::fputs("\x1b[H", stdout);

        auto line = [](const std::string& s) {
            std::fputs(s.c_str(), stdout);
            std::fputs("\x1b[K\n", stdout);
        };

        char buf[512];

        line("\x1b[1;32m=== libspotifyctl — now_playing ===\x1b[0m");
        std::snprintf(buf, sizeof(buf),
                      "  running=%s  status=\x1b[33m%s\x1b[0m  ad=%s  audible=%s",
                      client.IsRunning() ? "yes" : "no",
                      StatusName(st.status),
                      st.isAd ? "YES" : "no",
                      st.audible ? "yes" : "no");
        line(buf);
        line("");

        std::snprintf(buf, sizeof(buf), "  Artist : %s",
                      Truncate(st.artist, 70).c_str()); line(buf);
        std::snprintf(buf, sizeof(buf), "  Title  : %s",
                      Truncate(st.title, 70).c_str());  line(buf);
        std::snprintf(buf, sizeof(buf), "  Album  : %s",
                      Truncate(st.album, 70).c_str());  line(buf);
        line("");

        float posR = st.duration.count() > 0
                         ? static_cast<float>(st.position.count())
                           / static_cast<float>(st.duration.count())
                         : 0.0f;
        std::snprintf(buf, sizeof(buf), "  [%s] %s / %s",
                      Bar(posR, kProgressWidth).c_str(),
                      FormatMs(st.position).c_str(),
                      FormatMs(st.duration).c_str());
        line(buf);
        line("");

        std::string volPct = (st.appVolume >= 0)
            ? (std::to_string(static_cast<int>(st.appVolume * 100 + 0.5f)) + "%")
            : std::string("---");
        std::snprintf(buf, sizeof(buf), "  Volume : [%s] %-5s muted=%s",
                      Bar(st.appVolume, kMeterWidth).c_str(),
                      volPct.c_str(),
                      st.appMuted ? "yes" : "no");
        line(buf);
        std::string peakTxt = (peak < 0)
            ? std::string("---")
            : (std::to_string(static_cast<int>(peak * 100 + 0.5f)) + "%");
        std::snprintf(buf, sizeof(buf), "  Peak   : [%s] %-5s",
                      Bar(peak, kMeterWidth).c_str(),
                      peakTxt.c_str());
        line(buf);
        if (st.appVolume < 0) {
            line("\x1b[2m           (Core Audio session not resolved — play audio in Spotify"
                 " once to bind)\x1b[0m");
        } else {
            line("");
        }
        line("");

        std::snprintf(buf, sizeof(buf),
                      "  caps   : seek=%s next=%s prev=%s   art=%zu bytes",
                      st.canSeek     ? "yes" : "no",
                      st.canSkipNext ? "yes" : "no",
                      st.canSkipPrev ? "yes" : "no",
                      artBytes.load());
        line(buf);
        line("");

        line("\x1b[2m  Controls:\x1b[0m");
        line("\x1b[2m    Space/P play-pause   N next   B prev   M per-app mute   K media-key mute\x1b[0m");
        line("\x1b[2m    Up/Dn vol +/-5%      +/- vol +/-1%     Left/Right seek +/-10s\x1b[0m");
        line("\x1b[2m    0-9 seek to 0..90%   S open search:    R open sample playlist URI\x1b[0m");
        line("\x1b[2m    I save cover.jpg     Q/Esc quit\x1b[0m");
        line("");

        line("\x1b[36m  Log (most recent last):\x1b[0m");
        auto logs = log.snapshot();
        std::size_t start = logs.size() > kLogRows ? logs.size() - kLogRows : 0;
        std::size_t printed = 0;
        for (std::size_t i = start; i < logs.size(); ++i, ++printed) {
            line("    " + Truncate(logs[i], 90));
        }
        for (; printed < kLogRows; ++printed) line("");

        std::fputs("\x1b[J", stdout);  // clear anything below
        std::fflush(stdout);

        nextTick += std::chrono::milliseconds(33);
        auto now = std::chrono::steady_clock::now();
        if (nextTick < now) nextTick = now;
        ::MsgWaitForMultipleObjects(
            0, nullptr, FALSE,
            static_cast<DWORD>(std::chrono::duration_cast<std::chrono::milliseconds>(
                nextTick - now).count()),
            QS_ALLINPUT);
    }

    std::fputs("\x1b[?25h\n", stdout);
    client.Stop();
    return 0;
}
