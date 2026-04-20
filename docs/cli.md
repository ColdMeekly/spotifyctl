# `spotifyctl` CLI

A single Windows x64 binary that wraps libspotifyctl for shells and scripts.

```sh
spotifyctl <command> [args] [--flags]
```

## Global flags

| Flag | Default | Meaning |
|---|---|---|
| `--wait-ms <N>` | `1500` | Wait up to N ms for the Spotify window before issuing transport commands. |
| `--help`, `-h` | | Print usage and exit. |

## Commands

### `version`

Prints the semver from `spotify/version.h`.

### `now-playing [--json|--tsv]`

Prints a single snapshot of the current state and exits.

- Default: human-readable.
- `--json`: one JSON object.
- `--tsv`: tab-separated fields (for feeding `awk` / `cut`).

### `watch [--json]`

Subscribes to state changes and streams one line per delta until Ctrl-C.

- Default: human-readable.
- `--json`: newline-delimited JSON (NDJSON) — one object per line.

### `play` · `pause` · `toggle` · `next` · `prev`

Transport commands. Prefer SMTC, fall back to media keys. Exit 4 if the
command couldn't be delivered.

### `seek <time>`

Move the playhead. Accepts:

- `90` or `90s` — 90 seconds
- `500ms`
- `1:23` — 1 minute 23 seconds
- `+30s`, `-15s` — relative to current position

### `volume [get | set <0..1> | up | down]`

Per-app volume. `get` prints the current value in `[0,1]`, or `-1` if the
audio session hasn't resolved yet.

### `mute [on | off | toggle]`

Per-app mute.

### `url <spotify:uri>`

Calls `OpenUri` on the given URI and exits.

### `search <query>`

Shortcut for `url spotify:search:<percent-encoded query>`.

## Exit codes

| Code | Meaning |
|---|---|
| 0 | Success |
| 2 | Usage error (bad flags / subcommand) |
| 3 | Spotify not detected within `--wait-ms` |
| 4 | Command failed (SMTC/audio channel unavailable) |

## Examples

```sh
spotifyctl now-playing --json | jq -r '"\(.artist) — \(.title)"'

# Keep a log of every track:
spotifyctl watch --json | jq -r 'select(.status == "PLAYING") | "\(.artist)\t\(.title)"'

# Fade to 25% and mute on next:
spotifyctl volume set 0.25 && spotifyctl mute on

# Jump to 1:23:
spotifyctl seek 1:23
```
