#!/bin/bash
# Package MdEdit into a redistributable zip with the .bin (MacBinary —
# the universal classic-Mac container) and a .dsk (Disk Copy 4.2 image
# for Mini vMac), plus a short README. The zip is the broadest-reach
# format: modern users on any OS can unzip it, then route the .bin
# through MacBinary-aware emulators or transfer the .dsk into Mini vMac.
#
# Usage: ./scripts/release.sh [tag]
#   tag — optional suffix (default: today's date as YYYYMMDD)

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
RELEASE_DIR="$PROJECT_ROOT/release"

TAG="${1:-$(date +%Y%m%d)}"
NAME="MdEdit-$TAG"
STAGE="$RELEASE_DIR/$NAME"

# Always build first so the release reflects current source. Reuses the
# auto-configure path in `make build` if build/ is missing.
echo "==> Building MdEdit"
(cd "$PROJECT_ROOT" && make build >/dev/null)

BIN="$BUILD_DIR/MdEdit.bin"
DSK="$BUILD_DIR/MdEdit.dsk"
if [ ! -f "$BIN" ] || [ ! -f "$DSK" ]; then
    echo "error: build outputs missing — $BIN or $DSK not found" >&2
    exit 1
fi

echo "==> Staging $NAME/"
rm -rf "$STAGE"
mkdir -p "$STAGE"
cp "$BIN" "$STAGE/MdEdit.bin"
cp "$DSK" "$STAGE/MdEdit.dsk"

cat > "$STAGE/README.txt" <<EOF
MdEdit — a small Markdown editor for classic Mac OS (System 6.0.8+)

This archive contains two ways to get MdEdit onto a classic Mac:

  MdEdit.bin
      MacBinary II application. The standard classic-Mac transfer
      format — data fork, resource fork, and Finder info bundled into
      a single file that survives non-HFS filesystems.

      Basilisk II / SheepShaver:
        drop MdEdit.bin into the emulator's shared folder; the
        emulator's File Manager unpacks it automatically.

      Real hardware:
        transfer over a network or floppy, then unpack with StuffIt
        Expander or any MacBinary-aware utility.

  MdEdit.dsk
      Disk Copy 4.2 disk image, 800K, containing MdEdit pre-installed.

      Mini vMac:
        drag-and-drop the .dsk onto the running emulator window, or
        pass it as a command-line argument when launching.

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
