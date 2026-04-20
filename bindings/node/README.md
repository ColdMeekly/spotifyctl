# libspotifyctl — Node.js bindings

Offline control and observation of the Spotify desktop app on Windows — no Web
API, no OAuth. The local Spotify app publishes metadata via the Windows System
Media Transport Controls (SMTC) and its per-app audio session; this package
wraps `libspotifyctl.dll` via [koffi](https://koffi.dev/) so Node can read
state, subscribe to events, and drive playback.

## Install

```sh
npm install libspotifyctl
```

Platform: **Windows x64 only**. `engines.node >= 18`.

### Bun users

Bun blocks all postinstall scripts by default. The `koffi` FFI dependency uses
its postinstall to select the correct prebuilt native binary for your Bun ABI,
so after install you'll see:

```
Blocked 1 postinstall. Run `bun pm untrusted` for details.
```

Trust koffi once to unblock it:

```sh
bun pm trust koffi
```

Or declare it in your own `package.json` so it's trusted on every install:

```json
{
  "trustedDependencies": ["koffi"]
}
```

## Quickstart

```js
const { SpotifyClient, Status } = require('libspotifyctl');

const c = new SpotifyClient();
c.on('stateChanged', (s) => {
  console.log(`[${s.statusName}] ${s.artist} — ${s.title}`);
});
c.start();

// Transport
c.play();
c.pause();
c.next();
c.seekMs(90_000);

// Per-app audio
c.appVolume = 0.5;
c.appMuted = false;

// Cleanup — detaches callbacks and frees the client.
c.close();
```

## Events

| Event            | Payload              |
|------------------|----------------------|
| `opened`         | —                    |
| `closed`         | —                    |
| `stateChanged`   | `PlaybackState`      |
| `audibleChanged` | `boolean`            |
| `rawTitle`       | `string`             |

Callbacks fire on background threads. If your handler touches shared state,
synchronize it yourself.

## URI builders

```js
const { uriTrack, uriSearch } = require('libspotifyctl');
c.openUri(uriTrack('11dFghVXANMlKmJXsNCbNl'));
c.openUri(uriSearch('dark side of the moon'));
```

## Development

Build the C++ DLL and copy it into `prebuilt/` before running the binding:

```sh
cmake --build --preset release-shared
cp ../../build/release-shared/Release/libspotifyctl.dll prebuilt/
node examples/now_playing.js
```

`prebuilt/` is gitignored, so a freshly built DLL never lands in commits.
