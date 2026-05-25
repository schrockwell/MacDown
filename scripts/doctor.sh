#!/bin/bash
# Diagnose missing or misconfigured pieces of the local dev environment.
# Read-only — never modifies anything. Run after fetch-deps.sh and the
# Retro68 toolchain build to confirm everything's in place.
#
# Exits 0 if no issues found, non-zero if any [FAIL] check fires.
#
# Usage: ./scripts/doctor.sh

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEPS="$PROJECT_ROOT/deps"

if [ -t 1 ]; then
    G=$'\033[32m'; Y=$'\033[33m'; R=$'\033[31m'; B=$'\033[1m'; N=$'\033[0m'
else
    G=''; Y=''; R=''; B=''; N=''
fi

issues=0
warnings=0

check_pass() { echo "  ${G}[OK]${N}    $1"; }
check_warn() { echo "  ${Y}[WARN]${N}  $1${2:+ — $2}"; warnings=$((warnings+1)); }
check_fail() { echo "  ${R}[FAIL]${N}  $1${2:+ — $2}"; issues=$((issues+1)); }
section()    { echo; echo "${B}== $1 ==${N}"; }

# ---- Retro68 toolchain ----
section "Retro68 toolchain"

if [ -d "$DEPS/retro68/Retro68/.git" ]; then
    check_pass "Retro68 source cloned (deps/retro68/Retro68/)"
else
    check_fail "Retro68 source missing" "run scripts/fetch-deps.sh"
fi

GCC="$DEPS/retro68/Retro68-build/toolchain/bin/m68k-apple-macos-gcc"
if [ -x "$GCC" ]; then
    VER=$("$GCC" --version 2>/dev/null | head -1)
    check_pass "Toolchain built ($VER)"
else
    check_fail "m68k-apple-macos-gcc not found" "run scripts/build-retro68.sh"
fi

CMAKE_TC="$DEPS/retro68/Retro68-build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake"
if [ -f "$CMAKE_TC" ]; then
    check_pass "CMake toolchain file present"
else
    check_fail "CMake toolchain file missing" "$CMAKE_TC"
fi

# ---- Homebrew prerequisites ----
section "Homebrew prerequisites"

if ! command -v brew >/dev/null 2>&1; then
    check_fail "Homebrew not installed" "https://brew.sh"
else
    BREW_PREFIX="$(brew --prefix)"
    check_pass "Homebrew installed at $BREW_PREFIX"
    if [ -d /usr/local/Cellar ] && [ "$BREW_PREFIX" = "/opt/homebrew" ]; then
        check_warn "Intel Homebrew also present at /usr/local/Cellar" \
            "can cause arch mixups in the Retro68 build"
    fi
    for f in cmake gmp mpfr libmpc boost bison flex texinfo; do
        if brew list --formula "$f" >/dev/null 2>&1; then
            check_pass "$f installed"
        else
            check_fail "$f not installed" "brew install $f"
        fi
    done
    if brew list --formula hfsutils >/dev/null 2>&1; then
        check_warn "hfsutils installed via Homebrew" \
            "the ARM64 formula is broken; Retro68 ships its own — brew uninstall hfsutils"
    fi
fi

# ---- Basilisk II ----
section "Basilisk II  (deps/basiliskii/)"

if [ -d "$DEPS/basiliskii/BasiliskII.app" ]; then
    check_pass "BasiliskII.app present"
else
    check_warn "BasiliskII.app missing" \
        "grab the Emaculation bundle — see deps/basiliskii/README.md"
fi

if [ -f "$DEPS/basiliskii/Quadra.rom" ]; then
    check_pass "Quadra.rom present"
else
    check_fail "Quadra.rom missing" "run scripts/fetch-deps.sh"
fi

shopt -s nullglob
disks=("$DEPS/basiliskii"/*.dsk "$DEPS/basiliskii"/*.image)
shopt -u nullglob
if [ ${#disks[@]} -gt 0 ]; then
    check_pass "${#disks[@]} disk image(s) present"
else
    check_warn "no .dsk/.image found — emulator can't boot without one"
fi

if [ -d "$DEPS/basiliskii/shared" ]; then
    check_pass "shared/ folder present (host↔guest exchange)"
else
    check_warn "shared/ folder missing" \
        "scripts/run-basiliskii.sh has nowhere to copy build artifacts"
fi

if [ -f "$HOME/.basilisk_ii_prefs" ]; then
    check_pass "~/.basilisk_ii_prefs configured"
else
    check_warn "~/.basilisk_ii_prefs not found" \
        "Basilisk II will use defaults — see docs/EMULATOR_SETUP.md"
fi

# ---- Mini vMac ----
section "Mini vMac  (deps/minivmac/)"

MVM_APP="$DEPS/minivmac/minivmac-macOS-SEFDHD.app"
if [ -d "$MVM_APP" ]; then
    check_pass "minivmac-macOS-SEFDHD.app present"
    if xattr "$MVM_APP" 2>/dev/null | grep -q com.apple.quarantine; then
        check_warn ".app is quarantined" \
            "xattr -cr deps/minivmac/minivmac-macOS-SEFDHD.app"
    fi
else
    check_fail "minivmac-macOS-SEFDHD.app missing" "run scripts/fetch-deps.sh"
fi

MVM_ROM="$DEPS/minivmac/SEFDHD.ROM"
if [ -f "$MVM_ROM" ]; then
    check_pass "SEFDHD.ROM present"
    if xattr "$MVM_ROM" 2>/dev/null | grep -q com.apple.quarantine; then
        check_warn "SEFDHD.ROM is quarantined" "xattr -c deps/minivmac/SEFDHD.ROM"
    fi
else
    check_fail "SEFDHD.ROM missing" "run scripts/fetch-deps.sh"
fi

shopt -s nullglob
boot_disks=("$DEPS/minivmac"/*.dsk "$DEPS/minivmac"/*.img)
shopt -u nullglob
if [ ${#boot_disks[@]} -gt 0 ]; then
    check_pass "${#boot_disks[@]} boot disk(s) present"
else
    check_warn "no System 6/7 boot disk in deps/minivmac/" \
        "LaunchAPPL needs one — see docs/EMULATOR_SETUP.md"
fi

if [ -f "$HOME/.LaunchAPPL.cfg" ]; then
    check_pass "~/.LaunchAPPL.cfg configured"
else
    check_warn "~/.LaunchAPPL.cfg not found" \
        "LaunchAPPL won't know which emulator to launch"
fi

# ---- Summary ----
section "Summary"
echo
if [ "$issues" -eq 0 ] && [ "$warnings" -eq 0 ]; then
    echo "  ${G}All checks passed.${N}"
elif [ "$issues" -eq 0 ]; then
    echo "  ${Y}${warnings} warning(s)${N} — non-blocking but worth a look."
else
    echo "  ${R}${issues} issue(s)${N}, ${Y}${warnings} warning(s)${N}"
    echo "  Most issues are addressed by ${B}scripts/fetch-deps.sh${N}."
fi
echo

exit "$issues"
