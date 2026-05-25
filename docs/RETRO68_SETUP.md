# Retro68 Toolchain Setup Guide

Setup instructions for the Retro68 cross-compiler on Apple Silicon Mac (ARM64).
Retro68 is a GCC-based cross-compilation environment targeting 68K and PowerPC classic Macs.

**Source:** https://github.com/autc04/Retro68

## What Retro68 Provides

| Tool | Purpose |
|------|---------|
| `m68k-apple-macos-gcc` / `g++` | C/C++17 compiler targeting 68K Macs |
| `Rez` | Resource compiler (`.r` → resource fork) |
| `Elf2Mac` | Converts ELF binaries to Mac executable format |
| `LaunchAPPL` | Launches built apps in emulators or on real hardware |
| `hfsutils` | HFS disk image manipulation (`hformat`, `hcopy`, etc.) |

Every build target automatically produces four output formats:
- **Mac Application** — native app with resource fork
- **MacBinary II** (`.bin`) — single-file encapsulation of data + resource forks
- **HFS Disk Image** (`.dsk`) — 1.44 MB floppy image, ready for emulators
- **AppleDouble** — separate data and resource fork files

## Prerequisites

### Required Homebrew Packages

```bash
brew install cmake gmp mpfr libmpc boost bison flex texinfo
```

### Known Apple Silicon Gotchas

1. **Dual Homebrew installations:** If you ever migrated from an Intel Mac, you may
   have both `/usr/local` (Intel) and `/opt/homebrew` (ARM64). CMake will mix
   x86_64 headers with ARM64 libraries, causing link failures. Remove the Intel
   installation first:
   ```bash
   # Check if Intel Homebrew exists
   ls /usr/local/Cellar 2>/dev/null && echo "Intel Homebrew found — remove it"
   ```

2. **Don't install hfsutils via Homebrew:** The ARM64 Homebrew formula is broken
   (missing `_hfs_vsetattr` symbol). Let the Retro68 build script build its own
   copy instead.

3. **flex is keg-only:** Homebrew installs flex to `/opt/homebrew/opt/flex/bin`
   without symlinking. The build script handles this, but if you hit issues:
   ```bash
   export PATH="/opt/homebrew/opt/flex/bin:$PATH"
   ```

## Building the Toolchain

### Clone (if not already done)

From the project root:

```bash
git clone --recursive https://github.com/autc04/Retro68.git deps/retro68/Retro68
```

The toolchain lives under `deps/retro68/` — a gitignored project subdirectory.
See [../deps/retro68/README.md](../deps/retro68/README.md).

The `--recursive` flag is critical — it pulls the `multiversal` submodule
(open-source reimplementation of Apple's Universal Interfaces).

### Build (68K only)

```bash
mkdir -p deps/retro68/Retro68-build
cd deps/retro68/Retro68-build
../Retro68/build-toolchain.bash --no-ppc --clean-after-build
```

| Flag | Effect |
|------|--------|
| `--no-ppc` | Skip PowerPC and Carbon targets (also implies `--no-carbon`) |
| `--clean-after-build` | Remove intermediate build artifacts after completion |
| `--skip-thirdparty` | On subsequent rebuilds, skip GCC/binutils (much faster) |
| `--ninja` | Use Ninja instead of Make (faster if you have it) |

**Build time:** 30–60 minutes on Apple Silicon (building GCC from source).

### Verify the Build

```bash
deps/retro68/Retro68-build/toolchain/bin/m68k-apple-macos-gcc --version
```

### Toolchain File Location

After building, the CMake toolchain file lives at:
```
deps/retro68/Retro68-build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake
```

## Header/Interface Options

### Multiversal Interfaces (Default)

Open-source reimplementation of Apple's headers. Included automatically.
Covers most System 7 APIs but is incomplete — missing Carbon, MacTCP, and
some post-7.0 additions.

### Universal Interfaces (Apple's Original)

More complete, but cannot be redistributed. To install from the MPW 3.5
Golden Master disk image:

```bash
cd deps/retro68/Retro68-build
../Retro68/interfaces-and-libraries.sh ./toolchain /path/to/MPW-GM.img.bin
```

Or pass `--universal /path/to/MPW-GM.img.bin` during the initial build.

**Note:** MPW 3.1 interfaces have different API names (`DisposHandle` vs
`DisposeHandle`). Use MPW 3.5 GM for consistency.

## Project Structure

A minimal Retro68 project:

```
MyApp/
├── CMakeLists.txt
├── main.c
└── MyApp.r          # optional Rez resource definitions
```

### Minimal CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.9)
project(MyApp C)

add_application(MyApp
    main.c
    MyApp.r
)
```

**Note:** CMake 4.x requires `cmake_minimum_required()` at the top of every
CMakeLists.txt. Retro68's built-in samples omit this, so you'll need to add
it when building samples standalone.

`add_application()` is a custom CMake function provided by the Retro68
toolchain. It handles the full pipeline: compile → link ELF → Elf2Mac →
Rez → produce all four output formats.

### Building a Project

From the project root:

```bash
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=../deps/retro68/Retro68-build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake
make
```

### Suppressing Header Warnings

The interfaces generate thousands of legacy-code warnings. Silence them:

```cmake
include_directories(SYSTEM $ENV{RETRO68_TOOLCHAIN}/universal/CIncludes/)
```

## Included Sample Applications

The Retro68 source tree includes working examples in its `Samples/` directory:

| Sample | Description |
|--------|-------------|
| `HelloWorld` | Minimal C app — good starting template |
| `Dialog` | Demonstrates Rez resource compiler for dialogs |
| `Raytracer` | Complex C/C++ with performance-sensitive rendering |
| `WDEF` | Custom window definition (68K only) |
| `SystemExtension` | INIT/extension development |

Build all samples:
```bash
cd deps/retro68/Retro68-build
make
```

## Environment Variables

For convenience inside a shell session rooted in the project, export:

```bash
export RETRO68_TOOLCHAIN="$PWD/deps/retro68/Retro68-build/toolchain"
export PATH="$RETRO68_TOOLCHAIN/bin:$PATH"
```

These are intentionally not added to `~/.zshrc` — different VibeRetro68
checkouts may pin different Retro68 versions, so binding the env var to
`$HOME` would be wrong. Re-export per shell or use a tool like `direnv`.

## Rebuilding After Changes

If you only changed Retro68's own tools (not GCC/binutils):

```bash
cd deps/retro68/Retro68-build
../Retro68/build-toolchain.bash --no-ppc --skip-thirdparty
```

This skips the lengthy GCC rebuild and finishes in minutes.

## Troubleshooting

### `Undefined symbols for architecture arm64: "boost::filesystem::..."`
Mixed Intel/ARM64 Homebrew. Remove `/usr/local/Cellar` and `/usr/local/include/boost`.

### `Undefined symbols: "_hfs_vsetattr"`
Homebrew's ARM64 hfsutils is broken. Uninstall it (`brew uninstall hfsutils`)
and rebuild Retro68 to use its bundled copy.

### `libintl.h macro conflict` (macOS 15+)
Fixed in current Retro68 master. Make sure you're on the latest commit.

### `Could NOT find Boost (missing: system)` (Boost 1.90+)

`boost_system` is header-only in modern Boost — no library or cmake config
exists. Fix by removing `system` from `find_package(Boost COMPONENTS ...)`
in these files:
- `ResourceFiles/CMakeLists.txt`
- `Rez/CMakeLists.txt`
- `ConvertObj/CMakeLists.txt`
- `PEFTools/CMakeLists.txt`

Then re-run `--skip-thirdparty` to rebuild host tools only.

### Build fails finding flex
```bash
export PATH="/opt/homebrew/opt/flex/bin:$PATH"
```
Then re-run the build script.
