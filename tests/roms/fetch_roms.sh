#!/usr/bin/env bash
# Fetches the test ROMs used by the README's test procedure: blargg's suites
# from the canonical mirror (github.com/retrio/gb-test-roms) and dmg-acid2
# (github.com/mattcurrie/dmg-acid2). ROMs are gitignored; run once before
# testing.
#
# Both are pinned so tests/expected stays valid. Override GB_TEST_ROMS_REF
# (any commit or ref of the mirror) to try another revision.
set -euo pipefail
cd "$(dirname "$0")"

gb_test_roms_ref="${GB_TEST_ROMS_REF:-c240dd7d700e5c0b00a7bbba52b53e4ee67b5f15}"
if [ ! -d gb-test-roms ]; then
  git init -q gb-test-roms
  git -C gb-test-roms fetch -q --depth 1 https://github.com/retrio/gb-test-roms.git "$gb_test_roms_ref"
  git -C gb-test-roms checkout -q FETCH_HEAD
else
  echo "gb-test-roms already present"
fi

acid2_url="https://github.com/mattcurrie/dmg-acid2/releases/download/v1.0/dmg-acid2.gb"
acid2_sha256="464e14b7d42e7feea0b7ede42be7071dc88913f75b9ffa444299424b63d1dff1"
if [ ! -f dmg-acid2.gb ]; then
  curl -fsSL -o dmg-acid2.gb "$acid2_url"
else
  echo "dmg-acid2.gb already present"
fi
echo "$acid2_sha256  dmg-acid2.gb" | sha256sum -c -

echo "done: cpu_instrs at gb-test-roms/cpu_instrs/, dmg-acid2 at dmg-acid2.gb"
