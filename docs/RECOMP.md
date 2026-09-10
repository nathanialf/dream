# Recomp design

Target: a native C reimplementation of the game, verified frame-by-frame against the
original ROM. This page records the decisions that shape it; it is updated as the port
takes shape.

## Verification

- The reference is the ROM running in an embedded emulator core. The port and the
  reference execute the same scripted inputs; after every frame WRAM, VRAM, CGRAM, OAM
  and the APU's own state (the SPC700's 64 KB of ARAM, the 128 DSP registers and the SPC
  registers) are compared. A routine is "recomped" only when its C body passes that check
  across the test scripts; `config/recomp.txt` lists them and feeds the `recomp` badge.
- The disassembly (`src/`) is the source of truth for behaviour; the C mirrors its
  routine boundaries and names so the two can be read side by side.

## Input

- Modern controller only, through the platform gamepad API with its standard layout.
  No launcher, no configuration screen, no remapping UI, no config file for input.
- Fixed default mapping, SNES to gamepad: B = south face button, A = east, Y = west,
  X = north, L/R = shoulders, Start = start, Select = back/select, d-pad = d-pad and
  left stick. Keyboard fallback is likewise fixed (arrows, Z/X/A/S, Q/W, Enter, Shift).
- Hot-plug is handled silently; the first connected pad is player 1, and a second
  connected pad is player 2 with the same fixed mapping (no UI for it either).

## Presentation

- Runs the game directly at launch: no menu, no settings, no splash beyond what the
  game itself shows.
- Rendering reproduces the PPU output of the reference; enhancements (integer scaling,
  widescreen) come only after lockstep parity and never change simulation state.

## Gallery viewer

The app boots straight into the game, but a **gallery** can be opened from the running game
(hold Select+Start for half a second, or F1 on the keyboard; the same closes it). It is a
viewer, not a settings screen: no options, nothing persisted, the game is paused underneath
and resumes on close. It shows the ROM's content that the game itself never displays:

- the 113 alternate-format sprite frames and the 1555 live frames, with their palettes;
- the unreferenced font and the three picture strips in bank `$C1`;
- the previous build's tileset, palette block and animation-script table in the first 32 KB;
- every BRR sample (playable), with the four the songs never use marked;
- the stale duplicate regions, listed with what live data they shadow.

Everything is decoded from the user's ROM in C at runtime using the same formats the
Python codecs in `tools/assetcodec.py` implement; the list of what to show comes from the
committed manifest `config/assets.txt`. Nothing from the ROM is shipped.

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

## Native app

`recomp/app/` holds `dream`: the game itself, an SDL3 program that loads the
user's own ROM (command line, `./baserom/DREAM.sfc`, `~/.local/share/dream/`, in
that order; refused unless the SHA-1 matches), installs every routine the recomp
has registered, and runs at 60.0988 Hz with picture, sound and a gamepad. No
launcher, no menu, no settings, no config file — the mapping above is compiled in,
and Escape quits.

    make app        # builds SDL3 into build/sdl3 first if the system has none

`dream` and `dream_harness` are deliberately the same machine: the same core, the
same hook dispatcher, the same accessors and the same cycle charge, with SDL as
the only addition. Two hidden flags keep that checkable — `--frames N --input
SCRIPT` runs a harness input script and prints the harness's own frame line, which
must agree field for field with `dream_harness --hooks on` over the same script.
The picture and the sound are still produced under those flags, so the check
covers the platform layer instead of routing around it.

Build, controls and the presentation decisions (256x224 integer-scaled, DSP output
through an SDL audio stream, clock-paced rather than vsync-paced) are documented in
`recomp/app/README.md`.

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

## The sound driver

The ROM's other program runs on the APU: 3514 bytes of SPC700 code, uploaded at
boot and polling its command port for the rest of the session (`spc/driver.asm`,
`spc/spc_map.txt`). `recomp/spc/` is its half of the port, written against
`recomp/include/spc_state.h` and dispatched from a hook in the vendored SPC700's
instruction loop, exactly as `recomp/src/` is on the 65816 side.

Two things about it are its own. There is no cycle charge to calibrate: an APU
cycle is spent by exactly one read, write or idle, so a body that replays a
routine's access sequence costs what the routine cost by construction, and
`dream_harness --test-spc-timing` checks the per-opcode figures against the core.
And the yield boundary is the APU catch-up slice rather than the frame — the SPC
runs in steps against the CPU's clock, so the reference stops mid-routine at an
instant a hooked run would otherwise have to run past.

`config/recomp.txt` lists what has passed; `make recomp-check` is the gate, and
`tools/hooks/pre-commit` runs it when a commit touches `recomp/`.
