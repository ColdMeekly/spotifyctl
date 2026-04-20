#include <catch2/catch_test_macros.hpp>

#include "spotify/playback.h"
#include "aggregator.h"

using spotify::AudioFrag;
using spotify::FuseFragments;
using spotify::PlaybackState;
using spotify::TitleFrag;
using Status = PlaybackState::Status;

namespace {

PlaybackState SmtcFrag(Status st, std::string artist, std::string title,
                       std::string album = {}) {
    PlaybackState s;
    s.status = st;
    s.artist = std::move(artist);
    s.title  = std::move(title);
    s.album  = std::move(album);
    return s;
}

}  // namespace

// -----------------------------------------------------------------------
// Empty / skeleton state
// -----------------------------------------------------------------------

TEST_CASE("empty fragments yield skeleton state", "[aggregator]") {
    const auto out = FuseFragments({}, {}, {});

    CHECK(out.status == Status::Unknown);
    CHECK(out.artist.empty());
    CHECK(out.title.empty());
    CHECK(out.album.empty());
    CHECK_FALSE(out.isAd);
    CHECK_FALSE(out.audible);
    CHECK_FALSE(out.appMuted);
    // audio.resolved == false → appVolume forced to sentinel.
    CHECK(out.appVolume == -1.0f);
}

// -----------------------------------------------------------------------
// SMTC > Title precedence
// -----------------------------------------------------------------------

TEST_CASE("SMTC wins artist/title/album when present", "[aggregator]") {
    const auto smtc = SmtcFrag(Status::Playing, "SMTC Artist", "SMTC Title", "SMTC Album");
    TitleFrag title;
    title.any    = true;
    title.artist = "Title Artist";
    title.title  = "Title Title";
    title.status = Status::Paused;

    const auto out = FuseFragments(smtc, {}, title);

    CHECK(out.artist == "SMTC Artist");
    CHECK(out.title  == "SMTC Title");
    CHECK(out.album  == "SMTC Album");
    CHECK(out.status == Status::Playing);  // SMTC status wins too
}

TEST_CASE("title fills in when SMTC has no artist/title/status", "[aggregator]") {
    // SMTC frag is entirely empty — smtcHasContent == false, so we fall
    // through to the title fragment for all content.
    TitleFrag title;
    title.any    = true;
    title.artist = "Rick Astley";
    title.title  = "Never Gonna Give You Up";
    title.status = Status::Paused;

    const auto out = FuseFragments({}, {}, title);

    CHECK(out.artist == "Rick Astley");
    CHECK(out.title  == "Never Gonna Give You Up");
    CHECK(out.status == Status::Paused);
}

TEST_CASE("title fills only the empty SMTC fields", "[aggregator]") {
    // SMTC has title but no artist (e.g., podcast edge case). Aggregator
    // fills in artist from title frag but keeps SMTC title.
    PlaybackState smtc;
    smtc.status = Status::Playing;
    smtc.title  = "SMTC Title";
    TitleFrag title;
    title.any    = true;
    title.artist = "Fallback Artist";
    title.title  = "Fallback Title";

    const auto out = FuseFragments(smtc, {}, title);

    CHECK(out.artist == "Fallback Artist");  // filled from title
    CHECK(out.title  == "SMTC Title");       // SMTC wins
    CHECK(out.status == Status::Playing);
}

// -----------------------------------------------------------------------
// Title-only signals (status hint, isAd, untouched-any gate)
// -----------------------------------------------------------------------

TEST_CASE("title status is ignored until ApplyTitle has run once", "[aggregator]") {
    TitleFrag title;
    title.any    = false;     // never called
    title.status = Status::Paused;

    const auto out = FuseFragments({}, {}, title);
    CHECK(out.status == Status::Unknown);
}

TEST_CASE("isAd is always taken from the title fragment", "[aggregator]") {
    // Even when SMTC still reports the previous playing track, isAd from
    // the title ("Advertisement") wins — this is the documented behavior
    // and matches what README.md promises consumers.
    const auto smtc = SmtcFrag(Status::Playing, "Prev Artist", "Prev Title");
    TitleFrag title;
    title.any  = true;
    title.isAd = true;

    const auto out = FuseFragments(smtc, {}, title);
    CHECK(out.isAd);
    CHECK(out.artist == "Prev Artist");
    CHECK(out.title  == "Prev Title");
    CHECK(out.status == Status::Playing);
}

TEST_CASE("isAd defaults to false when title fragment has not flagged it",
          "[aggregator]") {
    PlaybackState smtc = SmtcFrag(Status::Playing, "Artist", "Title");
    smtc.isAd = true;  // Even if SMTC somehow carries it — should be overwritten.

    const auto out = FuseFragments(smtc, {}, {});
    CHECK_FALSE(out.isAd);
}

// -----------------------------------------------------------------------
// Audio layer overrides
// -----------------------------------------------------------------------

TEST_CASE("audio layer owns appVolume/appMuted/audible", "[aggregator]") {
    // Even if SMTC frag carried stale audio numbers, aggregator must replace
    // them with the audio fragment's values.
    PlaybackState smtc = SmtcFrag(Status::Playing, "A", "T");
    smtc.appVolume = 0.123f;
    smtc.appMuted  = true;
    smtc.audible   = true;

    AudioFrag audio;
    audio.vol      = 0.42f;
    audio.muted    = false;
    audio.audible  = false;
    audio.resolved = true;

    const auto out = FuseFragments(smtc, audio, {});

    CHECK(out.appVolume == 0.42f);
    CHECK_FALSE(out.appMuted);
    CHECK_FALSE(out.audible);
}

TEST_CASE("appVolume stays at -1 until audio session is resolved",
          "[aggregator]") {
    AudioFrag audio;
    audio.vol      = 0.9f;
    audio.resolved = false;  // not yet bound

    const auto out = FuseFragments({}, audio, {});
    CHECK(out.appVolume == -1.0f);
}

// -----------------------------------------------------------------------
// Album art + durations pass through from SMTC
// -----------------------------------------------------------------------

TEST_CASE("album art bytes propagate from SMTC frag", "[aggregator]") {
    PlaybackState smtc = SmtcFrag(Status::Playing, "A", "T");
    smtc.albumArt.assign(16, std::byte{0xEE});
    smtc.position = std::chrono::milliseconds(1500);
    smtc.duration = std::chrono::milliseconds(180'000);
    smtc.canSeek  = true;

    const auto out = FuseFragments(smtc, {}, {});
    CHECK(out.albumArt.size() == 16);
    CHECK(out.position == std::chrono::milliseconds(1500));
    CHECK(out.duration == std::chrono::milliseconds(180'000));
    CHECK(out.canSeek);
}

TEST_CASE("capability flags come from SMTC only", "[aggregator]") {
    // Title frag has no knowledge of canSeek/canSkip*; they must stay false
    // when SMTC is silent.
    TitleFrag title;
    title.any    = true;
    title.status = Status::Playing;

    const auto out = FuseFragments({}, {}, title);
    CHECK_FALSE(out.canSeek);
    CHECK_FALSE(out.canSkipNext);
    CHECK_FALSE(out.canSkipPrev);
}

// -----------------------------------------------------------------------
// PlaybackState equality: albumArt size + cached 64-bit hash
// -----------------------------------------------------------------------

TEST_CASE("equality: same art bytes compare equal via hash", "[equality]") {
    PlaybackState a;
    a.albumArt.assign(4096, std::byte{0xAB});
    a.RecomputeAlbumArtHash();

    PlaybackState b = a;  // copy includes hash
    CHECK(a == b);
}

TEST_CASE("equality: different art bytes of same length are unequal",
          "[equality]") {
    PlaybackState a, b;
    a.albumArt.assign(4096, std::byte{0xAB});
    b.albumArt.assign(4096, std::byte{0xCD});
    a.RecomputeAlbumArtHash();
    b.RecomputeAlbumArtHash();

    CHECK_FALSE(a == b);
}

TEST_CASE("equality: different art sizes are unequal even with matching hash",
          "[equality]") {
    // Both are empty prefixes, but with hash defaulted to 0. A zero hash is
    // the only valid value for "no art"; size disambiguates.
    PlaybackState a, b;
    a.albumArt.clear();          // size 0, hash 0
    b.albumArt.assign(1, std::byte{0});
    b.RecomputeAlbumArtHash();

    CHECK_FALSE(a == b);
}

TEST_CASE("equality: both empty art compare equal", "[equality]") {
    PlaybackState a, b;
    // Neither touches albumArt; both start empty with hash 0.
    CHECK(a == b);
}

TEST_CASE("equality: ignores albumArt contents when hashes match",
          "[equality]") {
    // This is the steady-state path: Aggregate() assigns the same art bytes
    // every republish; operator== must not pay for the byte compare.
    PlaybackState a;
    a.albumArt.assign(8192, std::byte{0x42});
    a.RecomputeAlbumArtHash();

    PlaybackState b = a;
    // Flip a single byte AFTER the copy and WITHOUT recomputing the hash.
    // This is a deliberately pathological test — we expect operator== to
    // consider them equal because it trusts the cached hash.
    b.albumArt[0] = std::byte{0x00};
    CHECK(a == b);  // matches the documented "hash is authoritative" rule
}
