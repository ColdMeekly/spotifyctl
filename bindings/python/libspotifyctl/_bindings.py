"""ctypes declarations mirroring spotify/c_api.h. Loaded lazily on import."""

from __future__ import annotations

import ctypes
import sys
from ctypes import (
    CFUNCTYPE,
    POINTER,
    Structure,
    c_char_p,
    c_float,
    c_int,
    c_int64,
    c_size_t,
    c_uint,
    c_uint8,
    c_void_p,
)
from pathlib import Path

if sys.platform != "win32":
    raise ImportError("libspotifyctl is a Windows-only library")


def _find_dll() -> Path:
    here = Path(__file__).resolve().parent
    bundled = here / "_prebuilt" / "libspotifyctl.dll"
    if not bundled.exists():
        raise FileNotFoundError(
            f"Bundled libspotifyctl.dll not found at {bundled}. Reinstall the wheel."
        )
    return bundled


_DLL_PATH = _find_dll()
lib = ctypes.CDLL(str(_DLL_PATH))


# ---------------------------------------------------------------------------
# Structs
# ---------------------------------------------------------------------------

class PlaybackStateRaw(Structure):
    _fields_ = [
        ("status",        c_int),
        ("artist",        c_char_p),
        ("title",         c_char_p),
        ("album",         c_char_p),
        ("position_ms",   c_int64),
        ("duration_ms",   c_int64),
        ("album_art",     POINTER(c_uint8)),
        ("album_art_len", c_size_t),
        ("can_seek",      c_int),
        ("can_skip_next", c_int),
        ("can_skip_prev", c_int),
        ("is_ad",         c_int),
        ("audible",       c_int),
        ("app_muted",     c_int),
        ("app_volume",    c_float),
    ]


# ---------------------------------------------------------------------------
# Callback types
# ---------------------------------------------------------------------------

StateCB        = CFUNCTYPE(None, POINTER(PlaybackStateRaw), c_void_p)
BoolCB         = CFUNCTYPE(None, c_int,                     c_void_p)
StringCB       = CFUNCTYPE(None, c_char_p,                  c_void_p)
VoidCB         = CFUNCTYPE(None,                            c_void_p)
PositionCB     = CFUNCTYPE(None, c_int64,                   c_void_p)
TrackChangedCB = CFUNCTYPE(
    None,
    POINTER(PlaybackStateRaw),  # previous
    POINTER(PlaybackStateRaw),  # current
    c_void_p,
)


# ---------------------------------------------------------------------------
# Function signatures
# ---------------------------------------------------------------------------

def _bind(name: str, restype, argtypes):
    fn = getattr(lib, name)
    fn.restype = restype
    fn.argtypes = argtypes
    return fn


# Lifecycle / library
_bind("spotifyctl_version",        c_char_p, [])
_bind("spotifyctl_new",            c_void_p, [])
_bind("spotifyctl_free",           None,     [c_void_p])
_bind("spotifyctl_start",          None,     [c_void_p])
_bind("spotifyctl_stop",           None,     [c_void_p])
_bind("spotifyctl_is_running",     c_int,    [c_void_p])

# Transport
for _n in ("play", "pause", "next", "previous"):
    _bind(f"spotifyctl_{_n}", c_int, [c_void_p])
_bind("spotifyctl_seek_ms",      c_int, [c_void_p, c_int64])
_bind("spotifyctl_send_command", c_int, [c_void_p, c_int])
_bind("spotifyctl_open_uri",     c_int, [c_void_p, c_char_p])
_bind("spotifyctl_send_key",     c_int, [c_void_p, c_uint])

# Audio
_bind("spotifyctl_get_app_volume",    c_float, [c_void_p])
_bind("spotifyctl_set_app_volume",    c_int,   [c_void_p, c_float])
_bind("spotifyctl_is_app_muted",      c_int,   [c_void_p])
_bind("spotifyctl_set_app_muted",     c_int,   [c_void_p, c_int])
_bind("spotifyctl_get_peak_amplitude", c_float, [c_void_p])

# State
_bind("spotifyctl_latest_state",             c_int,    [c_void_p, POINTER(PlaybackStateRaw)])
_bind("spotifyctl_latest_state_json",        c_size_t, [c_void_p, c_char_p, c_size_t])
_bind("spotifyctl_latest_position_smooth_ms", c_int64, [c_void_p])

# Callbacks
_bind("spotifyctl_on_state_changed",             c_size_t, [c_void_p, StateCB,        c_void_p])
_bind("spotifyctl_on_state_changed_with_replay", c_size_t, [c_void_p, StateCB,        c_void_p])
_bind("spotifyctl_on_audible_changed",           c_size_t, [c_void_p, BoolCB,         c_void_p])
_bind("spotifyctl_on_raw_title",                 c_size_t, [c_void_p, StringCB,       c_void_p])
_bind("spotifyctl_on_opened",                    c_size_t, [c_void_p, VoidCB,         c_void_p])
_bind("spotifyctl_on_closed",                    c_size_t, [c_void_p, VoidCB,         c_void_p])
_bind("spotifyctl_on_track_changed",             c_size_t, [c_void_p, TrackChangedCB, c_void_p])
_bind("spotifyctl_on_ad_started",                c_size_t, [c_void_p, VoidCB,         c_void_p])
_bind("spotifyctl_on_ad_ended",                  c_size_t, [c_void_p, VoidCB,         c_void_p])
_bind("spotifyctl_on_position_changed",          c_size_t, [c_void_p, PositionCB,     c_void_p])
_bind("spotifyctl_disconnect",                   None,     [c_void_p, c_size_t])

# URI builders — return opaque char* that must be freed. Use c_void_p so
# ctypes does NOT auto-convert to Python bytes (we'd lose the pointer).
for _n in ("track", "album", "playlist", "artist", "user", "search"):
    _bind(f"spotifyctl_uri_{_n}", c_void_p, [c_char_p])
_bind("spotifyctl_free_string", None, [c_void_p])
