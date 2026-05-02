#!/bin/sh
set -eu

PAK_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
APP_ID="nextfeed"

STATE_DIR="${USERDATA_PATH}/${APP_ID}"
CACHE_DIR="${STATE_DIR}/cache"
CONFIG_DIR="${STATE_DIR}/config"

BIN="${PAK_DIR}/bin/nextfeed"

mkdir -p "${STATE_DIR}" "${CACHE_DIR}" "${CONFIG_DIR}"

# Seed default feeds if no config exists
DEFAULT_FEEDS_SRC="${PAK_DIR}/assets/feeds/default_feeds.txt"
DEFAULT_FEEDS_DST="${CONFIG_DIR}/feeds.txt"
if [ -f "${DEFAULT_FEEDS_SRC}" ] && [ ! -f "${DEFAULT_FEEDS_DST}" ]; then
  cp "${DEFAULT_FEEDS_SRC}" "${DEFAULT_FEEDS_DST}"
fi

# CA certificate bundle for HTTPS
if [ -f "${PAK_DIR}/lib/cacert.pem" ]; then
  export CURL_CA_BUNDLE="${PAK_DIR}/lib/cacert.pem"
fi

if [ -x "${BIN}" ]; then
  export NEXTFEED_STATE_DIR="${STATE_DIR}"
  export NEXTFEED_CACHE_DIR="${CACHE_DIR}"
  export NEXTFEED_CONFIG_DIR="${CONFIG_DIR}"
  export NEXTFEED_PAK_DIR="${PAK_DIR}"

  cd "${PAK_DIR}"
  exec "${BIN}"
else
  echo "Executable not found: ${BIN}"
  exit 0
fi
