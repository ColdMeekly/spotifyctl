'use strict';

/**
 * libspotifyctl — Node.js bindings.
 *
 * Wraps the C ABI in `libspotifyctl.dll` via koffi. The DLL is bundled under
 * `prebuilt/` for win32-x64.
 */

const koffi = require('koffi');
const path = require('path');
const fs = require('fs');
const { EventEmitter } = require('events');

// ---------------------------------------------------------------------------
// DLL load
// ---------------------------------------------------------------------------

const dllPath = path.join(__dirname, 'prebuilt', 'libspotifyctl.dll');
if (!fs.existsSync(dllPath)) {
  throw new Error(
    `Bundled libspotifyctl.dll not found at ${dllPath}. Reinstall the package.`
  );
}
const lib = koffi.load(dllPath);

// ---------------------------------------------------------------------------
// Enums
// ---------------------------------------------------------------------------

const Status = Object.freeze({
  UNKNOWN:        0,
  STOPPED:        1,
  PAUSED:         2,
  PLAYING:        3,
  CHANGING_TRACK: 4,
});

const StatusName = Object.freeze({
  0: 'UNKNOWN',
  1: 'STOPPED',
  2: 'PAUSED',
  3: 'PLAYING',
  4: 'CHANGING_TRACK',
});

const AppCommand = Object.freeze({
  STOP:       0,
  PLAY:       1,
  PAUSE:      2,
  PLAY_PAUSE: 3,
  NEXT:       4,
  PREV:       5,
  VOL_UP:     6,
  VOL_DOWN:   7,
  MUTE:       8,
});

// ---------------------------------------------------------------------------
// Struct + callback prototypes
// ---------------------------------------------------------------------------

const PlaybackStateRaw = koffi.struct('spotifyctl_playback_state', {
  status:         'int',
  artist:         'const char *',
  title:          'const char *',
  album:          'const char *',
  position_ms:    'int64_t',
  duration_ms:    'int64_t',
  album_art:      'const uint8_t *',
  album_art_len:  'size_t',
  can_seek:       'int',
  can_skip_next:  'int',
  can_skip_prev:  'int',
  is_ad:          'int',
  audible:        'int',
  app_muted:      'int',
  app_volume:     'float',
});

const StateCB   = koffi.proto('void StateCB(const spotifyctl_playback_state *state, void *user)');
const BoolCB    = koffi.proto('void BoolCB(int value, void *user)');
const StringCB  = koffi.proto('void StringCB(const char *utf8, void *user)');
const VoidCB    = koffi.proto('void VoidCB(void *user)');
const PositionCB = koffi.proto('void PositionCB(int64_t position_ms, void *user)');
const TrackCB   = koffi.proto(
  'void TrackCB(const spotifyctl_playback_state *prev, const spotifyctl_playback_state *curr, void *user)'
);

// ---------------------------------------------------------------------------
// Function bindings
// ---------------------------------------------------------------------------

const fn = {
  version:           lib.func('const char *spotifyctl_version()'),
  new:               lib.func('void *spotifyctl_new()'),
  free:              lib.func('void spotifyctl_free(void *c)'),
  start:             lib.func('void spotifyctl_start(void *c)'),
  stop:              lib.func('void spotifyctl_stop(void *c)'),
  is_running:        lib.func('int spotifyctl_is_running(const void *c)'),

  play:              lib.func('int spotifyctl_play(void *c)'),
  pause:             lib.func('int spotifyctl_pause(void *c)'),
  next:              lib.func('int spotifyctl_next(void *c)'),
  previous:          lib.func('int spotifyctl_previous(void *c)'),
  seek_ms:           lib.func('int spotifyctl_seek_ms(void *c, int64_t pos)'),
  send_command:      lib.func('int spotifyctl_send_command(void *c, int cmd)'),
  open_uri:          lib.func('int spotifyctl_open_uri(void *c, const char *uri)'),
  send_key:          lib.func('int spotifyctl_send_key(void *c, unsigned int vk)'),

  get_app_volume:    lib.func('float spotifyctl_get_app_volume(const void *c)'),
  set_app_volume:    lib.func('int spotifyctl_set_app_volume(void *c, float v)'),
  is_app_muted:      lib.func('int spotifyctl_is_app_muted(const void *c)'),
  set_app_muted:     lib.func('int spotifyctl_set_app_muted(void *c, int muted)'),
  get_peak_amplitude:lib.func('float spotifyctl_get_peak_amplitude(const void *c)'),

  latest_state:      lib.func('int spotifyctl_latest_state(void *c, _Out_ spotifyctl_playback_state *out)'),
  latest_state_json: lib.func('size_t spotifyctl_latest_state_json(void *c, _Out_ char *buf, size_t cap)'),
  latest_position_smooth_ms: lib.func('int64_t spotifyctl_latest_position_smooth_ms(const void *c)'),

  on_state_changed:             lib.func('size_t spotifyctl_on_state_changed(void *c, StateCB *cb, void *user)'),
  on_state_changed_with_replay: lib.func('size_t spotifyctl_on_state_changed_with_replay(void *c, StateCB *cb, void *user)'),
  on_audible_changed:           lib.func('size_t spotifyctl_on_audible_changed(void *c, BoolCB *cb, void *user)'),
  on_raw_title:                 lib.func('size_t spotifyctl_on_raw_title(void *c, StringCB *cb, void *user)'),
  on_opened:                    lib.func('size_t spotifyctl_on_opened(void *c, VoidCB *cb, void *user)'),
  on_closed:                    lib.func('size_t spotifyctl_on_closed(void *c, VoidCB *cb, void *user)'),
  on_track_changed:             lib.func('size_t spotifyctl_on_track_changed(void *c, TrackCB *cb, void *user)'),
  on_ad_started:                lib.func('size_t spotifyctl_on_ad_started(void *c, VoidCB *cb, void *user)'),
  on_ad_ended:                  lib.func('size_t spotifyctl_on_ad_ended(void *c, VoidCB *cb, void *user)'),
  on_position_changed:          lib.func('size_t spotifyctl_on_position_changed(void *c, PositionCB *cb, void *user)'),
  disconnect:                   lib.func('void spotifyctl_disconnect(void *c, size_t token)'),

  // URI builders return heap char*; we need the raw pointer so we can free it.
  uri_track:         lib.func('void *spotifyctl_uri_track(const char *id)'),
  uri_album:         lib.func('void *spotifyctl_uri_album(const char *id)'),
  uri_playlist:      lib.func('void *spotifyctl_uri_playlist(const char *id)'),
  uri_artist:        lib.func('void *spotifyctl_uri_artist(const char *id)'),
  uri_user:          lib.func('void *spotifyctl_uri_user(const char *u)'),
  uri_search:        lib.func('void *spotifyctl_uri_search(const char *q)'),
  free_string:       lib.func('void spotifyctl_free_string(void *s)'),
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

function unpackState(raw) {
  let art = Buffer.alloc(0);
  const len = Number(raw.album_art_len || 0);
  if (raw.album_art && len > 0) {
    // koffi decodes a fixed-size uint8 array from the pointer.
    const bytes = koffi.decode(raw.album_art, 'uint8_t', len);
    art = Buffer.from(bytes);
  }
  return {
    status:        raw.status,
    statusName:    StatusName[raw.status] || 'UNKNOWN',
    artist:        raw.artist || '',
    title:         raw.title || '',
    album:         raw.album || '',
    positionMs:    Number(raw.position_ms),
    durationMs:    Number(raw.duration_ms),
    albumArt:      art,
    canSeek:       !!raw.can_seek,
    canSkipNext:   !!raw.can_skip_next,
    canSkipPrev:   !!raw.can_skip_prev,
    isAd:          !!raw.is_ad,
    audible:       !!raw.audible,
    appMuted:      !!raw.app_muted,
    appVolume:     raw.app_volume,
  };
}

function takeOwnedString(ptr) {
  if (!ptr) throw new Error('URI builder returned NULL');
  try {
    return koffi.decode(ptr, 'char', -1);  // NUL-terminated
  } finally {
    fn.free_string(ptr);
  }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

const version = fn.version();

class SpotifyClient extends EventEmitter {
  constructor() {
    super();
    const handle = fn.new();
    if (!handle) throw new Error('spotifyctl_new returned NULL');
    this._handle = handle;
    // token -> koffi registered pointer (must stay alive until disconnect)
    this._tokens = new Map();
    this._wired = false;
  }

  // -- lifecycle ---------------------------------------------------------

  start() {
    this._require();
    if (!this._wired) {
      this._wire();
      this._wired = true;
    }
    fn.start(this._handle);
  }

  stop() {
    if (this._handle) fn.stop(this._handle);
  }

  close() {
    if (!this._handle) return;
    for (const [, shim] of this._tokens) {
      try { koffi.unregister(shim); } catch { /* ignore */ }
    }
    this._tokens.clear();
    fn.stop(this._handle);
    fn.free(this._handle);
    this._handle = null;
    this._wired = false;
  }

  get isRunning() {
    this._require();
    return !!fn.is_running(this._handle);
  }

  // -- transport ---------------------------------------------------------

  play()              { this._require(); return !!fn.play(this._handle); }
  pause()             { this._require(); return !!fn.pause(this._handle); }
  next()              { this._require(); return !!fn.next(this._handle); }
  previous()          { this._require(); return !!fn.previous(this._handle); }
  seekMs(positionMs)  { this._require(); return !!fn.seek_ms(this._handle, BigInt(Math.trunc(positionMs))); }
  sendCommand(cmd)    { this._require(); return !!fn.send_command(this._handle, cmd | 0); }
  openUri(uri)        { this._require(); return !!fn.open_uri(this._handle, String(uri)); }
  sendKey(vk)         { this._require(); return !!fn.send_key(this._handle, vk >>> 0); }

  // -- per-app audio -----------------------------------------------------

  get appVolume() { this._require(); return fn.get_app_volume(this._handle); }
  set appVolume(v) { this._require(); fn.set_app_volume(this._handle, Number(v)); }

  get appMuted() { this._require(); return !!fn.is_app_muted(this._handle); }
  set appMuted(m) { this._require(); fn.set_app_muted(this._handle, m ? 1 : 0); }

  get peakAmplitude() { this._require(); return fn.get_peak_amplitude(this._handle); }

  // -- state -------------------------------------------------------------

  latestState() {
    this._require();
    const raw = {};
    fn.latest_state(this._handle, raw);
    return unpackState(raw);
  }

  latestStateJson() {
    this._require();
    const need = Number(fn.latest_state_json(this._handle, null, 0));
    const buf = Buffer.alloc(need + 1);
    fn.latest_state_json(this._handle, buf, BigInt(need + 1));
    const nul = buf.indexOf(0);
    return buf.slice(0, nul === -1 ? need : nul).toString('utf8');
  }

  get positionSmoothMs() {
    this._require();
    return Number(fn.latest_position_smooth_ms(this._handle));
  }

  disconnect(token) {
    if (!this._handle) return;
    const t = typeof token === 'bigint' ? token : BigInt(token);
    const shim = this._tokens.get(String(t));
    if (shim) {
      try { koffi.unregister(shim); } catch { /* ignore */ }
      this._tokens.delete(String(t));
    }
    fn.disconnect(this._handle, t);
  }

  // -- internals ---------------------------------------------------------

  _wire() {
    // Pre-register all event streams on start(); consumers use .on(...).
    this._subscribe(fn.on_opened,
      koffi.register((_user) => { try { this.emit('opened'); } catch {} }, koffi.pointer(VoidCB)));

    this._subscribe(fn.on_closed,
      koffi.register((_user) => { try { this.emit('closed'); } catch {} }, koffi.pointer(VoidCB)));

    // Use the replay variant so late listeners receive the current state
    // synchronously on first `.on('stateChanged', ...)` — matches what Node
    // EventEmitter users naturally expect. Zero cost for non-listeners.
    this._subscribe(fn.on_state_changed_with_replay,
      koffi.register((statePtr, _user) => {
        try {
          const raw = koffi.decode(statePtr, PlaybackStateRaw);
          this.emit('stateChanged', unpackState(raw));
        } catch {}
      }, koffi.pointer(StateCB)));

    this._subscribe(fn.on_audible_changed,
      koffi.register((value, _user) => { try { this.emit('audibleChanged', !!value); } catch {} },
        koffi.pointer(BoolCB)));

    this._subscribe(fn.on_raw_title,
      koffi.register((utf8, _user) => {
        try {
          const s = utf8 ? koffi.decode(utf8, 'char', -1) : '';
          this.emit('rawTitle', s);
        } catch {}
      }, koffi.pointer(StringCB)));

    this._subscribe(fn.on_track_changed,
      koffi.register((prevPtr, currPtr, _user) => {
        try {
          const prev = koffi.decode(prevPtr, PlaybackStateRaw);
          const curr = koffi.decode(currPtr, PlaybackStateRaw);
          this.emit('trackChanged', unpackState(prev), unpackState(curr));
        } catch {}
      }, koffi.pointer(TrackCB)));

    this._subscribe(fn.on_ad_started,
      koffi.register((_user) => { try { this.emit('adStarted'); } catch {} }, koffi.pointer(VoidCB)));

    this._subscribe(fn.on_ad_ended,
      koffi.register((_user) => { try { this.emit('adEnded'); } catch {} }, koffi.pointer(VoidCB)));

    this._subscribe(fn.on_position_changed,
      koffi.register((positionMs, _user) => {
        try { this.emit('positionChanged', Number(positionMs)); } catch {}
      }, koffi.pointer(PositionCB)));
  }

  _subscribe(registerFn, shimPtr) {
    const token = registerFn(this._handle, shimPtr, null);
    this._tokens.set(String(token), shimPtr);
    return token;
  }

  _require() {
    if (!this._handle) throw new Error('SpotifyClient has been closed');
  }
}

// ---------------------------------------------------------------------------
// URI builders
// ---------------------------------------------------------------------------

const uriTrack    = (id)  => takeOwnedString(fn.uri_track(String(id)));
const uriAlbum    = (id)  => takeOwnedString(fn.uri_album(String(id)));
const uriPlaylist = (id)  => takeOwnedString(fn.uri_playlist(String(id)));
const uriArtist   = (id)  => takeOwnedString(fn.uri_artist(String(id)));
const uriUser     = (u)   => takeOwnedString(fn.uri_user(String(u)));
const uriSearch   = (q)   => takeOwnedString(fn.uri_search(String(q)));

// ---------------------------------------------------------------------------
// Exports
// ---------------------------------------------------------------------------

module.exports = {
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
};
