#!/bin/bash
# Bootstrap the local deps/retro68/ toolchain dir, deps/basiliskii/, and deps/minivmac/
# by downloading source.
# Does NOT compile or build anything — only fetches sources and prints
# directions for the steps that have to be done by hand (Homebrew installs,
# the Retro68 toolchain build, ROM/emulator acquisition).
#
# Safe to re-run: skips work that's already done.
#
# Usage: ./scripts/fetch-deps.sh

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RETRO68_HOME="$PROJECT_ROOT/deps/retro68"

echo "==> Bootstrapping $RETRO68_HOME"
mkdir -p "$RETRO68_HOME"

# Retro68 source (and its submodules — multiversal interfaces, etc.)
RETRO68_DIR="$RETRO68_HOME/Retro68"
if [ -d "$RETRO68_DIR/.git" ]; then
    echo "==> Retro68 already cloned at $RETRO68_DIR — skipping"
else
    echo "==> Cloning Retro68 (with submodules)"
    git clone --recursive https://github.com/autc04/Retro68.git "$RETRO68_DIR"
fi

# Patch the Retro68 CMakeLists for Boost 1.90+ compatibility. `boost_system`
# became header-only in Boost 1.90, which broke the find_package() calls in
# four host-tool subdirs. We carry the fix in scripts/patches/.
BOOST_PATCH="$PROJECT_ROOT/scripts/patches/retro68-boost-system.patch"
if [ -f "$BOOST_PATCH" ]; then
    if git -C "$RETRO68_DIR" apply --reverse --check "$BOOST_PATCH" >/dev/null 2>&1; then
        echo "==> Boost system patch already applied — skipping"
    else
        echo "==> Applying Boost 1.90+ system patch to Retro68 source"
        git -C "$RETRO68_DIR" apply "$BOOST_PATCH"
    fi
fi

# Build output dir (empty placeholder — populated by the toolchain build later)
mkdir -p "$RETRO68_HOME/Retro68-build"

# Mac ROMs from the Internet Archive's Mac ROM archive.
# Both emulators need a ROM that's Apple IP and can't ship with the project.
# The Quadra 700/900 ROM (1MB, CRC 420DBFF3) is what Basilisk II expects.
# It's placed at deps/basiliskii/Quadra.rom (the directory we always own).
# The Mac SE FDHD ROM (256KB, CRC B306E171) is what the Mini vMac SE FDHD
# build expects.
BASILISKII_DIR="$PROJECT_ROOT/deps/basiliskii"
MINIVMAC_DIR="$PROJECT_ROOT/deps/minivmac"
mkdir -p "$BASILISKII_DIR/shared" "$MINIVMAC_DIR"

ROM_ARCHIVE_URL="https://archive.org/download/mac_rom_archive_-_as_of_8-19-2011/mac_rom_archive_-_as_of_8-19-2011.zip"

QUADRA_ROM_IN_ARCHIVE="420DBFF3 - Quadra 700&900 & PB140&170.ROM"
QUADRA_ROM_DEST="$BASILISKII_DIR/Quadra.rom"

SEFDHD_ROM_IN_ARCHIVE="B306E171 - Mac SE FDHD.ROM"
SEFDHD_ROM_DEST="$MINIVMAC_DIR/SEFDHD.ROM"

NEED_QUADRA=1
[ -f "$QUADRA_ROM_DEST" ] && NEED_QUADRA=0

NEED_SEFDHD=1
[ -f "$SEFDHD_ROM_DEST" ] && NEED_SEFDHD=0

if [ "$NEED_QUADRA" = "1" ] || [ "$NEED_SEFDHD" = "1" ]; then
    TMPZIP="$(mktemp -t mac_rom_archive).zip"
    echo "==> Downloading Mac ROM archive from archive.org (~52 MB)"
    curl --fail --location --progress-bar --output "$TMPZIP" "$ROM_ARCHIVE_URL"

    if [ "$NEED_QUADRA" = "1" ]; then
        echo "==> Extracting Quadra 700/900 ROM"
        unzip -j -o "$TMPZIP" "$QUADRA_ROM_IN_ARCHIVE" -d "$BASILISKII_DIR" >/dev/null
        mv "$BASILISKII_DIR/$QUADRA_ROM_IN_ARCHIVE" "$QUADRA_ROM_DEST"
        echo "==> Quadra ROM placed at $QUADRA_ROM_DEST"
    fi

    if [ "$NEED_SEFDHD" = "1" ]; then
        echo "==> Extracting Mac SE FDHD ROM"
        unzip -j -o "$TMPZIP" "$SEFDHD_ROM_IN_ARCHIVE" -d "$MINIVMAC_DIR" >/dev/null
        mv "$MINIVMAC_DIR/$SEFDHD_ROM_IN_ARCHIVE" "$SEFDHD_ROM_DEST"
        echo "==> SE FDHD ROM placed at $SEFDHD_ROM_DEST"
    fi

    rm -f "$TMPZIP"
else
    echo "==> Both ROMs already present — skipping ROM download"
fi

# Mini vMac SE FDHD build from erichelgeson/minivmac. Pinned to a known-good
# release; bump the URL when a newer one is desired.
MINIVMAC_URL="https://github.com/erichelgeson/minivmac/releases/download/2024.06.08/minivmac-macOS-SEFDHD.app.zip"
MINIVMAC_APP="$MINIVMAC_DIR/minivmac-macOS-SEFDHD.app"

if [ -d "$MINIVMAC_APP" ]; then
    echo "==> Mini vMac already present at $MINIVMAC_APP — skipping"
else
    TMPZIP="$(mktemp -t minivmac).zip"
    echo "==> Downloading Mini vMac SE FDHD build"
    curl --fail --location --progress-bar --output "$TMPZIP" "$MINIVMAC_URL"
    unzip -o -q "$TMPZIP" -d "$MINIVMAC_DIR"
    rm -f "$TMPZIP"
    # Strip any quarantine attribute the system might have applied — Mini vMac
    # silently fails to read the ROM when its .app bundle is quarantined.
    xattr -cr "$MINIVMAC_APP" 2>/dev/null || true
    xattr -c "$SEFDHD_ROM_DEST" 2>/dev/null || true
    echo "==> Mini vMac placed at $MINIVMAC_APP"
fi

# Homebrew prerequisite check — informational only, we don't install anything
REQUIRED_FORMULAE=(cmake gmp mpfr libmpc boost bison flex texinfo)
MISSING=()
HAS_BREW=0
if command -v brew >/dev/null 2>&1; then
    HAS_BREW=1
    for f in "${REQUIRED_FORMULAE[@]}"; do
        if ! brew list --formula "$f" >/dev/null 2>&1; then
            MISSING+=("$f")
        fi
    done
fi

# Detect presence of the manual artifacts so the report is accurate.
# The Emaculation Basilisk II bundle is recognized by its emulator binary.
RETRO68_BUILT=0
[ -x "$RETRO68_HOME/Retro68-build/toolchain/bin/m68k-apple-macos-gcc" ] && RETRO68_BUILT=1

BASILISK_PRESENT=0
[ -d "$BASILISKII_DIR/BasiliskII.app" ] && BASILISK_PRESENT=1

QUADRA_ROM_PRESENT=0
[ -f "$QUADRA_ROM_DEST" ] && QUADRA_ROM_PRESENT=1

SEFDHD_ROM_PRESENT=0
[ -f "$SEFDHD_ROM_DEST" ] && SEFDHD_ROM_PRESENT=1

MINIVMAC_PRESENT=0
[ -d "$MINIVMAC_APP" ] && MINIVMAC_PRESENT=1

# Detect a boot disk image in deps/minivmac/ (any .dsk or .img the user has added)
BOOTDISK_PRESENT=0
for f in "$MINIVMAC_DIR"/*.dsk "$MINIVMAC_DIR"/*.img; do
    [ -f "$f" ] && BOOTDISK_PRESENT=1 && break
done

mark()  { [ "$1" = "1" ] && echo "[x]" || echo "[ ]"; }

cat <<EOF

================================================================
Download phase complete.

Current state:
  [x] deps/retro68/README.md
  [x] deps/retro68/Retro68/                  (source cloned)
  $(mark $RETRO68_BUILT) deps/retro68/Retro68-build/            (toolchain — built by scripts/build-retro68.sh)
  $(mark $BASILISK_PRESENT) deps/basiliskii/BasiliskII.app            (manual — Emaculation forum)
  $(mark $QUADRA_ROM_PRESENT) deps/basiliskii/Quadra.rom                   (downloaded from archive.org)
  $(mark $SEFDHD_ROM_PRESENT) deps/minivmac/SEFDHD.ROM                     (downloaded from archive.org)
  $(mark $MINIVMAC_PRESENT) deps/minivmac/minivmac-macOS-SEFDHD.app      (downloaded from GitHub release)
  $(mark $BOOTDISK_PRESENT) deps/minivmac/*.dsk (System boot disk)       (manual — Apple legacy downloads)

Remaining steps to finish setup
================================================================
EOF

step=1

if [ "$HAS_BREW" = "0" ]; then
    cat <<EOF

$step. Install Homebrew, then re-run this script:

     /bin/bash -c "\$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

EOF
    step=$((step + 1))
elif [ ${#MISSING[@]} -gt 0 ]; then
    cat <<EOF

$step. Install missing Homebrew prerequisites:

     brew install ${MISSING[*]}

EOF
    step=$((step + 1))
else
    echo
    echo "    Homebrew prerequisites OK — nothing to install."
fi

if [ "$RETRO68_BUILT" = "0" ]; then
    cat <<EOF

$step. Build the Retro68 toolchain (one-time, ~30–60 min on Apple Silicon):

     scripts/build-retro68.sh

   See docs/RETRO68_SETUP.md for build flags, troubleshooting,
   and Universal-vs-Multiversal interface options.
EOF
    step=$((step + 1))
fi

if [ "$BASILISK_PRESENT" = "0" ]; then
    cat <<EOF

$step. Install the Basilisk II bundle (emulator + disk images — the Quadra
   ROM is already in place) from the Emaculation forum:

     https://www.emaculation.com/forum/viewtopic.php?t=7361

   The zip wraps its contents in a top-level "Basilisk II/" folder;
   flatten it into deps/basiliskii/ (no space):

     unzip "/path/to/Basilisk II.zip" -d /tmp/
     mv "/tmp/Basilisk II"/* deps/basiliskii/

   Expected final layout:
     deps/basiliskii/{BasiliskII.app, Quadra.rom, HDD753.dsk, shared/, ...}

   Then drop the prefs file into place and update its absolute paths:

     cp deps/basiliskii/basilisk_ii_prefs.dotfile ~/.basilisk_ii_prefs
     # edit ~/.basilisk_ii_prefs so rom/disk/extfs point at this project's
     # deps/basiliskii/ paths (absolute)
EOF
    step=$((step + 1))
fi

if [ "$BOOTDISK_PRESENT" = "0" ]; then
    cat <<EOF

$step. (Optional, for Mini vMac automated tests) Drop a System 6 or 7 boot
   floppy image into deps/minivmac/. Free downloads at Apple's legacy software
   site, or any classic Mac disk image archive. Save as e.g.
   deps/minivmac/disk1.dsk and point ~/.LaunchAPPL.cfg at it.

   See docs/EMULATOR_SETUP.md for full LaunchAPPL configuration.
EOF
    step=$((step + 1))
fi

cat <<EOF

$step. (Optional) Convenience env vars for this shell session:

     export RETRO68_TOOLCHAIN="\$PWD/deps/retro68/Retro68-build/toolchain"
     export PATH="\$RETRO68_TOOLCHAIN/bin:\$PATH"

Then run scripts/doctor.sh to verify everything's in place.
================================================================
EOF
