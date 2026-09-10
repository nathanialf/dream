/* One SPC700 instruction at a time.
 *
 * The sound driver observes its own clock everywhere: the loader and the command
 * port share a handshake counter the 65816 is busy-waiting on, tick_wait spins on
 * timer 0, cmd7 spins on timer 1, and every DSP write lands on a DSP that is
 * ticked once every 32 APU cycles. A C body that just computes would cost the
 * emulator nothing and all of that would move, so every converted routine models
 * the instruction stream.
 *
 * Each helper here reproduces one instruction's access sequence -- reads, writes
 * and idles in the order the core performs them -- written against LakeSnes'
 * own opcode implementations in third_party/lakesnes/snes/spc.c (spc_adrDp,
 * spc_adrDpWord, spc_movs, spc_inc, spc_cmp, spc_doBranch and friends), because
 * one APU cycle is spent by exactly one read, write or idle (apu_spcRead /
 * apu_spcWrite / apu_spcIdle each call apu_cycle once). A body built from them
 * therefore costs the emulator exactly what the routine cost, to the cycle, with
 * no charge to calibrate afterwards.
 *
 * The step macros also do the other half of the job. A hook is atomic where the
 * routine it replaces is not: apu_runCycles() runs whole opcodes until its
 * catch-up budget is spent, so the reference SPC stops between two instructions
 * of a routine at an instant a hooked run cannot. Before each instruction the
 * macros publish the registers the body is holding in locals and offer the rest
 * of the routine back (sps_yield_wanted). Everything is already exactly what the
 * SPC700 would have left at that boundary, so the driver picks the routine up
 * and finishes it.
 *
 * A body reads straight down spc/driver.asm, one macro per instruction, each
 * naming the address it stands for:
 *
 *     S(0x0683, 2);  a = s_load(sp, sps_dp(sp, 0xE9));   mov a,$E9
 *     S(0x0685, 2);  s_cmp(sp, a, s_read(sp, SPS_CPUIO0));  cmp a,!CPUIO0
 *     S(0x0687, 2);  s_branch(sp, sps_z(sp));            beq cmd_receive
 *
 * The macros need `sp`, `a`, `x` and `y` in scope; a body that does not track one
 * of the registers initialises it from the SPC and leaves it alone, so
 * publishing it is a no-op.
 */
#ifndef DREAM_SPC_TIME_H
#define DREAM_SPC_TIME_H

#include <stdint.h>
#include <stdbool.h>

#include "spc_state.h"

/* One modelled instruction: publish, offer the routine back, then fetch the
 * opcode and its operand bytes. Returns true when it yielded. Setting the pc
 * first means a body needs no thread of fall-through and branch targets through
 * the timing calls; the bytes fetched are the routine's own either way.
 *
 * `bytes` is the instruction's full encoded length whenever the core fetches all
 * of it before doing anything else, which is every form this driver uses except
 * dbnz dp,rel (the displacement is read after the read-modify-write) -- see
 * s_dbnz_dp below. */
static inline bool s_step(SpcState* sp, uint16_t addr, int bytes,
                          uint8_t a, uint8_t x, uint8_t y) {
  sps_set_a(sp, a);
  sps_set_x(sp, x);
  sps_set_y(sp, y);
  sps_set_pc(sp, addr);
  if(sps_yield_wanted(sp)) return true;
  sps_fetch(sp, bytes);
  return false;
}

/* The shorthand every body uses. */
#define S(addr, n) do { if(s_step(sp, (uint16_t) (addr), (n), a, x, y)) return; } while(0)

/* Hand the rest of the routine back at `addr` outside a step, for a body at an
 * instruction boundary the macro does not cover (after a callee, say). */
static inline bool s_yield(SpcState* sp, uint16_t addr) {
  if(!sps_yield_wanted(sp)) return false;
  sps_set_pc(sp, addr);
  return true;
}
#define S_YIELD(addr) do { if(s_yield(sp, (uint16_t) (addr))) return; } while(0)

/* Publish the registers the body is holding in locals, without a step: before
 * ending on sps_ret(), and before handing the routine on to another address. */
#define S_PUB()      do { sps_set_a(sp, a); sps_set_x(sp, x); sps_set_y(sp, y); } while(0)
/* Leave through a tail jmp, or fall through into the next routine, at `addr`.
 * The routine that owns `addr` runs next -- its own hook if it has one. */
#define S_GOTO(addr) do { S_PUB(); sps_set_pc(sp, (uint16_t) (addr)); return; } while(0)

/* ---- the cycles an instruction spends after its fetches ----------------- */

/* Every implied one-byte opcode reads the byte at pc and throws it away
 * (spc_read(spc, spc->pc) in each `imp` case), which is its second cycle. */
static inline void s_imp(SpcState* sp) { sps_read8(sp, sps_pc(sp)); }

/* The extra internal cycle dp+X, abs+X, abs+Y and (dp)+Y addressing costs
 * (spc_adrDpx / spc_adrAbx / spc_adrAby / spc_adrIdy each end in spc_idle). */
static inline void s_idx(SpcState* sp) { sps_idle(sp); }

/* A conditional branch: spc_doBranch spends two idles when it is taken. The
 * displacement byte is part of the step's fetch, and the body moves the pc by
 * naming the next address itself. */
static inline void s_branch(SpcState* sp, bool taken) {
  if(taken) { sps_idle(sp); sps_idle(sp); }
}

/* the data half of a load (spc_mov / spc_movx / spc_movy / spc_cmp) */
static inline uint8_t s_read(SpcState* sp, uint16_t adr) { return sps_read8(sp, adr); }

/* the data half of a store: the SPC700 reads the destination first and discards
 * it, then writes (spc_movs / spc_movsx / spc_movsy) */
static inline void s_movs(SpcState* sp, uint16_t adr, uint8_t v) {
  sps_read8(sp, adr);
  sps_write8(sp, adr, v);
}

/* (dp)+Y addressing (spc_adrIdy): the pointer pair is read out of the direct
 * page, then an internal cycle, then Y is added. */
static inline uint16_t s_adr_idy(SpcState* sp, uint8_t off, uint8_t y) {
  uint8_t lo = sps_read8(sp, sps_dp(sp, off));
  uint8_t hi = sps_read8(sp, sps_dp(sp, (uint8_t) (off + 1)));
  sps_idle(sp);
  return (uint16_t) ((lo | (hi << 8)) + y);
}

/* asl a (case 0x1c) */
static inline uint8_t s_asl_a(SpcState* sp, uint8_t a) {
  s_imp(sp);
  sps_set_c(sp, (a & 0x80) != 0);
  a = (uint8_t) (a << 1);
  sps_set_zn(sp, a);
  return a;
}

/* mov a,mem / mov x,mem / mov y,mem: load and set Z/N */
static inline uint8_t s_load(SpcState* sp, uint16_t adr) {
  uint8_t v = sps_read8(sp, adr);
  sps_set_zn(sp, v);
  return v;
}

/* inc mem (spc_inc): read, +1, write, Z/N */
static inline uint8_t s_inc_mem(SpcState* sp, uint16_t adr) {
  uint8_t v = (uint8_t) (sps_read8(sp, adr) + 1);
  sps_write8(sp, adr, v);
  sps_set_zn(sp, v);
  return v;
}

/* cmp a,mem / cmp x,mem / cmp y,mem (spc_cmp): the borrow is carry-set */
static inline void s_cmp(SpcState* sp, uint8_t reg, uint8_t mem) {
  int result = reg + (uint8_t) (mem ^ 0xff) + 1;
  sps_set_c(sp, result > 0xff);
  sps_set_zn(sp, (uint8_t) result);
}

/* cmp mem,#imm (spc_cmpm): read, compare, then an internal cycle before the
 * Z/N pair -- the one compare form in this driver that costs more than its
 * fetches and a read (seq_fir's `cmp !DSPADDR,#$8F`). It writes nothing back. */
static inline void s_cmpm(SpcState* sp, uint16_t dst, uint8_t value) {
  int result = sps_read8(sp, dst) + (uint8_t) (value ^ 0xff) + 1;
  sps_set_c(sp, result > 0xff);
  sps_idle(sp);
  sps_set_zn(sp, (uint8_t) result);
}

/* and a,mem (spc_and) */
static inline uint8_t s_and(SpcState* sp, uint8_t a, uint8_t mem) {
  uint8_t r = (uint8_t) (a & mem);
  sps_set_zn(sp, r);
  return r;
}

/* sbc a,mem (spc_sbc), V/H included exactly as the core computes them */
static inline uint8_t s_sbc(SpcState* sp, uint8_t a, uint8_t mem) {
  uint8_t value = (uint8_t) (mem ^ 0xff);
  int result = a + value + (sps_c(sp) ? 1 : 0);
  sps_set_v(sp, (a & 0x80) == (value & 0x80) && (value & 0x80) != (result & 0x80));
  sps_set_h(sp, ((a & 0xf) + (value & 0xf) + (sps_c(sp) ? 1 : 0)) > 0xf);
  sps_set_c(sp, result > 0xff);
  sps_set_zn(sp, (uint8_t) result);
  return (uint8_t) result;
}

/* adc mem,#imm (spc_adcm): read, add, write back, flags on the result */
static inline void s_adcm(SpcState* sp, uint16_t dst, uint8_t value) {
  uint8_t applyOn = sps_read8(sp, dst);
  int result = applyOn + value + (sps_c(sp) ? 1 : 0);
  sps_set_v(sp, (applyOn & 0x80) == (value & 0x80) && (value & 0x80) != (result & 0x80));
  sps_set_h(sp, ((applyOn & 0xf) + (value & 0xf) + (sps_c(sp) ? 1 : 0)) > 0xf);
  sps_set_c(sp, result > 0xff);
  sps_write8(sp, dst, (uint8_t) result);
  sps_set_zn(sp, (uint8_t) result);
}

/* adc a,mem (spc_adc), V/H exactly as the core computes them */
static inline uint8_t s_adc(SpcState* sp, uint8_t a, uint8_t value) {
  int result = a + value + (sps_c(sp) ? 1 : 0);
  sps_set_v(sp, (a & 0x80) == (value & 0x80) && (value & 0x80) != (result & 0x80));
  sps_set_h(sp, ((a & 0xf) + (value & 0xf) + (sps_c(sp) ? 1 : 0)) > 0xf);
  sps_set_c(sp, result > 0xff);
  sps_set_zn(sp, (uint8_t) result);
  return (uint8_t) result;
}

/* or a,mem (spc_or) */
static inline uint8_t s_or(SpcState* sp, uint8_t a, uint8_t mem) {
  uint8_t r = (uint8_t) (a | mem);
  sps_set_zn(sp, r);
  return r;
}

/* eor a,mem (spc_eor) */
static inline uint8_t s_eor(SpcState* sp, uint8_t a, uint8_t mem) {
  uint8_t r = (uint8_t) (a ^ mem);
  sps_set_zn(sp, r);
  return r;
}

/* dec mem (spc_dec): read, -1, write, Z/N */
static inline uint8_t s_dec_mem(SpcState* sp, uint16_t adr) {
  uint8_t v = (uint8_t) (sps_read8(sp, adr) - 1);
  sps_write8(sp, adr, v);
  sps_set_zn(sp, v);
  return v;
}

/* lsr mem (spc_lsr) */
static inline uint8_t s_lsr_mem(SpcState* sp, uint16_t adr) {
  uint8_t v = sps_read8(sp, adr);
  sps_set_c(sp, (v & 1) != 0);
  v = (uint8_t) (v >> 1);
  sps_write8(sp, adr, v);
  sps_set_zn(sp, v);
  return v;
}

/* ror mem (spc_ror): the old carry becomes bit 7, bit 0 becomes the carry */
static inline uint8_t s_ror_mem(SpcState* sp, uint16_t adr) {
  uint8_t v = sps_read8(sp, adr);
  bool newC = (v & 1) != 0;
  v = (uint8_t) ((v >> 1) | (sps_c(sp) ? 0x80 : 0x00));
  sps_set_c(sp, newC);
  sps_write8(sp, adr, v);
  sps_set_zn(sp, v);
  return v;
}

/* lsr a (case 0x5c) */
static inline uint8_t s_lsr_a(SpcState* sp, uint8_t a) {
  s_imp(sp);
  sps_set_c(sp, (a & 1) != 0);
  a = (uint8_t) (a >> 1);
  sps_set_zn(sp, a);
  return a;
}

/* ror a (case 0x7c) */
static inline uint8_t s_ror_a(SpcState* sp, uint8_t a) {
  s_imp(sp);
  bool newC = (a & 1) != 0;
  a = (uint8_t) ((a >> 1) | (sps_c(sp) ? 0x80 : 0x00));
  sps_set_c(sp, newC);
  sps_set_zn(sp, a);
  return a;
}

/* rol a (case 0x3c): bit 7 becomes the carry, the old carry becomes bit 0 */
static inline uint8_t s_rol_a(SpcState* sp, uint8_t a) {
  s_imp(sp);
  bool newC = (a & 0x80) != 0;
  a = (uint8_t) ((a << 1) | (sps_c(sp) ? 0x01 : 0x00));
  sps_set_c(sp, newC);
  sps_set_zn(sp, a);
  return a;
}

/* xcn a (case 0x9f): the nibble swap, three internal cycles after the dummy
 * read at pc */
static inline uint8_t s_xcn(SpcState* sp, uint8_t a) {
  s_imp(sp);
  sps_idle(sp);
  sps_idle(sp);
  sps_idle(sp);
  a = (uint8_t) ((a >> 4) | (a << 4));
  sps_set_zn(sp, a);
  return a;
}

/* mul ya (case 0xcf): seven internal cycles, Y:A = Y * A, Z/N on the high byte.
 * Returns the product as one 16-bit Y:A value. */
static inline uint16_t s_mul(SpcState* sp, uint8_t a, uint8_t y) {
  s_imp(sp);
  for(int i = 0; i < 7; i++) sps_idle(sp);
  uint16_t result = (uint16_t) (a * y);
  sps_set_zn(sp, (uint8_t) (result >> 8));
  return result;
}

/* div ya,x (case 0x9e): ten internal cycles after the dummy read at pc, then the
 * nine-step division the core performs bit by bit, H from the low nibbles and V
 * from the overflow bit. Returns quotient and remainder as one 16-bit Y:A value
 * (A the quotient, Y the remainder), like s_mul above. */
static inline uint16_t s_div(SpcState* sp, uint8_t a, uint8_t x, uint8_t y) {
  s_imp(sp);
  for(int i = 0; i < 10; i++) sps_idle(sp);
  sps_set_h(sp, (x & 0xf) <= (y & 0xf));
  int yva = (y << 8) | a;
  int xs = x << 9;
  for(int i = 0; i < 9; i++) {
    yva <<= 1;
    yva |= (yva & 0x20000) ? 1 : 0;
    yva &= 0x1ffff;
    if(yva >= xs) yva ^= 1;
    if(yva & 1) yva -= xs;
    yva &= 0x1ffff;
  }
  sps_set_v(sp, (yva & 0x100) != 0);
  sps_set_zn(sp, (uint8_t) (yva & 0xff));
  return (uint16_t) ((yva & 0xff) | ((yva >> 9) << 8));
}

/* dp+X addressing (spc_adrDpx): the operand byte is part of the step's fetch,
 * the index is added inside the page, and the internal cycle follows. */
static inline uint16_t s_adr_dpx(SpcState* sp, uint8_t off, uint8_t x) {
  uint16_t adr = sps_dp(sp, (uint8_t) (off + x));
  sps_idle(sp);
  return adr;
}

/* The dp,dp forms -- mov dp,dp (case 0xfa) and adc dp,dp (case 0x89).
 * spc_adrDpDp reads the *source* operand byte, then the source value, then the
 * destination operand byte, so the step covers the opcode and the source
 * operand only and this supplies the read and the second fetch. */
static inline uint8_t s_dpdp_src(SpcState* sp, uint8_t src) {
  uint8_t v = sps_read8(sp, sps_dp(sp, src));
  sps_fetch(sp, 1);
  return v;
}

/* ---- the word forms, all on a direct-page pair ------------------------- */
/* spc_adrDpWord: the low byte is the dp operand, the high byte is (dp+1) wrapped
 * inside the page -- which is why these take the raw operand byte, not an
 * address. */
static inline uint16_t s_dpw_lo(SpcState* sp, uint8_t off) { return sps_dp(sp, off); }
static inline uint16_t s_dpw_hi(SpcState* sp, uint8_t off) {
  return sps_dp(sp, (uint8_t) (off + 1));
}

/* movw ya,dp (case 0xba): read low, idle, read high */
static inline uint16_t s_movw_load(SpcState* sp, uint8_t off) {
  uint8_t lo = sps_read8(sp, s_dpw_lo(sp, off));
  sps_idle(sp);
  uint16_t v = (uint16_t) (lo | (sps_read8(sp, s_dpw_hi(sp, off)) << 8));
  sps_set_zn16(sp, v);
  return v;
}

/* movw dp,ya (case 0xda): dummy read of the low byte, then both writes. It sets
 * no flags. */
static inline void s_movw_store(SpcState* sp, uint8_t off, uint16_t ya) {
  sps_read8(sp, s_dpw_lo(sp, off));
  sps_write8(sp, s_dpw_lo(sp, off), (uint8_t) ya);
  sps_write8(sp, s_dpw_hi(sp, off), (uint8_t) (ya >> 8));
}

/* addw ya,dp (case 0x7a): read low, idle, read high, then the 16-bit add */
static inline uint16_t s_addw(SpcState* sp, uint8_t off, uint16_t ya) {
  uint8_t lo = sps_read8(sp, s_dpw_lo(sp, off));
  sps_idle(sp);
  uint16_t value = (uint16_t) (lo | (sps_read8(sp, s_dpw_hi(sp, off)) << 8));
  int result = ya + value;
  sps_set_v(sp, (ya & 0x8000) == (value & 0x8000) && (value & 0x8000) != (result & 0x8000));
  sps_set_h(sp, ((ya & 0xfff) + (value & 0xfff)) > 0xfff);
  sps_set_c(sp, result > 0xffff);
  sps_set_zn16(sp, (uint16_t) result);
  return (uint16_t) result;
}

/* subw ya,dp (case 0x9a): read low, idle, read high, then the 16-bit subtract,
 * which the core performs as an add of the complement plus one. */
static inline uint16_t s_subw(SpcState* sp, uint8_t off, uint16_t ya) {
  uint8_t lo = sps_read8(sp, s_dpw_lo(sp, off));
  sps_idle(sp);
  uint16_t value = (uint16_t) ((lo | (sps_read8(sp, s_dpw_hi(sp, off)) << 8)) ^ 0xffff);
  int result = ya + value + 1;
  sps_set_v(sp, (ya & 0x8000) == (value & 0x8000) && (value & 0x8000) != (result & 0x8000));
  sps_set_h(sp, ((ya & 0xfff) + (value & 0xfff) + 1) > 0xfff);
  sps_set_c(sp, result > 0xffff);
  sps_set_zn16(sp, (uint16_t) result);
  return (uint16_t) result;
}

/* decw dp (case 0x1a): read low, write low, read high, write high. The core
 * decrements the low byte first and folds the borrow into the high byte, and the
 * flags are set on the whole word. */
static inline uint16_t s_decw(SpcState* sp, uint8_t off) {
  uint16_t low = s_dpw_lo(sp, off), high = s_dpw_hi(sp, off);
  uint16_t value = (uint16_t) (sps_read8(sp, low) - 1);
  sps_write8(sp, low, (uint8_t) value);
  value = (uint16_t) (value + (sps_read8(sp, high) << 8));
  sps_write8(sp, high, (uint8_t) (value >> 8));
  sps_set_zn16(sp, value);
  return value;
}

/* ---- stack, call and return -------------------------------------------- */
/* push a / push x (cases 0x2d, 0x4d): dummy read at pc, the write, then an idle */
static inline void s_push(SpcState* sp, uint8_t v) {
  sps_read8(sp, sps_pc(sp));
  sps_push8(sp, v);
  sps_idle(sp);
}

/* pop a / pop x (cases 0xae, 0xce): dummy read at pc, an idle, then the pull */
static inline uint8_t s_pop(SpcState* sp) {
  sps_read8(sp, sps_pc(sp));
  sps_idle(sp);
  return sps_pull8(sp);
}

/* dbnz dp,rel (case 0x6e). The displacement byte is read *after* the
 * read-modify-write, so this one cannot be folded into the step's fetch: the
 * step covers the opcode and the dp operand only. Returns the branch condition. */
static inline bool s_dbnz_dp(SpcState* sp, uint8_t off) {
  uint16_t adr = sps_dp(sp, off);
  uint8_t result = (uint8_t) (sps_read8(sp, adr) - 1);
  sps_write8(sp, adr, result);
  sps_fetch(sp, 1);
  bool taken = result != 0;
  s_branch(sp, taken);
  return taken;
}

/* jmp (abs+x) (case 0x1f): idle, then the vector read. The driver's loader uses
 * it on an operand it patched itself, so the vector is read out of ARAM exactly
 * as the ROM reads it back. */
static inline uint16_t s_jmp_iax(SpcState* sp, uint16_t pointer, uint8_t x) {
  sps_idle(sp);
  uint8_t lo = sps_read8(sp, (uint16_t) (pointer + x));
  uint8_t hi = sps_read8(sp, (uint16_t) (pointer + x + 1));
  return (uint16_t) (lo | (hi << 8));
}

#endif
