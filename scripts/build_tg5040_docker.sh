#!/bin/bash
# Build NextFeed for tg5040 inside the toolchain Docker container.
set -e

REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${REPO_DIR}/build/tg5040"

mkdir -p "${BUILD_DIR}"

docker run --rm \
    -v "${REPO_DIR}":/workspace \
    ghcr.io/loveretro/tg5040-toolchain \
    make -C /workspace -f ports/tg5040/Makefile \
        BUILD_DIR=/workspace/build/tg5040

echo ""
echo "Build complete: ${BUILD_DIR}/nextfeed"