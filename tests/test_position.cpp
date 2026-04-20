#include <catch2/catch_test_macros.hpp>

#include "position.h"

using namespace std::chrono_literals;
using spotify::ExtrapolatePosition;
using spotify::IsTrackChange;
using spotify::PlaybackState;
using Status = PlaybackState::Status;

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
