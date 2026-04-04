#!/bin/bash
set -e

SANITIZER=${1:-none}
BUILD_DIR="build"

if [ "$SANITIZER" != "none" ]; then
    BUILD_DIR="build-${SANITIZER}"
fi

./scripts/build.sh "$SANITIZER"

echo ""
echo "Running tests (sanitizer: $SANITIZER)..."
echo ""
ctest --test-dir "$BUILD_DIR" --output-on-failure