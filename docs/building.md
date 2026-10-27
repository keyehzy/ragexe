# Building the client

The new client is C++20, built with CMake and Ninja. Third-party libraries are pinned in
`cmake/dependencies.cmake` by release archive and SHA-256. CMake uses an installed
package when it finds one (Homebrew's SDL3, for example), and otherwise downloads the
pinned release and builds it with the project (`FetchContent`). A library is added
there by the milestone that first uses it; so far SDL3 3.4.18, zlib 1.3.2 and, for the
tests, Catch2 3.16.1.

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

A fetched SDL3 or zlib is linked statically; an installed one is used as it is (shared
SDL3 from Homebrew, the system zlib on macOS and Linux).

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

## ccache

When `ccache` is on the `PATH`, every compile goes through it, including the fetched
libraries (`RAGEXE_CCACHE`, on by default). With MSVC the debug information then goes
into the object files (`/Z7`), which ccache can cache. CI keeps a cache per platform.

## Formatting and lint

`clang-format` and `clang-tidy` are pinned in `tools/requirements-lint.txt`. Install
them into the project's virtual environment:

```bash
.venv/bin/pip install -r tools/requirements-lint.txt
```

Format everything (Google style, 100 columns, `.clang-format`):

```bash
git ls-files -z '*.cpp' '*.h' | xargs -0 .venv/bin/clang-format -i
```

Lint a configured build. The checks are in `.clang-tidy`, and every finding is an error;
on macOS, also pass `--extra-arg=-isysroot$(xcrun --show-sdk-path)`:

```bash
git ls-files 'src/*.cpp' 'tests/unit/*.cpp' 'fuzz/*.cpp' | xargs .venv/bin/clang-tidy -p build/dev --quiet
```

CI runs both: the format check as its own job, and clang-tidy in the Linux job after
configuring.

## Fuzzing

The parsers have libFuzzer targets in `fuzz/`:

| Target | Input |
|--------|-------|
| `grf_table_fuzz` | `ParseGrfTable` |
| `grf_archive_fuzz` | a whole archive from memory: header, table, lookups, reads |
| `res_name_table_fuzz` | `ParseResNameTable` |
| `bitmap_fuzz` | the BMP, TGA and JPEG decoders |
| `sprite_fuzz` | the sprite decoder, at each detail level |
| `act_fuzz` | the action decoder, with a stand-in sprite |
| `gat_fuzz` | the attribute-map decoder |
| `gnd_fuzz` | the ground decoder |
| `rsw_fuzz` | the world decoder, with ISLIGHTMAP on or off |
| `rsm_fuzz` | the model decoder, with its face sort and normals |
| `rsx_fuzz` | the animated-model decoder |
| `imf_fuzz` | the `.imf` decoder |
| `rma_fuzz` | the `.rma` text decoder and its tokenizer |
| `str_fuzz` | the effect decoder |

Each target aborts when an invariant breaks, which the fuzzer reports as a crash.

**Building them** needs LLVM Clang, because Apple Clang has no libFuzzer; on macOS use
Homebrew's `llvm`. The `fuzz` preset builds the pinned libraries, optimized, with the
sanitizers:

```bash
CC=$(brew --prefix llvm)/bin/clang CXX=$(brew --prefix llvm)/bin/clang++ cmake --preset fuzz
```

```bash
cmake --build --preset fuzz --target grf_table_fuzz grf_archive_fuzz res_name_table_fuzz bitmap_fuzz sprite_fuzz act_fuzz gat_fuzz gnd_fuzz rsw_fuzz rsm_fuzz rsx_fuzz imf_fuzz rma_fuzz str_fuzz make_grf_seeds
```

**Running one:** `make_grf_seeds` writes small valid archives and tables to start from,
and `fuzz/grf.dict` holds the format's tokens. Ten minutes on the archive target:

```bash
build/fuzz/fuzz/make_grf_seeds seeds
```

```bash
build/fuzz/fuzz/grf_archive_fuzz corpus seeds/grf_archive -dict=fuzz/grf.dict -max_len=8192 -max_total_time=600
```

**A crash** leaves its input in a `crash-…` file. Save it in
`fuzz/regressions/<target>/` once fixed.

**In `ctest`,** every build replays the seeds and the saved regressions through each
target (`*_fuzz_replay`). Without libFuzzer the targets are linked with
`fuzz/replay_main.cpp`, which runs them over the inputs once.

**In CI,** a separate job fuzzes each target for 60 seconds and keeps any crashing
input as an artifact.

## ragres

`ragres` reads the game's archives (format: `docs/formats/grf.md`):

```bash
./build/dev/src/ragres list original/install/data.grf
```

```bash
./build/dev/src/ragres extract original/install/data.grf out 'data\clientinfo.xml'
```

```bash
./build/dev/src/ragres check original/install/data.grf original/install/*.gpf
```

- **`list`** prints size, stored size, flags, offset and name.
- **`extract`** writes every file, or only the named ones, under the output directory.
- **`check`** reads and inflates every entry.

Names are matched without regard to A–Z case, and inside the archive `\` is the
separator. When `original/install` is present at configure time, `ctest` also runs
`check` on all its archives (`grf_check_original`).

## Layout

`src/` has one static library per layer:
- `core` (no SDL);
- `platform` (SDL);
- `res` (archives and resources; zlib);
- `game` (modes).

The executables are in `app/` (the client) and `ragres/`. The remaining layers of
`docs/PLAN.md` §3 (`render`, `audio`, `net`, `ui`, `world`) arrive with the
milestones that port them. Unit tests (Catch2) are in `tests/unit`; the Python tests of the analysis tools
stay in `tests/` and run with `unittest`.
