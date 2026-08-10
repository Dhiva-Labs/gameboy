#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <string>

#include "gameboy.h"

namespace {

struct Options {
  std::string rom_path;
  int scale = 4;
  bool turbo = false;
};

void print_usage(const char* argv0) {
  std::fprintf(stderr,
               "usage: %s [options] <rom.gb>\n"
               "  --scale N   window scale factor (default 4)\n"
               "  --turbo     run uncapped\n"
               "keys: arrows = d-pad, X = A, Z = B, Enter = Start, RShift = Select,\n"
               "      Tab (hold) = turbo, Esc = quit\n",
               argv0);
}

bool parse_args(int argc, char** argv, Options& opts) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--scale") {
      if (i + 1 >= argc) return false;
      opts.scale = std::atoi(argv[++i]);
    } else if (arg == "--turbo") {
      opts.turbo = true;
    } else if (arg == "--help" || arg == "-h") {
      return false;
    } else if (!arg.empty() && arg[0] == '-') {
      std::fprintf(stderr, "error: unknown option '%s'\n", arg.c_str());
      return false;
    } else {
      opts.rom_path = arg;
    }
  }
  if (opts.scale < 1) opts.scale = 1;
  return !opts.rom_path.empty();
}

// DMG greens, light to dark (ARGB).
constexpr std::uint32_t kShades[4] = {0xFFD0F8E0, 0xFF70C088, 0xFF566834, 0xFF201808};

bool handle_key(gb::Joypad& joypad, const SDL_KeyboardEvent& event, bool down) {
  switch (event.keysym.scancode) {
    case SDL_SCANCODE_RIGHT: joypad.set_button(gb::Joypad::kRight, down); return true;
    case SDL_SCANCODE_LEFT: joypad.set_button(gb::Joypad::kLeft, down); return true;
    case SDL_SCANCODE_UP: joypad.set_button(gb::Joypad::kUp, down); return true;
    case SDL_SCANCODE_DOWN: joypad.set_button(gb::Joypad::kDown, down); return true;
    case SDL_SCANCODE_X: joypad.set_button(gb::Joypad::kA, down); return true;
    case SDL_SCANCODE_Z: joypad.set_button(gb::Joypad::kB, down); return true;
    case SDL_SCANCODE_RETURN: joypad.set_button(gb::Joypad::kStart, down); return true;
    case SDL_SCANCODE_RSHIFT: joypad.set_button(gb::Joypad::kSelect, down); return true;
    default: return false;
  }
}

}  // namespace

int main(int argc, char** argv) {
  Options opts;
  if (!parse_args(argc, argv, opts)) {
    print_usage(argv[0]);
    return 2;
  }

  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }

  int rc = 0;
  SDL_Window* window = nullptr;
  SDL_Renderer* renderer = nullptr;
  SDL_Texture* texture = nullptr;
  try {
    gb::GameBoy machine(gb::Cartridge::from_file(opts.rom_path));
    const std::string title = "gameboy - " + machine.cart.title();

    window = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              gb::Ppu::kWidth * opts.scale, gb::Ppu::kHeight * opts.scale,
                              SDL_WINDOW_SHOWN);
    if (!window) throw std::runtime_error(std::string("SDL_CreateWindow: ") + SDL_GetError());
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, 0);
    if (!renderer) throw std::runtime_error(std::string("SDL_CreateRenderer: ") + SDL_GetError());
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                gb::Ppu::kWidth, gb::Ppu::kHeight);
    if (!texture) throw std::runtime_error(std::string("SDL_CreateTexture: ") + SDL_GetError());

    // 59.727 fps: 70224 T-cycles per frame at 4194304 Hz.
    constexpr double kFramePeriod = 70224.0 / 4194304.0;
    const double freq = static_cast<double>(SDL_GetPerformanceFrequency());
    std::uint64_t last = SDL_GetPerformanceCounter();
    double frame_debt = 0.0;
    bool turbo_hold = false;

    bool running = true;
    while (running) {
      SDL_Event event;
      while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) running = false;
        if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
          const bool down = event.type == SDL_KEYDOWN;
          if (event.key.keysym.scancode == SDL_SCANCODE_ESCAPE && down) running = false;
          else if (event.key.keysym.scancode == SDL_SCANCODE_TAB) turbo_hold = down;
          else if (event.key.repeat == 0) handle_key(machine.joypad, event.key, down);
        }
      }

      // Run the machine until the PPU completes a frame.
      while (!machine.ppu.take_frame() && running) {
        machine.cpu.step();
        if (machine.cpu.stopped()) {
          std::fprintf(stderr, "CPU stopped (STOP or illegal opcode) at PC=0x%04X\n",
                       machine.cpu.pc());
          running = false;
        }
      }

      std::uint32_t pixels[gb::Ppu::kWidth * gb::Ppu::kHeight];
      const auto& fb = machine.ppu.framebuffer();
      for (int i = 0; i < gb::Ppu::kWidth * gb::Ppu::kHeight; ++i) pixels[i] = kShades[fb[i]];
      SDL_UpdateTexture(texture, nullptr, pixels, gb::Ppu::kWidth * sizeof(std::uint32_t));
      SDL_RenderClear(renderer);
      SDL_RenderCopy(renderer, texture, nullptr, nullptr);
      SDL_RenderPresent(renderer);

      // Pace to real time unless turbo.
      const std::uint64_t now = SDL_GetPerformanceCounter();
      const double elapsed = static_cast<double>(now - last) / freq;
      last = now;
      if (!opts.turbo && !turbo_hold) {
        frame_debt += kFramePeriod - elapsed;
        if (frame_debt > 0.0)
          SDL_Delay(static_cast<Uint32>(frame_debt * 1000.0));
        else if (frame_debt < -0.25)
          frame_debt = -0.25;
      } else {
        frame_debt = 0.0;
      }
    }
  } catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    rc = 1;
  }

  if (texture) SDL_DestroyTexture(texture);
  if (renderer) SDL_DestroyRenderer(renderer);
  if (window) SDL_DestroyWindow(window);
  SDL_Quit();
  return rc;
}
