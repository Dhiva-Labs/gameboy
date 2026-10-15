# gameboy

[![ARM64 builds](https://github.com/Dhiva-Labs/gameboy/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/Dhiva-Labs/gameboy/actions/workflows/ci.yml)
![Linux x64 | arm64](https://img.shields.io/badge/Linux-x64%20%7C%20arm64-informational?logo=linux&logoColor=white)
![Windows x64 | arm64](https://img.shields.io/badge/Windows-x64%20%7C%20arm64-informational?logo=windows&logoColor=white)

A Game Boy (DMG) emulator in C++20 with an SDL2 frontend. Phase 2 of a
two-phase emulator project (Phase 1 is the CHIP-8 interpreter at
`../chip8`, whose loop/timing architecture this builds on). Correctness is
proven by test ROMs, not visual inspection — see Test results.

## Build

Requires CMake ≥ 3.16, a C++20 compiler, and SDL2 development headers.

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Targets:

- `build/gameboy` — SDL2 frontend
- `build/gb_run_rom` — headless runner used by the test procedure

### Windows

Use MSVC (Visual Studio 2022) and install SDL2 with [vcpkg](https://vcpkg.io).
The triplet picks the architecture (`x64-windows` or `arm64-windows`):

```
vcpkg install sdl2:x64-windows
cmake -S . -B build -A x64 ^
  -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake ^
  -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build --config Release
```

For ARM64 use `sdl2:arm64-windows`, `-A ARM64` and `-DVCPKG_TARGET_TRIPLET=arm64-windows`.
The executables and `SDL2.dll` end up in `build\Release\`.

### Prebuilt binaries

Every release carries four archives, built and tested by CI on native runners:

| Platform | Archive |
|---|---|
| Linux x86-64 | `gameboy-linux-x64.tar.gz` |
| Linux ARM64 (aarch64) | `gameboy-linux-arm64.tar.gz` |
| Windows x64 | `gameboy-windows-x64.zip` |
| Windows ARM64 | `gameboy-windows-arm64.zip` |

The Linux archives are built on Ubuntu 24.04 and need the SDL2 runtime
(`sudo apt install libsdl2-2.0-0`). The Windows archives include `SDL2.dll`
and need the Microsoft Visual C++ Redistributable (2015-2022, same
architecture as the archive).

## Run

```
./build/gameboy [--scale N] [--turbo] [--mute] [--frames N] rom.gb
```

`--frames N` exits after N frames (used by the CI smoke test).

Controls: arrows = d-pad, `X` = A, `Z` = B, `Enter` = Start,
`Right Shift` = Select, `Tab` (hold) = turbo, `Esc` = quit.

No ROMs are bundled. Test ROMs are fetched by script (below); commercial
games (Tetris and Dr. Mario are the intended first targets — both ROM-only
or MBC1) must be your own dumps.

## Architecture

- **Cycle accounting through the bus.** Every CPU memory access and every
  internal cycle advances the machine exactly one M-cycle
  (`Mmu::tick_mcycle` → timer, PPU, OAM DMA). There is no per-opcode cycle
  table; instruction timing *emerges* from the access pattern, with
  explicit internal cycles where hardware has them (taken branches, 16-bit
  ops, push setup). This is what makes `instr_timing` and `mem_timing`
  pass.
- **CPU**: SM83 with the x/y/z bitfield decode (`src/cpu/opcodes.cpp`,
  `opcodes_cb.cpp`). EI delay, HALT wake rules, the halt bug, and the
  eleven removed opcodes (lock-up, surfaced as a stopped CPU) are
  implemented.
- **Interrupts**: IE/IF/IME with the priority order VBlank > STAT > Timer >
  Serial > Joypad; 5 M-cycle dispatch; IE-overwrite-during-push falls
  through to vector 0x0000 as on hardware.
- **Timer**: DIV is the top byte of the internal 16-bit counter; TIMA
  increments on the falling edge of the TAC-selected counter bit, with the
  4-cycle delayed TMA reload (and cancellation on TIMA write).
- **PPU**: scanline-accurate. The mode state machine (2→3→0 per line, 1 in
  VBlank) runs per-dot; each visible line is rendered in one shot at the
  mode 3→0 transition. STAT interrupts use the rising-edge "STAT line"
  blocking model. Sprites follow DMG priority (lower X wins, then OAM
  order), 10-per-line limit, 8x16 mode, both flips, both palettes, and the
  BG-priority flag.
- **MBC**: ROM-only and MBC1 (including the mode-1 bank-0 remap quirk),
  plus MBC3 *banking only* — the RTC registers are not implemented (reads
  0xFF). Added past the original v1 scope line because much homebrew ships
  with MBC3 headers for battery saves; the RTC itself stays out of scope.
- **APU**: full DMG synthesis — both pulse channels (CH1 with frequency
  sweep), the 4-bit wave channel, LFSR noise, envelopes, length counters,
  the 512Hz frame sequencer, NR50/NR51 mixing, and the hardware-style
  high-pass "capacitor" per side (without it, enabled-but-quiet DACs leave
  loud DC plateaus in the output). 48kHz stereo int16; the frontend plays
  it via SDL_QueueAudio with a ~170ms watermark, and `gb_run_rom --wav`
  captures it headlessly. Simplifications: the frame sequencer runs from
  its own 8192-cycle counter rather than DIV bit 4, and obscure
  length/sweep power-cycle edge cases are unmodeled — games sound right;
  blargg's dmg_sound suite is not a claimed target.

## Test results

Fetch the suites and dmg-acid2 (gitignored; the script pins them to the
revisions the expected output was recorded against):

```
./tests/roms/fetch_roms.sh
```

`tests/run_tests.sh [BUILD_DIR]` runs everything below headlessly: the blargg
suites by exit code, `halt_bug` and `dmg-acid2` by hashing the final
framebuffer against `tests/expected/*.sha256`, plus a smoke test of the SDL
frontend under SDL's dummy video/audio drivers. CI runs it on Linux and
Windows, x64 and ARM64. `--update` rewrites the hashes (check the screenshot
in `tests/out/` before committing).

`gb_run_rom` captures blargg's serial output and the 0xA000 memory-result
protocol, exits 0/1/2 for pass/fail/timeout, scripts joypad input
(`--press BUTTON:FRAME`), and dumps BMP screenshots:

```
for rom in tests/roms/gb-test-roms/cpu_instrs/individual/*.gb; do ./build/gb_run_rom "$rom"; done
./build/gb_run_rom tests/roms/gb-test-roms/cpu_instrs/cpu_instrs.gb
./build/gb_run_rom tests/roms/gb-test-roms/instr_timing/instr_timing.gb
./build/gb_run_rom tests/roms/gb-test-roms/mem_timing/mem_timing.gb
./build/gb_run_rom tests/roms/gb-test-roms/mem_timing-2/mem_timing.gb
```

| Suite | Result |
|---|---|
| blargg `cpu_instrs` — all 11 individual sub-tests | ✅ Passed |
| blargg `cpu_instrs` — full ROM (exercises MBC1) | ✅ Passed |
| blargg `instr_timing` | ✅ Passed |
| blargg `mem_timing` + `mem_timing-2` | ✅ Passed |
| blargg `halt_bug` | ✅ Passed (screen-verified) |
| `dmg-acid2` (PPU acceptance) | ✅ Pixel-perfect vs. reference |
| Libbet and the Magic Floor (homebrew, pinobatch) | ✅ Boots, menus, playable via scripted input |
| Adjustris (homebrew falling-block game, tbsp; MBC3 header) | ✅ Title, gameplay, gravity + steering verified |
| APU unit check (440Hz pulse via register pokes) | ✅ 439Hz measured, full-amplitude square |

The dmg-acid2 comparison quantizes both images to shade ranks and diffs
pixel-by-pixel: 0 / 23040 mismatches.

## Out of scope for v1

- Mid-scanline raster effects / pixel-FIFO accuracy (scanline renderer by
  design; a handful of games' split-screen tricks will shear)
- VRAM/OAM mode-blocking and OAM DMA bus restrictions
- MBC3 RTC / MBC5+, cartridge RAM battery saves (MBC3 banking works)
- APU deep edge cases (DIV-driven frame sequencer, blargg dmg_sound)
- Game Boy Color (CGB) mode, link cable
- mooneye-gb deep PPU/timer edge cases — next accuracy push if wanted

## Layout

```
src/
  cpu/cpu.{h,cpp}      registers, flags, step, interrupt dispatch, halt bug
  cpu/opcodes.cpp      base table (x/y/z decode)
  cpu/opcodes_cb.cpp   CB-prefixed table
  mmu.{h,cpp}          bus + cartridge (ROM-only, MBC1), serial log, OAM DMA
  ppu.{h,cpp}          mode state machine, STAT line, scanline renderer
  timer.{h,cpp}        DIV/TIMA falling-edge model
  interrupts.{h,cpp}   IE/IF
  joypad.{h,cpp}       JOYP matrix
  apu.{h,cpp}          4-channel synthesis, frame sequencer, high-pass mix
main.cpp               SDL2 window, ~59.73fps pacing, input
tests/run_rom.cpp      headless runner (serial + memory protocol + screenshots)
tests/roms/            fetch_roms.sh (suites gitignored)
```
