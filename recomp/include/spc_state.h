/* spc_state.h — the recomp's view of the reference SPC700.
 *
 * The 65816 half of the port is written against snes_state.h; this is the same
 * idea one processor down. An SPC recomp hook is a C function that stands in for
 * one routine of the sound driver in spc/driver.asm. It runs instead of the
 * routine's first instruction, with full access to the APU's registers, its 64 KB
 * of ARAM, the DSP and the timers, and is responsible for leaving the pc where
 * the routine would have left it (normally by calling sps_ret(), or by pointing
 * the pc at a tail jmp's target).
 *
 * Three things are different from the 65816 side, and they shape the whole API.
 *
 *   Cycles are the only clock. The SPC700 has no interrupts here; what the
 *   driver observes is its own timers, the DSP's 32-cycle tick and the four
 *   ports the 65816 writes. Every one of those moves on APU cycles, and an APU
 *   cycle is spent by exactly one thing: a read, a write or an idle
 *   (apu_spcRead / apu_spcWrite / apu_spcIdle each call apu_cycle once). So a
 *   body that replays a routine's access sequence in order costs the emulator
 *   exactly what the routine cost, to the cycle, and there is no separate
 *   "charge" to calibrate. sps_read8 / sps_write8 / sps_idle / sps_fetch are
 *   those three primitives; sps_aram_* are the untimed escape hatch.
 *
 *   The yield boundary is the catch-up slice, not the frame. The SPC does not
 *   run alongside the 65816: snes_catchupApu() hands apu_runCycles() a budget
 *   and it runs whole opcodes until the budget is spent, which happens at the
 *   end of a frame and before every read or write of $2140-$217F. The reference
 *   SPC therefore stops between two instructions in the middle of a routine, at
 *   an instant a hooked run would have to run the routine to its end. That is
 *   the same atomicity problem the 65816 side solves at a frame boundary, and it
 *   has the same solution: sps_yield_wanted() reports that the slice is spent,
 *   and a body that models the instruction stream points the pc at the next
 *   instruction and returns. The driver picks the routine up and finishes it.
 *
 *   The DSP is shared state, not a register file. Every DSP access goes through
 *   $F2/$F3 and therefore through the emulated DSP, so audio state (envelopes,
 *   key-on latches, the echo buffer) evolves exactly as it did. Write DSP
 *   registers with sps_write8(SPS_DSPADDR/SPS_DSPDATA, ...) where the ROM does;
 *   sps_dsp_read/sps_dsp_write are untimed inspection only.
 *
 * See recomp/README.md for the lockstep protocol, which compares ARAM, the 128
 * DSP registers and the SPC registers after every frame.
 */
#ifndef DREAM_SPC_STATE_H
#define DREAM_SPC_STATE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct SpcState SpcState;

/* The SPC700's memory-mapped registers, by the names spc/driver.asm uses. */
enum {
  SPS_TEST = 0xf0, SPS_CONTROL = 0xf1, SPS_DSPADDR = 0xf2, SPS_DSPDATA = 0xf3,
  SPS_CPUIO0 = 0xf4, SPS_CPUIO1 = 0xf5, SPS_CPUIO2 = 0xf6, SPS_CPUIO3 = 0xf7,
  SPS_AUXIO4 = 0xf8, SPS_AUXIO5 = 0xf9,
  SPS_T0TARGET = 0xfa, SPS_T1TARGET = 0xfb, SPS_T2TARGET = 0xfc,
  SPS_T0OUT = 0xfd, SPS_T1OUT = 0xfe, SPS_T2OUT = 0xff
};

/* ---- registers -------------------------------------------------------- */
uint8_t  sps_a(const SpcState* sp);
uint8_t  sps_x(const SpcState* sp);
uint8_t  sps_y(const SpcState* sp);
uint8_t  sps_sp(const SpcState* sp);
uint16_t sps_pc(const SpcState* sp);
uint16_t sps_ya(const SpcState* sp);              /* Y:A as one 16-bit value */
uint8_t  sps_psw(const SpcState* sp);             /* nvpbhizc, as push psw would */

void sps_set_a(SpcState* sp, uint8_t v);
void sps_set_x(SpcState* sp, uint8_t v);
void sps_set_y(SpcState* sp, uint8_t v);
void sps_set_sp(SpcState* sp, uint8_t v);
void sps_set_pc(SpcState* sp, uint16_t v);
void sps_set_ya(SpcState* sp, uint16_t v);
void sps_set_psw(SpcState* sp, uint8_t v);

/* ---- flags ------------------------------------------------------------ */
bool sps_c(const SpcState* sp);
bool sps_z(const SpcState* sp);
bool sps_v(const SpcState* sp);
bool sps_n(const SpcState* sp);
bool sps_i(const SpcState* sp);
bool sps_h(const SpcState* sp);
bool sps_p(const SpcState* sp);   /* direct page select: 0 = $00xx, 1 = $01xx */
bool sps_b(const SpcState* sp);
void sps_set_c(SpcState* sp, bool v);
void sps_set_z(SpcState* sp, bool v);
void sps_set_v(SpcState* sp, bool v);
void sps_set_n(SpcState* sp, bool v);
void sps_set_h(SpcState* sp, bool v);
void sps_set_p(SpcState* sp, bool v);
void sps_set_zn(SpcState* sp, uint8_t v);          /* the 8-bit Z/N pair */
void sps_set_zn16(SpcState* sp, uint16_t v);       /* the word forms: movw, addw */

/* The direct-page address of `off`, i.e. what "mov a,$34" resolves to: the P
 * flag selects page $00 or $01, exactly as spc_adrDp() does. */
uint16_t sps_dp(const SpcState* sp, uint8_t off);

/* ---- ARAM, untimed ---------------------------------------------------- */
/* The raw 64 KB. These bypass the $F0-$FF register block and the IPL ROM
 * overlay at $FFC0, so they are for bulk work and inspection; a routine's own
 * loads and stores belong on the timed path below. */
uint8_t  sps_aram_r8(const SpcState* sp, uint16_t adr);
uint16_t sps_aram_r16(const SpcState* sp, uint16_t adr);
void     sps_aram_w8(SpcState* sp, uint16_t adr, uint8_t v);
void     sps_aram_w16(SpcState* sp, uint16_t adr, uint16_t v);

/* ---- memory, timed (one SPC cycle each) ------------------------------- */
/* These are apu_spcRead / apu_spcWrite / apu_spcIdle, the very handlers the SPC
 * core is built on, so they charge one APU cycle each, tick the timers and the
 * DSP at the same instants, and see the register block, the port latches and the
 * IPL ROM exactly as the routine did. */
uint8_t sps_read8(SpcState* sp, uint16_t adr);
void    sps_write8(SpcState* sp, uint16_t adr, uint8_t v);
void    sps_idle(SpcState* sp);
void    sps_fetch(SpcState* sp, int bytes);   /* n opcode/operand reads at pc, pc += n */

/* ---- stack ------------------------------------------------------------ */
/* Page $01, descending, exactly as spc_pushByte / spc_pullByte. Each byte costs
 * one cycle. */
void    sps_push8(SpcState* sp, uint8_t v);
uint8_t sps_pull8(SpcState* sp);
void    sps_push16(SpcState* sp, uint16_t v);  /* high byte first, like the core */
uint16_t sps_pull16(SpcState* sp);

/* ---- returns ---------------------------------------------------------- */
/* The `ret` opcode minus its own opcode fetch, which the body supplies: the
 * dummy read at pc, one idle, then pc = pull16. Five cycles all told, the same
 * as spc.c case 0x6f. */
void sps_ret(SpcState* sp);

/* ---- DSP -------------------------------------------------------------- */
/* Untimed inspection of the emulated DSP's 128 registers, and of the $F2 latch.
 * A routine's own DSP traffic goes through sps_write8(SPS_DSPADDR/SPS_DSPDATA),
 * which is what makes the write land on the DSP at the cycle it landed before. */
uint8_t sps_dsp_addr(const SpcState* sp);
uint8_t sps_dsp_read(const SpcState* sp, uint8_t reg);   /* reg 0..$7F */
void    sps_dsp_write(SpcState* sp, uint8_t reg, uint8_t v);

/* ---- ports ------------------------------------------------------------ */
/* Port i of $F4-$F7. `in` is what the 65816 last wrote (what the SPC reads),
 * `out` what the SPC last wrote (what the 65816 reads). Untimed; the handshake
 * itself uses sps_read8/sps_write8 on SPS_CPUIO0.. so that the wait loop is
 * spent on the APU clock the 65816 is counting against. */
uint8_t sps_port_in(const SpcState* sp, int i);
uint8_t sps_port_out(const SpcState* sp, int i);
void    sps_set_port_out(SpcState* sp, int i, uint8_t v);

/* ---- timers ----------------------------------------------------------- */
/* i is 0..2. `counter` is the 4-bit value $FD-$FF returns and clears; reading it
 * here does not clear it, so a body can look without disturbing the driver. */
uint8_t sps_timer_target(const SpcState* sp, int i);
uint8_t sps_timer_counter(const SpcState* sp, int i);
uint8_t sps_timer_divider(const SpcState* sp, int i);
bool    sps_timer_enabled(const SpcState* sp, int i);

/* ---- cycle accounting ------------------------------------------------- */
/* APU cycles since reset, the clock everything above moves on. */
uint32_t sps_cycles(const SpcState* sp);
/* Spend n cycles as idles. Only for a body that is not modelling its own
 * instruction stream; every routine in recomp/spc/ pays through the accessors. */
void sps_consume_cycles(SpcState* sp, int cycles);
/* What the core's own opcode implementation costs for `opcode`, in APU cycles,
 * for the encodings the driver uses (the standard SPC700 timing table; a taken
 * branch costs two more, which is not in this figure). Returns 0 for an opcode
 * the table does not cover. This is a cross-check for a modelled body, not the
 * mechanism: see recomp/spc/spc_time.h. */
int sps_op_cycles(uint8_t opcode);

/* ---- calling a routine that is not converted yet ---------------------- */
/* Run the reference SPC over a callee the recomp does not implement, then come
 * back to C. sps_call pushes the return address the `call` opcode would have
 * pushed, points the pc at the callee and runs it until the stack pointer is
 * back above the frame; any hook the callee itself hits still fires, so a callee
 * that gets converted later needs no change at the call site.
 *
 * A/X/Y/flags are the caller's on entry and the callee's on return, as with a
 * real `call`; the five cycles the `call` opcode spends after its three fetches
 * are charged here, so a modelled body pays for the call by stepping the opcode
 * and then calling this.
 *
 * It does not stop at a slice boundary, so it is only for a callee short enough
 * that overrunning the catch-up budget cannot matter -- the SPC would otherwise
 * get ahead of the 65816 for the length of the callee. Every body in recomp/spc
 * uses sps_run_callee below instead, which is the same thing built by hand and
 * able to stop. */
void sps_call(SpcState* sp, uint16_t retAddr, uint16_t callee);

/* The primitive underneath, for a body that has already built the frame: run
 * until the stack pointer is back above spBefore. */
void sps_run_until_return(SpcState* sp, uint8_t spBefore);

/* The same, but it also stops when the catch-up slice ends underneath the hook
 * (sps_yield_wanted) and reports that by returning true. Only safe when the
 * frame that was pushed is the routine's *real* return address, because the
 * callee is left running: its own `ret` then lands where the driver expects and
 * the driver finishes the routine. dsp_init executes some 360 instructions
 * and cmd7's timer wait spins for about 25 600 APU cycles, so without this a hook would be atomic
 * across slice boundaries the reference run stops at. */
bool sps_run_callee(SpcState* sp, uint8_t spBefore);

/* ---- yielding back to the driver -------------------------------------- */
/* True once the catch-up slice this hook was entered in has been spent.
 *
 * apu_runCycles() runs whole opcodes until its budget is gone, so the reference
 * SPC stops between two instructions of a routine; a hook is atomic where the
 * routine it replaces is not, and the two runs would then be compared at
 * different points of the same routine. A body that models the instruction
 * stream can simply stop: every register, flag and byte of ARAM is already what
 * the SPC700 would have left at that boundary, so setting the pc to the address
 * of the next instruction and returning hands the rest of the routine to the
 * driver.
 *
 * It is false at a hook's first instruction by construction -- apu_runCycles()
 * only calls spc_runOpcode() while the budget is unspent -- so a body always
 * makes progress, which is what keeps that loop from spinning. recomp/spc's step
 * macros check it before *every* instruction, so a hook is never atomic over
 * more than one of them. */
bool sps_yield_wanted(const SpcState* sp);

/* ---- running with no CPU (--no-cpu) ------------------------------------- *
 *
 * The driver's C bodies as the program: the catch-up loop in the APU drives
 * them instead of the SPC700's instruction fetch, and a pc with no body is a
 * fatal error. Enable it once, after the SPC hook table is installed; the
 * 65816's twin is ss_nocpu_enable() in snes_state.h and both must be on.
 *
 * The SPC700's IPL boot ROM is the one exception: it is the console's firmware,
 * runs only to receive the loader block at power-on, and has no body. Those
 * instructions still execute on the core and are counted separately. */
void sps_nocpu_enable(SpcState* sp, bool on);
bool sps_nocpu_enabled(const SpcState* sp);

/* ---- routine registry ------------------------------------------------- */
/* An SPC recomped routine: it always handles the call, and is responsible for
 * leaving the pc where the SPC700 routine would have (sps_ret, or an explicit
 * sps_set_pc for a tail jmp or a fall-through hand-off to the next routine). */
typedef void (*SpcRecompFn)(SpcState* sp);

typedef struct SpcRecompEntry {
  uint16_t addr;        /* entry address in the SPC's own 64 KB */
  const char* name;     /* the label in spc/driver.asm */
  SpcRecompFn fn;
} SpcRecompEntry;

/* Called from a file-scope constructor in each recomp/spc file; --spc-hooks on
 * installs everything registered this way. Registering an address twice is a
 * fatal error. */
void recomp_spc_register(uint16_t entry_addr, const char* name, SpcRecompFn fn);
const SpcRecompEntry* recomp_spc_registry(unsigned* count);

/* Boilerplate for a file's static table:
 *     static const SpcRecompEntry kEntries[] = { { 0x103e, "dsp_init", dsp_init } };
 *     RECOMP_SPC_REGISTER(kEntries)
 */
#define RECOMP_SPC_REGISTER(tbl)                                              \
  __attribute__((constructor)) static void tbl##_recomp_spc_register(void) {  \
    for(unsigned i_ = 0; i_ < sizeof(tbl) / sizeof((tbl)[0]); i_++)           \
      recomp_spc_register((tbl)[i_].addr, (tbl)[i_].name, (tbl)[i_].fn);      \
  }

/* Per-entry invocation counters, parallel to the installed table, for the run
 * report. RECOMP_SPC_HOOKS_MAX bounds both the registry and the counters. */
#define RECOMP_SPC_HOOKS_MAX 128
extern unsigned long recomp_spc_hook_hits[RECOMP_SPC_HOOKS_MAX];

#endif
