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
        coro.h                    create/switch/destroy one stack, the --no-cpu
        coro_ucontext.c           scheduler's one platform split: ucontext on
        coro_fibers.c             POSIX, Win32 fibers on Windows (CMake picks)
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
      --dump-cgram DIR          write DIR/cgram_NNNNNN.bin after every frame
      --dump-oam DIR            write DIR/oam_NNNNNN.bin after every frame
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
      --no-cpu                  run the C bodies with neither CPU core executing
                                an instruction (see below); implies --hooks on
                                --spc-hooks on, and is the candidate under
                                --lockstep
      --test-coro               self-test: the coroutine backend the --no-cpu
                                scheduler runs bodies on; needs no ROM (see below)
      --test-nesting            self-test: a hook entered inside another hook's
                                callee keeps its own yield snapshot (see below)
      --test-spc-timing         self-test: sps_op_cycles() against the SPC700
                                core's own opcode timing (see below)
      --unit FILE               routine-level lockstep over a seed spec, for the
                                routines no input script can reach (see below)
      --unit-only A,B           restrict --unit to these routines, by name
      --quiet                   suppress the per-frame lines
      --help

Exit status is 0 on success, 1 on a lockstep mismatch, 2 on a usage or I/O error,
3 on a `--no-cpu` run that reached a pc with no C body (or an IRQ, which this ROM
never enables).

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
when a hash differs and you need to know where. `--dump-cgram DIR` and `--dump-oam DIR`
do the same for the other two PPU regions the frame line hashes — 512 bytes of CGRAM,
512 + 32 bytes of OAM — out of the same snapshot, so what lands in the file is what the
hash covered. They are how `docs/data_formats.md`'s "Palette assignment" section is
checked against the running game.

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
| control | `ss_rts`, `ss_rtl`, `ss_xce`, `ss_wai`, `ss_check_int`, `ss_int_pending`, `ss_yield_wanted` |
| cycles | `ss_cycles`, `ss_consume_cycles` |
| registry | `recomp_register`, `recomp_registry`, `recomp_find`, `RECOMP_REGISTER` |
| no-cpu | `ss_nocpu_enable`, `ss_nocpu_enabled`, `ss_nocpu_run_frame` |

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

### `--test-coro`

The other self-test with no ROM in it, and the only one with no emulator in it
either: the coroutine backend of `harness/coro.h`, on its own.

    ./build/recomp/dream_harness --test-coro

It checks the five things the `--no-cpu` scheduler asks of a backend: that a body
suspends and resumes where it stopped with its stack intact (16 KiB of locals
written before a yield and verified after); that coroutines *nest*, so a coro
started or resumed from inside another coro's stack comes back to that stack --
which is what happens every time the SPC700's driver stack is resumed from inside
a 65816 body catching the APU up; that a body yields across a simulated frame
boundary and resumes inside the same loop, which is `ss_yield_wanted()`'s shape
with the machine replaced by a counter; that a coroutine suspended halfway
through a body can be torn down, which the scheduler does whenever an interrupt
abandons one and at every exit; and that coro_start on a coroutine still parked
in coro_yield discards that parked body instead of resuming it and runs the new
fn from scratch -- checked with the restart issued from main and from inside
another coro's stack, the shape `ss_nocpu_reap()` abandons a driver's body chain
in. It exits 0 on pass, 1 on fail, one line per check.

It exists because the Windows backend cannot be reached by the gate: the gate
needs the ROM and no ROM ever enters CI. `--test-coro` runs on any machine, so
`.github/workflows/ci.yml` runs it on a real Windows runner and the fiber backend
is exercised there.

## The routine-level gate

    ./build/recomp/dream_harness --unit config/recomp_units.txt
    python3 tools/recomp_verify.py --units

The frame gate can only credit a routine some input script reaches, and a few
dozen routines in this port are reachable by nothing at all: the six port
commands no live 65816 code sends, the sequence opcodes no song or sound-effect
bank emits, the stale `seq_cmd_table` slots, the one-row OAM emitters (no sprite
frame in the ROM is short enough to pick them), the animation-rate entries no
table word points at, the `rti` at the unused vectors, and the five 65816
routines with no caller anywhere in the ROM. "Never entered" was the honest thing
to say about them, and it left the `recomp` figure short of the code the port
actually covers.

`--unit` is the same comparison at the granularity of one routine. For each seed
line it:

1. boots the ROM under a named input script to a named frame, so WRAM, VRAM,
   CGRAM, OAM, ARAM, the DSP and the SPC registers hold content the game itself
   produced rather than zeroes;
2. loads that state into two fresh machines — the reference, which runs no hooks
   at all, and the candidate, which runs the whole table on both processors —
   and checks that the two are byte-identical before anything else happens;
3. applies the seed's register and memory overrides to both, pushes the return
   frame the routine expects, and points both at the entry address (in the
   `$80`/`$81` mirror the ROM itself runs the routine in, so the fetches cost
   what they cost);
4. runs the ROM's own code on the reference until control leaves the routine, and
   the C body on the candidate from the identical state;
5. compares all seven regions, every register, and both cycle counts.

    unit pass unused_wram_clear_full   seed 1  .../title_start_right.txt@400 rts
         1685154 master 0 apu, rom ran 81159 instrs, C left 0   ; ...
    unitok unused_wram_clear_full 4 seeds
    unit: 170 seeds over 42 routines, 0 failed

Exit status is 0 when every seed passed, 1 when one did not, 2 on a bad spec.
The instruction counts are the check on the check: "rom ran N instrs" is what the
reference executed, and "C left M" is what the emulated core still had to execute
on the candidate — a callee the port has not converted, and nothing else when it
is zero.

### Knowing when the routine is over

Control has left when the instruction about to run is outside the routine's byte
range, the instruction *before* it was inside, and no frame the routine pushed is
still on the stack. All three conditions earn their place. The stack pointer
alone cannot say, because the pc leaves the range on every call to a routine that
is not converted yet and comes back. The range alone cannot say either, for the
same reason. And "the instruction before it was inside" is what keeps a callee
that deliberately unbalances the stack from looking like the end: the sound
driver has exactly one, `seq_pop_x`, which pops its own return address, pops the
slot index its caller pushed and pushes only the return address back — for two
instructions in the middle of it the stack pointer is *above* where the handler
started while the pc is nowhere near the handler.

The return frame the harness pushes carries a sentinel address no routine owns,
so an `rts`, an `rtl`, an `rti` and the SPC700's `ret` all satisfy the one rule
and no instruction at the sentinel is ever fetched. `ret=` in the seed says which
frame to push, because the shape of the frame is the routine's own business.

Interrupts are the one thing taken out of the picture. A hook is atomic where the
routine it replaces is not, so an NMI landing inside the reference's run and
inside a different instruction of the candidate's would be a difference the
routine is not responsible for. The boot therefore ends with NMI and both timer
IRQs off and the pending latch cleared, and with the machine parked just past the
end of vblank, which leaves a whole active frame — some 300 000 master cycles —
before the vblank flag or the frame counter can move under the routine. For the
one routine longer than that (`unused_wram_clear_full`, about five frames) the
candidate is additionally held to the end of its routine (`ss_unit_hold`,
`harness/ss_internal.h`), which is the 65816's equivalent of the far-away
`apu->sliceEnd` the SPC700 half of the same gate sets. Without it a body would
hand the rest back at the first frame boundary and only its first fifth would be
compared as C.

### The seed spec

`config/recomp_units.txt` is the data, one line per seed:

    name  script  frame  key=value ...  ; note

`end=` and `ret=` are required; the rest override registers (`a= x= y= p= db=
dp= sp=`, or `a= x= y= psw= sp=` on the SPC700), memory (`ram:ADDR=`,
`aram:ADDR=`, one byte or a little-endian word), the APU ports (`port:N=`) and
the DSP (`dsp:RR=`, `dspadr=`). `stack=HH..` pushes extra bytes on top of the
return frame, for the two callers in this ROM that leave something there: the
animation-rate handlers' caller leaves one `$80` byte for the tail's `plb`, and
`seq_fetch` pushes the slot index before it dispatches a sequence-opcode handler.
Anything a line does not name keeps the value the booted machine had. The file's
own header documents every key.

Every routine carries at least four seeds, including the edge values its own
bounds allow — slot 0 and the last slot the entity or channel arrays hold, a zero
and an all-ones operand, both sides of every branch the routine tests. A routine
counts only when all of its seeds pass.

`tools/recomp_verify.py --units` runs the whole spec and reports it per routine;
`--update` runs both gates and writes `config/recomp.txt` with a `; unit` suffix
on every routine credited this way, so the file says which routines were proved
against a seeded state rather than a played frame. `tools/progress.py` reads the
label before the semicolon and credits both the same.

## Running without the CPUs

    ./build/recomp/dream_harness --no-cpu --lockstep --quiet \
        --frames 900 --input recomp/harness/inputs/level_walk_jump.txt
    python3 tools/recomp_verify.py --no-cpu

In `--no-cpu` nothing fetches an instruction. The C bodies are the program: a
scheduler starts at the reset body and follows every pc the machine hands over
-- a tail `jmp`, an `rts`, a callee frame, interrupt entry, the resumption of a
routine that stopped at a frame boundary -- by looking up the body that owns
that address in the registry and running it. `cpu_runOpcode` is never called at
all, and `spc_runOpcode` never reaches its fetch -- except in the SPC700's IPL
boot ROM, the one documented exception below. The run report says so as a
measurement rather than a claim:

    no-cpu: 0 65816 instructions and 53547 SPC700 instructions executed by the
            emulated cores (53547 of them the IPL boot ROM)
    no-cpu: 38187 bodies dispatched, 152 suspended at a frame boundary or an
            interrupt, 0 abandoned by an interrupt, 1 stack at once
    no-cpu: spc 20206 bodies dispatched, 515307 suspended at a catch-up slice
            boundary
    no-cpu: the reference executed 6629318 65816 and 4352740 SPC700 instructions
            over the same run

Everything else is the same machine. The PPU, DMA and HDMA, the DSP, the APU
timers and the port handshake all keep running out of the vendored core, driven
by the cycles the bodies charge through the timed accessors, which is why the
frame timing is identical to the reference's rather than merely close: every
script passes `--lockstep` at `+0 master cycles and +0 APU cycles`.

### The scheduler

`ss_nocpu_run_frame()` (`harness/snes_state.c`) stands in for `snes_runFrame()`
-- same stopping point, same APU catch-up at the end -- and its step is:

1. `cpu_runNonInstruction()`, the core's own reset / `stp` / `wai` / interrupt
   entry path. It is factored out of `cpu_runOpcode` in the vendored core (one
   more `// dream:` change) rather than copied, so the frame `cpu_doInterrupt`
   pushes and the cycles it spends are the core's, exactly as they are with the
   CPU running. It returns false when the machine is standing at an instruction
   boundary and would fetch next.
2. Otherwise the pc is a body. It is resolved through the same hook callback the
   CPU core calls, so a body is entered, counted and cycle-charged identically.

A pc the registry does not name is a fatal error naming the pc and the body that
handed it over:

    dream_harness: --no-cpu: no C body at 80:9679 (canonical C09679)
                   handed over by mode1_particle_dispatch at C0922A, 0 bodies in flight

which is what makes the mode the port's **dead-code check**: it cannot run at
all until every address the program actually reaches has a body. Running it
until it stopped saying that is what added `loc_C08001`, `loc_C0A4FD`,
`loc_C0A4FE`, `loc_C0BB81`, `loc_C0A6CD`, `loc_C09679` and `loc_C097DD` on the
65816 side, and thirteen interior entries on the SPC700 side (below).

### Suspending a routine instead of handing it back

A body is not a resumable object. It is straight-line C that `return`s when
`ss_yield_wanted()` says the machine has moved on underneath it, leaving the ROM
to finish the routine from the address it stopped at -- and that address is in
the middle of a routine, which the registry does not name. With no ROM there is
nothing to finish it.

So a dispatched body chain runs on a stack of its own (`Coro`,
`harness/coro.h`) and a yield *suspends* that stack instead of unwinding it.
That header is the port's one platform split: create, switch and destroy with an
explicit stack size, over `getcontext`/`makecontext`/`swapcontext` on POSIX
(`harness/coro_ucontext.c`) and `ConvertThreadToFiber`/`CreateFiber`/
`SwitchToFiber`/`DeleteFiber` on Windows (`harness/coro_fibers.c`), chosen by
CMake per platform and by nothing else. `--test-coro` exercises whichever one was
built, with no ROM (below). The scheduler gets control back at exactly the instruction
boundary the reference CPU stops on; resuming continues the body from inside
`ss_yield_wanted()`, which then answers false. One suspended context is the
whole of the "resume at an interior address" problem, and it needs no change to
any body.

Only two boundaries need the machine back, and they are the two the reference
stops at:

* **an interrupt**, which the 65816 services between two instructions, and
* **the end of a frame**, where `snes_runFrame()` returns and the harness and
  the app sample the machine.

Every other yield the ROM would have been offered is invisible from outside the
routine -- the ROM would simply have finished it, which is what the body now
does itself -- so suspending for one would cost a context switch and change
nothing. A 900-frame script suspends about 150 times, because the machine is
parked on the `wai` at `loc_C0A4FD` at almost every frame boundary and there is
no body in flight to stop; the ones that do are the boot's long routines and the
frames where the NMI handler overruns.

A suspension an interrupt displaces is kept, not dropped: an `rti` landing on
its pc with its stack pointer would resume it. This game's NMI handler resets S
and parks instead of returning, so the context is reclaimed two instructions
into the handler (`ldx #$01FF ; txs`), and the hook-snapshot depth the abandoned
chain was holding goes back with it. The report's "abandoned by an interrupt"
count is that; it is zero in the current scripts.

### Interrupts

NMI is raised by the PPU at the start of vblank and taken by the core's own
`cpu_doInterrupt`, before any hook is consulted -- that has always been true of
the `nmi` body, which is *entered* with PB, PC and P already pushed rather than
called, and `--no-cpu` changes nothing about it. `--no-cpu` additionally
*checks* the other half of the story on every step: the IRQ path is never used.
NMITIMEN's shadow at `$34` is only ever `$00`, `$01` or `$81`, so the h/v timer
enables are never set; no `cop` or `brk` is executed; and the `rti` at
`unused_vec` ($C0:A442) that every non-NMI vector points at is never reached. If
an IRQ is ever enabled or raised the mode stops with an error rather than
running on.

### The SPC700 side

The catch-up loop drives the driver's bodies the same way, and needs no change
to the core at all: `sps_nocpu_enable()` puts its own dispatcher in front of the
harness's on `spc->hook`, and that hook never lets `spc_runOpcode` reach a
fetch. `apu_runCycles()` calls `spc_runOpcode` until its budget is spent; each
call either resumes the driver's suspended stack on the instruction it stopped
at or starts the body that owns the pc, and the body suspends when
`sps_yield_wanted()` sees the slice end. So the interleaving between the two
processors is the reference's, instruction boundary for instruction boundary,
which is what the upload handshake needs: `upload_spc_block` on the 65816 side
busy-waits against `loader_block_loop` on this one.

It needs one context and never more: the SPC700 takes no interrupts here and
nothing else moves its pc, so a suspension is always resumed where it stopped.

The **IPL boot ROM** is the one exception in the whole mode. `$FFC0-$FFFF` while
`apu->romReadable` is the console's own firmware, not this ROM's program: it
receives the 136-byte loader block at power-on, jumps to it, and is never
entered again (the driver's "back to the loader" command jumps to `$04F3` in
ARAM). There is no body for it and `--no-cpu` does not invent one; those
instructions execute on the core and are counted separately in the report --
53547 of them, the same number in every script, all of them before the title
screen appears.

### Interior entry points

The registry names routine entry addresses, and a body starts at its own first
instruction. Where the program jumps into the *middle* of a routine, the address
needs a row of its own and a body that can start there -- the shape
`reset_native_at(ss, entry)` has always used for `loc_C08012` and `loc_C0805E`.
`--no-cpu` needs it wherever the ROM's own code used to pick the pc up:

| side | entry | inside | reached from |
|---|---|---|---|
| 65816 | `loc_C08001` | `reset` | the `xce` after `reset`'s `clc` |
| 65816 | `loc_C0A4FD` / `loc_C0A4FE` | the park loop | `loc_C0A4F5`'s tail |
| 65816 | `loc_C0A6CD` | `entity_build_oam_frame` | every emitter's "OAM is full" exit |
| SPC700 | `loc_0B78` | `seq_instrument` | nine handlers' tails ($0BFF, $0C56, $0CD4, $0DF5, $0EA1, $0EAB, $0EB8, $0EC7, $0F53) |
| SPC700 | `loc_0B7B` | `seq_instrument` | ten more ($0BB3, $0BBF, $0D6D, $0D8C, $0DD3, $0DE8, $0E8A, $0F0C, $0F40, $0FAC) |
| SPC700 | `loc_0BBC` | `seq_volume` | `seq_adsr` ($0E4B) |
| SPC700 | `loc_0CF1`, `loc_0CF4` | `seq_call` | `seq_call_once` ($0D0B, $0D19) |
| SPC700 | `loc_0D6A` | `seq_return` | $0D4C, `seq_vibrato_delay`, `seq_echo_setup` |
| SPC700 | `loc_0DDF` | `seq_slide_off` | `seq_slide_up` / `seq_slide_down` ($0D98, $0E0E) |
| SPC700 | `loc_0DF2` | `seq_tempo` | `seq_tempo_add` ($0E02) |
| SPC700 | `loc_0E9E` | `seq_set_note_E1` | `seq_set_note_E0` ($0E94) |
| SPC700 | `loc_0F09` | `seq_echo_on` | seven, including `seq_echo_off`, `seq_fir` and `loc_0F61` |
| SPC700 | `loc_0F61` | `seq_noise_on` | `seq_noise_off` ($0F74) |
| SPC700 | `loc_0F86` | `orphan_slide_down2` | `orphan_slide_up2` ($0F7F) |
| SPC700 | `loc_078E` | `tick_wait` | `cmd6_play` ($0779) and $0790 |

Three of the addresses the check named were not interior at all: they were whole
routines the port had left to the ROM because a tail `jmp` was the only way in
and the ROM was still there to take it. They are bodies now, like any other --
`loc_C0BB81`, the title screen's init that `reset` jumps to, in
`src/top_level.c`; and `loc_C09679` and `loc_C097DD`, the mode-1 and mode-2
particle spawn/cull loops the two dispatchers jump to, in `src/particles_fx.c`.

### Two instructions that needed C equivalents

`ss_xce()` and `ss_wai()` (`include/snes_state.h`) are the 65816's `xce` and
`wai`, each mirroring LakeSnes' own case body minus the opcode fetch. They exist
because both instructions move state no other accessor reaches -- the emulation
flag, and the CPU's `waiting` park -- and both are executed by this ROM, so
without them `--no-cpu` could not get past the third instruction of the program
or past the end of the first frame. `wai` leaves the machine idling in the
emulator's own `waiting` path, which `cpu_runNonInstruction()` runs for the
scheduler exactly as `cpu_runOpcode` runs it for the CPU.

### In the app

`dream` links the same `harness/snes_state.c` and `harness/spc_state.c`, so the
mode is already in its build. A front end turns it on with

```c
ss_nocpu_enable(&m->ss, true);      /* after the hook tables are installed */
sps_nocpu_enable(&m->sps, true);
...
ss_nocpu_run_frame(&m->ss);         /* in place of snes_runFrame(m->snes) */
```

`ss_nocpu_run_frame()` is a drop-in for `snes_runFrame()`: it returns at the
same instruction boundary and catches the APU up at the same point.

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
| no-cpu | `sps_nocpu_enable`, `sps_nocpu_enabled` |

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

### The SPC700 driver in C

`recomp/spc/` holds the whole driver, every routine modelling its own instruction
stream (95 registered entry addresses, counting the interior ones below):

* `loader.c` — `spc_loader`, `loader_reset_dsp`, `loader_block_loop`, `loader_jump`:
  the IPL-uploaded block at `$04D8` and the handshake the 65816's `upload_spc_block`
  busy-waits against. The block loop patches the upload destination into the operand
  bytes of its own two `mov $0000+y,a` instructions and leaves through `jmp ($0539+x)`;
  the C body stores through the timed path and reads the vector back out of ARAM
  exactly where the SPC700's operand fetch reads it.
* `driver_cmd.c` — `driver_entry`, `driver_init`, `main_loop`, `cmd_receive`,
  `cmd_dispatch`, the eight `cmd_table` handlers, `start_song`, `play_sfx`,
  `dsp_step_toward_zero`.
* `dsp_init.c` — `dsp_init`, `dsp_flg_20`.
* `sequencer.c` — `tick_wait`, `channel_loop`, `seq_step`, `seq_fetch`, `seq_note`,
  `seq_note_length`, `channel_update`, `seq_end`, `seq_pop_x`, `seq_retrigger`,
  `sfx_start`. The opcode dispatch `jmp (seq_cmd_table+x)` reads its vector from ARAM
  and hands the pc to the registry so each handler's hook fires.
* `seq_ops_a.c`, `seq_ops_b.c` — the sequence-opcode handlers `$00-$32`, including the
  four stale table slots nothing emits.

Every route between routines goes through the registry (pc hand-off or a real pushed
frame plus `sps_run_callee`), never a direct C call, so each routine is entered at its
own address and credited by the gate.

66 of the 95 registered entry addresses are entered by the gate's scripts; every
script ends `+0 master cycles and +0 APU cycles` from the reference. The 29 never
entered fall into three groups: the command handlers no live 65816 code sends (only
`spc_command` sends `$FF`/`$FE` and `sfx_command_dispatch` sends sound-effect ids;
the dead `unused_spc_set_e7_and_play` would send `$F9` and its stale sibling `$FB`),
the sequence opcodes none of the three songs or the sound-effect banks use
(`seq_instr_full`, `seq_volume_preset`, `seq_master_percent`, `seq_volume_presets`,
`seq_set_length`, `seq_clear_length`, `seq_tempo_add`, `seq_vibrato`, `seq_set_note_E0`,
`seq_set_note_E1`, `seq_transpose_add`, the noise trio), and the four stale table
slots. Every one of them is credited by the routine-level gate above instead:
`config/recomp_units.txt` carries four seeds each, and `config/recomp.txt` marks
them `; unit`.
