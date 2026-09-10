# `dream` — the native application

One window, one game. `dream` loads your own `DREAM.sfc`, installs every routine
the recomp has converted so far (`recomp/src`), and runs the reference core at the
SNES's own 60.0988 Hz with picture, sound and a modern controller.

There is no launcher, no menu, no settings screen and no config file: the mapping
in [docs/RECOMP.md](../../docs/RECOMP.md) is fixed, and `main.c` is where it lives.
Escape quits. That is the whole of the user interface.

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

The first connected gamepad is player 1. Plugging one in or pulling it out is
handled silently while the game runs; if player 1's pad goes away and another is
still connected, that one takes over. The keyboard is always live alongside it.

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

`Escape` quits. There is nothing else to press.

## Presentation

- **Picture.** The core renders 512x480 — two subpixels per dot, two fields per
  line. The app takes the signal's own 256x224 out of that (row *y* of the picture
  sits at 2*y*+16) and hands SDL a 256x224 texture. `SDL_SetRenderLogicalPresentation`
  with `INTEGER_SCALE` and a nearest-neighbour texture then puts the largest whole
  multiple of it that fits in the window, centred: a pixel stays a square block of
  pixels at every window size, and resizing never produces a half-scaled row.

  This is the one place the app makes a choice the design note leaves open.
  256x224 scaled by a whole number is 8:7, not the 4:3 a CRT of the period
  stretched it to, and no presentation mode can be both: 4:3 needs a horizontal
  scale of 4/3 x 224/256 = 1.166..., which is not an integer and never will be. A
  whole-numbered pixel grid was taken as the stronger of the two requirements,
  because it is the one the eye notices when it is broken.
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

## Checking it against the harness

Two flags exist only so this binary can be proved to be the same machine as
`dream_harness`. They are not features.

    --frames N      run N frames, print the harness's frame line, exit
    --input FILE    drive it from a harness input script instead of the pad

The frame line is byte-identical to the one `dream_harness` prints, so:

    SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy \
      ./build/recomp/dream --frames 600 --input recomp/harness/inputs/title_start_right.txt
    ./build/recomp/dream_harness --hooks on --frames 600 \
      --input recomp/harness/inputs/title_start_right.txt | grep '^frame' | tail -1

must agree on every field. `--frames` still opens the window, draws every frame
and fills the audio stream — only the 60.0988 Hz pacing is dropped — so the
comparison covers the platform layer rather than routing around it. The dummy
drivers are what make that work with no display and no sound card.
