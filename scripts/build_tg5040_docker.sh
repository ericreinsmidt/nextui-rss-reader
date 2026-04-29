#!/bin/sh

set -eu

ROOT_DIR="/Users/htpc/Projects/nextui-rss-reader"

docker run --rm \
  -v "${ROOT_DIR}:/workspace" \
  -w /workspace/ports/tg5040 \
  ghcr.io/loveretro/tg5040-toolchain \
  make
