#pragma once

#include <cstdint>

#include "interrupts.h"

namespace gb {

// DIV/TIMA/TMA/TAC. Modeled on the real hardware: DIV is the top byte of a
// 16-bit counter that increments every T-cycle, and TIMA increments on the
// falling edge of a selected counter bit ANDed with the TAC enable bit. TIMA
// overflow reloads from TMA and raises the interrupt 4 T-cycles later.
class Timer {
 public:
  explicit Timer(Interrupts& interrupts) : interrupts_(interrupts) {}

  void tick(int tcycles);

  std::uint8_t read(std::uint16_t addr) const;
  void write(std::uint16_t addr, std::uint8_t value);

 private:
  bool timer_signal() const;
  void update_edge();

  Interrupts& interrupts_;
  std::uint16_t counter_ = 0xABCC;  // post-boot DIV = 0xAB
  std::uint8_t tima_ = 0x00;
  std::uint8_t tma_ = 0x00;
  std::uint8_t tac_ = 0x00;
  bool prev_signal_ = false;
  int reload_countdown_ = -1;  // T-cycles until TMA reload; -1 = inactive
};

}  // namespace gb
