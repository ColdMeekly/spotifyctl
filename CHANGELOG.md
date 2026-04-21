# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.3.4] — 2026-04-21

### Fixed
- `LatestPositionSmooth()` / `spotifyctl_latest_position_smooth_ms` / Python
  `position_smooth_ms` / Node `positionSmoothMs` no longer step visibly on
  every SMTC republish. Each SMTC timeline update carries the current
  position with tens-of-ms async delay, and the previous implementation
  re-anchored on every delta — producing a ~1 Hz visible jump during steady
  playback. The anchor now only advances across real discontinuities
  (play/pause, seek, track change); small skew between SMTC's clock and the
  monotonic clock is absorbed, so polling the smooth position at 30–60 Hz
  produces a steady progress bar. `OnPositionChanged` ticks deliver the same
  smoothed value.
- `LatestPositionSmooth()` no longer returns a bogus "system uptime" value
  when status flips to Playing via the window-title parser before SMTC has
  published (previously the anchor was a default-constructed `time_point`
  at epoch 0, so `now − 0` yielded many minutes of phantom elapsed time).
  Returns 0 until SMTC publishes on a fresh client.

## [0.3.3] — 2026-04-21

### Removed
- Bindings no longer honour the `LIBSPOTIFYCTL_DLL` environment variable.
  Both the Python and Node bindings now load exclusively from the bundled
  `_prebuilt/` (Python) / `prebuilt/` (Node) directory and raise a clear
  error if the DLL is missing. The Node binding previously consulted the
  env var *first*, so any process able to set it before `require()` could
  load an arbitrary DLL even when a bundled copy was present — that path
  is gone. Dev workflow is unchanged in shape: build the C++ DLL and copy
  it into the binding's prebuilt directory (still gitignored).

## [0.3.2] — 2026-04-21

### Fixed
- CI: bumped the npm-publish job to Node 24 so npm ≥ 11.5.1 ships in the
  runner. npm OIDC trusted publishing requires that version; on Node 20
  (npm 10.x) the publish PUT was rejected as a 404 (npm masks auth failures
  this way), so v0.3.1 never reached npm. The library code is unchanged
  from 0.3.1 — this release exists purely to ship the v0.3.1 metadata
  changes to npm. The npm registry will jump from `0.3.0` straight to
  `0.3.2`; PyPI keeps the full sequence including `0.3.1`.

## [0.3.1] — 2026-04-21

### Changed
- Project homepage now points at https://spotifyctl.cldmk.ly (PyPI, npm, and
  CMake metadata). Source / Issues URLs unchanged.
- PyPI metadata: added a `Changelog` entry under `[project.urls]` and a
  handful of additional classifiers (Win10/11, Console, library subcategories)
  to improve discoverability.
- Node `package.json` keywords expanded to align with the Python keyword set
  (`spotifyctl`, `media-controls`, `audio`, `cli`, `koffi`, `automation`,
  `win32`).

### Fixed
- `include/spotify/version.h` (the static fallback used by the MSBuild path)
  now reports the correct project version. Previously stuck at `0.2.0`.

## [0.3.0] — 2026-04-20

### Added
- `SpotifyClient::LatestPositionSmooth()` — playback position with monotonic-
  clock extrapolation between SMTC updates. Replaces the per-caller boilerplate
  in every "make the progress bar smooth" consumer. C ABI:
  `spotifyctl_latest_position_smooth_ms`. Python: `client.position_smooth_ms`.
  Node: `client.positionSmoothMs`.
- Four new signals next to `OnStateChanged`:
  - `OnTrackChanged(previous, current)` — fires when the `(artist, title,
    album)` tuple changes, including empty→populated at startup.
  - `OnAdStarted` / `OnAdEnded` — edges on the existing `isAd` flag.
  - `OnPositionChanged` — ~1 Hz tick while `status == Playing` and at least
    one slot is connected (zero cost when nobody subscribes).
- `Signal::ConnectAndReplay(slot, replayArgs...)` — atomic subscribe + one
  synchronous replay under the signal lock, so late subscribers don't race
  against `LatestState()` and `Emit` cannot interleave.
  - C ABI: `spotifyctl_on_state_changed_with_replay`.
  - Python: `client.on_state_changed(cb, replay=True)`.
  - Node: `stateChanged` listeners automatically get the current snapshot on
    first attach — matches `EventEmitter` user expectations.
- `spotifyctl events [--position]` CLI subcommand — typed NDJSON edge stream
  (`track_changed`, `ad_started`, `ad_ended`, optional `position`). Additive
  to `watch`, which is unchanged.
- ASan build preset (`debug-asan`) and matching CI job — ctest under
  `/fsanitize=address` on every push.

### Changed
- `PlaybackState::operator==` now compares album-art size plus a cached
  64-bit hash instead of the raw bytes, turning the steady-state republish
  short-circuit from a ~200 KB `memcmp` per SMTC tick into an 8-byte compare.
  Collision probability is negligible (~10⁻¹⁹) and the only consequence
  would be suppressing one `OnStateChanged` republish on art change — benign.
- `examples/now_playing` now calls `LatestPositionSmooth()` directly. Fixes a
  latent bug where the hand-rolled extrapolation re-anchored on any
  `OnStateChanged`, snapping the counter backwards on unrelated field
  updates (e.g. app-volume changes).

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

[Unreleased]: https://github.com/ColdMeekly/spotifyctl/compare/v0.3.3...HEAD
[0.3.3]: https://github.com/ColdMeekly/spotifyctl/compare/v0.3.2...v0.3.3
[0.3.2]: https://github.com/ColdMeekly/spotifyctl/compare/v0.3.1...v0.3.2
[0.3.1]: https://github.com/ColdMeekly/spotifyctl/compare/v0.3.0...v0.3.1
[0.3.0]: https://github.com/ColdMeekly/spotifyctl/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/ColdMeekly/spotifyctl/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/ColdMeekly/spotifyctl/releases/tag/v0.1.0
