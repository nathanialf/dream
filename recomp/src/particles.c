/* Particle / weather subsystem entry points, bank $C0.
 *
 * The particle slot table lives in bank $7F ($7F0906, 40 words), outside the
 * lockstep-compared WRAM mirror only in the sense that it is the upper 64 KB;
 * the harness compares all 128 KB, so it is covered.
 */
#include <stdint.h>
#include <stdbool.h>

#include "snes_state.h"
#include "dream_ram.h"
#include "dream_alu.h"
#include "dream_time.h"

/* ---------------------------------------------------------------------------
 * mode0_particle_draw_dispatch: $C0:9227
 *
 * One entry of jtbl_C0828A (game mode 0). A tail jump, so the routine leaves no
 * return of its own: execution continues at particle_update_and_draw_mode0 and
 * that routine's rts returns to the jsr (jtbl_C0828A,X) at $C0823C. The program
 * bank is unchanged by a jmp abs, so the pc keeps whichever mirror bank the
 * caller was running in.
 * ------------------------------------------------------------------------- */
void mode0_particle_draw_dispatch(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  S(0x9227, 3);                             /* C09227 jmp particle_update_and_draw_mode0 */
  ss_set_pc(ss, pb, 0x9331);
}

/* ---------------------------------------------------------------------------
 * particle_dispatch_noop: $C0:9233
 *
 * The do-nothing entry of jtbl_C08282 and jtbl_C0828A. Registers and flags pass
 * through untouched.
 * ------------------------------------------------------------------------- */
void particle_dispatch_noop(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  S(0x9233, 1);                             /* C09233 rts */
  ss_rts(ss);
}

/* ---------------------------------------------------------------------------
 * particle_table_clear: $C0:9246
 *
 * Zeroes the 40 particle slot words at $7F:0906. tdc loads the accumulator from
 * the direct page register rather than an immediate, which is zero throughout
 * this game, and sets N/Z on the 16-bit result.
 * Exit: A = D, X = $FFFE, N set (the dex that ended the loop), Z clear.
 *
 * This one is written cycle-faithfully (dream_time.h) rather than left to the
 * mean charge in config/recomp_cycles.txt: mode0_level_init calls it in the
 * middle of the SPC block upload, and the handshake counter at $0009 is sampled
 * by the lockstep check every frame, so a few dozen cycles of slack shows up as
 * a one-word difference in a partly-finished upload.
 * ------------------------------------------------------------------------- */
void particle_table_clear(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);

  ss_fetch(ss, 3);                          /* C09246 ldx #$004E */
  x = 0x004E;
  ss_set_nz16(ss, x);

  SI(0x9249);                               /* C09249 tdc */
  a = ss_dp(ss);
  ss_set_nz16(ss, a);

  for(;;) {
    S(0x924A, 4);                           /* C0924A sta $7F0906,X */
    t_write16(ss, (particle_table + x) & 0xffffff, a);

    SI(0x924E); x = alu_dec16(ss, x);       /* C0924E dex */
    SI(0x924F); x = alu_dec16(ss, x);       /* C0924F dex */

    ss_fetch(ss, 1);                        /* C09250 bpl loc_C0924A */
    const bool taken = (x & 0x8000) == 0;
    t_branch(ss, taken);
    if(!taken) break;
    ss_set_pc(ss, pb, 0x924A);
  }

  ss_set_a(ss, a);
  ss_set_x(ss, x);
  ss_fetch(ss, 1);                          /* C09252 rts: opcode fetch */
  ss_rts(ss);
}

/* ---------------------------------------------------------------------------
 * unused_stream_desc_dispatch: $C0:9206
 *
 * A jump-table dispatcher nothing calls: no jsr, jsl or table word anywhere in
 * out/dream.asm names the address, and config/recomp_order.txt marks it cold.
 * It adds the accumulator to the word at $0BB8, uses the sum as a byte index
 * into the word table at $C0:B208, and, when the entry is non-zero, parks
 * it in $04, reads and post-increments a second counter at $0BBA, and leaves
 * through `jmp ($0004)`, so the table entry is the routine that runs next. A
 * zero entry ends the table and the routine returns instead.
 *
 * What it reads is not a jump table any more: $C0:B208 is
 * vram_stream_desc_table, the VRAM streaming-descriptor array (docs/NOTES.md,
 * "Open items" 1), so the words it would jump to ($5000, $8AC0, $FFC9, ...)
 * are descriptor bytes. Nothing sets $0BB8 or $0BBA either, so this is dead code
 * rather than an unreached branch, and the unit gate therefore seeds both exits
 * explicitly instead of leaving them to a script (config/recomp_units.txt,
 * `dream_harness --unit`).
 *
 * The ten bytes after the rts ($C0:921D-$C0:9226) decode as `inc $0BB8 ; inc
 * $0BB8 ; stz $0BBA ; rts`; the tracer reaches none of them, so they stay a
 * byte run in out/dream.asm.
 *
 * Entry: A = the table offset to add, DB = the bank the counters live in.
 * Exit: through the table entry with A = the old $0BBA and X = the index, or
 * rts with A = 0.
 * ------------------------------------------------------------------------- */
void unused_stream_desc_dispatch(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint16_t dp = ss_dp(ss);

  SI(0x9206); ss_set_c(ss, false);          /* C09206 clc */
  S(0x9207, 3);                             /* C09207 adc $0BB8 */
  a = alu_adc16(ss, a, t_read16(ss, ss_abs(ss, 0x0BB8)));
  SI(0x920A); x = a; ss_set_nz16(ss, x);    /* C0920A tax */

  S(0x920B, 4);                             /* C0920B lda data_C0B208,X */
  a = t_read16(ss, (0x80B208u + x) & 0xffffff);
  ss_set_nz16(ss, a);
  S(0x920F, 1); t_branch(ss, a == 0);       /* C0920F beq loc_C0921C */
  if(a == 0) {
    ss_set_a(ss, a); ss_set_x(ss, x);
    S(0x921C, 1);                           /* C0921C rts */
    ss_rts(ss);
    return;
  }

  S(0x9211, 2);                             /* C09211 sta ptr_04 */
  t_write16(ss, dp + ptr_04, a);
  S(0x9213, 3);                             /* C09213 lda $0BBA */
  a = t_read16(ss, ss_abs(ss, 0x0BBA));
  ss_set_nz16(ss, a);

  S(0x9216, 3);                             /* C09216 inc $0BBA */
  /* a 16-bit read-modify-write: no latch inside the read, an internal cycle
   * between read and write, and the write-back high byte first */
  { const uint32_t adr = ss_abs(ss, 0x0BBA);
    const uint8_t lo = ss_bus_r8(ss, adr);
    const uint8_t hi = ss_bus_r8(ss, (adr + 1) & 0xffffff);
    const uint16_t v = (uint16_t) ((lo | (hi << 8)) + 1);
    ss_idle(ss);
    ss_bus_w8(ss, (adr + 1) & 0xffffff, (uint8_t) (v >> 8));
    ss_check_int(ss);
    ss_bus_w8(ss, adr, (uint8_t) v);
    ss_set_nz16(ss, v); }

  ss_set_a(ss, a); ss_set_x(ss, x);
  S(0x9219, 3);                             /* C09219 jmp ($0004) */
  /* the vector is read out of bank 0, low byte, latch, high byte */
  { const uint8_t lo = ss_bus_r8(ss, 0x000004);
    ss_check_int(ss);
    const uint8_t hi = ss_bus_r8(ss, 0x000005);
    ss_set_pc(ss, pb, (uint16_t) (lo | (hi << 8))); }
}

static const RecompEntry kParticles[] = {
  { 0xc09206, "unused_stream_desc_dispatch", unused_stream_desc_dispatch },
  { 0xc09227, "mode0_particle_draw_dispatch", mode0_particle_draw_dispatch },
  { 0xc09233, "particle_dispatch_noop", particle_dispatch_noop },
  { 0xc09246, "particle_table_clear", particle_table_clear },
};
RECOMP_REGISTER(kParticles)
