cmake_minimum_required(VERSION 3.21)
project(libspotifyctl
    VERSION 0.3.1
    DESCRIPTION "Offline (no Web API) library for reading from and controlling Spotify on Windows"
    HOMEPAGE_URL "https://spotifyctl.cldmk.ly"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT WIN32)
    message(FATAL_ERROR "libspotifyctl currently targets Windows only.")
endif()

# Default to STATIC unless the caller asks for SHARED explicitly.
option(BUILD_SHARED_LIBS "Build libspotifyctl as a DLL (otherwise STATIC)" OFF)

# --------------------------------------------------------------------------
# AddressSanitizer (MSVC /fsanitize=address). Intended for CI / local bug
# hunts, not for shipping artifacts. Incompatible with /RTC1 so we strip
# those flags from the Debug configuration when ASan is on.
# --------------------------------------------------------------------------
option(LIBSPOTIFYCTL_ENABLE_ASAN "Enable AddressSanitizer (MSVC /fsanitize=address)" OFF)
if(LIBSPOTIFYCTL_ENABLE_ASAN)
    if(NOT MSVC)
        message(FATAL_ERROR "LIBSPOTIFYCTL_ENABLE_ASAN currently only supports MSVC.")
    endif()
    foreach(_cfg CMAKE_C_FLAGS_DEBUG CMAKE_CXX_FLAGS_DEBUG
                 CMAKE_C_FLAGS_RELWITHDEBINFO CMAKE_CXX_FLAGS_RELWITHDEBINFO)
        string(REGEX REPLACE "/RTC[^ ]*" "" ${_cfg} "${${_cfg}}")
    endforeach()
    add_compile_options(/fsanitize=address /Zi)
    # Debug info helps ASan symbolize stack traces.
    add_link_options(/DEBUG)
endif()

include(GNUInstallDirs)
include(GenerateExportHeader)
include(CMakePackageConfigHelpers)

# --------------------------------------------------------------------------
# Core library
# --------------------------------------------------------------------------
set(_spotifyctl_sources
    src/client_win32.cpp
    src/control.cpp
    src/c_api.cpp
)
foreach(_src IN ITEMS
        src/smtc_source.cpp
        src/audio_session.cpp
        src/title_parser.cpp)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_src}")
        list(APPEND _spotifyctl_sources "${_src}")
    endif()
endforeach()

add_library(spotifyctl ${_spotifyctl_sources})
add_library(spotifyctl::spotifyctl ALIAS spotifyctl)

# Output file keeps the brand: libspotifyctl.{lib,dll}
set_target_properties(spotifyctl PROPERTIES
    OUTPUT_NAME       libspotifyctl
    DEBUG_POSTFIX     "d"
    VERSION           ${PROJECT_VERSION}
    SOVERSION         ${PROJECT_VERSION_MAJOR}
    WINDOWS_EXPORT_ALL_SYMBOLS OFF
)

# Generated headers land under build/include/spotify/ and are installed.
set(_spotifyctl_generated_dir "${CMAKE_CURRENT_BINARY_DIR}/include/spotify")
file(MAKE_DIRECTORY "${_spotifyctl_generated_dir}")

generate_export_header(spotifyctl
    BASE_NAME        SPOTIFYCTL
    EXPORT_MACRO_NAME SPOTIFYCTL_API
    EXPORT_FILE_NAME "${_spotifyctl_generated_dir}/export.h")

if(NOT BUILD_SHARED_LIBS)
    target_compile_definitions(spotifyctl PUBLIC SPOTIFYCTL_STATIC_DEFINE)
endif()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/include/spotify/version.h.in"
    "${_spotifyctl_generated_dir}/version.h"
    @ONLY)

target_include_directories(spotifyctl
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(spotifyctl PUBLIC
    user32
    shell32
    psapi
    runtimeobject
    ole32)

if(MSVC)
    target_compile_options(spotifyctl PRIVATE /W4 /permissive- /await:strict)
    target_compile_definitions(spotifyctl PRIVATE
        UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
        _WIN32_WINNT=0x0A00)
endif()

# --------------------------------------------------------------------------
# Examples
# --------------------------------------------------------------------------
option(LIBSPOTIFYCTL_BUILD_EXAMPLES "Build example programs" ON)
if(LIBSPOTIFYCTL_BUILD_EXAMPLES)
    # rickroll / now_playing use internal C++ APIs (Signal, string_view params)
    # that are not part of the DLL ABI — only build them against the static
    # archive.
    if(NOT BUILD_SHARED_LIBS)
        set(_examples rickroll)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/now_playing/main.cpp")
            list(APPEND _examples now_playing)
        endif()
        foreach(_t IN LISTS _examples)
            add_executable(${_t} examples/${_t}/main.cpp)
            target_link_libraries(${_t} PRIVATE spotifyctl)
            if(MSVC)
                target_compile_definitions(${_t} PRIVATE
                    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
                    _WIN32_WINNT=0x0A00)
            endif()
        endforeach()
    endif()

    # Pure-C smoke example uses only the exported C ABI — works against both
    # the static archive and the DLL.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/c_smoke/main.c")
        add_executable(c_smoke examples/c_smoke/main.c)
        target_link_libraries(c_smoke PRIVATE spotifyctl)
    endif()
endif()

# --------------------------------------------------------------------------
# CLI (spotifyctl.exe)  — added in 7c
# --------------------------------------------------------------------------
option(LIBSPOTIFYCTL_BUILD_CLI "Build the spotifyctl CLI" ON)
if(LIBSPOTIFYCTL_BUILD_CLI AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cli/main.cpp")
    add_executable(spotifyctl_cli cli/main.cpp)
    set_target_properties(spotifyctl_cli PROPERTIES OUTPUT_NAME spotifyctl)
    target_link_libraries(spotifyctl_cli PRIVATE spotifyctl)
    if(MSVC)
        target_compile_definitions(spotifyctl_cli PRIVATE
            UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
            _WIN32_WINNT=0x0A00)
    endif()
endif()

# --------------------------------------------------------------------------
# Tests
# --------------------------------------------------------------------------
option(LIBSPOTIFYCTL_BUILD_TESTS "Build tests (Catch2 via FetchContent)" ON)
# Tests exercise internal C++ APIs (Signal, title parser, URI builders) that
# are not part of the DLL ABI — link against the static archive only.
if(LIBSPOTIFYCTL_BUILD_TESTS
        AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt"
        AND NOT BUILD_SHARED_LIBS)
    enable_testing()
    add_subdirectory(tests)
endif()

# --------------------------------------------------------------------------
# Install / export — enables `find_package(libspotifyctl CONFIG)`
# --------------------------------------------------------------------------
option(LIBSPOTIFYCTL_INSTALL "Install libspotifyctl + CMake package files" ON)
if(LIBSPOTIFYCTL_INSTALL)
    set(_install_cmakedir ${CMAKE_INSTALL_LIBDIR}/cmake/libspotifyctl)

    install(TARGETS spotifyctl
        EXPORT  libspotifyctlTargets
        ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

    install(DIRECTORY include/spotify
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING
            PATTERN "*.h"
            PATTERN "*.h.in" EXCLUDE)

    install(FILES
            "${_spotifyctl_generated_dir}/export.h"
            "${_spotifyctl_generated_dir}/version.h"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/spotify)

    if(LIBSPOTIFYCTL_BUILD_CLI AND TARGET spotifyctl_cli)
        install(TARGETS spotifyctl_cli
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()

    install(EXPORT libspotifyctlTargets
        FILE      libspotifyctlTargets.cmake
        NAMESPACE spotifyctl::
        DESTINATION ${_install_cmakedir})

    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/libspotifyctlConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/libspotifyctlConfig.cmake"
        INSTALL_DESTINATION ${_install_cmakedir})

    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/libspotifyctlConfigVersion.cmake"
        VERSION       ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion)

    install(FILES
            "${CMAKE_CURRENT_BINARY_DIR}/libspotifyctlConfig.cmake"
            "${CMAKE_CURRENT_BINARY_DIR}/libspotifyctlConfigVersion.cmake"
        DESTINATION ${_install_cmakedir})
endif()
