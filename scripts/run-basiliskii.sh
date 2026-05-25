#!/bin/bash
# Build the project, drop the .bin into Basilisk II's shared folder, and
# launch Basilisk II if it's not already running. Unlike Mini vMac, Basilisk
# II's shared folder (extfs) syncs live with the host, so a running
# emulator picks up the new .bin automatically — no kill/relaunch needed.
#
# Usage: ./scripts/run-basiliskii.sh [app-name]
#   app-name defaults to whatever .bin is newest in build/

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
BASILISKII_DIR="$PROJECT_ROOT/deps/basiliskii"
SHARED_DIR="$BASILISKII_DIR/shared"
BASILISKII_APP="$BASILISKII_DIR/BasiliskII.app"
BASILISKII_BIN="$BASILISKII_APP/Contents/MacOS/BasiliskII"
PREFS="$BASILISKII_DIR/prefs"

# Bootstrap build/ on first run (or after `make clean`).
if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    echo "==> Configuring build/ with the Retro68 toolchain"
    mkdir -p "$BUILD_DIR"
    (cd "$BUILD_DIR" && cmake "$PROJECT_ROOT" \
        -DCMAKE_TOOLCHAIN_FILE="$PROJECT_ROOT/deps/retro68/Retro68-build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake")
fi

cmake --build "$BUILD_DIR"

# Regenerate the project-local prefs file every run. BasiliskII needs
# absolute paths for rom/disk/extfs, so we can't ship a static file — it
# has to be derived from $PROJECT_ROOT. Using --config keeps these prefs
# scoped to the project; ~/.basilisk_ii_prefs is left untouched.
cat > "$PREFS" <<EOF
rom $BASILISKII_DIR/Quadra.rom
disk $BASILISKII_DIR/System753.dsk
extfs $SHARED_DIR
ramsize 67108864
modelid 14
cpu 4
fpu true
jit true
jitfpu true
frameskip 0
scale_nearest true
EOF

if [ -n "$1" ]; then
    BIN="$BUILD_DIR/$1.bin"
else
    BIN="$(ls -t "$BUILD_DIR"/*.bin 2>/dev/null | grep -v "\.code\.bin$" | head -1)"
fi

if [ ! -f "$BIN" ]; then
    echo "No .bin found in $BUILD_DIR" >&2
    echo "Available outputs:" >&2
    ls "$BUILD_DIR"/*.bin "$BUILD_DIR"/*.dsk 2>/dev/null >&2 || echo "  (none)" >&2
    exit 1
fi

mkdir -p "$SHARED_DIR"
cp "$BIN" "$SHARED_DIR/"
echo "Deployed $(basename "$BIN") to Basilisk II shared folder"

if ! pgrep -f "$BASILISKII_BIN" >/dev/null 2>&1; then
    # Launch the binary directly so we can pass --config. `open -a` would
    # ignore the flag (Apple's launcher discards unknown args to .app
    # bundles), which is what made the emulator quit immediately on a
    # missing rom/disk/extfs.
    nohup "$BASILISKII_BIN" --config "$PREFS" >/dev/null 2>&1 &
    disown
    echo "Launched Basilisk II (config: $PREFS)"
else
    echo "Basilisk II already running — shared folder will update live"
fi
