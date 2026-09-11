/* Entity state, physics and dispatch helpers, bank $C0.
 *
 * Entities are a struct-of-arrays: X holds slot * 2 and every field is a flat
 * 32-byte column (docs/naming_proposals.md section 7), so an access is always
 * <field> + X through the data bank.
 */
#include <stdint.h>
#include <stdbool.h>

#include "snes_state.h"
#include "dream_ram.h"
#include "dream_alu.h"
#include "dream_time.h"

/* The three columns unused_entity_apply_velocity_z moves. $08E8 is the third position column
 * recomp/src/entities_ai.c names entity_z_dead; $0908 and $0928 are its
 * sub-pixel accumulator and velocity, at the same +$20 spacing the x and y
 * triples use. Guarded because dream_ram.h gains names as subsystems land. */
#ifndef entity_z_dead
#define entity_z_dead        0x08E8
#endif
#ifndef entity_z_sub_dead
#define entity_z_sub_dead    0x0908
#endif
#ifndef entity_vel_z_dead
#define entity_vel_z_dead    0x0928
#endif

/* ---------------------------------------------------------------------------
 * entity_apply_hit_reaction: $C0:9A61, shared body at loc_C09A66
 *
 * Turns a button word into a new entity_state. Entry to the body: A = the button
 * word, Y = a second word whose bit 15 forces state $0C, X = entity index.
 *
 * A 65816 detail worth spelling out, because the C would otherwise read wrong:
 * the adc #$0004 at $9A97 adds four *plus the carry*, and the carry is not
 * constant on the way in. Reaching loc_C09A7F through the bcc at $9A73 leaves it
 * clear; falling out of the bcc at $9A7D (state in [$18,$24)) leaves it set. So
 * a state in that window shifts the result by one. The bit and lda in between do
 * not touch C, which makes the value survive.
 * ------------------------------------------------------------------------- */
static void entity_hit_reaction_body(SnesState* ss, uint16_t a, uint16_t y) {
  const uint8_t pb = ss_pb(ss);
  uint16_t x = ss_x(ss);
  const uint16_t dp = ss_dp(ss);

  S(0x9A66, 2);                             /* C09A66 sta ptr_04 */
  t_write16_dp(ss, dp, ptr_04, a);
  S(0x9A68, 3); t_index(ss);                /* C09A68 lda entity_hitstun_timer,X */
  a = t_read16(ss, ss_abs(ss, (entity_hitstun_timer + x)));
  ss_set_nz16(ss, a);
  S(0x9A6B, 1); t_branch(ss, a != 0);       /* C09A6B bne loc_C09AA9 */
  if(a != 0) {
    ss_set_a(ss, a);
    S(0x9AA9, 1);                           /* C09AA9 rts */
    ss_rts(ss);
    return;
  }

  bool reactive = false;
  S(0x9A6D, 3); t_index(ss);                /* C09A6D lda entity_state,X */
  a = t_read16(ss, ss_abs(ss, (entity_state + x)));
  ss_set_nz16(ss, a);
  SIMM16(0x9A70); alu_cmp16(ss, a, 0x000C); /* C09A70 cmp #$000C */
  S(0x9A73, 1); t_branch(ss, !ss_c(ss));    /* C09A73 bcc loc_C09A7F */
  if(!ss_c(ss)) {
    reactive = true;
  } else {
    SIMM16(0x9A75); alu_cmp16(ss, a, 0x0024);  /* C09A75 cmp #$0024 */
    S(0x9A78, 1); t_branch(ss, ss_c(ss));   /* C09A78 bcs loc_C09AB7 */
    if(ss_c(ss)) {
      ss_set_a(ss, a);
      S(0x9AB7, 1);                         /* C09AB7 rts */
      ss_rts(ss);
      return;
    }
    SIMM16(0x9A7A); alu_cmp16(ss, a, 0x0018);  /* C09A7A cmp #$0018 */
    S(0x9A7D, 1); t_branch(ss, !ss_c(ss));  /* C09A7D bcc loc_C09AB7 */
    if(!ss_c(ss)) {
      ss_set_a(ss, a);
      S(0x9AB7, 1);
      ss_rts(ss);
      return;
    }
    reactive = true;                        /* falls into loc_C09A7F, C set */
  }

  if(reactive) {
    S(0x9A7F, 2);                           /* C09A7F lda ptr_04 */
    const uint16_t buttons = t_read16_dp(ss, dp, ptr_04);
    a = buttons;
    ss_set_nz16(ss, a);
    SIMM16(0x9A81); alu_bit_imm16(ss, a, 0x0200);  /* C09A81 bit #$0200 */
    S(0x9A84, 1); t_branch(ss, (a & 0x0200) == 0);/* C09A84 beq loc_C09A8B */
    bool store = false;
    if((a & 0x0200) != 0) {
      SIMM16(0x9A86); a = 0x0004; ss_set_nz16(ss, a); /* C09A86 lda #$0004 */
      S(0x9A89, 1); t_branch(ss, true);     /* C09A89 bra loc_C09A93 */
      store = true;
    } else {
      SIMM16(0x9A8B); alu_bit_imm16(ss, a, 0x0100); /* C09A8B bit #$0100 */
      S(0x9A8E, 1); t_branch(ss, (a & 0x0100) == 0); /* C09A8E beq loc_C09A9D */
      if((a & 0x0100) != 0) {
        SIMM16(0x9A90); a = 0x0006; ss_set_nz16(ss, a); /* C09A90 lda #$0006 */
        store = true;
      }
    }
    if(store) {
      S(0x9A93, 2);                         /* C09A93 bit ptr_04 */
      alu_bit16(ss, a, t_read16_dp(ss, dp, ptr_04));
      S(0x9A95, 1); t_branch(ss, !ss_v(ss));  /* C09A95 bvc loc_C09A9A */
      if(ss_v(ss)) {
        SIMM16(0x9A97);                     /* C09A97 adc #$0004 (plus C, see above) */
        a = alu_adc16(ss, a, 0x0004);
      }
      S(0x9A9A, 3); t_index(ss);            /* C09A9A sta entity_state,X */
      t_write16(ss, ss_abs(ss, (entity_state + x)), a);
    }
    (void) buttons;
  }

  SI(0x9A9D); a = y; ss_set_nz16(ss, a);    /* C09A9D tya */
  SIMM16(0x9A9E); alu_bit_imm16(ss, a, 0x8000);  /* C09A9E bit #$8000 */
  S(0x9AA1, 1); t_branch(ss, (a & 0x8000) == 0);  /* C09AA1 beq loc_C09AAA */
  if((a & 0x8000) != 0) {
    SIMM16(0x9AA3); a = 0x000C; ss_set_nz16(ss, a);  /* C09AA3 lda #$000C */
    S(0x9AA6, 3); t_index(ss);              /* C09AA6 sta entity_state,X */
    t_write16(ss, ss_abs(ss, (entity_state + x)), a);
    ss_set_a(ss, a);
    S(0x9AA9, 1);                           /* C09AA9 rts */
    ss_rts(ss);
    return;
  }

  S(0x9AAA, 2);                             /* C09AAA lda ptr_04 */
  a = t_read16_dp(ss, dp, ptr_04);
  ss_set_nz16(ss, a);
  SIMM16(0x9AAC); a = alu_and16(ss, a, 0x0300);  /* C09AAC and #$0300 */
  S(0x9AAF, 1); t_branch(ss, a != 0);       /* C09AAF bne loc_C09AB7 */
  if(a == 0) {
    SIMM16(0x9AB1); a = 0x0000; ss_set_nz16(ss, a); /* C09AB1 lda #$0000 */
    S(0x9AB4, 3); t_index(ss);              /* C09AB4 sta entity_state,X */
    t_write16(ss, ss_abs(ss, (entity_state + x)), a);
  }
  ss_set_a(ss, a);
  S(0x9AB7, 1);                             /* C09AB7 rts */
  ss_rts(ss);
}

void entity_apply_hit_reaction(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint16_t dp = ss_dp(ss);

  SI(0x9A61);                               /* C09A61 tyx */
  x = ss_y(ss);
  ss_set_x(ss, x);
  ss_set_nz16(ss, x);
  S(0x9A62, 2);                             /* C09A62 lda $8A */
  a = t_read16_dp(ss, dp, joy1_held);
  ss_set_nz16(ss, a);
  S(0x9A64, 2);                             /* C09A64 ldy $8C */
  y = t_read16_dp(ss, dp, joy1_pressed);
  ss_set_y(ss, y);
  ss_set_nz16(ss, y);
  entity_hit_reaction_body(ss, a, y);
}

/* ---------------------------------------------------------------------------
 * entity_ai_none: $C0:9A5F
 *
 * The two inert entries of jtbl_C099DD. entity_update_tick calls the table with
 * X = entity_type and Y = the entity index it saved with txy at $98DA, so every
 * handler has to put the index back into X before returning; that is all this
 * one does.
 * ------------------------------------------------------------------------- */
void entity_ai_none(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  SI(0x9A5F);                               /* C09A5F tyx */
  x = ss_y(ss);
  ss_set_x(ss, x);
  ss_set_nz16(ss, x);
  S(0x9A60, 1);                             /* C09A60 rts */
  ss_rts(ss);
}

/* ---------------------------------------------------------------------------
 * check_pending_player_attack: $C0:8E8E
 *
 * The per-mode hook at jtbl_C0827A for game modes 1..3. When $0BB4 is set it
 * hands player 2's button words to the hit-reaction body with X = $0BB4 as the
 * entity index; that is a jmp into loc_C09A66, i.e. a tail call, and both of the
 * body's exits are rts back to this routine's own caller.
 * ------------------------------------------------------------------------- */
void check_pending_player_attack(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint16_t dp = ss_dp(ss);

  S(0x8E8E, 3);                             /* C08E8E ldx $0BB4 */
  x = t_read16(ss, ss_abs(ss, player_attack_flag));
  ss_set_x(ss, x);
  ss_set_nz16(ss, x);
  S(0x8E91, 1); t_branch(ss, x == 0);       /* C08E91 beq loc_C08E9A */
  if(x == 0) {
    S(0x8E9A, 1);                           /* C08E9A rts */
    ss_rts(ss);
    return;
  }
  S(0x8E93, 2);                             /* C08E93 lda $8E */
  a = t_read16_dp(ss, dp, joy2_held);
  ss_set_nz16(ss, a);
  S(0x8E95, 2);                             /* C08E95 ldy $90 */
  y = t_read16_dp(ss, dp, joy2_pressed);
  ss_set_y(ss, y);
  ss_set_nz16(ss, y);
  SJMP(0x8E97);                             /* C08E97 jmp loc_C09A66 */
  entity_hit_reaction_body(ss, a, y);
}

/* ---------------------------------------------------------------------------
 * entity_ground_y_lookup: $C0:9BDA
 *
 * Walks the per-mode 6-byte ground table at $80:B2F6 for the first span whose
 * left edge is at or past the entity's X, and reports its slope byte in $0BAE.
 * A positive, non-zero slope with a non-zero third field also snaps entity_y.
 * Entry: X = entity index. Exit: X restored, $0BAE = the slope word.
 * The adc #$0006 that steps to the next record runs with the carry the bcs left
 * clear, so it really is a plain +6.
 * ------------------------------------------------------------------------- */
void entity_ground_y_lookup(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint16_t dp = ss_dp(ss);
  const uint16_t slot = ss_x(ss);

  SI(0x9BDA);                               /* C09BDA txy */
  y = slot;
  ss_set_y(ss, y);
  ss_set_nz16(ss, y);

  S(0x9BDB, 3); t_index(ss);                /* C09BDB lda entity_x,X */
  a = t_read16(ss, ss_abs(ss, entity_x + slot));
  ss_set_nz16(ss, a);
  S(0x9BDE, 1); t_branch(ss, (a & 0x8000) == 0);  /* C09BDE bpl */
  if((a & 0x8000) != 0) {
    SI(0x9BE0); a = dp; ss_set_nz16(ss, a);  /* C09BE0 tdc */
  }
  const uint16_t probe = a;
  S(0x9BE1, 2); t_write16_dp(ss, dp, ptr_04, a);  /* C09BE1 sta ptr_04 */
  S(0x9BE3, 3);                             /* C09BE3 stz $0BAE */
  t_write16(ss, ss_abs(ss, ground_probe_result), 0);

  S(0x9BE6, 2);                             /* C09BE6 lda game_mode */
  a = t_read16_dp(ss, dp, game_mode);
  ss_set_nz16(ss, a);
  SI(0x9BE8); a = alu_asl16(ss, a);         /* C09BE8 asl A */
  SI(0x9BE9); x = a; ss_set_nz16(ss, x);    /* C09BE9 tax */
  S(0x9BEA, 4);                             /* C09BEA lda data_C0B2F6,X */
  a = t_read16(ss, 0x80B2F6 + x);
  ss_set_nz16(ss, a);

  for(;;) {
    SI(0x9BEE); x = a; ss_set_nz16(ss, x);  /* C09BEE tax */
    S(0x9BEF, 4);                           /* C09BEF lda data_C0B2F6,X */
    a = t_read16(ss, 0x80B2F6 + x);
    ss_set_nz16(ss, a);
    S(0x9BF3, 1); t_branch(ss, a == 0);     /* C09BF3 beq loc_C09C03 */
    if(a == 0) break;
    S(0x9BF5, 2);                           /* C09BF5 cmp ptr_04 */
    alu_cmp16(ss, a, t_read16_dp(ss, dp, ptr_04));
    S(0x9BF7, 1); t_branch(ss, ss_c(ss)); /* C09BF7 bcs loc_C09BFF */
    if(ss_c(ss)) {
      S(0x9BFF, 4);                         /* C09BFF lda data_C0B2F8,X */
      a = t_read16(ss, 0x80B2F8 + x);
      ss_set_nz16(ss, a);
      break;
    }
    SI(0x9BF9); a = x; ss_set_nz16(ss, a);  /* C09BF9 txa */
    SIMM16(0x9BFA);                         /* C09BFA adc #$0006 (C clear here) */
    a = alu_adc16(ss, a, 0x0006);
    S(0x9BFD, 1); t_branch(ss, true);       /* C09BFD bra loc_C09BEE */
  }
  (void) probe;

  S(0x9C03, 3);                             /* C09C03 sta $0BAE */
  t_write16(ss, ss_abs(ss, ground_probe_result), a);
  S(0x9C06, 1); t_branch(ss, a == 0);       /* C09C06 beq loc_C09C14 */
  if(a != 0) {
    S(0x9C08, 1); t_branch(ss, (a & 0x8000) != 0); /* C09C08 bmi loc_C09C14 */
    if((a & 0x8000) == 0) {
      S(0x9C0A, 4);                         /* C09C0A lda data_C0B2FA,X */
      a = t_read16(ss, 0x80B2FA + x);
      ss_set_nz16(ss, a);
      S(0x9C0E, 1); t_branch(ss, a == 0); /* C09C0E beq loc_C09C14 */
      if(a != 0) {
        SI(0x9C10); x = y; ss_set_nz16(ss, x);  /* C09C10 tyx */
        S(0x9C11, 3); t_index(ss);          /* C09C11 sta entity_y,X */
        t_write16(ss, ss_abs(ss, (entity_y + x)), a);
      }
    }
  }

  SI(0x9C14); x = y; ss_set_x(ss, x); ss_set_nz16(ss, x);  /* C09C14 tyx */
  ss_set_a(ss, a);
  S(0x9C15, 1);                             /* C09C15 rts */
  ss_rts(ss);
}

/* ---------------------------------------------------------------------------
 * entity_vel_y_from_vel_x: $C0:A1F3
 *
 * Derives a vertical velocity from the horizontal one: negate entity_vel_x,
 * arithmetic-shift it right twice, then flip the sign again unless the ground
 * probe ($0BAE, set by entity_ground_y_lookup) says the slope is negative, and
 * zero it when the probe is zero, which the ROM spells as "tdc then negate".
 *
 * The arithmetic shift is the cmp #$8000 / ror A idiom: the compare sets carry
 * from the sign bit and the rotate shifts that copy back in.
 * Entry: X = entity index. Exit: entity_vel_y,X written.
 * ------------------------------------------------------------------------- */
void entity_vel_y_from_vel_x(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);

  S(0xA1F3, 3); t_index(ss);                /* C0A1F3 lda entity_vel_x,X */
  a = t_read16(ss, ss_abs(ss, (entity_vel_x + x)));
  ss_set_nz16(ss, a);
  SIMM16(0xA1F6); a = alu_eor16(ss, a, 0xFFFF);  /* C0A1F6 eor #$FFFF */
  SI(0xA1F9); a = alu_inc16(ss, a);         /* C0A1F9 inc A */
  SIMM16(0xA1FA); alu_cmp16(ss, a, 0x8000);   /* C0A1FA cmp #$8000 */
  SI(0xA1FD); a = alu_ror16(ss, a);         /* C0A1FD ror A */
  SIMM16(0xA1FE); alu_cmp16(ss, a, 0x8000);   /* C0A1FE cmp #$8000 */
  SI(0xA201); a = alu_ror16(ss, a);         /* C0A201 ror A */

  S(0xA202, 3);                             /* C0A202 ldy $0BAE */
  y = t_read16(ss, ss_abs(ss, ground_probe_result));
  ss_set_y(ss, y);
  ss_set_nz16(ss, y);
  S(0xA205, 1); t_branch(ss, y == 0);       /* C0A205 beq loc_C0A20E */
  if(y != 0) {
    S(0xA207, 1); t_branch(ss, (y & 0x8000) != 0); /* C0A207 bmi loc_C0A20A */
    if((y & 0x8000) == 0) {
      SI(0xA209); a = ss_dp(ss); ss_set_nz16(ss, a);  /* C0A209 tdc */
    }
    SIMM16(0xA20A); a = alu_eor16(ss, a, 0xFFFF);  /* C0A20A eor #$FFFF */
    SI(0xA20D); a = alu_inc16(ss, a);       /* C0A20D inc A */
  }
  S(0xA20E, 3); t_index(ss);                /* C0A20E sta entity_vel_y,X */
  t_write16(ss, ss_abs(ss, (entity_vel_y + x)), a);
  ss_set_a(ss, a);
  S(0xA211, 1);                             /* C0A211 rts */
  ss_rts(ss);
}

/* ---------------------------------------------------------------------------
 * entity_accelerate_velocity_x: $C0:A232
 *
 * Eases entity_vel_x toward entity_vel_x_target by an eighth of the gap. With no
 * target it instead brakes: add $0100 and, if the result is still below $0200,
 * snap to zero, so a velocity within one unit of standstill stops.
 *
 * The negative branch is a signed divide: sec ; ror A three times shifts ones in
 * from the top, and the closing cmp #$FFFF counts -1 as "no step left" at
 * loc_C0A255, where the shared bne tests it.
 * Entry: X = entity index.
 * ------------------------------------------------------------------------- */
void entity_accelerate_velocity_x(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint16_t vel = (uint16_t) (entity_vel_x + x);
  const uint16_t tgt = (uint16_t) (entity_vel_x_target + x);

  S(0xA232, 3); t_index(ss);                /* C0A232 lda ...target,X */
  a = t_read16(ss, ss_abs(ss, tgt));
  ss_set_nz16(ss, a);
  S(0xA235, 1); t_branch(ss, a != 0);       /* C0A235 bne loc_C0A247 */
  if(a == 0) {
    S(0xA237, 3); t_index(ss);              /* C0A237 lda entity_vel_x,X */
    a = t_read16(ss, ss_abs(ss, vel));
    ss_set_nz16(ss, a);
    SI(0xA23A); ss_set_c(ss, false);        /* C0A23A clc */
    SIMM16(0xA23B); a = alu_adc16(ss, a, 0x0100);  /* C0A23B adc #$0100 */
    SIMM16(0xA23E); alu_cmp16(ss, a, 0x0200);  /* C0A23E cmp #$0200 */
    S(0xA241, 1); t_branch(ss, ss_c(ss)); /* C0A241 bcs loc_C0A247 */
    if(!ss_c(ss)) {
      S(0xA243, 3); t_index(ss);            /* C0A243 stz entity_vel_x,X */
      t_write16(ss, ss_abs(ss, vel), 0);
      ss_set_a(ss, a);
      S(0xA246, 1);                         /* C0A246 rts */
      ss_rts(ss);
      return;
    }
  }

  S(0xA247, 3); t_index(ss);                /* C0A247 lda ...target,X */
  a = t_read16(ss, ss_abs(ss, tgt));
  ss_set_nz16(ss, a);
  SI(0xA24A); ss_set_c(ss, true);           /* C0A24A sec */
  S(0xA24B, 3); t_index(ss);                /* C0A24B sbc entity_vel_x,X */
  a = alu_sbc16(ss, a, t_read16(ss, ss_abs(ss, vel)));
  S(0xA24E, 1); t_branch(ss, a == 0);       /* C0A24E beq loc_C0A263 */
  if(a == 0) {
    ss_set_a(ss, a);
    S(0xA263, 1);                           /* C0A263 rts */
    ss_rts(ss);
    return;
  }
  S(0xA250, 1); t_branch(ss, (a & 0x8000) != 0);  /* C0A250 bmi loc_C0A264 */
  bool step;
  if((a & 0x8000) != 0) {
    SI(0xA264); ss_set_c(ss, true);         /* C0A264 sec */
    SI(0xA265); a = alu_ror16(ss, a);       /* C0A265 ror A */
    SI(0xA266); ss_set_c(ss, true);         /* C0A266 sec */
    SI(0xA267); a = alu_ror16(ss, a);       /* C0A267 ror A */
    SI(0xA268); ss_set_c(ss, true);         /* C0A268 sec */
    SI(0xA269); a = alu_ror16(ss, a);       /* C0A269 ror A */
    SIMM16(0xA26A); alu_cmp16(ss, a, 0xFFFF);  /* C0A26A cmp #$FFFF */
    S(0xA26D, 1); t_branch(ss, true);       /* C0A26D bra loc_C0A255 */
    step = a != 0xFFFF;
  } else {
    SI(0xA252); a = alu_lsr16(ss, a);       /* C0A252 lsr A */
    SI(0xA253); a = alu_lsr16(ss, a);       /* C0A253 lsr A */
    SI(0xA254); a = alu_lsr16(ss, a);       /* C0A254 lsr A */
    step = a != 0;
  }
  S(0xA255, 1); t_branch(ss, step);         /* C0A255 bne loc_C0A25C */
  if(!step) {
    S(0xA257, 3); t_index(ss);              /* C0A257 lda ...target,X */
    a = t_read16(ss, ss_abs(ss, tgt));
    ss_set_nz16(ss, a);
    S(0xA25A, 1); t_branch(ss, true);       /* C0A25A bra loc_C0A260 */
  } else {
    SI(0xA25C); ss_set_c(ss, false);        /* C0A25C clc */
    S(0xA25D, 3); t_index(ss);              /* C0A25D adc entity_vel_x,X */
    a = alu_adc16(ss, a, t_read16(ss, ss_abs(ss, vel)));
  }
  S(0xA260, 3); t_index(ss);                /* C0A260 sta entity_vel_x,X */
  t_write16(ss, ss_abs(ss, vel), a);
  ss_set_a(ss, a);
  S(0xA263, 1);                             /* C0A263 rts */
  ss_rts(ss);
}

/* ---------------------------------------------------------------------------
 * entity_apply_velocity_x: $C0:A26F, entity_apply_velocity_y: $C0:A2B9
 *
 * The 8.8 fixed-point integrator, and the same twelve instructions at both
 * addresses (the y copy sits $4A bytes later with the same internal offsets).
 * It reads the velocity word one byte low ($0867,X rather than $0868,X) so that
 * "and #$FF00" isolates the fractional byte already shifted into place, adds it
 * to the sub-pixel accumulator, then reads one byte high, sign-extends the
 * integer byte and adds it to the position *with the carry the first add
 * produced*. The carry has to survive: and, bit and ora leave C alone, so the
 * fractional overflow reaches the second add untouched. Getting it wrong loses
 * a pixel per wrap.
 *
 * Entry: X = entity index. Exit: Y = 0, position and accumulator updated.
 * The addresses in the comments below are the x copy's; every step is written
 * relative to `base` because the y copy is the same code $4A bytes later, and a
 * yield has to name the address the *caller's* copy would resume at.
 * ------------------------------------------------------------------------- */
static void entity_apply_velocity(SnesState* ss, uint16_t base,
                                  uint16_t vel, uint16_t sub, uint16_t pos) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);

  S(base + 0, 3);                           /* C0A26F ldy #$0000 */
  y = 0x0000;
  ss_set_y(ss, y);
  ss_set_nz16(ss, y);

  S(base + 3, 3); t_index(ss);              /* C0A272 lda $0867,X */
  a = t_read16(ss, ss_abs(ss, (vel - 1 + x)));
  ss_set_nz16(ss, a);
  S(base + 6, 3); a = alu_and16(ss, a, 0xFF00);  /* C0A275 and #$FF00 */
  SI(base + 9); ss_set_c(ss, false);        /* C0A278 clc */
  S(base + 10, 3); t_index(ss);             /* C0A279 adc entity_x_sub,X */
  a = alu_adc16(ss, a, t_read16(ss, ss_abs(ss, (sub + x))));
  S(base + 13, 3); t_index(ss);             /* C0A27C sta entity_x_sub,X */
  t_write16(ss, ss_abs(ss, (sub + x)), a);

  S(base + 16, 3); t_index(ss);             /* C0A27F lda $0869,X */
  a = t_read16(ss, ss_abs(ss, (vel + 1 + x)));
  ss_set_nz16(ss, a);
  S(base + 19, 3); a = alu_and16(ss, a, 0x00FF);  /* C0A282 and #$00FF */
  S(base + 22, 3); alu_bit_imm16(ss, a, 0x0080);  /* C0A285 bit #$0080 */
  S(base + 25, 1); t_branch(ss, (a & 0x0080) == 0);/* C0A288 beq */
  if((a & 0x0080) != 0) {
    S(base + 27, 3); a = alu_ora16(ss, a, 0xFF00); /* C0A28A ora #$FF00 */
  }
  S(base + 30, 3); t_index(ss);             /* C0A28D adc entity_x,X */
  a = alu_adc16(ss, a, t_read16(ss, ss_abs(ss, (pos + x))));
  S(base + 33, 3); t_index(ss);             /* C0A290 sta entity_x,X */
  t_write16(ss, ss_abs(ss, (pos + x)), a);

  ss_set_a(ss, a);
  S(base + 36, 1);                          /* C0A293 rts */
  ss_rts(ss);
}

void entity_apply_velocity_x(SnesState* ss) {
  entity_apply_velocity(ss, 0xA26F, entity_vel_x, entity_x_sub, entity_x);
}

void entity_apply_velocity_y(SnesState* ss) {
  entity_apply_velocity(ss, 0xA2B9, entity_vel_y, entity_y_sub, entity_y);
}

/* ---------------------------------------------------------------------------
 * unused_entity_apply_velocity_z: $C0:A294
 *
 * A third copy of the same thirty-seven bytes, sitting between the x and y
 * copies, on the column triple $08E8 / $0908 / $0928. $08E8 is entity_z_dead
 * (recomp/src/entities_ai.c), the third position column entity_init_from_table
 * and the spawn transforms still copy but nothing moves; $0908 and $0928 are
 * its sub-pixel accumulator and velocity by the same +$20 spacing the x and y
 * columns use, and no other routine in the ROM touches either.
 *
 * Nothing calls it: no jsr, jsl or table word anywhere in out/dream.asm names
 * the address. No input script can reach it, so it is credited by the unit gate
 * instead
 * (config/recomp_units.txt, `dream_harness --unit`).
 * ------------------------------------------------------------------------- */
void unused_entity_apply_velocity_z(SnesState* ss) {
  entity_apply_velocity(ss, 0xA294, entity_vel_z_dead, entity_z_sub_dead, entity_z_dead);
}

static const RecompEntry kEntities[] = {
  { 0xc08e8e, "check_pending_player_attack", check_pending_player_attack },
  { 0xc09a5f, "entity_ai_none", entity_ai_none },
  { 0xc09a61, "entity_apply_hit_reaction", entity_apply_hit_reaction },
  { 0xc09bda, "entity_ground_y_lookup", entity_ground_y_lookup },
  { 0xc0a1f3, "entity_vel_y_from_vel_x", entity_vel_y_from_vel_x },
  { 0xc0a232, "entity_accelerate_velocity_x", entity_accelerate_velocity_x },
  { 0xc0a26f, "entity_apply_velocity_x", entity_apply_velocity_x },
  { 0xc0a294, "unused_entity_apply_velocity_z", unused_entity_apply_velocity_z },
  { 0xc0a2b9, "entity_apply_velocity_y", entity_apply_velocity_y },
};
RECOMP_REGISTER(kEntities)
