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
