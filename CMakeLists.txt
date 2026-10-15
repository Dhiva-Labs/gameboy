cmake_minimum_required(VERSION 3.16)
project(gameboy LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# SDL2: prefer the CMake config package (vcpkg, SDL2 dev packages), fall back
# to pkg-config.
find_package(SDL2 CONFIG QUIET)
if(NOT TARGET SDL2::SDL2)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
  add_library(SDL2::SDL2 ALIAS PkgConfig::SDL2)
endif()

# GCC/Clang get -Wall -Wextra; MSVC gets /W4, UTF-8 sources and no CRT
# deprecation noise for sscanf and friends.
function(gb_set_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /utf-8)
    target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra)
  endif()
endfunction()

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
gb_set_warnings(gb_core)
target_include_directories(gb_core PUBLIC src)

add_executable(gameboy main.cpp)
gb_set_warnings(gameboy)
# SDL.h renames main() to SDL_main() on Windows; SDL2main supplies the real
# entry point.
if(WIN32 AND TARGET SDL2::SDL2main)
  target_link_libraries(gameboy PRIVATE SDL2::SDL2main)
endif()
target_link_libraries(gameboy PRIVATE gb_core SDL2::SDL2)
# Windows: put SDL2.dll next to the executable so the build tree runs as is.
if(WIN32 AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.21)
  add_custom_command(TARGET gameboy POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:gameboy> $<TARGET_FILE_DIR:gameboy>
    COMMAND_EXPAND_LISTS)
endif()

# Headless runner: executes a ROM, captures blargg-style serial output, and can
# dump the framebuffer. Used for the test procedure in the README.
add_executable(gb_run_rom tests/run_rom.cpp)
gb_set_warnings(gb_run_rom)
target_link_libraries(gb_run_rom PRIVATE gb_core)
