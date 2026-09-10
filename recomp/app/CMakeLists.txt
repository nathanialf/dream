# `dream` — the native application. Built only when SDL3 is present; the headless
# harness never depends on it (see the parent CMakeLists.txt).

# The per-routine cycle charge dream_harness calibrated (config/recomp_cycles.txt)
# is baked in rather than read at run time: the app is a game, not a test rig, and
# it must charge exactly what the harness charges or the two stop being the same
# machine. Re-running `make recomp-profile` changes the file, and CONFIGURE_DEPENDS
# re-runs cmake when it does.
set(CYCLES_TXT ${CMAKE_CURRENT_SOURCE_DIR}/../../config/recomp_cycles.txt)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CYCLES_TXT})

set(CYCLES_BODY "")
if(EXISTS ${CYCLES_TXT})
  file(STRINGS ${CYCLES_TXT} CYCLES_LINES)
  foreach(line IN LISTS CYCLES_LINES)
    if(line MATCHES "^[ \t]*[;#]")
      continue()
    endif()
    if(line MATCHES "^[ \t]*([0-9A-Fa-f]+)[ \t]+([^ \t]+)[ \t]+(-?[0-9]+)")
      string(APPEND CYCLES_BODY
             "  { 0x${CMAKE_MATCH_1}u, ${CMAKE_MATCH_3} },   /* ${CMAKE_MATCH_2} */\n")
    endif()
  endforeach()
endif()

file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/dream_cycles.h.in
"/* Generated from config/recomp_cycles.txt by recomp/app/CMakeLists.txt. Do not edit:\n"
" * run `make recomp-profile` and rebuild. */\n"
"#ifndef DREAM_CYCLES_H\n"
"#define DREAM_CYCLES_H\n"
"\n"
"#include <stdint.h>\n"
"\n"
"static const struct { uint32_t addr; int cycles; } kRecompCycles[] = {\n"
"${CYCLES_BODY}"
"  { 0u, 0 }\n"
"};\n"
"static const unsigned kRecompCyclesCount =\n"
"  (unsigned) (sizeof(kRecompCycles) / sizeof(kRecompCycles[0]) - 1u);\n"
"\n"
"#endif\n")
configure_file(${CMAKE_CURRENT_BINARY_DIR}/dream_cycles.h.in
               ${CMAKE_CURRENT_BINARY_DIR}/dream_cycles.h COPYONLY)

# snes_state.c / hooks.c / xxh64.c are the harness's, unchanged: the app installs
# the same routines through the same accessors and prints the same frame line, so
# `dream --frames N --input S` can be diffed against `dream_harness --hooks on`.
add_executable(dream
  main.c
  sha1.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../harness/snes_state.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../harness/hooks.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../harness/xxh64.c
  ${RECOMP_SOURCES}
)
target_include_directories(dream PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
  ${CMAKE_CURRENT_SOURCE_DIR}/../harness
)
target_link_libraries(dream PRIVATE lakesnes SDL3::SDL3)

# Next to dream_harness, so both are build/recomp/<name>.
set_target_properties(dream PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
