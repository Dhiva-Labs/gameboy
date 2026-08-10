// Base opcode table via the classic x/y/z bitfield decode:
//   x = op >> 6, y = (op >> 3) & 7, z = op & 7, p = y >> 1, q = y & 1.
// SM83 differs from Z80 in the x=0/z=0 row (no EX AF / DJNZ; STOP and
// LD (nn),SP instead), the x=3 memory rows (LDH / LD (C) / ADD SP,e /
// LD HL,SP+e), RETI at 0xD9, and eleven removed opcodes that lock the CPU.
#include "cpu.h"

namespace gb {

void Cpu::execute(std::uint8_t op) {
  const int x = op >> 6;
  const int y = (op >> 3) & 7;
  const int z = op & 7;
  const int p = y >> 1;
  const int q = y & 1;

  switch (x) {
    case 0:
      switch (z) {
        case 0:
          switch (y) {
            case 0: break;  // NOP
            case 1: {       // LD (nn),SP
              const std::uint16_t addr = fetch16();
              write8(addr, static_cast<std::uint8_t>(sp_ & 0xFF));
              write8(addr + 1, static_cast<std::uint8_t>(sp_ >> 8));
              break;
            }
            case 2:  // STOP
              fetch8();
              stopped_ = true;
              break;
            case 3: {  // JR e
              const auto e = static_cast<std::int8_t>(fetch8());
              tick_internal();
              pc_ = static_cast<std::uint16_t>(pc_ + e);
              break;
            }
            default: {  // JR cc,e
              const auto e = static_cast<std::int8_t>(fetch8());
              if (test_cc(y - 4)) {
                tick_internal();
                pc_ = static_cast<std::uint16_t>(pc_ + e);
              }
              break;
            }
          }
          break;
        case 1:
          if (q) {
            add_hl(get_rp(p));  // ADD HL,rp
          } else {
            set_rp(p, fetch16());  // LD rp,nn
          }
          break;
        case 2: {  // LD (BC)/(DE)/(HL+)/(HL-),A and the A,(...) mirrors
          std::uint16_t addr;
          switch (p) {
            case 0: addr = bc(); break;
            case 1: addr = de(); break;
            case 2: addr = hl(); set_hl(static_cast<std::uint16_t>(addr + 1)); break;
            default: addr = hl(); set_hl(static_cast<std::uint16_t>(addr - 1)); break;
          }
          if (q)
            a_ = read8(addr);
          else
            write8(addr, a_);
          break;
        }
        case 3:  // INC/DEC rp
          tick_internal();
          set_rp(p, static_cast<std::uint16_t>(get_rp(p) + (q ? -1 : 1)));
          break;
        case 4: set_r(y, inc8(get_r(y))); break;  // INC r
        case 5: set_r(y, dec8(get_r(y))); break;  // DEC r
        case 6: set_r(y, fetch8()); break;        // LD r,n
        case 7:
          switch (y) {
            case 0: {  // RLCA
              const int c = a_ >> 7;
              a_ = static_cast<std::uint8_t>((a_ << 1) | c);
              set_znhc(false, false, false, c);
              break;
            }
            case 1: {  // RRCA
              const int c = a_ & 1;
              a_ = static_cast<std::uint8_t>((a_ >> 1) | (c << 7));
              set_znhc(false, false, false, c);
              break;
            }
            case 2: {  // RLA
              const int old = fc() ? 1 : 0;
              const int c = a_ >> 7;
              a_ = static_cast<std::uint8_t>((a_ << 1) | old);
              set_znhc(false, false, false, c);
              break;
            }
            case 3: {  // RRA
              const int old = fc() ? 1 : 0;
              const int c = a_ & 1;
              a_ = static_cast<std::uint8_t>((a_ >> 1) | (old << 7));
              set_znhc(false, false, false, c);
              break;
            }
            case 4: daa(); break;
            case 5:  // CPL
              a_ = static_cast<std::uint8_t>(~a_);
              set_flag(kFlagN, true);
              set_flag(kFlagH, true);
              break;
            case 6:  // SCF
              set_flag(kFlagN, false);
              set_flag(kFlagH, false);
              set_flag(kFlagC, true);
              break;
            default:  // CCF
              set_flag(kFlagN, false);
              set_flag(kFlagH, false);
              set_flag(kFlagC, !fc());
              break;
          }
          break;
      }
      break;

    case 1:
      if (op == 0x76) {  // HALT
        if (!ime_ && interrupts_.pending())
          halt_bug_ = true;  // PC fails to advance past the next byte
        else
          halted_ = true;
      } else {
        set_r(y, get_r(z));  // LD r,r'
      }
      break;

    case 2:
      alu(y, get_r(z));
      break;

    case 3:
      switch (z) {
        case 0:
          switch (y) {
            case 0: case 1: case 2: case 3: {  // RET cc
              tick_internal();
              if (test_cc(y)) {
                pc_ = pop16();
                tick_internal();
              }
              break;
            }
            case 4:  // LDH (n),A
              write8(static_cast<std::uint16_t>(0xFF00 + fetch8()), a_);
              break;
            case 5: {  // ADD SP,e
              const std::uint16_t result = add_sp_e();
              tick_internal();
              tick_internal();
              sp_ = result;
              break;
            }
            case 6:  // LDH A,(n)
              a_ = read8(static_cast<std::uint16_t>(0xFF00 + fetch8()));
              break;
            default: {  // LD HL,SP+e
              const std::uint16_t result = add_sp_e();
              tick_internal();
              set_hl(result);
              break;
            }
          }
          break;
        case 1:
          if (!q) {  // POP rp2
            const std::uint16_t value = pop16();
            switch (p) {
              case 0: set_bc(value); break;
              case 1: set_de(value); break;
              case 2: set_hl(value); break;
              default: set_af(value); break;
            }
          } else {
            switch (p) {
              case 0:  // RET
                pc_ = pop16();
                tick_internal();
                break;
              case 1:  // RETI
                pc_ = pop16();
                tick_internal();
                ime_ = true;
                break;
              case 2:  // JP HL
                pc_ = hl();
                break;
              default:  // LD SP,HL
                tick_internal();
                sp_ = hl();
                break;
            }
          }
          break;
        case 2:
          switch (y) {
            case 0: case 1: case 2: case 3: {  // JP cc,nn
              const std::uint16_t addr = fetch16();
              if (test_cc(y)) {
                tick_internal();
                pc_ = addr;
              }
              break;
            }
            case 4: write8(static_cast<std::uint16_t>(0xFF00 + c_), a_); break;  // LD (C),A
            case 5: write8(fetch16(), a_); break;                                // LD (nn),A
            case 6: a_ = read8(static_cast<std::uint16_t>(0xFF00 + c_)); break;  // LD A,(C)
            default: a_ = read8(fetch16()); break;                               // LD A,(nn)
          }
          break;
        case 3:
          switch (y) {
            case 0: {  // JP nn
              const std::uint16_t addr = fetch16();
              tick_internal();
              pc_ = addr;
              break;
            }
            case 1: execute_cb(); break;
            case 6:  // DI
              ime_ = false;
              ei_delay_ = 0;
              break;
            case 7:  // EI
              if (!ime_ && ei_delay_ == 0) ei_delay_ = 2;
              break;
            default: illegal(op); break;  // 0xD3, 0xDB, 0xE3, 0xE4(z=3? no) — removed
          }
          break;
        case 4:
          if (y < 4) {  // CALL cc,nn
            const std::uint16_t addr = fetch16();
            if (test_cc(y)) {
              push16(pc_);
              pc_ = addr;
            }
          } else {
            illegal(op);  // 0xE4, 0xEC, 0xF4, 0xFC
          }
          break;
        case 5:
          if (!q) {  // PUSH rp2
            switch (p) {
              case 0: push16(bc()); break;
              case 1: push16(de()); break;
              case 2: push16(hl()); break;
              default: push16(af()); break;
            }
          } else if (p == 0) {  // CALL nn
            const std::uint16_t addr = fetch16();
            push16(pc_);
            pc_ = addr;
          } else {
            illegal(op);  // 0xDD, 0xED, 0xFD
          }
          break;
        case 6:
          alu(y, fetch8());
          break;
        default:  // RST y*8
          push16(pc_);
          pc_ = static_cast<std::uint16_t>(y * 8);
          break;
      }
      break;
  }
}

}  // namespace gb
