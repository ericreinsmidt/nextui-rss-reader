#!/usr/bin/env python3
"""Sync default_feeds.txt from assets/ into ports/tg5040/pak/assets/."""

import shutil
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SRC  = REPO / "assets" / "feeds" / "default_feeds.txt"
DST  = REPO / "ports" / "tg5040" / "pak" / "assets" / "feeds" / "default_feeds.txt"

def main():
    if not SRC.exists():
        print(f"ERROR: source not found: {SRC}")
        return 1
    DST.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(SRC, DST)
    print(f"Synced: {SRC}")
    print(f"    ->  {DST}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
