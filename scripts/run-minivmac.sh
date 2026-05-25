#!/bin/bash
# Build the project and (re)launch Mini vMac with the resulting .dsk.
# Kills any running Mini vMac *before* building — the emulator holds the
# disk image mmap'd while running, and rewriting it underneath can produce
# a corrupted .dsk (silently missing its resource fork).
#
# Usage: ./scripts/run-minivmac.sh [app-name]
#   app-name defaults to whatever .dsk is found in build/

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
MINIVMAC_APP="$PROJECT_ROOT/deps/minivmac/minivmac-macOS-SEFDHD.app"

pkill -f "$MINIVMAC_APP" 2>/dev/null && sleep 1 || true

cmake --build "$BUILD_DIR"

if [ -n "$1" ]; then
    DSK="$BUILD_DIR/$1.dsk"
else
    DSK="$(ls -t "$BUILD_DIR"/*.dsk 2>/dev/null | head -1)"
fi

if [ ! -f "$DSK" ]; then
    echo "No .dsk found in $BUILD_DIR" >&2
    exit 1
fi

open -a "$MINIVMAC_APP" "$DSK"
echo "Launched Mini vMac with $(basename "$DSK")"
