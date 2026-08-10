#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "apu.h"
#include "interrupts.h"
#include "joypad.h"
#include "ppu.h"
#include "timer.h"

namespace gb {

// Cartridge ROM/RAM with bank switching. Supported mappers: none (32KB flat)
// and MBC1. MBC3/MBC5 are out of scope for v1.
class Cartridge {
 public:
  // Throws std::runtime_error on I/O failure or unsupported mapper.
  static Cartridge from_file(const std::string& path);

  std::uint8_t read(std::uint16_t addr) const;
  void write(std::uint16_t addr, std::uint8_t value);

  const std::string& title() const { return title_; }
  int mapper() const { return mapper_; }

  // Test-harness peek into external RAM bank 0, ignoring the enable latch —
  // blargg ROMs report results at 0xA000 and may disable RAM afterwards.
  std::uint8_t debug_ram(std::uint16_t offset) const {
    return offset < ram_.size() ? ram_[offset] : 0xFF;
  }

 private:
  std::vector<std::uint8_t> rom_;
  std::vector<std::uint8_t> ram_;
  std::string title_;
  int mapper_ = 0;  // 0 = none, 1 = MBC1
  int rom_banks_ = 2;
  int ram_banks_ = 0;

  bool ram_enabled_ = false;
  std::uint8_t bank_lo_ = 1;   // MBC1 5-bit ROM bank (0 maps to 1)
  std::uint8_t bank_hi_ = 0;   // MBC1 2-bit upper ROM bank / RAM bank
  bool banking_mode_ = false;  // false = simple, true = advanced (RAM banking)
};

// The bus. Owns work RAM/HRAM and routes everything else to the right
// component. All CPU accesses go through read8/write8, which advance the
// machine clock by one M-cycle (4 T-cycles) BEFORE the access — cycle counts
// come from access patterns, not per-opcode tables.
class Mmu {
 public:
  Mmu(Cartridge& cart, Interrupts& interrupts, Timer& timer, Ppu& ppu,
      Joypad& joypad, Apu& apu);

  // One M-cycle: advances timer/PPU/DMA. Also used for internal CPU cycles.
  void tick_mcycle();

  std::uint8_t read8(std::uint16_t addr);
  void write8(std::uint16_t addr, std::uint8_t value);

  std::uint64_t cycles() const { return tcycles_; }
  const std::string& serial_log() const { return serial_log_; }

 private:
  std::uint8_t bus_read(std::uint16_t addr);
  void bus_write(std::uint16_t addr, std::uint8_t value);
  std::uint8_t io_read(std::uint16_t addr);
  void io_write(std::uint16_t addr, std::uint8_t value);

  Cartridge& cart_;
  Interrupts& interrupts_;
  Timer& timer_;
  Ppu& ppu_;
  Joypad& joypad_;
  Apu& apu_;

  std::vector<std::uint8_t> wram_ = std::vector<std::uint8_t>(0x2000);
  std::vector<std::uint8_t> hram_ = std::vector<std::uint8_t>(0x7F);

  // Serial port: SC bit 7 + internal clock "transfers" SB instantly and logs
  // it — this is how blargg test ROMs report results.
  std::uint8_t sb_ = 0x00;
  std::uint8_t sc_ = 0x7E;
  std::string serial_log_;

  // OAM DMA (FF46): 160 bytes copied over 160 M-cycles.
  bool dma_active_ = false;
  std::uint16_t dma_src_ = 0;
  int dma_index_ = 0;

  std::uint64_t tcycles_ = 0;
};

}  // namespace gb
