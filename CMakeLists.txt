cmake_minimum_required(VERSION 3.16)
project(gameboy LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# SDL2: prefer the CMake config package, fall back to pkg-config.
find_package(SDL2 CONFIG QUIET)
if(NOT TARGET SDL2::SDL2)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
  add_library(SDL2::SDL2 ALIAS PkgConfig::SDL2)
endif()

add_library(gb_core
  src/cpu/cpu.cpp
  src/cpu/opcodes.cpp
  src/cpu/opcodes_cb.cpp
  src/mmu.cpp
  src/ppu.cpp
  src/timer.cpp
  src/interrupts.cpp
  src/joypad.cpp
  src/apu.cpp
)
target_compile_options(gb_core PRIVATE -Wall -Wextra)
target_include_directories(gb_core PUBLIC src)

add_executable(gameboy main.cpp)
target_compile_options(gameboy PRIVATE -Wall -Wextra)
target_link_libraries(gameboy PRIVATE gb_core SDL2::SDL2)

# Headless runner: executes a ROM, captures blargg-style serial output, and can
# dump the framebuffer. Used for the test procedure in the README.
add_executable(gb_run_rom tests/run_rom.cpp)
target_compile_options(gb_run_rom PRIVATE -Wall -Wextra)
target_link_libraries(gb_run_rom PRIVATE gb_core)
