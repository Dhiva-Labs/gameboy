#include "mmu.h"

#include <cstdio>
#include <fstream>
#include <stdexcept>

namespace gb {

Cartridge Cartridge::from_file(const std::string& path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) throw std::runtime_error("cannot open ROM: " + path);
  const auto size = static_cast<std::size_t>(file.tellg());
  if (size < 0x150) throw std::runtime_error("ROM too small to have a header: " + path);

  Cartridge cart;
  cart.rom_.resize(size);
  file.seekg(0);
  file.read(reinterpret_cast<char*>(cart.rom_.data()), static_cast<std::streamsize>(size));
  if (!file) throw std::runtime_error("failed to read ROM: " + path);

  for (int i = 0x134; i < 0x144 && cart.rom_[i] != 0; ++i)
    cart.title_ += static_cast<char>(cart.rom_[i]);

  const std::uint8_t type = cart.rom_[0x147];
  switch (type) {
    case 0x00:
    case 0x08:
    case 0x09:
      cart.mapper_ = 0;
      break;
    case 0x01:
    case 0x02:
    case 0x03:
      cart.mapper_ = 1;
      break;
    case 0x0F:  // MBC3 variants; the RTC registers are not implemented
    case 0x10:
    case 0x11:
    case 0x12:
    case 0x13:
      cart.mapper_ = 3;
      break;
    default: {
      char msg[80];
      std::snprintf(msg, sizeof(msg),
                    "unsupported cartridge type 0x%02X (ROM-only/MBC1/MBC3 only)", type);
      throw std::runtime_error(msg);
    }
  }

  cart.rom_banks_ = 2 << cart.rom_[0x148];
  static constexpr int kRamBanks[] = {0, 0, 1, 4, 16, 8};
  const std::uint8_t ram_code = cart.rom_[0x149];
  cart.ram_banks_ = ram_code < 6 ? kRamBanks[ram_code] : 0;
  cart.ram_.resize(static_cast<std::size_t>(cart.ram_banks_) * 0x2000);
  return cart;
}

std::uint8_t Cartridge::read(std::uint16_t addr) const {
  if (addr < 0x4000) {
    std::size_t bank0 = 0;
    if (mapper_ == 1 && banking_mode_)
      bank0 = (static_cast<std::size_t>(bank_hi_) << 5) % rom_banks_;
    return rom_[(bank0 * 0x4000 + addr) % rom_.size()];
  }
  if (addr < 0x8000) {
    std::size_t bank = 1;
    if (mapper_ == 1)
      bank = ((static_cast<std::size_t>(bank_hi_) << 5) | bank_lo_) % rom_banks_;
    else if (mapper_ == 3)
      bank = bank_lo_ % rom_banks_;
    return rom_[(bank * 0x4000 + (addr - 0x4000)) % rom_.size()];
  }
  // A000-BFFF external RAM
  if (mapper_ == 3 && bank_hi_ > 0x03) return 0xFF;  // RTC register select: absent
  if (!ram_enabled_ || ram_.empty()) return 0xFF;
  std::size_t bank = 0;
  if (mapper_ == 1 && banking_mode_) bank = bank_hi_ % ram_banks_;
  if (mapper_ == 3) bank = bank_hi_ % ram_banks_;
  return ram_[(bank * 0x2000 + (addr - 0xA000)) % ram_.size()];
}

void Cartridge::write(std::uint16_t addr, std::uint8_t value) {
  if (addr >= 0xA000) {  // external RAM
    if (mapper_ == 3 && bank_hi_ > 0x03) return;  // RTC register select: absent
    if (!ram_enabled_ || ram_.empty()) return;
    std::size_t bank = 0;
    if (mapper_ == 1 && banking_mode_) bank = bank_hi_ % ram_banks_;
    if (mapper_ == 3) bank = bank_hi_ % ram_banks_;
    ram_[(bank * 0x2000 + (addr - 0xA000)) % ram_.size()] = value;
    return;
  }
  if (mapper_ == 1) {
    switch (addr >> 13) {
      case 0:  // 0000-1FFF: RAM enable
        ram_enabled_ = (value & 0x0F) == 0x0A;
        break;
      case 1:  // 2000-3FFF: ROM bank low 5 bits; 0 selects 1
        bank_lo_ = value & 0x1F;
        if (bank_lo_ == 0) bank_lo_ = 1;
        break;
      case 2:  // 4000-5FFF: upper 2 bits (ROM bank hi / RAM bank)
        bank_hi_ = value & 0x03;
        break;
      case 3:  // 6000-7FFF: banking mode
        banking_mode_ = value & 0x01;
        break;
    }
  } else if (mapper_ == 3) {
    switch (addr >> 13) {
      case 0:  // RAM/RTC enable
        ram_enabled_ = (value & 0x0F) == 0x0A;
        break;
      case 1:  // 7-bit ROM bank; 0 selects 1
        bank_lo_ = value & 0x7F;
        if (bank_lo_ == 0) bank_lo_ = 1;
        break;
      case 2:  // RAM bank 0-3, or RTC register select 0x08-0x0C (unimplemented)
        bank_hi_ = value & 0x0F;
        break;
      case 3:  // RTC latch: ignored
        break;
    }
  }
}

Mmu::Mmu(Cartridge& cart, Interrupts& interrupts, Timer& timer, Ppu& ppu,
         Joypad& joypad, Apu& apu)
    : cart_(cart),
      interrupts_(interrupts),
      timer_(timer),
      ppu_(ppu),
      joypad_(joypad),
      apu_(apu) {}

void Mmu::tick_mcycle() {
  tcycles_ += 4;
  timer_.tick(4);
  ppu_.tick(4);
  apu_.tick(4);
  if (dma_active_) {
    // One byte per M-cycle. Source reads bypass the CPU bus restrictions.
    ppu_.oam_write(static_cast<std::uint16_t>(dma_index_),
                   bus_read(static_cast<std::uint16_t>(dma_src_ + dma_index_)));
    if (++dma_index_ >= 0xA0) dma_active_ = false;
  }
}

std::uint8_t Mmu::read8(std::uint16_t addr) {
  tick_mcycle();
  return bus_read(addr);
}

void Mmu::write8(std::uint16_t addr, std::uint8_t value) {
  tick_mcycle();
  bus_write(addr, value);
}

std::uint8_t Mmu::bus_read(std::uint16_t addr) {
  if (addr < 0x8000) return cart_.read(addr);
  if (addr < 0xA000) return ppu_.vram_read(addr);
  if (addr < 0xC000) return cart_.read(addr);
  if (addr < 0xE000) return wram_[addr - 0xC000];
  if (addr < 0xFE00) return wram_[addr - 0xE000];  // echo RAM
  if (addr < 0xFEA0) return ppu_.oam_read(addr - 0xFE00);
  if (addr < 0xFF00) return 0xFF;  // unusable region
  if (addr < 0xFF80) return io_read(addr);
  if (addr < 0xFFFF) return hram_[addr - 0xFF80];
  return interrupts_.read_ie();
}

void Mmu::bus_write(std::uint16_t addr, std::uint8_t value) {
  if (addr < 0x8000) {
    cart_.write(addr, value);
  } else if (addr < 0xA000) {
    ppu_.vram_write(addr, value);
  } else if (addr < 0xC000) {
    cart_.write(addr, value);
  } else if (addr < 0xE000) {
    wram_[addr - 0xC000] = value;
  } else if (addr < 0xFE00) {
    wram_[addr - 0xE000] = value;
  } else if (addr < 0xFEA0) {
    ppu_.oam_write(addr - 0xFE00, value);
  } else if (addr < 0xFF00) {
    // unusable: ignore
  } else if (addr < 0xFF80) {
    io_write(addr, value);
  } else if (addr < 0xFFFF) {
    hram_[addr - 0xFF80] = value;
  } else {
    interrupts_.write_ie(value);
  }
}

std::uint8_t Mmu::io_read(std::uint16_t addr) {
  if (addr == 0xFF00) return joypad_.read();
  if (addr == 0xFF01) return sb_;
  if (addr == 0xFF02) return sc_;
  if (addr >= 0xFF04 && addr <= 0xFF07) return timer_.read(addr);
  if (addr == 0xFF0F) return interrupts_.read_if();
  if (addr >= 0xFF10 && addr <= 0xFF3F) return apu_.read(addr);
  if (addr == 0xFF46) return static_cast<std::uint8_t>(dma_src_ >> 8);
  if (addr >= 0xFF40 && addr <= 0xFF4B) return ppu_.reg_read(addr);
  return 0xFF;
}

void Mmu::io_write(std::uint16_t addr, std::uint8_t value) {
  if (addr == 0xFF00) {
    joypad_.write(value);
  } else if (addr == 0xFF01) {
    sb_ = value;
  } else if (addr == 0xFF02) {
    sc_ = value & 0x7F;
    if ((value & 0x80) && (value & 0x01)) {
      // Internal-clock transfer: complete instantly, log the byte.
      serial_log_ += static_cast<char>(sb_);
      sb_ = 0xFF;
      interrupts_.request(Interrupts::kSerial);
    }
  } else if (addr >= 0xFF04 && addr <= 0xFF07) {
    timer_.write(addr, value);
  } else if (addr == 0xFF0F) {
    interrupts_.write_if(value);
  } else if (addr >= 0xFF10 && addr <= 0xFF3F) {
    apu_.write(addr, value);
  } else if (addr == 0xFF46) {
    dma_src_ = static_cast<std::uint16_t>(value) << 8;
    dma_index_ = 0;
    dma_active_ = true;
  } else if (addr >= 0xFF40 && addr <= 0xFF4B) {
    ppu_.reg_write(addr, value);
  }
}

}  // namespace gb
