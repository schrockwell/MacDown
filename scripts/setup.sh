#!/bin/bash
# One-time project setup: fetch dependencies, build the Retro68 toolchain,
# and verify the install. Equivalent to running the three sub-scripts in
# sequence; safe to re-run because each sub-script is itself idempotent.
#
# Usage: ./scripts/setup.sh

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

section() {
    echo
    echo "================================================================"
    echo "  $1"
    echo "================================================================"
}

section "1/4  Installing Homebrew prerequisites"
if ! command -v brew >/dev/null 2>&1; then
    cat >&2 <<EOF
Homebrew is not installed.

Install it with:
  /bin/bash -c "\$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

Then re-run scripts/setup.sh.
EOF
    exit 1
fi
# brew bundle is a no-op for formulae that are already installed, so
# re-running setup.sh is cheap. Edit ./Brewfile to add deps.
brew bundle install --file="$PROJECT_ROOT/Brewfile"

section "2/4  Fetching dependencies"
"$PROJECT_ROOT/scripts/fetch-deps.sh"

section "3/4  Building Retro68 toolchain"
"$PROJECT_ROOT/scripts/build-retro68.sh"

section "4/4  Running doctor"
"$PROJECT_ROOT/scripts/doctor.sh"
