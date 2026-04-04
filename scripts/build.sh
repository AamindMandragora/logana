#!/bin/bash
set -e

SANITIZER=${1:-none}
BUILD_DIR="build"

if [ "$SANITIZER" != "none" ]; then
    BUILD_DIR="build-${SANITIZER}"
fi

cmake -B "$BUILD_DIR" -DSANITIZER="$SANITIZER" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" -j$(nproc)

echo ""
echo "Build complete: $BUILD_DIR (sanitizer: $SANITIZER)"