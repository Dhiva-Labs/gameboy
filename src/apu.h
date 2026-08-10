#pragma once

#include <array>
#include <cstdint>

namespace gb {

// Register-file stub covering 0xFF10-0xFF3F. Sound generation is explicitly
// last in the build order and optional for v1; games can write/read their
// registers without wedging, and everything stays silent.
class Apu {
 public:
  std::uint8_t read(std::uint16_t addr) const { return regs_[addr - 0xFF10]; }
  void write(std::uint16_t addr, std::uint8_t value) { regs_[addr - 0xFF10] = value; }

 private:
  std::array<std::uint8_t, 0x30> regs_{};
};

}  // namespace gb
