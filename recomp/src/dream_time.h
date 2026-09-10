/* One 65816 instruction at a time.
 *
 * A C body that just computes costs the emulator nothing, and this ROM notices:
 * the SPC upload handshake counts words in a busy-wait against the APU's own
 * clock, and the joypad wait loop reads a register whose value changes with the
 * beam position. So every converted routine models the instruction stream. The
 * helpers here reproduce one instruction's bus activity each, in the same order
 * and with the same interrupt-latch points the core uses, written against
 * LakeSnes' own opcode implementations (cpu_adrImp, cpu_adrAbs, cpu_adrAbx,
 * cpu_sta, cpu_doBranch and friends). A body built from them costs the emulator
 * exactly what the routine cost, which `dream_harness --profile` confirms by
 * reporting the ROM's and the hook's per-call intervals side by side.
 *
 * The step macros also do the other half of the job. A hook is atomic where the
 * routine it replaces is not, so before each instruction they publish the
 * registers the body is holding in locals and offer the rest of the routine back
 * to the ROM if the machine has moved on underneath (ss_yield_wanted): a frame
 * boundary, or an interrupt the 65816 would service between two instructions.
 * Everything is already exactly what the CPU would have left at that boundary,
 * so the ROM picks the routine up and finishes it.
 *
 * A body therefore reads straight down the listing, one macro per instruction,
 * each naming the address it stands for:
 *
 *     S(0xA1B0, 3);                      lda entity_x        (3 bytes)
 *     a = t_read16(ss, ss_abs(ss, entity_x));
 *     ss_set_nz16(ss, a);
 *     SI(0xA1B3); ss_set_c(ss, true);    sec
 *
 * The macros need `ss`, `pb`, `a`, `x` and `y` in scope; a body that does not
 * track one of the registers initialises it from the CPU and leaves it alone, so
 * publishing it is a no-op. A body reached at two addresses (the two copies of
 * entity_apply_velocity, the two joypad tails) must name its addresses relative
 * to its base, because the yield has to resume in the caller's copy.
 */
#ifndef DREAM_TIME_H
#define DREAM_TIME_H

#include "snes_state.h"

/* One modelled instruction: publish, offer the routine back, then fetch the
 * opcode and its operands. Returns true when it yielded. Setting the pc first
 * means a body needs no thread of fall-through and branch targets through the
 * timing calls; the bytes fetched are the routine's own either way. */
static inline bool t_step(SnesState* ss, uint8_t pb, uint16_t addr, int bytes,
                          uint16_t a, uint16_t x, uint16_t y) {
  ss_set_a(ss, a);
  ss_set_x(ss, x);
  ss_set_y(ss, y);
  ss_set_pc(ss, pb, addr);
  if(ss_yield_wanted(ss)) return true;
  ss_fetch(ss, bytes);
  return false;
}

/* The same, for a 2-cycle implied or accumulator opcode. */
static inline bool t_step_imp(SnesState* ss, uint8_t pb, uint16_t addr,
                              uint16_t a, uint16_t x, uint16_t y) {
  if(t_step(ss, pb, addr, 1, a, x, y)) return true;
  ss_check_int(ss);
  ss_idle(ss);
  return false;
}

/* The same, for sep #imm / rep #imm, flag change included. Applying the change
 * matters because a body can yield: the ROM would pick up the rest of the
 * routine with an 8-bit accumulator or 8-bit index registers, and the p register
 * has to say so. Setting the x flag narrows X and Y, exactly as the CPU does. */
static inline bool t_step_sep(SnesState* ss, uint8_t pb, uint16_t addr, uint8_t bits,
                              uint16_t a, uint16_t x, uint16_t y) {
  if(t_step(ss, pb, addr, 2, a, x, y)) return true;
  ss_check_int(ss);
  ss_idle(ss);
  ss_set_p(ss, (uint8_t) (ss_p(ss) | bits));
  return false;
}

static inline bool t_step_rep(SnesState* ss, uint8_t pb, uint16_t addr, uint8_t bits,
                              uint16_t a, uint16_t x, uint16_t y) {
  if(t_step(ss, pb, addr, 2, a, x, y)) return true;
  ss_check_int(ss);
  ss_idle(ss);
  ss_set_p(ss, (uint8_t) (ss_p(ss) & (uint8_t) ~bits));
  return false;
}

/* The shorthand every body uses. */
#define S(addr, n)   do { if(t_step(ss, pb, (uint16_t) (addr), (n), a, x, y)) return; } while(0)
#define SI(addr)     do { if(t_step_imp(ss, pb, (uint16_t) (addr), a, x, y)) return; } while(0)
#define SEP(addr, b) do { if(t_step_sep(ss, pb, (uint16_t) (addr), (b), a, x, y)) return; } while(0)
#define REP(addr, b) do { if(t_step_rep(ss, pb, (uint16_t) (addr), (b), a, x, y)) return; } while(0)

/* Hand the rest of the routine back to the ROM at `addr` outside a step, for a
 * body that is at an instruction boundary the macros do not cover. */
static inline bool t_yield(SnesState* ss, uint8_t pb, uint16_t addr) {
  if(!ss_yield_wanted(ss)) return false;
  ss_set_pc(ss, pb, addr);
  return true;
}

/* The extra internal cycle absolute-indexed and direct-page-indexed addressing
 * costs. Absolute-indexed pays it on every write, and on a read whenever the
 * index is 16-bit (which is all but one routine here) or the index crosses a
 * page; direct-page-indexed always pays it. */
static inline void t_index(SnesState* ss) { ss_idle(ss); }

/* the data half of a 16-bit store: low byte, latch, high byte */
static inline void t_write16(SnesState* ss, uint32_t adr, uint16_t v) {
  ss_bus_w8(ss, adr, (uint8_t) v);
  ss_check_int(ss);
  ss_bus_w8(ss, (adr + 1) & 0xffffff, (uint8_t) (v >> 8));
}

/* the data half of an 8-bit store: latch, then the byte */
static inline void t_write8(SnesState* ss, uint32_t adr, uint8_t v) {
  ss_check_int(ss);
  ss_bus_w8(ss, adr, v);
}

/* the data half of an 8-bit load */
static inline uint8_t t_read8(SnesState* ss, uint32_t adr) {
  ss_check_int(ss);
  return ss_bus_r8(ss, adr);
}

/* the data half of a 16-bit load: low byte, latch, high byte */
static inline uint16_t t_read16(SnesState* ss, uint32_t adr) {
  uint8_t lo = ss_bus_r8(ss, adr);
  ss_check_int(ss);
  uint8_t hi = ss_bus_r8(ss, (adr + 1) & 0xffffff);
  return (uint16_t) (lo | (hi << 8));
}

/* A conditional branch. The opcode fetch is the step above; this covers the
 * displacement fetch, the latch and, when the branch is taken, the extra
 * internal cycle. The body moves the pc by naming the next address itself. */
static inline void t_branch(SnesState* ss, bool taken) {
  if(!taken) ss_check_int(ss);
  ss_fetch(ss, 1);
  if(taken) {
    ss_check_int(ss);
    ss_idle(ss);
  }
}

#endif
