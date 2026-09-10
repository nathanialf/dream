# recomp — verification harness

`dream_harness` runs the original ROM in an embedded SNES core, hashes the machine
state after every frame, and can run two instances side by side so a C
reimplementation of a routine can be proved equivalent to the ROM's own code. It is
the measuring instrument for the port described in `docs/RECOMP.md`; it does not
contain any of the port itself beyond one worked example.

Headless, deterministic, no SDL, no X, no network, no threads.

## Layout

    recomp/
      CMakeLists.txt              build (C11, -Wall -Wextra clean)
      include/snes_state.h        the hook API a recomped routine is written against
      harness/
        main.c                    CLI, frame loop, hashing, tracing, lockstep
        snes_state.c              hook API implemented over the emulator core
        ss_internal.h             private glue (struct SnesState)
        hooks.c                   recomp_hooks[] + the worked example
        xxh64.c/.h                XXH64, the per-frame region digest
        compare_coverage.py       trace vs out/codemap.txt
        inputs/                   input scripts
      third_party/lakesnes/       vendored emulator core (MIT)
        LICENSE.txt UPSTREAM.txt
        snes/*.c *.h

## Build

    make harness            # configure + build into build/recomp/dream_harness

or by hand:

    cmake -S recomp -B build/recomp -DCMAKE_BUILD_TYPE=Release
    cmake --build build/recomp

`build/` is gitignored, so nothing the harness produces can be committed.

## The emulator core

`third_party/lakesnes/` is a copy of [LakeSnes](https://github.com/elzo-d/LakeSnes)
by elzo-d (MIT), commit `9db90b86`, core only: no SDL frontend, no zip support, no
tracing frontend. It is a copy rather than a submodule because the hook mechanism
needs two lines inside the CPU's instruction loop. Every local change is marked
`// dream:` and listed in `third_party/lakesnes/UPSTREAM.txt`.

### Forcing HiROM

This ROM's internal header at `$FFC0` is tilemap fill (`06 37 00 37 ...`) and it has
no checksum, so `snes_loadRom()`'s header scoring is meaningless on it — it reads the
"name" as `.7.7.7.7.7.7.7.7.7.7.` and fails the checksum test. (As it happens the
scorer still lands on HiROM for this image, but only by accident of how the other
candidates score; nothing about that is a guarantee.)

The harness therefore does not call `snes_loadRom()` at all. `machine_load_rom()` in
`harness/main.c` does by hand what `snes_loadRom()` does *after* the scoring:

```c
cart_load(m->snes->cart, 2 /* HiROM */, rom, 0x200000, 0 /* no SRAM */);
snes_reset(m->snes, true);
m->snes->palTiming = false;   /* NTSC */
```

2 MiB is already a power of two, so the mirroring pass `snes_loadRom()` performs is a
no-op here. This is a harness-side decision; no emulator source is changed for it.

## CLI

    dream_harness [options]
      --rom FILE                ROM image (default baserom/DREAM.sfc)
      --frames N                frames to run (default 600)
      --input FILE              input script (default: no buttons)
      --trace FILE              write the executed-PC coverage set
      --dump-wram DIR           write DIR/wram_NNNNNN.bin after every frame
      --hooks on|off            install the recomp hook table (default off)
      --hook-table demo|empty   which table --hooks on installs (default demo)
      --lockstep                run hooks-off vs hooks-on, compare every frame
      --quiet                   suppress the per-frame lines
      --help

Exit status is 0 on success, 1 on a lockstep mismatch, 2 on a usage or I/O error.

### Per-frame output

One line per frame:

    frame    243 wram=640a08124c92765e vram=1566f57878afbe94 cgram=0d712e46f018d2fc
                 oam=1b7d5f6b3899896e pc=80:A4FE a=0081 x=0400 y=001E db=80 dp=0000
                 p=05 sp=01FF inidisp=0F mode=1

The four digests are XXH64 (seed 0) of, respectively, the 128 KB of WRAM, the 64 KB of
VRAM serialised little-endian, the 512 bytes of CGRAM, and the 544 bytes of OAM (512
low + 32 high). `inidisp` is the reconstructed `$2100` value (bit 7 = forced blank,
low nibble = brightness) and `mode` the BG mode, both handy for telling a running
game from a blanked one. The sample point is the instant the frame enters vblank, so
`inidisp` reflects the frame that just finished.

`--dump-wram DIR` additionally writes the raw 128 KB per frame, for `cmp`/`xxd` work
when a hash differs and you need to know where.

### Input scripts

One line per change, `frame Button+Button+...`; a set is held until a later line
changes it. `#` starts a comment, `-` and `none` mean no buttons. Button names are
`A B X Y L R Start Select Up Down Left Right`, case-insensitive. Example
(`harness/inputs/title_start_right.txt`):

    0    none
    120  Start
    126  none
    240  Right

### Traces

`--trace FILE` records every distinct PC executed during the run and writes them at
exit as sorted canonical addresses, one per line:

    # dream_harness PC coverage: 2986 distinct ROM addresses
    # 0 executed PCs fell outside the cart map (WRAM/registers)
    C08000
    C08003
    ...

The game executes most of its code through the `$80`/`$81` mirror banks, so PCs are
folded onto the disassembly's `$C0:0000 + file offset` form before being written —
the same form `out/codemap.txt`, `out/symbols.txt` and `out/dream.asm` use. PCs
outside the cart map (WRAM, registers) are not representable in that form and are
only counted, in the second header line.

`harness/compare_coverage.py` diffs a trace against the static tracer:

    python3 recomp/harness/compare_coverage.py TRACE \
        [--codemap out/codemap.txt] [--symbols out/symbols.txt] \
        [--list-unmapped] [--list-cold]

It reports executed addresses that fall outside every static code range (a static
trace that missed something, or code the tracer could not see) and static routines
that never executed (expected in bulk: a short scripted run touches a fraction of
the game).

## Hook API

A recomp hook is a C function that stands in for one 65816 routine. Before every
instruction fetch the core calls the harness with the 24-bit PC; if an entry in
`recomp_hooks[]` claims that address, the C function runs instead of the routine and
is responsible for leaving the CPU where the routine would have left it.

```c
#include "snes_state.h"

static bool hook_my_routine(SnesState* ss) {
  if(ss_flag_m(ss)) return false;      /* decline: let the ROM routine run */
  ss_wram_w16(ss, 0x0400, 0);
  ss_set_a(ss, 0x0200);
  ss_rts(ss);
  return true;
}

const RecompHook recomp_hooks[] = {
  { 0xc0a500, hook_my_routine, "my_routine" },
  { 0, NULL, NULL }                    /* terminator */
};
```

Entry addresses are written in the canonical `$C0:0000 + offset` form; the harness
folds the running PC through the mirror banks before matching, so one entry catches
the routine whether the ROM reaches it as `$C0:A500` or `$80:A500`.

Returning `false` means "not handled" and the original instruction executes
normally, which is how a hook guards its preconditions (CPU mode, direct page) rather
than silently doing the wrong thing.

`include/snes_state.h` is the whole API. In outline:

| group | functions |
|-------|-----------|
| registers | `ss_a/x/y/sp/dp/pc/db/pb/p` and `ss_set_*`, `ss_flag_e/m/x`, `ss_set_nz8/16` |
| WRAM, untimed | `ss_wram_r8/r16`, `ss_wram_w8/w16` (offset into the 128 KB) |
| bus, untimed | `ss_r8/r16`, `ss_w8/w16` (24-bit address, full memory map) |
| bus, timed | `ss_bus_r8`, `ss_bus_w8`, `ss_bus_w16`, `ss_fetch`, `ss_idle` |
| control | `ss_rts`, `ss_rtl`, `ss_check_int`, `ss_int_pending` |

The timed accessors are the reason a hook can be cycle-exact: they go through the
same `snes_cpuRead`/`snes_cpuWrite`/`snes_cpuIdle` entry points the CPU core itself
uses, so they charge the same access times, run DMA and HDMA at the same points, and
leave the same open-bus value. A hook that replays a routine's bus transactions in
order costs the emulator exactly what the routine cost. `ss_rts`/`ss_rtl` reproduce
the core's own 6-cycle return sequences, interrupt latch included.

### Known limitation

An interrupt raised part-way through a hooked routine is serviced when the hook
returns, not between the instructions it replaced: a hook is atomic where the real
routine is not. `ss_check_int()` keeps the *pending* state identical, so nothing is
lost, but an NMI can be taken up to one routine-length later. For the routines a
recomp starts with (hundreds of cycles, no interrupt interaction) this has not been
observable — see the lockstep results below — but it is the first thing to suspect if
a long hook drifts.

## Lockstep protocol

`--lockstep` creates two independent emulator instances from the same ROM image:

* **reference** — no hook table installed; the ROM's own code runs.
* **candidate** — `recomp_hooks[]` installed; hooked routines run as C.

Both are driven with the same input script, one frame at a time. After every frame
all four regions — WRAM (128 KB), VRAM (64 KB), CGRAM (512 B), OAM (544 B) — are
compared byte for byte. The first difference is printed as

    MISMATCH frame 188 region wram offset 0x00408 expected 00 got 01

("expected" is the reference, "got" the candidate) and the run stops with exit
status 1. If every frame matches, the run prints `lockstep: N frames, no mismatches`
and exits 0. Per-hook call counts are printed at the end, so a table that never fired
cannot be mistaken for a table that passed.

`--hooks` is ignored in lockstep mode: the point of the mode is precisely the
off-versus-on comparison. `--hook-table empty` still works, and trivially passes.

## The worked example

`harness/hooks.c` reimplements `clear_sprite_table` (`$C0:A500`, called once per
main-loop iteration from `$C08235` and once from `$C09324`): sixteen 16-bit `stz`
into `$0400..$041F`, then `$94 = $0200`, `$96 = 0`, then `rts`. The C body writes the
same values through the timed accessors and models the instruction stream (three
fetches then two writes per `stz abs`, and so on), so the routine costs the emulator
the same master cycles it did before.

Results on this ROM, 600 frames each:

| run | hook calls | result |
|-----|-----------|--------|
| no input | 0 | 600 frames, no mismatches (the routine is never reached on the title screen) |
| `inputs/title_start_right.txt` | 412 | 600 frames, no mismatches |

Two negative controls confirm the check has teeth: writing `01` instead of `00` in
the hook is caught at frame 188, `wram` offset `0x408`, exit 1. Dropping one 8-cycle
fetch per `stz` (a pure timing error, ~128 cycles per call) is *not* caught over 600
frames — this routine's timing is not observable in the compared regions at frame
granularity. State divergence is what lockstep proves; cycle fidelity is a discipline
the hook author keeps, not something these four regions can always police.

## Reproducing the reported runs

    make harness
    ./build/recomp/dream_harness --frames 600
    ./build/recomp/dream_harness --frames 600 --input recomp/harness/inputs/title_start_right.txt
    ./build/recomp/dream_harness --lockstep --hooks on --frames 600 --quiet \
        --input recomp/harness/inputs/title_start_right.txt
    ./build/recomp/dream_harness --frames 600 --quiet --trace /tmp/trace.txt \
        --input recomp/harness/inputs/title_start_right.txt
    python3 recomp/harness/compare_coverage.py /tmp/trace.txt
