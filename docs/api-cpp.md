# C++ API

The native C++ surface lives under `include/spotify/`. It is the richest of the
four APIs — callbacks, state, and controls share `std::chrono`, `std::string`,
and `std::vector<std::byte>` types directly.

## Public headers

| Header | Contents |
|---|---|
| `spotify/client.h` | `SpotifyClient` — the entry point. |
| `spotify/playback.h` | `PlaybackState` — the unified snapshot struct. |
| `spotify/control.h` | `AppCommand` enum + `uri::*` builders. |
| `spotify/events.h` | `Signal<void(Args...)>` — thread-safe observer. |
| `spotify/export.h` (generated) | `SPOTIFYCTL_API` visibility macro. |
| `spotify/version.h` (generated) | `LIBSPOTIFYCTL_VERSION_*` and `spotify::Version()`. |

## Lifecycle

```cpp
#include <spotify/client.h>

spotify::SpotifyClient spotify;
spotify.Start();   // begin window / SMTC / audio monitoring
spotify.Stop();    // destructor calls Stop() too
```

`SpotifyClient` is move-only. Copy it and you'll get a compile error.

## Reading state

```cpp
auto s = spotify.LatestState();       // snapshot at any time, any thread
spotify.OnStateChanged.connect(       // stream
    [](const spotify::PlaybackState& s) { /* ... */ });
```

See the README's "Reading state" section for the full `PlaybackState`
definition and field semantics.

## Controls

Transport and per-app audio methods return `bool` — `false` when the channel
is unavailable (e.g., Spotify not running, audio session not bound).

```cpp
spotify.Play();
spotify.Pause();
spotify.Next();
spotify.Previous();
spotify.Seek(std::chrono::seconds(30));

spotify.SendCommand(spotify::AppCommand::PlayPause);

float v = spotify.GetAppVolume();
spotify.SetAppVolume(0.5f);
bool m = spotify.IsAppMuted();
spotify.SetAppMuted(true);
```

## Signals

All signals live on the client and expose `.connect(slot) -> Token`, plus a
`.disconnect(Token)`. Slots fire on Windows-owned background threads; don't
block, and synchronize your own state if the slot touches shared data.

| Signal | Payload | Fires when |
|---|---|---|
| `OnOpened` | — | The Spotify window is detected. |
| `OnClosed` | — | The Spotify window disappears. |
| `OnStateChanged` | `const PlaybackState&` | Any field of `PlaybackState` changed. |
| `OnAudibleChanged` | `bool` | Audible ↔ silent transition (~1 s debounce). |
| `OnRawTitle` | `const std::string&` | Window title changed. |

## Consuming from CMake

```cmake
find_package(libspotifyctl CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE spotifyctl::spotifyctl)
```

Or in-tree:

```cmake
add_subdirectory(third_party/libspotifyctl)
target_link_libraries(my_app PRIVATE spotifyctl::spotifyctl)
```
