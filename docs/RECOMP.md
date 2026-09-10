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
are documented in `recomp/README.md`.

## The port

`recomp/src/` holds the C, one file per subsystem and one function per 65816
routine, named as in `out/symbols.txt` and commented with the source address of
each block so the two can be read side by side. Each file registers its own entry
addresses from a file-scope constructor, so adding a routine touches no central
table and no build file.

Routine bodies are transliterations, not rewrites: the same branches in the same
order, the same flag side effects, and the same bus accesses in the same order,
because this ROM turns out to observe its own timing in two places (the SPC upload
handshake counts words against the APU's clock, and the joypad wait loop reads the
hblank flag). `recomp/src/dream_time.h` supplies one helper per 65816 access
pattern for that; `--profile` measures a routine against the ROM's own per-call
cost and reports the two as intervals, so "exact" is a thing the harness confirms
rather than a thing the author claims.

A hook is atomic where the routine it replaces is not, so every loop offers the
ROM the chance to take the rest of the routine back (`ss_yield_wanted`) at the top
of each iteration. That is what keeps a several-thousand-cycle routine honest when
the frame boundary or an interrupt lands inside it.

`config/recomp.txt` lists what has passed; `make recomp-check` is the gate, and
`tools/hooks/pre-commit` runs it when a commit touches `recomp/`.
