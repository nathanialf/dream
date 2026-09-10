/* Camera, bank $C0. */
#include <stdint.h>
#include <stdbool.h>

#include "snes_state.h"
#include "dream_ram.h"
#include "dream_alu.h"
#include "dream_time.h"

/* ---------------------------------------------------------------------------
 * camera_follow_player: $C0:A1B0
 *
 * Puts the camera $80 pixels left of and $20 pixels above the player, clamped to
 * [0, level_width_mask] and [0, level_height_mask]. The no-index forms of
 * entity_x / entity_y are slot 0, the player.
 *
 * The two "subtract then add back" pairs are how the ROM records the per-frame
 * camera delta without a second subtraction: subtract the old camera, keep that
 * in layer_parallax_mode ($98) / camera_y_lookahead ($9A), then add the old
 * camera back to recover the target. The vertical half also folds in
 * camera_y_bias ($74), the per-mode constant each modeN_level_init sets, so $9A
 * is the lookahead the column builders read rather than a plain delta.
 *
 * Game mode 1 keeps its own vertical scroll and skips the second half entirely.
 * Exit: camera_x, camera_y, $98 and (outside mode 1) $9A updated.
 * ------------------------------------------------------------------------- */
void camera_follow_player(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint16_t dp = ss_dp(ss);

  S(0xA1B0, 3);                             /* C0A1B0 lda entity_x */
  a = t_read16(ss, ss_abs(ss, entity_x));
  ss_set_nz16(ss, a);
  SI(0xA1B3); ss_set_c(ss, true);           /* C0A1B3 sec */
  S(0xA1B4, 3);                             /* C0A1B4 sbc #$0080 */
  a = alu_sbc16(ss, a, 0x0080);
  S(0xA1B7, 1); t_branch(ss, (a & 0x8000) == 0);  /* C0A1B7 bpl */
  if((a & 0x8000) != 0) {
    S(0xA1B9, 3);                           /* C0A1B9 lda #$0000 */
    a = 0x0000; ss_set_nz16(ss, a);
  }
  S(0xA1BC, 2);                             /* C0A1BC cmp level_width_mask */
  alu_cmp16(ss, a, t_read16(ss, dp + level_width_mask));
  S(0xA1BE, 1); t_branch(ss, !ss_c(ss));    /* C0A1BE bcc */
  if(ss_c(ss)) {
    S(0xA1C0, 2);                           /* C0A1C0 lda level_width_mask */
    a = t_read16(ss, dp + level_width_mask);
    ss_set_nz16(ss, a);
  }
  SI(0xA1C2); ss_set_c(ss, true);           /* C0A1C2 sec */
  S(0xA1C3, 2);                             /* C0A1C3 sbc camera_x */
  a = alu_sbc16(ss, a, t_read16(ss, dp + camera_x));
  S(0xA1C5, 2);                             /* C0A1C5 sta layer_parallax_mode */
  t_write16(ss, dp + layer_parallax_mode, a);
  SI(0xA1C7); ss_set_c(ss, false);          /* C0A1C7 clc */
  S(0xA1C8, 2);                             /* C0A1C8 adc camera_x */
  a = alu_adc16(ss, a, t_read16(ss, dp + camera_x));
  S(0xA1CA, 2);                             /* C0A1CA sta camera_x */
  t_write16(ss, dp + camera_x, a);

  S(0xA1CC, 2);                             /* C0A1CC lda game_mode */
  a = t_read16(ss, dp + game_mode);
  ss_set_nz16(ss, a);
  S(0xA1CE, 3);                             /* C0A1CE cmp #$0001 */
  alu_cmp16(ss, a, 0x0001);
  S(0xA1D1, 1); t_branch(ss, a == 0x0001);/* C0A1D1 beq loc_C0A1F2 */
  if(a != 0x0001) {
    S(0xA1D3, 3);                           /* C0A1D3 lda entity_y */
    a = t_read16(ss, ss_abs(ss, entity_y));
    ss_set_nz16(ss, a);
    SI(0xA1D6); ss_set_c(ss, true);         /* C0A1D6 sec */
    S(0xA1D7, 3);                           /* C0A1D7 sbc #$0020 */
    a = alu_sbc16(ss, a, 0x0020);
    S(0xA1DA, 1); t_branch(ss, (a & 0x8000) == 0);  /* C0A1DA bpl */
    if((a & 0x8000) != 0) {
      S(0xA1DC, 3);                         /* C0A1DC lda #$0000 */
      a = 0x0000; ss_set_nz16(ss, a);
    }
    S(0xA1DF, 2);                           /* C0A1DF cmp level_height_mask */
    alu_cmp16(ss, a, t_read16(ss, dp + level_height_mask));
    S(0xA1E1, 1); t_branch(ss, !ss_c(ss));/* C0A1E1 bcc */
    if(ss_c(ss)) {
      S(0xA1E3, 2);                         /* C0A1E3 lda level_height_mask */
      a = t_read16(ss, dp + level_height_mask);
      ss_set_nz16(ss, a);
    }
    SI(0xA1E5); ss_set_c(ss, true);         /* C0A1E5 sec */
    S(0xA1E6, 2);                           /* C0A1E6 sbc camera_y */
    a = alu_sbc16(ss, a, t_read16(ss, dp + camera_y));
    SI(0xA1E8); ss_set_c(ss, false);        /* C0A1E8 clc */
    S(0xA1E9, 2);                           /* C0A1E9 adc camera_y_bias */
    a = alu_adc16(ss, a, t_read16(ss, dp + camera_y_bias));
    S(0xA1EB, 2);                           /* C0A1EB sta camera_y_lookahead */
    t_write16(ss, dp + camera_y_lookahead, a);
    SI(0xA1ED); ss_set_c(ss, false);        /* C0A1ED clc */
    S(0xA1EE, 2);                           /* C0A1EE adc camera_y */
    a = alu_adc16(ss, a, t_read16(ss, dp + camera_y));
    S(0xA1F0, 2);                           /* C0A1F0 sta camera_y */
    t_write16(ss, dp + camera_y, a);
  }

  ss_set_a(ss, a);
  S(0xA1F2, 1);                             /* C0A1F2 rts */
  ss_rts(ss);
}

static const RecompEntry kCamera[] = {
  { 0xc0a1b0, "camera_follow_player", camera_follow_player },
};
RECOMP_REGISTER(kCamera)
