// Live now-playing watcher — prints a line on every state change.
//
// Run with Spotify open and playing:
//   node bindings/node/examples/now_playing.js

'use strict';

const { SpotifyClient, Status } = require('..');

function fmt(ms) {
  const s = Math.max(ms | 0, 0) / 1000 | 0;
  return `${(s / 60) | 0}:${String(s % 60).padStart(2, '0')}`;
}

function main() {
  const client = new SpotifyClient();

  const render = (state) => {
    const pos = fmt(state.positionMs);
    const dur = fmt(state.durationMs);
    const label = state.statusName.toLowerCase();
    if (state.status === Status.UNKNOWN) {
      console.log(`[${label}] (Spotify not detected)`);
    } else {
      console.log(`[${label}] ${state.artist} — ${state.title}  [${pos} / ${dur}]`);
    }
  };

  client.on('stateChanged', render);

  const shutdown = () => {
    client.close();
    process.exit(0);
  };
  process.on('SIGINT', shutdown);
  process.on('SIGTERM', shutdown);

  client.start();
  render(client.latestState());  // initial baseline
}

main();
