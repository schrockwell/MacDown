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
docs/
  RETRO68_SETUP.md      Toolchain installation and configuration
  EMULATOR_SETUP.md     Basilisk II and Mini vMac setup
  WORKFLOW.md           Iterative dev workflow with Claude Code
scripts/
  build-and-deploy.sh   Build and copy artifact to Basilisk II shared folder
```

## Quick Start

### Prerequisites

```bash
brew install cmake gmp mpfr libmpc boost bison flex texinfo
```

### Build Retro68 (one-time, ~30-60 min)

```bash
git clone --recursive https://github.com/autc04/Retro68.git ~/Code/Retro68
mkdir ~/Code/Retro68-build && cd ~/Code/Retro68-build
../Retro68/build-toolchain.bash --no-ppc --clean-after-build
```

### Start a New Project

Create a `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.9)
project(MyApp C)

add_application(MyApp
    SOURCES src/main.c
    RESOURCES resources/MyApp.r
)
```

Configure and build:

```bash
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=~/Code/Retro68-build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake
make
```

### Test in Basilisk II

Copy the `.bin` output to the Basilisk II shared folder:

```bash
cp build/MyApp.bin ~/Code/Basilisk\ II/shared/
```

The file appears on the emulated Mac's desktop as a mounted volume.

## Toolchain

| Component | Location |
|-----------|----------|
| Retro68 source | `~/Code/Retro68` |
| Build output / toolchain | `~/Code/Retro68-build/toolchain/` |
| CMake toolchain file | `~/Code/Retro68-build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake` |
| Basilisk II | `~/Code/Basilisk II/` |
| Mini vMac | `roms/minivmac-macOS-SEFDHD.app` |

## Documentation

See the `docs/` directory for detailed guides:

- **[RETRO68_SETUP.md](docs/RETRO68_SETUP.md)** — Full toolchain reference: prerequisites, build flags, troubleshooting Apple Silicon issues, Universal vs Multiversal interfaces
- **[EMULATOR_SETUP.md](docs/EMULATOR_SETUP.md)** — Basilisk II (interactive testing) and Mini vMac (automated testing) configuration
- **[WORKFLOW.md](docs/WORKFLOW.md)** — The edit-build-test loop using Claude Code as a coding partner
