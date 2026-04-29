#!/bin/sh

set -eu

PAK_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
APP_ID="nextfeed"

STATE_DIR="${USERDATA_PATH}/${APP_ID}"
CACHE_DIR="${STATE_DIR}/cache"
CONFIG_DIR="${STATE_DIR}/config"
LOG_FILE="${LOGS_PATH}/${APP_ID}.log"

BIN="${PAK_DIR}/bin/nextfeed"

mkdir -p "${STATE_DIR}" "${CACHE_DIR}" "${CONFIG_DIR}" "$(dirname "${LOG_FILE}")"

{
  echo "==== NextFeed launch ===="
  date
  echo "PAK_DIR=${PAK_DIR}"
  echo "STATE_DIR=${STATE_DIR}"
  echo "CACHE_DIR=${CACHE_DIR}"
  echo "CONFIG_DIR=${CONFIG_DIR}"
  echo "LOG_FILE=${LOG_FILE}"
  echo "USERDATA_PATH=${USERDATA_PATH:-}"
  echo "LOGS_PATH=${LOGS_PATH:-}"
} >> "${LOG_FILE}" 2>&1

DEFAULT_FEEDS_SRC="${PAK_DIR}/assets/feeds/default_feeds.txt"
DEFAULT_FEEDS_DST="${CONFIG_DIR}/feeds.txt"

if [ -f "${DEFAULT_FEEDS_SRC}" ] && [ ! -f "${DEFAULT_FEEDS_DST}" ]; then
  cp "${DEFAULT_FEEDS_SRC}" "${DEFAULT_FEEDS_DST}"
  echo "Seeded default feeds to ${DEFAULT_FEEDS_DST}" >> "${LOG_FILE}" 2>&1
fi

if [ -x "${BIN}" ]; then
  export NEXTFEED_STATE_DIR="${STATE_DIR}"
  export NEXTFEED_CACHE_DIR="${CACHE_DIR}"
  export NEXTFEED_CONFIG_DIR="${CONFIG_DIR}"
  export NEXTFEED_LOG_FILE="${LOG_FILE}"
  export NEXTFEED_PAK_DIR="${PAK_DIR}"

  echo "Launching binary: ${BIN}" >> "${LOG_FILE}" 2>&1
  exec "${BIN}" >> "${LOG_FILE}" 2>&1
else
  {
    echo "WARNING: Executable not found: ${BIN}"
    echo "Creating placeholder state file instead."
  } >> "${LOG_FILE}" 2>&1

  cat > "${STATE_DIR}/NOT_BUILT.txt" <<EOF2
NextFeed pak launched successfully.
The launcher is working, but the app binary is not built yet.

Expected binary path:
${BIN}
EOF2

  exit 0
fi
