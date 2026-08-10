// Headless runner. Executes a ROM and watches the serial port, which is how
// blargg's test ROMs report results ("Passed" / "Failed #n"). Also dumps the
// framebuffer as a BMP for PPU milestones.
//
//   gb_run_rom rom.gb [--max-cycles N] [--frames N] [--screenshot PATH]
//                     [--press BTN:FRAME]...
//
// Exit codes: 0 = serial says Passed, 1 = serial says Failed, 2 = timeout
// (no verdict before the cycle/frame budget ran out).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

#include "gameboy.h"

namespace {

struct PressEvent {
  gb::Joypad::Button button;
  long frame;
};

struct Options {
  std::string rom_path;
  std::uint64_t max_cycles = 800'000'000;  // ~190s of emulated time
  long frames = -1;                        // stop after N frames instead of a verdict
  std::string screenshot_path;
  std::string wav_path;                    // capture APU output
  std::vector<PressEvent> presses;         // press at FRAME, release 8 frames later
};

bool parse_button(const char* name, gb::Joypad::Button& out) {
  static const struct { const char* name; gb::Joypad::Button button; } kMap[] = {
      {"right", gb::Joypad::kRight}, {"left", gb::Joypad::kLeft},
      {"up", gb::Joypad::kUp},       {"down", gb::Joypad::kDown},
      {"a", gb::Joypad::kA},         {"b", gb::Joypad::kB},
      {"select", gb::Joypad::kSelect}, {"start", gb::Joypad::kStart},
  };
  for (const auto& entry : kMap) {
    if (std::strcmp(name, entry.name) == 0) {
      out = entry.button;
      return true;
    }
  }
  return false;
}

bool parse_args(int argc, char** argv, Options& opts) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto next_value = [&]() -> const char* {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "error: %s requires a value\n", arg.c_str());
        return nullptr;
      }
      return argv[++i];
    };
    if (arg == "--max-cycles") {
      const char* v = next_value();
      if (!v) return false;
      opts.max_cycles = std::strtoull(v, nullptr, 0);
    } else if (arg == "--frames") {
      const char* v = next_value();
      if (!v) return false;
      opts.frames = std::atol(v);
    } else if (arg == "--screenshot") {
      const char* v = next_value();
      if (!v) return false;
      opts.screenshot_path = v;
    } else if (arg == "--wav") {
      const char* v = next_value();
      if (!v) return false;
      opts.wav_path = v;
    } else if (arg == "--press") {
      const char* v = next_value();
      if (!v) return false;
      char name[16] = {};
      long frame = 0;
      gb::Joypad::Button button;
      if (std::sscanf(v, "%15[a-z]:%ld", name, &frame) != 2 || !parse_button(name, button)) {
        std::fprintf(stderr, "error: bad --press '%s' (want BUTTON:FRAME)\n", v);
        return false;
      }
      opts.presses.push_back({button, frame});
    } else if (!arg.empty() && arg[0] == '-') {
      std::fprintf(stderr, "error: unknown option '%s'\n", arg.c_str());
      return false;
    } else {
      opts.rom_path = arg;
    }
  }
  return !opts.rom_path.empty();
}

bool write_wav(const std::string& path, const std::vector<std::int16_t>& samples) {
  std::FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  const std::uint32_t data_bytes = static_cast<std::uint32_t>(samples.size() * 2);
  const std::uint32_t byte_rate = gb::Apu::kSampleRate * 2 * 2;
  std::uint8_t header[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E',
                             'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 2, 0};
  const auto put32 = [&header](int off, std::uint32_t v) {
    header[off] = v & 0xFF;
    header[off + 1] = (v >> 8) & 0xFF;
    header[off + 2] = (v >> 16) & 0xFF;
    header[off + 3] = (v >> 24) & 0xFF;
  };
  put32(4, 36 + data_bytes);
  put32(24, gb::Apu::kSampleRate);
  put32(28, byte_rate);
  header[32] = 4;   // block align
  header[34] = 16;  // bits per sample
  header[36] = 'd'; header[37] = 'a'; header[38] = 't'; header[39] = 'a';
  put32(40, data_bytes);
  std::fwrite(header, 1, sizeof(header), f);
  std::fwrite(samples.data(), 2, samples.size(), f);
  std::fclose(f);
  return true;
}

bool write_bmp(const std::string& path, const gb::Ppu& ppu, int scale) {
  // DMG greens, light to dark.
  static constexpr std::uint8_t kShades[4][3] = {
      {0xD0, 0xF8, 0xE0}, {0x70, 0xC0, 0x88}, {0x56, 0x68, 0x34}, {0x20, 0x18, 0x08}};
  const int w = gb::Ppu::kWidth * scale;
  const int h = gb::Ppu::kHeight * scale;
  const int row_bytes = w * 3;
  const int pad = (4 - row_bytes % 4) % 4;
  const auto data_size = static_cast<std::uint32_t>((row_bytes + pad) * h);

  std::FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  std::uint8_t header[54] = {'B', 'M'};
  const auto put32 = [&header](int off, std::uint32_t v) {
    header[off] = v & 0xFF;
    header[off + 1] = (v >> 8) & 0xFF;
    header[off + 2] = (v >> 16) & 0xFF;
    header[off + 3] = (v >> 24) & 0xFF;
  };
  put32(2, 54 + data_size);
  put32(10, 54);
  put32(14, 40);
  put32(18, static_cast<std::uint32_t>(w));
  put32(22, static_cast<std::uint32_t>(h));
  header[26] = 1;
  header[28] = 24;
  put32(34, data_size);
  std::fwrite(header, 1, sizeof(header), f);

  const auto& fb = ppu.framebuffer();
  std::vector<std::uint8_t> line(static_cast<std::size_t>(row_bytes + pad), 0);
  for (int y = h - 1; y >= 0; --y) {
    for (int x = 0; x < w; ++x) {
      const std::uint8_t shade = fb[(y / scale) * gb::Ppu::kWidth + (x / scale)];
      line[x * 3 + 0] = kShades[shade][2];
      line[x * 3 + 1] = kShades[shade][1];
      line[x * 3 + 2] = kShades[shade][0];
    }
    std::fwrite(line.data(), 1, line.size(), f);
  }
  std::fclose(f);
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  Options opts;
  if (!parse_args(argc, argv, opts)) {
    std::fprintf(stderr,
                 "usage: %s <rom.gb> [--max-cycles N] [--frames N] [--screenshot PATH]\n"
                 "       [--press BUTTON:FRAME]...\n",
                 argv[0]);
    return 2;
  }

  try {
    gb::GameBoy machine(gb::Cartridge::from_file(opts.rom_path));
    std::fprintf(stderr, "loaded '%s' (mapper %d)\n", machine.cart.title().c_str(),
                 machine.cart.mapper());

    std::size_t printed = 0;
    long frame_count = 0;
    int verdict = 2;
    bool mem_protocol_started = false;
    std::vector<std::int16_t> wav_samples;
    while (machine.mmu.cycles() < opts.max_cycles) {
      machine.cpu.step();

      const auto& log = machine.mmu.serial_log();
      if (log.size() > printed) {
        std::fwrite(log.data() + printed, 1, log.size() - printed, stdout);
        std::fflush(stdout);
        printed = log.size();
        if (log.find("Passed") != std::string::npos) {
          verdict = 0;
          break;
        }
        if (log.find("Failed") != std::string::npos) {
          verdict = 1;
          break;
        }
      }

      if (machine.ppu.take_frame()) {
        ++frame_count;
        if (!opts.wav_path.empty()) {
          const auto chunk = machine.apu.take_samples();
          wav_samples.insert(wav_samples.end(), chunk.begin(), chunk.end());
        }
        for (const auto& press : opts.presses) {
          if (frame_count == press.frame) machine.joypad.set_button(press.button, true);
          if (frame_count == press.frame + 8) machine.joypad.set_button(press.button, false);
        }
        if (opts.frames >= 0 && frame_count >= opts.frames) break;

        // blargg memory-result protocol: signature DE B0 61 at 0xA001 while
        // status 0xA000 reads 0x80 (running); any other status is the result.
        const bool sig = machine.cart.debug_ram(1) == 0xDE &&
                         machine.cart.debug_ram(2) == 0xB0 &&
                         machine.cart.debug_ram(3) == 0x61;
        if (sig) {
          const std::uint8_t status = machine.cart.debug_ram(0);
          if (status == 0x80) {
            mem_protocol_started = true;
          } else if (mem_protocol_started) {
            std::string text;
            for (std::uint16_t i = 4; i < 0x1000; ++i) {
              const std::uint8_t ch = machine.cart.debug_ram(i);
              if (ch == 0) break;
              text += static_cast<char>(ch);
            }
            std::printf("%s\n[memory result: %u]\n", text.c_str(), status);
            verdict = status == 0 ? 0 : 1;
            break;
          }
        }
      }
    }

    std::printf("\n[%llu cycles, %ld frames]\n",
                static_cast<unsigned long long>(machine.mmu.cycles()), frame_count);
    if (verdict == 2 && opts.frames >= 0) verdict = 0;  // frame-budget mode has no verdict
    if (verdict == 2) std::printf("TIMEOUT: no Passed/Failed on serial\n");

    if (!opts.screenshot_path.empty() && !write_bmp(opts.screenshot_path, machine.ppu, 2)) {
      std::fprintf(stderr, "error: failed to write %s\n", opts.screenshot_path.c_str());
      return 1;
    }
    if (!opts.wav_path.empty()) {
      if (!write_wav(opts.wav_path, wav_samples)) {
        std::fprintf(stderr, "error: failed to write %s\n", opts.wav_path.c_str());
        return 1;
      }
      std::fprintf(stderr, "wrote %zu stereo samples to %s\n", wav_samples.size() / 2,
                   opts.wav_path.c_str());
    }
    return verdict;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
  }
}
