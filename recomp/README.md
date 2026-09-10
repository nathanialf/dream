# recomp — verification harness

`dream_harness` runs the original ROM in an embedded SNES core, hashes the machine
state after every frame, and can run two instances side by side so a C
reimplementation of a routine can be proved equivalent to the ROM's own code. It is
the measuring instrument for the port described in `docs/RECOMP.md`. The port
itself lives next to it, in `recomp/src/`.

Headless, deterministic, no SDL, no X, no network, no threads.

## Layout

    recomp/
      CMakeLists.txt              build (C11, -Wall -Wextra clean)
      include/snes_state.h        the hook API a recomped routine is written against
      src/                        the port: one file per subsystem, one C
        anim.c camera.c           function per 65816 routine. Each file
        entities.c input.c        registers its own entry addresses from a
        oam.c particles.c         constructor, so adding a routine edits no
        ppu_dma.c                 central table, and CMake globs the directory.
        dream_ram.h               RAM and register names (tools/names.txt)
        dream_alu.h               65816 arithmetic with its flag side effects
        dream_time.h              one instruction at a time: cycles and yields
      harness/
        main.c                    CLI, frame loop, hashing, tracing, lockstep
        snes_state.c              hook API implemented over the emulator core
        ss_internal.h             private glue (struct SnesState)
        hooks.c                   the --hook-table demo/empty tables
        xxh64.c/.h                XXH64, the per-frame region digest
        compare_coverage.py       trace vs out/codemap.txt
        explore.py                search tool: builds/keeps input scripts that add coverage
        inputs/                   input scripts, and inputs/README.md documenting them
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
      --hooks on|off            install the recomp routines (default off)
      --hook-table all|demo|empty  which table to install (default all)
                                  all   everything recomp/src registered
                                  demo  the single worked example
                                  empty nothing
      --only A,B,C              install only these routines, by name
      --cycles FILE             per-routine cycle charge (default
                                config/recomp_cycles.txt; 'none' disables it)
      --profile FILE            measure the charge and write FILE (see below)
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
instruction fetch the core calls the harness with the 24-bit PC; if an installed
entry claims that address, the C function runs instead of the routine and is
responsible for leaving the CPU where the routine would have left it.

A routine in `recomp/src/` is written against `RecompEntry` and registers itself:

```c
#include "snes_state.h"

void clear_sprite_table(SnesState* ss) {
  ...
  ss_rts(ss);
}

static const RecompEntry kOam[] = {
  { 0xc0a500, "clear_sprite_table", clear_sprite_table },
};
RECOMP_REGISTER(kOam)          /* file-scope constructor -> recomp_register() */
```

`RECOMP_REGISTER` expands to an `__attribute__((constructor))` that calls
`recomp_register(addr, name, fn)` for each row before `main()` runs, so a new
routine needs no edit to any central table and no change to `CMakeLists.txt`
(the glob picks the file up). `--hook-table all`, the default for `--hooks on`,
installs the whole registry.

`harness/hooks.c` keeps the older `recomp_hooks[]` shape for `--hook-table demo`:
its entries return `bool`, and returning `false` means "not handled", which lets a
hook guard a precondition (CPU mode, direct page) instead of silently doing the
wrong thing. Registry routines return `void` and always handle the call.

Entry addresses are written in the canonical `$C0:0000 + offset` form; the harness
folds the running PC through the mirror banks before matching, so one entry catches
the routine whether the ROM reaches it as `$C0:A500` or `$80:A500`.

`include/snes_state.h` is the whole API. In outline:

| group | functions |
|-------|-----------|
| registers | `ss_a/x/y/sp/dp/pc/db/pb/p` and `ss_set_*`, `ss_flag_e/m/x`, `ss_set_nz8/16` |
| flags | `ss_c/z/v/n`, `ss_set_c/z/v/n` |
| WRAM, untimed | `ss_wram_r8/r16`, `ss_wram_w8/w16` (offset into the 128 KB) |
| bus, untimed | `ss_r8/r16`, `ss_w8/w16` (24-bit address, full memory map) |
| direct page | `ss_dp_r8/r16`, `ss_dp_w8/w16` (offset added to DP, bank 0) |
| data bank | `ss_db_r8/r16`, `ss_db_w8/w16` (absolute address through DB) |
| registers, timed | `ss_reg_r8/r16`, `ss_reg_w8/w16`, `ss_dma_run` |
| bus, timed | `ss_bus_r8`, `ss_bus_w8`, `ss_bus_w16`, `ss_fetch`, `ss_idle` |
| stack | `ss_push8/16`, `ss_pull8/16` |
| callees | `ss_call_sub`, `ss_call_long`, `ss_run_callee`, `ss_run_until_return` |
| control | `ss_rts`, `ss_rtl`, `ss_check_int`, `ss_int_pending`, `ss_yield_wanted` |
| cycles | `ss_cycles`, `ss_consume_cycles` |
| registry | `recomp_register`, `recomp_registry`, `RECOMP_REGISTER` |

The timed accessors are the reason a hook can be cycle-exact: they go through the
same `snes_cpuRead`/`snes_cpuWrite`/`snes_cpuIdle` entry points the CPU core itself
uses, so they charge the same access times, run DMA and HDMA at the same points, and
leave the same open-bus value. A hook that replays a routine's bus transactions in
order costs the emulator exactly what the routine cost. `ss_rts`/`ss_rtl` reproduce
the core's own 6-cycle return sequences, interrupt latch included; note that they do
*not* include the return opcode's own fetch, which the body supplies.

Hardware registers must go through `ss_reg_*` rather than the untimed accessors: an
untimed write to `MDMAEN` would only arm the channel instead of running the
transfer, and an untimed read of `HVBJOY` would never change, so the joypad wait
loop would never end.

### Calling a routine that is not converted yet

`ss_call_sub` (callee ends in `rts`) and `ss_call_long` (`rtl`) push a return frame,
point the CPU at the callee and run it on the emulator until the stack pointer is
back above the frame, then return to C. Interrupts taken inside the callee are
serviced normally, and any hook the callee hits still fires, so a callee that gets
converted later needs no change at the call site. `ss_run_until_return` is the
primitive underneath, for a body that is also modelling the calling instruction's
own cycles and wants to build the frame itself. `ss_run_callee` is the same thing
but it stops at a boundary inside the callee when the machine moves on, which a
hand-built frame can afford because the address it pushed is the routine's real
one. `src/anim.c` does exactly that for the `jsl anim_update` at the end of every
animation-rate handler, which is what keeps that hook from being atomic across a
routine that can reach a VRAM block upload.

### Cycle cost, and `--profile`

A C body costs the emulator only what its timed accesses cost, which for a routine
that mostly computes is nothing. Running a routine in zero cycles moves every later
NMI, and this ROM notices: the SPC upload handshake counts words in a busy-wait
against the APU's own clock, and the joypad wait loop reads the hblank flag.

There are two ways to pay the cost back.

The one every routine in the port uses is to *model the instruction stream*:
`src/dream_time.h` has one helper per 65816 access pattern (`S`/`SI`/`SEP`/`REP`
for an instruction's opcode and operands, `t_read8/16`, `t_write8/16`, `t_index`,
`t_branch`), written against the core's own opcode implementations. A body built
from them costs the emulator exactly what the routine cost, to the master cycle,
and `--profile` confirms it.

The other is the charge `--profile` measures, for a routine where modelling is not
worth doing. It runs two
machines side by side over the same input: the reference with hooks off, measuring
what the ROM's own code spends between a routine's first instruction and the return
that pops its frame, and the candidate with hooks on, measuring what the C body
spends on its own. It writes `config/recomp_cycles.txt`:

    C09246 particle_table_clear    0    2   ; rom 3789 [3464-4114] hook 3789 [3464-4114]

The number after the name is the charge: the difference of the two means, which the
harness spends after the hook returns (in short steps, so the DRAM refresh lands
where it would have). Taking the *difference* rather than the ROM's total is what
makes a DMA routine come out right: the transfer time is in both means and cancels,
leaving only the instruction overhead the C body skipped. The bracketed ranges are
the per-call minimum and maximum, and they are the check that a body is exact: when
the ROM's range and the hook's range are the same interval, the charge is 0 and the
routine costs what it always did. Every routine in this batch is in that state, so
the file is all zeros; it exists for the next one that is not.

    make recomp-profile        # rewrite config/recomp_cycles.txt

Some entries carry a note instead of a charge. A routine the ROM only reaches by
falling through from the one above it is never entered as a hook, so there is
nothing to calibrate. And a hook that did not always return -- one ending in a
tail `jmp`, whose callee has not run yet, or one that handed the routine back to
the ROM at a frame boundary -- has the two figures covering different work, so the
harness bills only a hook that returned.

### Atomicity, and yielding back to the ROM

A hook is atomic where the routine it replaces is not, and that shows up twice.
An interrupt is serviced by the 65816 between two instructions but by a hook only
after all of them. And the frame loop stops at an instruction boundary, so a
reference run stops in the middle of a long routine at exactly the point a hooked
run cannot: the two are then compared at different points of the same routine.

`ss_yield_wanted()` reports either condition, and a body that models the
instruction stream can simply stop. Every register, flag and byte of memory is
already what the 65816 would have left at that boundary, so pointing the pc at the
address of the next instruction and returning hands the rest of the routine to the
ROM, which finishes it.

That check is folded into the step helpers, so it happens before *every*
instruction rather than at chosen points: `S(addr, n)`, `SI(addr)`, `SEP`/`REP`
publish the registers the body is holding in locals, offer the routine back, and
only then fetch. A body therefore reads down the listing, one macro per
instruction, and is never atomic over more than one of them:

```c
S(0xA1B0, 3);                        /* lda entity_x */
a = t_read16(ss, ss_abs(ss, entity_x));
ss_set_nz16(ss, a);
SI(0xA1B3); ss_set_c(ss, true);      /* sec */
```

The same applies while a not-yet-converted callee is running: `ss_run_callee`
stops at a boundary inside it and leaves it running, because the frame the hook
pushed is the routine's real return address, so the callee's own `rtl` lands where
the ROM expects. Without that, the animation-rate handlers would be atomic across
`anim_update`, which can reach a VRAM block upload two hundred thousand cycles
long, and the long input scripts catch it within a couple of thousand frames.

## Lockstep protocol

`--lockstep` creates two independent emulator instances from the same ROM image:

* **reference** — no hook table installed; the ROM's own code runs.
* **candidate** — the routine table installed; hooked routines run as C.

Both are driven with the same input script, one frame at a time. After every frame
all four regions — WRAM (128 KB), VRAM (64 KB), CGRAM (512 B), OAM (544 B) — are
compared byte for byte. The first difference is printed as

    MISMATCH frame 188 region wram offset 0x00408 expected 00 got 01

("expected" is the reference, "got" the candidate) and the run stops with exit
status 1. If every frame matches, the run prints `lockstep: N frames, no mismatches`
and exits 0. Per-hook call counts are printed at the end, so a table that never fired
cannot be mistaken for a table that passed.

`--hooks` is ignored in lockstep mode: the point of the mode is precisely the
off-versus-on comparison. `--hook-table empty` still works, and trivially passes,
and `--only NAME` narrows the table to one routine, which is how a mismatch gets
bisected. The summary line also reports how far the candidate's master-cycle count
has drifted from the reference's, which separates a timing problem from a
behavioural one at a glance.

## The worked example

`src/oam.c` reimplements `clear_sprite_table` (`$C0:A500`, called once per
main-loop iteration from `$C08235` and once from `$C09324`): sixteen 16-bit `stz`
into `$0400..$041F`, then `$94 = $0200`, `$96 = 0`, then `rts`. It is also what
`--hook-table demo` installs, through the declining wrapper in `harness/hooks.c`
that checks the CPU mode first.

Like every other routine in `src/`, the body writes the values through the timed
accessors and models the instruction stream (three fetches then two writes per
`stz abs`, and so on), so the routine costs the emulator the same master cycles it
did before: `--profile` reports `rom 703 [658-754] hook 703 [658-754]`, the same
interval, and therefore a charge of zero.

## The gate

    make recomp-check          # build the harness, then tools/recomp_verify.py

`tools/recomp_verify.py` runs `--lockstep --hooks on` over every script in
`harness/inputs/`, 900 frames each unless the script carries a `# frames N` header,
and prints per-script pass/fail plus the hook call counts summed across the scripts.
A routine counts as verified only when every script passed *and* it was entered at
least once; `--update` rewrites `config/recomp.txt` (which `tools/progress.py`
credits to the `recomp` badge) from that, listing the never-entered ones separately
as unverified rather than crediting them.

`tools/hooks/pre-commit` runs the gate after `make check` when a commit touches
`recomp/` or `config/recomp*`.

Three of the thirty converted routines are listed as unverified rather than
credited, because no script *enters* them:

* `anim_rate_1_2` (`$99D3`) has no table word pointing at it anywhere in the ROM
  (`docs/handler_tables.md` section 1 lists it as unreferenced). The only way the
  ROM reaches it is by falling through from `$99D2`, which `anim_rate_1_4`'s hook
  already covers, so its entry address can never fire. It is converted because it
  is one step of that fall-through chain.
* `anim_rate_3_8` (`$99BA`) is the same story one table entry along: it is
  reachable in principle (entity type `$0E`), but `anim_rate_3_16` at `$99B9`
  falls into it, so whenever the shape is used it is `$99B9` that gets entered.
* `anim_rate_3_16` (`$99B9`) does fire, 115 times, but only past frame 900:
  `level_walk_jump.txt` run to its full 3700 frames enters it and still matches.
  The gate's default length is 900 frames, so it does not count yet. A
  `# frames 3700` header on that script would make it count.

## Coverage scripts

`harness/inputs/` holds a small set of scripts built to maximize the union of static
routines exercised, for the recomp lockstep gate: `title_start_right.txt` (the
worked example above), `title_attract_then_start.txt`, `mode_cycle.txt`,
`level_walk_jump.txt` and `level_long_traverse.txt`. Together they take
`compare_coverage.py`'s count from 74/123 (the single `title_start_right.txt` run)
to 110/123, and reach all four `game_mode` values -- 0, 1 and 2 (the level scenes)
and 3 (title) -- not just mode 0. The key to modes 1/2/3 is a debug/attract feature
found during the search: pressing **Select** while no screen fade is in progress
advances `game_mode` by one (wrapping 3 back to 0), which is otherwise not
documented anywhere in the ROM. `harness/explore.py` is the search tool that found
these scripts (it runs candidate button scripts, traces coverage, and greedily
keeps whatever adds previously-uncovered routines); `harness/inputs/README.md` has
the full per-script/union coverage table and, for the 13 routines still cold,
which are dead code versus which would need a specific entity encounter not yet
located.

## Reproducing the reported runs

    make harness
    make recomp-check                       # the gate: every script, every routine
    ./build/recomp/dream_harness --frames 600
    ./build/recomp/dream_harness --frames 600 --input recomp/harness/inputs/title_start_right.txt
    ./build/recomp/dream_harness --lockstep --hooks on --frames 900 --quiet \
        --input recomp/harness/inputs/title_start_right.txt
    ./build/recomp/dream_harness --frames 600 --quiet --trace /tmp/trace.txt \
        --input recomp/harness/inputs/title_start_right.txt
    python3 recomp/harness/compare_coverage.py /tmp/trace.txt

To bisect a mismatch, install one routine at a time:

    ./build/recomp/dream_harness --lockstep --frames 900 --quiet \
        --only entity_sort_draw_order --input recomp/harness/inputs/mode_cycle.txt

and to see whether a body is cycle-exact, compare the two intervals `--profile`
reports for it:

    make recomp-profile && cat config/recomp_cycles.txt
