#!/bin/bash
# Build the Retro68 cross-compiler toolchain into
# deps/retro68/Retro68-build/. Run after scripts/fetch-deps.sh has
# cloned the source and after the Homebrew prerequisites are installed
# (scripts/doctor.sh will tell you what's missing).
#
# Idempotent: if the m68k-apple-macos-gcc binary already exists, this
# exits without re-running the build. Delete
# deps/retro68/Retro68-build/ to force a clean rebuild.
#
# Usage: ./scripts/build-retro68.sh

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RETRO68_HOME="$PROJECT_ROOT/deps/retro68"
RETRO68_SRC="$RETRO68_HOME/Retro68"
BUILD_DIR="$RETRO68_HOME/Retro68-build"
GCC="$BUILD_DIR/toolchain/bin/m68k-apple-macos-gcc"

if [ ! -d "$RETRO68_SRC/.git" ]; then
    echo "Retro68 source missing at $RETRO68_SRC" >&2
    echo "Run scripts/fetch-deps.sh first." >&2
    exit 1
fi

if [ -x "$GCC" ]; then
    echo "==> Toolchain already built ($("$GCC" --version | head -1))"
    echo "    Delete $BUILD_DIR to force a clean rebuild."
    exit 0
fi

# Homebrew's flex is keg-only and not symlinked into /opt/homebrew/bin.
# The Retro68 build script needs a modern flex; without this PATH bump
# it falls back to the macOS system flex and bombs out partway through.
if [ -d /opt/homebrew/opt/flex/bin ]; then
    export PATH="/opt/homebrew/opt/flex/bin:$PATH"
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo "==> Building Retro68 toolchain (~30–60 min on Apple Silicon)"
echo "    Source: $RETRO68_SRC"
echo "    Build:  $BUILD_DIR"
echo

../Retro68/build-toolchain.bash --no-ppc --clean-after-build

if [ ! -x "$GCC" ]; then
    echo >&2
    echo "Build finished but $GCC is missing." >&2
    echo "Check the output above for errors." >&2
    exit 1
fi

cat <<EOF

================================================================
Build complete: $("$GCC" --version | head -1)

Toolchain installed at:
    $BUILD_DIR/toolchain

For convenience in this shell session:
    export RETRO68_TOOLCHAIN="$BUILD_DIR/toolchain"
    export PATH="\$RETRO68_TOOLCHAIN/bin:\$PATH"

Then point CMake at the toolchain file:
    -DCMAKE_TOOLCHAIN_FILE=$BUILD_DIR/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake
================================================================
EOF
