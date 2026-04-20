# libspotifyctl — Python bindings

Offline (no Web API) Python bindings for reading from and controlling Spotify on Windows.

Thin `ctypes` wrapper over the `libspotifyctl.dll` C ABI — no `node-gyp`-style
per-Python-version builds. One wheel (`py3-none-win_amd64`) serves all CPython
3.9+ on 64-bit Windows.

## Install

```bash
pip install libspotifyctl
```

## Quick start

```python
import time
from libspotifyctl import SpotifyClient

with SpotifyClient() as c:
    c.on_state_changed(lambda s: print(f"{s.artist} — {s.title}"))
    time.sleep(30)
```

See the project repo for the full API and the underlying C library:
https://github.com/ColdMeekly/spotifyctl
