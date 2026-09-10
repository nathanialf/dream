# Recomp design

Target: a native C reimplementation of the game, verified frame-by-frame against the
original ROM. This page records the decisions that shape it; it is updated as the port
takes shape.

## Verification

- The reference is the ROM running in an embedded emulator core. The port and the
  reference execute the same scripted inputs; after every frame WRAM, VRAM, CGRAM and OAM
  are compared. A routine is "recomped" only when its C body passes that check across
  the test scripts; `config/recomp.txt` lists them and feeds the `recomp` badge.
- The disassembly (`src/`) is the source of truth for behaviour; the C mirrors its
  routine boundaries and names so the two can be read side by side.

## Input

- Modern controller only, through the platform gamepad API with its standard layout.
  No launcher, no configuration screen, no remapping UI, no config file for input.
- Fixed default mapping, SNES to gamepad: B = south face button, A = east, Y = west,
  X = north, L/R = shoulders, Start = start, Select = back/select, d-pad = d-pad and
  left stick. Keyboard fallback is likewise fixed (arrows, Z/X/A/S, Q/W, Enter, Shift).
- Hot-plug is handled silently; the first connected pad is player 1.

## Presentation

- Runs the game directly at launch: no menu, no settings, no splash beyond what the
  game itself shows.
- Rendering reproduces the PPU output of the reference; enhancements (integer scaling,
  widescreen) come only after lockstep parity and never change simulation state.

## Out of scope

- Emulator-style features (save states, rewind, cheats, shader menus).
- Reading assets from anything but the user's own ROM at first run.

## Harness

The reference emulator described above exists: `recomp/` holds `dream_harness`, a
headless C11 program built on a vendored copy of the LakeSnes core (MIT). It runs the
ROM under a scripted input, hashes WRAM/VRAM/CGRAM/OAM after every frame, records the
executed-PC set for comparison against `out/codemap.txt`, and implements the lockstep
protocol: two instances of the same ROM, one with the recomp's `recomp_hooks[]` table
installed and one without, compared byte for byte after every frame.

    make harness
    ./build/recomp/dream_harness --lockstep --hooks on --frames 600 \
        --input recomp/harness/inputs/title_start_right.txt

Build, CLI, the hook API (`recomp/include/snes_state.h`) and the lockstep protocol
are documented in `recomp/README.md`. The first routine to pass the check is
`clear_sprite_table` (`$C0:A500`), 412 hooked calls over 600 frames with no
mismatches.
