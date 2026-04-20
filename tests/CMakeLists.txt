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

add_executable(spotify_tests
    test_title_parser.cpp
    test_signal.cpp
    test_uri_builders.cpp
)

target_link_libraries(spotify_tests PRIVATE
    spotify_control
    Catch2::Catch2WithMain
)

if(MSVC)
    target_compile_definitions(spotify_tests PRIVATE
        UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
        _WIN32_WINNT=0x0A00)
endif()

catch_discover_tests(spotify_tests)
