# Recomp design

Target: a native C reimplementation of the game, verified frame-by-frame against the
original ROM. This page records the design decisions; it is updated as the port
progresses.

## Verification

- The reference is the ROM running in an embedded emulator core. The port and the
  reference execute the same scripted inputs; after every frame WRAM, VRAM, CGRAM, OAM
  and the APU's own state (the SPC700's 64 KB of ARAM, the 128 DSP registers and the SPC
  registers) are compared. A routine is "recomped" only when its C body passes that check
  across the test scripts; `config/recomp.txt` lists them and feeds the `recomp` badge.
- Some routines no script can reach: the port commands no live 65816 code sends, the
  sequence opcodes no song emits, the stale jump-table slots, the one-row OAM emitters,
  the animation-rate entries no table word points at, and the routines with no caller
  anywhere in the ROM. They get the same comparison one routine at a time instead:
  `dream_harness --unit` boots the ROM to a frame of a script, seeds the registers and a
  few memory cells from `config/recomp_units.txt`, runs the ROM's routine and the C body
  from that identical state, and compares the seven regions, every register and the cycle
  counts. At least four seeds each, all of which must pass. `config/recomp.txt` marks a
  routine credited this way `; unit`, so the badge shows how each routine was credited.
- The disassembly (`src/`) is the source of truth for behaviour; the C mirrors its
  routine boundaries and names so the two can be read side by side.

## Input

- Modern controller only, through the platform gamepad API with its standard layout.
  No launcher, no remapping UI, no config file for input. The only in-app UI is the menu
  bar described below.
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

## Menu bar and gallery viewer

The app boots straight into the game, with a conventional desktop-style menu bar drawn
across the top of the window (SDL3 has no native menus, so it is rendered by the app).
Mouse-driven, with a keyboard fallback (Alt/F10, arrows, Enter, Escape).

- **File**: Quit.
- **View**: Scale 1x/2x/3x/4x, Fit to window (integer), Aspect 8:7 or 4:3, Fullscreen.
  Scaling never touches emulation state; there is no config file, the window size is the
  only memory.
- **Gallery**: a read-only viewer. Opening a page pauses the game and shows
  the page in the viewport; closing resumes exactly. It shows the ROM's content that the
  game itself never displays:

- each `game_mode`'s whole level and the title screen, composed out of the game's own
  code and the emulator's own PPU rather than assembled by the viewer;
- VRAM as each scene's init leaves it, with the palette row its maps give each tile;
- the 113 alternate-format sprite frames and the 1555 live frames, with their palettes;
- the unreferenced font and the three picture strips in bank `$C1`;
- the previous build's tileset, palette block and animation-script table in the first 32 KB;
- every BRR sample (playable), with the four the songs never use marked;
- the stale duplicate regions, listed with what live data they shadow.

Everything but the level pages is decoded from the user's ROM in C at runtime using the
same formats the Python codecs in `tools/assetcodec.py` implement; the list of what to
show comes from the committed manifest `config/assets.txt`. Nothing from the ROM is
shipped.

A level is the exception, because it is not a file: several tilesets at different VRAM
addresses, a metatile map the blitter turns into tilemap columns one column a frame as
the camera moves, a static tilemap on another layer, a palette row per tilemap word and
a scroll register per layer written by the mode's own NMI handler. Nothing assembled by
hand can be trusted to agree with that, so the viewer assembles nothing: a second
machine is booted from reset (the trick the Music page already uses for the sound
driver), its camera is walked along the level with the game's own code doing every
upload, and the picture is read back out of the PPU that drew it. `dream --scene-verify`
is the gate on that: a composed screen and a frame of the running game, compared pixel
for pixel with sprites masked.

The app's own lettering is the ROM's too — the 96-glyph 2bpp font at `014FE0` that no
code in the game ever uploads. There is no other font in the binary: nothing is drawn
before the ROM is read and its SHA-1 checked.

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
    ./build/recomp/dream_harness --lockstep --no-cpu --frames 600 \
        --input recomp/harness/inputs/title_start_right.txt

Build, CLI, the hook API (`recomp/include/snes_state.h`) and the lockstep protocol
are documented in `recomp/README.md`.

## Native app

`recomp/app/` holds `dream`: the game itself, an SDL3 program that loads the
user's own ROM (command line, `./baserom/DREAM.sfc`, `~/.local/share/dream/`, in
that order; on Windows, next to `dream.exe` and then `%APPDATA%\dream\` first;
refused unless the SHA-1 matches), installs every routine the recomp
has registered, and runs at 60.0988 Hz with picture, sound and a gamepad. No
launcher, no menu, no settings, no config file: the mapping above is compiled in,
and Escape quits.

    make app        # builds SDL3 into build/sdl3 first if the system has none
    make app-win    # the same two executables for Windows, cross-built with mingw-w64

`dream` and `dream_harness` are deliberately the same machine: the same core, the
same hook dispatcher, the same accessors and the same cycle charge, with SDL as
the only addition. Two hidden flags keep that checkable: `--frames N --input
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

Routine bodies are transliterations: the same branches in the same
order, the same flag side effects, and the same bus accesses in the same order,
because this ROM turns out to observe its own timing in two places (the SPC upload
handshake counts words against the APU's clock, and the joypad wait loop reads the
hblank flag). `recomp/src/dream_time.h` supplies one helper per 65816 access
pattern for that; `--profile` measures a routine against the ROM's own per-call
cost and reports the two as intervals, so an exact-cost claim is measured rather
than asserted.

A hook is atomic where the routine it replaces is not, so every loop offers the
ROM the chance to take the rest of the routine back (`ss_yield_wanted`) at the top
of each iteration. Without that, a several-thousand-cycle routine would run past a
frame boundary or an interrupt that lands inside it.

## Running without the CPUs

The end state of the port is that the C *is* the program, and `--no-cpu` is that
state made runnable and checkable. In it neither emulated processor executes an
instruction: a scheduler starts at the reset body and follows every pc the machine
hands over (a tail `jmp`, a return, a callee frame, interrupt entry, the
resumption of a routine that stopped at a frame boundary) by looking up the body
that owns the address in the registry. The PPU, DMA and HDMA, the DSP, the APU
timers and the port handshake keep running out of the vendored core, driven by the
cycles the bodies already charge, so the frame timing is identical rather than
close: every gate script passes `--lockstep` against full emulation at +0 master
cycles and +0 APU cycles with 0 instructions executed.

Two consequences shape it. A pc with no body is a fatal error naming the pc and the
body that handed it over, so the mode doubles as the port's dead-code check: it cannot
run at all until every address the program reaches has a body, and where the program
jumps into the middle of a routine that address needs a registry row and a body that
can start there. And a body cannot be handed back to the ROM half-finished, because
there is no ROM: a dispatched body chain runs on a stack of its own and a yield
suspends that stack instead of unwinding it, so the scheduler stops exactly where
the reference CPU stops and resumes the routine from inside the yield.

That stack is the port's one platform split: `recomp/harness/coro.h` is create,
switch and destroy with an explicit stack size, implemented over
`makecontext`/`swapcontext` on POSIX and Win32 fibers on Windows, chosen by CMake
and exercised without a ROM by `dream_harness --test-coro`.

`recomp/README.md` ("Running without the CPUs") documents the scheduler, the
interrupt rules, the SPC700 side and the one exception: the SPC700's IPL boot ROM.
It is the console's firmware rather than this ROM's program, has no body, and still
executes on the core at power-on.

## The sound driver

The ROM's other program runs on the APU: 3514 bytes of SPC700 code, uploaded at
boot and polling its command port for the rest of the session (`spc/driver.asm`,
`spc/spc_map.txt`). `recomp/spc/` is its half of the port, written against
`recomp/include/spc_state.h` and dispatched from a hook in the vendored SPC700's
instruction loop, exactly as `recomp/src/` is on the 65816 side.

Two properties of that side differ. There is no cycle charge to calibrate: an APU
cycle is spent by exactly one read, write or idle, so a body that replays a
routine's access sequence costs what the routine cost by construction, and
`dream_harness --test-spc-timing` checks the per-opcode figures against the core.
And the yield boundary is the APU catch-up slice rather than the frame: the SPC
runs in steps against the CPU's clock, so the reference stops mid-routine at an
instant a hooked run would otherwise have to run past.

`config/recomp.txt` lists what has passed; `make recomp-check` is the gate,
`make recomp-check-units` the routine-level one for what no script reaches, and
`tools/hooks/pre-commit` runs the first when a commit touches `recomp/`.
