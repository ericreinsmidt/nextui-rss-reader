#!/bin/sh

set -eu

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"

echo "Preparing tg5040 pak bundle..."
make -C "${SCRIPT_DIR}"
echo "Done."
echo "Pak folder: ${SCRIPT_DIR}/pak"
