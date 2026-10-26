# Building the client

The new client is C++20, built with CMake and Ninja. Third-party libraries are pinned in
`cmake/dependencies.cmake` by release archive and SHA-256. CMake uses an installed
package when it finds one (Homebrew's SDL3, for example), and otherwise downloads the
pinned release and builds it with the project (`FetchContent`). A library is added
there by the milestone that first uses it; so far SDL3 3.4.18 and, for the tests,
Catch2 3.16.1.

## Prerequisites

- CMake 3.25 or newer, Ninja, and a C++20 compiler: Apple Clang (Xcode), GCC 12+ or
  Clang 15+ on Linux, MSVC 2022 on Windows (run from a developer prompt).
- Network access on the first configure, unless every library is installed.
- On Linux, SDL3 builds against the system's X11, Wayland, input and D-Bus headers;
  the package list is in `.github/workflows/ci.yml`.

## Presets

| Preset | Build | For |
|--------|-------|-----|
| `dev` | Debug, AddressSanitizer and UndefinedBehaviorSanitizer (AddressSanitizer only with MSVC) | everyday work and CI |
| `release` | RelWithDebInfo | playing and profiling |

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

The sanitizers apply to the project's own targets only; the libraries are built without
them. The test presets select SDL's `dummy` video driver, so the tests need no display.

To build the pinned versions even where a package is installed, as CI does:

```bash
cmake --preset dev -DFETCHCONTENT_TRY_FIND_PACKAGE_MODE=NEVER
```

A fetched SDL3 is linked statically; an installed one is used as it is (shared, for
Homebrew).

## Running

```bash
./build/dev/src/ragexe
```

The window size and full-screen flag are in `settings.txt` in the user's data directory
(`~/Library/Application Support/ragexe-recomp/client` on macOS,
`~/.local/share/ragexe-recomp/client` on Linux, `%APPDATA%\ragexe-recomp\client` on
Windows), created with defaults on the first run. Options for unattended runs:

| Option | Effect |
|--------|--------|
| `--frames N` | quit after N frames |
| `--auto-login N` | leave the login mode for the game mode after N frames |
| `--map NAME` | the map `--auto-login` enters (default `prontera.rsw`) |

## Layout

`src/` has one static library per layer: `core` (no SDL), `platform` (SDL), `game`
(modes), and the executable in `app/`. The remaining layers of `docs/PLAN.md` §3
(`res`, `render`, `audio`, `net`, `ui`, `world`) arrive with the milestones that port
them. Unit tests (Catch2) are in `tests/unit`; the Python tests of the analysis tools
stay in `tests/` and run with `unittest`.
