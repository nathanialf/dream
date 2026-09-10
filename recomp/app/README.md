# `dream` — the native application

One window, one game. `dream` loads your own `DREAM.sfc`, installs every routine
the recomp has converted so far (`recomp/src`), and runs the reference core at the
SNES's own 60.0988 Hz with picture, sound and a modern controller.

There is no launcher and no config file: the controller mapping in
[docs/RECOMP.md](../../docs/RECOMP.md) is fixed, and `main.c` is where it lives. The
app's own UI is one desktop-style menu bar across the top of the window — **File**,
**View**, **Gallery** — drawn by the app itself, since SDL3 has no native menus. The
game boots straight into play underneath it; nothing the bar offers is persisted, and
the window's current size is the only thing the app remembers between actions.

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
`./baserom/DREAM.sfc`, then `~/.local/share/dream/DREAM.sfc`. It must be the
prototype this port was verified against — SHA-1
`2675d7afe886f20462337aa1ee3aa5c3135fff3a`, 2 MiB. Anything else is refused with
one line naming both hashes; no ROM ships with this repository and none ever will
(see [docs/LEGAL.md](../../docs/LEGAL.md)).

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
| Gallery | the seven pages below, and Close gallery |

The View items change nothing but the window: the machine is not told about them and
does not run differently at 4x than at 1x. With a gallery page open the bar grows a
**Back** item, which closes it exactly as `Escape` and the `A` button do.

## Presentation

- **Picture.** The core renders 512x480 — two subpixels per dot, two fields per
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

Everything on a page is decoded from *your* ROM at run time, with the same formats
[`tools/assetcodec.py`](../../tools/assetcodec.py) implements — 4bpp/2bpp/8bpp tile
planes, 15-bit BGR palette words, the live and alternate sprite-frame assemblies with
their tile spill strips, the BRR filter and shift rules. What exists and where it
lives comes from the committed manifest [`config/assets.txt`](../../config/assets.txt)
through a table `gen_gallery_table.py` generates at cmake time: offsets, sizes, kinds,
paths and the manifest's own notes, and never a byte of the ROM
([docs/LEGAL.md](../../docs/LEGAL.md)). "Unused" is derived there too, from the kinds
that are unreferenced by construction (`stale`, `filler`, `unknown`, `sprite_frame_alt`),
from notes that say so, and from bank `$C1`'s font and picture strips, which
[docs/data_formats.md](../../docs/data_formats.md) lists as referenced by nothing.

### Controls on a page

The fixed mapping and nothing else: **d-pad or left stick** moves (left/right walks
the list, up/down works the palette picker where a page has one), **L/R** pages or
jumps, **B** activates (plays a sample, a song, a sound effect), **A** closes the
page. `Escape`, the bar's **Back** item and **Gallery > Close gallery** do the same
as `A`. Every page prints its own line of controls along the bottom.

### The pages

- **Sprite frames (live)** — all 1555 frames the frame table at `040000` points at,
  assembled the way `sub_C0A538` reads them: 16x16 sprites at their OAM positions,
  the tiles that cannot be placed uniquely spilled into a strip below, header fields
  shown as text, and a palette row picked from the main palette block at `046C48`
  (the title block and the cycling ramp are on the same picker).
- **Sprite frames (alternate)** — the 113 frames plus the ROM's truncated tail frame
  in the second format nothing in the ROM reads, with their 8-byte header as hex and
  their `{x, y, attr}` records counted. The layout is the documented guess
  (docs/data_formats.md 1c): one tile per record, in order, the rest spilled — which
  is why most of a frame ends up in the spill strip.
- **Backgrounds** — every tileset asset, drawn as a tile grid with the palette picker
  and paged with L/R. This is where the four scenes' BG1/BG2 tilesets live, and also
  the unreferenced tile blobs the classifier could only call "tile-like".
- **Fonts and picture strips** — the 2bpp font at `014FE0`, all 96 glyphs of ASCII
  `$20-$7F`, which no code ever uploads; the page also renders a line of text with it
  to show that it is a font. Then the three bank `$C1` picture strips, each drawn
  through its own 32x4 tilemap with its tileset below. Their tile numbers are
  VRAM-relative and start at `$44`, which is read off the maps rather than derived —
  no code loads these, so there is no upload to take a base from; at that base they
  resolve into legible word art, at base 0 into noise. What they say is a surprise:
  strip 1 reads `STRIKE`, strip 2 `OUT`, strip 3 runs through `PITCH` and `HIT B` —
  baseball captions, in a giant-and-bear platformer that has no such screen.
- **Previous build** — the older assembly of the game in the first 32 KB: its 4bpp
  tileset with its *own* palette block at `007AC8` on the picker (508 colours, 32
  rows), that palette as swatches, and its animation-script table at `003000` as a
  record listing, 8 bytes to a line.
- **Music and sound effects** — the eight song slots and both sound-effect banks,
  playable. See below.
- **Samples (BRR)** — all 51 sample records, decoded to PCM on selection with the
  waveform drawn under the list, `B` plays one through the app's audio stream. The
  four no song's sample list mentions (0, 27, 28, 40) are marked `unused`; that is
  computed by walking the song table's sample lists in the ROM, not hard-coded.
- **Stale duplicates** — the 14 stale regions as text, each with the live region its
  note says it shadows.

### Playing music and sound effects

A song is not a file that can be played: it is a command to the sound driver, and the
driver only exists once the 65816 has uploaded it. So the Music page gets a machine of
its own — a second core, the same ROM, booted from reset until `spc_init` has uploaded
the loader and the driver — and asks *it* for a song through the routines the game
calls: `spc_command` with A = the song number, `sfx_command_dispatch` with A = the
16-bit sound-effect command, over the same emulated APU ports and the same block
uploads. Its DSP output is what you hear, and the machine is destroyed when the page
closes. The game's own machine is not involved at any point; it stays paused, byte for
byte.

Two things the page reports because playing them makes it plain:

- The empty song slots (3-7, the ROM's own 4-byte blocks) are listed but not playable.
  Sent through `spc_command`, the upload path does not come back: with no words to
  send the driver never reaches the state the next handshake waits for. The game never
  asks for them either.
- Of bank 1's 21 sound effects, ids `$00`, `$01`, `$13` and `$14` are triggered by
  nothing — no `anim_cb_sfx_*` callback in
  [docs/handler_tables.md](../../docs/handler_tables.md), not `play_footstep_sound`,
  not `play_zone_transition_sound`, not `anim_cb_hit_enemies` — and neither is bank 2's
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

Three more exist for the gallery, and are equally not features:

    --screenshot FILE       write the 256x224 framebuffer as a binary PPM at exit
    --gallery SEC[:NAV]     open a gallery page first (sprites, alt, backgrounds,
                            fonts, prev, music, samples, stale), then apply NAV: one
                            press per character — u d l r for the d-pad, p/n for L/R,
                            b and a for the buttons
    --gallery-toggle N,IT   at frame N open the gallery, spend IT iterations walking
                            every section, close it, carry on

The frame line is byte-identical to the one `dream_harness` prints, so:

    SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy \
      ./build/recomp/dream --frames 600 --input recomp/harness/inputs/title_start_right.txt
    ./build/recomp/dream_harness --hooks on --frames 600 \
      --input recomp/harness/inputs/title_start_right.txt | grep '^frame' | tail -1

must agree on every field. `--frames` still opens the window, draws every frame
and fills the audio stream — only the 60.0988 Hz pacing is dropped — so the
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
