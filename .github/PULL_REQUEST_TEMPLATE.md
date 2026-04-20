<!-- Thanks for sending a PR! Please fill in the sections below. -->

## Summary

<!-- One or two sentences: what does this change, and why? -->

## Changes

<!-- Bullet list of the substantive changes. -->

-
-

## Affected surfaces

<!-- Tick everything that applies. -->

- [ ] Core C++ library (`include/spotify/`, `src/`)
- [ ] C ABI (`include/spotify/c_api.h`, `src/c_api.cpp`)
- [ ] CLI (`cli/`)
- [ ] Python binding (`bindings/python/`)
- [ ] Node binding (`bindings/node/`)
- [ ] Build / CI (`CMakeLists.txt`, `.github/workflows/`)
- [ ] Docs / README

## Testing

<!-- How did you verify this? Copy-paste ctest/pytest/node --test output if useful. -->

- [ ] `ctest --preset release` (or equivalent)
- [ ] `pytest bindings/python` (if Python touched)
- [ ] `node --test bindings/node/test` (if Node touched)
- [ ] Manual check with Spotify running (if behavior-affecting)

## Breaking changes?

<!-- If yes, describe the break and the migration. Add a `### Changed` entry to CHANGELOG.md. -->

- [ ] This PR is a breaking change.

## Checklist

- [ ] Relevant `CHANGELOG.md` entry added under `[Unreleased]`
- [ ] Updated docs if public API changed
- [ ] No unrelated formatting / whitespace changes
