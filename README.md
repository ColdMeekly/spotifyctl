# spotify-title-reader

Offline C++ library for reading from and controlling the Spotify desktop app on
Windows — **no** Spotify Web API, no OAuth, no client ID, no network calls.

Fuses three native Windows data sources:

| Source | What it gives you | Requires |
|---|---|---|
| Window-title hook (`SetWinEventHook`) | artist/title, open/close edges | always |
| SMTC (`GlobalSystemMediaTransportControlsSessionManager`) | status, position, duration, album, album art, seek | Win10 1809+ |
| Core Audio (`IAudioSessionManager2`, `IAudioMeterInformation`) | per-app volume, mute, real audible-output detection | always |

## Quickstart

```cpp
#include <spotify/client.h>
#include <chrono>
#include <iostream>
#include <thread>

int main() {
    spotify::SpotifyClient spotify;

    spotify.OnStateChanged.connect([](const spotify::PlaybackState& s) {
        std::cout << s.artist << " - " << s.title
                  << "  [" << s.position.count() << " / " << s.duration.count() << " ms]\n";
    });

    spotify.Start();
    std::this_thread::sleep_for(std::chrono::minutes(10));
}
```

That's the whole integration. No polling, no manual hook plumbing.

## Build

### CMake (recommended)

```sh
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Produces `spotify_control.lib`, `rickroll.exe`, `now_playing.exe`, and
`spotify_tests.exe`. Catch2 is fetched via `FetchContent` at configure time.

### Visual Studio solution

Open `spotify_control.sln`. It holds three projects:

- `spotify_control` — the static library
- `rickroll` — the legacy demo (depends on `spotify_control`)
- `now_playing` — the live dashboard (depends on `spotify_control`)

Outputs land in `bin\x64\{Debug,Release}\`. The solution is guarded by CI
alongside the CMake build.

Tests (`spotify_tests`) are only wired up in the CMake path because Catch2 is
pulled in via `FetchContent`.

## Releases

Every push to `main` produces a GitHub pre-release tagged
`build-<short-sha>` with a zip containing the library, public headers, both
demo executables, and a `VERSION.txt`. Grab the latest from the repo's
[Releases](../../releases) page.

## Consuming from CMake

```cmake
add_subdirectory(third_party/spotify-title-reader)
target_link_libraries(my_app PRIVATE spotify::control)
```

Public headers live under `include/spotify/`. `<Windows.h>` is kept out of
every public header via PIMPL.

## Controls

```cpp
spotify.Play();
spotify.Pause();
spotify.Next();
spotify.Previous();
spotify.Seek(std::chrono::seconds(30));   // SMTC-only, unlike WM_APPCOMMAND
spotify.SetAppVolume(0.25f);              // affects only Spotify
spotify.SetAppMuted(true);                // affects only Spotify
spotify.OpenUri(spotify::uri::Track("4uLU6hMCjMI75M1A2tKUQC"));
```

`Play`/`Pause`/`Next`/`Previous` prefer SMTC when available and fall back to
`WM_APPCOMMAND` media keys. `Seek` requires SMTC — media keys can't express it.

## Threading

Signals fire on Windows-owned background threads (the WinEvent dispatcher, a
SMTC callback thread, or the audio-poll thread). **Do not block inside slots.**
If your application has its own loop, post the state to it and return quickly.

The signal implementation in `include/spotify/events.h` is thread-safe: slots
are captured into a snapshot before invocation, so a slot can safely disconnect
itself or other slots mid-emit.

## Caveats

- **SMTC requires Win10 1809+.** Older builds get a degraded experience (title
  hook only — no position, duration, or album art).
- The Core Audio session for Spotify doesn't exist until Spotify has produced
  audio at least once. `GetAppVolume()` returns `-1` until then; the library
  re-resolves automatically.
- "Artist - Track" parsing falls back to `Status::Unknown` for localized idle
  strings the parser doesn't know (contributions welcome in
  `src/title_parser.cpp`).
- Ad detection is best-effort: Spotify sets the window title to
  `"Advertisement"` during ads, but SMTC may still carry the previous track's
  metadata. The aggregator surfaces `isAd = true` and trusts SMTC for
  `status`.

## License

MIT — see [LICENSE](LICENSE).
