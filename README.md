# VibeRetro68

A reference guide and project template for building classic Macintosh (System 7, 68K) applications on modern Apple Silicon Macs using the Retro68 cross-compiler toolchain.

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
docs/
  RETRO68_SETUP.md      Toolchain installation and configuration
  EMULATOR_SETUP.md     Basilisk II and Mini vMac setup
  WORKFLOW.md           Iterative dev workflow with Claude Code
scripts/
  fetch-deps.sh         Download Retro68 source, emulators, ROMs into deps/
  build-retro68.sh      Build the Retro68 cross-compiler (~30-60 min, one-time)
  doctor.sh             Diagnose missing or misconfigured pieces of deps/
  build-and-deploy.sh   Build the project and copy artifact to Basilisk II shared folder
deps/                   Retro68 toolchain + emulators (gitignored — see deps/*/README.md)
```

Put your source files under `src/`. The template's CMakeLists.txt and
`scripts/build-and-deploy.sh` both assume that layout.

## Quick Start

### Prerequisites

```bash
brew install cmake gmp mpfr libmpc boost bison flex texinfo
```

### Fetch Sources and Build Retro68

```bash
scripts/fetch-deps.sh       # downloads Retro68 source, emulators, ROMs
scripts/build-retro68.sh    # builds the toolchain (~30-60 min, one-time)
scripts/doctor.sh           # verifies the install
```

The `deps/` directory is gitignored — every clone builds its own
toolchain. See [deps/retro68/README.md](deps/retro68/README.md) for layout.

### Start a New Project

Create a `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.9)
project(MyApp C)

add_application(MyApp
    src/main.c
    resources/MyApp.r
)
```

Configure and build:

```bash
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=../deps/retro68/Retro68-build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake
make
```

### Test in Basilisk II

Copy the `.bin` output to the Basilisk II shared folder:

```bash
cp build/MyApp.bin deps/basiliskii/shared/
```

The file appears on the emulated Mac's desktop as a mounted volume.

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
