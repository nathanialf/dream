# `dream`: the native application

One window, one game. `dream` loads your own `DREAM.sfc`, installs every routine
the recomp has converted so far (`recomp/src`), and runs the reference core at the
SNES's own 60.0988 Hz with picture, sound and a modern controller.

There is no launcher and no config file: the controller mapping in
[docs/RECOMP.md](../../docs/RECOMP.md) is fixed, and `main.c` is where it lives. The
app's own UI is one desktop-style menu bar across the top of the window (**File**,
**View**, **Gallery**), drawn by the app itself, since SDL3 has no native menus. The
game boots straight into play underneath it; nothing the bar offers is persisted, and
the window's current size is all the app remembers between actions.

`dream` and `dream_harness` are the same machine. They share the core, the hook
dispatcher, the accessors in `harness/snes_state.c` and the cycle charge
calibrated into `config/recomp_cycles.txt`; the app adds SDL and nothing else. The
two hidden flags below exist to keep that claim checkable.

## Build

    make app

That builds into `build/recomp/dream`. It uses a system SDL3 when cmake can find
one; otherwise it clones SDL `release-3.2.x` into `build/sdl3-src`, builds it
static into `build/sdl3`, and points cmake at that. Nothing is installed system
wide and nothing outside `build/` is written.

By hand, with an SDL3 you already have:

    cmake -S recomp -B build/recomp -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH=/path/to/sdl3
    cmake --build build/recomp -j --target dream

The app target is optional: `find_package(SDL3 CONFIG)` failing is not an error,
it just leaves `dream_harness` as the only executable. A machine with no SDL, no
X and no sudo still gets the harness, which is the arrangement this repository's
own verification box runs under.

## Run

    ./build/recomp/dream [path/to/DREAM.sfc]

The ROM is looked for in this order: the path on the command line, then
`./baserom/DREAM.sfc`, then `~/.local/share/dream/DREAM.sfc` (on Windows, see
below). It must be the
prototype this port was verified against: SHA-1
`2675d7afe886f20462337aa1ee3aa5c3135fff3a`, 2 MiB. Anything else is refused with
one line naming both hashes; no ROM ships with this repository and none ever will
(see [docs/LEGAL.md](../../docs/LEGAL.md)).

## Windows

The release zips at
[github.com/nathanialf/dream/releases](https://github.com/nathanialf/dream/releases)
carry `dream.exe`, the README and the LICENSE, and nothing else: no ROM, no data
([docs/LEGAL.md](../../docs/LEGAL.md)). The executable is self-contained: it
imports only Windows' own DLLs (SDL3 is linked statically, and so are libgcc and
libwinpthread), so there is no runtime to install and nothing to unpack beside it.

**Where to put the ROM.** Unzip anywhere and put your own `DREAM.sfc` next to
`dream.exe`. The search order on Windows is:

1. the path on the command line,
2. `DREAM.sfc` in the directory `dream.exe` is in,
3. `%APPDATA%\dream\DREAM.sfc`: for an unpack in a read-only place, or to keep
   one copy for several builds,
4. `baserom\DREAM.sfc` relative to the current directory (a checkout).

It must be the prototype this port was verified against, SHA-1
`2675d7afe886f20462337aa1ee3aa5c3135fff3a`, 2 MiB; anything else is refused with
one line naming both hashes.

**How to run it.** Double-click `dream.exe`, or from a terminal:

    dream.exe                      # ROM next to the exe or in %APPDATA%\dream
    dream.exe C:\path\to\DREAM.sfc
    dream.exe --frames 600 --input inputs\title_start_right.txt

`dream.exe` is a console-subsystem program, so a console window accompanies the
game window and the hidden `--frames` mode prints the same frame line it prints on
Linux, into the terminal it was started from. That comparison is the point of the
flag (see "Checking it against the harness" below) and it is worth more than the
tidier windowed subsystem would be.

The controls, the menu bar and the gallery are the same as everywhere else.

### Building it yourself

Two supported ways, both MinGW. MSVC is not: every `recomp/src` file registers its
routines from a file-scope constructor (`RECOMP_REGISTER`,
[`recomp/include/snes_state.h`](../include/snes_state.h)), which needs
`__attribute__((constructor))`: GCC, clang and MinGW have it, MSVC does not, and
the header stops the build with an `#error` rather than linking an empty registry
and reporting every routine as missing. Adding the MSVC `.CRT$XCU` section trick to
that one macro is all it would take, if someone wants it.

**On Windows, MSYS2/UCRT64** (what the CI and release workflows use):

    pacman -S mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-cmake \
              mingw-w64-ucrt-x86_64-ninja
    cmake -S recomp -B build/recomp -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DDREAM_FETCH_SDL3=ON -DCMAKE_EXE_LINKER_FLAGS=-static
    cmake --build build/recomp -j

`-DDREAM_FETCH_SDL3=ON` has cmake fetch and build SDL3 (a pinned 3.2.x release)
itself, so there is no separate SDL step; `-static` is what keeps `libgcc_s_seh-1.dll`
and `libwinpthread-1.dll` out of the imports.

**Cross-built from Linux with mingw-w64:**

    make app-win

which builds SDL3 for the target into `build/sdl3-win` (static, the same options
`make sdl3` uses), configures `recomp/` with
[`recomp/cmake/mingw-w64.cmake`](../cmake/mingw-w64.cmake), and writes
`build/win/dream.exe` and `build/win/dream_harness.exe`. It finishes by listing
what the two executables import and failing if any of it is not a Windows system
DLL. `make win-dlls` on its own repeats that check. As built here they import:

    dream.exe          ADVAPI32 GDI32 IMM32 KERNEL32 OLEAUT32 SETUPAPI SHELL32
                       USER32 VERSION WINMM msvcrt ole32
    dream_harness.exe  KERNEL32 msvcrt

### When it crashes: `dream.log`

Every run writes a log beside the executable: `dream.log` in the directory
`dream.exe` is in on Windows, in the working directory everywhere else. It is
opened before anything else in `main()`, one timestamped line a stage, flushed
to disk (and `FlushFileBuffers`'d on Windows) as each line is written, because
the line that matters is always the last one before the fault:

    2026-09-10 19:47:19.448    +0.000  dream: log opened at dream.log
    2026-09-10 19:47:19.448    +0.000  crash handlers installed (signals)
    2026-09-10 19:47:19.448    +0.000  argv: 5 argument(s)
    ...
    2026-09-10 19:47:19.468    +0.020  rom: sha1 2675d7... (expected 2675d7...): match
    2026-09-10 19:47:19.474    +0.026  run: entering the frame loop (--frames, unpaced)
    2026-09-10 19:47:19.477    +0.029  frame 1: wram=e12ecc387a955073

Wall clock, then seconds since the log was opened, then the line. What is
recorded: the arguments, every ROM path tried and what was there, the ROM's size
and SHA-1, the SDL version and the video and audio driver SDL chose, the window,
renderer, texture and audio device (with `SDL_GetError()` on a failure), the
gamepads, the two hook table sizes, the coroutine backend, the first frame and
then every 300th with its WRAM hash, every gallery open and close, and the exit.
Nothing goes to stdout that did not go there before: the frame line `--frames`
prints stays byte-identical to `dream_harness`'s, which is the point of the flag.

**A fault writes itself down.** On Windows the app installs a
`SetUnhandledExceptionFilter` that logs the exception code and name, the faulting
address, the module's load address and the base it was *linked* at, the fault
address as `dream+RVA` and as the address to look up in the map file, the current
fiber (so a fault on a coroutine stack says so), the last stage logged, and a
stack walk if `RtlCaptureStackBackTrace` is there: addresses only, since
`dbghelp.dll` is not among the DLLs this executable is allowed to import. Then it
flushes, closes the log and leaves with exit code **86**. `SIGSEGV`, `SIGABRT`,
`SIGILL` and `SIGFPE` are caught on every platform and leave with **87**.
`SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX)` is set as well, so
a crash under a script or a shortcut falls over and is gone instead of waiting on
a dialog nobody is looking at.

**Reading an address back.** `make app-win` builds with `-g` and writes
`build/win/dream.map` next to `build/win/dream.exe`. The log already does the
arithmetic: `dream+0x2a1b40  (map 0x1402a2b40)`, so the second number is the one
to search for in the map, which lists every symbol at its linked address. (ASLR
means the load address differs every run, which is why the offset and not the raw
address is what identifies the code.) The executable is not stripped either, so
the same number goes straight into `addr2line` against the build the log came
from, which gives the line as well as the symbol:

    x86_64-w64-mingw32-addr2line -f -e build/win/dream.exe 0x1402a2b40

### The coroutine backend

The one part of the port with no portable spelling is the stack the `--no-cpu`
scheduler runs a body chain on. It lives behind
[`recomp/harness/coro.h`](../harness/coro.h) (create, switch and destroy with an
explicit stack size) with `coro_ucontext.c` (`makecontext`/`swapcontext`) on POSIX
and `coro_fibers.c` (`ConvertThreadToFiber`/`CreateFiber`/`SwitchToFiber`/`DeleteFiber`)
on Windows, chosen by CMake. `dream_harness --test-coro` exercises whichever one was
built (nesting, a yield across a frame boundary, tearing down a suspended
coroutine) and needs no ROM, which is how the fiber backend is checked on a real
Windows runner in CI while the ROM stays out of it.

One case that test does *not* cover is `coro_start` on a coroutine that is
suspended rather than finished, which is what the scheduler does when it reuses
the slot of a body chain the NMI handler displaced (`ss_nocpu_reap`). `makecontext`
gives the POSIX backend a fresh stack every start and so cannot get it wrong;
`SwitchToFiber` has no such step, so `coro_fibers.c` deletes a parked fiber and
makes a new one at `coro_start` and only reuses a finished one. A test for it
belongs in `--test-coro`.

## Controls

The first connected gamepad is player 1; a second connected gamepad is player 2,
with the same fixed mapping. Plugging pads in or pulling them out is handled
silently while the game runs; if a pad goes away and another is still connected,
that one takes over its slot. The keyboard is always live alongside player 1;
there is no keyboard fallback for player 2 (no UI to configure one).

| SNES   | Gamepad (SDL standard layout) | Keyboard      |
|--------|-------------------------------|---------------|
| B      | south face button             | `Z`           |
| A      | east face button              | `X`           |
| Y      | west face button              | `A`           |
| X      | north face button             | `S`           |
| L      | left shoulder                 | `Q`           |
| R      | right shoulder                | `W`           |
| Start  | start                         | `Enter`       |
| Select | back / select                 | `Shift`       |
| d-pad  | d-pad, and left stick past half deflection | arrow keys |

`Escape` closes an open gallery page, and quits when there is none.

The menu bar takes the mouse (click a title, click an item) and, as a fallback, the
keyboard: `Alt` or `F10` focuses it, left/right move between menus, up/down between
items, `Enter` picks one and `Escape` closes it. While the bar has the keyboard the
game does not see it, so a menu cannot be walked and played at the same time.

| Menu | Items |
|------|-------|
| File | Quit |
| View | Scale 1x/2x/3x/4x (the window resizes to fit), Fit to window (integer), Aspect 8:7 (square pixels) or 4:3, Fullscreen |
| Gallery | the nine pages below, and Close gallery |

The View items change nothing but the window: the machine is not told about them and
does not run differently at 4x than at 1x. With a gallery page open the bar grows a
**Back** item, which closes it exactly as `Escape` and the `A` button do.

## Presentation

- **Lettering.** Every character the app draws (the menu bar, the gallery's
  headings, its labels and its status lines) is the ROM's own font: the 96 2bpp
  glyphs at file offset `014FE0` that no code in the game ever uploads
  ([docs/data_formats.md](../../docs/data_formats.md); the gallery has a page for
  them for exactly that reason). `romfont.c` decodes them out of the loaded image
  at startup and the menu bar puts the whole set into one texture; spacing is
  proportional, because the glyphs sit inside their 8-pixel cell with their own
  margins and a fixed cell would be a third wider than the layout. There is no
  second font and no fallback: the app draws nothing at all before the ROM is
  read and its SHA-1 checked, and refuses to start without one.
- **Picture.** The core renders 512x480: two subpixels per dot, two fields per
  line. The app takes the signal's own 256x224 out of that (row *y* of the picture
  sits at 2*y*+16) into a 256x224 framebuffer of its own, which is also what the
  gallery draws into, and hands SDL that as a nearest-neighbour texture. The
  viewport is the area below the menu bar: by default the largest whole multiple of
  256x224 that fits in it, centred, so a pixel stays a square block of pixels at
  every window size and resizing never produces a half-scaled row. (The bar is drawn
  at the window's own resolution above it, which is why the app computes that rect
  itself instead of using `SDL_SetRenderLogicalPresentation`.)

  This is the one place the app makes a choice the design note leaves open.
  256x224 scaled by a whole number is 8:7, not the 4:3 a CRT of the period
  stretched it to, and no presentation mode can be both: 4:3 needs a horizontal
  scale of 4/3 x 224/256 = 1.166..., which is not an integer and never will be. A
  whole-numbered pixel grid was taken as the stronger of the two requirements,
  because it is the one the eye notices when it is broken. **View > Aspect 4:3** is
  there for anyone who disagrees; it stretches the same picture and interpolates
  nothing else.
- **Sound.** LakeSnes' DSP output, 534 stereo samples a frame, put into an
  `SDL_AudioStream` declared at 534 x 60.0988 = 32093 Hz and resampled by SDL to
  whatever the device wants. The sample count is nudged by about 1% when the queue
  drifts a frame either side of its three-frame target, which is what keeps
  latency bounded on a machine whose audio clock and 60.0988 Hz disagree;
  `dsp_getSamples()` resamples to whatever it is asked for, so the cost is a pitch
  shift far below hearing. No audio device is not a reason to refuse to play: the
  game runs silent.
- **Rate.** Paced off `SDL_GetTicksNS`, not the display, because the game's rate
  is 60.0988 Hz whatever the monitor runs at. A frame more than eight frames late
  abandons the backlog rather than sprinting through it.

## Gallery

A viewer for what the ROM holds and the game never puts on screen, opened from the
**Gallery** menu. It is not a settings screen and not a launcher: opening a page
pauses the game and draws the page into the same 256x224 framebuffer; closing it
resumes exactly where it stopped. Nothing is saved anywhere.

Most of the pages decode *your* ROM at run time, with the same formats
[`tools/assetcodec.py`](../../tools/assetcodec.py) implements: 4bpp/2bpp/8bpp tile
planes, 15-bit BGR palette words, the alternate sprite-frame assembly with its tile
spill strip, the BRR filter and shift rules. What exists and where it
lives comes from the committed manifest [`config/assets.txt`](../../config/assets.txt)
through a table `gen_gallery_table.py` generates at cmake time: offsets, sizes, kinds,
paths and the manifest's own notes, and never a byte of the ROM
([docs/LEGAL.md](../../docs/LEGAL.md)). "Unused" is derived there too, from the kinds
that are unreferenced by construction (`stale`, `filler`, `unknown`, `sprite_frame_alt`),
from notes that say so, and from bank `$C1`'s font and picture strips, which
[docs/data_formats.md](../../docs/data_formats.md) lists as referenced by nothing.

Two pages decode nothing at all. **Sprite frames (live)** makes the game draw the frame
on a scratch machine and reads the picture back (see "Sprite frames, drawn by the game"
below), and **Scenes** does the same for a level. A level is not a file: it is several
tilesets at different VRAM addresses, a metatile map the blitter turns into tilemap
columns one column a frame as the camera moves, a second static tilemap on another
layer, a palette row picked per tilemap word, and a scroll register per layer written
by the mode's own NMI handler. Nothing assembled by hand can be trusted to agree with
that, so the page does not assemble anything: it boots a second machine (the trick
`music.c` already uses for the sound driver), walks that machine's camera across the
level with the game's own code doing every upload, and reads the picture back out of
the PPU that drew it. See [`scene.h`](scene.h) and "Scenes" below.

### Colour

Pages are drawn in the colours the game gives them, not in a colour the viewer guesses
at. [docs/data_formats.md](../../docs/data_formats.md)'s "Palette assignment" section is
the derivation; the generated table carries its addresses and counts (which palette bytes
each scene's init DMAs to which CGRAM entries, which tileset the PPU registers make which
BG of which scene, and where the entity/animation/frame tables live) and the app reads the
bytes at those addresses out of your ROM:

- a **background** page rebuilds what its scene's init would produce: the whole
  256-entry CGRAM (the same DMAs in the same order, so a later narrow upload overwrites
  an earlier wide one exactly as it does in the game) and the whole 32 KB of VRAM (the
  same uploads in the same order, from `kGalleryVramUploads`, read off the mode-init
  bodies the same way). VRAM is what the page shows by default, because a tileset on its
  own is not what the game draws: several sets land at different addresses and the maps
  index tiles across them from a character base, so a map word only means what it says
  once every set is where the init put it. Each tile is then drawn through the palette
  row the scene's own maps reference *that tile* with (voted per tile rather than per
  set), and the page says how many tiles on the page any map names at all. The old view
  is still there on **B**: one manifest asset, in file order, through the one row most of
  its tiles are referenced with. The title's tiles are 8bpp in BGMODE 3, where the pixel
  byte *is* the CGRAM index, so all 256 title colours apply at once and no row is
  involved.
- a **sprite frame** page has three sources for its palette and says which it is using.
  **Observed**: the scene machine runs three of the harness's own input scripts and
  records, for every frame id an entity is showing while that entity's OAM attribute
  byte is in the OAM the PPU just drew from, the palette bits that byte carries. That is
  a measurement, not a second derivation. **Derived**: the walk over entity init records
  and animation scripts that
  [docs/data_formats.md](../../docs/data_formats.md) 1d sets out, which reaches frames no
  script ever plays but can only say what the tables allow. **Guess**: for a frame no
  entity in any scene plays, the eight OBJ palettes of `game_mode` 0, labelled as
  guesses. Observed pairs come first and are labelled `OBSERVED`; the derived ones
  follow, labelled `DERIVED`; the guesses come last, labelled `GUESS`. Up and down cycle
  them and the page names the entity types each belongs to. Over the 1555 live frames the
  three scripts leave **194 observed, 869 derived only and 492 with neither**: the 492
  are the frames no animation any of the four scenes' entities can reach, and they say so
  instead of pretending. What a candidate carries is the whole `entity_flags` word, not
  just its palette bits: the OBJ tile slot the frame's tiles are DMA'd to and the
  priority come with it, because a forced render hands the entity that word back. An
  alternate-format frame is played by nothing at all, so it falls back to the palette its
  own `{x, y, attr}` records carry.
- the **picker is still there**, on up/down past the end of the derived list, and every
  page labels it `OVERRIDE picker` and says it is not the palette the game uses. It is
  the only colour source on the pages that have no other: the unreferenced font and
  picture strips, the previous build's tiles, and the tile blobs no scene uploads.

### Controls on a page

The fixed mapping and nothing else: **d-pad or left stick** moves (left/right walks
the list, up/down walks the palettes a page's current item can be drawn with and then
the override picker), **L/R** pages or jumps, **B** activates (plays a sample, a song,
a sound effect) or switches a page's sub-view (Backgrounds: VRAM, the raw set or the
metatiles; Sprite frames: the game's own render or the file layout; Scenes: which
layers), **A** closes the page. **Y** walks the four flip combinations on the sprite
page and zooms on Scenes. On **Scenes** the d-pad scrolls the composed level instead of
walking a list and **L/R** picks the scene. Moving to a new item returns to that item's own
palette: the override is a deliberate act, not a mode. `Escape`, the bar's **Back** item and **Gallery > Close gallery** do the same
as `A`. Every page prints its own line of controls along the bottom.

### The pages

- **Scenes**: each `game_mode`'s whole level, and the title screen, composed out of
  the emulator's own PPU. See "Scenes" below for how, and for what the composed image
  is and is not.
- **Sprite frames (live)**: all 1555 frames the frame table at `040000` points at,
  each drawn by the game itself. The page assembles nothing: it puts the frame on one
  entity of a scratch machine already inside a scene and reads the picture out of the
  PPU that drew it. See "Sprite frames, drawn by the game" below. **B** switches to the
  file layout, which is the old reconstruction: 16x16 sprites at their OAM positions,
  the tiles that cannot be placed uniquely spilled into a strip below. **Y** walks the
  four flip combinations, which are four copies of the emit loop in the ROM and so four
  pictures the game itself can draw.
- **Sprite frames (alternate)**: the 113 frames plus the ROM's truncated tail frame
  in the second format nothing in the ROM reads, with their 8-byte header as hex and
  their `{x, y, attr}` records counted, drawn with the palette those records' own
  attribute bytes ask for. The game has no code path for them, so this page keeps the
  reconstruction: the layout is the documented guess (docs/data_formats.md 1c), one tile
  per record, in order, the rest spilled, which is why most of a frame ends up in the
  spill strip. What can be checked is the art. Every live frame's tiles go into one
  table, 32 bytes to a tile, and every alternate frame's tiles are looked up in it.
  **No alternate frame shares more than two tiles with any live frame**, so there is no
  live frame whose placement and palette could be borrowed, and the page says so per
  frame. The alternate format is separate art, not the live art in another container.
- **Backgrounds**: three views, on **B**. VRAM as a scene's init leaves it is the
  default: all 32 KB, drawn as a tile grid at its VRAM addresses through the scene's
  real CGRAM, with the palette row voted per tile out of the scene's own maps.
  Left/right picks the scene, L/R pages, and the page opens on the page the scene
  uploaded most of. The second view is the metatiles, which is how the art was composed:
  every 32x32 metatile of the scene's table drawn once, in index order, each of its
  sixteen tilemap words through the palette row, priority and flips that word carries.
  A level map is a grid of those. The third is the raw-set view (one manifest asset in
  file order through one row), which is where the unreferenced tile blobs the classifier
  could only call "tile-like" live, since no scene uploads them and they have only the
  override picker. A tileset in file order is unaligned art by definition, which is why
  it is last.
- **Fonts and picture strips**: the 2bpp font at `014FE0`, all 96 glyphs of ASCII
  `$20-$7F`, which no code ever uploads and which the app itself is lettered with; the
  page also renders a line of text with it to show that it is a font. Then the three
  bank `$C1` picture strips, each drawn through its own 32x4 tilemap with its tileset
  below.

  Their tile numbers are VRAM-relative and nothing uploads them, so the base has to
  come out of the bytes. Each map pads with one index over and over (all three pad with
  `$044`) and its content runs upward from just above it, so: if the set holds an
  all-zero tile, that is what the pad means and the base is `pad - (that tile's index)`;
  if it holds none, the pad is below the set and the base is the lowest content
  index, which puts the first content tile at tile 0. That gives **`$44`, `$50` and
  `$43`** (a different base per strip, not the one base for all three the earlier
  reading assumed), and with them **no word in any of the three maps lands outside its
  own set**, so nothing spills into the next. The page prints the base, the pad and the
  count of words outside the set (zero for all three).

  What they say, at those bases: strip 1 reads `STRIKE 1`, strip 2 `TIME` … `OUT`, and
  strip 3 `HIT BY` … `PITCH`: baseball captions, in a giant-and-bear platformer that
  has no such screen. Every all-zero tile in a set starts another block of the same
  shape (strip 1 has four, 312 tiles for a 70-tile caption; strip 3 has three), and
  re-basing the same map onto strip 1's second block reads as `STRIKE 2`. The later
  blocks want maps of their own, which are not in the ROM, so they come out mis-tiled;
  **L/R** walks them and the page says so.

  Palette: the maps' words all ask for palette 7 and nothing uploads one, so the page
  ranks the ROM's own 16-colour rows by how well they suit the art (colour 0 dark and
  the other fifteen a monotone ramp, which is what a metallic caption needs) and offers
  the winner as a **guess**, labelled `BEST GUESS`, never as the palette the game uses.
  Row 7 of the main palette block at `046C48` wins: it is the row the words name, and it
  is a clean fifteen-step gold ramp. Up and down cycle the runners-up (the title block's
  row 0, the previous build's row 30, three scene rows) and then the ordinary override
  picker. The font is 2bpp and only pixel values 0 and 1 ever occur in it, so what
  matters there is entries 0 and 1: the title palette's row 0 is black then white, and
  that is the guess it opens on.
- **Previous build**: the older assembly of the game in the first 32 KB: its 4bpp
  tileset with its *own* palette block at `007AC8` on the picker (508 colours, 32
  rows), that palette as swatches, and its animation-script table at `003000` as a
  record listing, 8 bytes to a line.
- **Music and sound effects**: the eight song slots and both sound-effect banks,
  playable. See below.
- **Samples (BRR)**: all 51 sample records, decoded to PCM on selection with the
  waveform drawn under the list, `B` plays one through the app's audio stream. The
  four no song's sample list mentions (0, 27, 28, 40) are marked `unused`; that is
  computed by walking the song table's sample lists in the ROM, not hard-coded.
- **Stale duplicates**: the 14 stale regions as text, each with the live region its
  note says it shadows.

### Scenes

The level pages are the one part of the gallery that decodes nothing. `scene.c` boots a
second machine (the same core, the same ROM, from reset, exactly as `music.c` does for
the sound driver) and replays as much of
[`recomp/harness/inputs/mode_cycle.txt`](../harness/inputs/mode_cycle.txt) as it takes
to enter the scene (Start, then one Select per mode) and then holds no button at all,
because that script keeps advancing the mode every 120 frames and a walk across a level
runs for thousands. From there it walks the camera along the level and reads the picture
back out of the PPU.

**How the camera is driven.** `camera_follow_player` (`$C0:A1B0`) is a clamp and nothing
else (`camera_x = clamp(entity_x[0] - $80, 0, level_width_mask)`, and the same for y
outside `game_mode` 1), so writing the player's position *is* writing the camera's. The
scene machine installs one CPU hook, at that routine's own entry address, which writes
the position the walk wants and then returns false so the ROM's own routine runs and
does the clamping. No instruction is replaced. Everything downstream is the game's:
`build_metatile_column_580` lays down one tilemap column, `vram_upload_column_580` DMAs
it, the mode's NMI handler writes the scroll registers. The walk steps 8 pixels a frame
because that is exactly one column a frame, which is the rate the blitter keeps up with.

**What a screen is.** Every 256 pixels the walk stops the camera for two frames (each
mode's NMI handler writes the scroll registers from the camera the *previous* frame
computed, so a frame taken while the camera is still moving is drawn two columns away
from where `camera_x` says it is), and then renders 224 lines with `ppu_runLine` and
blits the result at the camera it was taken at. Each screen is rendered once per view:
once with every layer the mode's TM enables and once with each of those layers on its
own, so **B** toggles between them without walking the level again.

**What is switched off, and why.** OBJ is off (a level shot with the player standing in
it is not a level shot), forced blank is off and brightness is 15 (a frame captured
mid-fade would otherwise be black), and HDMA is off: every register a channel drives is
put back to the value the mode's init and the mode's scroll handler wrote, rather than the
channel being switched off. That matters: `game_mode` 1's HDMA switches BG1's tilemap halfway down
the screen and `game_mode` 3's rewrites TM, so the value left at the end of a frame is
the last scanline's rather than the scene's. Without the restore, mode 1's level layer
does not appear at all. What HDMA leaves in CGRAM is still there, so mode 1's
`$00-$0F` hold the last scanline's colours
([docs/data_formats.md](../../docs/data_formats.md), "CGRAM after the init").

**What it costs.** One emulated frame per 8 pixels of camera travel, per vertical band:
`game_mode` 0 is 3840x512 and takes 1904 frames and 45 screens, about nine seconds. It
is composed on demand, one scene at a time, in 12-millisecond slices while the page
stays responsive, and cached until another scene is asked for. The page draws a progress
bar and says what the walk is doing.

**Where it can disagree with the game.** The layer the metatile blitter feeds is
continuous across the whole composition, so a crop of it at any camera position is the
frame the game draws there. A parallax layer is not: it moves at its own rate, so it
steps 256 pixels of *its* travel per screen and can jump at a screen boundary. That is
what the layer toggle is for, and `--scene-verify` measures both (below).

### Sprite frames, drawn by the game

The live sprite page used to assemble a picture out of the frame file, and the assembly
was a guess: which tile goes where, which palette, which spill. It assembles nothing now.
It makes the game draw the frame.

**How.** `scene.c` boots a second machine into a scene the way the Scenes page does, and
from then on writes one entity's record from a hook at `entity_build_oam_frame`'s own
entry address ($C0:A538). Slot 0 gets the frame id asked for, the flag word asked for
(palette, priority, the two flip bits and the OBJ tile slot), and a position taken from
the camera the scene settled at. `entity_count` ($A6) goes to 2, which is how the game
itself spells a roster of one, and the other fifteen slots get the init table's own
terminator type and the zero substate the OAM builder skips on. The player's input is a
held-down nothing: the machine takes no button from the moment it is inside the scene.

`entity_frame_loaded` ($0808) is written as `$FFFF` in the same pass. That is the cell
the builder compares the frame id against, so it always reads as changed and the frame's
tiles are queued into `entity_tile_job` every frame. `entity_upload_pending_tiles` DMAs
that queue at the top of the next NMI, before the builder runs again, so two NMIs after
the first forced record the VRAM holds this frame's tiles and OAM holds its sprites. The
scene which draws it is the one the palette evidence names, and `game_mode` 0 when there
is none.

**Where the sprite lands.** The entity being drawn is the player, so the camera follows
it and its own position cancels out of the screen position. What places the sprite is
the gap between the anchor the camera hook pins the player to and the position the forced
record carries. `entity_build_oam_frame` writes `entity_screen_y` before it culls, and
nothing else in the program writes that cell, so the constant relating the two is
measured rather than predicted. The emitter drops a sprite whose own screen row reaches
`$F0` and the frame's records carry that row, so ten screen rows are tried until one of
them emits something, and then the entity is nudged until its OAM box is centred.

**What is cropped.** Which OAM entries belong to the entity is measured too. A hook at
$A5B9 (the emitter choice, `Y` = the slot) reads `oam_write_ptr` before the emitter runs
and one at $A6BF reads it after, so a particle the mode's own dispatcher emitted is never
inside the crop. The render is `ppu_runLine` over the machine as the frame left it, with
TM set to OBJ alone, forced blank off, brightness 15 and the scene's own registers put
back over whatever HDMA left, exactly as a composed screen is rendered. Every OAM entry
outside the entity's range is pushed to X = 256, which reads as -256 and is past the
widest sprite the PPU has.

Shots are cached by frame, scene and flag word. The first frame asked for in a scene pays
that scene's boot, about a second, sliced by the same job stepper the Scenes page uses;
every one after that costs the handful of NMIs the upload and the OAM build take.

**What it is checked against: `--sprite-verify`.** Two passes.

The first is the gate. The three observation scripts are walked once more and, the first
time a frame id has stood still for two builds, that entity is cropped out of the frame
the game is actually showing. Standing still for two builds matters: the OAM the builder
assembles in frame N is DMA'd by the next frame's scroll handler and the tiles it queues
are uploaded by the next frame's `entity_upload_pending_tiles`, so what the PPU holds at
the end of frame N is the build of frame N-1, tiles and OAM together, and on the frame an
animation changes the game is drawing the previous frame's tiles. Then the same frame id
is forced onto a scratch machine with the same scene and the same flag word, and the two
crops are compared in the entity's own box. As it stands:

    observed frames        194 of 1555 live frames
    crops taken            182
    no crop                 12   the game showed the frame for one build only
    exact, whole sprite    132/182
    exact where comparable  50   the game's own sprite met a screen edge
    differ                   0

The 50 are frames the running game only ever showed part of on screen. The part of the
box both pictures cover is exact for all of them; the part the screen edge cut off is not
there to compare. Nothing differs.

The second pass renders every live frame once, with the scene and flag word the page
would use:

    every live frame      1555 drawn, 0 refused, of 1555
                          194 at an observed palette, 869 derived, 492 a guess

And the alternate-format check, which is the tile-content comparison described under
**Sprite frames (alternate)** above:

    alternate frames        0 of 114 have a nearest live frame
                            the most tiles any one shares with a live frame is 2

### Playing music and sound effects

A song is not a file that can be played: it is a command to the sound driver, and the
driver only exists once the 65816 has uploaded it. So the Music page gets a machine of
its own (a second core, the same ROM, booted from reset until `spc_init` has uploaded
the loader and the driver) and asks *it* for a song through the routines the game
calls: `spc_command` with A = the song number, `sfx_command_dispatch` with A = the
16-bit sound-effect command, over the same emulated APU ports and the same block
uploads. Its DSP output is what you hear, and the machine is destroyed when the page
closes. The game's own machine is not involved at any point; it stays paused, byte for
byte.

Two facts the page reports because playing them makes it plain:

- The empty song slots (3-7, the ROM's own 4-byte blocks) are listed but not playable.
  Sent through `spc_command`, the upload path does not come back: with no words to
  send the driver never reaches the state the next handshake waits for. The game never
  asks for them either.
- Of bank 1's 21 sound effects, ids `$00`, `$01`, `$13` and `$14` are triggered by
  nothing: no `anim_cb_sfx_*` callback in
  [docs/handler_tables.md](../../docs/handler_tables.md), not `play_footstep_sound`,
  not `play_zone_transition_sound`, not `anim_cb_hit_enemies`, and neither is bank 2's
  single effect (`$60`). The page marks them. It also notes that command `$FB`, the
  driver's fade-and-start-song (`cmd3_fade_and_song`), is never sent: the only code
  that would is the dead byte run at `C18403`.

The page is silent until you pick something. The scratch machine is the game booted to
its title screen, so before your first selection it is playing to nobody; from then on
a song replaces what the driver was playing and a sound effect layers over it, exactly
as they do in the game.

### Pausing

While a page is open the emulator is not stepped, is not written to and is not even
handed an input state, and the game's audio stream is cleared, so it falls silent
instead of looping its last buffer. Closing the page resumes the machine on the next
iteration with no state change at all. `--gallery-toggle` exists to prove that: see
below.

## Checking it against the harness

Two flags exist only so this binary can be proved to be the same machine as
`dream_harness`. They are not features.

    --frames N      run N frames, print the harness's frame line, exit
    --input FILE    drive it from a harness input script instead of the pad

More exist for the gallery, and are equally not features:

    --screenshot FILE       write the 256x224 framebuffer as a binary PPM at exit
    --screenshot-ui FILE    write the whole window, menu bar included, as a PPM
    --gallery SEC[:NAV]     open a gallery page first (scenes, sprites, alt,
                            backgrounds, fonts, prev, music, samples, stale), then
                            apply NAV, one press per character: u d l r for the
                            d-pad, p/n for L/R, b x y and a for the buttons
    --gallery-toggle N,IT   at frame N open the gallery, spend IT iterations walking
                            every section, close it, carry on
    --scene-dump M[,V]:F    compose scene M (0-3, 4 = the title) and write view V
                            (0 = every layer, n = BG n alone) as a PPM of its own size
    --scene-probe M:X:Y:V:F one composed screen at camera (X, Y), as a 256x224 PPM
    --scene-verify [DIR]    compose every scene and compare it against the running
                            game, printing the pixel counts; with DIR, write the
                            pictures it compared
    --sprite-pal-report     run the OAM observation pass and print how the 1555 live
                            frames come out: observed, derived only, neither
    --sprite-probe F:M:X:P  draw frame F in game_mode M with entity_flags X (hex)
                            and write the crop as a PPM of its own size
    --sprite-verify         every forced sprite render against the running game,
                            then every live frame once

`--scene-verify` is the gate on the Scenes page. For each scene it composes the level
the way the page does, boots a second machine the ordinary way (the harness script, no
walk, no camera hook) and holds it until its camera has stopped moving, renders that
machine's BG layers through the same PPU with the same masking, and compares that screen
against the composed image cropped at the camera the second machine is at. As it stands:

    game_mode 0    level layer   57344/57344 exact      all layers   one screen exact
    game_mode 1    level layer   57344/57344 exact
    game_mode 2    level layer   57344/57344 exact
    game_mode 3    level layer   57344/57344 exact      all layers   one screen exact
    title screen   all layers    57344/57344 exact

The level layer is exact in every scene, both as a single composed screen and as a crop
out of the stitched image. The all-layers rows differ where the crop straddles two of
the screens the composition took, because the parallax layers step at their own rate
between them; in modes 1 and 2 a single screen differs as well, and the difference is
confined to the band those modes drive with a per-scanline HDMA scroll (mode 1's floor,
mode 2's horizon), which is what "HDMA off" cannot reconstruct from one frame.

`--scene-verify` also checks the Backgrounds page's metatile view against the same
composed scenes. A composed level is the game's own blitter output, so every 32x32 cell
of it should be one of the scene's metatiles as the page draws it. Eight cells a scene,
near the level start, each compared first against the metatile the level map's own word
names and then against every metatile the scene has:

    game_mode 0    8 of 8 cells are exactly a metatile, 8 of them the one the map names
    game_mode 1    8 of 8 cells are exactly a metatile, 7 of them the one the map names
    game_mode 2    8 of 8 cells are exactly a metatile, 8 of them the one the map names
    game_mode 3    8 of 8 cells are exactly a metatile, 8 of them the one the map names

Every cell checked is exactly a metatile the view draws, pixel for pixel, and 31 of the
32 are the one the level map names at that cell. Three things are taken out of the
comparison first. Cells are taken near the level start, because the streaming
descriptors at `$80:B208` re-upload palettes as the camera moves while the page's CGRAM
is the one the init leaves; a cell whose metatile names a CGRAM row the scene's HDMA
rewrites is skipped, which is 21 cells in `game_mode` 1 (docs/data_formats.md, "CGRAM
after the init"); and only the pixels the metatile itself owns are compared, because
where its tile pixel is zero the picture is the backdrop. `game_mode` 0 and 1 write their
level layer's vertical scroll one line off the camera the composition blits at, so the
grid offset is found once per scene from the first cell and held.

The frame line is byte-identical to the one `dream_harness` prints, so:

    SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy \
      ./build/recomp/dream --frames 600 --input recomp/harness/inputs/title_start_right.txt
    ./build/recomp/dream_harness --hooks on --frames 600 \
      --input recomp/harness/inputs/title_start_right.txt | grep '^frame' | tail -1

must agree on every field. `--frames` still opens the window, draws every frame
and fills the audio stream (only the 60.0988 Hz pacing is dropped), so the
comparison covers the platform layer rather than routing around it. The dummy
drivers are what make that work with no display and no sound card.

The same trick proves the gallery changes nothing. A run with the gallery opened
mid-way, walked through every page and closed again has to end on the frame line a run
without it ends on:

    SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy \
      ./build/recomp/dream --frames 400 --input recomp/harness/inputs/level_walk_jump.txt
    SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy \
      ./build/recomp/dream --frames 400 --input recomp/harness/inputs/level_walk_jump.txt \
        --gallery-toggle 150,60

Paused iterations are not frames, so the toggled run still emulates 400 of them; the
WRAM/VRAM/CGRAM/OAM hashes and every register in the line come out identical.
