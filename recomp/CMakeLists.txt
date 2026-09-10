# Verification harness for the Dream recomp.
#
#   cmake -S recomp -B build/recomp -DCMAKE_BUILD_TYPE=Release && cmake --build build/recomp
#   (or just: make harness, from the repository root)
#
# No SDL, no network, no X: dream_harness is a plain headless C11 program.
cmake_minimum_required(VERSION 3.16)
project(dream_recomp C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

add_compile_options(-Wall -Wextra)

# --- vendored LakeSnes core (MIT; see third_party/lakesnes/UPSTREAM.txt) ------
add_library(lakesnes STATIC
  third_party/lakesnes/snes/apu.c
  third_party/lakesnes/snes/cart.c
  third_party/lakesnes/snes/cpu.c
  third_party/lakesnes/snes/dma.c
  third_party/lakesnes/snes/dsp.c
  third_party/lakesnes/snes/input.c
  third_party/lakesnes/snes/ppu.c
  third_party/lakesnes/snes/snes.c
  third_party/lakesnes/snes/snes_other.c
  third_party/lakesnes/snes/spc.c
  third_party/lakesnes/snes/statehandler.c
)
target_include_directories(lakesnes PUBLIC third_party/lakesnes/snes)

# --- the port ----------------------------------------------------------------
# One file per subsystem, one C function per 65816 routine. Each file registers
# its own entry addresses from a file-scope constructor (RECOMP_REGISTER), so a
# new routine needs no edit here and no central table: the glob picks the file
# up and --hook-table all installs whatever registered itself.
#
# CONFIGURE_DEPENDS re-globs on build, so adding a file does not need a manual
# cmake re-run.
file(GLOB RECOMP_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)

# The SPC700 half of the port, one C function per routine of the sound driver in
# spc/driver.asm, registering itself the same way (RECOMP_SPC_REGISTER) and
# globbed the same way. --spc-hooks on installs whatever registered itself.
file(GLOB RECOMP_SPC_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/spc/*.c)

# --- harness -----------------------------------------------------------------
add_executable(dream_harness
  harness/main.c
  harness/snes_state.c
  harness/spc_state.c
  harness/hooks.c
  harness/xxh64.c
  ${RECOMP_SOURCES}
  ${RECOMP_SPC_SOURCES}
)
target_include_directories(dream_harness PRIVATE include harness)
target_link_libraries(dream_harness PRIVATE lakesnes)

# --- native app (optional) ---------------------------------------------------
# `dream` is the game: SDL3 window, sound and gamepad around the same core and the
# same recomp routines. It is built only when SDL3 is found, so a machine with no
# SDL (CI, this repository's own verification box) still gets dream_harness.
#
#   cmake -S recomp -B build/recomp -DCMAKE_PREFIX_PATH=$PWD/build/sdl3
#
# `make app` from the repository root does that, building SDL3 into build/sdl3
# first if the system has none.
find_package(SDL3 CONFIG QUIET)
if(SDL3_FOUND)
  add_subdirectory(app)
  message(STATUS "SDL3 found: building the `dream` app as well as dream_harness")
else()
  message(STATUS "SDL3 not found: `dream` skipped, dream_harness only "
                 "(set CMAKE_PREFIX_PATH to an SDL3 install to enable it)")
endif()
