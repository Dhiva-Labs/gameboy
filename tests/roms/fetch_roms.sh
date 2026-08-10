#!/usr/bin/env bash
# Fetches blargg's Game Boy test ROMs from the canonical mirror
# (github.com/retrio/gb-test-roms). ROMs are gitignored; run once before
# testing. See the README for the test procedure.
set -euo pipefail
cd "$(dirname "$0")"

if [ ! -d gb-test-roms ]; then
  git clone --depth 1 https://github.com/retrio/gb-test-roms.git
else
  echo "gb-test-roms already present"
fi
echo "done: cpu_instrs at gb-test-roms/cpu_instrs/"
