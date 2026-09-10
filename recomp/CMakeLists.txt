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

# MinGW: use mingw's own C99 printf rather than msvcrt's, so %zu, %llu and the
# PRI macros mean on Windows what they mean everywhere else. The app's frame
# line has to come out byte-identical to dream_harness's on every platform --
# that comparison is the whole point of `dream --frames` (recomp/app/README.md)
# -- and msvcrt's printf would spell those three differently. Recent mingw-w64
# turns this on by default under -std=c11; older ones do not, so say it.
if(MINGW)
  add_compile_definitions(__USE_MINGW_ANSI_STDIO=1)
endif()

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

# --- the coroutine backend ---------------------------------------------------
# The --no-cpu scheduler runs each dispatched body chain on a stack of its own
# and suspends it at a yield (harness/coro.h). That is the one thing in the port
# with no portable spelling, so it is one small API with one backend per
# platform, chosen here and nowhere else: POSIX gets ucontext, Windows gets
# Win32 fibers. Both are exercised without a ROM by `dream_harness --test-coro`.
if(WIN32)
  set(RECOMP_CORO_SRC ${CMAKE_CURRENT_SOURCE_DIR}/harness/coro_fibers.c)
else()
  set(RECOMP_CORO_SRC ${CMAKE_CURRENT_SOURCE_DIR}/harness/coro_ucontext.c)
endif()

# --- harness -----------------------------------------------------------------
add_executable(dream_harness
  harness/main.c
  harness/snes_state.c
  harness/spc_state.c
  harness/hooks.c
  harness/xxh64.c
  ${RECOMP_CORO_SRC}
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
#
# DREAM_FETCH_SDL3=ON has cmake fetch and build SDL3 itself when the machine has
# none, so a CI runner needs no SDL step of its own (.github/workflows/ci.yml,
# release.yml). It is off by default deliberately: a plain `make harness` on a
# box with no network must not try to clone anything, and `make app` builds SDL3
# into build/sdl3 through the root Makefile instead.
option(DREAM_FETCH_SDL3
       "Fetch and build SDL3 from source when the system has none (CI)" OFF)

find_package(SDL3 CONFIG QUIET)

if(NOT SDL3_FOUND AND DREAM_FETCH_SDL3)
  include(FetchContent)
  # The same options `make sdl3` uses: static, no tests, no examples. They are
  # cache variables of SDL's own build, so they have to be set before it is
  # added, and FORCE'd because a previous configure may have cached the default.
  set(SDL_STATIC        ON  CACHE BOOL "" FORCE)
  set(SDL_SHARED        OFF CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY  OFF CACHE BOOL "" FORCE)
  set(SDL_TESTS         OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES      OFF CACHE BOOL "" FORCE)
  set(SDL_INSTALL_TESTS OFF CACHE BOOL "" FORCE)
  set(SDL_INSTALL       OFF CACHE BOOL "" FORCE)
  # Pinned to a release tag, not to the release-3.2.x branch `make sdl3` and
  # `make sdl3-win` clone: CI should not start failing because SDL moved. Bump
  # it deliberately.
  set(DREAM_SDL3_TAG release-3.2.30 CACHE STRING "SDL3 tag to fetch")
  FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        ${DREAM_SDL3_TAG}
    GIT_SHALLOW    TRUE)
  message(STATUS "SDL3 not found: fetching ${DREAM_SDL3_TAG} (DREAM_FETCH_SDL3=ON)")
  # SDL sets no C standard of its own, so it would inherit this file's strict
  # C11 (-std=c11) and lose the POSIX and GNU declarations its platform code
  # needs. Build it the way its own project does -- the compiler's default --
  # and put the port's standard back afterwards.
  set(_dream_c_standard "${CMAKE_C_STANDARD}")
  set(_dream_c_extensions "${CMAKE_C_EXTENSIONS}")
  unset(CMAKE_C_STANDARD)
  unset(CMAKE_C_EXTENSIONS)
  FetchContent_MakeAvailable(SDL3)
  set(CMAKE_C_STANDARD "${_dream_c_standard}")
  set(CMAKE_C_EXTENSIONS "${_dream_c_extensions}")
  # SDL's own CMakeLists always defines the SDL3::SDL3 alias, whichever of the
  # shared and static libraries it built, so app/CMakeLists.txt needs no branch.
  set(SDL3_FOUND TRUE)
endif()

if(SDL3_FOUND)
  add_subdirectory(app)
  message(STATUS "SDL3 found: building the `dream` app as well as dream_harness")
else()
  message(STATUS "SDL3 not found: `dream` skipped, dream_harness only "
                 "(set CMAKE_PREFIX_PATH to an SDL3 install, or "
                 "-DDREAM_FETCH_SDL3=ON to fetch it, to enable it)")
endif()
