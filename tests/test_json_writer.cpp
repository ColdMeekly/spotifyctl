#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>

#include "spotify/playback.h"
#include "json_writer.h"

using spotify::PlaybackState;
using spotify::json::AppendEscaped;
using spotify::json::StatusName;
using spotify::json::WriteStateJson;

namespace {

bool Contains(const std::string& hay, const char* needle) {
    return hay.find(needle) != std::string::npos;
}

}  // namespace

TEST_CASE("StatusName covers every enumerator", "[json_writer]") {
    CHECK(std::string(StatusName(PlaybackState::Status::Unknown))       == "unknown");
    CHECK(std::string(StatusName(PlaybackState::Status::Stopped))       == "stopped");
    CHECK(std::string(StatusName(PlaybackState::Status::Paused))        == "paused");
    CHECK(std::string(StatusName(PlaybackState::Status::Playing))       == "playing");
    CHECK(std::string(StatusName(PlaybackState::Status::ChangingTrack)) == "changing_track");
}

TEST_CASE("AppendEscaped escapes JSON metacharacters", "[json_writer]") {
    std::string out;
    AppendEscaped(out, std::string_view("quote\"slash\\newline\ntab\tbell\b"));
    CHECK(out == "\"quote\\\"slash\\\\newline\\ntab\\tbell\\b\"");
}

TEST_CASE("AppendEscaped \\u-escapes control chars below 0x20", "[json_writer]") {
    std::string out;
    const char raw[] = {'A', 0x01, 0x1f, 'B', '\0'};
    AppendEscaped(out, std::string_view(raw, 4));
    CHECK(out == "\"A\\u0001\\u001fB\"");
}

TEST_CASE("AppendEscaped passes UTF-8 bytes through untouched", "[json_writer]") {
    // \xE2\x80\x94 = em dash, \xF0\x9F\x8E\xB5 = musical note U+1F3B5.
    std::string out;
    AppendEscaped(out, std::string_view("caf\xC3\xA9 \xE2\x80\x94 \xF0\x9F\x8E\xB5"));
    CHECK(out == "\"caf\xC3\xA9 \xE2\x80\x94 \xF0\x9F\x8E\xB5\"");
}

TEST_CASE("WriteStateJson emits every field of a default state", "[json_writer]") {
    PlaybackState s;  // all defaults
    const std::string j = WriteStateJson(s);

    CHECK(Contains(j, "\"status\":\"unknown\""));
    CHECK(Contains(j, "\"artist\":\"\""));
    CHECK(Contains(j, "\"title\":\"\""));
    CHECK(Contains(j, "\"album\":\"\""));
    CHECK(Contains(j, "\"position_ms\":0"));
    CHECK(Contains(j, "\"duration_ms\":0"));
    CHECK(Contains(j, "\"album_art_len\":0"));
    CHECK(Contains(j, "\"can_seek\":false"));
    CHECK(Contains(j, "\"can_skip_next\":false"));
    CHECK(Contains(j, "\"can_skip_prev\":false"));
    CHECK(Contains(j, "\"is_ad\":false"));
    CHECK(Contains(j, "\"audible\":false"));
    CHECK(Contains(j, "\"app_muted\":false"));
    // appVolume == -1.0f is the "unresolved" sentinel → null.
    CHECK(Contains(j, "\"app_volume\":null"));
    CHECK(j.front() == '{');
    CHECK(j.back()  == '}');
}

TEST_CASE("WriteStateJson serializes a fully populated state", "[json_writer]") {
    PlaybackState s;
    s.status       = PlaybackState::Status::Playing;
    s.artist       = "Rick Astley";
    s.title        = "Never Gonna Give You Up";
    s.album        = "Whenever You Need Somebody";
    s.position     = std::chrono::milliseconds(123456);
    s.duration     = std::chrono::milliseconds(213000);
    s.albumArt     = std::vector<std::byte>(1024, std::byte{0xFF});
    s.canSeek      = true;
    s.canSkipNext  = true;
    s.canSkipPrev  = false;
    s.isAd         = false;
    s.audible      = true;
    s.appMuted     = false;
    s.appVolume    = 0.5f;

    const std::string j = WriteStateJson(s);

    CHECK(Contains(j, "\"status\":\"playing\""));
    CHECK(Contains(j, "\"artist\":\"Rick Astley\""));
    CHECK(Contains(j, "\"title\":\"Never Gonna Give You Up\""));
    CHECK(Contains(j, "\"album\":\"Whenever You Need Somebody\""));
    CHECK(Contains(j, "\"position_ms\":123456"));
    CHECK(Contains(j, "\"duration_ms\":213000"));
    CHECK(Contains(j, "\"album_art_len\":1024"));
    CHECK(Contains(j, "\"can_seek\":true"));
    CHECK(Contains(j, "\"can_skip_next\":true"));
    CHECK(Contains(j, "\"can_skip_prev\":false"));
    CHECK(Contains(j, "\"audible\":true"));
    // Volume serialized with 4-decimal precision, no leading sign.
    CHECK(Contains(j, "\"app_volume\":0.5000"));
}

TEST_CASE("WriteStateJson escapes strings in artist/title/album", "[json_writer]") {
    PlaybackState s;
    s.artist = "Weird \"quotes\"";
    s.title  = "back\\slash\nnewline";
    s.album  = "tab\there";
    const std::string j = WriteStateJson(s);

    CHECK(Contains(j, "\"artist\":\"Weird \\\"quotes\\\"\""));
    CHECK(Contains(j, "\"title\":\"back\\\\slash\\nnewline\""));
    CHECK(Contains(j, "\"album\":\"tab\\there\""));
}

TEST_CASE("WriteStateJson handles extreme durations", "[json_writer]") {
    PlaybackState s;
    s.position = std::chrono::milliseconds(-1);              // sentinel-ish negative
    s.duration = std::chrono::milliseconds(1'000'000'000);   // > i32 seconds
    const std::string j = WriteStateJson(s);

    CHECK(Contains(j, "\"position_ms\":-1"));
    CHECK(Contains(j, "\"duration_ms\":1000000000"));
}

TEST_CASE("WriteStateJson reports album_art length without bytes", "[json_writer]") {
    PlaybackState s;
    s.albumArt.assign(42, std::byte{0xAB});
    const std::string j = WriteStateJson(s);
    CHECK(Contains(j, "\"album_art_len\":42"));
    // Bytes themselves are intentionally not in the JSON.
    CHECK_FALSE(Contains(j, "album_art\":\""));
}

TEST_CASE("WriteStateJson clamps appVolume null sentinel at 0", "[json_writer]") {
    // 0.0 is a valid volume (silence), not the unresolved sentinel.
    PlaybackState s;
    s.appVolume = 0.0f;
    const std::string j = WriteStateJson(s);
    CHECK(Contains(j, "\"app_volume\":0.0000"));
    CHECK_FALSE(Contains(j, "\"app_volume\":null"));
}
