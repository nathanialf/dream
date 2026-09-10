# Cross toolchain: Linux host -> Windows x86_64 via mingw-w64.
#
#   cmake -S recomp -B build/win -DCMAKE_TOOLCHAIN_FILE=recomp/cmake/mingw-w64.cmake
#
# or, with SDL3 built for the same target first, `make app-win` from the
# repository root (see recomp/app/README.md, "Windows").
#
# MinGW is the supported Windows compiler for this port. The registry every
# recomp/src file fills in from its own file-scope constructor
# (RECOMP_REGISTER, recomp/include/snes_state.h) needs __attribute__((constructor)),
# which GCC, clang and MinGW have and MSVC does not; the header says so in an
# #error rather than building an empty registry.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(_prefix x86_64-w64-mingw32)
set(_hints "")
if(DEFINED ENV{DREAM_MINGW_ROOT})
  list(APPEND _hints "$ENV{DREAM_MINGW_ROOT}/bin")
endif()

# The -posix flavour first where a distribution ships both: SDL3 builds its own
# threading on Win32 primitives either way, but the flavour that carries
# winpthreads is the one everything else expects.
find_program(CMAKE_C_COMPILER   NAMES ${_prefix}-gcc-posix ${_prefix}-gcc HINTS ${_hints} REQUIRED)
find_program(CMAKE_RC_COMPILER  NAMES ${_prefix}-windres                  HINTS ${_hints})
# Nothing here is C++ (the port, the core and SDL3 are C), so a distribution
# that ships only the C cross driver (this one does) is enough. The variable is
# still set when the g++ driver exists, for anything that enables CXX.
find_program(CMAKE_CXX_COMPILER NAMES ${_prefix}-g++-posix ${_prefix}-g++ HINTS ${_hints})
if(NOT CMAKE_CXX_COMPILER)
  unset(CMAKE_CXX_COMPILER CACHE)
endif()

# Cross-built binaries do not run on the build host: there is no Wine here, so
# nothing may try to execute one during configure or build.
set(CMAKE_CROSSCOMPILING_EMULATOR "")

# The target's own sysroot, plus anything the caller pointed CMAKE_PREFIX_PATH
# at: find_package only looks inside CMAKE_FIND_ROOT_PATH here (PACKAGE mode is
# ONLY, so a host SDL3 in /usr can never be picked up by mistake), and the SDL3
# built for this target lives outside the sysroot, in build/sdl3-win.
set(CMAKE_FIND_ROOT_PATH /usr/${_prefix})
foreach(_p IN LISTS CMAKE_PREFIX_PATH)
  list(APPEND CMAKE_FIND_ROOT_PATH "${_p}")
endforeach()
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Static libgcc and winpthread, so the release zip is the executables and
# nothing else: the only DLLs the .exe imports are Windows' own. Checked with
# `x86_64-w64-mingw32-objdump -p build/win/dream.exe | grep 'DLL Name'`.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")
