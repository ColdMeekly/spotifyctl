#pragma once

// Internal-only header. Pure, Windows-free helpers for position extrapolation
// and edge-detection between successive PlaybackState snapshots. Extracted so
// the rules can be unit-tested without spinning up the SMTC / audio layers.

#include <algorithm>
#include <chrono>
#include <tuple>

#include "spotify/playback.h"

namespace spotify {

// Extrapolates a SMTC position forward when playback is Playing. SMTC
// delivers positions only on play/pause/seek/track edges, so consumers who
// want a ticking progress bar need to add `elapsed` on the client side —
// this encapsulates the rule so every caller gets the same answer.
//
// Rules:
//   * status != Playing  → return `position` unchanged (SMTC is authoritative
//     when not actively playing).
//   * status == Playing  → return clamp(position + elapsed, 0, max(duration,
//     position)). The max() guards against SMTC reporting position > duration
//     transiently near a track boundary (happens in practice on some tracks).
//   * duration == 0 is treated as "unknown"; no upper clamp is applied.
inline std::chrono::milliseconds ExtrapolatePosition(
    PlaybackState::Status status,
    std::chrono::milliseconds position,
    std::chrono::milliseconds duration,
    std::chrono::milliseconds elapsedSinceAnchor) {

    if (status != PlaybackState::Status::Playing) return position;

    auto extrapolated = position + elapsedSinceAnchor;
    if (extrapolated.count() < 0) extrapolated = std::chrono::milliseconds(0);

    if (duration.count() > 0) {
        const auto cap = std::max(duration, position);
        if (extrapolated > cap) extrapolated = cap;
    }
    return extrapolated;
}

// Track identity: artist + title + album. Empty-vs-populated transitions are
// "track changes" in both directions (startup and shutdown).
inline bool IsTrackChange(const PlaybackState& prev, const PlaybackState& cur) {
    return std::tie(prev.artist, prev.title, prev.album)
        != std::tie(cur.artist,  cur.title,  cur.album);
}

}  // namespace spotify
