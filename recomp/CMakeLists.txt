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

# --- harness -----------------------------------------------------------------
add_executable(dream_harness
  harness/main.c
  harness/snes_state.c
  harness/hooks.c
  harness/xxh64.c
)
target_include_directories(dream_harness PRIVATE include harness)
target_link_libraries(dream_harness PRIVATE lakesnes)
