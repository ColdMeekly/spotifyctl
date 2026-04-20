// spotifyctl CLI — thin consumer of the libspotifyctl C ABI. Works against
// both the static archive and libspotifyctl.dll.

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifdef _WIN32
#  include <windows.h>
#  include <fcntl.h>
#  include <io.h>
#endif

#include "spotify/c_api.h"

namespace {

constexpr int kExitOk        = 0;
constexpr int kExitUsage     = 2;
constexpr int kExitNoSpotify = 3;
constexpr int kExitFailure   = 4;

struct GlobalArgs {
    int wait_ms = 1500;
};

const char* StatusText(spotifyctl_status s) {
    switch (s) {
        case SPOTIFYCTL_STATUS_STOPPED:        return "stopped";
        case SPOTIFYCTL_STATUS_PAUSED:         return "paused";
        case SPOTIFYCTL_STATUS_PLAYING:        return "playing";
        case SPOTIFYCTL_STATUS_CHANGING_TRACK: return "changing_track";
        case SPOTIFYCTL_STATUS_UNKNOWN:
        default:                               return "unknown";
    }
}

std::string FormatTime(int64_t ms) {
    if (ms < 0) ms = 0;
    int64_t total_sec = ms / 1000;
    int64_t m = total_sec / 60;
    int64_t s = total_sec % 60;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%lld:%02lld",
                  static_cast<long long>(m), static_cast<long long>(s));
    return buf;
}

bool WaitForSpotify(spotifyctl_client* c, int wait_ms) {
    using namespace std::chrono;
    auto deadline = steady_clock::now() + milliseconds(wait_ms);
    while (steady_clock::now() < deadline) {
        if (spotifyctl_is_running(c)) return true;
        std::this_thread::sleep_for(milliseconds(25));
    }
    return spotifyctl_is_running(c) != 0;
}

// ---------------------------------------------------------------------------
// now-playing
// ---------------------------------------------------------------------------

void PrintStateHuman(const spotifyctl_playback_state& s) {
    std::printf("Status:   %s\n", StatusText(s.status));
    std::printf("Artist:   %s\n", s.artist);
    std::printf("Title:    %s\n", s.title);
    std::printf("Album:    %s\n", s.album);
    std::printf("Position: %s / %s\n",
                FormatTime(s.position_ms).c_str(),
                FormatTime(s.duration_ms).c_str());
    std::printf("Audible:  %s\n", s.audible ? "yes" : "no");
    if (s.app_volume < 0.0f) {
        std::printf("Volume:   (unresolved)\n");
    } else {
        std::printf("Volume:   %.2f%s\n",
                    static_cast<double>(s.app_volume),
                    s.app_muted ? " (muted)" : "");
    }
}

void PrintStateTsv(const spotifyctl_playback_state& s) {
    std::printf("%s\t%s\t%s\t%s\t%lld\t%lld\t%d\t%.4f\n",
                StatusText(s.status),
                s.artist, s.title, s.album,
                static_cast<long long>(s.position_ms),
                static_cast<long long>(s.duration_ms),
                s.audible ? 1 : 0,
                static_cast<double>(s.app_volume));
}

void PrintStateJson(spotifyctl_client* c) {
    size_t need = spotifyctl_latest_state_json(c, nullptr, 0);
    std::string buf(need + 1, '\0');
    spotifyctl_latest_state_json(c, buf.data(), buf.size());
    std::fwrite(buf.data(), 1, need, stdout);
    std::fputc('\n', stdout);
}

int CmdVersion() {
    std::printf("%s\n", spotifyctl_version());
    return kExitOk;
}

int CmdNowPlaying(const std::vector<std::string_view>& args, const GlobalArgs& g) {
    bool want_json = false, want_tsv = false;
    for (auto a : args) {
        if      (a == "--json") want_json = true;
        else if (a == "--tsv")  want_tsv  = true;
        else {
            std::fprintf(stderr, "now-playing: unknown argument: %.*s\n",
                         static_cast<int>(a.size()), a.data());
            return kExitUsage;
        }
    }
    if (want_json && want_tsv) {
        std::fprintf(stderr, "now-playing: --json and --tsv are mutually exclusive\n");
        return kExitUsage;
    }

    spotifyctl_client* c = spotifyctl_new();
    if (!c) return kExitFailure;
    spotifyctl_start(c);
    WaitForSpotify(c, g.wait_ms);  // best-effort; still emit a skeleton snapshot

    if (want_json) {
        PrintStateJson(c);
    } else {
        spotifyctl_playback_state st{};
        spotifyctl_latest_state(c, &st);
        if (want_tsv) PrintStateTsv(st);
        else          PrintStateHuman(st);
    }
    spotifyctl_stop(c);
    spotifyctl_free(c);
    return kExitOk;
}

// ---------------------------------------------------------------------------
// watch — NDJSON stream until Ctrl-C
// ---------------------------------------------------------------------------

std::atomic<bool> g_stop{false};

void OnSigInt(int) { g_stop.store(true); }

void WatchEmit(spotifyctl_client* c) {
    size_t need = spotifyctl_latest_state_json(c, nullptr, 0);
    std::string buf(need + 1, '\0');
    spotifyctl_latest_state_json(c, buf.data(), buf.size());
    std::fwrite(buf.data(), 1, need, stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

void WatchCallback(const spotifyctl_playback_state* /*s*/, void* user) {
    WatchEmit(static_cast<spotifyctl_client*>(user));
}

int CmdWatch(const std::vector<std::string_view>& args, const GlobalArgs& /*g*/) {
    for (auto a : args) {
        if (a == "--json") continue;  // default; accepted for clarity
        std::fprintf(stderr, "watch: unknown argument: %.*s\n",
                     static_cast<int>(a.size()), a.data());
        return kExitUsage;
    }

    spotifyctl_client* c = spotifyctl_new();
    if (!c) return kExitFailure;
    spotifyctl_start(c);

    std::signal(SIGINT, OnSigInt);

    WatchEmit(c);  // baseline line so tail consumers have an initial state
    auto tok = spotifyctl_on_state_changed(c, WatchCallback, c);

    while (!g_stop.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    spotifyctl_disconnect(c, tok);
    spotifyctl_stop(c);
    spotifyctl_free(c);
    return kExitOk;
}

// ---------------------------------------------------------------------------
// events — typed NDJSON stream per signal: track changes, ad edges, position
// ticks. Additive to `watch`; callers who want the old per-field stream keep
// using `watch`.
// ---------------------------------------------------------------------------

void EmitJsonLine(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::vprintf(fmt, ap);
    va_end(ap);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

// Minimal NDJSON field-escaping for artist/title/album. The library's own
// json_writer lives in src/ — we stay within the public ABI and inline this.
std::string JsonEscape(const char* s) {
    std::string out;
    if (!s) return out;
    for (const char* p = s; *p; ++p) {
        unsigned char c = static_cast<unsigned char>(*p);
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

void EmitTrackObj(const char* key, const spotifyctl_playback_state* s) {
    std::printf("\"%s\":{\"artist\":\"%s\",\"title\":\"%s\",\"album\":\"%s\",\"status\":\"%s\"}",
                key,
                JsonEscape(s->artist).c_str(),
                JsonEscape(s->title).c_str(),
                JsonEscape(s->album).c_str(),
                StatusText(s->status));
}

void OnTrackChangedCb(const spotifyctl_playback_state* prev,
                      const spotifyctl_playback_state* curr,
                      void* /*user*/) {
    std::printf("{\"type\":\"track_changed\",");
    EmitTrackObj("previous", prev);
    std::printf(",");
    EmitTrackObj("current", curr);
    std::printf("}\n");
    std::fflush(stdout);
}

void OnAdStartedCb(void* /*user*/) { EmitJsonLine("{\"type\":\"ad_started\"}"); }
void OnAdEndedCb(void* /*user*/)   { EmitJsonLine("{\"type\":\"ad_ended\"}"); }

void OnPositionChangedCb(int64_t position_ms, void* /*user*/) {
    EmitJsonLine("{\"type\":\"position\",\"position_ms\":%lld}",
                 static_cast<long long>(position_ms));
}

int CmdEvents(const std::vector<std::string_view>& args, const GlobalArgs& /*g*/) {
    bool want_position = false;
    for (auto a : args) {
        if (a == "--position") { want_position = true; continue; }
        std::fprintf(stderr, "events: unknown argument: %.*s\n",
                     static_cast<int>(a.size()), a.data());
        return kExitUsage;
    }

    spotifyctl_client* c = spotifyctl_new();
    if (!c) return kExitFailure;
    spotifyctl_start(c);

    std::signal(SIGINT, OnSigInt);

    auto t1 = spotifyctl_on_track_changed(c, OnTrackChangedCb, c);
    auto t2 = spotifyctl_on_ad_started  (c, OnAdStartedCb,    c);
    auto t3 = spotifyctl_on_ad_ended    (c, OnAdEndedCb,      c);
    spotifyctl_token t4 = 0;
    if (want_position) {
        t4 = spotifyctl_on_position_changed(c, OnPositionChangedCb, c);
    }

    while (!g_stop.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (t4) spotifyctl_disconnect(c, t4);
    spotifyctl_disconnect(c, t3);
    spotifyctl_disconnect(c, t2);
    spotifyctl_disconnect(c, t1);
    spotifyctl_stop(c);
    spotifyctl_free(c);
    return kExitOk;
}

// ---------------------------------------------------------------------------
// Transport
// ---------------------------------------------------------------------------

int CmdTransport(std::string_view verb, const GlobalArgs& g) {
    spotifyctl_client* c = spotifyctl_new();
    if (!c) return kExitFailure;
    spotifyctl_start(c);
    if (!WaitForSpotify(c, g.wait_ms)) {
        spotifyctl_free(c);
        std::fprintf(stderr, "spotifyctl: Spotify not detected\n");
        return kExitNoSpotify;
    }
    int ok = 0;
    if      (verb == "play")   ok = spotifyctl_play(c);
    else if (verb == "pause")  ok = spotifyctl_pause(c);
    else if (verb == "toggle") ok = spotifyctl_send_command(c, SPOTIFYCTL_CMD_PLAY_PAUSE);
    else if (verb == "next")   ok = spotifyctl_next(c);
    else if (verb == "prev")   ok = spotifyctl_previous(c);
    spotifyctl_free(c);
    return ok ? kExitOk : kExitFailure;
}

// ---------------------------------------------------------------------------
// seek — parses "90", "90s", "500ms", "1:23", "1:23.5", optional +/- prefix
// ---------------------------------------------------------------------------

std::optional<int64_t> ParseAbsDurationMs(std::string_view s) {
    if (s.empty()) return std::nullopt;

    auto colon = s.find(':');
    if (colon != std::string_view::npos) {
        std::string mm(s.substr(0, colon));
        std::string rest(s.substr(colon + 1));
        try {
            size_t p = 0;
            long long m = std::stoll(mm, &p);
            if (p != mm.size() || m < 0) return std::nullopt;
            size_t q = 0;
            double sec = std::stod(rest, &q);
            if (q != rest.size() || sec < 0) return std::nullopt;
            return static_cast<int64_t>(m * 60'000 + sec * 1000.0);
        } catch (...) { return std::nullopt; }
    }

    std::string num;
    int mult_ms = 1000;  // bare numbers are seconds
    if (s.size() >= 2 && s.substr(s.size() - 2) == "ms") {
        num   = std::string(s.substr(0, s.size() - 2));
        mult_ms = 1;
    } else if (s.back() == 's') {
        num   = std::string(s.substr(0, s.size() - 1));
        mult_ms = 1000;
    } else {
        num = std::string(s);
    }
    try {
        size_t p = 0;
        double v = std::stod(num, &p);
        if (p != num.size() || v < 0) return std::nullopt;
        return static_cast<int64_t>(v * mult_ms);
    } catch (...) { return std::nullopt; }
}

int CmdSeek(const std::vector<std::string_view>& args, const GlobalArgs& g) {
    if (args.size() != 1) {
        std::fprintf(stderr, "seek: expected TIME (90, 90s, 500ms, 1:23, +30s, -15s)\n");
        return kExitUsage;
    }
    auto spec = args[0];
    bool relative = false;
    int sign = 1;
    if (!spec.empty() && (spec[0] == '+' || spec[0] == '-')) {
        relative = true;
        if (spec[0] == '-') sign = -1;
        spec = spec.substr(1);
    }
    auto parsed = ParseAbsDurationMs(spec);
    if (!parsed) {
        std::fprintf(stderr, "seek: invalid time format: %.*s\n",
                     static_cast<int>(args[0].size()), args[0].data());
        return kExitUsage;
    }

    spotifyctl_client* c = spotifyctl_new();
    if (!c) return kExitFailure;
    spotifyctl_start(c);
    if (!WaitForSpotify(c, g.wait_ms)) {
        spotifyctl_free(c);
        std::fprintf(stderr, "spotifyctl: Spotify not detected\n");
        return kExitNoSpotify;
    }

    int64_t target;
    if (relative) {
        spotifyctl_playback_state st{};
        spotifyctl_latest_state(c, &st);
        target = st.position_ms + static_cast<int64_t>(sign) * (*parsed);
        if (target < 0) target = 0;
    } else {
        target = *parsed;
    }
    int ok = spotifyctl_seek_ms(c, target);
    spotifyctl_free(c);
    return ok ? kExitOk : kExitFailure;
}

// ---------------------------------------------------------------------------
// volume / mute
// ---------------------------------------------------------------------------

int CmdVolume(const std::vector<std::string_view>& args, const GlobalArgs& g) {
    if (args.empty()) {
        std::fprintf(stderr, "volume: expected get | set VALUE | up | down\n");
        return kExitUsage;
    }
    spotifyctl_client* c = spotifyctl_new();
    if (!c) return kExitFailure;
    spotifyctl_start(c);
    if (!WaitForSpotify(c, g.wait_ms)) {
        spotifyctl_free(c);
        std::fprintf(stderr, "spotifyctl: Spotify not detected\n");
        return kExitNoSpotify;
    }

    auto sub = args[0];
    int rc = kExitOk;
    if (sub == "get") {
        float v = spotifyctl_get_app_volume(c);
        if (v < 0.0f) std::printf("unresolved\n");
        else          std::printf("%.4f\n", static_cast<double>(v));
    } else if (sub == "set") {
        if (args.size() != 2) {
            std::fprintf(stderr, "volume set: expected VALUE in [0,1]\n");
            rc = kExitUsage;
        } else {
            try {
                double v = std::stod(std::string(args[1]));
                if (!spotifyctl_set_app_volume(c, static_cast<float>(v))) rc = kExitFailure;
            } catch (...) {
                std::fprintf(stderr, "volume set: invalid value\n");
                rc = kExitUsage;
            }
        }
    } else if (sub == "up") {
        if (!spotifyctl_send_command(c, SPOTIFYCTL_CMD_VOL_UP))   rc = kExitFailure;
    } else if (sub == "down") {
        if (!spotifyctl_send_command(c, SPOTIFYCTL_CMD_VOL_DOWN)) rc = kExitFailure;
    } else {
        std::fprintf(stderr, "volume: unknown subcommand: %.*s\n",
                     static_cast<int>(sub.size()), sub.data());
        rc = kExitUsage;
    }
    spotifyctl_free(c);
    return rc;
}

int CmdMute(const std::vector<std::string_view>& args, const GlobalArgs& g) {
    if (args.size() != 1) {
        std::fprintf(stderr, "mute: expected on | off | toggle\n");
        return kExitUsage;
    }
    spotifyctl_client* c = spotifyctl_new();
    if (!c) return kExitFailure;
    spotifyctl_start(c);
    if (!WaitForSpotify(c, g.wait_ms)) {
        spotifyctl_free(c);
        std::fprintf(stderr, "spotifyctl: Spotify not detected\n");
        return kExitNoSpotify;
    }

    auto sub = args[0];
    int ok = 0;
    if      (sub == "on")     ok = spotifyctl_set_app_muted(c, 1);
    else if (sub == "off")    ok = spotifyctl_set_app_muted(c, 0);
    else if (sub == "toggle") ok = spotifyctl_set_app_muted(c, spotifyctl_is_app_muted(c) ? 0 : 1);
    else {
        std::fprintf(stderr, "mute: unknown subcommand: %.*s\n",
                     static_cast<int>(sub.size()), sub.data());
        spotifyctl_free(c);
        return kExitUsage;
    }
    spotifyctl_free(c);
    return ok ? kExitOk : kExitFailure;
}

// ---------------------------------------------------------------------------
// url / search
// ---------------------------------------------------------------------------

int CmdUrl(const std::vector<std::string_view>& args, const GlobalArgs& /*g*/) {
    if (args.size() != 1) {
        std::fprintf(stderr, "url: expected a spotify: URI\n");
        return kExitUsage;
    }
    std::string uri(args[0]);
    spotifyctl_client* c = spotifyctl_new();
    if (!c) return kExitFailure;
    int ok = spotifyctl_open_uri(c, uri.c_str());
    spotifyctl_free(c);
    return ok ? kExitOk : kExitFailure;
}

int CmdSearch(const std::vector<std::string_view>& args, const GlobalArgs& /*g*/) {
    if (args.empty()) {
        std::fprintf(stderr, "search: expected a query\n");
        return kExitUsage;
    }
    std::string query;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) query.push_back(' ');
        query.append(args[i]);
    }
    char* uri = spotifyctl_uri_search(query.c_str());
    if (!uri) return kExitFailure;

    spotifyctl_client* c = spotifyctl_new();
    if (!c) { spotifyctl_free_string(uri); return kExitFailure; }
    int ok = spotifyctl_open_uri(c, uri);
    spotifyctl_free_string(uri);
    spotifyctl_free(c);
    return ok ? kExitOk : kExitFailure;
}

// ---------------------------------------------------------------------------
// usage
// ---------------------------------------------------------------------------

void PrintUsage(FILE* out) {
    std::fprintf(out,
        "usage: spotifyctl [--wait-ms N] COMMAND [ARGS]\n"
        "\n"
        "Commands:\n"
        "  version                     print library version\n"
        "  now-playing [--json|--tsv]  print the current state\n"
        "  watch                       stream NDJSON state deltas until Ctrl-C\n"
        "  events [--position]         stream typed NDJSON edge events until Ctrl-C\n"
        "  play | pause | toggle       transport\n"
        "  next | prev\n"
        "  seek TIME                   90, 90s, 500ms, 1:23, +30s, -15s\n"
        "  volume get | set X | up | down\n"
        "  mute on | off | toggle\n"
        "  url SPOTIFY_URI             launch a spotify: URI\n"
        "  search QUERY...             launch a search URI\n"
        "\n"
        "Exit codes: 0 ok, 2 usage, 3 Spotify not detected, 4 command failed.\n");
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    // UTF-8 stdout so non-ASCII track titles survive the trip to the console.
    SetConsoleOutputCP(CP_UTF8);
#endif

    GlobalArgs g;
    std::vector<std::string_view> positional;
    for (int i = 1; i < argc; ++i) {
        std::string_view a = argv[i];
        if (a == "--wait-ms") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "--wait-ms: missing value\n");
                return kExitUsage;
            }
            try {
                g.wait_ms = std::stoi(argv[i + 1]);
                ++i;
            } catch (...) {
                std::fprintf(stderr, "--wait-ms: invalid value: %s\n", argv[i + 1]);
                return kExitUsage;
            }
        } else if (a == "-h" || a == "--help" || a == "help") {
            PrintUsage(stdout);
            return kExitOk;
        } else {
            positional.push_back(a);
        }
    }

    if (positional.empty()) {
        PrintUsage(stderr);
        return kExitUsage;
    }

    auto cmd = positional.front();
    std::vector<std::string_view> rest(positional.begin() + 1, positional.end());

    if (cmd == "version")     return CmdVersion();
    if (cmd == "now-playing") return CmdNowPlaying(rest, g);
    if (cmd == "watch")       return CmdWatch(rest, g);
    if (cmd == "events")      return CmdEvents(rest, g);
    if (cmd == "play" || cmd == "pause" || cmd == "toggle" ||
        cmd == "next" || cmd == "prev") return CmdTransport(cmd, g);
    if (cmd == "seek")        return CmdSeek(rest, g);
    if (cmd == "volume")      return CmdVolume(rest, g);
    if (cmd == "mute")        return CmdMute(rest, g);
    if (cmd == "url")         return CmdUrl(rest, g);
    if (cmd == "search")      return CmdSearch(rest, g);

    std::fprintf(stderr, "unknown command: %.*s\n",
                 static_cast<int>(cmd.size()), cmd.data());
    PrintUsage(stderr);
    return kExitUsage;
}
