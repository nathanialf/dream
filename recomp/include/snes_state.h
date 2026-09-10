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

/* Per-entry invocation counters, parallel to recomp_hooks, for the run report. */
extern unsigned long recomp_hook_hits[];
unsigned recomp_hooks_count(const RecompHook* table);

#endif
