#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
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

    // 64-bit digest of `albumArt` used by operator== to avoid a multi-KB
    // memcmp per SMTC republish. 0 is both the default (no art) and a valid
    // hash value — size is compared alongside to disambiguate. Call
    // RecomputeAlbumArtHash() after mutating albumArt directly.
    std::uint64_t albumArtHash = 0;

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

    void RecomputeAlbumArtHash() {
        if (albumArt.empty()) {
            albumArtHash = 0;
            return;
        }
        std::string_view bytes(reinterpret_cast<const char*>(albumArt.data()),
                               albumArt.size());
        albumArtHash = std::hash<std::string_view>{}(bytes);
    }

    // Hand-written equality: we compare `albumArt` via (size, albumArtHash)
    // instead of a byte-by-byte memcmp, so the steady-state Aggregate() path
    // (same track republished on every SMTC tick) stays cheap. 64-bit hash
    // collision risk is ~10⁻¹⁹ and the only consequence of a false positive
    // would be suppressing one OnStateChanged for an art change — benign.
    friend bool operator==(const PlaybackState& a, const PlaybackState& b) {
        return a.status       == b.status
            && a.position     == b.position
            && a.duration     == b.duration
            && a.canSeek      == b.canSeek
            && a.canSkipNext  == b.canSkipNext
            && a.canSkipPrev  == b.canSkipPrev
            && a.isAd         == b.isAd
            && a.audible      == b.audible
            && a.appMuted     == b.appMuted
            && a.appVolume    == b.appVolume
            && a.albumArt.size() == b.albumArt.size()
            && a.albumArtHash == b.albumArtHash
            && a.artist       == b.artist
            && a.title        == b.title
            && a.album        == b.album;
    }
};

}  // namespace spotify
