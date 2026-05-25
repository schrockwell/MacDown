# VibeRetro68

A reference guide and project template for building classic Macintosh (System 6 and 7, 68K) applications on modern Apple Silicon Macs using the Retro68 cross-compiler toolchain.

## Using This for a New Project

This repo is a template. If you're starting your own project, download a zip instead of cloning so you get a clean slate without our git history:

1. Click **Code → Download ZIP** on GitHub, or:

   ```bash
   curl -L https://github.com/erikbuild/VibeRetro68/archive/refs/heads/main.zip -o VibeRetro68.zip
   unzip VibeRetro68.zip
   cd VibeRetro68-main
   ```

2. Initialize your own repo:

   ```bash
   git init
   git add -A
   git commit -m "Initial commit from VibeRetro68 template"
   ```

## What's Here

```
src/                    Your project source (C/C++) — create as needed
resources/              Rez resource definitions (.r files) — create as needed
Brewfile                Homebrew prerequisites (cmake, boost, flex, …)
docs/
  RETRO68_SETUP.md      Toolchain installation and configuration
  EMULATOR_SETUP.md     Basilisk II and Mini vMac setup
  WORKFLOW.md           Iterative dev workflow with Claude Code
scripts/
  setup.sh              One-shot: fetch-deps → build-retro68 → doctor
  fetch-deps.sh         Download Retro68 source, emulators, ROMs into deps/
  build-retro68.sh      Build the Retro68 cross-compiler (~30-60 min, one-time)
  doctor.sh             Diagnose missing or misconfigured pieces of deps/
  run-basiliskii.sh     Build, copy .bin to Basilisk II shared folder, launch the emulator
  run-minivmac.sh       Build and (re)launch Mini vMac with the resulting .dsk
deps/                   Retro68 toolchain + emulators (gitignored — see deps/*/README.md)
```

Put your source files under `src/`.

## Quick Start

### One-shot setup

```bash
scripts/setup.sh
```

That runs the four sub-steps in order:

| Step | What it does |
|------|--------------|
| 1. `brew bundle` | Install the Homebrew formulae listed in [Brewfile](Brewfile) (cmake, boost, flex, etc.) |
| 2. `fetch-deps.sh` | Clone Retro68, download emulator binaries, ROMs, and the System 7.5.3 disk image into `deps/` |
| 3. `build-retro68.sh` | Build the Retro68 toolchain (~30-60 min, one-time) and configure the project's `build/` against it |
| 4. `doctor.sh` | Verify every piece is in place; exits non-zero on any failure |

Homebrew itself is required up front — `setup.sh` errors out with
install instructions if `brew` isn't on `PATH`.

Each sub-script is idempotent, so `setup.sh` is safe to re-run after a
partial install or a `git pull` that adds new deps. You can also invoke
any sub-script directly if you only need that step.

The `deps/` directory is gitignored — every clone builds its own
toolchain. See [deps/retro68/README.md](deps/retro68/README.md) for layout.

### Start a New Project

Create a `CMakeLists.txt` at the project root:

```cmake
cmake_minimum_required(VERSION 3.9)
project(MyApp C)

add_application(MyApp
    src/main.c
    resources/MyApp.r
)
```

No separate CMake-configure step needed — `scripts/build-retro68.sh`
configures `build/` against the toolchain on its way out. If you ever
delete `build/`, just re-run `scripts/build-retro68.sh` and it'll
reconfigure (the toolchain build itself is already cached).

### Edit → Build → Run

Pick the emulator that fits the moment:

```bash
scripts/run-basiliskii.sh    # System 7.5.3 / Quadra 950 — interactive testing
scripts/run-minivmac.sh      # Mac SE FDHD — fast, minimal, drag-disk workflow
```

Each script does `cmake --build build/` first, then hands off to the emulator:

- **`run-basiliskii.sh`** drops the freshly-built `.bin` into
  `deps/basiliskii/shared/` and launches Basilisk II if it isn't already
  running. Basilisk II's shared folder is `extfs`-synced live, so a
  running emulator picks up the new `.bin` automatically — no restart
  needed when iterating.
- **`run-minivmac.sh`** kills any running Mini vMac, then relaunches it
  with the fresh `MyApp.dsk` (or whatever `.dsk` your build produced).
  The kill-first ordering matters: Mini vMac mmaps the disk image, and
  overwriting it under a live emulator corrupts the resource fork.

Optional arg picks a specific app name when you have multiple outputs:
`scripts/run-basiliskii.sh MyApp` or `scripts/run-minivmac.sh MyApp`.

### Testing on Real Hardware

Mount `build/` over AFP on the classic Mac to easily run the compiled application.

The [TashTalk USB](https://www.tindie.com/products/feralfirmware/tashtalk-usb/) device and [GUI interface](https://github.com/FeralFirmware/TailTalk/releases) are recommended.

## Toolchain

All toolchain components live under `deps/retro68/` (gitignored — every clone builds its own).

| Component | Location |
|-----------|----------|
| Retro68 source | `deps/retro68/Retro68/` |
| Build output / toolchain | `deps/retro68/Retro68-build/toolchain/` |
| CMake toolchain file | `deps/retro68/Retro68-build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake` |
| Basilisk II | `deps/basiliskii/` |
| Mini vMac | `deps/minivmac/minivmac-macOS-SEFDHD.app` |

## Documentation

See the `docs/` directory for detailed guides:

- **[RETRO68_SETUP.md](docs/RETRO68_SETUP.md)** — Full toolchain reference: prerequisites, build flags, troubleshooting Apple Silicon issues, Universal vs Multiversal interfaces
- **[EMULATOR_SETUP.md](docs/EMULATOR_SETUP.md)** — Basilisk II (interactive testing) and Mini vMac (automated testing) configuration
- **[WORKFLOW.md](docs/WORKFLOW.md)** — The edit-build-test loop using Claude Code as a coding partner
