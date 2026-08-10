#pragma once

#include <array>
#include <cstdint>

#include "interrupts.h"

namespace gb {

// P1/JOYP (0xFF00): two 4-bit groups (d-pad, buttons) selected by bits 4-5.
// All lines are active-low.
class Joypad {
 public:
  enum Button { kRight = 0, kLeft, kUp, kDown, kA, kB, kSelect, kStart };

  explicit Joypad(Interrupts& interrupts) : interrupts_(interrupts) {}

  void set_button(Button button, bool pressed) {
    if (pressed && !pressed_[button]) interrupts_.request(Interrupts::kJoypad);
    pressed_[button] = pressed;
  }

  std::uint8_t read() const {
    std::uint8_t low = 0x0F;
    if (!(select_ & 0x10)) {  // d-pad group selected
      for (int i = 0; i < 4; ++i)
        if (pressed_[i]) low = static_cast<std::uint8_t>(low & ~(1 << i));
    }
    if (!(select_ & 0x20)) {  // button group selected
      for (int i = 0; i < 4; ++i)
        if (pressed_[kA + i]) low = static_cast<std::uint8_t>(low & ~(1 << i));
    }
    return static_cast<std::uint8_t>(0xC0 | select_ | low);
  }

  void write(std::uint8_t value) { select_ = value & 0x30; }

 private:
  Interrupts& interrupts_;
  std::uint8_t select_ = 0x30;
  std::array<bool, 8> pressed_{};
};

}  // namespace gb
