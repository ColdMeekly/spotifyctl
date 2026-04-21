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

// Reference point used by LatestPositionSmooth / the ~1 Hz tick to project the
// playback position forward without snapping to every SMTC update.
//
// `valid = false` means "no SMTC anchor has ever been captured on this client";
// consumers should fall through to a safe default (e.g., 0) rather than
// extrapolating from `time` — default-constructed `steady_clock::time_point`
// is epoch 0, and `now() - 0` is system uptime, not elapsed playback.
struct PositionAnchor {
    std::chrono::milliseconds             position{0};
    std::chrono::steady_clock::time_point time{};
    bool                                  valid = false;
};

// Default tolerance for "is this SMTC update a real seek/track-change, or just
// clock skew between SMTC and steady_clock?" Picked so that:
//   * normal SMTC republishes during playback (tens-of-ms skew) keep the old
//     anchor — extrapolation stays smooth across the update, and
//   * any realistic seek (users don't scrub by less than a second) snaps.
inline constexpr std::chrono::milliseconds kDefaultAnchorSnapThreshold{1500};

// Decides how to update a smoothing anchor given a new SMTC observation.
//
// Returns a *new* anchor (never mutates `prev`). Rules:
//   * No prior anchor → snap (establish one).
//   * Status change   → snap (play/pause/seek/track edges are discontinuities).
//   * Not Playing     → snap (extrapolation is off; keep the anchor position
//                        matched to SMTC so LatestPositionSmooth returns the
//                        current raw position).
//   * Playing steady  → compute where we *would have* extrapolated to at `now`.
//                        If `newPosition` is within `snapThreshold` of that
//                        prediction, keep the old anchor so the progress bar
//                        doesn't visibly step on every SMTC republish.
//                        Otherwise snap (seek, track change, long stall).
inline PositionAnchor UpdateAnchor(
    const PositionAnchor& prev,
    PlaybackState::Status prevStatus,
    PlaybackState::Status newStatus,
    std::chrono::milliseconds newPosition,
    std::chrono::milliseconds duration,
    std::chrono::steady_clock::time_point now,
    std::chrono::milliseconds snapThreshold = kDefaultAnchorSnapThreshold) {

    const PositionAnchor snapped{newPosition, now, true};

    if (!prev.valid)                                       return snapped;
    if (newStatus != prevStatus)                           return snapped;
    if (newStatus != PlaybackState::Status::Playing)       return snapped;

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - prev.time);
    const auto predicted = ExtrapolatePosition(
        PlaybackState::Status::Playing, prev.position, duration, elapsed);
    const auto delta = newPosition - predicted;
    const auto absDelta = delta < std::chrono::milliseconds(0) ? -delta : delta;

    return absDelta > snapThreshold ? snapped : prev;
}

// Computes the smoothed playback position from a PositionAnchor. This is the
// single source of truth used by both pull (LatestPositionSmooth) and push
// (OnPositionChanged tick) paths.
//
// Returns 0 when the anchor has never been captured — callers that want a
// different fallback (e.g., latest.position) can substitute.
inline std::chrono::milliseconds SmoothPosition(
    const PositionAnchor& anchor,
    PlaybackState::Status status,
    std::chrono::milliseconds duration,
    std::chrono::steady_clock::time_point now) {

    if (!anchor.valid) return std::chrono::milliseconds(0);
    if (status != PlaybackState::Status::Playing) return anchor.position;

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - anchor.time);
    return ExtrapolatePosition(status, anchor.position, duration, elapsed);
}

// Track identity: artist + title + album. Empty-vs-populated transitions are
// "track changes" in both directions (startup and shutdown).
inline bool IsTrackChange(const PlaybackState& prev, const PlaybackState& cur) {
    return std::tie(prev.artist, prev.title, prev.album)
        != std::tie(cur.artist,  cur.title,  cur.album);
}

}  // namespace spotify
