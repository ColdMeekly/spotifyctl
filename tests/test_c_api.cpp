#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <string>
#include <string_view>

#include "spotify/c_api.h"

TEST_CASE("c_api: version string is non-empty and semver-ish", "[c_api]") {
    const char* v = spotifyctl_version();
    REQUIRE(v != nullptr);
    REQUIRE(std::strlen(v) > 0);
    // Expect at least one dot (e.g. "0.2.0").
    REQUIRE(std::string_view(v).find('.') != std::string_view::npos);
}

TEST_CASE("c_api: new/free lifecycle", "[c_api]") {
    spotifyctl_client* c = spotifyctl_new();
    REQUIRE(c != nullptr);
    REQUIRE(spotifyctl_is_running(c) == 0);
    spotifyctl_free(c);
}

TEST_CASE("c_api: latest_state on fresh client returns skeleton", "[c_api]") {
    spotifyctl_client* c = spotifyctl_new();
    REQUIRE(c != nullptr);

    spotifyctl_playback_state st{};
    REQUIRE(spotifyctl_latest_state(c, &st) == 1);

    REQUIRE(st.status == SPOTIFYCTL_STATUS_UNKNOWN);
    REQUIRE(st.artist != nullptr);
    REQUIRE(st.title  != nullptr);
    REQUIRE(st.album  != nullptr);
    REQUIRE(std::strlen(st.artist) == 0);
    REQUIRE(std::strlen(st.title)  == 0);
    REQUIRE(std::strlen(st.album)  == 0);
    REQUIRE(st.position_ms   == 0);
    REQUIRE(st.duration_ms   == 0);
    REQUIRE(st.album_art     == nullptr);
    REQUIRE(st.album_art_len == 0);
    REQUIRE(st.audible       == 0);

    spotifyctl_free(c);
}

TEST_CASE("c_api: latest_state_json produces a valid JSON object", "[c_api]") {
    spotifyctl_client* c = spotifyctl_new();
    REQUIRE(c != nullptr);

    // Size query.
    size_t needed = spotifyctl_latest_state_json(c, nullptr, 0);
    REQUIRE(needed > 2);

    std::string buf(needed + 1, '\0');
    size_t wrote = spotifyctl_latest_state_json(c, buf.data(), buf.size());
    REQUIRE(wrote == needed);
    REQUIRE(buf.front() == '{');
    REQUIRE(buf[needed - 1] == '}');
    REQUIRE(buf.find("\"status\"") != std::string::npos);
    REQUIRE(buf.find("\"artist\"") != std::string::npos);

    // Truncation path: buffer too small still NUL-terminates.
    char tiny[4];
    size_t wrote2 = spotifyctl_latest_state_json(c, tiny, sizeof(tiny));
    REQUIRE(wrote2 == needed);
    REQUIRE(tiny[sizeof(tiny) - 1] == '\0');

    spotifyctl_free(c);
}

TEST_CASE("c_api: controls no-op when Spotify is not running", "[c_api]") {
    spotifyctl_client* c = spotifyctl_new();
    REQUIRE(c != nullptr);
    REQUIRE(spotifyctl_is_running(c) == 0);

    // These should all fail gracefully (return 0) without crashing.
    REQUIRE(spotifyctl_play(c)                            == 0);
    REQUIRE(spotifyctl_pause(c)                           == 0);
    REQUIRE(spotifyctl_next(c)                            == 0);
    REQUIRE(spotifyctl_previous(c)                        == 0);
    REQUIRE(spotifyctl_seek_ms(c, 1000)                   == 0);
    REQUIRE(spotifyctl_send_command(c, SPOTIFYCTL_CMD_PLAY_PAUSE) == 0);
    REQUIRE(spotifyctl_send_key(c, 0)                     == 0);

    spotifyctl_free(c);
}

TEST_CASE("c_api: URI builders round-trip", "[c_api]") {
    char* t = spotifyctl_uri_track("abc123");
    REQUIRE(t != nullptr);
    REQUIRE(std::string_view(t) == "spotify:track:abc123");
    spotifyctl_free_string(t);

    char* al = spotifyctl_uri_album("xyz");
    REQUIRE(al != nullptr);
    REQUIRE(std::string_view(al) == "spotify:album:xyz");
    spotifyctl_free_string(al);

    char* pl = spotifyctl_uri_playlist("37i9dQ");
    REQUIRE(std::string_view(pl) == "spotify:playlist:37i9dQ");
    spotifyctl_free_string(pl);

    char* ar = spotifyctl_uri_artist("4Z8W4fKeB5YxbusRsdQVPb");
    REQUIRE(std::string_view(ar) == "spotify:artist:4Z8W4fKeB5YxbusRsdQVPb");
    spotifyctl_free_string(ar);

    char* u = spotifyctl_uri_user("spotify");
    REQUIRE(std::string_view(u) == "spotify:user:spotify");
    spotifyctl_free_string(u);

    char* s = spotifyctl_uri_search("dark side of the moon");
    REQUIRE(s != nullptr);
    // Spaces must be percent-encoded.
    REQUIRE(std::string_view(s).find(' ') == std::string_view::npos);
    REQUIRE(std::string_view(s).substr(0, 16) == "spotify:search:d");
    spotifyctl_free_string(s);
}

TEST_CASE("c_api: disconnect with zero or unknown token is a no-op", "[c_api]") {
    spotifyctl_client* c = spotifyctl_new();
    REQUIRE(c != nullptr);
    spotifyctl_disconnect(c, 0);
    spotifyctl_disconnect(c, 9999);
    spotifyctl_free(c);
}
