#include "ppu.h"

#include <algorithm>

namespace gb {

namespace {
constexpr int kDotsPerLine = 456;
constexpr int kMode2End = 80;
constexpr int kMode3End = 252;
constexpr int kVBlankStart = 144;
constexpr int kLastLine = 153;
}  // namespace

void Ppu::set_mode(int mode) {
  stat_ = static_cast<std::uint8_t>((stat_ & ~0x03) | mode);
}

// The STAT interrupt fires on the rising edge of the OR of all enabled
// conditions ("STAT blocking") — two conditions active back to back produce
// one interrupt, which games rely on.
void Ppu::update_stat_line() {
  const int mode = stat_ & 0x03;
  bool line = false;
  if ((stat_ & 0x40) && (stat_ & 0x04)) line = true;             // LYC
  if ((stat_ & 0x20) && mode == 2) line = true;
  if ((stat_ & 0x10) && mode == 1) line = true;
  if ((stat_ & 0x08) && mode == 0) line = true;
  if (line && !stat_line_) interrupts_.request(Interrupts::kStat);
  stat_line_ = line;
}

void Ppu::tick(int tcycles) {
  if (!(lcdc_ & 0x80)) return;  // LCD off: frozen at LY=0, mode 0

  for (int i = 0; i < tcycles; ++i) {
    ++dot_;
    if (dot_ >= kDotsPerLine) {
      dot_ = 0;
      ++ly_;
      if (ly_ > kLastLine) {
        ly_ = 0;
        window_line_ = 0;
      }
      if (ly_ == kVBlankStart) {
        set_mode(1);
        interrupts_.request(Interrupts::kVBlank);
        frame_ready_ = true;
      }
      stat_ = static_cast<std::uint8_t>((stat_ & ~0x04) | (ly_ == lyc_ ? 0x04 : 0));
      update_stat_line();
    }

    if (ly_ < kVBlankStart) {
      const int mode = stat_ & 0x03;
      if (dot_ < kMode2End) {
        if (mode != 2) {
          set_mode(2);
          update_stat_line();
        }
      } else if (dot_ < kMode3End) {
        if (mode != 3) {
          set_mode(3);
          update_stat_line();  // no interrupt source for mode 3; keeps line fresh
        }
      } else if (mode != 0) {
        set_mode(0);
        render_scanline();
        update_stat_line();
      }
    }
  }
}

std::uint8_t Ppu::reg_read(std::uint16_t addr) const {
  switch (addr) {
    case 0xFF40: return lcdc_;
    case 0xFF41: return static_cast<std::uint8_t>(0x80 | stat_);
    case 0xFF42: return scy_;
    case 0xFF43: return scx_;
    case 0xFF44: return ly_;
    case 0xFF45: return lyc_;
    case 0xFF47: return bgp_;
    case 0xFF48: return obp0_;
    case 0xFF49: return obp1_;
    case 0xFF4A: return wy_;
    case 0xFF4B: return wx_;
    default: return 0xFF;
  }
}

void Ppu::reg_write(std::uint16_t addr, std::uint8_t value) {
  switch (addr) {
    case 0xFF40: {
      const bool was_on = lcdc_ & 0x80;
      lcdc_ = value;
      if (was_on && !(lcdc_ & 0x80)) {  // turning off resets the state machine
        ly_ = 0;
        dot_ = 0;
        window_line_ = 0;
        set_mode(0);
        stat_ = static_cast<std::uint8_t>((stat_ & ~0x04) | (ly_ == lyc_ ? 0x04 : 0));
      }
      break;
    }
    case 0xFF41:
      stat_ = static_cast<std::uint8_t>((stat_ & 0x07) | (value & 0x78));
      update_stat_line();
      break;
    case 0xFF42: scy_ = value; break;
    case 0xFF43: scx_ = value; break;
    case 0xFF44: break;  // LY is read-only
    case 0xFF45:
      lyc_ = value;
      stat_ = static_cast<std::uint8_t>((stat_ & ~0x04) | (ly_ == lyc_ ? 0x04 : 0));
      update_stat_line();
      break;
    case 0xFF47: bgp_ = value; break;
    case 0xFF48: obp0_ = value; break;
    case 0xFF49: obp1_ = value; break;
    case 0xFF4A: wy_ = value; break;
    case 0xFF4B: wx_ = value; break;
    default: break;
  }
}

void Ppu::render_scanline() {
  std::array<std::uint8_t, kWidth> bg_index{};  // pre-palette color index, for OBJ priority
  std::uint8_t* line = &fb_[static_cast<std::size_t>(ly_) * kWidth];

  // Background
  if (lcdc_ & 0x01) {
    const std::uint16_t map_base = (lcdc_ & 0x08) ? 0x1C00 : 0x1800;
    const std::uint8_t y = static_cast<std::uint8_t>(ly_ + scy_);
    const std::uint16_t map_row = map_base + (y / 8) * 32;
    for (int x = 0; x < kWidth; ++x) {
      const std::uint8_t xx = static_cast<std::uint8_t>(x + scx_);
      const std::uint8_t tile = vram_[map_row + xx / 8];
      std::uint16_t tile_addr;
      if (lcdc_ & 0x10)
        tile_addr = static_cast<std::uint16_t>(tile * 16);
      else
        tile_addr = static_cast<std::uint16_t>(0x1000 + static_cast<std::int8_t>(tile) * 16);
      tile_addr = static_cast<std::uint16_t>(tile_addr + (y % 8) * 2);
      const int bit = 7 - (xx % 8);
      const int index = ((vram_[tile_addr] >> bit) & 1) | (((vram_[tile_addr + 1] >> bit) & 1) << 1);
      bg_index[x] = static_cast<std::uint8_t>(index);
      line[x] = static_cast<std::uint8_t>((bgp_ >> (index * 2)) & 0x03);
    }
  } else {
    for (int x = 0; x < kWidth; ++x) {
      bg_index[x] = 0;
      line[x] = 0;
    }
  }

  // Window
  bool window_drawn = false;
  if ((lcdc_ & 0x01) && (lcdc_ & 0x20) && ly_ >= wy_ && wx_ <= 166) {
    const std::uint16_t map_base = (lcdc_ & 0x40) ? 0x1C00 : 0x1800;
    const std::uint16_t map_row = map_base + (window_line_ / 8) * 32;
    const int start_x = wx_ < 7 ? 0 : wx_ - 7;
    for (int x = start_x; x < kWidth; ++x) {
      const int wx_px = x - (wx_ - 7);
      if (wx_px < 0) continue;
      const std::uint8_t tile = vram_[map_row + wx_px / 8];
      std::uint16_t tile_addr;
      if (lcdc_ & 0x10)
        tile_addr = static_cast<std::uint16_t>(tile * 16);
      else
        tile_addr = static_cast<std::uint16_t>(0x1000 + static_cast<std::int8_t>(tile) * 16);
      tile_addr = static_cast<std::uint16_t>(tile_addr + (window_line_ % 8) * 2);
      const int bit = 7 - (wx_px % 8);
      const int index = ((vram_[tile_addr] >> bit) & 1) | (((vram_[tile_addr + 1] >> bit) & 1) << 1);
      bg_index[x] = static_cast<std::uint8_t>(index);
      line[x] = static_cast<std::uint8_t>((bgp_ >> (index * 2)) & 0x03);
      window_drawn = true;
    }
  }
  if (window_drawn) ++window_line_;

  // Sprites
  if (lcdc_ & 0x02) {
    const int height = (lcdc_ & 0x04) ? 16 : 8;
    struct Sprite {
      int x, y, oam_index;
      std::uint8_t tile, flags;
    };
    std::array<Sprite, 10> selected;
    int count = 0;
    for (int i = 0; i < 40 && count < 10; ++i) {
      const int sy = oam_[i * 4 + 0] - 16;
      if (ly_ < sy || ly_ >= sy + height) continue;
      selected[count++] = {oam_[i * 4 + 1] - 8, sy, i, oam_[i * 4 + 2], oam_[i * 4 + 3]};
    }
    // DMG priority: smaller X wins; ties resolved by OAM order (stable sort).
    std::stable_sort(selected.begin(), selected.begin() + count,
                     [](const Sprite& a, const Sprite& b) { return a.x < b.x; });
    for (int x = 0; x < kWidth; ++x) {
      for (int s = 0; s < count; ++s) {
        const Sprite& sp = selected[s];
        if (x < sp.x || x >= sp.x + 8) continue;
        int row = ly_ - sp.y;
        if (sp.flags & 0x40) row = height - 1 - row;  // Y flip
        std::uint8_t tile = sp.tile;
        if (height == 16) tile = (tile & 0xFE) | (row >= 8 ? 1 : 0);
        const std::uint16_t tile_addr = static_cast<std::uint16_t>(tile * 16 + (row % 8) * 2);
        int bit = x - sp.x;
        if (!(sp.flags & 0x20)) bit = 7 - bit;  // X flip inverts the natural order
        const int index =
            ((vram_[tile_addr] >> bit) & 1) | (((vram_[tile_addr + 1] >> bit) & 1) << 1);
        if (index == 0) continue;  // transparent
        if ((sp.flags & 0x80) && bg_index[x] != 0) break;  // BG priority
        const std::uint8_t pal = (sp.flags & 0x10) ? obp1_ : obp0_;
        line[x] = static_cast<std::uint8_t>((pal >> (index * 2)) & 0x03);
        break;  // first sprite in priority order wins
      }
    }
  }
}

}  // namespace gb
