#!/bin/bash
# Build the project and copy the result to the Basilisk II shared folder.
# Usage: ./scripts/build-and-deploy.sh [app-name]

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP_NAME="${1:-MyApp}"
BUILD_DIR="$PROJECT_ROOT/build"
SHARED_DIR="$PROJECT_ROOT/deps/basiliskii/shared"

cmake --build "$BUILD_DIR"

if [ -f "$BUILD_DIR/$APP_NAME.bin" ]; then
    cp "$BUILD_DIR/$APP_NAME.bin" "$SHARED_DIR/"
    echo "Deployed $APP_NAME.bin to Basilisk II shared folder"
else
    echo "Warning: $BUILD_DIR/$APP_NAME.bin not found"
    echo "Available outputs:"
    ls "$BUILD_DIR"/*.bin "$BUILD_DIR"/*.dsk 2>/dev/null || echo "  (none)"
fi
