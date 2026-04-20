"""libspotifyctl — offline control and observation of Spotify on Windows.

The local Spotify desktop app publishes metadata via the Windows System Media
Transport Controls (SMTC) and its per-app audio session. This package wraps
``libspotifyctl.dll`` so Python callers can read state, subscribe to events,
and drive playback without touching the Spotify Web API.
"""

from __future__ import annotations

import ctypes
from dataclasses import dataclass, field
from enum import IntEnum
from typing import Callable, Optional

from . import _bindings as _b

__all__ = [
    "__version__",
    "Status",
    "AppCommand",
    "PlaybackState",
    "SpotifyClient",
    "uri_track",
    "uri_album",
    "uri_playlist",
    "uri_artist",
    "uri_user",
    "uri_search",
]

__version__: str = _b.lib.spotifyctl_version().decode("utf-8")


# ---------------------------------------------------------------------------
# Enums
# ---------------------------------------------------------------------------

class Status(IntEnum):
    UNKNOWN         = 0
    STOPPED         = 1
    PAUSED          = 2
    PLAYING         = 3
    CHANGING_TRACK  = 4


class AppCommand(IntEnum):
    STOP        = 0
    PLAY        = 1
    PAUSE       = 2
    PLAY_PAUSE  = 3
    NEXT        = 4
    PREV        = 5
    VOL_UP      = 6
    VOL_DOWN    = 7
    MUTE        = 8


# ---------------------------------------------------------------------------
# Playback state
# ---------------------------------------------------------------------------

@dataclass
class PlaybackState:
    status:         Status = Status.UNKNOWN
    artist:         str = ""
    title:          str = ""
    album:          str = ""
    position_ms:    int = 0
    duration_ms:    int = 0
    album_art:      bytes = b""
    can_seek:       bool = False
    can_skip_next:  bool = False
    can_skip_prev:  bool = False
    is_ad:          bool = False
    audible:        bool = False
    app_muted:      bool = False
    app_volume:     float = -1.0   # -1 until the audio session is resolved


def _unpack(raw: _b.PlaybackStateRaw) -> PlaybackState:
    art = b""
    if raw.album_art and raw.album_art_len:
        art = bytes(raw.album_art[: raw.album_art_len])
    return PlaybackState(
        status        = Status(raw.status),
        artist        = (raw.artist or b"").decode("utf-8", errors="replace"),
        title         = (raw.title  or b"").decode("utf-8", errors="replace"),
        album         = (raw.album  or b"").decode("utf-8", errors="replace"),
        position_ms   = raw.position_ms,
        duration_ms   = raw.duration_ms,
        album_art     = art,
        can_seek      = bool(raw.can_seek),
        can_skip_next = bool(raw.can_skip_next),
        can_skip_prev = bool(raw.can_skip_prev),
        is_ad         = bool(raw.is_ad),
        audible       = bool(raw.audible),
        app_muted     = bool(raw.app_muted),
        app_volume    = raw.app_volume,
    )


# ---------------------------------------------------------------------------
# Client
# ---------------------------------------------------------------------------

class SpotifyClient:
    """Handle to the local Spotify desktop app.

    Use as a context manager — ``__enter__`` calls :meth:`start`, ``__exit__``
    calls :meth:`close`. Signals may fire on background threads; callbacks
    must not block.
    """

    def __init__(self) -> None:
        handle = _b.lib.spotifyctl_new()
        if not handle:
            raise MemoryError("spotifyctl_new returned NULL")
        self._handle: Optional[int] = handle
        # token -> (ctypes callback, original python callable) — prevents GC
        # from collecting the CFUNCTYPE shim while the C side holds the pointer.
        self._callbacks: dict[int, tuple] = {}

    # -- lifecycle -------------------------------------------------------

    def __enter__(self) -> "SpotifyClient":
        self.start()
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()

    def __del__(self) -> None:  # best-effort finalizer
        try:
            self.close()
        except Exception:
            pass

    def start(self) -> None:
        self._require()
        _b.lib.spotifyctl_start(self._handle)

    def stop(self) -> None:
        if self._handle:
            _b.lib.spotifyctl_stop(self._handle)

    def close(self) -> None:
        if self._handle:
            _b.lib.spotifyctl_stop(self._handle)
            _b.lib.spotifyctl_free(self._handle)
            self._handle = None
            self._callbacks.clear()

    @property
    def is_running(self) -> bool:
        self._require()
        return bool(_b.lib.spotifyctl_is_running(self._handle))

    # -- transport -------------------------------------------------------

    def play(self) -> bool:
        self._require()
        return bool(_b.lib.spotifyctl_play(self._handle))

    def pause(self) -> bool:
        self._require()
        return bool(_b.lib.spotifyctl_pause(self._handle))

    def next(self) -> bool:
        self._require()
        return bool(_b.lib.spotifyctl_next(self._handle))

    def previous(self) -> bool:
        self._require()
        return bool(_b.lib.spotifyctl_previous(self._handle))

    def seek_ms(self, position_ms: int) -> bool:
        self._require()
        return bool(_b.lib.spotifyctl_seek_ms(self._handle, int(position_ms)))

    def send_command(self, cmd: AppCommand) -> bool:
        self._require()
        return bool(_b.lib.spotifyctl_send_command(self._handle, int(cmd)))

    def open_uri(self, uri: str) -> bool:
        self._require()
        return bool(_b.lib.spotifyctl_open_uri(self._handle, uri.encode("utf-8")))

    def send_key(self, virtual_key: int) -> bool:
        self._require()
        return bool(_b.lib.spotifyctl_send_key(self._handle, int(virtual_key)))

    # -- per-app audio ---------------------------------------------------

    @property
    def app_volume(self) -> float:
        self._require()
        return float(_b.lib.spotifyctl_get_app_volume(self._handle))

    @app_volume.setter
    def app_volume(self, value: float) -> None:
        self._require()
        _b.lib.spotifyctl_set_app_volume(self._handle, float(value))

    @property
    def app_muted(self) -> bool:
        self._require()
        return bool(_b.lib.spotifyctl_is_app_muted(self._handle))

    @app_muted.setter
    def app_muted(self, muted: bool) -> None:
        self._require()
        _b.lib.spotifyctl_set_app_muted(self._handle, 1 if muted else 0)

    @property
    def peak_amplitude(self) -> float:
        self._require()
        return float(_b.lib.spotifyctl_get_peak_amplitude(self._handle))

    # -- state -----------------------------------------------------------

    def latest_state(self) -> PlaybackState:
        self._require()
        raw = _b.PlaybackStateRaw()
        _b.lib.spotifyctl_latest_state(self._handle, ctypes.byref(raw))
        return _unpack(raw)

    def latest_state_json(self) -> str:
        self._require()
        need = _b.lib.spotifyctl_latest_state_json(self._handle, None, 0)
        buf = ctypes.create_string_buffer(need + 1)
        _b.lib.spotifyctl_latest_state_json(self._handle, buf, need + 1)
        return buf.value.decode("utf-8")

    # -- callbacks -------------------------------------------------------

    def on_state_changed(self, callback: Callable[[PlaybackState], None]) -> int:
        @_b.StateCB
        def _shim(state_ptr, _user):
            try:
                callback(_unpack(state_ptr.contents))
            except Exception:
                pass  # never propagate Python exceptions across the ABI
        return self._register(_b.lib.spotifyctl_on_state_changed, _shim, callback)

    def on_audible_changed(self, callback: Callable[[bool], None]) -> int:
        @_b.BoolCB
        def _shim(value, _user):
            try:
                callback(bool(value))
            except Exception:
                pass
        return self._register(_b.lib.spotifyctl_on_audible_changed, _shim, callback)

    def on_raw_title(self, callback: Callable[[str], None]) -> int:
        @_b.StringCB
        def _shim(raw, _user):
            try:
                callback((raw or b"").decode("utf-8", errors="replace"))
            except Exception:
                pass
        return self._register(_b.lib.spotifyctl_on_raw_title, _shim, callback)

    def on_opened(self, callback: Callable[[], None]) -> int:
        @_b.VoidCB
        def _shim(_user):
            try:
                callback()
            except Exception:
                pass
        return self._register(_b.lib.spotifyctl_on_opened, _shim, callback)

    def on_closed(self, callback: Callable[[], None]) -> int:
        @_b.VoidCB
        def _shim(_user):
            try:
                callback()
            except Exception:
                pass
        return self._register(_b.lib.spotifyctl_on_closed, _shim, callback)

    def disconnect(self, token: int) -> None:
        if not self._handle:
            return
        _b.lib.spotifyctl_disconnect(self._handle, token)
        self._callbacks.pop(token, None)

    # -- internals -------------------------------------------------------

    def _register(self, fn, shim, original) -> int:
        self._require()
        token = int(fn(self._handle, shim, None))
        self._callbacks[token] = (shim, original)
        return token

    def _require(self) -> None:
        if not self._handle:
            raise RuntimeError("SpotifyClient has been closed")


# ---------------------------------------------------------------------------
# URI builders
# ---------------------------------------------------------------------------

def _uri(fn, arg: str) -> str:
    ptr = fn(arg.encode("utf-8"))
    if not ptr:
        raise MemoryError("URI builder returned NULL")
    try:
        return ctypes.string_at(ptr).decode("utf-8")
    finally:
        _b.lib.spotifyctl_free_string(ptr)


def uri_track(id: str) -> str:    return _uri(_b.lib.spotifyctl_uri_track,    id)
def uri_album(id: str) -> str:    return _uri(_b.lib.spotifyctl_uri_album,    id)
def uri_playlist(id: str) -> str: return _uri(_b.lib.spotifyctl_uri_playlist, id)
def uri_artist(id: str) -> str:   return _uri(_b.lib.spotifyctl_uri_artist,   id)
def uri_user(username: str) -> str: return _uri(_b.lib.spotifyctl_uri_user,   username)
def uri_search(query: str) -> str:  return _uri(_b.lib.spotifyctl_uri_search, query)
