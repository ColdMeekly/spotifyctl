// C ABI implementation — marshals the C++ SpotifyClient surface into a stable
// extern "C" interface for Python, Node, CLI, and any future FFI consumer.

#include "spotify/c_api.h"

#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "spotify/client.h"
#include "spotify/control.h"
#include "spotify/playback.h"
#include "spotify/version.h"
#include "json_writer.h"

namespace {

using spotify::AppCommand;
using spotify::PlaybackState;
using spotify::SpotifyClient;

spotifyctl_status ToCStatus(PlaybackState::Status s) {
    switch (s) {
        case PlaybackState::Status::Stopped:       return SPOTIFYCTL_STATUS_STOPPED;
        case PlaybackState::Status::Paused:        return SPOTIFYCTL_STATUS_PAUSED;
        case PlaybackState::Status::Playing:       return SPOTIFYCTL_STATUS_PLAYING;
        case PlaybackState::Status::ChangingTrack: return SPOTIFYCTL_STATUS_CHANGING_TRACK;
        case PlaybackState::Status::Unknown:
        default:                                   return SPOTIFYCTL_STATUS_UNKNOWN;
    }
}

AppCommand FromCCommand(spotifyctl_command cmd) {
    switch (cmd) {
        case SPOTIFYCTL_CMD_PLAY:       return AppCommand::Play;
        case SPOTIFYCTL_CMD_PAUSE:      return AppCommand::Pause;
        case SPOTIFYCTL_CMD_PLAY_PAUSE: return AppCommand::PlayPause;
        case SPOTIFYCTL_CMD_NEXT:       return AppCommand::NextTrack;
        case SPOTIFYCTL_CMD_PREV:       return AppCommand::PrevTrack;
        case SPOTIFYCTL_CMD_VOL_UP:     return AppCommand::VolUp;
        case SPOTIFYCTL_CMD_VOL_DOWN:   return AppCommand::VolDown;
        case SPOTIFYCTL_CMD_MUTE:       return AppCommand::MuteUnmute;
        case SPOTIFYCTL_CMD_STOP:
        default:                        return AppCommand::Stop;
    }
}

char* DupString(std::string_view s) {
    char* out = static_cast<char*>(std::malloc(s.size() + 1));
    if (!out) return nullptr;
    if (!s.empty()) std::memcpy(out, s.data(), s.size());
    out[s.size()] = '\0';
    return out;
}

}  // namespace

// Storage for the strings exposed through spotifyctl_playback_state. Kept
// alive on the client so returned const char* pointers stay valid until the
// next latest_state call or client destruction.
struct StateHolder {
    std::string artist;
    std::string title;
    std::string album;
    std::vector<std::uint8_t> albumArt;
};

// Opaque to the C layer. The C++ type lives in the anonymous-namespace-free
// scope only because the forward declaration in c_api.h uses extern "C".
struct spotifyctl_client {
    SpotifyClient core;
    std::mutex    holderMu;
    StateHolder   holder;

    // Token remap: external C tokens → internal per-signal Signal::Token +
    // a disconnect shim that knows which signal to hit.
    std::mutex nextMu;
    std::size_t nextToken = 0;

    struct ShimConn {
        std::function<void()> disconnect;
    };
    std::mutex conMu;
    std::unordered_map<std::size_t, ShimConn> conns;

    std::size_t reserveToken() {
        std::lock_guard<std::mutex> lk(nextMu);
        return ++nextToken;
    }

    void storeConn(std::size_t token, std::function<void()> disc) {
        std::lock_guard<std::mutex> lk(conMu);
        conns.emplace(token, ShimConn{std::move(disc)});
    }

    void removeConn(std::size_t token) {
        std::function<void()> disc;
        {
            std::lock_guard<std::mutex> lk(conMu);
            auto it = conns.find(token);
            if (it == conns.end()) return;
            disc = std::move(it->second.disconnect);
            conns.erase(it);
        }
        if (disc) disc();
    }
};

extern "C" {

// -------------------------------------------------------------------------
// Library-level
// -------------------------------------------------------------------------

SPOTIFYCTL_API const char* spotifyctl_version(void) {
    return LIBSPOTIFYCTL_VERSION_STRING;
}

// -------------------------------------------------------------------------
// Lifecycle
// -------------------------------------------------------------------------

SPOTIFYCTL_API spotifyctl_client* spotifyctl_new(void) {
    try {
        return new spotifyctl_client();
    } catch (...) {
        return nullptr;
    }
}

SPOTIFYCTL_API void spotifyctl_free(spotifyctl_client* c) {
    if (!c) return;
    // Disconnect any surviving callback shims before tearing down the client.
    {
        std::lock_guard<std::mutex> lk(c->conMu);
        for (auto& [_tok, shim] : c->conns) {
            if (shim.disconnect) shim.disconnect();
        }
        c->conns.clear();
    }
    delete c;
}

SPOTIFYCTL_API void spotifyctl_start(spotifyctl_client* c) {
    if (!c) return;
    c->core.Start();
}

SPOTIFYCTL_API void spotifyctl_stop(spotifyctl_client* c) {
    if (!c) return;
    c->core.Stop();
}

SPOTIFYCTL_API int spotifyctl_is_running(const spotifyctl_client* c) {
    if (!c) return 0;
    return c->core.IsRunning() ? 1 : 0;
}

// -------------------------------------------------------------------------
// Controls
// -------------------------------------------------------------------------

SPOTIFYCTL_API int spotifyctl_play(spotifyctl_client* c) {
    if (!c) return 0;
    return c->core.Play() ? 1 : 0;
}

SPOTIFYCTL_API int spotifyctl_pause(spotifyctl_client* c) {
    if (!c) return 0;
    return c->core.Pause() ? 1 : 0;
}

SPOTIFYCTL_API int spotifyctl_next(spotifyctl_client* c) {
    if (!c) return 0;
    return c->core.Next() ? 1 : 0;
}

SPOTIFYCTL_API int spotifyctl_previous(spotifyctl_client* c) {
    if (!c) return 0;
    return c->core.Previous() ? 1 : 0;
}

SPOTIFYCTL_API int spotifyctl_seek_ms(spotifyctl_client* c, int64_t position_ms) {
    if (!c) return 0;
    return c->core.Seek(std::chrono::milliseconds(position_ms)) ? 1 : 0;
}

SPOTIFYCTL_API int spotifyctl_send_command(spotifyctl_client* c, spotifyctl_command cmd) {
    if (!c) return 0;
    return c->core.SendCommand(FromCCommand(cmd)) ? 1 : 0;
}

SPOTIFYCTL_API int spotifyctl_open_uri(spotifyctl_client* c, const char* utf8_uri) {
    if (!c || !utf8_uri) return 0;
    return c->core.OpenUri(std::string_view(utf8_uri)) ? 1 : 0;
}

SPOTIFYCTL_API int spotifyctl_send_key(spotifyctl_client* c, unsigned int virtual_key) {
    if (!c) return 0;
    return c->core.SendKey(virtual_key) ? 1 : 0;
}

// -------------------------------------------------------------------------
// Per-app audio
// -------------------------------------------------------------------------

SPOTIFYCTL_API float spotifyctl_get_app_volume(const spotifyctl_client* c) {
    if (!c) return -1.0f;
    return c->core.GetAppVolume();
}

SPOTIFYCTL_API int spotifyctl_set_app_volume(spotifyctl_client* c, float volume) {
    if (!c) return 0;
    return c->core.SetAppVolume(volume) ? 1 : 0;
}

SPOTIFYCTL_API int spotifyctl_is_app_muted(const spotifyctl_client* c) {
    if (!c) return 0;
    return c->core.IsAppMuted() ? 1 : 0;
}

SPOTIFYCTL_API int spotifyctl_set_app_muted(spotifyctl_client* c, int muted) {
    if (!c) return 0;
    return c->core.SetAppMuted(muted != 0) ? 1 : 0;
}

SPOTIFYCTL_API float spotifyctl_get_peak_amplitude(const spotifyctl_client* c) {
    if (!c) return -1.0f;
    return c->core.GetPeakAmplitude();
}

// -------------------------------------------------------------------------
// State snapshot
// -------------------------------------------------------------------------

SPOTIFYCTL_API int spotifyctl_latest_state(spotifyctl_client* c,
                                           spotifyctl_playback_state* out) {
    if (!c || !out) return 0;
    PlaybackState s = c->core.LatestState();

    std::lock_guard<std::mutex> lk(c->holderMu);
    c->holder.artist = std::move(s.artist);
    c->holder.title  = std::move(s.title);
    c->holder.album  = std::move(s.album);

    c->holder.albumArt.resize(s.albumArt.size());
    for (std::size_t i = 0; i < s.albumArt.size(); ++i) {
        c->holder.albumArt[i] = static_cast<std::uint8_t>(s.albumArt[i]);
    }

    out->status        = ToCStatus(s.status);
    out->artist        = c->holder.artist.c_str();
    out->title         = c->holder.title.c_str();
    out->album         = c->holder.album.c_str();
    out->position_ms   = s.position.count();
    out->duration_ms   = s.duration.count();
    out->album_art     = c->holder.albumArt.empty() ? nullptr : c->holder.albumArt.data();
    out->album_art_len = c->holder.albumArt.size();
    out->can_seek      = s.canSeek      ? 1 : 0;
    out->can_skip_next = s.canSkipNext  ? 1 : 0;
    out->can_skip_prev = s.canSkipPrev  ? 1 : 0;
    out->is_ad         = s.isAd         ? 1 : 0;
    out->audible       = s.audible      ? 1 : 0;
    out->app_muted     = s.appMuted     ? 1 : 0;
    out->app_volume    = s.appVolume;
    return 1;
}

SPOTIFYCTL_API size_t spotifyctl_latest_state_json(spotifyctl_client* c,
                                                   char* buf, size_t cap) {
    if (!c) return 0;
    std::string json = spotify::json::WriteStateJson(c->core.LatestState());
    if (buf && cap > 0) {
        std::size_t n = json.size() < cap - 1 ? json.size() : cap - 1;
        std::memcpy(buf, json.data(), n);
        buf[n] = '\0';
    }
    return json.size();
}

// -------------------------------------------------------------------------
// Callbacks
// -------------------------------------------------------------------------

SPOTIFYCTL_API spotifyctl_token spotifyctl_on_state_changed(spotifyctl_client* c,
                                                            spotifyctl_state_cb cb,
                                                            void* user) {
    if (!c || !cb) return 0;
    auto core_tok = c->core.OnStateChanged.connect(
        [cb, user](const PlaybackState& s) {
            // Stack-local storage: pointers are valid for the callback duration only.
            std::string artist = s.artist;
            std::string title  = s.title;
            std::string album  = s.album;
            std::vector<std::uint8_t> art(s.albumArt.size());
            for (std::size_t i = 0; i < s.albumArt.size(); ++i) {
                art[i] = static_cast<std::uint8_t>(s.albumArt[i]);
            }

            spotifyctl_playback_state cs{};
            cs.status        = ToCStatus(s.status);
            cs.artist        = artist.c_str();
            cs.title         = title.c_str();
            cs.album         = album.c_str();
            cs.position_ms   = s.position.count();
            cs.duration_ms   = s.duration.count();
            cs.album_art     = art.empty() ? nullptr : art.data();
            cs.album_art_len = art.size();
            cs.can_seek      = s.canSeek      ? 1 : 0;
            cs.can_skip_next = s.canSkipNext  ? 1 : 0;
            cs.can_skip_prev = s.canSkipPrev  ? 1 : 0;
            cs.is_ad         = s.isAd         ? 1 : 0;
            cs.audible       = s.audible      ? 1 : 0;
            cs.app_muted     = s.appMuted     ? 1 : 0;
            cs.app_volume    = s.appVolume;

            cb(&cs, user);
        });

    auto ext = c->reserveToken();
    c->storeConn(ext, [c, core_tok]() { c->core.OnStateChanged.disconnect(core_tok); });
    return ext;
}

SPOTIFYCTL_API spotifyctl_token spotifyctl_on_audible_changed(spotifyctl_client* c,
                                                              spotifyctl_bool_cb cb,
                                                              void* user) {
    if (!c || !cb) return 0;
    auto core_tok = c->core.OnAudibleChanged.connect(
        [cb, user](bool audible) { cb(audible ? 1 : 0, user); });
    auto ext = c->reserveToken();
    c->storeConn(ext, [c, core_tok]() { c->core.OnAudibleChanged.disconnect(core_tok); });
    return ext;
}

SPOTIFYCTL_API spotifyctl_token spotifyctl_on_raw_title(spotifyctl_client* c,
                                                        spotifyctl_string_cb cb,
                                                        void* user) {
    if (!c || !cb) return 0;
    auto core_tok = c->core.OnRawTitle.connect(
        [cb, user](const std::string& s) { cb(s.c_str(), user); });
    auto ext = c->reserveToken();
    c->storeConn(ext, [c, core_tok]() { c->core.OnRawTitle.disconnect(core_tok); });
    return ext;
}

SPOTIFYCTL_API spotifyctl_token spotifyctl_on_opened(spotifyctl_client* c,
                                                     spotifyctl_void_cb cb,
                                                     void* user) {
    if (!c || !cb) return 0;
    auto core_tok = c->core.OnOpened.connect([cb, user]() { cb(user); });
    auto ext = c->reserveToken();
    c->storeConn(ext, [c, core_tok]() { c->core.OnOpened.disconnect(core_tok); });
    return ext;
}

SPOTIFYCTL_API spotifyctl_token spotifyctl_on_closed(spotifyctl_client* c,
                                                     spotifyctl_void_cb cb,
                                                     void* user) {
    if (!c || !cb) return 0;
    auto core_tok = c->core.OnClosed.connect([cb, user]() { cb(user); });
    auto ext = c->reserveToken();
    c->storeConn(ext, [c, core_tok]() { c->core.OnClosed.disconnect(core_tok); });
    return ext;
}

SPOTIFYCTL_API void spotifyctl_disconnect(spotifyctl_client* c, spotifyctl_token token) {
    if (!c || token == 0) return;
    c->removeConn(token);
}

// -------------------------------------------------------------------------
// URI builders
// -------------------------------------------------------------------------

SPOTIFYCTL_API char* spotifyctl_uri_track(const char* id) {
    if (!id) return nullptr;
    return DupString(spotify::uri::Track(id));
}
SPOTIFYCTL_API char* spotifyctl_uri_album(const char* id) {
    if (!id) return nullptr;
    return DupString(spotify::uri::Album(id));
}
SPOTIFYCTL_API char* spotifyctl_uri_playlist(const char* id) {
    if (!id) return nullptr;
    return DupString(spotify::uri::Playlist(id));
}
SPOTIFYCTL_API char* spotifyctl_uri_artist(const char* id) {
    if (!id) return nullptr;
    return DupString(spotify::uri::Artist(id));
}
SPOTIFYCTL_API char* spotifyctl_uri_user(const char* username) {
    if (!username) return nullptr;
    return DupString(spotify::uri::User(username));
}
SPOTIFYCTL_API char* spotifyctl_uri_search(const char* query) {
    if (!query) return nullptr;
    return DupString(spotify::uri::Search(query));
}

SPOTIFYCTL_API void spotifyctl_free_string(char* s) {
    std::free(s);
}

}  // extern "C"
