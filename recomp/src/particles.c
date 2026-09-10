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
 * mode0_particle_draw_dispatch — $C0:9227
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
 * particle_dispatch_noop — $C0:9233
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
 * particle_table_clear — $C0:9246
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

static const RecompEntry kParticles[] = {
  { 0xc09227, "mode0_particle_draw_dispatch", mode0_particle_draw_dispatch },
  { 0xc09233, "particle_dispatch_noop", particle_dispatch_noop },
  { 0xc09246, "particle_table_clear", particle_table_clear },
};
RECOMP_REGISTER(kParticles)
