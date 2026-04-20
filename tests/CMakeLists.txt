include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.3
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(Catch2)
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)

add_executable(libspotifyctl_tests
    test_title_parser.cpp
    test_signal.cpp
    test_uri_builders.cpp
    test_c_api.cpp
    test_json_writer.cpp
    test_aggregator.cpp
    test_position.cpp
)

target_link_libraries(libspotifyctl_tests PRIVATE
    spotifyctl
    Catch2::Catch2WithMain
)

# Tests exercise a couple of internal helpers (json_writer) that live under
# src/ and are not installed as public headers.
target_include_directories(libspotifyctl_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src)

if(MSVC)
    target_compile_definitions(libspotifyctl_tests PRIVATE
        UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
        _WIN32_WINNT=0x0A00)
endif()

catch_discover_tests(libspotifyctl_tests)
