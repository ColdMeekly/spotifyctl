#include <catch2/catch_test_macros.hpp>

#include "position.h"

using namespace std::chrono_literals;
using spotify::ExtrapolatePosition;
using spotify::IsTrackChange;
using spotify::PlaybackState;
using spotify::PositionAnchor;
using spotify::SmoothPosition;
using spotify::UpdateAnchor;
using Status = PlaybackState::Status;

namespace {
// All anchor-math tests use a small fixed time_point so we can reason about
// elapsed math directly rather than calling steady_clock::now().
using tp = std::chrono::steady_clock::time_point;
constexpr tp kT0 = tp{std::chrono::milliseconds(1'000'000)};
}  // namespace

// -----------------------------------------------------------------------
// ExtrapolatePosition: non-Playing paths return raw position unchanged
// -----------------------------------------------------------------------

TEST_CASE("extrapolate returns raw position when not Playing", "[position]") {
    CHECK(ExtrapolatePosition(Status::Paused,   10'000ms, 180'000ms, 5'000ms) == 10'000ms);
    CHECK(ExtrapolatePosition(Status::Stopped,  10'000ms, 180'000ms, 5'000ms) == 10'000ms);
    CHECK(ExtrapolatePosition(Status::Unknown,  10'000ms, 180'000ms, 5'000ms) == 10'000ms);
    CHECK(ExtrapolatePosition(Status::ChangingTrack, 10'000ms, 180'000ms, 5'000ms) == 10'000ms);
}

// -----------------------------------------------------------------------
// Playing: adds elapsed, clamps to duration
// -----------------------------------------------------------------------

TEST_CASE("extrapolate advances by elapsed when Playing", "[position]") {
    CHECK(ExtrapolatePosition(Status::Playing, 10'000ms, 180'000ms,   500ms) == 10'500ms);
    CHECK(ExtrapolatePosition(Status::Playing, 10'000ms, 180'000ms, 5'000ms) == 15'000ms);
    CHECK(ExtrapolatePosition(Status::Playing,      0ms, 180'000ms, 1'234ms) ==  1'234ms);
}

TEST_CASE("extrapolate clamps to duration when elapsed would overshoot", "[position]") {
    // Normal case: position + elapsed > duration → cap at duration.
    CHECK(ExtrapolatePosition(Status::Playing, 175'000ms, 180'000ms, 10'000ms) == 180'000ms);
}

TEST_CASE("extrapolate permits position > duration when raw position already was",
          "[position]") {
    // SMTC occasionally reports position > duration transiently (e.g., at the
    // very end of a track). The cap is max(duration, position) so we don't
    // rewind into "before the track ended" values.
    CHECK(ExtrapolatePosition(Status::Playing, 182'000ms, 180'000ms, 500ms) == 182'000ms);
}

TEST_CASE("extrapolate with zero duration applies no upper cap", "[position]") {
    // duration == 0 means "not set / not known". Should still extrapolate.
    CHECK(ExtrapolatePosition(Status::Playing, 5'000ms, 0ms, 1'000ms) == 6'000ms);
}

TEST_CASE("extrapolate never returns negative values", "[position]") {
    // Negative elapsed is pathological (clock went backwards) but should be
    // clamped to 0 rather than producing a negative position.
    CHECK(ExtrapolatePosition(Status::Playing, 1'000ms, 0ms, -10'000ms) == 0ms);
}

// -----------------------------------------------------------------------
// IsTrackChange: (artist, title, album) tuple compare
// -----------------------------------------------------------------------

TEST_CASE("track-change detector fires on title flip", "[position][edges]") {
    PlaybackState a; a.artist = "Rick"; a.title = "Up";
    PlaybackState b; b.artist = "Rick"; b.title = "Down";
    CHECK(IsTrackChange(a, b));
}

TEST_CASE("track-change detector fires on empty->populated (startup)",
          "[position][edges]") {
    PlaybackState empty;
    PlaybackState a; a.artist = "Rick"; a.title = "Up";
    CHECK(IsTrackChange(empty, a));
    CHECK(IsTrackChange(a, empty));  // and populated->empty (shutdown)
}

TEST_CASE("track-change detector ignores unrelated field changes",
          "[position][edges]") {
    PlaybackState a; a.artist = "Rick"; a.title = "Up"; a.status = Status::Playing;
    PlaybackState b = a; b.position = std::chrono::milliseconds(5'000);
    b.appVolume = 0.5f; b.audible = true;
    CHECK_FALSE(IsTrackChange(a, b));
}

TEST_CASE("track-change detector counts album-only changes", "[position][edges]") {
    // Same artist + title, different album → still a track change (distinct
    // track IDs with identical names, e.g. single vs album release).
    PlaybackState a; a.artist = "X"; a.title = "Y"; a.album = "AlbumA";
    PlaybackState b = a; b.album = "AlbumB";
    CHECK(IsTrackChange(a, b));
}

// -----------------------------------------------------------------------
// SmoothPosition: bridges anchor + elapsed -> extrapolated position
// -----------------------------------------------------------------------

TEST_CASE("smooth position returns 0 when anchor has never been captured",
          "[position][anchor]") {
    PositionAnchor empty{};
    CHECK(SmoothPosition(empty, Status::Playing, 180'000ms, kT0 + 5'000ms) == 0ms);
    CHECK(SmoothPosition(empty, Status::Paused,  180'000ms, kT0 + 5'000ms) == 0ms);
}

TEST_CASE("smooth position returns raw anchor position when not Playing",
          "[position][anchor]") {
    PositionAnchor a{10'000ms, kT0, true};
    // A second of wall time has passed but Paused → no extrapolation.
    CHECK(SmoothPosition(a, Status::Paused,        180'000ms, kT0 + 1'000ms) == 10'000ms);
    CHECK(SmoothPosition(a, Status::ChangingTrack, 180'000ms, kT0 + 1'000ms) == 10'000ms);
}

TEST_CASE("smooth position advances by elapsed when Playing",
          "[position][anchor]") {
    PositionAnchor a{10'000ms, kT0, true};
    CHECK(SmoothPosition(a, Status::Playing, 180'000ms, kT0 + 1'500ms) == 11'500ms);
}

// -----------------------------------------------------------------------
// UpdateAnchor: snap vs keep decisions
// -----------------------------------------------------------------------

TEST_CASE("update-anchor snaps on first observation", "[position][anchor]") {
    PositionAnchor prev{};
    auto next = UpdateAnchor(prev, Status::Unknown, Status::Playing,
                             12'000ms, 180'000ms, kT0);
    CHECK(next.valid);
    CHECK(next.position == 12'000ms);
    CHECK(next.time == kT0);
}

TEST_CASE("update-anchor snaps on status change", "[position][anchor]") {
    // Playing → Paused: the anchor must track SMTC's paused position so
    // LatestPositionSmooth returns the right number immediately.
    PositionAnchor prev{10'000ms, kT0, true};
    auto next = UpdateAnchor(prev, Status::Playing, Status::Paused,
                             10'500ms, 180'000ms, kT0 + 500ms);
    CHECK(next.position == 10'500ms);
    CHECK(next.time == kT0 + 500ms);
}

TEST_CASE("update-anchor always snaps while not Playing", "[position][anchor]") {
    // Paused state: any reported SMTC position should be reflected 1:1 so
    // that scrubbing while paused updates the smooth position immediately.
    PositionAnchor prev{10'000ms, kT0, true};
    auto next = UpdateAnchor(prev, Status::Paused, Status::Paused,
                             30'000ms, 180'000ms, kT0 + 2'000ms);
    CHECK(next.position == 30'000ms);
    CHECK(next.time == kT0 + 2'000ms);
}

TEST_CASE("update-anchor keeps prior anchor on small delta during Playing",
          "[position][anchor]") {
    // Anchor was (10s, t0). One second later SMTC republishes position=10.900s
    // (100ms behind real) — within the snap threshold, so the anchor is
    // preserved. Extrapolation at t0+1000 still reads 11'000 (smooth).
    PositionAnchor prev{10'000ms, kT0, true};
    auto next = UpdateAnchor(prev, Status::Playing, Status::Playing,
                             10'900ms, 180'000ms, kT0 + 1'000ms);
    CHECK(next.valid);
    CHECK(next.position == prev.position);
    CHECK(next.time == prev.time);

    // Verify smoothness: reading SmoothPosition at t0+1000 gives 11'000
    // regardless of whether the anchor was updated or not.
    CHECK(SmoothPosition(next, Status::Playing, 180'000ms, kT0 + 1'000ms) == 11'000ms);
}

TEST_CASE("update-anchor snaps on large positive delta (forward seek)",
          "[position][anchor]") {
    // Anchor (10s, t0). 1 second later SMTC reports 25s — a 14-second seek
    // forward. Far outside the snap threshold; must re-anchor so the next
    // tick reflects the seek.
    PositionAnchor prev{10'000ms, kT0, true};
    auto next = UpdateAnchor(prev, Status::Playing, Status::Playing,
                             25'000ms, 180'000ms, kT0 + 1'000ms);
    CHECK(next.position == 25'000ms);
    CHECK(next.time == kT0 + 1'000ms);
}

TEST_CASE("update-anchor snaps on large negative delta (backward seek / new track)",
          "[position][anchor]") {
    // Anchor (175s, t0). 1 second later SMTC reports 2s (new track started).
    // Extrapolation would predict 176s; |2 - 176| = 174s, far above threshold.
    PositionAnchor prev{175'000ms, kT0, true};
    auto next = UpdateAnchor(prev, Status::Playing, Status::Playing,
                             2'000ms, 180'000ms, kT0 + 1'000ms);
    CHECK(next.position == 2'000ms);
    CHECK(next.time == kT0 + 1'000ms);
}

TEST_CASE("update-anchor respects custom snap threshold", "[position][anchor]") {
    // Same observation (100ms skew from predicted) tested against two
    // thresholds. With 50ms threshold the delta exceeds it → snap. With
    // 200ms threshold it's absorbed → keep. Proves the parameter wires
    // through the decision.
    PositionAnchor prev{10'000ms, kT0, true};

    auto snapped = UpdateAnchor(prev, Status::Playing, Status::Playing,
                                10'900ms, 180'000ms, kT0 + 1'000ms,
                                /*snapThreshold=*/50ms);
    CHECK(snapped.position == 10'900ms);
    CHECK(snapped.time == kT0 + 1'000ms);

    auto kept = UpdateAnchor(prev, Status::Playing, Status::Playing,
                             10'900ms, 180'000ms, kT0 + 1'000ms,
                             /*snapThreshold=*/200ms);
    CHECK(kept.position == prev.position);
    CHECK(kept.time == prev.time);
}
