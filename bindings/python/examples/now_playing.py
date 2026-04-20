"""Live now-playing watcher — prints a line on every state change.

Run with Spotify open and playing:
    python bindings/python/examples/now_playing.py
"""

from __future__ import annotations

import signal
import threading
import time

from libspotifyctl import SpotifyClient, Status


def _fmt(ms: int) -> str:
    s = max(ms, 0) // 1000
    return f"{s // 60}:{s % 60:02d}"


def main() -> int:
    stop = threading.Event()
    signal.signal(signal.SIGINT, lambda *_: stop.set())

    def on_change(state) -> None:
        pos = _fmt(state.position_ms)
        dur = _fmt(state.duration_ms)
        label = state.status.name.lower()
        if state.status == Status.UNKNOWN:
            print(f"[{label}] (Spotify not detected)")
        else:
            print(f"[{label}] {state.artist} — {state.title}  [{pos} / {dur}]")

    with SpotifyClient() as client:
        client.on_state_changed(on_change)
        on_change(client.latest_state())  # print an initial baseline

        while not stop.is_set():
            stop.wait(timeout=0.25)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
