/* snes_state.h — the recomp's view of the reference machine.
 *
 * A recomp hook is a C function that stands in for one 65816 routine. It runs
 * instead of the routine's first instruction, with full access to the emulator's
 * registers and memory, and is responsible for leaving the CPU where the routine
 * would have left it (normally by calling ss_rts() / ss_rtl()).
 *
 * Two families of accessors are offered:
 *
 *   ss_r8, ss_w8, ss_wram_...  untimed. Use these when a hook does not care
 *                           about cycle fidelity (bulk work, scratch RAM).
 *   ss_reg_...              timed *and* DMA-aware. Every access to a hardware
 *                           register ($2100-$21FF, $4200-$44FF) must go through
 *                           these: an untimed write to MDMAEN would leave the
 *                           transfer pending instead of running it, and an
 *                           untimed read of HVBJOY would never change.
 *   ss_bus_..., ss_fetch, ss_idle
 *                           timed: they charge the emulator exactly what the
 *                           corresponding 65816 bus cycle costs and let DMA/HDMA
 *                           run, so a hook can reproduce a routine's timing to the
 *                           master cycle. --lockstep compares a hooked run against
 *                           an unhooked one, so a hook that skips this will drift.
 *
 * See recomp/README.md for the lockstep protocol and the worked example.
 */
#ifndef DREAM_SNES_STATE_H
#define DREAM_SNES_STATE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct SnesState SnesState;

/* ---- registers -------------------------------------------------------- */
uint16_t ss_a(const SnesState* ss);
uint16_t ss_x(const SnesState* ss);
uint16_t ss_y(const SnesState* ss);
uint16_t ss_sp(const SnesState* ss);
uint16_t ss_dp(const SnesState* ss);
uint16_t ss_pc(const SnesState* ss);
uint8_t  ss_db(const SnesState* ss);
uint8_t  ss_pb(const SnesState* ss);
uint8_t  ss_p(const SnesState* ss);      /* nvmxdizc, as php would push */

void ss_set_a(SnesState* ss, uint16_t v);
void ss_set_x(SnesState* ss, uint16_t v);
void ss_set_y(SnesState* ss, uint16_t v);
void ss_set_sp(SnesState* ss, uint16_t v);
void ss_set_dp(SnesState* ss, uint16_t v);
void ss_set_pc(SnesState* ss, uint8_t bank, uint16_t pc);
void ss_set_db(SnesState* ss, uint8_t v);
void ss_set_p(SnesState* ss, uint8_t v);

bool ss_flag_e(const SnesState* ss);     /* emulation mode */
bool ss_flag_m(const SnesState* ss);     /* 1 = 8-bit accumulator */
bool ss_flag_x(const SnesState* ss);     /* 1 = 8-bit index */
void ss_set_nz16(SnesState* ss, uint16_t v);
void ss_set_nz8(SnesState* ss, uint8_t v);

/* An interrupt is already latched or pending; a hook that cannot be interrupted
 * mid-routine should decline (return false) when this is true. */
bool ss_int_pending(const SnesState* ss);

/* Latch pending interrupts exactly as the last cycle of a 65816 instruction does.
 * A hook that models a multi-instruction routine calls this where the real
 * instructions would, so the pending-interrupt state it leaves behind matches.
 * (An interrupt raised inside a hooked routine is serviced when the hook returns,
 * not part-way through it — see "Hook API" in recomp/README.md.) */
void ss_check_int(SnesState* ss);

/* ---- memory, untimed -------------------------------------------------- */
uint8_t  ss_wram_r8(const SnesState* ss, uint32_t off);       /* off: 0..0x1ffff */
uint16_t ss_wram_r16(const SnesState* ss, uint32_t off);
void     ss_wram_w8(SnesState* ss, uint32_t off, uint8_t v);
void     ss_wram_w16(SnesState* ss, uint32_t off, uint16_t v);

uint8_t  ss_r8(SnesState* ss, uint32_t adr24);                /* full bus map */
uint16_t ss_r16(SnesState* ss, uint32_t adr24);
void     ss_w8(SnesState* ss, uint32_t adr24, uint8_t v);
void     ss_w16(SnesState* ss, uint32_t adr24, uint16_t v);

/* ---- memory, timed (one 65816 bus cycle each) ------------------------- */
uint8_t ss_bus_r8(SnesState* ss, uint32_t adr24);
void    ss_bus_w8(SnesState* ss, uint32_t adr24, uint8_t v);
void    ss_bus_w16(SnesState* ss, uint32_t adr24, uint16_t v); /* low byte first */
void    ss_fetch(SnesState* ss, int bytes); /* n opcode/operand reads at pb:pc, pc += n */
void    ss_idle(SnesState* ss);             /* one internal (6 master cycle) cycle */

/* ---- returns ---------------------------------------------------------- */
void ss_rts(SnesState* ss);   /* 6-cycle rts: pc = pop16 + 1 */
void ss_rtl(SnesState* ss);   /* 6-cycle rtl: pc = pop16 + 1, pb = pop8 */

/* ---- flags, individually ---------------------------------------------- */
bool ss_c(const SnesState* ss);
bool ss_z(const SnesState* ss);
bool ss_v(const SnesState* ss);
bool ss_n(const SnesState* ss);
void ss_set_c(SnesState* ss, bool v);
void ss_set_z(SnesState* ss, bool v);
void ss_set_v(SnesState* ss, bool v);
void ss_set_n(SnesState* ss, bool v);

/* ---- addressing helpers ----------------------------------------------- */
/* Direct page: the 65816 forms "lda $12" / "lda $12,X". The offset is added to
 * DP and wrapped to bank 0, exactly as the CPU does with DP low byte 0. */
uint8_t  ss_dp_r8(const SnesState* ss, uint16_t off);
uint16_t ss_dp_r16(const SnesState* ss, uint16_t off);
void     ss_dp_w8(SnesState* ss, uint16_t off, uint8_t v);
void     ss_dp_w16(SnesState* ss, uint16_t off, uint16_t v);

/* Data-bank-relative absolute: the 65816 forms "lda $0708" / "lda $0708,X".
 * Goes through the full bus map, so DB = $80 reaches the WRAM mirror and DB = $C0
 * reaches ROM, just as the routine's own addressing does. */
uint8_t  ss_db_r8(SnesState* ss, uint16_t abs);
uint16_t ss_db_r16(SnesState* ss, uint16_t abs);
void     ss_db_w8(SnesState* ss, uint16_t abs, uint8_t v);
void     ss_db_w16(SnesState* ss, uint16_t abs, uint16_t v);

/* ---- hardware registers (timed, DMA-aware) ---------------------------- */
uint8_t  ss_reg_r8(SnesState* ss, uint32_t adr24);
uint16_t ss_reg_r16(SnesState* ss, uint32_t adr24);   /* low byte first */
void     ss_reg_w8(SnesState* ss, uint32_t adr24, uint8_t v);
void     ss_reg_w16(SnesState* ss, uint32_t adr24, uint16_t v); /* low byte first */

/* ---- stack ------------------------------------------------------------ */
uint8_t  ss_pull8(SnesState* ss);
uint16_t ss_pull16(SnesState* ss);
void     ss_push8(SnesState* ss, uint8_t v);
void     ss_push16(SnesState* ss, uint16_t v);

/* ---- calling a routine that is not converted yet ---------------------- */
/* Run the reference CPU over a callee the recomp does not implement, then come
 * back to C. A return address is pushed, pc/pb are set to the callee and the
 * emulator executes instructions until the stack pointer is back above the
 * pushed frame; NMIs taken inside the callee are serviced normally. Any hook
 * the callee itself hits still fires, so a converted callee stays converted.
 *   ss_call_sub  — callee ends in rts (entered with jsr)
 *   ss_call_long — callee ends in rtl (entered with jsl)
 * A/X/Y/DB/flags are the caller's on entry and the callee's on return, exactly
 * as with the real jsr/jsl. */
void ss_call_sub(SnesState* ss, uint8_t bank, uint16_t addr);
void ss_call_long(SnesState* ss, uint8_t bank, uint16_t addr);

/* The primitive underneath both, for a hook that wants to build the call frame
 * itself because it is also modelling the caller instruction's cycles: push the
 * return frame and set pc/pb by hand, then run until the stack pointer is back
 * above spBefore. */
void ss_run_until_return(SnesState* ss, uint16_t spBefore);

/* The same, but it also stops when the machine moves on underneath the hook
 * (ss_yield_wanted) and reports that by returning true. Only safe when the frame
 * that was pushed is the routine's *real* return address, because the callee is
 * left running: its own rts/rtl then lands where the ROM would have gone, and
 * the ROM finishes the routine. A callee can run for a long time -- anim_update
 * reaches a VRAM block upload -- so without this the hook would be atomic across
 * a frame boundary that the reference run stops at. */
bool ss_run_callee(SnesState* ss, uint16_t spBefore);

/* ---- yielding back to the ROM ------------------------------------------- */
/* True once the machine has crossed into vblank, started a new frame, or
 * latched an interrupt since this hook began.
 *
 * A hook is atomic where the routine it replaces is not: the emulator's frame
 * loop stops at an instruction boundary, and the reference run stops in the
 * middle of a long routine where a hooked run would have to finish it first. So
 * would an interrupt: the 65816 services it between two instructions, a hook
 * only after all of them. Either way the two runs are then compared at different
 * points in the same routine.
 *
 * A body that models the instruction stream can simply stop. Every register,
 * flag and byte of memory is already what the 65816 would have left at that
 * boundary, so setting the pc to the address of the next instruction and
 * returning hands the rest of the routine to the ROM, which finishes it. The
 * port checks this before every instruction (see recomp/src/dream_time.h), which
 * is what keeps a hook from being atomic over more than one of them. */
bool ss_yield_wanted(const SnesState* ss);

/* ---- instructions the hook API cannot otherwise perform ----------------- *
 *
 * Two 65816 instructions move state no other accessor reaches, and this ROM
 * executes both. Without them a body has to hand the instruction back to the
 * emulated CPU, which is exactly what --no-cpu cannot do.
 *
 * ss_xce mirrors LakeSnes cpu.c case 0xfb and ss_wai case 0xcb, in each case
 * minus the opcode fetch the body supplies. `wai` parks the CPU: the machine
 * idles until an interrupt is raised, which the scheduler (or the core's own
 * cpu_runOpcode) handles from the `waiting` flag, so the body simply returns
 * afterwards with the pc on the instruction after it. */
void ss_xce(SnesState* ss);
void ss_wai(SnesState* ss);

/* ---- running with no CPU (--no-cpu) ------------------------------------- *
 *
 * The C bodies are the program: nothing fetches an instruction, and every pc
 * hand-off -- a tail jmp, a return, a callee frame, interrupt entry, the
 * resumption of a yielded routine -- is resolved through the registry instead.
 * A pc with no body is a fatal error naming the pc and the body that handed it
 * over, which is what makes --no-cpu the port's dead-code check.
 *
 * ss_nocpu_run_frame() stands in for snes_runFrame(): same stopping point, same
 * APU catch-up at the end of the frame. Enable the mode once, after the hook
 * table is installed, on both processors (sps_nocpu_enable is its twin). */
void ss_nocpu_enable(SnesState* ss, bool on);
bool ss_nocpu_enabled(const SnesState* ss);
void ss_nocpu_run_frame(SnesState* ss);

/* ---- DMA ---------------------------------------------------------------- */
/* Let a transfer the hook just started actually run. A write to MDMAEN only
 * arms the channel; the emulator performs the transfer on the next bus cycle
 * after that, which for the ROM is the instruction that follows the store. A
 * hook that arms a channel and then reprograms it without spending any bus time
 * in between would run the transfer with the *next* job's registers. Call this
 * immediately after every MDMAEN write, where the ROM's next instructions are.
 */
void ss_dma_run(SnesState* ss);

/* ---- cycle accounting ------------------------------------------------- */
uint64_t ss_cycles(const SnesState* ss);        /* master cycles since reset */
void     ss_consume_cycles(SnesState* ss, int cycles); /* charge n master cycles */

/* ---- routine registry ------------------------------------------------- */
/* A recomped routine: it always handles the call, and is responsible for
 * leaving the CPU where the 65816 routine would have (ss_rts / ss_rtl / an
 * explicit ss_set_pc for a tail jmp). */
typedef void (*RecompFn)(SnesState* ss);

typedef struct RecompEntry {
  uint32_t addr;        /* canonical $C0:0000+offset entry address */
  const char* name;     /* the name in out/symbols.txt */
  RecompFn fn;
} RecompEntry;

/* Called from a file-scope constructor in each recomp/src file; --hook-table all
 * installs everything registered this way. Registering the same address twice
 * is a fatal error. */
void recomp_register(uint32_t entry_addr, const char* name, RecompFn fn);
const RecompEntry* recomp_registry(unsigned* count);

/* The registry entry that owns a running 24-bit pc, or NULL. The pc is folded
 * through the $80/$81 mirror banks onto the canonical $C0:0000+offset form
 * first, so one entry catches the routine however the ROM reached it. */
const RecompEntry* recomp_find(uint32_t pc24);

/* Boilerplate for a file's static table:
 *     static const RecompEntry kEntries[] = { { 0xc0a500, "clear_sprite_table", clear_sprite_table } };
 *     RECOMP_REGISTER(kEntries)
 */
#define RECOMP_REGISTER(tbl)                                              \
  __attribute__((constructor)) static void tbl##_recomp_register(void) {  \
    for(unsigned i_ = 0; i_ < sizeof(tbl) / sizeof((tbl)[0]); i_++)       \
      recomp_register((tbl)[i_].addr, (tbl)[i_].name, (tbl)[i_].fn);      \
  }

/* ---- hook table ------------------------------------------------------- */
/* Return true if the hook ran and set pc/pb; false to let the ROM routine run. */
typedef bool (*RecompHookFn)(SnesState* ss);

typedef struct RecompHook {
  uint32_t addr;        /* 24-bit entry address, e.g. 0xc0a500 */
  RecompHookFn fn;
  const char* name;
} RecompHook;

/* The table the harness installs with --hooks on. Terminated by a {0,NULL,NULL}
 * entry. recomp_hooks_empty is the zero-entry table (--hook-table empty). */
extern const RecompHook recomp_hooks[];
extern const RecompHook recomp_hooks_empty[];

/* Per-entry invocation counters, parallel to the installed table, for the run
 * report. RECOMP_HOOKS_MAX bounds both the registry and the counters. */
#define RECOMP_HOOKS_MAX 256
extern unsigned long recomp_hook_hits[RECOMP_HOOKS_MAX];
unsigned recomp_hooks_count(const RecompHook* table);

#endif
