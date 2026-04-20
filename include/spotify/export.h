#pragma once

// Static fallback for the Visual Studio solution path. CMake's
// generate_export_header emits a file by the same name into the build tree
// that overrides this one (the build-interface include directory is listed
// before the source one).

#ifndef SPOTIFYCTL_API
#  if defined(_WIN32) && !defined(SPOTIFYCTL_STATIC_DEFINE)
#    ifdef spotifyctl_EXPORTS
#      define SPOTIFYCTL_API __declspec(dllexport)
#    else
#      define SPOTIFYCTL_API __declspec(dllimport)
#    endif
#  else
#    define SPOTIFYCTL_API
#  endif
#endif

#ifndef SPOTIFYCTL_NO_EXPORT
#  define SPOTIFYCTL_NO_EXPORT
#endif

#ifndef SPOTIFYCTL_DEPRECATED
#  if defined(_MSC_VER)
#    define SPOTIFYCTL_DEPRECATED __declspec(deprecated)
#  else
#    define SPOTIFYCTL_DEPRECATED __attribute__((__deprecated__))
#  endif
#endif
