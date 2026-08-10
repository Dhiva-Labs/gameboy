#pragma once

#include <cstdint>

#include "../interrupts.h"
#include "../mmu.h"

namespace gb {

// Sharp SM83 (LR35902). Cycle accounting flows through Mmu::read8/write8 and
// tick_internal(): each advances the machine one M-cycle, so instruction
// timing emerges from the access pattern rather than a per-opcode table.
class Cpu {
 public:
  Cpu(Mmu& mmu, Interrupts& interrupts);

  // Executes one instruction, or services one interrupt, or burns one
  // M-cycle if halted.
  void step();

  std::uint16_t pc() const { return pc_; }
  bool stopped() const { return stopped_; }

 private:
  // Flag bits in F.
  static constexpr std::uint8_t kFlagZ = 0x80;
  static constexpr std::uint8_t kFlagN = 0x40;
  static constexpr std::uint8_t kFlagH = 0x20;
  static constexpr std::uint8_t kFlagC = 0x10;

  // --- cycle-accounted bus helpers ---
  std::uint8_t read8(std::uint16_t addr) { return mmu_.read8(addr); }
  void write8(std::uint16_t addr, std::uint8_t value) { mmu_.write8(addr, value); }
  void tick_internal() { mmu_.tick_mcycle(); }
  std::uint8_t fetch8() { return read8(pc_++); }
  std::uint16_t fetch16() {
    const std::uint8_t lo = fetch8();
    const std::uint8_t hi = fetch8();
    return static_cast<std::uint16_t>(lo | (hi << 8));
  }

  // --- flags ---
  bool fz() const { return f_ & kFlagZ; }
  bool fn() const { return f_ & kFlagN; }
  bool fh() const { return f_ & kFlagH; }
  bool fc() const { return f_ & kFlagC; }
  void set_flag(std::uint8_t flag, bool on) {
    f_ = static_cast<std::uint8_t>(on ? (f_ | flag) : (f_ & ~flag));
  }
  void set_znhc(bool z, bool n, bool h, bool c) {
    f_ = static_cast<std::uint8_t>((z ? kFlagZ : 0) | (n ? kFlagN : 0) | (h ? kFlagH : 0) |
                                   (c ? kFlagC : 0));
  }

  // --- register pairs ---
  std::uint16_t af() const { return static_cast<std::uint16_t>((a_ << 8) | f_); }
  std::uint16_t bc() const { return static_cast<std::uint16_t>((b_ << 8) | c_); }
  std::uint16_t de() const { return static_cast<std::uint16_t>((d_ << 8) | e_); }
  std::uint16_t hl() const { return static_cast<std::uint16_t>((h_ << 8) | l_); }
  void set_af(std::uint16_t v) {
    a_ = static_cast<std::uint8_t>(v >> 8);
    f_ = static_cast<std::uint8_t>(v & 0xF0);  // low nibble of F is wired to 0
  }
  void set_bc(std::uint16_t v) { b_ = static_cast<std::uint8_t>(v >> 8); c_ = static_cast<std::uint8_t>(v); }
  void set_de(std::uint16_t v) { d_ = static_cast<std::uint8_t>(v >> 8); e_ = static_cast<std::uint8_t>(v); }
  void set_hl(std::uint16_t v) { h_ = static_cast<std::uint8_t>(v >> 8); l_ = static_cast<std::uint8_t>(v); }

  // r[idx] table: B,C,D,E,H,L,(HL),A — index 6 is a memory access.
  std::uint8_t get_r(int idx);
  void set_r(int idx, std::uint8_t value);
  // rp[idx]: BC,DE,HL,SP.  rp2[idx]: BC,DE,HL,AF (PUSH/POP).
  std::uint16_t get_rp(int idx) const;
  void set_rp(int idx, std::uint16_t value);

  bool test_cc(int idx) const;  // NZ,Z,NC,C

  void push16(std::uint16_t value);   // includes the internal setup cycle
  std::uint16_t pop16();

  // --- ALU ---
  void alu(int op, std::uint8_t value);  // ADD,ADC,SUB,SBC,AND,XOR,OR,CP
  std::uint8_t inc8(std::uint8_t value);
  std::uint8_t dec8(std::uint8_t value);
  void add_hl(std::uint16_t value);
  std::uint16_t add_sp_e();  // shared by ADD SP,e and LD HL,SP+e (flag rules)
  void daa();

  void execute(std::uint8_t opcode);  // opcodes.cpp
  void execute_cb();                  // opcodes_cb.cpp
  void service_interrupt();
  void illegal(std::uint8_t opcode);

  Mmu& mmu_;
  Interrupts& interrupts_;

  // DMG post-boot register state (no boot ROM).
  std::uint8_t a_ = 0x01, f_ = 0xB0;
  std::uint8_t b_ = 0x00, c_ = 0x13;
  std::uint8_t d_ = 0x00, e_ = 0xD8;
  std::uint8_t h_ = 0x01, l_ = 0x4D;
  std::uint16_t sp_ = 0xFFFE;
  std::uint16_t pc_ = 0x0100;

  bool ime_ = false;
  int ei_delay_ = 0;  // EI takes effect after the following instruction
  bool halted_ = false;
  bool halt_bug_ = false;
  bool stopped_ = false;
};

}  // namespace gb
