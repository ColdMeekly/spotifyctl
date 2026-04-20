// Smoke tests — exercise the FFI.
//
// Tests that require an inactive Spotify skip automatically when a Spotify
// process is detected, so the suite works on both CI and developer machines.

import { test } from 'node:test';
import assert from 'node:assert/strict';

import lsc from '../index.js';

const {
  version,
  Status,
  AppCommand,
  SpotifyClient,
  uriTrack,
  uriAlbum,
  uriPlaylist,
  uriArtist,
  uriUser,
  uriSearch,
} = lsc;

function withClient(fn) {
  return () => {
    const c = new SpotifyClient();
    c.start();
    try {
      return fn(c);
    } finally {
      c.close();
    }
  };
}

test('version is semver-ish', () => {
  assert.equal(typeof version, 'string');
  assert.ok(version.split('.').length >= 3, version);
});

test('client lifecycle does not crash', () => {
  const c = new SpotifyClient();
  try {
    assert.equal(typeof c.isRunning, 'boolean');
  } finally {
    c.close();
  }
});

test('latest_state returns skeleton when Spotify not running', withClient((c) => {
  if (c.isRunning) return;
  const s = c.latestState();
  assert.equal(s.status, Status.UNKNOWN);
  assert.equal(s.artist, '');
  assert.equal(s.title, '');
  assert.equal(s.album, '');
  assert.equal(s.positionMs, 0);
  assert.equal(s.durationMs, 0);
  assert.equal(s.albumArt.length, 0);
  assert.equal(s.audible, false);
}));

test('latest_state shape when Spotify live', withClient((c) => {
  if (!c.isRunning) return;
  const s = c.latestState();
  assert.equal(typeof s.status, 'number');
  assert.equal(typeof s.artist, 'string');
  assert.equal(typeof s.title, 'string');
  assert.equal(typeof s.album, 'string');
  assert.equal(typeof s.positionMs, 'number');
  assert.equal(typeof s.durationMs, 'number');
  assert.ok(Buffer.isBuffer(s.albumArt));
  assert.equal(typeof s.audible, 'boolean');
  assert.equal(typeof s.appVolume, 'number');
}));

test('latest_state_json is parseable JSON', withClient((c) => {
  const payload = c.latestStateJson();
  const obj = JSON.parse(payload);
  assert.ok('status' in obj);
  assert.ok('artist' in obj);
  assert.ok('title' in obj);
  assert.ok('album' in obj);
  assert.ok('position_ms' in obj);
  assert.equal(typeof obj.position_ms, 'number');
}));

test('uri builders', () => {
  assert.equal(uriTrack('abc123'),    'spotify:track:abc123');
  assert.equal(uriAlbum('xyz'),       'spotify:album:xyz');
  assert.equal(uriPlaylist('37i9dQ'), 'spotify:playlist:37i9dQ');
  assert.equal(uriArtist('foo'),      'spotify:artist:foo');
  assert.equal(uriUser('spotify'),    'spotify:user:spotify');
});

test('uri_search percent-encodes spaces', () => {
  assert.ok(!uriSearch('dark side of the moon').includes(' '));
  assert.equal(uriSearch('radiohead'), 'spotify:search:radiohead');
});

test('transport is no-op when Spotify not running', withClient((c) => {
  if (c.isRunning) return;
  assert.equal(c.play(),        false);
  assert.equal(c.pause(),       false);
  assert.equal(c.next(),        false);
  assert.equal(c.previous(),    false);
  assert.equal(c.seekMs(1000),  false);
  assert.equal(c.sendCommand(AppCommand.PLAY_PAUSE), false);
}));

test('disconnect with unknown token is a no-op', withClient((c) => {
  c.disconnect(0);
  c.disconnect(99999);
}));

test('closed client throws on further use', () => {
  const c = new SpotifyClient();
  c.close();
  assert.throws(() => { void c.isRunning; }, /closed/);
});
