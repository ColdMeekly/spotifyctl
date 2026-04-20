#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace spotify {

struct PlaybackState {
    enum class Status : std::uint8_t {
        Unknown,
        Stopped,
        Paused,
        Playing,
        ChangingTrack,
    };

    Status status = Status::Unknown;

    std::string artist;
    std::string title;
    std::string album;

    std::chrono::milliseconds position{0};
    std::chrono::milliseconds duration{0};

    // Raw bytes of the current track's album art thumbnail, as delivered by
    // SMTC. Usually JPEG; consumers should not assume a particular format.
    std::vector<std::byte> albumArt;

    bool canSeek = false;
    bool canSkipNext = false;
    bool canSkipPrev = false;

    // True when Spotify is playing an advertisement.
    bool isAd = false;

    // True when the Spotify audio session is producing audible output
    // (peak > threshold) regardless of reported play/pause state.
    bool audible = false;

    // Per-app volume in [0, 1]. -1 while the audio session hasn't been
    // resolved yet.
    float appVolume = -1.0f;
    bool  appMuted  = false;

    friend bool operator==(const PlaybackState&, const PlaybackState&) = default;
};

}  // namespace spotify
