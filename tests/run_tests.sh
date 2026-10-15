#!/usr/bin/env bash
# Headless regression suite for the Game Boy core.
#
#   * blargg suites: gb_run_rom exits 0 when the ROM reports "Passed".
#   * halt_bug and dmg-acid2 report on screen only: the final framebuffer
#     (gb_run_rom --screenshot) must hash to tests/expected/<name>.bmp.sha256.
#     The dmg-acid2 hash was recorded from a screenshot verified pixel-perfect
#     against upstream's reference-dmg.png (0 of 23040 pixels differ).
#   * the SDL frontend is smoke-tested under SDL's dummy video/audio drivers.
#
#   tests/run_tests.sh [BUILD_DIR]            check (default BUILD_DIR: build)
#   tests/run_tests.sh --update [BUILD_DIR]   rewrite tests/expected/*.sha256
#
# Fetch the ROMs first: tests/roms/fetch_roms.sh
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
update=0
if [ "${1:-}" = "--update" ]; then update=1; shift; fi
build="$(cd "${1:-$root/build}" && pwd)"
# Everything below runs from the repo root with relative paths, so the
# (possibly native Windows) executables never see MSYS-style absolute paths.
cd "$root"

# Locate a built executable (single-config, or multi-config like MSVC's Release/).
find_bin() {
  local name="$1" dir ext
  for dir in "$build" "$build/Release" "$build/RelWithDebInfo"; do
    for ext in "" ".exe"; do
      if [ -f "$dir/$name$ext" ]; then echo "$dir/$name$ext"; return 0; fi
    done
  done
  echo "error: $name not found under $build (build the project first)" >&2
  return 1
}

run_rom="$(find_bin gb_run_rom)"
frontend="$(find_bin gameboy)"
blargg=tests/roms/gb-test-roms
acid2=tests/roms/dmg-acid2.gb
expected=tests/expected

if [ ! -f "$blargg/cpu_instrs/cpu_instrs.gb" ] || [ ! -f "$acid2" ]; then
  echo "error: test ROMs missing; run tests/roms/fetch_roms.sh first" >&2
  exit 2
fi
mkdir -p tests/out

failures=0
total=0

# verdict NAME ROM: blargg serial/memory protocol, exit code 0 = Passed.
verdict() {
  local name="$1" rom="$2" out rc=0
  total=$((total + 1))
  out="$("$run_rom" "$rom" 2>&1 | tr -d '\r')" || rc=$?
  if [ "$rc" = 0 ] && grep -q 'Passed' <<<"$out"; then
    echo "PASS  $name"
  else
    echo "FAIL  $name (exit $rc)"; printf '%s\n' "$out" | tail -15
    failures=$((failures + 1))
  fi
}

# screen NAME ROM FRAMES: hash of the framebuffer after FRAMES frames.
screen() {
  local name="$1" rom="$2" frames="$3" shot want got
  total=$((total + 1))
  shot="tests/out/$name.bmp"
  rm -f "$shot"
  if ! "$run_rom" "$rom" --frames "$frames" --screenshot "$shot" >/dev/null 2>&1; then
    echo "FAIL  $name (runner exited non-zero)"; failures=$((failures + 1)); return
  fi
  got="$(sha256sum "$shot" | cut -d' ' -f1)"
  if [ "$update" = 1 ]; then
    echo "$got" >"$expected/$name.bmp.sha256"
    echo "WROTE $name ($got)"
    return
  fi
  want="$(tr -d ' \r\n' <"$expected/$name.bmp.sha256")"
  if [ "$got" = "$want" ]; then
    echo "PASS  $name"
  else
    echo "FAIL  $name (framebuffer hash $got, expected $want; see $shot)"
    failures=$((failures + 1))
  fi
}

if [ "$update" = 0 ]; then
  count=0
  for rom in "$blargg"/cpu_instrs/individual/*.gb; do
    verdict "cpu_instrs/$(basename "$rom" .gb)" "$rom"
    count=$((count + 1))
  done
  if [ "$count" != 11 ]; then
    echo "FAIL  expected 11 cpu_instrs sub-tests, found $count"; failures=$((failures + 1))
  fi
  verdict cpu_instrs "$blargg/cpu_instrs/cpu_instrs.gb"
  verdict instr_timing "$blargg/instr_timing/instr_timing.gb"
  verdict mem_timing "$blargg/mem_timing/mem_timing.gb"
  verdict mem_timing-2 "$blargg/mem_timing-2/mem_timing.gb"
fi

screen halt_bug "$blargg/halt_bug.gb" 300
screen dmg-acid2 "$acid2" 120

# SDL frontend smoke test: real SDL init, window, renderer, audio device and
# frame loop, headless via the dummy drivers (needs the SDL2 runtime to load).
if [ "$update" = 0 ]; then
  total=$((total + 1))
  rc=0
  err="$(SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy "$frontend" --scale 1 --frames 120 "$acid2" 2>&1)" || rc=$?
  if [ "$rc" = 0 ] && ! grep -qiE 'CPU stopped|error|warning' <<<"$err"; then
    echo "PASS  frontend (dummy SDL video+audio drivers, 120 frames)"
  else
    echo "FAIL  frontend smoke test (exit $rc)"; printf '%s\n' "$err"; failures=$((failures + 1))
  fi
fi

echo "$((total - failures))/$total passed"
[ "$failures" = 0 ]
