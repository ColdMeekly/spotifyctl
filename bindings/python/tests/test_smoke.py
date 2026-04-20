"""Smoke tests — exercise the FFI.

Several tests want to observe behavior when Spotify is NOT running. Those
skip automatically when a Spotify process is detected so the suite works on
CI runners and developer machines alike.
"""

from __future__ import annotations

import json

import pytest

import libspotifyctl as lsc


@pytest.fixture
def client():
    c = lsc.SpotifyClient()
    c.start()  # synchronous scan so is_running reflects actual state
    try:
        yield c
    finally:
        c.close()


def _requires_no_spotify(c: lsc.SpotifyClient) -> None:
    if c.is_running:
        pytest.skip("Spotify is running — test requires an inactive environment")


def test_version_is_semver_ish():
    v = lsc.__version__
    assert isinstance(v, str)
    assert v.count(".") >= 2


def test_client_lifecycle_does_not_crash():
    c = lsc.SpotifyClient()
    try:
        # Observation only — either True or False is valid depending on env.
        assert isinstance(c.is_running, bool)
    finally:
        c.close()


def test_context_manager_roundtrip(client):
    assert isinstance(client.is_running, bool)


def test_latest_state_returns_skeleton_when_no_spotify(client):
    _requires_no_spotify(client)
    s = client.latest_state()
    assert s.status == lsc.Status.UNKNOWN
    assert s.artist == ""
    assert s.title == ""
    assert s.album == ""
    assert s.position_ms == 0
    assert s.duration_ms == 0
    assert s.album_art == b""
    assert s.audible is False


def test_latest_state_shape_when_spotify_live(client):
    if not client.is_running:
        pytest.skip("Spotify is not running")
    s = client.latest_state()
    # Whatever Spotify reports, the shape must be correct.
    assert isinstance(s.status, lsc.Status)
    assert isinstance(s.artist, str)
    assert isinstance(s.title, str)
    assert isinstance(s.album, str)
    assert isinstance(s.position_ms, int)
    assert isinstance(s.duration_ms, int)
    assert isinstance(s.album_art, bytes)
    assert isinstance(s.audible, bool)
    assert isinstance(s.app_volume, float)


def test_latest_state_json_is_parseable(client):
    payload = client.latest_state_json()
    obj = json.loads(payload)
    assert "status" in obj
    assert "artist" in obj
    assert "title" in obj
    assert "album" in obj
    assert "position_ms" in obj
    assert isinstance(obj["position_ms"], int)


@pytest.mark.parametrize(
    "fn,arg,expected",
    [
        (lsc.uri_track,    "abc123", "spotify:track:abc123"),
        (lsc.uri_album,    "xyz",    "spotify:album:xyz"),
        (lsc.uri_playlist, "37i9dQ", "spotify:playlist:37i9dQ"),
        (lsc.uri_artist,   "foo",    "spotify:artist:foo"),
        (lsc.uri_user,     "spotify","spotify:user:spotify"),
    ],
)
def test_simple_uri_builders(fn, arg, expected):
    assert fn(arg) == expected


def test_search_uri_percent_encodes_spaces():
    assert " " not in lsc.uri_search("dark side of the moon")
    assert lsc.uri_search("radiohead") == "spotify:search:radiohead"


def test_transport_noop_when_spotify_not_running(client):
    _requires_no_spotify(client)
    assert client.play() is False
    assert client.pause() is False
    assert client.next() is False
    assert client.previous() is False
    assert client.seek_ms(1000) is False
    assert client.send_command(lsc.AppCommand.PLAY_PAUSE) is False


def test_disconnect_unknown_token_is_noop(client):
    client.disconnect(0)
    client.disconnect(99999)


def test_callbacks_register_and_return_token(client):
    token = client.on_state_changed(lambda _s: None)
    assert isinstance(token, int)
    assert token > 0
    client.disconnect(token)


def test_closed_client_raises():
    c = lsc.SpotifyClient()
    c.close()
    with pytest.raises(RuntimeError):
        _ = c.is_running
