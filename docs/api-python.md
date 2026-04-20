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
    position_smooth_ms: int          # read-only property — monotonic-clock
                                     # extrapolation between SMTC updates

    # Callbacks — all return an int token, pass to disconnect().
    def on_state_changed  (self, cb: Callable[[PlaybackState], None], *,
                           replay: bool = False)                       -> int: ...
    def on_track_changed  (self, cb: Callable[[PlaybackState, PlaybackState], None]) -> int: ...
    def on_ad_started     (self, cb: Callable[[], None])              -> int: ...
    def on_ad_ended       (self, cb: Callable[[], None])              -> int: ...
    def on_position_changed(self, cb: Callable[[int], None])          -> int: ...
    def on_audible_changed(self, cb: Callable[[bool], None])          -> int: ...
    def on_raw_title      (self, cb: Callable[[str], None])           -> int: ...
    def on_opened         (self, cb: Callable[[], None])              -> int: ...
    def on_closed         (self, cb: Callable[[], None])              -> int: ...
    def disconnect        (self, token: int) -> None: ...
```

Callbacks fire on Windows-owned background threads. Exceptions raised inside
a callback are swallowed (they cannot propagate across the C ABI).

### Late subscribers to `on_state_changed`

Pass `replay=True` to receive the current snapshot synchronously on subscribe,
atomically with the connect — no race against `latest_state()`:

```python
c.on_state_changed(print_state, replay=True)
```

### Track & ad edges

```python
def on_track(prev, curr):
    print(f"now: {curr.artist} — {curr.title}")

c.on_track_changed(on_track)    # fires on any (artist, title, album) change
c.on_ad_started(lambda: print("ad"))
c.on_ad_ended  (lambda: print("music's back"))
```

### Smooth position

```python
c.on_position_changed(lambda ms: print(f"{ms / 1000:.1f}s"))   # ~1 Hz while Playing
print(c.position_smooth_ms)                                    # pull instead of push
```

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

Build the C++ DLL, copy it into the binding's `_prebuilt/` directory, then
install the binding in editable mode:

```sh
cmake --build --preset release-shared
cp build/release-shared/Release/libspotifyctl.dll bindings/python/libspotifyctl/_prebuilt/
pip install -e bindings/python
python -m pytest bindings/python
```

The `_prebuilt/` directory is gitignored, so a freshly built DLL never lands
in commits.
