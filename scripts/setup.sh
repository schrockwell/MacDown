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

section "1/3  Fetching dependencies"
"$PROJECT_ROOT/scripts/fetch-deps.sh"

section "2/3  Building Retro68 toolchain"
"$PROJECT_ROOT/scripts/build-retro68.sh"

section "3/3  Running doctor"
"$PROJECT_ROOT/scripts/doctor.sh"
