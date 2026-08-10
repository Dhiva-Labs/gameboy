#pragma once

#include <utility>

#include "apu.h"
#include "cpu/cpu.h"
#include "interrupts.h"
#include "joypad.h"
#include "mmu.h"
#include "ppu.h"
#include "timer.h"

namespace gb {

// Wires one machine together. Construction order matters: components before
// the Mmu that routes to them, Cpu last.
struct GameBoy {
  Cartridge cart;
  Interrupts interrupts;
  Timer timer{interrupts};
  Ppu ppu{interrupts};
  Joypad joypad{interrupts};
  Apu apu;
  Mmu mmu{cart, interrupts, timer, ppu, joypad, apu};
  Cpu cpu{mmu, interrupts};

  explicit GameBoy(Cartridge cartridge) : cart(std::move(cartridge)) {}
};

}  // namespace gb
