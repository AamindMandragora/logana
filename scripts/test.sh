#!/bin/bash
set -e

SANITIZER=${1:-none}
BUILD_DIR="build"

if [ "$SANITIZER" != "none" ]; then
    BUILD_DIR="build-${SANITIZER}"
fi

cmake -B "$BUILD_DIR" -DSANITIZER="$SANITIZER" -DCMAKE_BUILD_TYPE=Debug
cmake --build "$BUILD_DIR" -j$(nproc)

echo ""
echo "Running tests (sanitizer: $SANITIZER)..."
echo ""
ctest --test-dir "$BUILD_DIR" --output-on-failure