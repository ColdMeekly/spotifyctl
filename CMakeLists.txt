cmake_minimum_required(VERSION 3.20)
project(spotify_control
    VERSION 0.1.0
    DESCRIPTION "Offline (no Web API) library for reading from and controlling Spotify on Windows"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "spotify_control currently targets Windows only.")
endif()

# --------------------------------------------------------------------------
# Core library
# --------------------------------------------------------------------------
set(_spotify_sources
    src/client_win32.cpp
    src/control.cpp
)
foreach(_src IN ITEMS src/smtc_source.cpp src/audio_session.cpp src/title_parser.cpp)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_src}")
        list(APPEND _spotify_sources "${_src}")
    endif()
endforeach()

add_library(spotify_control STATIC ${_spotify_sources})
add_library(spotify::control ALIAS spotify_control)

target_include_directories(spotify_control
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(spotify_control PUBLIC
    user32
    shell32
    psapi
    runtimeobject   # WinRT (Phase 3)
    ole32           # CoInitialize / Core Audio
)

if(MSVC)
    target_compile_options(spotify_control PRIVATE /W4 /permissive- /await:strict)
    target_compile_definitions(spotify_control PRIVATE
        UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
        _WIN32_WINNT=0x0A00    # Windows 10
    )
endif()

# --------------------------------------------------------------------------
# Examples
# --------------------------------------------------------------------------
option(SPOTIFY_CONTROL_BUILD_EXAMPLES "Build example programs" ON)
if(SPOTIFY_CONTROL_BUILD_EXAMPLES)
    set(_examples rickroll)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/now_playing/main.cpp")
        list(APPEND _examples now_playing)
    endif()
    foreach(_t IN LISTS _examples)
        add_executable(${_t} examples/${_t}/main.cpp)
        target_link_libraries(${_t} PRIVATE spotify_control)
        if(MSVC)
            target_compile_definitions(${_t} PRIVATE
                UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
                _WIN32_WINNT=0x0A00)
        endif()
    endforeach()
endif()

# --------------------------------------------------------------------------
# Tests
# --------------------------------------------------------------------------
option(SPOTIFY_CONTROL_BUILD_TESTS "Build tests (Catch2 via FetchContent)" ON)
if(SPOTIFY_CONTROL_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    enable_testing()
    add_subdirectory(tests)
endif()
