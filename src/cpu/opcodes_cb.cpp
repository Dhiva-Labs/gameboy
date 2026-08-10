// CB-prefixed opcodes: rotates/shifts/SWAP (x=0), BIT (x=1), RES (x=2),
// SET (x=3). SM83 replaces Z80's SLL with SWAP.
#include "cpu.h"

namespace gb {

void Cpu::execute_cb() {
  const std::uint8_t op = fetch8();
  const int x = op >> 6;
  const int y = (op >> 3) & 7;
  const int z = op & 7;

  if (x == 1) {  // BIT y,r — read-only, C preserved
    const std::uint8_t value = get_r(z);
    set_flag(kFlagZ, !(value & (1 << y)));
    set_flag(kFlagN, false);
    set_flag(kFlagH, true);
    return;
  }
  if (x == 2) {  // RES y,r
    set_r(z, static_cast<std::uint8_t>(get_r(z) & ~(1 << y)));
    return;
  }
  if (x == 3) {  // SET y,r
    set_r(z, static_cast<std::uint8_t>(get_r(z) | (1 << y)));
    return;
  }

  // x == 0: rotate/shift group
  std::uint8_t value = get_r(z);
  int carry;
  switch (y) {
    case 0:  // RLC
      carry = value >> 7;
      value = static_cast<std::uint8_t>((value << 1) | carry);
      break;
    case 1:  // RRC
      carry = value & 1;
      value = static_cast<std::uint8_t>((value >> 1) | (carry << 7));
      break;
    case 2:  // RL
      carry = value >> 7;
      value = static_cast<std::uint8_t>((value << 1) | (fc() ? 1 : 0));
      break;
    case 3:  // RR
      carry = value & 1;
      value = static_cast<std::uint8_t>((value >> 1) | (fc() ? 0x80 : 0));
      break;
    case 4:  // SLA
      carry = value >> 7;
      value = static_cast<std::uint8_t>(value << 1);
      break;
    case 5:  // SRA
      carry = value & 1;
      value = static_cast<std::uint8_t>((value >> 1) | (value & 0x80));
      break;
    case 6:  // SWAP
      carry = 0;
      value = static_cast<std::uint8_t>((value << 4) | (value >> 4));
      break;
    default:  // SRL
      carry = value & 1;
      value = static_cast<std::uint8_t>(value >> 1);
      break;
  }
  set_r(z, value);
  set_znhc(value == 0, false, false, carry != 0);
}

}  // namespace gb
