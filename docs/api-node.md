# Node.js API

```sh
npm install libspotifyctl
```

Windows x64 only, Node 18+. Package bundles `libspotifyctl.dll` under
`prebuilt/`.

**Bun users:** Bun blocks all postinstall scripts by default, including koffi's
prebuild selector. After install, run `bun pm trust koffi` once, or add
`"trustedDependencies": ["koffi"]` to your `package.json`.

## Quickstart

```js
const { SpotifyClient, Status } = require('libspotifyctl');

const c = new SpotifyClient();
c.on('stateChanged', (s) => {
  console.log(`[${s.statusName}] ${s.artist} — ${s.title}`);
});
c.start();
```

## Types

```ts
interface PlaybackState {
  status:      Status;        // numeric enum
  statusName:  'UNKNOWN' | 'STOPPED' | 'PAUSED' | 'PLAYING' | 'CHANGING_TRACK';
  artist:      string;
  title:       string;
  album:       string;
  positionMs:  number;
  durationMs:  number;
  albumArt:    Buffer;
  canSeek:     boolean;
  canSkipNext: boolean;
  canSkipPrev: boolean;
  isAd:        boolean;
  audible:     boolean;
  appMuted:    boolean;
  appVolume:   number;        // -1 until audio session resolves
}
```

`Status` and `AppCommand` are plain frozen objects with the same numeric
values as the C ABI enums.

## SpotifyClient

Extends `EventEmitter`. Events:

| Event             | Payload                                  |
|-------------------|------------------------------------------|
| `opened`          | —                                        |
| `closed`          | —                                        |
| `stateChanged`    | `PlaybackState`                          |
| `trackChanged`    | `(previous: PlaybackState, current: PlaybackState)` |
| `adStarted`       | —                                        |
| `adEnded`         | —                                        |
| `audibleChanged`  | `boolean`                                |
| `rawTitle`        | `string`                                 |
| `positionChanged` | `number` — ms, ~1 Hz while Playing       |

`stateChanged` listeners automatically receive the current snapshot
synchronously on first attach — matching `EventEmitter` conventions, so late
subscribers don't need a separate `latestState()` call.

Methods:

```ts
class SpotifyClient extends EventEmitter {
  start(): void;
  stop(): void;
  close(): void;

  readonly isRunning: boolean;

  // Transport — return true on success.
  play(): boolean;
  pause(): boolean;
  next(): boolean;
  previous(): boolean;
  seekMs(positionMs: number): boolean;
  sendCommand(cmd: AppCommand | number): boolean;
  openUri(uri: string): boolean;
  sendKey(virtualKey: number): boolean;

  // Per-app audio (getters/setters).
  appVolume: number;
  appMuted: boolean;
  readonly peakAmplitude: number;

  // State.
  latestState(): PlaybackState;
  latestStateJson(): string;
  readonly positionSmoothMs: number;   // monotonic-clock extrapolation

  // Low-level callback management. Events registered via .on() are preferred.
  disconnect(token: bigint | number): void;
}
```

Callbacks fire on background threads from the underlying C library. Node's
`EventEmitter` serializes them back onto the main thread via koffi's
registered-callback dispatch; still, don't block for long inside a handler.

## URI builders

```js
const { uriTrack, uriSearch } = require('libspotifyctl');
c.openUri(uriTrack('4uLU6hMCjMI75M1A2tKUQC'));
c.openUri(uriSearch('dark side of the moon'));
```

## Development

Build the C++ DLL, copy it into the binding's `prebuilt/` directory, then run:

```sh
cmake --build --preset release-shared
cp build/release-shared/Release/libspotifyctl.dll bindings/node/prebuilt/
cd bindings/node && npm install && node --test test/
```

The `prebuilt/` directory is gitignored, so a freshly built DLL never lands
in commits.
