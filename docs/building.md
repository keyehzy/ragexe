# Building the client

The new client is C++20, built with CMake and Ninja; its libraries come from vcpkg in
manifest mode (`vcpkg.json`, pinned by `builtin-baseline`).

## Prerequisites

- CMake 3.25 or newer, Ninja, and a C++20 compiler: Apple Clang (Xcode), GCC 12+ or
  Clang 15+ on Linux, MSVC 2022 on Windows (run from a developer prompt).
- vcpkg. Any checkout works when `VCPKG_ROOT` points at it. A local one inside the
  project is ignored by git:

  ```bash
  git clone https://github.com/microsoft/vcpkg.git .vcpkg
  ./.vcpkg/bootstrap-vcpkg.sh -disableMetrics
  export VCPKG_ROOT=$PWD/.vcpkg
  ```
- On Linux, SDL3 builds against the system's X11, Wayland and input headers; the list
  is in `.github/workflows/ci.yml`.

The first configure builds the dependencies (a few minutes, mostly SDL3); later ones
reuse vcpkg's binary cache.

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

The test presets select SDL's `dummy` video driver, so the tests need no display.

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
