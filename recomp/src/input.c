/* Controller reading, bank $C0.
 *
 * $8A/$8C hold player 1's held and newly-pressed button words, $8E/$90 the same
 * for player 2. All four are what the rest of the game tests.
 *
 * Of every routine in the port this is the one that most needs its timing to be
 * the ROM's: it opens by polling HVBJOY until the auto-joypad read finishes, and
 * bit 6 of that register is the hblank flag, so what the last poll returns
 * depends on where the beam is when it happens. The V flag that poll leaves
 * behind survives into the flags the next NMI pushes, which the lockstep check
 * compares. Polling at the C body's natural speed rather than the ROM's exits
 * the loop at a different point in the scanline, and the pushed flags differ.
 */
#include <stdint.h>
#include <stdbool.h>

#include "snes_state.h"
#include "dream_ram.h"
#include "dream_alu.h"
#include "dream_time.h"

/* One player's tail ($C0:A307 for player 1, the same shape $2A bytes later for
 * player 2): if the pad reports a non-standard device id (the low three bits of
 * the held word), drain sixteen manual shifts out of the serial port and throw
 * the reading away; otherwise take one shift and require its bit 0 to be set.
 * Either way a pad that fails the check leaves both words zeroed.
 *
 * The step macros in dream_time.h end a body with a plain `return`, so this one
 * is void and reports a yield through *yielded. SY and friends are the same
 * steps with the accumulator handed back to the caller first. */
#define SY(addr, n)   do { if(t_step(ss, pb, (uint16_t) (addr), (n), a, x, y)) \
                             { *ap = a; *yielded = true; return; } } while(0)
#define SYI(addr)     do { if(t_step_imp(ss, pb, (uint16_t) (addr), a, x, y)) \
                             { *ap = a; *yielded = true; return; } } while(0)
#define SYSEP(addr,b) do { if(t_step_sep(ss, pb, (uint16_t) (addr), (b), a, x, y)) \
                             { *ap = a; *yielded = true; return; } } while(0)
#define SYREP(addr,b) do { if(t_step_rep(ss, pb, (uint16_t) (addr), (b), a, x, y)) \
                             { *ap = a; *yielded = true; return; } } while(0)

static void read_joypads_check(SnesState* ss, uint8_t pb, uint16_t base, uint16_t* ap,
                               uint16_t held, uint16_t pressed, uint16_t serial,
                               bool* yielded) {
  uint16_t x = ss_x(ss), y = ss_y(ss);
  const uint32_t db = (uint32_t) ss_db(ss) << 16;
  uint16_t a = *ap;

  SY(base + 0x00, 2);                       /* C0A307 lda $8A */
  a = t_read16(ss, ss_dp(ss) + held);
  ss_set_nz16(ss, a);
  SY(base + 0x02, 3);                       /* C0A309 and #$0007 */
  a = alu_and16(ss, a, 0x0007);
  SY(base + 0x05, 1);                       /* C0A30C beq loc_C0A321 */
  t_branch(ss, a == 0);

  if(a != 0) {
    SYSEP(base + 0x07, 0x20);               /* C0A30E sep #$20 */
    SY(base + 0x09, 3);                     /* C0A310 ldy #$0010 */
    y = 0x0010;
    ss_set_nz16(ss, y);
    do {
      SY(base + 0x0C, 3);                   /* C0A313 lda JOYSER0 */
      uint8_t v = t_read8(ss, db | serial);
      a = (uint16_t) ((a & 0xff00) | v);
      ss_set_nz8(ss, v);
      SYI(base + 0x0F); /* C0A316 dey */
      y = alu_dec16(ss, y);
      SY(base + 0x10, 1);                   /* C0A317 bne loc_C0A313 */
      t_branch(ss, y != 0);
    } while(y != 0);
    ss_set_y(ss, y);
    SYREP(base + 0x12, 0x20);               /* C0A319 rep #$20 */
    SY(base + 0x14, 2);                     /* C0A31B stz $8A */
    t_write16(ss, ss_dp(ss) + held, 0);
    SY(base + 0x16, 2);                     /* C0A31D stz $8C */
    t_write16(ss, ss_dp(ss) + pressed, 0);
    SY(base + 0x18, 1);                     /* C0A31F bra loc_C0A331 */
    t_branch(ss, true);
    *ap = a;
    return;
  }

  SYSEP(base + 0x1A, 0x20);                 /* C0A321 sep #$20 */
  SY(base + 0x1C, 3);                       /* C0A323 lda JOYSER0 */
  uint8_t v = t_read8(ss, db | serial);
  a = (uint16_t) ((a & 0xff00) | v);
  ss_set_nz8(ss, v);
  SYREP(base + 0x1F, 0x20);                 /* C0A326 rep #$20 */
  SY(base + 0x21, 3);                       /* C0A328 bit #$0001 */
  alu_bit_imm16(ss, a, 0x0001);
  SY(base + 0x24, 1);                       /* C0A32B bne loc_C0A331 */
  t_branch(ss, (a & 0x0001) != 0);
  if((a & 0x0001) != 0) { *ap = a; return; }

  SY(base + 0x26, 2);                       /* C0A32D stz $8A */
  t_write16(ss, ss_dp(ss) + held, 0);
  SY(base + 0x28, 2);                       /* C0A32F stz $8C */
  t_write16(ss, ss_dp(ss) + pressed, 0);
  *ap = a;
}

#undef SY
#undef SYI
#undef SYSEP
#undef SYREP

/* ---------------------------------------------------------------------------
 * read_joypads — $C0:A2DE
 *
 * Waits out the auto-joypad read, then latches held and newly-pressed words for
 * both pads. Newly-pressed is the classic (new ^ old) & new.
 * Exit: $8A/$8C/$8E/$90 updated; A, Y and the flags as the last executed
 * instruction of whichever tail path ran left them.
 * ------------------------------------------------------------------------- */
void read_joypads(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint32_t db = (uint32_t) ss_db(ss) << 16;
  const uint16_t dp = ss_dp(ss);

  SEP(0xA2DE, 0x20);                        /* C0A2DE sep #$20 */
  S(0xA2E0, 2);                             /* C0A2E0 lda #$01 */
  a = (uint16_t) ((a & 0xff00) | 0x01);
  ss_set_nz8(ss, 0x01);

  for(;;) {
    S(0xA2E2, 3);                           /* C0A2E2 bit HVBJOY */
    uint8_t hv = t_read8(ss, db | HVBJOY);
    alu_bit8(ss, (uint8_t) a, hv);
    const bool busy = ((uint8_t) a & hv) != 0;
    S(0xA2E5, 1);                           /* C0A2E5 bne loc_C0A2E2 */
    t_branch(ss, busy);
    if(!busy) break;
  }
  REP(0xA2E7, 0x20);                        /* C0A2E7 rep #$20 */

  S(0xA2E9, 3);                             /* C0A2E9 lda JOY1L */
  a = t_read16(ss, db | JOY1L); ss_set_nz16(ss, a);
  S(0xA2EC, 2);                             /* C0A2EC eor $8A */
  a = alu_eor16(ss, a, t_read16(ss, dp + joy1_held));
  S(0xA2EE, 3);                             /* C0A2EE and JOY1L */
  a = alu_and16(ss, a, t_read16(ss, db | JOY1L));
  S(0xA2F1, 2);                             /* C0A2F1 sta $8C */
  t_write16(ss, dp + joy1_pressed, a);
  S(0xA2F3, 3);                             /* C0A2F3 lda JOY1L */
  a = t_read16(ss, db | JOY1L); ss_set_nz16(ss, a);
  S(0xA2F6, 2);                             /* C0A2F6 sta $8A */
  t_write16(ss, dp + joy1_held, a);

  S(0xA2F8, 3);                             /* C0A2F8 lda JOY2L */
  a = t_read16(ss, db | JOY2L); ss_set_nz16(ss, a);
  S(0xA2FB, 2);                             /* C0A2FB eor $8E */
  a = alu_eor16(ss, a, t_read16(ss, dp + joy2_held));
  S(0xA2FD, 3);                             /* C0A2FD and JOY2L */
  a = alu_and16(ss, a, t_read16(ss, db | JOY2L));
  S(0xA300, 2);                             /* C0A300 sta $90 */
  t_write16(ss, dp + joy2_pressed, a);
  S(0xA302, 3);                             /* C0A302 lda JOY2L */
  a = t_read16(ss, db | JOY2L); ss_set_nz16(ss, a);
  S(0xA305, 2);                             /* C0A305 sta $8E */
  t_write16(ss, dp + joy2_held, a);

  bool yielded = false;
  read_joypads_check(ss, pb, 0xA307, &a, joy1_held, joy1_pressed, JOYSER0, &yielded);
  if(yielded) return;
  read_joypads_check(ss, pb, 0xA331, &a, joy2_held, joy2_pressed, JOYSER1, &yielded);
  if(yielded) return;

  ss_set_a(ss, a);
  S(0xA35B, 1);                             /* C0A35B rts: opcode fetch */
  ss_rts(ss);
}

static const RecompEntry kInput[] = {
  { 0xc0a2de, "read_joypads", read_joypads },
};
RECOMP_REGISTER(kInput)
