from pathlib import Path
import os
import stat

ROOT = Path("/Users/htpc/Projects/nextui-rss-reader")

dirs = [
    "assets/feeds",
    "assets/themes",
    "docs",
    "include",
    "scripts",
    "src",
    "third_party",
    "ports/tg5040/pak/bin",
    "ports/tg5040/pak/lib",
    "ports/tg5040/pak/assets/feeds",
    "ports/tg5040/pak/assets/themes",
]

files = {
    "pak.json": """{
  "name": "NextFeed",
  "version": "0.1.0",
  "type": "TOOL",
  "description": "Offline-friendly RSS and Atom reader for NextUI devices.",
  "author": "Eric",
  "repo_url": "https://github.com/YOUR_USERNAME/nextui-rss-reader",
  "release_filename": "NextFeed.v0.1.0.zip",
  "platforms": ["tg5040"],
  "changelog": {
    "0.1.0": [
      "Initial project skeleton",
      "Pak launcher and persistent state setup",
      "Ready for MVP development"
    ]
  },
  "screenshots": []
}
""",
    "README.md": """# NextFeed

NextFeed is a NextUI Tool pak for TrimUI devices.

## Goals

- Read RSS and Atom feeds on-device
- Work well on small screens
- Cache content locally for offline reading
- Support NextUI handheld targets cleanly

## Current status

Early project skeleton.
""",
    "LICENSE": """TODO: Choose a license.
""",
    ".gitignore": """.DS_Store
Thumbs.db

build/
dist/
out/
release/.local-test/

*.o
*.a
*.so
*.elf
*.log
*.tmp

ports/tg5040/pak/bin/nextfeed
""",
    "Makefile": """# Top-level Makefile for NextFeed.PHONY: help tg5040 package clean

help:
\t@echo "Available targets:"
\t@echo "  make tg5040   - prepare tg5040 pak assets"
\t@echo "  make package  - create release zip"
\t@echo "  make clean    - remove generated artifacts"

tg5040:
\t$(MAKE) -C ports/tg5040

package:
\tsh scripts/package_pak.sh

clean:
\trm -rf dist release build out
\t$(MAKE) -C ports/tg5040 clean
""",
    "assets/feeds/default_feeds.txt": """https://hnrss.org/frontpage
https://planet.gnome.org/rss20.xml
https://lwn.net/headlines/rss
""",
    "assets/themes/default.theme": """name=default
background=black
foreground=white
accent=cyan
""",
    "docs/architecture.md": """# Architecture Notes

## App type

NextUI Tool pak with `launch.sh` entrypoint.

## Initial target

- `tg5040`
""",
    "docs/mvp-notes.md": """# MVP Notes

## MVP feature set

- fixed feed list
- manual refresh
- feed list screen
- article list screen
- article detail screen
- local cache
- read/unread state
""",
    "scripts/dev_run.sh": """#!/bin/sh
set -eu

echo "Dev helper"
echo "This project is intended to run on-device as a NextUI pak."
echo

find. -maxdepth 4 -type f | sort
""",
    "scripts/package_pak.sh": """#!/bin/sh

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
""",
    "scripts/test_launch_local.sh": """#!/bin/sh

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
""",
    "ports/tg5040/Makefile": """# NextFeed pak Makefile for tg5040.PHONY: all sync-assets clean

all: sync-assets
\t@echo "tg5040 pak prepared."

sync-assets:
\tmkdir -p pak/assets/feeds pak/assets/themes pak/bin
\tcp../../assets/feeds/default_feeds.txt pak/assets/feeds/default_feeds.txt
\tcp../../assets/themes/default.theme pak/assets/themes/default.theme
\tcp../../pak.json pak/pak.json

clean:
\trm -f pak/pak.json
\trm -f pak/assets/feeds/default_feeds.txt
\trm -f pak/assets/themes/default.theme
""",
    "ports/tg5040/export_bundle.sh": """#!/bin/sh

set -eu

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"

echo "Preparing tg5040 pak bundle..."
make -C "${SCRIPT_DIR}"
echo "Done."
echo "Pak folder: ${SCRIPT_DIR}/pak"
""",
    "ports/tg5040/pak/README.txt": """NextFeed for NextUI

This pak is under active development.

Persistent data is stored outside the pak.
If launch succeeds, the app will create state and log files automatically.

Expected runtime locations:
- USERDATA_PATH/nextfeed/
- LOGS_PATH/nextfeed.log
""",
    "ports/tg5040/pak/launch.sh": """#!/bin/sh

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
""",
    "include/.gitkeep": "",
    "src/.gitkeep": "",
    "third_party/.gitkeep": "",
    "ports/tg5040/pak/bin/.gitkeep": "",
    "ports/tg5040/pak/lib/.gitkeep": "",
}

executable_files = {
    "scripts/dev_run.sh",
    "scripts/package_pak.sh",
    "scripts/test_launch_local.sh",
    "ports/tg5040/export_bundle.sh",
    "ports/tg5040/pak/launch.sh",
}

for d in dirs:
    (ROOT / d).mkdir(parents=True, exist_ok=True)

for rel_path, content in files.items():
    path = ROOT / rel_path
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")

# seed pak asset copies
(ROOT / "ports/tg5040/pak/assets/feeds/default_feeds.txt").write_text(
    (ROOT / "assets/feeds/default_feeds.txt").read_text(encoding="utf-8"),
    encoding="utf-8",
)
(ROOT / "ports/tg5040/pak/assets/themes/default.theme").write_text(
    (ROOT / "assets/themes/default.theme").read_text(encoding="utf-8"),
    encoding="utf-8",
)

for rel_path in executable_files:
    path = ROOT / rel_path
    mode = path.stat().st_mode
    path.chmod(mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)

print(f"Bootstrap complete at: {ROOT}")
print("Next steps:")
print(f"  cd {ROOT}")
print("  make tg5040")
print("  sh scripts/test_launch_local.sh")
print("  make package")
