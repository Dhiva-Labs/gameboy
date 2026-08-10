#include "cpu.h"

namespace gb {

Cpu::Cpu(Mmu& mmu, Interrupts& interrupts) : mmu_(mmu), interrupts_(interrupts) {}

std::uint8_t Cpu::get_r(int idx) {
  switch (idx) {
    case 0: return b_;
    case 1: return c_;
    case 2: return d_;
    case 3: return e_;
    case 4: return h_;
    case 5: return l_;
    case 6: return read8(hl());
    default: return a_;
  }
}

void Cpu::set_r(int idx, std::uint8_t value) {
  switch (idx) {
    case 0: b_ = value; break;
    case 1: c_ = value; break;
    case 2: d_ = value; break;
    case 3: e_ = value; break;
    case 4: h_ = value; break;
    case 5: l_ = value; break;
    case 6: write8(hl(), value); break;
    default: a_ = value; break;
  }
}

std::uint16_t Cpu::get_rp(int idx) const {
  switch (idx) {
    case 0: return bc();
    case 1: return de();
    case 2: return hl();
    default: return sp_;
  }
}

void Cpu::set_rp(int idx, std::uint16_t value) {
  switch (idx) {
    case 0: set_bc(value); break;
    case 1: set_de(value); break;
    case 2: set_hl(value); break;
    default: sp_ = value; break;
  }
}

bool Cpu::test_cc(int idx) const {
  switch (idx) {
    case 0: return !fz();
    case 1: return fz();
    case 2: return !fc();
    default: return fc();
  }
}

void Cpu::push16(std::uint16_t value) {
  tick_internal();
  write8(--sp_, static_cast<std::uint8_t>(value >> 8));
  write8(--sp_, static_cast<std::uint8_t>(value & 0xFF));
}

std::uint16_t Cpu::pop16() {
  const std::uint8_t lo = read8(sp_++);
  const std::uint8_t hi = read8(sp_++);
  return static_cast<std::uint16_t>(lo | (hi << 8));
}

void Cpu::alu(int op, std::uint8_t value) {
  switch (op) {
    case 0: {  // ADD
      const int result = a_ + value;
      set_znhc((result & 0xFF) == 0, false, (a_ & 0xF) + (value & 0xF) > 0xF, result > 0xFF);
      a_ = static_cast<std::uint8_t>(result);
      break;
    }
    case 1: {  // ADC
      const int carry = fc() ? 1 : 0;
      const int result = a_ + value + carry;
      set_znhc((result & 0xFF) == 0, false, (a_ & 0xF) + (value & 0xF) + carry > 0xF,
               result > 0xFF);
      a_ = static_cast<std::uint8_t>(result);
      break;
    }
    case 2: {  // SUB
      const int result = a_ - value;
      set_znhc((result & 0xFF) == 0, true, (a_ & 0xF) < (value & 0xF), a_ < value);
      a_ = static_cast<std::uint8_t>(result);
      break;
    }
    case 3: {  // SBC
      const int carry = fc() ? 1 : 0;
      const int result = a_ - value - carry;
      set_znhc((result & 0xFF) == 0, true, (a_ & 0xF) < (value & 0xF) + carry,
               a_ < value + carry);
      a_ = static_cast<std::uint8_t>(result);
      break;
    }
    case 4:  // AND
      a_ &= value;
      set_znhc(a_ == 0, false, true, false);
      break;
    case 5:  // XOR
      a_ ^= value;
      set_znhc(a_ == 0, false, false, false);
      break;
    case 6:  // OR
      a_ |= value;
      set_znhc(a_ == 0, false, false, false);
      break;
    default: {  // CP
      const int result = a_ - value;
      set_znhc((result & 0xFF) == 0, true, (a_ & 0xF) < (value & 0xF), a_ < value);
      break;
    }
  }
}

std::uint8_t Cpu::inc8(std::uint8_t value) {
  const auto result = static_cast<std::uint8_t>(value + 1);
  set_flag(kFlagZ, result == 0);
  set_flag(kFlagN, false);
  set_flag(kFlagH, (result & 0xF) == 0);
  return result;
}

std::uint8_t Cpu::dec8(std::uint8_t value) {
  const auto result = static_cast<std::uint8_t>(value - 1);
  set_flag(kFlagZ, result == 0);
  set_flag(kFlagN, true);
  set_flag(kFlagH, (result & 0xF) == 0xF);
  return result;
}

void Cpu::add_hl(std::uint16_t value) {
  tick_internal();
  const std::uint16_t old = hl();
  const std::uint32_t result = old + value;
  set_flag(kFlagN, false);
  set_flag(kFlagH, (old & 0x0FFF) + (value & 0x0FFF) > 0x0FFF);
  set_flag(kFlagC, result > 0xFFFF);
  set_hl(static_cast<std::uint16_t>(result));
}

// ADD SP,e and LD HL,SP+e: H/C come from unsigned addition on the low byte;
// Z and N are always cleared.
std::uint16_t Cpu::add_sp_e() {
  const auto e = static_cast<std::int8_t>(fetch8());
  const auto ue = static_cast<std::uint8_t>(e);
  set_znhc(false, false, (sp_ & 0xF) + (ue & 0xF) > 0xF, (sp_ & 0xFF) + ue > 0xFF);
  return static_cast<std::uint16_t>(sp_ + e);
}

void Cpu::daa() {
  int adjust = 0;
  bool carry = fc();
  if (fh() || (!fn() && (a_ & 0xF) > 0x9)) adjust |= 0x06;
  if (fc() || (!fn() && a_ > 0x99)) {
    adjust |= 0x60;
    carry = true;
  }
  a_ = static_cast<std::uint8_t>(fn() ? a_ - adjust : a_ + adjust);
  set_flag(kFlagZ, a_ == 0);
  set_flag(kFlagH, false);
  set_flag(kFlagC, carry);
}

void Cpu::service_interrupt() {
  ime_ = false;
  tick_internal();
  tick_internal();
  write8(--sp_, static_cast<std::uint8_t>(pc_ >> 8));
  // Note: pending is re-sampled after the high push on hardware; the simple
  // pre-sampled model is enough for the v1 test targets.
  const std::uint8_t pending = interrupts_.pending();
  std::uint16_t vector = 0x0000;
  for (int bit = 0; bit < 5; ++bit) {
    if (pending & (1 << bit)) {
      interrupts_.clear(bit);
      vector = static_cast<std::uint16_t>(0x40 + bit * 8);
      break;
    }
  }
  write8(--sp_, static_cast<std::uint8_t>(pc_ & 0xFF));
  pc_ = vector;
  tick_internal();
}

void Cpu::illegal(std::uint8_t) {
  // Hardware locks up. Behave as a HALT that never wakes so tests fail loudly
  // (timeout) instead of corrupting state silently.
  stopped_ = true;
}

void Cpu::step() {
  if (interrupts_.pending()) {
    halted_ = false;
    if (ime_) {
      service_interrupt();
      return;
    }
  }
  if (halted_ || stopped_) {
    tick_internal();
    return;
  }

  const std::uint8_t opcode = read8(pc_);
  if (halt_bug_)
    halt_bug_ = false;  // PC fails to advance: next fetch sees this byte again
  else
    ++pc_;

  execute(opcode);

  if (ei_delay_ > 0 && --ei_delay_ == 0) ime_ = true;
}

}  // namespace gb
