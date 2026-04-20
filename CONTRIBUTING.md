# Contributing to libspotifyctl

Thanks for your interest in improving libspotifyctl. This project is
Windows-only (SMTC + Core Audio + window-title hook) and targets x64.

## Reporting issues

Open a GitHub issue using one of the templates. For bugs, please include:
- Windows build number (`winver`)
- Spotify desktop version
- Minimal repro (paste into `tests/` if you can)
- What you saw vs. what you expected

## Development setup

Requires Visual Studio 2022 (MSVC 19.30+), CMake 3.21+, and git.

### Core library (C++)

```sh
cmake --preset release
cmake --build --preset release
ctest --preset release --output-on-failure
```

For the DLL + CLI variant:

```sh
cmake --preset release-shared
cmake --build --preset release-shared
```

### Python bindings

```sh
cd bindings/python
# Copy a freshly built DLL into the prebuilt dir (gitignored).
cp ../../build/release-shared/Release/libspotifyctl.dll libspotifyctl/_prebuilt/
pip install -e .
pytest
```

### Node bindings

```sh
cd bindings/node
cp ../../build/release-shared/Release/libspotifyctl.dll prebuilt/
npm install
node --test test/
```

## Code style

- **C++:** 4-space indent, no tabs. Run `clang-format -i` on changed files
  using the repo-root `.clang-format`.
- **Python:** PEP 8, 4-space indent, `from __future__ import annotations`
  at the top of each module.
- **JavaScript:** 2-space indent, single quotes, semicolons.

Don't mix unrelated changes in one PR. Don't reformat files you didn't edit.

## Tests

Any non-trivial change should add at least one test:
- C++ tests under `tests/` (Catch2, CMake-only path).
- C ABI tests under `tests/test_c_api.cpp`.
- Python tests under `bindings/python/tests/`.
- Node tests under `bindings/node/test/`.

Tests that require Spotify to be running should skip when it isn't (see
`_requires_no_spotify` helper in `bindings/python/tests/test_smoke.py`).

## Commit + PR

- One logical change per commit. Imperative subject, < 72 chars.
- Reference the issue in the body, not the subject.
- Open PRs against `main`. CI must be green.
- If you touch the public C ABI, bump `CHANGELOG.md` under `[Unreleased]` and
  note whether it's additive or breaking.

## Releases

- Every push / PR runs the full CI matrix (static, shared, MSBuild, Python,
  Node) as a verification pass. Workflow artifacts are available from the
  Actions tab but no GitHub release is created.
- Tag `vX.Y.Z` on `main` to cut a stable release. CI publishes the full
  artifact set to a new GitHub release, to PyPI (via OIDC trusted
  publisher), and to NPM (via `NPM_TOKEN` secret).

## Code of Conduct

This project follows the [Contributor Covenant](CODE_OF_CONDUCT.md). Report
issues via GitHub issues or by contacting the repo owner.
