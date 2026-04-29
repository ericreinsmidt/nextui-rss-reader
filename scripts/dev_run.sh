#!/bin/sh
set -eu

echo "Dev helper"
echo "This project is intended to run on-device as a NextUI pak."
echo

find. -maxdepth 4 -type f | sort
