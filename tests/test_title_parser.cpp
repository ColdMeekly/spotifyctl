#include <catch2/catch_test_macros.hpp>

#include "spotify/title_parser.h"

using spotify::ParseWindowTitle;
using Status = spotify::PlaybackState::Status;

TEST_CASE("idle sentinels map to Paused + idle=true", "[title_parser]") {
    for (auto sentinel : {"Spotify", "Spotify Free", "Spotify Premium"}) {
        const auto r = ParseWindowTitle(sentinel);
        CHECK(r.idle);
        CHECK_FALSE(r.isAd);
        CHECK(r.status == Status::Paused);
        CHECK(r.artist.empty());
        CHECK(r.title.empty());
    }
}

TEST_CASE("Advertisement sentinel flags isAd + Playing", "[title_parser]") {
    const auto r = ParseWindowTitle("Advertisement");
    CHECK_FALSE(r.idle);
    CHECK(r.isAd);
    CHECK(r.status == Status::Playing);
    CHECK(r.artist.empty());
    CHECK(r.title.empty());
}

TEST_CASE("Artist - Track splits on first ' - '", "[title_parser]") {
    const auto r = ParseWindowTitle("Rick Astley - Never Gonna Give You Up");
    CHECK(r.artist == "Rick Astley");
    CHECK(r.title == "Never Gonna Give You Up");
    CHECK(r.status == Status::Playing);
    CHECK_FALSE(r.idle);
    CHECK_FALSE(r.isAd);
}

TEST_CASE("track title containing ' - ' keeps the tail intact", "[title_parser]") {
    // Spotify uses the FIRST ' - ' as the separator, so the rest stays in title.
    const auto r = ParseWindowTitle("Artist - Track - Remastered 2009");
    CHECK(r.artist == "Artist");
    CHECK(r.title == "Track - Remastered 2009");
    CHECK(r.status == Status::Playing);
}

TEST_CASE("empty or whitespace-only title yields default parse", "[title_parser]") {
    for (auto s : {"", "   ", "\t\t"}) {
        const auto r = ParseWindowTitle(s);
        CHECK_FALSE(r.idle);
        CHECK_FALSE(r.isAd);
        CHECK(r.status == Status::Unknown);
        CHECK(r.artist.empty());
        CHECK(r.title.empty());
    }
}

TEST_CASE("unknown / unrecognized title yields default parse", "[title_parser]") {
    const auto r = ParseWindowTitle("Some Weird Window Caption");
    CHECK_FALSE(r.idle);
    CHECK_FALSE(r.isAd);
    CHECK(r.status == Status::Unknown);
    CHECK(r.artist.empty());
    CHECK(r.title.empty());
}

TEST_CASE("leading/trailing whitespace is trimmed", "[title_parser]") {
    const auto r = ParseWindowTitle("   Rick Astley - Never Gonna Give You Up   ");
    CHECK(r.artist == "Rick Astley");
    CHECK(r.title == "Never Gonna Give You Up");
    CHECK(r.status == Status::Playing);
}

TEST_CASE("half-empty 'Artist - ' does not become a track", "[title_parser]") {
    const auto r = ParseWindowTitle("Artist - ");
    CHECK(r.artist.empty());
    CHECK(r.title.empty());
    CHECK(r.status == Status::Unknown);
}
