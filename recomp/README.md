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
      include/spc_state.h         the same, for an SPC700 sound-driver routine
      spc/                        the SPC700 half of the port: one C function
        loader.c driver_cmd.c     per routine of spc/driver.asm, registering
        dsp_init.c                itself the same way (RECOMP_SPC_REGISTER)
        spc_time.h                one SPC700 instruction at a time
      harness/
        main.c                    CLI, frame loop, hashing, tracing, lockstep
        snes_state.c              hook API implemented over the emulator core
        spc_state.c               the SPC700 hook API, over the vendored APU
        ss_internal.h             private glue (struct SnesState)
        sps_internal.h            private glue (struct SpcState)
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
needs two lines inside the CPU's instruction loop, and two more inside the
SPC700's. Every local change is marked `// dream:` and listed in
`third_party/lakesnes/UPSTREAM.txt`.

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
      --hooks on|off            install the recomp 65816 routines (default off)
      --spc-hooks on|off        install the recomp SPC700 routines (default off)
      --hook-table all|demo|empty  which table to install (default all)
                                  all   everything recomp/src registered
                                  demo  the single worked example
                                  empty nothing
      --only A,B,C              install only these routines, by name
      --cycles FILE             per-routine cycle charge (default
                                config/recomp_cycles.txt; 'none' disables it)
      --profile FILE            measure the charge and write FILE (see below)
      --lockstep                run hooks-off vs hooks-on, compare every frame
      --test-nesting            self-test: a hook entered inside another hook's
                                callee keeps its own yield snapshot (see below)
      --test-spc-timing         self-test: sps_op_cycles() against the SPC700
                                core's own opcode timing (see below)
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

An optional `| Button+Button+...` second column addresses player 2's controller
(`snes->input2`); a line that omits it leaves player 2's held set unchanged, so
existing scripts (all first-column-only) still load and behave exactly as
before. `harness/inputs/p2_enemy_attack.txt` is the worked example: a live
second controller is otherwise unreachable in-game (`harness/inputs/README.md`
has the full story, filed as a debug/test feature the developers left in).

    240  Right
    500  none | B

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
worth doing. It runs two machines side by side over the same input: the reference with hooks off, measuring
what the ROM's own code spends between a routine's first instruction and the
return that leaves it (see below -- that is not the same thing as the return that
pops its frame), and the candidate with hooks on, measuring what the C body spends
on its own. It writes `config/recomp_cycles.txt`:

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

#### When the ROM's side of the measurement stops

The reference side has to decide when the ROM's own code is finished with a
routine. The stack pointer alone cannot say: three shapes in this ROM unwind
their own frame and keep running, so the frame is already gone before the routine
is.

* `entity_animate_only` and the three `entity_spawn_transform` routines end
  `jsl anim_update ; pla ; rts`, where the 16-bit `pla` drops the return address
  `entity_update_tick`'s `jsr` pushed so that the `rts` leaves the whole tick.
* the animation-rate handlers run into `anim_rate_store`, whose `plb` pops the
  byte the caller's `pea $8080 ; plb` left on the stack -- three instructions
  before the `jsl anim_update` that is nearly all of the routine's cost.
* every OAM emitter's "table is full" exit is `pla ; jmp loc_C0A6CD`, a jump into
  `entity_build_oam_frame`'s `pea $8080 ; plb ; plb ; rtl` tail.

Closing on the stack pointer stopped the clock at the `pla`, at the `plb` and at
the `jmp`. It cost the `pla ; rts` routines their last instruction, and it cost
`anim_rate_store` the entire `jsl anim_update`: it was billed 69 master cycles for
a routine that costs 1709.

A frame closes on the *pc* instead. At the routine's first instruction the
harness remembers the eight stack bytes above the frame; a frame is closed when
the pc is the instruction a return that moved the stack pointer to where it now
stands would land on, read back out of those bytes -- `rts` pops a word and adds
one, `rtl` pops a word and a bank and adds one, `rti` pops flags, a word and a
bank and adds nothing. The stack pointer is still the guard: a frame that is
still on the stack cannot have returned. With that, `entity_spawn_transform_b`
and `_c` report the ROM and the hook covering the same interval to the master
cycle, which is the reading that says a body is exact.

The limitation that remains: a routine that leaves through a tail `jmp` reaches
no return of its own, so its frame can only be closed when the routine it was
called from returns, and its figure then covers that tail as well. Those lines
say so -- `(rom side left through a tail jmp: measured to its caller's return)` --
and are never charged. Frames still open when the run ends are reported and
dropped rather than billed, and an entry that could not be measured because 64
routines were already in flight is counted in the same line.

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

#### One snapshot per invocation

"Since this hook began" is the whole of that mechanism, and hooks nest. A
converted routine reaches a converted callee through `ss_run_callee`, which runs
the reference CPU over it: the callee's entry address is dispatched again and its
own hook fires *inside* the caller's. The chain the ROM builds every frame is
`nmi_handler -> entity_update_tick -> an animation-rate handler -> anim_update ->
an animation callback`, and the gate's own scripts reach six hooks in flight at
once (the run report prints the high-water mark).

The dispatcher therefore keeps a *stack* of entry snapshots, one frame per hook in
flight (`harness/ss_internal.h`), pushed by `ss_enter_hook()` and popped by
`ss_leave_hook()` on every path out, the declining `harness/hooks.c` wrappers
included. `ss_yield_wanted()` answers from the innermost frame, so the inner hook
is asked about its own entry and the outer hook gets its own answer back when the
callee returns. Overflowing the stack is a fatal error, not a wrong answer.

A single slot per machine looks like it works -- `ss_run_callee` checks before
every opcode, so the inner hook is normally entered at an instant the outer hook
has just approved -- but it is only true while every path into a nested hook is
one of those checks. `ss_call_sub` / `ss_call_long` / `ss_run_until_return` run a
callee with no check at all, and a hook entered from inside one of those, after
the machine crossed a frame boundary, would leave the outer hook holding a
snapshot from the wrong side of the boundary: the outer hook would then finish its
routine atomically across the boundary the reference run stops at.

`--test-nesting` is that case, built out of two hooks at addresses no routine
uses, so it needs no ROM code and no input script:

    ./build/recomp/dream_harness --test-nesting

The outer hook is entered, spends cycles until the machine moves on under it
(vblank starts in phase 0, the frame counter moves in phase 1), then calls the
inner hook the way a converted routine reaches a converted callee. The inner hook
must see nothing to hand back -- it has only just started -- and the outer hook
must *still* want to yield afterwards. It exits 0 on pass, 1 on fail, and prints
one line per check.

## The SPC700 hook API

The sound driver is the ROM's other program: 3514 bytes of SPC700 code
(`spc/driver.asm`, `spc/spc_map.txt`) uploaded into the APU at boot and running
on its own processor for the rest of the session. `recomp/spc/` is its half of
the port, `include/spc_state.h` is the API it is written against, and
`harness/spc_state.c` implements that over the vendored APU. The shape is the
same as the 65816 side deliberately — a dispatcher in the core's instruction
loop, a self-registering table, timed accessors, a yield — but three things
differ enough to change the design, and they are the whole of what is new.

**Cycles are the only clock, and there is no charge to calibrate.** The SPC700
takes no interrupts here. What the driver observes is its own timers, the DSP's
tick and the four ports the 65816 writes, and every one of those moves on APU
cycles. An APU cycle is spent by exactly one thing: `apu_spcRead`,
`apu_spcWrite` and `apu_spcIdle` each call `apu_cycle()` once and nothing else
does. So a body that replays a routine's access sequence in order costs the
emulator exactly what the routine cost, to the cycle — there is no `--profile`
step for this side and no `config/recomp_cycles.txt` entry, because the figure
is zero by construction. `sps_read8` / `sps_write8` / `sps_idle` / `sps_fetch`
are those primitives; `sps_aram_*` are the untimed escape hatch.

**The yield boundary is the catch-up slice, not the frame.** The SPC does not
run alongside the 65816. `snes_catchupApu()` hands `apu_runCycles()` a budget —
at the end of every frame, and before every read or write of `$2140-$217F` — and
it runs whole opcodes until the budget is spent. The reference SPC therefore
stops *between two instructions in the middle of a routine*, at an instant a
hooked run would have to run the routine to its end. That is the same atomicity
problem the 65816 side solves at a frame boundary, and it has the same solution:
`apu_runCycles()` publishes where the step ends (`apu->sliceEnd`, the one field
added to the core for this), `sps_yield_wanted()` compares the cycle count
against it, and a body that models the instruction stream points the pc at the
next instruction and returns. The driver picks the routine up and finishes it.

It needs no entry snapshot and no snapshot stack, which is the one place this
side is *simpler*. The 65816's condition is a level — "vblank has started" stays
true for the rest of the frame — so a hook has to remember what the machine
looked like when it began, and hooks nest (`--test-nesting`). The SPC's is a
threshold on a monotonically increasing cycle count, and `apu_runCycles()` only
calls `spc_runOpcode()` while the budget is unspent, so every dispatch happens
strictly below the threshold and the same question has the same right answer for
every hook in flight. That also guarantees progress: a hook can never yield at
its own first instruction, so it always spends at least one cycle, so the
catch-up loop cannot spin.

**The DSP is shared state, not a register file.** Every DSP access goes through
`$F2`/`$F3` and therefore through the emulated DSP, so envelopes, key-on latches
and the echo buffer evolve exactly as they did. A body writes DSP registers with
`sps_write8(SPS_DSPADDR, ...)` / `sps_write8(SPS_DSPDATA, ...)` where the ROM
does; `sps_dsp_read` / `sps_dsp_write` are untimed inspection only.

A routine in `recomp/spc/` registers itself the same way `recomp/src/` does, and
the entry address is the SPC's own 16-bit one — no bank folding, because ARAM has
no mirrors:

```c
#include "spc_state.h"

static void dsp_flg_20(SpcState* sp) {
  ...
  sps_ret(sp);
}

static const SpcRecompEntry kDspInit[] = {
  { 0x1123, "dsp_flg_20", dsp_flg_20 },
};
RECOMP_SPC_REGISTER(kDspInit)   /* constructor -> recomp_spc_register() */
```

The name is the label in `spc/driver.asm`, because that is what
`tools/progress.py` credits an SPC routine by.

`include/spc_state.h` is the whole API. In outline:

| group | functions |
|-------|-----------|
| registers | `sps_a/x/y/sp/pc/ya/psw` and `sps_set_*` |
| flags | `sps_c/z/v/n/i/h/p/b`, `sps_set_*`, `sps_set_zn`, `sps_set_zn16`, `sps_dp` |
| ARAM, untimed | `sps_aram_r8/r16`, `sps_aram_w8/w16` |
| memory, timed | `sps_read8`, `sps_write8`, `sps_idle`, `sps_fetch` |
| stack | `sps_push8/16`, `sps_pull8/16` |
| return | `sps_ret` |
| DSP | `sps_dsp_addr`, `sps_dsp_read`, `sps_dsp_write` |
| ports | `sps_port_in`, `sps_port_out`, `sps_set_port_out` |
| timers | `sps_timer_target/counter/divider/enabled` |
| cycles | `sps_cycles`, `sps_consume_cycles`, `sps_op_cycles` |
| callees | `sps_call`, `sps_run_until_return`, `sps_run_callee` |
| yield | `sps_yield_wanted` |
| registry | `recomp_spc_register`, `recomp_spc_registry`, `RECOMP_SPC_REGISTER` |

`spc/spc_time.h` is the SPC700 counterpart of `src/dream_time.h`: one helper per
opcode form the driver uses, each reproducing that opcode's access sequence from
LakeSnes' own `spc.c` case body, and an `S(addr, bytes)` macro that publishes the
registers the body is holding in locals, offers the routine back, and fetches. A
body reads straight down `spc/driver.asm`, one macro per instruction:

```c
S(0x0683, 2); a = s_load(sp, sps_dp(sp, 0xE9));           /* mov a,$E9 */
S(0x0685, 2); s_cmp(sp, a, s_read(sp, sps_dp(sp, SPS_CPUIO0)));  /* cmp a,!CPUIO0 */
bool cmd = sps_z(sp);
S(0x0687, 2); s_branch(sp, cmd);                          /* beq cmd_receive */
if(cmd) S_GOTO(0x068C);
```

`S_GOTO(addr)` is how a body leaves through a tail `jmp` or falls through into
the next routine: it publishes the registers and points the pc at `addr`, so the
routine that owns that address runs next — its own hook if it has one. Most of
this driver's routines end that way rather than on `ret`.

### Calling a routine that is not converted yet

`sps_call(retAddr, callee)` is `ss_call_sub`'s twin: it charges the five cycles
`call abs` spends after its three fetches, pushes the return address, points the
pc at the callee and runs the reference SPC over it until the stack pointer is
back above the frame. Any hook the callee hits still fires, so `driver_init`'s
`call dsp_init` reaches `dsp_init`'s own C body.

`sps_run_callee(spBefore)` is the yielding form, and the bodies here use it
directly because these callees are long: `dsp_init` executes some 360
instructions (its two loops run eight times each) and `cmd7_stop_to_loader` spins
on `$FE` for a full timer-1 period, about 25 600 APU cycles. It stops when
the slice ends and returns true, leaving the callee running — safe because the
address the body pushed is the routine's real return address, so the callee's own
`ret` lands where the driver expects.

### `--test-spc-timing`

`sps_op_cycles()` states what the core's own implementation of an opcode costs,
and `spc_time.h` is built on the same counts. `--test-spc-timing` checks the
statement rather than trusting it: for every opcode the table covers it assembles
the instruction into scratch ARAM, executes it on the reference SPC700 with hooks
off, and compares the cycles actually spent against the figure. Operands are
chosen to stay out of the `$F0-$FF` register block, so nothing in the test touches
a timer, a port or the DSP; a taken branch's two extra cycles are added back where
the flags make the branch taken.

    ./build/recomp/dream_harness --test-spc-timing
    ...
      D7 mov (dp)+y,a           want  7 got  7  ok
      3F call abs               want  8 got  8  ok
    test-spc-timing: 61 opcodes, PASS

It exits 0 on pass, 1 on fail.

### The first batch

`recomp/spc/` converts the loader, the driver's entry and command set, and
`dsp_init` — twenty-two routines, every one of them modelling its own instruction
stream:

* `loader.c` — `spc_loader`, `loader_reset_dsp`, `loader_block_loop`,
  `loader_jump`. The IPL-uploaded block at `$04D8` that stays resident for the
  whole run, and the handshake the 65816's `spc_send_words` busy-waits against.
  The block loop patches the upload destination into the operand bytes of its own
  two `mov $0000+y,a` instructions and then leaves through `jmp ($0539+x)`, so the
  ROM writes its own jump target into its own code; the C body does exactly that,
  storing through the timed path and reading the vector back out of ARAM one byte
  at a time where the SPC700's operand fetch reads it.
* `driver_cmd.c` — `driver_entry`, `driver_init`, `main_loop`, `cmd_receive`,
  `cmd_dispatch`, the eight `cmd_table` handlers, `start_song`, `play_sfx` and
  `dsp_step_toward_zero`. `scale_volume` and `sfx_start` are not converted yet and
  run through `sps_run_callee`; `tick_wait` (`$0781`) is not either, and every
  handler hands the pc back to it, which is what the ROM's own `jmp tick_wait`
  does.
* `dsp_init.c` — `dsp_init` and `dsp_flg_20`: the DSP reset, the per-voice
  defaults and the sixteen per-slot arrays read out of the song header.

Fourteen of the twenty-two are entered by the gate's scripts and credited; the
call counts summed across all seven, with the tables installed on both
processors, are

    main_loop 18947   loader_block_loop 1531   cmd_receive 571   play_sfx 527
    dsp_flg_20 73     dsp_init 51              cmd_dispatch 44   driver_entry 29
    driver_init 29    loader_reset_dsp 29      loader_jump 29    cmd6_play 22
    cmd7_stop_to_loader 22                     spc_loader 7

and every script ends `+0 master cycles and +0 APU cycles` from the reference.
The other eight are command handlers no live 65816 code sends. Only three places
write the command port (`src/bank_C1.asm`): `spc_command` sends `$FF` with a
parameter (`cmd7_stop_to_loader`) and then `$FE` (`cmd6_play`);
`sfx_command_dispatch` sends the byte in X, which is the sound-effect path below
`$80` (`play_sfx`); and `orphan_C183F1`, which sends `$F9` (`cmd1_set_E7`), has
no caller. That leaves `cmd0_set_E8`, `cmd1_set_E7`, `cmd2_set_mono`,
`cmd4_pitch_offset` and `cmd5_voice5_volume` with no sender at all, and
`cmd3_fade_and_song` sent only by the eighteen stale bytes at `$C1:8403` — which
also makes `start_song` and `dsp_step_toward_zero`, reachable only through
`cmd3`, unreachable. They are converted because they are `cmd_table` entries and
the dispatch is not honest without them, and the gate lists them as unverified
rather than crediting them.

## Lockstep protocol

`--lockstep` creates two independent emulator instances from the same ROM image:

* **reference** — no hook table installed; the ROM's own code runs.
* **candidate** — the routine table installed; hooked routines run as C.

Both are driven with the same input script, one frame at a time. After every frame
seven regions are compared byte for byte: the four the PPU and the 65816 own —
WRAM (128 KB), VRAM (64 KB), CGRAM (512 B), OAM (544 B) — and the three the APU
owns, ARAM (the SPC700's 64 KB), `dspreg` (the DSP's 128 registers, read out of
the emulated DSP's own register file) and `spcreg` (A, X, Y, SP, PSW, the pc and
the `$F2` DSP-address latch, eight bytes). The first difference is printed as

    MISMATCH frame 188 region wram offset 0x00408 expected 00 got 01

("expected" is the reference, "got" the candidate) and the run stops with exit
status 1. If every frame matches, the run prints `lockstep: N frames, no mismatches`
and exits 0. Per-hook call counts are printed at the end — `hook` lines for the
65816 table and `spchook` lines for the SPC700 one — so a table that never fired
cannot be mistaken for a table that passed.

`--hooks` and `--spc-hooks` are both honoured in lockstep mode, but only on the
candidate: the reference never runs a hook of either kind, because the point of
the mode is precisely the off-versus-on comparison. `--hook-table empty` still
works, and trivially passes, and `--only NAME` narrows *both* tables to the named
routines, which is how a mismatch gets bisected down to one body on either
processor. The summary line reports how far the candidate has drifted from the
reference on both clocks:

    lockstep: 900 frames, no mismatches (candidate ended +0 master cycles
              and +0 APU cycles from the reference)

The master-cycle figure separates a 65816 timing problem from a behavioural one
at a glance. The APU-cycle figure is weaker on its own, because the SPC does not
free-run: `snes_catchupApu()` hands `apu_runCycles()` a budget and it runs whole
opcodes until the budget is spent, so the total spent per frame is set by the
budget rather than by what the SPC did. A timing error inside a sound-driver body
shows up instead as a *pc* difference in `spcreg` — the SPC is a few cycles ahead
or behind and is therefore on a different instruction when the frame ends — and
the APU figure then reports the size of it. Removing one internal cycle from
`main_loop` is caught that way at frame 82 of `title_start_right.txt`:

    MISMATCH frame 82 region spcreg offset 0x00005 expected C7 got CA
             (candidate is +0 master cycles, -3 APU cycles)

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

`tools/recomp_verify.py` runs `--lockstep --hooks on --spc-hooks on` over every
script in `harness/inputs/`, 900 frames each unless the script carries a
`# frames N` header, and prints per-script pass/fail plus the hook call counts
summed across the scripts. Both processors are gated in the same run and by the
same rule: a routine counts as verified only when every script passed *and* it was
entered at least once. The two tables are reported separately (`65816 routine call
counts`, `spc700 routine call counts`), and `--update` rewrites
`config/recomp.txt` (which `tools/progress.py` credits to the `recomp` badge) with
both, under `; --- 65816 ---` and `; --- SPC700 sound driver ---` headings, listing
the never-entered ones separately as unverified rather than crediting them. The
names are the labels `progress.py` looks routines up by: `out/symbols.txt` for the
65816 side, `spc/driver.asm` for the SPC700 side.

`--spc-hooks off` runs the 65816 side alone, which is how a change to one half is
checked against the other. It refuses `--update`, because every SPC routine would
then be written out as unverified.

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
    ./build/recomp/dream_harness --test-nesting      # the nested-hook snapshot test
    ./build/recomp/dream_harness --test-spc-timing   # SPC700 per-opcode cycle counts
    ./build/recomp/dream_harness --frames 600
    ./build/recomp/dream_harness --frames 600 --input recomp/harness/inputs/title_start_right.txt
    ./build/recomp/dream_harness --lockstep --hooks on --frames 900 --quiet \
        --input recomp/harness/inputs/title_start_right.txt
    ./build/recomp/dream_harness --lockstep --hooks on --spc-hooks on --frames 900 \
        --quiet --input recomp/harness/inputs/title_start_right.txt
    ./build/recomp/dream_harness --frames 600 --quiet --trace /tmp/trace.txt \
        --input recomp/harness/inputs/title_start_right.txt
    python3 recomp/harness/compare_coverage.py /tmp/trace.txt

To bisect a mismatch, install one routine at a time:

    ./build/recomp/dream_harness --lockstep --frames 900 --quiet \
        --only entity_sort_draw_order --input recomp/harness/inputs/mode_cycle.txt

and to see whether a body is cycle-exact, compare the two intervals `--profile`
reports for it:

    make recomp-profile && cat config/recomp_cycles.txt
