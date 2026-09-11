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

/* An immediate operand is not fetched by the addressing mode: cpu_adrImm only
 * advances the pc, and the opcode's own data read *is* the operand read. So the
 * latch sits between the two bytes of a 16-bit operand
 * (`cpu_readWord(low, high, true)`) and before the single byte of an 8-bit one
 * (`cpu_checkInt(cpu); cpu_read(cpu, low)`), not after the operand. Every
 * immediate opcode in the core has that shape: lda, ldx, ldy, cmp, cpx, cpy,
 * adc, sbc, and, ora, eor and bit, in both widths.
 *
 * In normal operation this ROM takes its NMI while the CPU is parked on the wai
 * at $C0:A4FD, and cpu_runNonInstruction's waiting branch tests nmiWanted rather
 * than intWanted, so the park path never notices the difference. It matters when
 * a frame's work overruns into vblank, which is exactly when timing is already
 * marginal, so the port models it everywhere rather than only where a divergence
 * has been observed. */
#define SIMM16(addr) do { if(t_step(ss, pb, (uint16_t) (addr), 2, a, x, y)) return; \
                          ss_check_int(ss); ss_fetch(ss, 1); } while(0)
#define SIMM8(addr)  do { if(t_step(ss, pb, (uint16_t) (addr), 1, a, x, y)) return; \
                          ss_check_int(ss); ss_fetch(ss, 1); } while(0)

/* jmp abs. LakeSnes' case 0x4c is `cpu->pc = cpu_readOpcodeWord(cpu, true);`, so the
 * two operand bytes are fetched with a cpu_checkInt() between them; a plain three-byte
 * step issues all three fetches with no latch, and an NMI that rises during the operand
 * is then seen one instruction late. That matters most for a bare tail jump, where the
 * body sets the pc and returns and nothing later re-latches. The body names the
 * destination itself, so this only spends the instruction.
 *
 * `jmp (abs,X)` and `jmp (abs)` are not this shape: the core fetches their pointer with
 * intCheck false and takes the latch on the indirect read instead. */
#define SJMP(addr)   do { if(t_step(ss, pb, (uint16_t) (addr), 2, a, x, y)) return; \
                          ss_check_int(ss); ss_fetch(ss, 1); } while(0)

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

/* ---- the direct page ---------------------------------------------------
 * A direct-page effective address is formed in 16 bits and wraps there. LakeSnes'
 * cpu_adrDp is `*low = (cpu->dp + adr) & 0xffff` with the high byte at
 * `(cpu->dp + adr + 1) & 0xffff`, and cpu_adrDpx adds the index inside the same
 * mask, so a 16-bit access whose low byte is at $FFFF takes its high byte from
 * $0000 rather than from bank 1. t_read16 and t_write16 above are the absolute
 * forms: they carry into the bank byte, which is right there and wrong here.
 *
 * This ROM keeps D at $0000 and every direct-page offset is one byte, so nothing
 * wraps today and dream_alu.h records why. These exist so that the masking lives
 * in one place instead of being remembered at six hundred call sites: before
 * them, 164 sites added the offset unmasked and 388 cast, and a routine converted
 * later that runs with D set would have inherited whichever the author copied.
 * Every direct-page access in recomp/src goes through one of them, either as the
 * accessor or, where a body's own read-modify-write helper takes a formed address,
 * as t_dp() on the way in. The helpers that are shared with absolute callers
 * (t_rmw_r16 / t_rmw_w16 in entities_ai.c, anim_scripts.c, mode_init.c,
 * sound_iface.c and top_level.c, t_inc16 in top_level.c) keep the 24-bit carry
 * their absolute callers need, so the one case they would still get wrong is a
 * 16-bit read-modify-write whose low byte lands exactly on $FFFF.
 */
static inline uint32_t t_dp(uint16_t dp, uint32_t off) {
  return (uint32_t) (uint16_t) (dp + off);
}

static inline uint8_t t_read8_dp(SnesState* ss, uint16_t dp, uint32_t off) {
  ss_check_int(ss);
  return ss_bus_r8(ss, t_dp(dp, off));
}

static inline uint16_t t_read16_dp(SnesState* ss, uint16_t dp, uint32_t off) {
  uint8_t lo = ss_bus_r8(ss, t_dp(dp, off));
  ss_check_int(ss);
  uint8_t hi = ss_bus_r8(ss, t_dp(dp, off + 1));
  return (uint16_t) (lo | (hi << 8));
}

static inline void t_write8_dp(SnesState* ss, uint16_t dp, uint32_t off, uint8_t v) {
  ss_check_int(ss);
  ss_bus_w8(ss, t_dp(dp, off), v);
}

static inline void t_write16_dp(SnesState* ss, uint16_t dp, uint32_t off, uint16_t v) {
  ss_bus_w8(ss, t_dp(dp, off), (uint8_t) v);
  ss_check_int(ss);
  ss_bus_w8(ss, t_dp(dp, off + 1), (uint8_t) (v >> 8));
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
