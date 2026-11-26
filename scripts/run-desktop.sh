#!/bin/bash
# Runs the desktop build against this repository's store/ and its drafts, with a folder standing in for the SD card.
#   scripts/run-desktop.sh [extra portzone arguments]
# PORTZONE_SD (default ./sdmc) changes the SD card folder.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
sd="${PORTZONE_SD:-$root/sdmc}"
cd "$root/build" # borealis loads ./resources
exec ./portzone --index "$root/store/ports.json" --drafts "$root/store/drafts" --sd "$sd" "$@"
