#pragma once

#include <array>
#include <cstdint>

#include "interrupts.h"

namespace gb {

// Scanline-accurate PPU: each visible line is rendered in one shot at the
// mode 3 -> mode 0 transition. Mid-scanline raster effects are out of scope
// for v1 (see README). VRAM/OAM access is not mode-blocked in v1.
class Ppu {
 public:
  static constexpr int kWidth = 160;
  static constexpr int kHeight = 144;

  explicit Ppu(Interrupts& interrupts) : interrupts_(interrupts) {}

  void tick(int tcycles);

  std::uint8_t reg_read(std::uint16_t addr) const;
  void reg_write(std::uint16_t addr, std::uint8_t value);

  std::uint8_t vram_read(std::uint16_t addr) const { return vram_[addr & 0x1FFF]; }
  void vram_write(std::uint16_t addr, std::uint8_t value) { vram_[addr & 0x1FFF] = value; }
  std::uint8_t oam_read(std::uint16_t addr) const { return oam_[addr % oam_.size()]; }
  void oam_write(std::uint16_t addr, std::uint8_t value) { oam_[addr % oam_.size()] = value; }

  // Framebuffer of final DMG shades (0 = white .. 3 = darkest).
  const std::array<std::uint8_t, kWidth * kHeight>& framebuffer() const { return fb_; }
  // True once per frame, at the moment VBlank begins.
  bool take_frame() {
    const bool ready = frame_ready_;
    frame_ready_ = false;
    return ready;
  }

 private:
  void set_mode(int mode);
  void update_stat_line();
  void render_scanline();

  Interrupts& interrupts_;

  std::array<std::uint8_t, 0x2000> vram_{};
  std::array<std::uint8_t, 0xA0> oam_{};
  std::array<std::uint8_t, kWidth * kHeight> fb_{};

  std::uint8_t lcdc_ = 0x91;
  std::uint8_t stat_ = 0x85;
  std::uint8_t scy_ = 0, scx_ = 0;
  std::uint8_t ly_ = 0, lyc_ = 0;
  std::uint8_t bgp_ = 0xFC, obp0_ = 0xFF, obp1_ = 0xFF;
  std::uint8_t wy_ = 0, wx_ = 0;

  int dot_ = 0;
  int window_line_ = 0;  // internal window line counter
  bool stat_line_ = false;
  bool frame_ready_ = false;
};

}  // namespace gb
