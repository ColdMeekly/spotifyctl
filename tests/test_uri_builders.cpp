#include <catch2/catch_test_macros.hpp>

#include "spotify/control.h"

namespace uri = spotify::uri;

TEST_CASE("Track/Album/Playlist/Artist/User concatenate the scheme prefix", "[uri]") {
    CHECK(uri::Track("4uLU6hMCjMI75M1A2tKUQC")    == "spotify:track:4uLU6hMCjMI75M1A2tKUQC");
    CHECK(uri::Album("1DFixLWuPkv3KT3TnV35m3")    == "spotify:album:1DFixLWuPkv3KT3TnV35m3");
    CHECK(uri::Playlist("37i9dQZF1DXcBWIGoYBM5M") == "spotify:playlist:37i9dQZF1DXcBWIGoYBM5M");
    CHECK(uri::Artist("0gxyHStUsqpMadRV0Di1Qt")   == "spotify:artist:0gxyHStUsqpMadRV0Di1Qt");
    CHECK(uri::User("spotify")                    == "spotify:user:spotify");
}

TEST_CASE("Search percent-encodes reserved characters", "[uri]") {
    CHECK(uri::Search("hello")            == "spotify:search:hello");
    CHECK(uri::Search("hello world")      == "spotify:search:hello%20world");
    CHECK(uri::Search("a&b=c")            == "spotify:search:a%26b%3Dc");
    CHECK(uri::Search("a/b?c#d")          == "spotify:search:a%2Fb%3Fc%23d");
}

TEST_CASE("Search leaves RFC 3986 unreserved characters alone", "[uri]") {
    // A-Z a-z 0-9 - _ . ~
    CHECK(uri::Search("ABCxyz0123-_.~") == "spotify:search:ABCxyz0123-_.~");
}

TEST_CASE("Search upper-cases hex digits", "[uri]") {
    // Space = 0x20 -> "%20", not "%2f" / "%2F" ambiguity but double-check case.
    CHECK(uri::Search(":") == "spotify:search:%3A");
    CHECK(uri::Search("/") == "spotify:search:%2F");
}

TEST_CASE("Search handles empty query", "[uri]") {
    CHECK(uri::Search("") == "spotify:search:");
}

TEST_CASE("Search encodes UTF-8 byte-by-byte", "[uri]") {
    // "é" is 0xC3 0xA9 in UTF-8.
    CHECK(uri::Search("\xC3\xA9") == "spotify:search:%C3%A9");
}
