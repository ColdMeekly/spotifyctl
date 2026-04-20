# libspotifyctl

A Windows library and command-line tool for reading state from and controlling
the Spotify desktop application entirely offline. No Spotify Web API, no OAuth,
no client id, no network calls — built on SMTC and Core Audio.

Ships as:

- a native **C++20** library (`spotifyctl::spotifyctl`)
- a stable **C ABI** (`libspotifyctl.dll` — consumable from any language)
- a **`spotifyctl`** command-line tool
- a **Python** package (`pip install libspotifyctl`)
- a **Node.js** package (`npm install libspotifyctl`)

The library fuses three native Windows data sources into a single
`PlaybackState` event stream:

| Source | Contribution | Requires |
|---|---|---|
| Window-title hook (`SetWinEventHook`) | artist / title, open & close edges | always |
| SMTC (`GlobalSystemMediaTransportControlsSessionManager`) | playback status, position, duration, album, album art, seek | Windows 10 1809+ |
| Core Audio (`IAudioSessionManager2`, `IAudioMeterInformation`) | per-app volume, mute, real audible-output detection | always |

---

## Language bindings

| Language | Install | Reference |
|---|---|---|
| C++ | `find_package(libspotifyctl CONFIG)` | [docs/api-cpp.md](docs/api-cpp.md) |
| C (any FFI) | link `libspotifyctl.dll`, include `<spotify/c_api.h>` | [docs/api-c.md](docs/api-c.md) |
| Python 3.9+ | `pip install libspotifyctl` | [docs/api-python.md](docs/api-python.md) |
| Node 18+ | `npm install libspotifyctl` | [docs/api-node.md](docs/api-node.md) |

Every binding exposes the same concepts: a `SpotifyClient` with start/stop,
a `PlaybackState` snapshot, a handful of state-change signals, transport
controls, per-app audio controls, and URI builders. Pick the language; the
shape is the same.

### Python

```python
from libspotifyctl import SpotifyClient

with SpotifyClient() as c:
    c.on_state_changed(lambda s: print(f"{s.artist} — {s.title}"))
    input("press enter to quit...\n")
```

### Node

```js
const { SpotifyClient } = require('libspotifyctl');
const c = new SpotifyClient();
c.on('stateChanged', s => console.log(`${s.artist} — ${s.title}`));
c.start();
```

---

## CLI

```sh
spotifyctl now-playing --json
spotifyctl watch              # streams NDJSON of state changes
spotifyctl seek 1:23
spotifyctl volume set 0.5
spotifyctl mute toggle
```

Full reference: [docs/cli.md](docs/cli.md).

---

## Quickstart (C++)

```cpp
#include <spotify/client.h>

#include <chrono>
#include <iostream>
#include <thread>

int main() {
    spotify::SpotifyClient spotify;

    spotify.OnStateChanged.connect([](const spotify::PlaybackState& s) {
        std::cout << s.artist << " - " << s.title << '\n';
    });

    spotify.Start();
    std::this_thread::sleep_for(std::chrono::minutes(10));
}
```

That is the entire integration. No polling, no hook plumbing. Callbacks fire on
Windows-owned background threads; do not block inside a slot.

---

## Reading state

There are two ways to read state:

1. **Push** — connect a slot to `OnStateChanged`. It fires whenever any field
   changes, with the full fused `PlaybackState` snapshot.
2. **Pull** — call `spotify.LatestState()` at any time from any thread.

Both paths return the same `PlaybackState`:

```cpp
namespace spotify {

struct PlaybackState {
    enum class Status { Unknown, Stopped, Paused, Playing, ChangingTrack };

    Status status;
    std::string artist, title, album;

    std::chrono::milliseconds position, duration;
    std::vector<std::byte>    albumArt;   // raw thumbnail bytes (JPEG)

    bool canSeek, canSkipNext, canSkipPrev;
    bool isAd;          // Spotify is playing an advertisement
    bool audible;       // real audio is reaching the endpoint

    float appVolume;    // [0,1], or -1 before the audio session resolves
    bool  appMuted;
};

}
```

Notes on individual fields:

- **`status`** comes from SMTC and follows Windows' own media-session state
  machine. `ChangingTrack` is Spotify flipping tracks; treat it like "about to
  be Playing."
- **`position` / `duration`** are delivered by SMTC events. Between events,
  `LatestState()` returns the last reported `position`; if you need a smooth
  counter, call `spotify.LatestPositionSmooth()` — it extrapolates from the
  last SMTC anchor via `steady_clock` while `status == Playing`, clamped to
  `duration`. `now_playing` uses this directly. Subscribe to
  `OnPositionChanged` for a ~1 Hz push stream of the same value.
- **`albumArt`** is the raw bytes of SMTC's thumbnail stream, typically JPEG.
  Do not assume a container format.
- **`canSeek` / `canSkipNext` / `canSkipPrev`** reflect SMTC's advertised
  capabilities, which track what Spotify currently permits (disabled for
  ads, early in a queue, etc.). Use them to drive UI affordances.
- **`isAd`** is best-effort: it is set when the window title equals
  `"Advertisement"`. SMTC may still carry the previous track's metadata
  during an ad, so prefer `isAd` over checking `title`.
- **`audible`** is driven by the Core Audio peak meter with hysteresis, so it
  distinguishes "playing-but-silent" (e.g., muted at the OS level) from
  "actually making sound." It is independent of `status`.
- **`appVolume`** is `-1.0f` until Spotify has produced audio at least once.
  Windows does not create a per-app audio session until the app first outputs
  sound; the library rebinds automatically.

### Events

| Signal | Fires when | Payload |
|---|---|---|
| `OnOpened` | the Spotify window is detected | — |
| `OnClosed` | the Spotify window goes away | — |
| `OnStateChanged` | any `PlaybackState` field changes | `const PlaybackState&` |
| `OnTrackChanged` | the `(artist, title, album)` tuple changes | `const PlaybackState& previous, current` |
| `OnAdStarted` | `isAd` false → true | — |
| `OnAdEnded` | `isAd` true → false | — |
| `OnAudibleChanged` | audible/silent edge (debounced ~1 s) | `bool audible` |
| `OnRawTitle` | the window title changes | `const std::string&` (UTF-8) |
| `OnPositionChanged` | ~1 Hz while Playing & ≥1 slot connected | `std::chrono::milliseconds` |

Every signal exposes `.connect(slot) -> Token` and `.disconnect(Token)`. The
signal type is thread-safe; slots are invoked on a stable snapshot, so a slot
may safely disconnect itself or another slot mid-emit.

For late subscribers to `OnStateChanged`, use `ConnectAndReplay` to receive
the current snapshot synchronously on connect — atomic with respect to any
concurrent `Emit`, so no value is dropped or duplicated:

```cpp
auto tok = spotify.OnStateChanged.ConnectAndReplay(
    [](const spotify::PlaybackState& s) { /* ... */ },
    spotify.LatestState());
```

---

## Controls

All methods return `bool` — `false` when the underlying channel is
unavailable (e.g., Spotify not running, audio session not yet bound, SMTC
unavailable on older Windows).

### Transport

```cpp
spotify.Play();
spotify.Pause();
spotify.Next();
spotify.Previous();
spotify.Seek(std::chrono::seconds(30));
```

`Play`, `Pause`, `Next`, and `Previous` prefer SMTC when available and fall
back to media keys (`WM_APPCOMMAND`). SMTC is the only path that supports
`Seek`; media keys cannot express a target position.

### Media-key channel

For callers that want the raw channel:

```cpp
spotify.SendCommand(spotify::AppCommand::PlayPause);
spotify.SendCommand(spotify::AppCommand::VolUp);
spotify.SendCommand(spotify::AppCommand::MuteUnmute);
```

`AppCommand` covers `Stop`, `Play`, `Pause`, `PlayPause`, `NextTrack`,
`PrevTrack`, `VolUp`, `VolDown`, `MuteUnmute`. Media-key volume / mute affects
the OS master level, not the per-app level.

### Per-app audio (Core Audio)

```cpp
float v = spotify.GetAppVolume();     // [0,1] or -1
spotify.SetAppVolume(0.25f);          // changes only Spotify's slider
bool m = spotify.IsAppMuted();
spotify.SetAppMuted(true);            // mute only Spotify
float peak = spotify.GetPeakAmplitude();  // [0,1] or -1, polled at 50 ms
```

These read and write the entries you see in Windows' Volume Mixer for
Spotify specifically.

### URIs

```cpp
spotify.OpenUri(spotify::uri::Track("4uLU6hMCjMI75M1A2tKUQC"));
spotify.OpenUri(spotify::uri::Album("1DFixLWuPkv3KT3TnV35m3"));
spotify.OpenUri(spotify::uri::Playlist("37i9dQZF1DXcBWIGoYBM5M"));
spotify.OpenUri(spotify::uri::Artist("0gxyHStUsqpMadRV0Di1Qt"));
spotify.OpenUri(spotify::uri::User("spotify"));
spotify.OpenUri(spotify::uri::Search("rickroll"));   // percent-encoded
```

`OpenUri` invokes `ShellExecute` on the URI, so anything that Windows knows
how to dispatch to Spotify works — including your own `spotify:...` strings.

### Raw key injection

```cpp
spotify.SendKey(VK_RETURN);   // posts WM_KEYDOWN to the Spotify window
```

Useful for niche automations such as dismissing modal dialogs.

---

## Example applications

Two example programs ship in `examples/`:

- **`rickroll`** — opens a track URI and presses Enter, as a minimal smoke
  test of `OpenUri` and `SendKey`.
- **`now_playing`** — a live console dashboard (30 Hz redraw) exercising
  every signal and every control. Keys: `Space` play/pause, `N`/`B` next/prev,
  arrows for volume and seek, `0`-`9` jump to 0..90 %, `M` per-app mute, `K`
  media-key mute, `I` save cover art, `S` prompt for a search, `R` open a
  sample playlist, `Q`/`Esc` quit.

---

## Build

### CMake (presets)

```sh
cmake --preset release
cmake --build --preset release
ctest --preset release --output-on-failure
```

Other presets: `debug`, `release-shared` (DLL + `spotifyctl.exe`),
`release-install` (adds `cmake --install`).

Static preset produces `libspotifyctl.lib`, `rickroll.exe`, `now_playing.exe`,
and `libspotifyctl_tests.exe`. Catch2 is pulled via `FetchContent` at configure
time, so tests are only available through the CMake path.

### Visual Studio

Open `libspotifyctl.sln`. It contains:

- **`libspotifyctl`** — the library
- **`rickroll`** / **`now_playing`** — C++ examples
- **`spotifyctl`** — the CLI

Outputs land in `bin\x64\{Debug,Release}\`.

### Consuming from CMake

After `cmake --install`:

```cmake
find_package(libspotifyctl CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE spotifyctl::spotifyctl)
```

Or in-tree:

```cmake
add_subdirectory(third_party/libspotifyctl)
target_link_libraries(my_app PRIVATE spotifyctl::spotifyctl)
```

Public headers live under `include/spotify/`. `<Windows.h>` is kept out of
every public header via PIMPL.

---

## Releases

Releases are cut from git tags only. Tag `vX.Y.Z` on `main` and CI produces
a stable GitHub release with the full artifact set, and also publishes to
PyPI (via OIDC trusted publishing) and NPM automatically:

- `libspotifyctl-static-<sha>.zip` — static `.lib` + headers + example exes
- `libspotifyctl-shared-<sha>.zip` — `.dll` + import `.lib` + `spotifyctl.exe` + install tree
- `libspotifyctl-<ver>-py3-none-win_amd64.whl` — Python wheel
- `libspotifyctl-<ver>.tgz` — NPM tarball

Every push / PR still runs the full CI matrix (static, shared, MSBuild,
Python wheel, Node tarball) — it just doesn't publish. Workflow artifacts
are available from the Actions tab if you need a bleeding-edge build.

Download the latest from the repository's
[Releases](../../releases) page.

---

## Threading

Signals fire on Windows-owned background threads:

- `OnOpened`, `OnClosed`, `OnRawTitle` — the WinEvent dispatcher thread
- `OnStateChanged` — whichever source thread produced the change (SMTC
  callback thread or audio-poll thread)
- `OnAudibleChanged` — audio-poll thread

Do not block inside a slot. If your application has its own event loop, post
to it and return quickly.

The signal implementation in `include/spotify/events.h` is thread-safe: slots
are captured into a snapshot before invocation.

---

## Limitations

- **SMTC requires Windows 10 1809 or newer.** On older builds, the library
  falls back to the window-title hook only — `position`, `duration`,
  `albumArt`, and seek are unavailable, and `status` is derived from title
  sentinels.
- **The per-app audio session only exists after Spotify first outputs sound.**
  `appVolume` reads `-1` and `SetAppVolume` / `SetAppMuted` return `false`
  until then. The library rebinds automatically.
- **Ad detection is title-based.** Spotify sets the window title to
  `"Advertisement"` during ads, but SMTC can still report the previous track
  during the transition. Check `isAd`, not `title`.
- **Title parsing handles the English UI only.** Localized idle strings are
  treated as unknown; the aggregator falls back to SMTC for `status`.

---

## License

MIT. See [LICENSE](LICENSE).
