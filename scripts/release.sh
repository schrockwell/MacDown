#!/bin/bash
# Package MacDown into a redistributable zip with the .bin (MacBinary —
# the universal classic-Mac container) and a .dsk (Disk Copy 4.2 image
# for Mini vMac), plus a short README. The zip is the broadest-reach
# format: modern users on any OS can unzip it, then route the .bin
# through MacBinary-aware emulators or transfer the .dsk into Mini vMac.
#
# Every artifact is named MacDown-<version>.<ext>, and the application
# inside the .bin/.hqx/.dsk/.img is named "MacDown <version>". The
# build/ outputs keep the plain "MacDown" name.
#
# Usage: ./scripts/release.sh [version]
#   version — optional (default: the contents of /VERSION)

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
RELEASE_DIR="$PROJECT_ROOT/release"

TAG="${1:-$(tr -d '[:space:]' < "$PROJECT_ROOT/VERSION")}"
if [ -z "$TAG" ]; then
    echo "error: no version given and $PROJECT_ROOT/VERSION is empty" >&2
    exit 1
fi
NAME="MacDown-$TAG"
APP_NAME="MacDown $TAG"
STAGE="$RELEASE_DIR/$NAME"

# Always build first so the release reflects current source. Reuses the
# auto-configure path in `make build` if build/ is missing.
echo "==> Building MacDown"
(cd "$PROJECT_ROOT" && make build >/dev/null)

BIN="$BUILD_DIR/MacDown.bin"
DSK="$BUILD_DIR/MacDown.dsk"
if [ ! -f "$BIN" ] || [ ! -f "$DSK" ]; then
    echo "error: build outputs missing under $BUILD_DIR" >&2
    exit 1
fi

for tool in /usr/bin/macbinary /usr/bin/binhex; do
    if [ ! -x "$tool" ]; then
        echo "error: $tool not found — needs Apple's binhex/macbinary tools (macOS)" >&2
        exit 1
    fi
done
# hfsutils ship with the Retro68 toolchain; fall back to PATH.
HFS_BIN="$PROJECT_ROOT/deps/retro68/Retro68-build/toolchain/bin"
hfs() {
    local t="$1"; shift
    if [ -x "$HFS_BIN/$t" ]; then "$HFS_BIN/$t" "$@"; else "$t" "$@"; fi
}

echo "==> Staging $NAME/ (application named \"$APP_NAME\")"
rm -rf "$STAGE"
mkdir -p "$STAGE"

WORK="$(mktemp -d "${TMPDIR:-/tmp}/MacDown-release-XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

# .bin / .hqx: decode the build's MacBinary to a real two-fork file under
# the versioned app name, then re-encode. Both containers embed the file
# name, so this is what the app is called once unpacked on the Mac.
/usr/bin/macbinary decode -C "$WORK" -o "$APP_NAME" "$BIN" >/dev/null
/usr/bin/macbinary encode -t 2 -o "$STAGE/$NAME.bin" "$WORK/$APP_NAME" >/dev/null
/usr/bin/binhex encode -o "$STAGE/$NAME.hqx" "$WORK/$APP_NAME" >/dev/null

# .dsk: rename the app on the HFS floppy image, then rebuild the Disk
# Copy 4.2 .img from it so both images match.
cp "$DSK" "$STAGE/$NAME.dsk"
hfs hmount "$STAGE/$NAME.dsk" >/dev/null
hfs hrename ":MacDown" ":$APP_NAME"
hfs humount >/dev/null
python3 "$PROJECT_ROOT/scripts/raw-to-dc42.py" \
    "$STAGE/$NAME.dsk" "$STAGE/$NAME.img" MacDown >/dev/null
if command -v SetFile >/dev/null 2>&1; then
    SetFile -t dCp4 -c dCpy "$STAGE/$NAME.img"
fi

cat > "$STAGE/README.txt" <<EOF
MacDown — a small Markdown editor for classic Mac OS (System 6.0.8+)

This archive contains two ways to get MacDown onto a classic Mac:

  $NAME.hqx
      BinHex 4.0 — text-only, 7-bit ASCII, embeds the file name, type,
      creator, Finder flags, and both forks. The most bulletproof
      classic-Mac transfer format because nothing in the route can
      strip metadata (every byte is text).

      Real hardware / emulator:
        drag onto StuffIt Expander on the receiving Mac; it decodes
        to a real MacDown application with the correct type / creator
        / icon in one step.

  $NAME.bin
      MacBinary II application. Data fork, resource fork, and Finder
      info bundled into a single file. Smaller than .hqx but only
      survives binary-safe transports.

      Basilisk II / SheepShaver:
        drop into the emulator's shared folder; the File Manager
        unpacks it automatically.

      Real hardware:
        transfer and unpack with StuffIt Expander. May need its file
        type set to 'BINA' on the receiving Mac if the transport
        stripped Finder metadata.

  $NAME.dsk
      Raw 800K disk image — bytes only, no Disk Copy header.

      Mini vMac / Basilisk II:
        drag-and-drop onto the emulator window, or pass it as a
        command-line argument when launching.

  $NAME.img
      Disk Copy 4.2 disk image (same disk content as $NAME.dsk, but
      with the 84-byte DC 4.2 header + checksums and type=dCp4).

      Real System 6/7 hardware:
        copy to the Mac with metadata preserved (AppleShare,
        AppleDouble FTP, HFS floppy, Floppy Emu). Double-click in the
        Finder mounts the image via Disk Copy 4.2 / MountImage /
        ShrinkWrap.

System requirements
  - 68K Macintosh (or PowerPC running 68K emulation)
  - System 6.0.8 or System 7+
  - ~256K free memory

Features
  - Markdown-aware styling: headers, bold/italic, lists, task lists,
    horizontal rules
  - Multi-document with a Windows menu
  - List auto-continuation; Cmd-L to toggle task checkboxes
  - Cmd-D duplicates the current line; Cmd-[/Cmd-] outdent/indent
  - Opt-Up/Down moves the current line up or down
  - Preserves CR / LF / CRLF line endings

Built with the Retro68 cross-compiler. Source:
  https://github.com/rockwellschrock/VibeRetro68
EOF

echo "==> Zipping $NAME.zip"
ZIP="$RELEASE_DIR/$NAME.zip"
rm -f "$ZIP"
(cd "$RELEASE_DIR" && zip -qr "$NAME.zip" "$NAME")

# Keep the staging dir around — handy for inspecting contents — but
# the .zip is what users get.
echo
echo "Release: $ZIP"
ls -lh "$ZIP" | awk '{print "  size: " $5}'
echo "  contents:"
unzip -l "$ZIP" | sed 's/^/    /'
