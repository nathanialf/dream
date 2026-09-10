/* Entity spawn, per-tick dispatch and state changes, bank $C0.
 *
 * The per-entity tick (entity_update_tick, $98DA) is the spine: it dispatches
 * the type's AI handler through jtbl_C099DD, ages the hitstun timer, runs the
 * physics leaves, folds the ground probe into facing/state, picks the animation
 * id out of entity_state_anim_table and finally hands the velocity to one of
 * the animation-rate handlers in recomp/src/anim.c through the jmp ($0004) at
 * $99B6. docs/naming_proposals.md section 4 describes the shape; the entity
 * struct-of-arrays columns are section 7 (X or Y = slot * 2, every field a flat
 * 32-byte column).
 *
 * Two 65816 details drive how the C is written:
 *
 *  - Four of the seven distinct AI handlers (five of jtbl_C099DD's nine
 *    entries) end in `pla ; rts`. The pla discards the return address
 *    entity_update_tick's `jsr (jtbl_C099DD,X)` pushed, so the rts returns to
 *    *entity_update_tick's own caller* and the rest of the tick never runs.
 *    The hook therefore checks where the callee left the pc rather than
 *    assuming it came back (see the dispatch below).
 *  - The `pea $8080 ; plb` at $98EE leaves one byte on the stack for the whole
 *    tail; it is anim_rate_store's plb at $99D7 that pulls it. So the two
 *    routines share a stack frame across a hook boundary, which is fine because
 *    each hook leaves the stack exactly as the 65816 would.
 */
#include <stdint.h>
#include <stdbool.h>

#include "snes_state.h"
#include "dream_ram.h"
#include "dream_alu.h"
#include "dream_time.h"

/* ---- names dream_ram.h does not carry yet -------------------------------- */
/* The first five are docs/naming_proposals.md section 7's proposals, guarded
 * because dream_ram.h gains names as other subsystems land. The columns section
 * 7 leaves uncharacterised keep their address as their name, as out/dream.asm
 * prints them, and so do the two scratch words. */
#ifndef entity_attack_timer
#define entity_attack_timer  0x0748   /* random_next-derived AI delay / hit count */
#endif
#ifndef entity_parent_index
#define entity_parent_index  0x0968
#endif
#ifndef entity_z_dead
#define entity_z_dead        0x08E8
#endif
#ifndef entity_z_sub_dead
#define entity_z_sub_dead    0x0908
#endif
#ifndef entity_vel_z_dead
#define entity_vel_z_dead    0x0928
#endif
#define entity_field_07E8    0x07E8
#define entity_field_0808    0x0808
#define entity_field_09C8    0x09C8
#define entity_field_0A08    0x0A08   /* animation script cursor (frame * 8) */
#define entity_field_0A28    0x0A28   /* ticks left on the current frame */
#define entity_field_0A48    0x0A48   /* animation id currently installed */
#define entity_count         0x00A6   /* $A6: live entity count, in slot * 2 units */
#define state_row_offset     0x0BAC   /* $0BAC: $0018 in game mode 1, else 0 */
#define mode1_row_index      0x0BB6

/* ROM tables, bank $C0 read through the $80 mirror (docs/handler_tables.md) */
#define facing_flag_table           0xB652
#define facing_state_bits_table     0xB654
#define anim_rate_fn_table          0xB66A
#define entity_state_velocity_table 0xB6DC
#define entity_state_anim_table     0xB7AE
#define entity_spawn_table          0xB4A4  /* data_C0B4A4: 18-byte init records */

#define anim_update_entry    0xAEC8
#define anim_update_bank     0x80

/* ---- 65816 shapes dream_time.h does not cover --------------------------- */
/* A 16-bit read-modify-write (inc/dec abs): the read takes no interrupt latch
 * between its bytes and the write-back goes high byte first, which is the one
 * place the 65816 reverses a 16-bit store. */
static uint16_t t_rmw_r16(SnesState* ss, uint32_t adr) {
  uint8_t lo = ss_bus_r8(ss, adr);
  uint8_t hi = ss_bus_r8(ss, (adr + 1) & 0xffffff);
  return (uint16_t) (lo | (hi << 8));
}

static void t_rmw_w16(SnesState* ss, uint32_t adr, uint16_t v) {
  ss_bus_w8(ss, (adr + 1) & 0xffffff, (uint8_t) (v >> 8));
  ss_check_int(ss);
  ss_bus_w8(ss, adr, (uint8_t) v);
}

/* pull16 with the interrupt latch the 65816 takes between the two bytes (pla,
 * plx, ply); ss_pull16 does not model it. */
static uint16_t t_pull16(SnesState* ss) {
  uint8_t lo = ss_pull8(ss);
  ss_check_int(ss);
  uint8_t hi = ss_pull8(ss);
  return (uint16_t) (lo | (hi << 8));
}

static void t_push16(SnesState* ss, uint16_t v) {
  ss_push8(ss, (uint8_t) (v >> 8));
  ss_check_int(ss);
  ss_push8(ss, (uint8_t) v);
}

/* The second half of `jsr abs` / `jsl long`: the caller's S() has already
 * fetched the opcode and the address bytes, so what is left is the internal
 * cycle, the return-address push and the transfer of control.
 *
 * The callee then runs on the reference CPU, which is what keeps a converted
 * callee converted: the hook installed on its entry address fires from inside
 * this loop and its C body runs, exactly as if it had been called. Because the
 * frame pushed here is the routine's *real* return address, the callee can also
 * be left running when the machine moves on underneath the hook -- true is
 * returned then, and the ROM finishes both the callee and the caller. */
static bool t_call_sub(SnesState* ss, uint8_t bank, uint16_t target) {
  const uint16_t sp0 = ss_sp(ss);
  const uint16_t ret = (uint16_t) (ss_pc(ss) - 1);   /* rts adds 1 */
  ss_idle(ss);
  t_push16(ss, ret);
  ss_set_pc(ss, bank, target);
  return ss_run_callee(ss, sp0);
}

static bool t_call_long(SnesState* ss, uint8_t bank, uint16_t target) {
  const uint16_t sp0 = ss_sp(ss);
  ss_push8(ss, ss_pb(ss));
  ss_idle(ss);
  ss_fetch(ss, 1);                                   /* the operand's bank byte */
  const uint16_t ret = (uint16_t) (ss_pc(ss) - 1);   /* rtl adds 1 */
  t_push16(ss, ret);
  ss_set_pc(ss, bank, target);
  return ss_run_callee(ss, sp0);
}

/* ---------------------------------------------------------------------------
 * entity_init_from_table — $C0:9D6A
 *
 * Walks the 18-byte records of data_C0B4A4 for the current game mode, filling
 * one entity slot per record until a record whose first word is negative ends
 * the list or all 16 slots are used, then zeroes every remaining slot. $A6 is
 * left holding the number of live entities, in slot * 2 units, which is what
 * the main loop's entity loop and anim_cb_hit_enemies compare against.
 *
 * `pea $8000 ; plb` sets DB = $00, not $80: the pushed word's *low* byte is
 * what plb pulls. The entity columns are in bank 0 either way; the record reads
 * are long, so they do not depend on it at all.
 * ------------------------------------------------------------------------- */

/* The two clear blocks are runs of identical `sta <column>,Y` instructions,
 * three bytes apart, so the bodies below step the address rather than spelling
 * out fifty-odd copies of the same line. */
static const uint16_t kInitClearFields[] = {   /* $9DE3..$9E15 */
  entity_frame_id, entity_field_07E8, entity_field_0808, entity_attack_timer,
  entity_x_sub, entity_vel_x, entity_vel_x_target, entity_y_sub,
  entity_z_sub_dead, entity_vel_z_dead, entity_vel_y, entity_depth_key,
  entity_field_09C8, entity_field_0A08, entity_field_0A28, entity_field_0A48,
  entity_anim_rate,
};

static const uint16_t kSlotClearFields[] = {   /* $9E29..$9E79 */
  entity_type, entity_state, entity_hitstun_timer, entity_attack_timer,
  entity_anim_id, entity_substate, entity_x, entity_y, entity_z_dead,
  entity_flags, entity_parent_index, entity_frame_id, entity_field_07E8,
  entity_field_0808, entity_x_sub, entity_vel_x, entity_vel_x_target,
  entity_y_sub, entity_z_sub_dead, entity_vel_z_dead, entity_vel_y,
  entity_depth_key, entity_field_09C8, entity_field_0A08, entity_field_0A28,
  entity_field_0A48, entity_anim_rate,
};

void entity_init_from_table(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint16_t dp = ss_dp(ss);
  unsigned i;

  S(0x9D6A, 3);                             /* C09D6A pea $8000 */
  ss_push8(ss, 0x80);
  ss_check_int(ss);
  ss_push8(ss, 0x00);
  S(0x9D6D, 1);                             /* C09D6D plb */
  ss_idle(ss);
  ss_idle(ss);
  ss_check_int(ss);
  { uint8_t b = ss_pull8(ss); ss_set_db(ss, b); ss_set_nz8(ss, b); }

  S(0x9D6E, 2);                             /* C09D6E stz $A6 */
  t_write16(ss, dp + entity_count, 0);
  S(0x9D70, 2);                             /* C09D70 lda game_mode */
  a = t_read16(ss, dp + game_mode);
  ss_set_nz16(ss, a);
  SI(0x9D72); a = alu_asl16(ss, a);         /* C09D72 asl A */
  SI(0x9D73); x = a; ss_set_x(ss, x); ss_set_nz16(ss, x);  /* C09D73 tax */
  S(0x9D74, 4);                             /* C09D74 lda entity_spawn_table,X */
  a = t_read16(ss, 0x800000 + entity_spawn_table + x);
  ss_set_nz16(ss, a);
  S(0x9D78, 3);                             /* C09D78 ldy #$0000 */
  y = 0x0000; ss_set_y(ss, y); ss_set_nz16(ss, y);

  for(;;) {                                 /* loc_C09D7B */
    SI(0x9D7B); x = a; ss_set_x(ss, x); ss_set_nz16(ss, x);  /* C09D7B tax */
    S(0x9D7C, 4);                           /* C09D7C lda entity_spawn_table,X */
    a = t_read16(ss, 0x800000 + entity_spawn_table + x);
    ss_set_nz16(ss, a);
    S(0x9D80, 1); t_branch(ss, (a & 0x8000) == 0);  /* C09D80 bpl loc_C09D85 */
    if((a & 0x8000) != 0) {
      S(0x9D82, 3);                         /* C09D82 jmp loc_C09E28 */
      goto loc_C09E28;
    }

    S(0x9D85, 3); t_index(ss);              /* C09D85 sta entity_type,Y */
    t_write16(ss, ss_abs(ss, (uint16_t) (entity_type + y)), a);
    S(0x9D88, 2);                           /* C09D88 sta ptr_04 */
    t_write16(ss, dp + ptr_04, a);
    S(0x9D8A, 4);                           /* C09D8A lda data_C0B4A6,X */
    a = t_read16(ss, 0x800000 + entity_spawn_table + 2 + x);
    ss_set_nz16(ss, a);
    S(0x9D8E, 3); t_index(ss);              /* C09D8E sta entity_state,Y */
    t_write16(ss, ss_abs(ss, (uint16_t) (entity_state + y)), a);
    SI(0x9D91); ss_set_c(ss, false);        /* C09D91 clc */
    S(0x9D92, 3);                           /* C09D92 adc $0BAC */
    a = alu_adc16(ss, a, t_read16(ss, ss_abs(ss, state_row_offset)));
    S(0x9D95, 1);                           /* C09D95 phx */
    ss_idle(ss);
    t_push16(ss, x);
    S(0x9D96, 2);                           /* C09D96 ldx ptr_04 */
    x = t_read16(ss, dp + ptr_04);
    ss_set_x(ss, x); ss_set_nz16(ss, x);
    S(0x9D98, 4);                           /* C09D98 adc entity_state_anim_table,X */
    a = alu_adc16(ss, a, t_read16(ss, 0x800000 + entity_state_anim_table + x));
    SI(0x9D9C); x = a; ss_set_x(ss, x); ss_set_nz16(ss, x);  /* C09D9C tax */
    S(0x9D9D, 4);                           /* C09D9D lda entity_state_anim_table,X */
    a = t_read16(ss, 0x800000 + entity_state_anim_table + x);
    ss_set_nz16(ss, a);
    S(0x9DA1, 1);                           /* C09DA1 plx */
    ss_idle(ss);
    ss_idle(ss);
    x = t_pull16(ss);
    ss_set_x(ss, x); ss_set_nz16(ss, x);
    S(0x9DA2, 3); t_index(ss);              /* C09DA2 sta entity_anim_id,Y */
    t_write16(ss, ss_abs(ss, (uint16_t) (entity_anim_id + y)), a);

    /* $9DA5..$9DD5: seven record fields into their columns, each a long,X read
     * followed by an absolute,Y store. */
    {
      static const uint16_t kRecord[7][2] = {
        { 4,  entity_substate },     { 6,  entity_x },
        { 8,  entity_y },            { 10, entity_z_dead },
        { 12, entity_flags },        { 14, entity_parent_index },
        { 16, entity_hitstun_timer },
      };
      for(i = 0; i < 7; i++) {
        const uint16_t at = (uint16_t) (0x9DA5 + 7 * i);
        S(at, 4);                           /* C09DA5 lda data_C0B4A8,X ... */
        a = t_read16(ss, 0x800000 + entity_spawn_table + kRecord[i][0] + x);
        ss_set_nz16(ss, a);
        S(at + 4, 3); t_index(ss);          /* C09DA9 sta entity_substate,Y ... */
        t_write16(ss, ss_abs(ss, (uint16_t) (kRecord[i][1] + y)), a);
      }
    }

    S(0x9DD6, 3); t_index(ss);              /* C09DD6 lda entity_type,Y */
    a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_type + y)));
    ss_set_nz16(ss, a);
    S(0x9DD9, 1); t_branch(ss, a != 0);     /* C09DD9 bne loc_C09DE2 */
    if(a == 0) {
      S(0x9DDB, 4);                         /* C09DDB lda data_C0B4A6,X */
      a = t_read16(ss, 0x800000 + entity_spawn_table + 2 + x);
      ss_set_nz16(ss, a);
      S(0x9DDF, 3); t_index(ss);            /* C09DDF sta entity_anim_id,Y */
      t_write16(ss, ss_abs(ss, (uint16_t) (entity_anim_id + y)), a);
    }

    SI(0x9DE2); a = dp; ss_set_nz16(ss, a); /* C09DE2 tdc */
    for(i = 0; i < sizeof kInitClearFields / sizeof kInitClearFields[0]; i++) {
      S(0x9DE3 + 3 * i, 3); t_index(ss);    /* C09DE3 sta entity_frame_id,Y ... */
      t_write16(ss, ss_abs(ss, (uint16_t) (kInitClearFields[i] + y)), a);
    }

    S(0x9E16, 2);                           /* C09E16 inc $A6 */
    { uint16_t v = t_rmw_r16(ss, dp + entity_count);
      v = (uint16_t) (v + 1); ss_idle(ss);
      t_rmw_w16(ss, dp + entity_count, v); ss_set_nz16(ss, v); }
    S(0x9E18, 2);                           /* C09E18 inc $A6 */
    { uint16_t v = t_rmw_r16(ss, dp + entity_count);
      v = (uint16_t) (v + 1); ss_idle(ss);
      t_rmw_w16(ss, dp + entity_count, v); ss_set_nz16(ss, v); }
    SI(0x9E1A); y = (uint16_t) (y + 1); ss_set_y(ss, y); ss_set_nz16(ss, y);  /* iny */
    SI(0x9E1B); y = (uint16_t) (y + 1); ss_set_y(ss, y); ss_set_nz16(ss, y);  /* iny */
    S(0x9E1C, 3); alu_cmp16(ss, y, 0x0020); /* C09E1C cpy #$0020 */
    S(0x9E1F, 1); t_branch(ss, ss_c(ss));   /* C09E1F bcs loc_C09E81 */
    if(ss_c(ss)) goto loc_C09E81;
    SI(0x9E21); a = x; ss_set_nz16(ss, a);  /* C09E21 txa */
    S(0x9E22, 3);                           /* C09E22 adc #$0012 (C clear here) */
    a = alu_adc16(ss, a, 0x0012);
    S(0x9E25, 3);                           /* C09E25 jmp loc_C09D7B */
  }

loc_C09E28:
  SI(0x9E28); a = dp; ss_set_nz16(ss, a);   /* C09E28 tdc */
  do {                                      /* loc_C09E29 */
    for(i = 0; i < sizeof kSlotClearFields / sizeof kSlotClearFields[0]; i++) {
      S(0x9E29 + 3 * i, 3); t_index(ss);    /* C09E29 sta entity_type,Y ... */
      t_write16(ss, ss_abs(ss, (uint16_t) (kSlotClearFields[i] + y)), a);
    }
    SI(0x9E7A); y = (uint16_t) (y + 1); ss_set_y(ss, y); ss_set_nz16(ss, y);  /* iny */
    SI(0x9E7B); y = (uint16_t) (y + 1); ss_set_y(ss, y); ss_set_nz16(ss, y);  /* iny */
    S(0x9E7C, 3); alu_cmp16(ss, y, 0x0020); /* C09E7C cpy #$0020 */
    S(0x9E7F, 1); t_branch(ss, !ss_c(ss));  /* C09E7F bcc loc_C09E29 */
  } while(!ss_c(ss));

loc_C09E81:
  S(0x9E81, 1);                             /* C09E81 plb */
  ss_idle(ss);
  ss_idle(ss);
  ss_check_int(ss);
  { uint8_t b = ss_pull8(ss); ss_set_db(ss, b); ss_set_nz8(ss, b); }
  ss_set_a(ss, a);
  S(0x9E82, 1);                             /* C09E82 rts */
  ss_rts(ss);
}

/* ---------------------------------------------------------------------------
 * entity_update_tick — $C0:98DA
 *
 * Entry: X = entity index (slot * 2). Called once per live entity per frame
 * from the main loop at $81C5.
 * ------------------------------------------------------------------------- */
void entity_update_tick(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint16_t dp = ss_dp(ss);

  SI(0x98DA);                               /* C098DA txy */
  y = x; ss_set_y(ss, y); ss_set_nz16(ss, y);
  S(0x98DB, 3); t_index(ss);                /* C098DB lda entity_type,X */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_type + x)));
  ss_set_nz16(ss, a);
  SI(0x98DE); x = a; ss_set_x(ss, x); ss_set_nz16(ss, x);  /* C098DE tax */

  /* C098DF jsr (jtbl_C099DD,X) — the AI handler for this entity type. jsr iax
   * pushes the pc between the two operand-byte fetches, so the pushed word is
   * $98E1 and the rts that pops it lands on $98E2. */
  {
    const uint16_t sp0 = ss_sp(ss);
    S(0x98DF, 2);
    ss_push16(ss, ss_pc(ss));
    ss_fetch(ss, 1);
    ss_idle(ss);
    const uint16_t handler =
      t_read16(ss, ((uint32_t) pb << 16) | (uint16_t) (0x99DD + x));
    ss_set_pc(ss, pb, handler);
    if(ss_run_callee(ss, sp0)) return;
    /* Four of the handlers end in `pla ; rts`: the pla drops the frame just
     * pushed, so the rts returns to *this* routine's caller and the rest of the
     * tick is skipped. Whether the ROM or a hook ran the handler, the test is
     * the same -- did control come back to $98E2? If not, everything the 65816
     * would have left is already in place, so hand the machine over. */
    if(ss_pb(ss) != pb || ss_pc(ss) != 0x98E2) return;
    a = ss_a(ss); x = ss_x(ss); y = ss_y(ss);
  }

  S(0x98E2, 3); t_index(ss);                /* C098E2 lda entity_hitstun_timer,X */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_hitstun_timer + x)));
  ss_set_nz16(ss, a);
  S(0x98E5, 1); t_branch(ss, a == 0);       /* C098E5 beq loc_C098EA */
  if(a != 0) {
    S(0x98E7, 3); t_index(ss);              /* C098E7 dec entity_hitstun_timer,X */
    const uint32_t adr = ss_abs(ss, (uint16_t) (entity_hitstun_timer + x));
    uint16_t v = (uint16_t) (t_rmw_r16(ss, adr) - 1);
    ss_idle(ss);
    t_rmw_w16(ss, adr, v);
    ss_set_nz16(ss, v);
  }

  S(0x98EA, 3); t_index(ss);                /* C098EA lda entity_type,X */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_type + x)));
  ss_set_nz16(ss, a);
  SI(0x98ED); y = a; ss_set_y(ss, y); ss_set_nz16(ss, y);  /* C098ED tay */
  S(0x98EE, 3);                             /* C098EE pea $8080 */
  ss_push8(ss, 0x80);
  ss_check_int(ss);
  ss_push8(ss, 0x80);
  S(0x98F1, 1);                             /* C098F1 plb */
  ss_idle(ss);
  ss_idle(ss);
  ss_check_int(ss);
  { uint8_t b = ss_pull8(ss); ss_set_db(ss, b); ss_set_nz8(ss, b); }

  S(0x98F2, 3); t_index(ss);                /* C098F2 lda entity_hitstun_timer,X */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_hitstun_timer + x)));
  ss_set_nz16(ss, a);
  S(0x98F5, 1); t_branch(ss, a == 0);       /* C098F5 beq loc_C098FC */
  bool have_target = false;
  if(a != 0) {
    S(0x98F7, 3); t_index(ss);              /* C098F7 lda entity_vel_x_target,X */
    a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_vel_x_target + x)));
    ss_set_nz16(ss, a);
    S(0x98FA, 1); t_branch(ss, a != 0);     /* C098FA bne loc_C0990D */
    have_target = a != 0;
  }
  if(!have_target) {                        /* loc_C098FC */
    S(0x98FC, 3); t_index(ss);              /* C098FC lda entity_state,X */
    a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_state + x)));
    ss_set_nz16(ss, a);
    SI(0x98FF); ss_set_c(ss, false);        /* C098FF clc */
    S(0x9900, 3);                           /* C09900 adc $0BAC */
    a = alu_adc16(ss, a, t_read16(ss, ss_abs(ss, state_row_offset)));
    S(0x9903, 3); t_index(ss);              /* C09903 adc entity_state_velocity_table,Y */
    a = alu_adc16(ss, a, t_read16(ss, ss_abs(ss, (uint16_t) (entity_state_velocity_table + y))));
    SI(0x9906); y = a; ss_set_y(ss, y); ss_set_nz16(ss, y);  /* C09906 tay */
    S(0x9907, 3); t_index(ss);              /* C09907 lda entity_state_velocity_table,Y */
    a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_state_velocity_table + y)));
    ss_set_nz16(ss, a);
    S(0x990A, 3); t_index(ss);              /* C0990A sta entity_vel_x_target,X */
    t_write16(ss, ss_abs(ss, (uint16_t) (entity_vel_x_target + x)), a);
  }

  S(0x990D, 3);                             /* C0990D jsr entity_accelerate_velocity_x */
  if(t_call_sub(ss, pb, 0xA232)) return;
  a = ss_a(ss); x = ss_x(ss); y = ss_y(ss);

  bool airborne = false;                    /* reached loc_C0993D directly */
  S(0x9910, 3); t_index(ss);                /* C09910 lda entity_anim_id,X */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_anim_id + x)));
  ss_set_nz16(ss, a);
  S(0x9913, 1); t_branch(ss, a == 0);       /* C09913 beq loc_C09927 */
  if(a != 0) {
    S(0x9915, 3); t_index(ss);              /* C09915 lda entity_state,X */
    a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_state + x)));
    ss_set_nz16(ss, a);
    S(0x9918, 3); alu_cmp16(ss, a, 0x000C); /* C09918 cmp #$000C */
    S(0x991B, 1); t_branch(ss, !ss_c(ss));  /* C0991B bcc loc_C09927 */
    if(ss_c(ss)) {
      S(0x991D, 3); alu_cmp16(ss, a, 0x0024);  /* C0991D cmp #$0024 */
      S(0x9920, 1); t_branch(ss, ss_c(ss));    /* C09920 bcs loc_C0993D */
      if(ss_c(ss)) {
        airborne = true;
      } else {
        S(0x9922, 3); alu_cmp16(ss, a, 0x0018);  /* C09922 cmp #$0018 */
        S(0x9925, 1); t_branch(ss, !ss_c(ss));   /* C09925 bcc loc_C0993D */
        airborne = !ss_c(ss);
      }
    }
  }

  bool moving = false;
  if(!airborne) {                           /* loc_C09927 */
    S(0x9927, 3); t_index(ss);              /* C09927 lda entity_vel_x,X */
    a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_vel_x + x)));
    ss_set_nz16(ss, a);
    S(0x992A, 1); t_branch(ss, a != 0);     /* C0992A bne loc_C09948 */
    moving = a != 0;
    if(!moving) {
      S(0x992C, 3); t_index(ss);            /* C0992C stz entity_vel_y,X */
      t_write16(ss, ss_abs(ss, (uint16_t) (entity_vel_y + x)), 0);
      S(0x992F, 3); a = 0x0000; ss_set_nz16(ss, a);  /* C0992F lda #$0000 */
      S(0x9932, 3); t_index(ss);            /* C09932 bit entity_flags,X */
      alu_bit16(ss, a, t_read16(ss, ss_abs(ss, (uint16_t) (entity_flags + x))));
      S(0x9935, 1); t_branch(ss, ss_v(ss)); /* C09935 bvs loc_C0993A */
      if(!ss_v(ss)) {
        S(0x9937, 3); a = 0x0002; ss_set_nz16(ss, a);  /* C09937 lda #$0002 */
      }
      S(0x993A, 3); t_index(ss);            /* C0993A sta entity_state,X */
      t_write16(ss, ss_abs(ss, (uint16_t) (entity_state + x)), a);
    }
  }

  if(!moving) {                             /* loc_C0993D */
    S(0x993D, 3);                           /* C0993D jsr entity_ground_y_lookup */
    if(t_call_sub(ss, pb, 0x9BDA)) return;
    a = ss_a(ss); x = ss_x(ss); y = ss_y(ss);
    S(0x9940, 3); t_index(ss);              /* C09940 lda entity_flags,X */
    a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_flags + x)));
    ss_set_nz16(ss, a);
    S(0x9943, 3); alu_cmp16(ss, a, 0x4000); /* C09943 cmp #$4000 */
    S(0x9946, 1); t_branch(ss, true);       /* C09946 bra loc_C0995A */
  } else {                                  /* loc_C09948 */
    S(0x9948, 3);                           /* C09948 jsr entity_ground_y_lookup */
    if(t_call_sub(ss, pb, 0x9BDA)) return;
    a = ss_a(ss); x = ss_x(ss); y = ss_y(ss);
    S(0x994B, 3);                           /* C0994B jsr sub_C0A1F3 */
    if(t_call_sub(ss, pb, 0xA1F3)) return;
    a = ss_a(ss); x = ss_x(ss); y = ss_y(ss);
    S(0x994E, 3);                           /* C0994E jsr entity_apply_velocity_x */
    if(t_call_sub(ss, pb, 0xA26F)) return;
    a = ss_a(ss); x = ss_x(ss); y = ss_y(ss);
    S(0x9951, 3);                           /* C09951 jsr entity_apply_velocity_y */
    if(t_call_sub(ss, pb, 0xA2B9)) return;
    a = ss_a(ss); x = ss_x(ss); y = ss_y(ss);
    S(0x9954, 3); t_index(ss);              /* C09954 lda entity_vel_x,X */
    a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_vel_x + x)));
    ss_set_nz16(ss, a);
    S(0x9957, 3); alu_cmp16(ss, a, 0x8000); /* C09957 cmp #$8000 */
  }

  SI(0x995A); a = alu_rol16(ss, a);         /* C0995A rol A */
  SI(0x995B); a = alu_asl16(ss, a);         /* C0995B asl A */
  S(0x995C, 3); a = alu_and16(ss, a, 0x0002);  /* C0995C and #$0002 */
  S(0x995F, 3);                             /* C0995F ora $0BAE */
  a = alu_ora16(ss, a, t_read16(ss, ss_abs(ss, ground_probe_result)));
  S(0x9962, 3); a = alu_and16(ss, a, 0x000E);  /* C09962 and #$000E */
  SI(0x9965); a = alu_asl16(ss, a);         /* C09965 asl A */
  SI(0x9966); y = a; ss_set_y(ss, y); ss_set_nz16(ss, y);  /* C09966 tay */

  S(0x9967, 3); t_index(ss);                /* C09967 lda entity_flags,X */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_flags + x)));
  ss_set_nz16(ss, a);
  S(0x996A, 3); a = alu_and16(ss, a, 0xBFFF);  /* C0996A and #$BFFF */
  S(0x996D, 3); t_index(ss);                /* C0996D ora facing_flag_table,Y */
  a = alu_ora16(ss, a, t_read16(ss, ss_abs(ss, (uint16_t) (facing_flag_table + y))));
  S(0x9970, 3); t_index(ss);                /* C09970 sta entity_flags,X */
  t_write16(ss, ss_abs(ss, (uint16_t) (entity_flags + x)), a);
  S(0x9973, 3); t_index(ss);                /* C09973 lda entity_state,X */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_state + x)));
  ss_set_nz16(ss, a);
  S(0x9976, 3); a = alu_and16(ss, a, 0xFFFC);  /* C09976 and #$FFFC */
  S(0x9979, 3); t_index(ss);                /* C09979 ora facing_state_bits_table,Y */
  a = alu_ora16(ss, a, t_read16(ss, ss_abs(ss, (uint16_t) (facing_state_bits_table + y))));
  S(0x997C, 3); t_index(ss);                /* C0997C sta entity_state,X */
  t_write16(ss, ss_abs(ss, (uint16_t) (entity_state + x)), a);

  S(0x997F, 3); t_index(ss);                /* C0997F lda entity_type,X */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_type + x)));
  ss_set_nz16(ss, a);
  SI(0x9982); y = a; ss_set_y(ss, y); ss_set_nz16(ss, y);  /* C09982 tay */
  S(0x9983, 3); t_index(ss);                /* C09983 lda entity_state,X */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_state + x)));
  ss_set_nz16(ss, a);
  SI(0x9986); ss_set_c(ss, false);          /* C09986 clc */
  S(0x9987, 3);                             /* C09987 adc $0BAC */
  a = alu_adc16(ss, a, t_read16(ss, ss_abs(ss, state_row_offset)));
  S(0x998A, 3); t_index(ss);                /* C0998A adc entity_state_anim_table,Y */
  a = alu_adc16(ss, a, t_read16(ss, ss_abs(ss, (uint16_t) (entity_state_anim_table + y))));
  SI(0x998D); y = a; ss_set_y(ss, y); ss_set_nz16(ss, y);  /* C0998D tay */
  S(0x998E, 3); t_index(ss);                /* C0998E lda entity_state_anim_table,Y */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_state_anim_table + y)));
  ss_set_nz16(ss, a);
  S(0x9991, 3); t_index(ss);                /* C09991 sta entity_anim_id,X */
  t_write16(ss, ss_abs(ss, (uint16_t) (entity_anim_id + x)), a);

  S(0x9994, 3); t_index(ss);                /* C09994 lda entity_type,X */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_type + x)));
  ss_set_nz16(ss, a);
  SI(0x9997); y = a; ss_set_y(ss, y); ss_set_nz16(ss, y);  /* C09997 tay */
  S(0x9998, 3); t_index(ss);                /* C09998 lda entity_state,X */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_state + x)));
  ss_set_nz16(ss, a);
  S(0x999B, 3);                             /* C0999B adc $0BAC (no clc: C from the ora chain) */
  a = alu_adc16(ss, a, t_read16(ss, ss_abs(ss, state_row_offset)));
  SI(0x999E); a = alu_lsr16(ss, a);         /* C0999E lsr A */
  S(0x999F, 3); a = alu_and16(ss, a, 0xFFFE);  /* C0999F and #$FFFE */
  S(0x99A2, 3); t_index(ss);                /* C099A2 adc anim_rate_fn_table,Y */
  a = alu_adc16(ss, a, t_read16(ss, ss_abs(ss, (uint16_t) (anim_rate_fn_table + y))));
  SI(0x99A5); y = a; ss_set_y(ss, y); ss_set_nz16(ss, y);  /* C099A5 tay */
  S(0x99A6, 3); t_index(ss);                /* C099A6 lda anim_rate_fn_table,Y */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (anim_rate_fn_table + y)));
  ss_set_nz16(ss, a);
  S(0x99A9, 1); t_branch(ss, a == 0);       /* C099A9 beq anim_rate_store */
  if(a == 0) {
    /* The shared tail lives in recomp/src/anim.c and has its own entry, so hand
     * the machine the address the branch reaches: the hook there fires next. */
    ss_set_a(ss, a);
    ss_set_pc(ss, pb, 0x99D4);
    return;
  }
  S(0x99AB, 2);                             /* C099AB sta ptr_04 */
  t_write16(ss, dp + ptr_04, a);
  S(0x99AD, 3); t_index(ss);                /* C099AD lda entity_vel_x,X */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_vel_x + x)));
  ss_set_nz16(ss, a);
  S(0x99B0, 1); t_branch(ss, (a & 0x8000) == 0);  /* C099B0 bpl loc_C099B6 */
  if((a & 0x8000) != 0) {
    S(0x99B2, 3); a = alu_eor16(ss, a, 0xFFFF);   /* C099B2 eor #$FFFF */
    SI(0x99B5); a = alu_inc16(ss, a);             /* C099B5 inc A */
  }

  /* C099B6 jmp ($0004) — the animation-rate handler picked out of
   * anim_rate_fn_table. They are converted in recomp/src/anim.c and every one
   * of them is a tail: it falls through into anim_rate_store, whose rts ends
   * this routine. Pointing the pc at the handler is what the jmp does, and the
   * hook installed there takes it from here. */
  S(0x99B6, 3);
  { const uint16_t target = t_read16(ss, ptr_04);
    ss_set_a(ss, a);
    ss_set_pc(ss, pb, target); }
}

/* ---------------------------------------------------------------------------
 * entity_ai_chase_player — $C0:99EF
 *
 * jtbl_C099DD's entry for type $0C. Entry: Y = entity index, X = entity type.
 * Turns the signed distance to the player (entity 0) into one of four states,
 * arms a random delay in entity_attack_timer, and otherwise ages that delay one
 * step per tick with an 8-bit dec so the high byte's $8000 marker survives.
 * ------------------------------------------------------------------------- */
void entity_ai_chase_player(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint16_t dp = ss_dp(ss);

  SI(0x99EF);                               /* C099EF tyx */
  x = y; ss_set_x(ss, x); ss_set_nz16(ss, x);
  S(0x99F0, 3); t_index(ss);                /* C099F0 lda entity_type,X */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_type + x)));
  ss_set_nz16(ss, a);
  S(0x99F3, 3); alu_cmp16(ss, a, 0x000C);   /* C099F3 cmp #$000C */
  S(0x99F6, 1); t_branch(ss, a != 0x000C);  /* C099F6 bne loc_C09A51 */
  if(a != 0x000C) goto loc_C09A51;

  S(0x99F8, 3); t_index(ss);                /* C099F8 lda entity_hitstun_timer,X */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_hitstun_timer + x)));
  ss_set_nz16(ss, a);
  S(0x99FB, 1); t_branch(ss, a != 0);       /* C099FB bne loc_C09A51 */
  if(a != 0) goto loc_C09A51;

  S(0x99FD, 3);                             /* C099FD lda entity_state (player) */
  a = t_read16(ss, ss_abs(ss, entity_state));
  ss_set_nz16(ss, a);
  S(0x9A00, 3); alu_cmp16(ss, a, 0x000C);   /* C09A00 cmp #$000C */
  S(0x9A03, 1); t_branch(ss, ss_c(ss));     /* C09A03 bcs loc_C09A11 */
  if(!ss_c(ss)) {
    S(0x9A05, 3); t_index(ss);              /* C09A05 lda entity_attack_timer,X */
    a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_attack_timer + x)));
    ss_set_nz16(ss, a);
    S(0x9A08, 1); t_branch(ss, (a & 0x8000) != 0);  /* C09A08 bmi loc_C09A11 */
    if((a & 0x8000) == 0) {
      S(0x9A0A, 1); t_branch(ss, a != 0);   /* C09A0A bne loc_C09A49 */
      if(a != 0) goto loc_C09A49;
      S(0x9A0C, 3); t_index(ss);            /* C09A0C lda $0A08,X */
      a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_field_0A08 + x)));
      ss_set_nz16(ss, a);
      S(0x9A0F, 1); t_branch(ss, a != 0);   /* C09A0F bne loc_C09A51 */
      if(a != 0) goto loc_C09A51;
    }
  }

                                            /* loc_C09A11 */
  S(0x9A11, 3);                             /* C09A11 lda entity_x (player) */
  a = t_read16(ss, ss_abs(ss, entity_x));
  ss_set_nz16(ss, a);
  SI(0x9A14); ss_set_c(ss, true);           /* C09A14 sec */
  S(0x9A15, 3); t_index(ss);                /* C09A15 sbc entity_x,X */
  a = alu_sbc16(ss, a, t_read16(ss, ss_abs(ss, (uint16_t) (entity_x + x))));
  S(0x9A18, 2);                             /* C09A18 sta ptr_04 */
  t_write16(ss, dp + ptr_04, a);
  S(0x9A1A, 1); t_branch(ss, (a & 0x8000) == 0);  /* C09A1A bpl loc_C09A20 */
  if((a & 0x8000) != 0) {
    S(0x9A1C, 3); a = alu_eor16(ss, a, 0xFFFF);   /* C09A1C eor #$FFFF */
    SI(0x9A1F); a = alu_inc16(ss, a);             /* C09A1F inc A */
  }
                                            /* loc_C09A20 */
  S(0x9A20, 3); alu_cmp16(ss, a, 0x0040);   /* C09A20 cmp #$0040 */
  S(0x9A23, 1); t_branch(ss, !ss_c(ss));    /* C09A23 bcc loc_C09A52 */
  if(!ss_c(ss)) {                           /* loc_C09A52 */
    S(0x9A52, 3); a = 0x0000; ss_set_nz16(ss, a);  /* C09A52 lda #$0000 */
    S(0x9A55, 3); t_index(ss);              /* C09A55 sta entity_state,X */
    t_write16(ss, ss_abs(ss, (uint16_t) (entity_state + x)), a);
    S(0x9A58, 3); a = 0x0001; ss_set_nz16(ss, a);  /* C09A58 lda #$0001 */
    S(0x9A5B, 3); t_index(ss);              /* C09A5B sta entity_attack_timer,X */
    t_write16(ss, ss_abs(ss, (uint16_t) (entity_attack_timer + x)), a);
    ss_set_a(ss, a);
    S(0x9A5E, 1);                           /* C09A5E rts */
    ss_rts(ss);
    return;
  }
  S(0x9A25, 3); alu_cmp16(ss, a, 0x0080);   /* C09A25 cmp #$0080 */
  S(0x9A28, 1); t_branch(ss, !ss_c(ss));    /* C09A28 bcc loc_C09A2F */
  if(ss_c(ss)) {
    S(0x9A2A, 3); a = 0x0008; ss_set_nz16(ss, a);  /* C09A2A lda #$0008 */
    S(0x9A2D, 1); t_branch(ss, true);       /* C09A2D bra loc_C09A32 */
  } else {
    S(0x9A2F, 3); a = 0x0004; ss_set_nz16(ss, a);  /* C09A2F lda #$0004 */
  }
                                            /* loc_C09A32 */
  S(0x9A32, 2);                             /* C09A32 bit ptr_04 */
  alu_bit16(ss, a, t_read16(ss, dp + ptr_04));
  S(0x9A34, 1); t_branch(ss, ss_n(ss));     /* C09A34 bmi loc_C09A38 */
  if(!ss_n(ss)) {
    SI(0x9A36); a = alu_inc16(ss, a);       /* C09A36 inc A */
    SI(0x9A37); a = alu_inc16(ss, a);       /* C09A37 inc A */
  }
                                            /* loc_C09A38 */
  S(0x9A38, 3); t_index(ss);                /* C09A38 sta entity_state,X */
  t_write16(ss, ss_abs(ss, (uint16_t) (entity_state + x)), a);
  S(0x9A3B, 3);                             /* C09A3B jsr random_next */
  if(t_call_sub(ss, pb, 0xA212)) return;
  a = ss_a(ss); x = ss_x(ss); y = ss_y(ss);
  S(0x9A3E, 2);                             /* C09A3E lda init_magic_AA55 */
  a = t_read16(ss, dp + init_magic_AA55);
  ss_set_nz16(ss, a);
  S(0x9A40, 3); a = alu_and16(ss, a, 0x003F);  /* C09A40 and #$003F */
  S(0x9A43, 3); a = alu_ora16(ss, a, 0x8000);  /* C09A43 ora #$8000 */
  S(0x9A46, 3); t_index(ss);                /* C09A46 sta entity_attack_timer,X */
  t_write16(ss, ss_abs(ss, (uint16_t) (entity_attack_timer + x)), a);

loc_C09A49:
  SEP(0x9A49, 0x20);                        /* C09A49 sep #$20 */
  SI(0x9A4B);                               /* C09A4B dec A (8-bit) */
  a = (uint16_t) ((a & 0xff00) | ((a - 1) & 0x00ff));
  ss_set_nz8(ss, (uint8_t) a);
  REP(0x9A4C, 0x20);                        /* C09A4C rep #$20 */
  S(0x9A4E, 3); t_index(ss);                /* C09A4E sta entity_attack_timer,X */
  t_write16(ss, ss_abs(ss, (uint16_t) (entity_attack_timer + x)), a);

loc_C09A51:
  ss_set_a(ss, a);
  S(0x9A51, 1);                             /* C09A51 rts */
  ss_rts(ss);
}

/* ---------------------------------------------------------------------------
 * entity_animate_only — $C0:9AF6, entity_spawn_transform_a/b/c — $9AB8/$9B00/$9B89
 *
 * jtbl_C099DD's entries for types 0, 4, 6/8 and $0A. All four end the same way:
 * `jsl anim_update ; pla ; rts`, where the pla drops the return address
 * entity_update_tick's jsr pushed, so the rts leaves the whole tick. The three
 * transforms first copy a parent entity's (entity_parent_index,X -> Y) position
 * and orientation bits into this slot and derive its animation id from the
 * parent's; docs/naming_proposals.md section 4 tabulates the differences.
 * ------------------------------------------------------------------------- */

/* the shared `pla ; rts` tail; `at` is the address of the pla */
static void spawn_transform_tail(SnesState* ss, uint16_t at) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  S(at, 1);                                 /* C09AF4 pla */
  ss_idle(ss);
  ss_idle(ss);
  a = t_pull16(ss);
  ss_set_a(ss, a);
  ss_set_nz16(ss, a);
  S(at + 1, 1);                             /* C09AF5 rts */
  ss_rts(ss);
}

void entity_animate_only(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);

  SI(0x9AF6);                               /* C09AF6 tyx */
  x = y; ss_set_x(ss, x); ss_set_nz16(ss, x);
  S(0x9AF7, 3);                             /* C09AF7 jsl anim_update */
  if(t_call_long(ss, anim_update_bank, anim_update_entry)) return;
  spawn_transform_tail(ss, 0x9AFB);
}

void entity_spawn_transform_a(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);

  SI(0x9AB8);                               /* C09AB8 tyx */
  x = y; ss_set_x(ss, x); ss_set_nz16(ss, x);
  S(0x9AB9, 3); t_index(ss);                /* C09AB9 lda entity_parent_index,X */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_parent_index + x)));
  ss_set_nz16(ss, a);
  SI(0x9ABC); y = a; ss_set_y(ss, y); ss_set_nz16(ss, y);  /* C09ABC tay */

  {
    static const uint16_t kCopy[3] = { entity_x, entity_y, entity_z_dead };
    for(unsigned i = 0; i < 3; i++) {
      const uint16_t at = (uint16_t) (0x9ABD + 6 * i);
      S(at, 3); t_index(ss);                /* C09ABD lda entity_x,Y ... */
      a = t_read16(ss, ss_abs(ss, (uint16_t) (kCopy[i] + y)));
      ss_set_nz16(ss, a);
      S(at + 3, 3); t_index(ss);            /* C09AC0 sta entity_x,X ... */
      t_write16(ss, ss_abs(ss, (uint16_t) (kCopy[i] + x)), a);
    }
  }

  S(0x9ACF, 3); t_index(ss);                /* C09ACF lda entity_flags,Y */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_flags + y)));
  ss_set_nz16(ss, a);
  S(0x9AD2, 3); t_index(ss);                /* C09AD2 eor entity_flags,X */
  a = alu_eor16(ss, a, t_read16(ss, ss_abs(ss, (uint16_t) (entity_flags + x))));
  S(0x9AD5, 3); a = alu_and16(ss, a, 0x7000);  /* C09AD5 and #$7000 */
  S(0x9AD8, 3); t_index(ss);                /* C09AD8 eor entity_flags,X */
  a = alu_eor16(ss, a, t_read16(ss, ss_abs(ss, (uint16_t) (entity_flags + x))));
  S(0x9ADB, 3); t_index(ss);                /* C09ADB sta entity_flags,X */
  t_write16(ss, ss_abs(ss, (uint16_t) (entity_flags + x)), a);

  S(0x9ADE, 3); t_index(ss);                /* C09ADE lda entity_anim_id,Y */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_anim_id + y)));
  ss_set_nz16(ss, a);
  S(0x9AE1, 1); t_branch(ss, a == 0);       /* C09AE1 beq loc_C09AE7 */
  if(a != 0) {
    SI(0x9AE3); ss_set_c(ss, false);        /* C09AE3 clc */
    S(0x9AE4, 3); a = alu_adc16(ss, a, 0x0002);  /* C09AE4 adc #$0002 */
  }
  S(0x9AE7, 3); t_index(ss);                /* C09AE7 sta entity_anim_id,X */
  t_write16(ss, ss_abs(ss, (uint16_t) (entity_anim_id + x)), a);
  S(0x9AEA, 3); t_index(ss);                /* C09AEA lda entity_anim_rate,Y */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_anim_rate + y)));
  ss_set_nz16(ss, a);
  S(0x9AED, 3); t_index(ss);                /* C09AED sta entity_anim_rate,X */
  t_write16(ss, ss_abs(ss, (uint16_t) (entity_anim_rate + x)), a);
  S(0x9AF0, 3);                             /* C09AF0 jsl anim_update */
  if(t_call_long(ss, anim_update_bank, anim_update_entry)) return;
  spawn_transform_tail(ss, 0x9AF4);
}

void entity_spawn_transform_b(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint16_t dp = ss_dp(ss);

  SI(0x9B00);                               /* C09B00 tyx */
  x = y; ss_set_x(ss, x); ss_set_nz16(ss, x);
  S(0x9B01, 3);                             /* C09B01 lda $0BAC */
  a = t_read16(ss, ss_abs(ss, state_row_offset));
  ss_set_nz16(ss, a);
  S(0x9B04, 1); t_branch(ss, a != 0);       /* C09B04 bne loc_C09B51 */
  if(a != 0) goto loc_C09B51;

  S(0x9B06, 2);                             /* C09B06 lda game_mode */
  a = t_read16(ss, dp + game_mode);
  ss_set_nz16(ss, a);
  S(0x9B08, 3); alu_cmp16(ss, a, 0x0002);   /* C09B08 cmp #$0002 */
  S(0x9B0B, 1); t_branch(ss, a == 0x0002);  /* C09B0B beq loc_C09B4F */
  if(a == 0x0002) { spawn_transform_tail(ss, 0x9B4F); return; }

  S(0x9B0D, 3); t_index(ss);                /* C09B0D lda entity_parent_index,X */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_parent_index + x)));
  ss_set_nz16(ss, a);
  SI(0x9B10); y = a; ss_set_y(ss, y); ss_set_nz16(ss, y);  /* C09B10 tay */
  S(0x9B11, 3); t_index(ss);                /* C09B11 lda entity_x,Y */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_x + y)));
  ss_set_nz16(ss, a);
  S(0x9B14, 3); t_index(ss);                /* C09B14 sta entity_x,X */
  t_write16(ss, ss_abs(ss, (uint16_t) (entity_x + x)), a);
  S(0x9B17, 3); t_index(ss);                /* C09B17 lda $08E8,Y */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_z_dead + y)));
  ss_set_nz16(ss, a);
  SI(0x9B1A); ss_set_c(ss, false);          /* C09B1A clc */
  S(0x9B1B, 3); a = alu_adc16(ss, a, 0x0010);  /* C09B1B adc #$0010 */
  S(0x9B1E, 3); t_index(ss);                /* C09B1E sta $08E8,X */
  t_write16(ss, ss_abs(ss, (uint16_t) (entity_z_dead + x)), a);
  S(0x9B21, 3); t_index(ss);                /* C09B21 lda entity_y,Y */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_y + y)));
  ss_set_nz16(ss, a);
  S(0x9B24, 3); t_index(ss);                /* C09B24 sta entity_y,X */
  t_write16(ss, ss_abs(ss, (uint16_t) (entity_y + x)), a);

  S(0x9B27, 3); t_index(ss);                /* C09B27 lda entity_flags,Y */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_flags + y)));
  ss_set_nz16(ss, a);
  S(0x9B2A, 3); t_index(ss);                /* C09B2A eor entity_flags,X */
  a = alu_eor16(ss, a, t_read16(ss, ss_abs(ss, (uint16_t) (entity_flags + x))));
  S(0x9B2D, 3); a = alu_and16(ss, a, 0x4000);  /* C09B2D and #$4000 */
  S(0x9B30, 3); t_index(ss);                /* C09B30 eor entity_flags,X */
  a = alu_eor16(ss, a, t_read16(ss, ss_abs(ss, (uint16_t) (entity_flags + x))));
  S(0x9B33, 3); a = alu_ora16(ss, a, 0x8000);  /* C09B33 ora #$8000 */
  S(0x9B36, 3); t_index(ss);                /* C09B36 sta entity_flags,X */
  t_write16(ss, ss_abs(ss, (uint16_t) (entity_flags + x)), a);

  S(0x9B39, 3); t_index(ss);                /* C09B39 lda entity_anim_id,Y */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_anim_id + y)));
  ss_set_nz16(ss, a);
  S(0x9B3C, 1); t_branch(ss, a == 0);       /* C09B3C beq loc_C09B42 */
  if(a != 0) {
    SI(0x9B3E); ss_set_c(ss, false);        /* C09B3E clc */
    S(0x9B3F, 3); a = alu_adc16(ss, a, 0x0004);  /* C09B3F adc #$0004 */
  }
  S(0x9B42, 3); t_index(ss);                /* C09B42 sta entity_anim_id,X */
  t_write16(ss, ss_abs(ss, (uint16_t) (entity_anim_id + x)), a);
  S(0x9B45, 3); t_index(ss);                /* C09B45 lda entity_anim_rate,Y */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_anim_rate + y)));
  ss_set_nz16(ss, a);
  S(0x9B48, 3); t_index(ss);                /* C09B48 sta entity_anim_rate,X */
  t_write16(ss, ss_abs(ss, (uint16_t) (entity_anim_rate + x)), a);
  S(0x9B4B, 3);                             /* C09B4B jsl anim_update */
  if(t_call_long(ss, anim_update_bank, anim_update_entry)) return;
  spawn_transform_tail(ss, 0x9B4F);
  return;

loc_C09B51:
  S(0x9B51, 3); t_index(ss);                /* C09B51 lda entity_parent_index,X */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_parent_index + x)));
  ss_set_nz16(ss, a);
  SI(0x9B54); y = a; ss_set_y(ss, y); ss_set_nz16(ss, y);  /* C09B54 tay */
  S(0x9B55, 3); t_index(ss);                /* C09B55 lda entity_x,Y */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_x + y)));
  ss_set_nz16(ss, a);
  S(0x9B58, 3); t_index(ss);                /* C09B58 sta entity_x,X */
  t_write16(ss, ss_abs(ss, (uint16_t) (entity_x + x)), a);
  S(0x9B5B, 3); t_index(ss);                /* C09B5B lda $08E8,Y */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_z_dead + y)));
  ss_set_nz16(ss, a);
  S(0x9B5E, 3); t_index(ss);                /* C09B5E sta $08E8,X */
  t_write16(ss, ss_abs(ss, (uint16_t) (entity_z_dead + x)), a);
  S(0x9B61, 3); t_index(ss);                /* C09B61 lda entity_y,Y */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_y + y)));
  ss_set_nz16(ss, a);
  SI(0x9B64); a = alu_dec16(ss, a);         /* C09B64 dec A */
  S(0x9B65, 3); t_index(ss);                /* C09B65 sta entity_y,X */
  t_write16(ss, ss_abs(ss, (uint16_t) (entity_y + x)), a);
  S(0x9B68, 3); t_index(ss);                /* C09B68 lda entity_flags,Y */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_flags + y)));
  ss_set_nz16(ss, a);
  S(0x9B6B, 3); t_index(ss);                /* C09B6B eor entity_flags,X */
  a = alu_eor16(ss, a, t_read16(ss, ss_abs(ss, (uint16_t) (entity_flags + x))));
  S(0x9B6E, 3); a = alu_and16(ss, a, 0xC000);  /* C09B6E and #$C000 */
  S(0x9B71, 3); t_index(ss);                /* C09B71 eor entity_flags,X */
  a = alu_eor16(ss, a, t_read16(ss, ss_abs(ss, (uint16_t) (entity_flags + x))));
  S(0x9B74, 3); t_index(ss);                /* C09B74 sta entity_flags,X */
  t_write16(ss, ss_abs(ss, (uint16_t) (entity_flags + x)), a);
  S(0x9B77, 3); a = 0x0158; ss_set_nz16(ss, a);  /* C09B77 lda #$0158 */
  S(0x9B7A, 3); t_index(ss);                /* C09B7A sta entity_anim_id,X */
  t_write16(ss, ss_abs(ss, (uint16_t) (entity_anim_id + x)), a);
  S(0x9B7D, 3); t_index(ss);                /* C09B7D lda entity_anim_rate,Y */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_anim_rate + y)));
  ss_set_nz16(ss, a);
  S(0x9B80, 3); t_index(ss);                /* C09B80 sta entity_anim_rate,X */
  t_write16(ss, ss_abs(ss, (uint16_t) (entity_anim_rate + x)), a);
  S(0x9B83, 3);                             /* C09B83 jsl anim_update */
  if(t_call_long(ss, anim_update_bank, anim_update_entry)) return;
  spawn_transform_tail(ss, 0x9B87);
}

void entity_spawn_transform_c(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);

  SI(0x9B89);                               /* C09B89 tyx */
  x = y; ss_set_x(ss, x); ss_set_nz16(ss, x);
  S(0x9B8A, 3); t_index(ss);                /* C09B8A lda entity_parent_index,X */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_parent_index + x)));
  ss_set_nz16(ss, a);
  SI(0x9B8D); y = a; ss_set_y(ss, y); ss_set_nz16(ss, y);  /* C09B8D tay */

  {
    static const uint16_t kCopy[3] = { entity_x, entity_y, entity_z_dead };
    for(unsigned i = 0; i < 3; i++) {
      const uint16_t at = (uint16_t) (0x9B8E + 6 * i);
      S(at, 3); t_index(ss);                /* C09B8E lda entity_x,Y ... */
      a = t_read16(ss, ss_abs(ss, (uint16_t) (kCopy[i] + y)));
      ss_set_nz16(ss, a);
      S(at + 3, 3); t_index(ss);            /* C09B91 sta entity_x,X ... */
      t_write16(ss, ss_abs(ss, (uint16_t) (kCopy[i] + x)), a);
    }
  }

  S(0x9BA0, 3); t_index(ss);                /* C09BA0 lda entity_flags,Y */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_flags + y)));
  ss_set_nz16(ss, a);
  S(0x9BA3, 3); t_index(ss);                /* C09BA3 eor entity_flags,X */
  a = alu_eor16(ss, a, t_read16(ss, ss_abs(ss, (uint16_t) (entity_flags + x))));
  S(0x9BA6, 3); a = alu_and16(ss, a, 0x7000);  /* C09BA6 and #$7000 */
  S(0x9BA9, 3); t_index(ss);                /* C09BA9 eor entity_flags,X */
  a = alu_eor16(ss, a, t_read16(ss, ss_abs(ss, (uint16_t) (entity_flags + x))));
  S(0x9BAC, 3); t_index(ss);                /* C09BAC sta entity_flags,X */
  t_write16(ss, ss_abs(ss, (uint16_t) (entity_flags + x)), a);

  S(0x9BAF, 3);                             /* C09BAF lda $0BB6 */
  a = t_read16(ss, ss_abs(ss, mode1_row_index));
  ss_set_nz16(ss, a);
  S(0x9BB2, 1); t_branch(ss, a == 0);       /* C09BB2 beq loc_C09BD5 */
  if(a == 0) {
    S(0x9BD5, 3); t_index(ss);              /* C09BD5 stz entity_substate,X */
    t_write16(ss, ss_abs(ss, (uint16_t) (entity_substate + x)), 0);
    spawn_transform_tail(ss, 0x9BD8);
    return;
  }
  S(0x9BB4, 3); a = 0x0002; ss_set_nz16(ss, a);  /* C09BB4 lda #$0002 */
  S(0x9BB7, 3); t_index(ss);                /* C09BB7 sta entity_substate,X */
  t_write16(ss, ss_abs(ss, (uint16_t) (entity_substate + x)), a);
  S(0x9BBA, 3); t_index(ss);                /* C09BBA lda entity_anim_id,Y */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_anim_id + y)));
  ss_set_nz16(ss, a);
  S(0x9BBD, 1); t_branch(ss, a == 0);       /* C09BBD beq loc_C09BC6 */
  if(a != 0) {
    SI(0x9BBF); ss_set_c(ss, false);        /* C09BBF clc */
    S(0x9BC0, 3); a = alu_adc16(ss, a, 0x0004);  /* C09BC0 adc #$0004 */
    S(0x9BC3, 3);                           /* C09BC3 adc $0BB6 */
    a = alu_adc16(ss, a, t_read16(ss, ss_abs(ss, mode1_row_index)));
  }
  S(0x9BC6, 3); t_index(ss);                /* C09BC6 sta entity_anim_id,X */
  t_write16(ss, ss_abs(ss, (uint16_t) (entity_anim_id + x)), a);
  S(0x9BC9, 3); t_index(ss);                /* C09BC9 lda entity_anim_rate,Y */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_anim_rate + y)));
  ss_set_nz16(ss, a);
  S(0x9BCC, 3); t_index(ss);                /* C09BCC sta entity_anim_rate,X */
  t_write16(ss, ss_abs(ss, (uint16_t) (entity_anim_rate + x)), a);
  S(0x9BCF, 3);                             /* C09BCF jsl anim_update */
  if(t_call_long(ss, anim_update_bank, anim_update_entry)) return;
  spawn_transform_tail(ss, 0x9BD3);
}

/* ---------------------------------------------------------------------------
 * anim_cb_reset_state — $C0:B1AE, set_entity_state — $C0:B1B1
 *
 * anim_cb_reset_state is `lda #$0000` falling into set_entity_state, the way the
 * animation-rate handlers fall into their shared tail, so the two are one body
 * here as well. set_entity_state re-derives facing from the ground probe, ORs
 * the new state word in, rewinds the animation cursor and picks the animation id
 * for the new (type, state) pair. Entry: A = the new state, X = entity index.
 * ------------------------------------------------------------------------- */
static void set_entity_state_body(SnesState* ss, uint16_t a) {
  const uint8_t pb = ss_pb(ss);
  uint16_t x = ss_x(ss), y = ss_y(ss);
  const uint16_t dp = ss_dp(ss);

  S(0xB1B1, 2);                             /* C0B1B1 sta $18 */
  t_write16(ss, dp + scratch_18, a);
  S(0xB1B3, 3);                             /* C0B1B3 jsr entity_ground_y_lookup */
  if(t_call_sub(ss, pb, 0x9BDA)) return;
  a = ss_a(ss); x = ss_x(ss); y = ss_y(ss);
  SI(0xB1B6);                               /* C0B1B6 txy */
  y = x; ss_set_y(ss, y); ss_set_nz16(ss, y);

  S(0xB1B7, 3); t_index(ss);                /* C0B1B7 lda entity_flags,Y */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_flags + y)));
  ss_set_nz16(ss, a);
  S(0xB1BA, 3); alu_cmp16(ss, a, 0x4000);   /* C0B1BA cmp #$4000 */
  SI(0xB1BD); a = alu_rol16(ss, a);         /* C0B1BD rol A */
  SI(0xB1BE); a = alu_asl16(ss, a);         /* C0B1BE asl A */
  S(0xB1BF, 3); a = alu_and16(ss, a, 0x0002);  /* C0B1BF and #$0002 */
  S(0xB1C2, 3);                             /* C0B1C2 ora $0BAE */
  a = alu_ora16(ss, a, t_read16(ss, ss_abs(ss, ground_probe_result)));
  S(0xB1C5, 3); a = alu_and16(ss, a, 0x000E);  /* C0B1C5 and #$000E */
  SI(0xB1C8); a = alu_asl16(ss, a);         /* C0B1C8 asl A */
  SI(0xB1C9); x = a; ss_set_x(ss, x); ss_set_nz16(ss, x);  /* C0B1C9 tax */

  S(0xB1CA, 3); t_index(ss);                /* C0B1CA lda entity_flags,Y */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_flags + y)));
  ss_set_nz16(ss, a);
  S(0xB1CD, 3); a = alu_and16(ss, a, 0xBFFF);  /* C0B1CD and #$BFFF */
  S(0xB1D0, 4);                             /* C0B1D0 ora facing_flag_table,X */
  a = alu_ora16(ss, a, t_read16(ss, 0x800000 + facing_flag_table + x));
  S(0xB1D4, 3); t_index(ss);                /* C0B1D4 sta entity_flags,Y */
  t_write16(ss, ss_abs(ss, (uint16_t) (entity_flags + y)), a);
  S(0xB1D7, 2);                             /* C0B1D7 lda $18 */
  a = t_read16(ss, dp + scratch_18);
  ss_set_nz16(ss, a);
  S(0xB1D9, 4);                             /* C0B1D9 ora facing_state_bits_table,X */
  a = alu_ora16(ss, a, t_read16(ss, 0x800000 + facing_state_bits_table + x));
  S(0xB1DD, 3); t_index(ss);                /* C0B1DD sta entity_state,Y */
  t_write16(ss, ss_abs(ss, (uint16_t) (entity_state + y)), a);
  SI(0xB1E0);                               /* C0B1E0 tyx */
  x = y; ss_set_x(ss, x); ss_set_nz16(ss, x);
  S(0xB1E1, 3); t_index(ss);                /* C0B1E1 stz $0A08,X */
  t_write16(ss, ss_abs(ss, (uint16_t) (entity_field_0A08 + x)), 0);
  S(0xB1E4, 3); t_index(ss);                /* C0B1E4 stz $0A28,X */
  t_write16(ss, ss_abs(ss, (uint16_t) (entity_field_0A28 + x)), 0);

  S(0xB1E7, 3); t_index(ss);                /* C0B1E7 ldx entity_type,Y */
  x = t_read16(ss, ss_abs(ss, (uint16_t) (entity_type + y)));
  ss_set_x(ss, x); ss_set_nz16(ss, x);
  S(0xB1EA, 3); t_index(ss);                /* C0B1EA lda entity_state,Y */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_state + y)));
  ss_set_nz16(ss, a);
  S(0xB1ED, 3);                             /* C0B1ED adc $0BAC */
  a = alu_adc16(ss, a, t_read16(ss, ss_abs(ss, state_row_offset)));
  S(0xB1F0, 4);                             /* C0B1F0 adc entity_state_anim_table,X */
  a = alu_adc16(ss, a, t_read16(ss, 0x800000 + entity_state_anim_table + x));
  SI(0xB1F4); x = a; ss_set_x(ss, x); ss_set_nz16(ss, x);  /* C0B1F4 tax */
  S(0xB1F5, 4);                             /* C0B1F5 lda entity_state_anim_table,X */
  a = t_read16(ss, 0x800000 + entity_state_anim_table + x);
  ss_set_nz16(ss, a);
  S(0xB1F9, 3); t_index(ss);                /* C0B1F9 sta entity_anim_id,Y */
  t_write16(ss, ss_abs(ss, (uint16_t) (entity_anim_id + y)), a);
  SI(0xB1FC);                               /* C0B1FC tyx */
  x = y; ss_set_x(ss, x); ss_set_nz16(ss, x);
  ss_set_a(ss, a);
  S(0xB1FD, 1);                             /* C0B1FD rts */
  ss_rts(ss);
}

void set_entity_state(SnesState* ss) {
  set_entity_state_body(ss, ss_a(ss));
}

void anim_cb_reset_state(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  S(0xB1AE, 3);                             /* C0B1AE lda #$0000, falls through */
  a = 0x0000;
  ss_set_nz16(ss, a);
  set_entity_state_body(ss, a);
}

/* ---------------------------------------------------------------------------
 * entity_hit_react — $C0:B171
 *
 * The hit applied to one entity by anim_cb_hit_player / anim_cb_hit_enemies.
 * Entry: Y = the target's entity index, X = the attacker's (saved and restored).
 * States $10-$17 are the reaction states; the fourth hit in a row (counted in
 * entity_attack_timer) escalates from $10/$12 to $14/$16, and a target already
 * in $10 or $14 is left alone.
 * ------------------------------------------------------------------------- */
void entity_hit_react(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);

  S(0xB171, 1);                             /* C0B171 phx */
  ss_idle(ss);
  t_push16(ss, x);
  SI(0xB172);                               /* C0B172 tyx */
  x = y; ss_set_x(ss, x); ss_set_nz16(ss, x);
  S(0xB173, 3); t_index(ss);                /* C0B173 lda entity_state,X */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_state + x)));
  ss_set_nz16(ss, a);
  S(0xB176, 3); a = alu_and16(ss, a, 0xFFFC);  /* C0B176 and #$FFFC */
  S(0xB179, 3); alu_cmp16(ss, a, 0x0010);   /* C0B179 cmp #$0010 */
  S(0xB17C, 1); t_branch(ss, a == 0x0010);  /* C0B17C beq loc_C0B1AC */
  if(a != 0x0010) {
    S(0xB17E, 3); alu_cmp16(ss, a, 0x0014); /* C0B17E cmp #$0014 */
    S(0xB181, 1); t_branch(ss, a == 0x0014);  /* C0B181 beq loc_C0B1AC */
    if(a != 0x0014) {
      S(0xB183, 3); t_index(ss);            /* C0B183 inc entity_attack_timer,X */
      const uint32_t adr = ss_abs(ss, (uint16_t) (entity_attack_timer + x));
      uint16_t v = (uint16_t) (t_rmw_r16(ss, adr) + 1);
      ss_idle(ss);
      t_rmw_w16(ss, adr, v);
      ss_set_nz16(ss, v);
      S(0xB186, 3); t_index(ss);            /* C0B186 lda entity_attack_timer,X */
      a = t_read16(ss, adr);
      ss_set_nz16(ss, a);
      S(0xB189, 3); alu_cmp16(ss, a, 0x0004);  /* C0B189 cmp #$0004 */
      S(0xB18C, 1); t_branch(ss, a != 0x0004); /* C0B18C bne loc_C0B19E */
      if(a == 0x0004) {
        S(0xB18E, 3); t_index(ss);          /* C0B18E stz entity_attack_timer,X */
        t_write16(ss, adr, 0);
        S(0xB191, 3); a = 0x0014; ss_set_nz16(ss, a);  /* C0B191 lda #$0014 */
        S(0xB194, 3); t_index(ss);          /* C0B194 bit entity_flags,X */
        alu_bit16(ss, a, t_read16(ss, ss_abs(ss, (uint16_t) (entity_flags + x))));
        S(0xB197, 1); t_branch(ss, ss_v(ss));  /* C0B197 bvs loc_C0B1A9 */
        if(!ss_v(ss)) {
          S(0xB199, 3); a = 0x0016; ss_set_nz16(ss, a);  /* C0B199 lda #$0016 */
          S(0xB19C, 1); t_branch(ss, true); /* C0B19C bra loc_C0B1A9 */
        }
      } else {                              /* loc_C0B19E */
        S(0xB19E, 3); a = 0x0010; ss_set_nz16(ss, a);  /* C0B19E lda #$0010 */
        S(0xB1A1, 3); t_index(ss);          /* C0B1A1 bit entity_flags,X */
        alu_bit16(ss, a, t_read16(ss, ss_abs(ss, (uint16_t) (entity_flags + x))));
        S(0xB1A4, 1); t_branch(ss, ss_v(ss));  /* C0B1A4 bvs loc_C0B1A9 */
        if(!ss_v(ss)) {
          S(0xB1A6, 3); a = 0x0012; ss_set_nz16(ss, a);  /* C0B1A6 lda #$0012 */
        }
      }
      S(0xB1A9, 3);                         /* C0B1A9 jsr set_entity_state */
      if(t_call_sub(ss, pb, 0xB1B1)) return;
      a = ss_a(ss); x = ss_x(ss); y = ss_y(ss);
    }
  }
                                            /* loc_C0B1AC */
  S(0xB1AC, 1);                             /* C0B1AC plx */
  ss_idle(ss);
  ss_idle(ss);
  x = t_pull16(ss);
  ss_set_x(ss, x); ss_set_nz16(ss, x);
  ss_set_a(ss, a);
  S(0xB1AD, 1);                             /* C0B1AD rts */
  ss_rts(ss);
}

/* ---------------------------------------------------------------------------
 * entity_clear_anim_unused — $C0:B1FE
 *
 * Ten bytes past set_entity_state's rts, and no word anywhere in the ROM points
 * at it (docs/handler_tables.md section 2). It decodes cleanly at m0x0 and
 * clears three of this entity's columns, so it is converted as the disassembly
 * reads it; whether anything ever enters it is the gate's business.
 * ------------------------------------------------------------------------- */
void entity_clear_anim_unused(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);

  S(0xB1FE, 3); t_index(ss);                /* C0B1FE stz entity_substate,X */
  t_write16(ss, ss_abs(ss, (uint16_t) (entity_substate + x)), 0);
  S(0xB201, 3); t_index(ss);                /* C0B201 stz $0808,X */
  t_write16(ss, ss_abs(ss, (uint16_t) (entity_field_0808 + x)), 0);
  S(0xB204, 3); t_index(ss);                /* C0B204 stz $0A48,X */
  t_write16(ss, ss_abs(ss, (uint16_t) (entity_field_0A48 + x)), 0);
  S(0xB207, 1);                             /* C0B207 rts */
  ss_rts(ss);
}

static const RecompEntry kEntitiesAi[] = {
  { 0xc098da, "entity_update_tick", entity_update_tick },
  { 0xc099ef, "entity_ai_chase_player", entity_ai_chase_player },
  { 0xc09ab8, "entity_spawn_transform_a", entity_spawn_transform_a },
  { 0xc09af6, "entity_animate_only", entity_animate_only },
  { 0xc09b00, "entity_spawn_transform_b", entity_spawn_transform_b },
  { 0xc09b89, "entity_spawn_transform_c", entity_spawn_transform_c },
  { 0xc09d6a, "entity_init_from_table", entity_init_from_table },
  { 0xc0b171, "entity_hit_react", entity_hit_react },
  { 0xc0b1ae, "anim_cb_reset_state", anim_cb_reset_state },
  { 0xc0b1b1, "set_entity_state", set_entity_state },
  { 0xc0b1fe, "entity_clear_anim_unused", entity_clear_anim_unused },
};
RECOMP_REGISTER(kEntitiesAi)
