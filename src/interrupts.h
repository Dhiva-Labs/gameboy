#pragma once

#include <cstdint>

namespace gb {

// IE (0xFFFF) / IF (0xFF0F) registers. Priority order is bit order:
// VBlank (0) > LCD STAT (1) > Timer (2) > Serial (3) > Joypad (4).
class Interrupts {
 public:
  enum Source : std::uint8_t {
    kVBlank = 1 << 0,
    kStat = 1 << 1,
    kTimer = 1 << 2,
    kSerial = 1 << 3,
    kJoypad = 1 << 4,
  };

  void request(Source source) { if_ |= source; }

  // Set of requested-and-enabled sources; nonzero wakes HALT.
  std::uint8_t pending() const { return static_cast<std::uint8_t>(ie_ & if_ & 0x1F); }

  std::uint8_t read_if() const { return static_cast<std::uint8_t>(0xE0 | if_); }
  void write_if(std::uint8_t value) { if_ = value & 0x1F; }
  std::uint8_t read_ie() const { return ie_; }
  void write_ie(std::uint8_t value) { ie_ = value; }

  void clear(int bit) { if_ = static_cast<std::uint8_t>(if_ & ~(1 << bit)); }

 private:
  std::uint8_t if_ = 0x01;  // post-boot: VBlank already requested
  std::uint8_t ie_ = 0x00;
};

}  // namespace gb
