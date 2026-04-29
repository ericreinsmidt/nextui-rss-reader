#!/bin/sh

set -eu

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
PAK_DIR="${ROOT_DIR}/ports/tg5040/pak"
DIST_DIR="${ROOT_DIR}/dist"
PAK_JSON="${ROOT_DIR}/pak.json"

VERSION="$(grep -E '"version"' "${PAK_JSON}" | head -n 1 | cut -d '"' -f 4)"
RELEASE_FILENAME="$(grep -E '"release_filename"' "${PAK_JSON}" | head -n 1 | cut -d '"' -f 4)"
OUTPUT_ZIP="${DIST_DIR}/${RELEASE_FILENAME}"
TMP_DIR="${DIST_DIR}/tmp-package"

mkdir -p "${DIST_DIR}"

if [ ! -d "${PAK_DIR}" ]; then
  echo "Pak directory not found: ${PAK_DIR}"
  exit 1
fi

if [ -z "${VERSION}" ] || [ -z "${RELEASE_FILENAME}" ]; then
  echo "Failed to read version or release_filename from pak.json"
  exit 1
fi

rm -rf "${TMP_DIR}"
mkdir -p "${TMP_DIR}"

echo "Packaging pak..."
cp -R "${PAK_DIR}/." "${TMP_DIR}/"

if [ ! -f "${TMP_DIR}/launch.sh" ]; then
  echo "Missing launch.sh in temp package directory"
  exit 1
fi

rm -f "${OUTPUT_ZIP}"

cd "${TMP_DIR}"
set -- *
if [ "$1" = "*" ]; then
  echo "No files found in temp package directory"
  exit 1
fi

zip -r "${OUTPUT_ZIP}" "$@"

cd "${ROOT_DIR}"
rm -rf "${TMP_DIR}"

echo "Created: ${OUTPUT_ZIP}"
