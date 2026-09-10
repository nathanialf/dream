/* The pseudo-random number generator, bank $C0.
 *
 * One routine, $C0:A212, and the whole particle/weather subsystem
 * (docs/naming_proposals.md section 5) is built on it: every spawn routine
 * draws position, velocity and lifetime out of it, so the exact state update
 * decides where forty sprites end up. It runs on four bytes of direct page,
 * $9C-$9F, which reset seeds with $AA55 and $FFFF (hence the names
 * tools/names.txt gives $9C and $9E) and never touches again.
 *
 * The update is byte-wide: sep #$20 for the body, rep #$20 on the way out, so
 * only the low byte of A is written and the high byte the caller had survives.
 * Writing s0..s3 for the old $9C..$9F, one call leaves
 *
 *     t    = ((s2 << 2) | (bit7(s1) << 1) | bit7(s2)) & $FF   two rols of $9E,
 *                                   carry-in from the asl of s1, then from itself
 *     $9C' = s1 ^ t
 *     $9D' = s2 ^ s3
 *     $9E' = s0
 *     $9F' = s1
 *
 * Two details decide the sequence. $9E is read into A at $A218 and only rolled
 * afterwards, so the eor at $A21E takes the *old* $9E while the eor at $A225
 * takes the rolled one; and $9E is then overwritten with s0, so t survives only
 * inside $9C'. A body that rolled first, or that kept t in $9E, would produce a
 * different stream and put forty particles in the wrong places.
 *
 * particle_update_and_draw_mode0 carries an inline copy of this routine that
 * uses $28 instead of the stack for its one temporary; it is transliterated
 * separately in particles_fx.c rather than shared, because the two differ
 * instruction for instruction.
 */
#include <stdint.h>
#include <stdbool.h>

#include "snes_state.h"
#include "dream_ram.h"
#include "dream_alu.h"
#include "dream_time.h"

/* rol dp with an 8-bit accumulator: read, an internal cycle, the carry, then
 * the write back (LakeSnes cpu_rol, mf branch). */
static uint8_t t_rol8_dp(SnesState* ss, uint16_t off) {
  const uint32_t adr = (uint32_t) (uint16_t) (ss_dp(ss) + off);
  int result = (ss_bus_r8(ss, adr) << 1) | (ss_c(ss) ? 1 : 0);
  ss_idle(ss);
  ss_set_c(ss, (result & 0x100) != 0);
  ss_check_int(ss);
  ss_bus_w8(ss, adr, (uint8_t) result);
  ss_set_nz8(ss, (uint8_t) result);
  return (uint8_t) result;
}

/* ---------------------------------------------------------------------------
 * random_next — $C0:A212
 *
 * Advances the four-byte state and leaves the fresh words in $9C and $9E for
 * the caller to mask; callers read $9D and $9F too.
 * Exit: A = the caller's high byte over the new $9C, m back to 16-bit. N/Z are
 * the pla at $A22C (i.e. the new $9C, as a byte); C is bit 7 of the value the
 * first rol left in $9E, which no caller tests.
 * ------------------------------------------------------------------------- */
void random_next(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint16_t dp = ss_dp(ss);
  uint8_t b;

  SEP(0xA212, 0x20);                        /* C0A212 sep #$20 */

  S(0xA214, 2);                             /* C0A214 lda $9D */
  b = t_read8(ss, dp + 0x9D);
  a = (uint16_t) ((a & 0xff00) | b);
  ss_set_nz8(ss, b);

  S(0xA216, 1);                             /* C0A216 pha */
  ss_idle(ss);
  ss_check_int(ss);
  ss_push8(ss, (uint8_t) a);

  SI(0xA217);                               /* C0A217 asl A */
  ss_set_c(ss, (a & 0x80) != 0);
  a = (uint16_t) ((a & 0xff00) | ((a << 1) & 0xff));
  ss_set_nz8(ss, (uint8_t) a);

  S(0xA218, 2);                             /* C0A218 lda init_magic_FFFF */
  b = t_read8(ss, dp + init_magic_FFFF);
  a = (uint16_t) ((a & 0xff00) | b);
  ss_set_nz8(ss, b);

  S(0xA21A, 2);                             /* C0A21A rol init_magic_FFFF */
  t_rol8_dp(ss, init_magic_FFFF);
  S(0xA21C, 2);                             /* C0A21C rol init_magic_FFFF */
  t_rol8_dp(ss, init_magic_FFFF);

  S(0xA21E, 2);                             /* C0A21E eor $9F */
  b = (uint8_t) (a ^ t_read8(ss, dp + 0x9F));
  a = (uint16_t) ((a & 0xff00) | b);
  ss_set_nz8(ss, b);

  S(0xA220, 2);                             /* C0A220 sta $9D */
  t_write8(ss, dp + 0x9D, (uint8_t) a);

  S(0xA222, 1);                             /* C0A222 pla */
  ss_idle(ss);
  ss_idle(ss);
  ss_check_int(ss);
  b = ss_pull8(ss);
  a = (uint16_t) ((a & 0xff00) | b);
  ss_set_nz8(ss, b);

  S(0xA223, 2);                             /* C0A223 sta $9F */
  t_write8(ss, dp + 0x9F, (uint8_t) a);

  S(0xA225, 2);                             /* C0A225 eor init_magic_FFFF */
  b = (uint8_t) (a ^ t_read8(ss, dp + init_magic_FFFF));
  a = (uint16_t) ((a & 0xff00) | b);
  ss_set_nz8(ss, b);

  S(0xA227, 1);                             /* C0A227 pha */
  ss_idle(ss);
  ss_check_int(ss);
  ss_push8(ss, (uint8_t) a);

  S(0xA228, 2);                             /* C0A228 lda init_magic_AA55 */
  b = t_read8(ss, dp + init_magic_AA55);
  a = (uint16_t) ((a & 0xff00) | b);
  ss_set_nz8(ss, b);

  S(0xA22A, 2);                             /* C0A22A sta init_magic_FFFF */
  t_write8(ss, dp + init_magic_FFFF, (uint8_t) a);

  S(0xA22C, 1);                             /* C0A22C pla */
  ss_idle(ss);
  ss_idle(ss);
  ss_check_int(ss);
  b = ss_pull8(ss);
  a = (uint16_t) ((a & 0xff00) | b);
  ss_set_nz8(ss, b);

  S(0xA22D, 2);                             /* C0A22D sta init_magic_AA55 */
  t_write8(ss, dp + init_magic_AA55, (uint8_t) a);

  REP(0xA22F, 0x20);                        /* C0A22F rep #$20 */

  S(0xA231, 1);                             /* C0A231 rts */
  ss_rts(ss);
}

static const RecompEntry kRandom[] = {
  { 0xc0a212, "random_next", random_next },
};
RECOMP_REGISTER(kRandom)
