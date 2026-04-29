#!/bin/sh

set -eu

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
TEST_ROOT="${ROOT_DIR}/.local-test"

mkdir -p "${TEST_ROOT}/userdata" "${TEST_ROOT}/logs"

export USERDATA_PATH="${TEST_ROOT}/userdata"
export LOGS_PATH="${TEST_ROOT}/logs"

sh "${ROOT_DIR}/ports/tg5040/pak/launch.sh"

echo
echo "Local launch test complete."
echo "Userdata: ${USERDATA_PATH}"
echo "Logs:     ${LOGS_PATH}"
