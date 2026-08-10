#include "timer.h"

namespace gb {
namespace {

// TAC bits 0-1 select which bit of the internal counter clocks TIMA:
// 00 = 4096Hz (bit 9), 01 = 262144Hz (bit 3), 10 = 65536Hz (bit 5),
// 11 = 16384Hz (bit 7).
constexpr int kTacBit[4] = {9, 3, 5, 7};

}  // namespace

bool Timer::timer_signal() const {
  if (!(tac_ & 0x04)) return false;
  return (counter_ >> kTacBit[tac_ & 0x03]) & 1;
}

void Timer::update_edge() {
  const bool signal = timer_signal();
  if (prev_signal_ && !signal) {
    ++tima_;
    if (tima_ == 0) reload_countdown_ = 4;
  }
  prev_signal_ = signal;
}

void Timer::tick(int tcycles) {
  for (int i = 0; i < tcycles; ++i) {
    if (reload_countdown_ >= 0) {
      if (reload_countdown_ == 0) {
        tima_ = tma_;
        interrupts_.request(Interrupts::kTimer);
      }
      --reload_countdown_;
    }
    ++counter_;
    update_edge();
  }
}

std::uint8_t Timer::read(std::uint16_t addr) const {
  switch (addr) {
    case 0xFF04: return static_cast<std::uint8_t>(counter_ >> 8);
    case 0xFF05: return tima_;
    case 0xFF06: return tma_;
    case 0xFF07: return static_cast<std::uint8_t>(0xF8 | (tac_ & 0x07));
    default: return 0xFF;
  }
}

void Timer::write(std::uint16_t addr, std::uint8_t value) {
  switch (addr) {
    case 0xFF04:
      counter_ = 0;  // resetting DIV can drop the timer signal mid-period
      update_edge();
      break;
    case 0xFF05:
      // Writing TIMA in the reload-delay window cancels the pending reload.
      if (reload_countdown_ > 0) reload_countdown_ = -1;
      tima_ = value;
      break;
    case 0xFF06:
      tma_ = value;
      break;
    case 0xFF07:
      tac_ = value & 0x07;
      update_edge();
      break;
    default:
      break;
  }
}

}  // namespace gb
