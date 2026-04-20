# Python API

```sh
pip install libspotifyctl
```

Windows x64 only, CPython 3.9+. The wheel bundles `libspotifyctl.dll` under
`libspotifyctl/_prebuilt/`.

## Quickstart

```python
from libspotifyctl import SpotifyClient, Status
import time

with SpotifyClient() as c:
    def on_change(s):
        print(f"[{s.status.name}] {s.artist} — {s.title}")
    c.on_state_changed(on_change)
    time.sleep(30)
```

## Types

```python
class Status(IntEnum):
    UNKNOWN        = 0
    STOPPED        = 1
    PAUSED         = 2
    PLAYING        = 3
    CHANGING_TRACK = 4

class AppCommand(IntEnum):
    STOP       = 0
    PLAY       = 1
    PAUSE      = 2
    PLAY_PAUSE = 3
    NEXT       = 4
    PREV       = 5
    VOL_UP     = 6
    VOL_DOWN   = 7
    MUTE       = 8

@dataclass
class PlaybackState:
    status:        Status
    artist:        str
    title:         str
    album:         str
    position_ms:   int
    duration_ms:   int
    album_art:     bytes
    can_seek:      bool
    can_skip_next: bool
    can_skip_prev: bool
    is_ad:         bool
    audible:       bool
    app_muted:     bool
    app_volume:    float   # -1 until the audio session is resolved
```

## SpotifyClient

```python
class SpotifyClient:
    def start(self) -> None: ...
    def stop(self) -> None: ...
    def close(self) -> None: ...

    is_running: bool         # property

    # Transport — return True on success, False on failure.
    def play(self) -> bool: ...
    def pause(self) -> bool: ...
    def next(self) -> bool: ...
    def previous(self) -> bool: ...
    def seek_ms(self, position_ms: int) -> bool: ...
    def send_command(self, cmd: AppCommand) -> bool: ...
    def open_uri(self, uri: str) -> bool: ...
    def send_key(self, virtual_key: int) -> bool: ...

    # Per-app audio.
    app_volume: float        # property — get/set
    app_muted:  bool         # property — get/set
    peak_amplitude: float    # read-only

    # State.
    def latest_state(self) -> PlaybackState: ...
    def latest_state_json(self) -> str: ...

    # Callbacks — all return an int token, pass to disconnect().
    def on_state_changed  (self, cb: Callable[[PlaybackState], None]) -> int: ...
    def on_audible_changed(self, cb: Callable[[bool], None])          -> int: ...
    def on_raw_title      (self, cb: Callable[[str], None])           -> int: ...
    def on_opened         (self, cb: Callable[[], None])              -> int: ...
    def on_closed         (self, cb: Callable[[], None])              -> int: ...
    def disconnect        (self, token: int) -> None: ...
```

Callbacks fire on Windows-owned background threads. Exceptions raised inside
a callback are swallowed (they cannot propagate across the C ABI).

## URI builders

```python
from libspotifyctl import uri_track, uri_album, uri_playlist, uri_artist, uri_user, uri_search

uri_track("4uLU6hMCjMI75M1A2tKUQC")    # "spotify:track:4uLU6hMCjMI75M1A2tKUQC"
uri_search("dark side of the moon")   # spaces are percent-encoded
```

## Context manager

`SpotifyClient` works with `with`. `__enter__` calls `start()`, `__exit__`
calls `close()`.

```python
with SpotifyClient() as c:
    c.play()
```

## Development

Point the loader at an out-of-tree DLL during development:

```sh
LIBSPOTIFYCTL_DLL=build\release-shared\Release\libspotifyctl.dll python -m pytest
```
