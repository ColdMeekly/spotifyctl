#pragma once

// This file is the static fallback used by the Visual Studio solution path.
// When the CMake build is used, CMake generates a version of this file from
// version.h.in into the build tree and that one is included instead — the
// build-interface include directory is listed before the source one.

#define LIBSPOTIFYCTL_VERSION_MAJOR 0
#define LIBSPOTIFYCTL_VERSION_MINOR 2
#define LIBSPOTIFYCTL_VERSION_PATCH 0
#define LIBSPOTIFYCTL_VERSION_STRING "0.2.0"

#ifdef __cplusplus
namespace spotify {
constexpr const char* Version() noexcept { return LIBSPOTIFYCTL_VERSION_STRING; }
}  // namespace spotify
#endif
