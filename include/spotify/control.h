#pragma once

#include <string>
#include <string_view>

namespace spotify {

enum class AppCommand {
    Stop,
    Play,
    Pause,
    PlayPause,
    NextTrack,
    PrevTrack,
    VolUp,
    VolDown,
    MuteUnmute,
};

// Builders for `spotify:` URIs consumable by OpenUri() or any ShellExecute-
// style caller. IDs are the base62 ids used by the Spotify ecosystem.
namespace uri {

std::string Track(std::string_view id);
std::string Album(std::string_view id);
std::string Playlist(std::string_view id);
std::string Artist(std::string_view id);
std::string User(std::string_view username);
std::string Search(std::string_view query);  // percent-encodes query

}  // namespace uri

}  // namespace spotify
