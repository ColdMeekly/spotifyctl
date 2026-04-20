#pragma once

// Minimal hand-rolled JSON emitter for PlaybackState. Private to the library;
// shared between the C ABI (spotifyctl_latest_state_json) and the CLI.

#include <cstdint>
#include <string>
#include <string_view>

#include "spotify/playback.h"

namespace spotify::json {

inline void AppendEscaped(std::string& out, std::string_view s) {
    out.push_back('"');
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    static constexpr char kHex[] = "0123456789abcdef";
                    char buf[7] = {'\\', 'u', '0', '0', 0, 0, 0};
                    buf[4] = kHex[(c >> 4) & 0xF];
                    buf[5] = kHex[c & 0xF];
                    out.append(buf, 6);
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    out.push_back('"');
}

inline const char* StatusName(PlaybackState::Status s) {
    switch (s) {
        case PlaybackState::Status::Stopped:       return "stopped";
        case PlaybackState::Status::Paused:        return "paused";
        case PlaybackState::Status::Playing:       return "playing";
        case PlaybackState::Status::ChangingTrack: return "changing_track";
        case PlaybackState::Status::Unknown:
        default:                                   return "unknown";
    }
}

inline std::string WriteStateJson(const PlaybackState& s) {
    std::string out;
    out.reserve(256 + s.artist.size() + s.title.size() + s.album.size());

    out += "{\"status\":";
    AppendEscaped(out, StatusName(s.status));
    out += ",\"artist\":";
    AppendEscaped(out, s.artist);
    out += ",\"title\":";
    AppendEscaped(out, s.title);
    out += ",\"album\":";
    AppendEscaped(out, s.album);
    out += ",\"position_ms\":" + std::to_string(s.position.count());
    out += ",\"duration_ms\":" + std::to_string(s.duration.count());
    out += ",\"album_art_len\":" + std::to_string(s.albumArt.size());
    out += ",\"can_seek\":";       out += s.canSeek       ? "true" : "false";
    out += ",\"can_skip_next\":";  out += s.canSkipNext   ? "true" : "false";
    out += ",\"can_skip_prev\":";  out += s.canSkipPrev   ? "true" : "false";
    out += ",\"is_ad\":";          out += s.isAd          ? "true" : "false";
    out += ",\"audible\":";        out += s.audible       ? "true" : "false";
    out += ",\"app_muted\":";      out += s.appMuted      ? "true" : "false";

    out += ",\"app_volume\":";
    if (s.appVolume < 0.0f) {
        out += "null";
    } else {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.4f", static_cast<double>(s.appVolume));
        out += buf;
    }
    out += "}";
    return out;
}

}  // namespace spotify::json
