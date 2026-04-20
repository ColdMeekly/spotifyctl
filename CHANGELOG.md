# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.2.0] — 2026-04-20

### Added
- Stable C ABI (`include/spotify/c_api.h`, `src/c_api.cpp`) — opaque handle,
  UTF-8 strings, `size_t` tokens, JSON snapshot helper. One bridge for every
  language binding.
- `spotifyctl` command-line tool (`cli/`) — `now-playing`, `watch`, transport,
  `seek`, `volume`, `mute`, `url`, `search`. Supports `--json` and `--tsv`.
- Python bindings (`bindings/python/`) — ctypes wrapper published as
  `libspotifyctl` on PyPI. Wheel tag `py3-none-win_amd64`.
- Node.js bindings (`bindings/node/`) — koffi wrapper published as
  `libspotifyctl` on NPM. `SpotifyClient` extends `EventEmitter`.
- CMake install/export — `find_package(libspotifyctl CONFIG)` resolves after
  `cmake --install`. Target `spotifyctl`, alias `spotifyctl::spotifyctl`,
  `BUILD_SHARED_LIBS`-aware, generated `spotify/export.h` + `spotify/version.h`.
- `CMakePresets.json` with `debug`, `release`, `release-shared`, `release-install`.
- Industry-standard housekeeping: `CONTRIBUTING.md`, `CODE_OF_CONDUCT.md`,
  `.clang-format`, `.editorconfig`, `.gitattributes`, issue/PR templates,
  dependabot, per-binding READMEs, `docs/`.

### Changed
- CMake target renamed `libspotifyctl` → `spotifyctl` (alias
  `spotifyctl::spotifyctl`). File name is pinned to `libspotifyctl.{lib,dll}`
  via `OUTPUT_NAME`, so downstream consumers keep the same brand.
- CI now builds static + shared, packs a Python wheel and an NPM tarball, and
  publishes to PyPI + NPM on tag push.

## [0.1.0] — 2025-11-10

### Added
- Initial release: C++20 `SpotifyClient`, SMTC + Core Audio + window-title
  fusion, unified `PlaybackState`, transport controls, URI builders, Catch2
  test suite, `rickroll` and `now_playing` example programs.

[Unreleased]: https://github.com/ColdMeekly/spotifyctl/compare/v0.2.0...HEAD
[0.2.0]: https://github.com/ColdMeekly/spotifyctl/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/ColdMeekly/spotifyctl/releases/tag/v0.1.0
