/* The animation player and its script callbacks, bank $C0.
 *
 * anim_update ($AEC8) walks the 8-byte frame records of the bank $C4 scripts
 * (docs/handler_tables.md section 2): each record is
 * { callback:u16, mode:u16, duration:u16, frame:u16 }, the cursor lives in
 * $0A08,X, the tick countdown in $0A28,X and the installed animation id in
 * $0A48,X. A record with mode 1 or 2 puts its callback address into ptr_04 and
 * calls anim_callback_dispatch, which is one `jmp ($0004)`. Duration $FFFE
 * loops the script, $FFFF switches to the animation id in the frame field, and
 * duration 0 in the first record selects the velocity-driven mode at $AF9B,
 * where the per-tick advance comes from entity_anim_rate instead of a counter.
 *
 * The callbacks are the twelve routines the 96 scripts point at: nine sound
 * triggers, two hit tests and the state reset in recomp/src/entities_ai.c.
 *
 * Both dispatches here, the `jmp ($0004)` and the `bra play_sound_effect`
 * tails of the sfx stubs, are transfers of control, not calls, so the bodies
 * model the instruction and then point the pc where the 65816 pointed it. The
 * hook installed on the destination fires next, which is how a converted
 * callback runs as C and an unconverted one runs on the reference CPU, with no
 * lookup table of its own.
 */
#include <stdint.h>
#include <stdbool.h>

#include "snes_state.h"
#include "dream_ram.h"
#include "dream_alu.h"
#include "dream_time.h"

/* ---- names dream_ram.h does not carry yet -------------------------------- */
#define entity_field_0A08    0x0A08   /* animation script cursor (frame * 8) */
#define entity_field_0A28    0x0A28   /* ticks left on the current frame */
#define entity_field_0A48    0x0A48   /* animation id currently installed */
#define anim_script_ptr      0x00A0   /* $A0/$A2: 24-bit pointer into bank $C4 */
#define anim_script_bank     0x00A2
#define anim_new_frame       0x0052   /* $52: "this tick entered a new frame";
                                       * dream_ram.h names the same word
                                       * depth_sort_key for the sort routine */
#define scratch_06           0x0006   /* $06: ptr_04's high word, scratch here */
#ifndef walk_cycle_parity
#define walk_cycle_parity    0x0072
#endif
#define entity_count         0x00A6   /* $A6: live entity count, slot * 2 units */

#define anim_script_table    0xC41858 /* data_C41858, long */

/* ---- 65816 shapes dream_time.h does not cover --------------------------- */
/* A 16-bit read-modify-write (dec abs,X): no interrupt latch inside the read,
 * and the write-back goes high byte first. */
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

/* `lda [$A0],Y`: the pointer word and its bank byte come out of the direct page
 * with no interrupt latch between them, then the data read takes one. */
static uint16_t t_read_ily(SnesState* ss, uint16_t dp, uint16_t off, uint16_t y) {
  uint8_t lo = ss_bus_r8(ss, (uint16_t) (dp + off));
  uint8_t hi = ss_bus_r8(ss, (uint16_t) (dp + off + 1));
  uint8_t bank = ss_bus_r8(ss, (uint16_t) (dp + off + 2));
  uint32_t ptr = ((uint32_t) bank << 16) | (uint16_t) (lo | (hi << 8));
  return t_read16(ss, (ptr + y) & 0xffffff);
}

/* The second half of `jsr abs`: the opcode and address bytes are the caller's
 * S(), so what is left is the internal cycle, the return-address push and the
 * transfer of control. The callee runs on the reference CPU, so whatever hook
 * it has still fires, and because the pushed frame is the routine's real return
 * address the callee can be left running at a frame boundary (true is returned
 * then, and the ROM finishes both). */
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
 * anim_update: $C0:AEC8
 *
 * Entry: X = entity index, entered with jsl (from the animation-rate tail at
 * $99D8 and from each spawn transform), exits with rtl. It re-enters itself
 * with a plain jmp at $AF97 and $B00E when a script hands over to another
 * animation id, which is the `goto` back to the top here.
 * ------------------------------------------------------------------------- */
void anim_update(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint16_t dp = ss_dp(ss);

restart:
  S(0xAEC8, 2);                             /* C0AEC8 stz $52 */
  t_write16(ss, dp + anim_new_frame, 0);
  S(0xAECA, 3); t_index(ss);                /* C0AECA lda entity_anim_id,X */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_anim_id + x)));
  ss_set_nz16(ss, a);
  S(0xAECD, 1); t_branch(ss, a == 0);       /* C0AECD beq loc_C0AF04 */
  if(a == 0) goto loc_C0AF04;
  S(0xAECF, 3); t_index(ss);                /* C0AECF cmp $0A48,X */
  { uint16_t v = t_read16(ss, ss_abs(ss, (uint16_t) (entity_field_0A48 + x)));
    alu_cmp16(ss, a, v); }
  S(0xAED2, 1); t_branch(ss, ss_z(ss));     /* C0AED2 beq loc_C0AF05 */
  if(ss_z(ss)) goto loc_C0AF05;

  S(0xAED4, 3); t_index(ss);                /* C0AED4 stz $0A08,X */
  t_write16(ss, ss_abs(ss, (uint16_t) (entity_field_0A08 + x)), 0);
  S(0xAED7, 3); t_index(ss);                /* C0AED7 sta $0A48,X */
  t_write16(ss, ss_abs(ss, (uint16_t) (entity_field_0A48 + x)), a);
  SI(0xAEDA); y = x; ss_set_y(ss, y); ss_set_nz16(ss, y);  /* C0AEDA txy */
  SI(0xAEDB); x = a; ss_set_x(ss, x); ss_set_nz16(ss, x);  /* C0AEDB tax */
  S(0xAEDC, 4);                             /* C0AEDC lda anim_script_table,X */
  a = t_read16(ss, (anim_script_table + x) & 0xffffff);
  ss_set_nz16(ss, a);
  SI(0xAEE0); x = y; ss_set_x(ss, x); ss_set_nz16(ss, x);  /* C0AEE0 tyx */
  S(0xAEE1, 2);                             /* C0AEE1 sta $A0 */
  t_write16(ss, dp + anim_script_ptr, a);
  S(0xAEE3, 3); a = 0x00C4; ss_set_nz16(ss, a);  /* C0AEE3 lda #$00C4 */
  S(0xAEE6, 2);                             /* C0AEE6 sta $A2 */
  t_write16(ss, dp + anim_script_bank, a);
  S(0xAEE8, 3); y = 0x0006;                 /* C0AEE8 ldy #$0006 */
  ss_set_y(ss, y); ss_set_nz16(ss, y);
  S(0xAEEB, 2);                             /* C0AEEB lda [$A0],Y */
  a = t_read_ily(ss, dp, anim_script_ptr, y);
  ss_set_nz16(ss, a);
  S(0xAEED, 1); t_branch(ss, a == 0);       /* C0AEED beq loc_C0AF01 */
  if(a == 0) goto loc_C0AF01;
  S(0xAEEF, 3); y = 0x0004;                 /* C0AEEF ldy #$0004 */
  ss_set_y(ss, y); ss_set_nz16(ss, y);
  S(0xAEF2, 2);                             /* C0AEF2 lda [$A0],Y */
  a = t_read_ily(ss, dp, anim_script_ptr, y);
  ss_set_nz16(ss, a);
  S(0xAEF4, 1); t_branch(ss, a != 0);       /* C0AEF4 bne loc_C0AEF9 */
  if(a == 0) {
    S(0xAEF6, 3);                           /* C0AEF6 jmp loc_C0AF9B */
    goto loc_C0AF9B;
  }
                                            /* loc_C0AEF9 */
  S(0xAEF9, 3); t_index(ss);                /* C0AEF9 sta $0A28,X */
  t_write16(ss, ss_abs(ss, (uint16_t) (entity_field_0A28 + x)), a);
  S(0xAEFC, 3); a = 0x0000; ss_set_nz16(ss, a);  /* C0AEFC lda #$0000 */
  S(0xAEFF, 1); t_branch(ss, true);         /* C0AEFF bra loc_C0AF30 */
  goto loc_C0AF30;

loc_C0AF01:
  S(0xAF01, 3); t_index(ss);                /* C0AF01 sta entity_frame_id,X */
  t_write16(ss, ss_abs(ss, (uint16_t) (entity_frame_id + x)), a);
loc_C0AF04:
  ss_set_a(ss, a);
  S(0xAF04, 1);                             /* C0AF04 rtl */
  ss_rtl(ss);
  return;

loc_C0AF05:
  SI(0xAF05); y = x; ss_set_y(ss, y); ss_set_nz16(ss, y);  /* C0AF05 txy */
  SI(0xAF06); x = a; ss_set_x(ss, x); ss_set_nz16(ss, x);  /* C0AF06 tax */
  S(0xAF07, 4);                             /* C0AF07 lda anim_script_table,X */
  a = t_read16(ss, (anim_script_table + x) & 0xffffff);
  ss_set_nz16(ss, a);
  SI(0xAF0B); x = y; ss_set_x(ss, x); ss_set_nz16(ss, x);  /* C0AF0B tyx */
  S(0xAF0C, 2);                             /* C0AF0C sta $A0 */
  t_write16(ss, dp + anim_script_ptr, a);
  S(0xAF0E, 3); a = 0x00C4; ss_set_nz16(ss, a);  /* C0AF0E lda #$00C4 */
  S(0xAF11, 2);                             /* C0AF11 sta $A2 */
  t_write16(ss, dp + anim_script_bank, a);
  S(0xAF13, 3); y = 0x0006;                 /* C0AF13 ldy #$0006 */
  ss_set_y(ss, y); ss_set_nz16(ss, y);
  S(0xAF16, 2);                             /* C0AF16 lda [$A0],Y */
  a = t_read_ily(ss, dp, anim_script_ptr, y);
  ss_set_nz16(ss, a);
  S(0xAF18, 1); t_branch(ss, a == 0);       /* C0AF18 beq loc_C0AF01 */
  if(a == 0) goto loc_C0AF01;
  S(0xAF1A, 3); y = 0x0004;                 /* C0AF1A ldy #$0004 */
  ss_set_y(ss, y); ss_set_nz16(ss, y);
  S(0xAF1D, 2);                             /* C0AF1D lda [$A0],Y */
  a = t_read_ily(ss, dp, anim_script_ptr, y);
  ss_set_nz16(ss, a);
  S(0xAF1F, 1); t_branch(ss, a != 0);       /* C0AF1F bne loc_C0AF24 */
  if(a == 0) {
    S(0xAF21, 3);                           /* C0AF21 jmp loc_C0AF9B */
    goto loc_C0AF9B;
  }
                                            /* loc_C0AF24 */
  S(0xAF24, 3); t_index(ss);                /* C0AF24 dec $0A28,X */
  { const uint32_t adr = ss_abs(ss, (uint16_t) (entity_field_0A28 + x));
    uint16_t v = (uint16_t) (t_rmw_r16(ss, adr) - 1);
    ss_idle(ss);
    t_rmw_w16(ss, adr, v);
    ss_set_nz16(ss, v); }
  S(0xAF27, 1); t_branch(ss, !ss_n(ss));    /* C0AF27 bpl loc_C0AF44 */
  if(!ss_n(ss)) goto loc_C0AF44;
  S(0xAF29, 3); t_index(ss);                /* C0AF29 lda $0A08,X */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_field_0A08 + x)));
  ss_set_nz16(ss, a);
  SI(0xAF2C); ss_set_c(ss, false);          /* C0AF2C clc */
  S(0xAF2D, 3); a = alu_adc16(ss, a, 0x0008);  /* C0AF2D adc #$0008 */

loc_C0AF30:
  S(0xAF30, 3); t_index(ss);                /* C0AF30 sta $0A08,X */
  t_write16(ss, ss_abs(ss, (uint16_t) (entity_field_0A08 + x)), a);
  S(0xAF33, 3); t_index(ss);                /* C0AF33 lda $0A08,X */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_field_0A08 + x)));
  ss_set_nz16(ss, a);
  S(0xAF36, 3); a = alu_ora16(ss, a, 0x0004);  /* C0AF36 ora #$0004 */
  SI(0xAF39); y = a; ss_set_y(ss, y); ss_set_nz16(ss, y);  /* C0AF39 tay */
  S(0xAF3A, 2);                             /* C0AF3A lda [$A0],Y */
  a = t_read_ily(ss, dp, anim_script_ptr, y);
  ss_set_nz16(ss, a);
  S(0xAF3C, 3); t_index(ss);                /* C0AF3C sta $0A28,X */
  t_write16(ss, ss_abs(ss, (uint16_t) (entity_field_0A28 + x)), a);
  S(0xAF3F, 3); a = 0x0001; ss_set_nz16(ss, a);  /* C0AF3F lda #$0001 */
  S(0xAF42, 2);                             /* C0AF42 sta $52 */
  t_write16(ss, dp + anim_new_frame, a);

loc_C0AF44:
  S(0xAF44, 3); t_index(ss);                /* C0AF44 lda $0A08,X */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_field_0A08 + x)));
  ss_set_nz16(ss, a);
  S(0xAF47, 3); a = alu_and16(ss, a, 0x0FF8);  /* C0AF47 and #$0FF8 */
  S(0xAF4A, 3); a = alu_ora16(ss, a, 0x0004);  /* C0AF4A ora #$0004 */
  SI(0xAF4D); y = a; ss_set_y(ss, y); ss_set_nz16(ss, y);  /* C0AF4D tay */
  S(0xAF4E, 2);                             /* C0AF4E lda [$A0],Y */
  a = t_read_ily(ss, dp, anim_script_ptr, y);
  ss_set_nz16(ss, a);
  SI(0xAF50); y = (uint16_t) (y + 1); ss_set_y(ss, y); ss_set_nz16(ss, y);  /* iny */
  SI(0xAF51); y = (uint16_t) (y + 1); ss_set_y(ss, y); ss_set_nz16(ss, y);  /* iny */
  SI(0xAF52); ss_set_c(ss, true);           /* C0AF52 sec */
  S(0xAF53, 3); a = alu_sbc16(ss, a, 0xFFFE);  /* C0AF53 sbc #$FFFE */
  S(0xAF56, 1); t_branch(ss, ss_c(ss));     /* C0AF56 bcs loc_C0AF5D */
  if(!ss_c(ss)) {
    S(0xAF58, 2);                           /* C0AF58 lda [$A0],Y */
    a = t_read_ily(ss, dp, anim_script_ptr, y);
    ss_set_nz16(ss, a);
    S(0xAF5A, 3); t_index(ss);              /* C0AF5A sta entity_frame_id,X */
    t_write16(ss, ss_abs(ss, (uint16_t) (entity_frame_id + x)), a);
  }
                                            /* loc_C0AF5D */
  SI(0xAF5D); y = (uint16_t) (y - 1); ss_set_y(ss, y); ss_set_nz16(ss, y);  /* dey */
  SI(0xAF5E); y = (uint16_t) (y - 1); ss_set_y(ss, y); ss_set_nz16(ss, y);  /* dey */
  SI(0xAF5F); y = (uint16_t) (y - 1); ss_set_y(ss, y); ss_set_nz16(ss, y);  /* dey */
  SI(0xAF60); y = (uint16_t) (y - 1); ss_set_y(ss, y); ss_set_nz16(ss, y);  /* dey */
  S(0xAF61, 2);                             /* C0AF61 lda [$A0],Y */
  a = t_read_ily(ss, dp, anim_script_ptr, y);
  ss_set_nz16(ss, a);
  S(0xAF63, 1); t_branch(ss, a == 0);       /* C0AF63 beq loc_C0AF7C */
  if(a == 0) goto loc_C0AF7C;
  S(0xAF65, 3); alu_cmp16(ss, a, 0x0002);   /* C0AF65 cmp #$0002 */
  S(0xAF68, 1); t_branch(ss, a == 0x0002);  /* C0AF68 beq loc_C0AF73 */
  if(a != 0x0002) {
    S(0xAF6A, 3); alu_cmp16(ss, a, 0x0001); /* C0AF6A cmp #$0001 */
    S(0xAF6D, 1); t_branch(ss, a != 0x0001);  /* C0AF6D bne loc_C0AF7C */
    if(a != 0x0001) goto loc_C0AF7C;
    S(0xAF6F, 2);                           /* C0AF6F lda $52 */
    a = t_read16(ss, dp + anim_new_frame);
    ss_set_nz16(ss, a);
    S(0xAF71, 1); t_branch(ss, a == 0);     /* C0AF71 beq loc_C0AF7C */
    if(a == 0) goto loc_C0AF7C;
  }
                                            /* loc_C0AF73 */
  SI(0xAF73); y = (uint16_t) (y - 1); ss_set_y(ss, y); ss_set_nz16(ss, y);  /* dey */
  SI(0xAF74); y = (uint16_t) (y - 1); ss_set_y(ss, y); ss_set_nz16(ss, y);  /* dey */
  S(0xAF75, 2);                             /* C0AF75 lda [$A0],Y */
  a = t_read_ily(ss, dp, anim_script_ptr, y);
  ss_set_nz16(ss, a);
  S(0xAF77, 2);                             /* C0AF77 sta ptr_04 */
  t_write16(ss, dp + ptr_04, a);
  S(0xAF79, 3);                             /* C0AF79 jsr anim_callback_dispatch */
  if(t_call_sub(ss, pb, 0xB022)) return;
  a = ss_a(ss); x = ss_x(ss); y = ss_y(ss);

loc_C0AF7C:
  S(0xAF7C, 3); t_index(ss);                /* C0AF7C lda $0A08,X */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_field_0A08 + x)));
  ss_set_nz16(ss, a);
  S(0xAF7F, 3); a = alu_and16(ss, a, 0x0FF8);  /* C0AF7F and #$0FF8 */
  S(0xAF82, 3); a = alu_ora16(ss, a, 0x0004);  /* C0AF82 ora #$0004 */
  SI(0xAF85); y = a; ss_set_y(ss, y); ss_set_nz16(ss, y);  /* C0AF85 tay */
  S(0xAF86, 2);                             /* C0AF86 lda [$A0],Y */
  a = t_read_ily(ss, dp, anim_script_ptr, y);
  ss_set_nz16(ss, a);
  SI(0xAF88); ss_set_c(ss, true);           /* C0AF88 sec */
  S(0xAF89, 3); a = alu_sbc16(ss, a, 0xFFFE);  /* C0AF89 sbc #$FFFE */
  S(0xAF8C, 1); t_branch(ss, a == 0);       /* C0AF8C beq loc_C0AF30 */
  if(a == 0) goto loc_C0AF30;
  S(0xAF8E, 1); t_branch(ss, !ss_c(ss));    /* C0AF8E bcc loc_C0AF9A */
  if(!ss_c(ss)) {
    ss_set_a(ss, a);
    S(0xAF9A, 1);                           /* C0AF9A rtl */
    ss_rtl(ss);
    return;
  }
  SI(0xAF90); y = (uint16_t) (y + 1); ss_set_y(ss, y); ss_set_nz16(ss, y);  /* iny */
  SI(0xAF91); y = (uint16_t) (y + 1); ss_set_y(ss, y); ss_set_nz16(ss, y);  /* iny */
  S(0xAF92, 2);                             /* C0AF92 lda [$A0],Y */
  a = t_read_ily(ss, dp, anim_script_ptr, y);
  ss_set_nz16(ss, a);
  S(0xAF94, 3); t_index(ss);                /* C0AF94 sta entity_anim_id,X */
  t_write16(ss, ss_abs(ss, (uint16_t) (entity_anim_id + x)), a);
  S(0xAF97, 3);                             /* C0AF97 jmp anim_update */
  goto restart;

loc_C0AF9B:
  S(0xAF9B, 3); t_index(ss);                /* C0AF9B lda entity_anim_rate,X */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_anim_rate + x)));
  ss_set_nz16(ss, a);
  S(0xAF9E, 3); alu_cmp16(ss, a, 0x0100);   /* C0AF9E cmp #$0100 */
  S(0xAFA1, 1); t_branch(ss, !ss_c(ss));    /* C0AFA1 bcc loc_C0AFA6 */
  if(ss_c(ss)) {
    S(0xAFA3, 3); a = 0x0100; ss_set_nz16(ss, a);  /* C0AFA3 lda #$0100 */
  }
                                            /* loc_C0AFA6 */
  SI(0xAFA6); ss_set_c(ss, false);          /* C0AFA6 clc */
  S(0xAFA7, 3); t_index(ss);                /* C0AFA7 adc $0A28,X */
  a = alu_adc16(ss, a, t_read16(ss, ss_abs(ss, (uint16_t) (entity_field_0A28 + x))));
  S(0xAFAA, 2);                             /* C0AFAA sta $18 */
  t_write16(ss, dp + scratch_18, a);
  S(0xAFAC, 3); a = alu_and16(ss, a, 0x00FF);  /* C0AFAC and #$00FF */
  S(0xAFAF, 3); t_index(ss);                /* C0AFAF sta $0A28,X */
  t_write16(ss, ss_abs(ss, (uint16_t) (entity_field_0A28 + x)), a);
  S(0xAFB2, 3); t_index(ss);                /* C0AFB2 lda $0A08,X */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_field_0A08 + x)));
  ss_set_nz16(ss, a);
  S(0xAFB5, 2);                             /* C0AFB5 sta $52 */
  t_write16(ss, dp + anim_new_frame, a);
  S(0xAFB7, 2);                             /* C0AFB7 lda $19 (the high byte of $18) */
  a = t_read16(ss, dp + scratch_18 + 1);
  ss_set_nz16(ss, a);
  S(0xAFB9, 3); a = alu_and16(ss, a, 0x00FF);  /* C0AFB9 and #$00FF */
  SI(0xAFBC); ss_set_c(ss, false);          /* C0AFBC clc */
  S(0xAFBD, 3); t_index(ss);                /* C0AFBD adc $0A08,X */
  a = alu_adc16(ss, a, t_read16(ss, ss_abs(ss, (uint16_t) (entity_field_0A08 + x))));
  S(0xAFC0, 3); t_index(ss);                /* C0AFC0 sta $0A08,X */
  t_write16(ss, ss_abs(ss, (uint16_t) (entity_field_0A08 + x)), a);
  SI(0xAFC3); ss_set_c(ss, true);           /* C0AFC3 sec */
  S(0xAFC4, 2);                             /* C0AFC4 sbc $52 */
  a = alu_sbc16(ss, a, t_read16(ss, dp + anim_new_frame));
  S(0xAFC6, 2);                             /* C0AFC6 sta $52 */
  t_write16(ss, dp + anim_new_frame, a);
  S(0xAFC8, 3); t_index(ss);                /* C0AFC8 lda $0A08,X */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_field_0A08 + x)));
  ss_set_nz16(ss, a);
  SI(0xAFCB); a = alu_asl16(ss, a);         /* C0AFCB asl A */
  SI(0xAFCC); a = alu_asl16(ss, a);         /* C0AFCC asl A */
  SI(0xAFCD); a = alu_asl16(ss, a);         /* C0AFCD asl A */
  S(0xAFCE, 3); a = alu_and16(ss, a, 0x0FF8);  /* C0AFCE and #$0FF8 */
  S(0xAFD1, 3); a = alu_ora16(ss, a, 0x0002);  /* C0AFD1 ora #$0002 */
  SI(0xAFD4); y = a; ss_set_y(ss, y); ss_set_nz16(ss, y);  /* C0AFD4 tay */
  S(0xAFD5, 2);                             /* C0AFD5 lda [$A0],Y */
  a = t_read_ily(ss, dp, anim_script_ptr, y);
  ss_set_nz16(ss, a);
  S(0xAFD7, 1); t_branch(ss, a == 0);       /* C0AFD7 beq loc_C0AFF0 */
  if(a == 0) goto loc_C0AFF0;
  S(0xAFD9, 3); alu_cmp16(ss, a, 0x0002);   /* C0AFD9 cmp #$0002 */
  S(0xAFDC, 1); t_branch(ss, a == 0x0002);  /* C0AFDC beq loc_C0AFE7 */
  if(a != 0x0002) {
    S(0xAFDE, 3); alu_cmp16(ss, a, 0x0001); /* C0AFDE cmp #$0001 */
    S(0xAFE1, 1); t_branch(ss, a != 0x0001);  /* C0AFE1 bne loc_C0AFF0 */
    if(a != 0x0001) goto loc_C0AFF0;
    S(0xAFE3, 2);                           /* C0AFE3 lda $52 */
    a = t_read16(ss, dp + anim_new_frame);
    ss_set_nz16(ss, a);
    S(0xAFE5, 1); t_branch(ss, a == 0);     /* C0AFE5 beq loc_C0AFF0 */
    if(a == 0) goto loc_C0AFF0;
  }
                                            /* loc_C0AFE7 */
  SI(0xAFE7); y = (uint16_t) (y - 1); ss_set_y(ss, y); ss_set_nz16(ss, y);  /* dey */
  SI(0xAFE8); y = (uint16_t) (y - 1); ss_set_y(ss, y); ss_set_nz16(ss, y);  /* dey */
  S(0xAFE9, 2);                             /* C0AFE9 lda [$A0],Y */
  a = t_read_ily(ss, dp, anim_script_ptr, y);
  ss_set_nz16(ss, a);
  S(0xAFEB, 2);                             /* C0AFEB sta ptr_04 */
  t_write16(ss, dp + ptr_04, a);
  S(0xAFED, 3);                             /* C0AFED jsr anim_callback_dispatch */
  if(t_call_sub(ss, pb, 0xB022)) return;
  a = ss_a(ss); x = ss_x(ss); y = ss_y(ss);

loc_C0AFF0:
  S(0xAFF0, 3); t_index(ss);                /* C0AFF0 lda $0A08,X */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_field_0A08 + x)));
  ss_set_nz16(ss, a);
  SI(0xAFF3); a = alu_asl16(ss, a);         /* C0AFF3 asl A */
  SI(0xAFF4); a = alu_asl16(ss, a);         /* C0AFF4 asl A */
  SI(0xAFF5); a = alu_asl16(ss, a);         /* C0AFF5 asl A */
  S(0xAFF6, 3); a = alu_and16(ss, a, 0x0FF8);  /* C0AFF6 and #$0FF8 */
  S(0xAFF9, 3); a = alu_ora16(ss, a, 0x0004);  /* C0AFF9 ora #$0004 */
  SI(0xAFFC); y = a; ss_set_y(ss, y); ss_set_nz16(ss, y);  /* C0AFFC tay */
  S(0xAFFD, 2);                             /* C0AFFD lda [$A0],Y */
  a = t_read_ily(ss, dp, anim_script_ptr, y);
  ss_set_nz16(ss, a);
  SI(0xAFFF); ss_set_c(ss, true);           /* C0AFFF sec */
  S(0xB000, 3); a = alu_sbc16(ss, a, 0xFFFE);  /* C0B000 sbc #$FFFE */
  S(0xB003, 1); t_branch(ss, a == 0);       /* C0B003 beq loc_C0B011 */
  if(a != 0) {
    S(0xB005, 1); t_branch(ss, !ss_c(ss));  /* C0B005 bcc loc_C0B01A */
    if(!ss_c(ss)) goto loc_C0B01A;
    SI(0xB007); y = (uint16_t) (y + 1); ss_set_y(ss, y); ss_set_nz16(ss, y);  /* iny */
    SI(0xB008); y = (uint16_t) (y + 1); ss_set_y(ss, y); ss_set_nz16(ss, y);  /* iny */
    S(0xB009, 2);                           /* C0B009 lda [$A0],Y */
    a = t_read_ily(ss, dp, anim_script_ptr, y);
    ss_set_nz16(ss, a);
    S(0xB00B, 3); t_index(ss);              /* C0B00B sta entity_anim_id,X */
    t_write16(ss, ss_abs(ss, (uint16_t) (entity_anim_id + x)), a);
    S(0xB00E, 3);                           /* C0B00E jmp anim_update */
    goto restart;
  }
                                            /* loc_C0B011 */
  S(0xB011, 3); y = 0x0004;                 /* C0B011 ldy #$0004 */
  ss_set_y(ss, y); ss_set_nz16(ss, y);
  S(0xB014, 3); t_index(ss);                /* C0B014 stz $0A28,X */
  t_write16(ss, ss_abs(ss, (uint16_t) (entity_field_0A28 + x)), 0);
  S(0xB017, 3); t_index(ss);                /* C0B017 stz $0A08,X */
  t_write16(ss, ss_abs(ss, (uint16_t) (entity_field_0A08 + x)), 0);

loc_C0B01A:
  SI(0xB01A); y = (uint16_t) (y + 1); ss_set_y(ss, y); ss_set_nz16(ss, y);  /* iny */
  SI(0xB01B); y = (uint16_t) (y + 1); ss_set_y(ss, y); ss_set_nz16(ss, y);  /* iny */
  S(0xB01C, 2);                             /* C0B01C lda [$A0],Y */
  a = t_read_ily(ss, dp, anim_script_ptr, y);
  ss_set_nz16(ss, a);
  S(0xB01E, 3); t_index(ss);                /* C0B01E sta entity_frame_id,X */
  t_write16(ss, ss_abs(ss, (uint16_t) (entity_frame_id + x)), a);
  ss_set_a(ss, a);
  S(0xB021, 1);                             /* C0B021 rtl */
  ss_rtl(ss);
}

/* ---------------------------------------------------------------------------
 * anim_callback_dispatch: $C0:B022
 *
 * One instruction: `jmp ($0004)`, the pointer being the callback address the
 * animation player read out of the script record. It is a transfer of control,
 * not a call (the callback's own rts returns to anim_update), so the body
 * models the jmp and leaves the pc on the callback. Whichever of the twelve
 * callbacks it is, the registry decides what runs next: a converted one enters
 * its hook (and is counted as entered by the gate), an unconverted one runs on
 * the reference CPU. Neither needs a table here.
 * ------------------------------------------------------------------------- */
void anim_callback_dispatch(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);

  S(0xB022, 3);                             /* C0B022 jmp ($0004) */
  { const uint16_t target = t_read16(ss, ptr_04);  /* the vector lives in bank 0 */
    ss_set_pc(ss, pb, target); }
}

/* ---------------------------------------------------------------------------
 * play_sound_effect: $C0:B0C5
 *
 * `phx ; phy ; jsl sfx_command_dispatch ; ply ; plx ; rts`: a sound command
 * with X and Y preserved, because every caller is an animation callback that
 * still needs the entity index. A = the packed channel:effect word.
 * sfx_command_dispatch is in bank $C1 and is another agent's file, so it runs
 * on the reference CPU here; the frame pushed is the real one, so it can be
 * left running when the machine moves on.
 * ------------------------------------------------------------------------- */
static void play_sound_effect_body(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);

  S(0xB0C5, 1);                             /* C0B0C5 phx */
  ss_idle(ss);
  t_push16(ss, x);
  S(0xB0C6, 1);                             /* C0B0C6 phy */
  ss_idle(ss);
  t_push16(ss, y);
  S(0xB0C7, 3);                             /* C0B0C7 jsl sfx_command_dispatch */
  if(t_call_long(ss, 0x81, 0x8415)) return;
  a = ss_a(ss); x = ss_x(ss); y = ss_y(ss);
  S(0xB0CB, 1);                             /* C0B0CB ply */
  ss_idle(ss);
  ss_idle(ss);
  y = t_pull16(ss);
  ss_set_y(ss, y); ss_set_nz16(ss, y);
  S(0xB0CC, 1);                             /* C0B0CC plx */
  ss_idle(ss);
  ss_idle(ss);
  x = t_pull16(ss);
  ss_set_x(ss, x); ss_set_nz16(ss, x);
  ss_set_a(ss, a);
  S(0xB0CD, 1);                             /* C0B0CD rts */
  ss_rts(ss);
}

void play_sound_effect(SnesState* ss) {
  play_sound_effect_body(ss);
}

/* ---------------------------------------------------------------------------
 * The seven 5-byte sfx stubs: $C0:B052..$B074
 *
 * `lda #<effect> ; bra play_sound_effect`. The bra is the routine's tail, so
 * the body models it and hands the pc over.
 * ------------------------------------------------------------------------- */
static void anim_cb_sfx_stub(SnesState* ss, uint16_t at, uint16_t effect) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);

  S(at, 3);                                 /* C0B052 lda #$0506 ... */
  a = effect;
  ss_set_nz16(ss, a);
  S(at + 3, 1); t_branch(ss, true);         /* C0B055 bra play_sound_effect */
  ss_set_a(ss, a);
  ss_set_pc(ss, pb, 0xB0C5);
}

void anim_cb_sfx_0506(SnesState* ss) { anim_cb_sfx_stub(ss, 0xB052, 0x0506); }
void anim_cb_sfx_0606(SnesState* ss) { anim_cb_sfx_stub(ss, 0xB057, 0x0606); }
void anim_cb_sfx_0602(SnesState* ss) { anim_cb_sfx_stub(ss, 0xB05C, 0x0602); }
void anim_cb_sfx_0507(SnesState* ss) { anim_cb_sfx_stub(ss, 0xB061, 0x0507); }
void anim_cb_sfx_050C(SnesState* ss) { anim_cb_sfx_stub(ss, 0xB066, 0x050C); }
void anim_cb_sfx_0508(SnesState* ss) { anim_cb_sfx_stub(ss, 0xB06B, 0x0508); }
void anim_cb_sfx_0709(SnesState* ss) { anim_cb_sfx_stub(ss, 0xB070, 0x0709); }

/* ---------------------------------------------------------------------------
 * anim_cb_sfx_0704: $C0:B025, anim_cb_sfx_060E: $C0:B08F
 *
 * The two callbacks that pick between a near and a far variant of the same
 * effect: in game mode 2 by whether the entity's X is inside the $0748..$07B0
 * band, otherwise (mode 0, where A is still the mode and the `and #$FFFF` only
 * tests it) by whether it is past $0090.
 * ------------------------------------------------------------------------- */
void anim_cb_sfx_0704(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint16_t dp = ss_dp(ss);
  bool far_variant;

  S(0xB025, 2);                             /* C0B025 lda game_mode */
  a = t_read16(ss, dp + game_mode);
  ss_set_nz16(ss, a);
  S(0xB027, 3); alu_cmp16(ss, a, 0x0002);   /* C0B027 cmp #$0002 */
  S(0xB02A, 1); t_branch(ss, a != 0x0002);  /* C0B02A bne loc_C0B039 */
  if(a == 0x0002) {
    S(0xB02C, 3); t_index(ss);              /* C0B02C lda entity_x,X */
    a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_x + x)));
    ss_set_nz16(ss, a);
    S(0xB02F, 3); alu_cmp16(ss, a, 0x0748); /* C0B02F cmp #$0748 */
    S(0xB032, 1); t_branch(ss, !ss_c(ss));  /* C0B032 bcc loc_C0B048 */
    if(!ss_c(ss)) { far_variant = true; goto emit; }
    S(0xB034, 3); alu_cmp16(ss, a, 0x07B0); /* C0B034 cmp #$07B0 */
    S(0xB037, 1); t_branch(ss, !ss_c(ss));  /* C0B037 bcc loc_C0B04D */
    if(!ss_c(ss)) { far_variant = false; goto emit; }
  }
                                            /* loc_C0B039 */
  S(0xB039, 3); a = alu_and16(ss, a, 0xFFFF);  /* C0B039 and #$FFFF */
  S(0xB03C, 1); t_branch(ss, a != 0);       /* C0B03C bne loc_C0B048 */
  if(a != 0) { far_variant = true; goto emit; }
  S(0xB03E, 3); t_index(ss);                /* C0B03E lda entity_x,X */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_x + x)));
  ss_set_nz16(ss, a);
  S(0xB041, 1); t_branch(ss, (a & 0x8000) != 0);  /* C0B041 bmi loc_C0B04D */
  if((a & 0x8000) != 0) { far_variant = false; goto emit; }
  S(0xB043, 3); alu_cmp16(ss, a, 0x0090);   /* C0B043 cmp #$0090 */
  S(0xB046, 1); t_branch(ss, !ss_c(ss));    /* C0B046 bcc loc_C0B04D */
  far_variant = ss_c(ss);

emit:
  if(far_variant) {
    S(0xB048, 3); a = 0x0704; ss_set_nz16(ss, a);  /* C0B048 lda #$0704 */
    S(0xB04B, 1); t_branch(ss, true);       /* C0B04B bra play_sound_effect */
  } else {
    S(0xB04D, 3); a = 0x0705; ss_set_nz16(ss, a);  /* C0B04D lda #$0705 */
    S(0xB050, 1); t_branch(ss, true);       /* C0B050 bra play_sound_effect */
  }
  ss_set_a(ss, a);
  ss_set_pc(ss, pb, 0xB0C5);
}

void anim_cb_sfx_060E(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint16_t dp = ss_dp(ss);
  bool far_variant;

  S(0xB08F, 2);                             /* C0B08F lda game_mode */
  a = t_read16(ss, dp + game_mode);
  ss_set_nz16(ss, a);
  S(0xB091, 3); alu_cmp16(ss, a, 0x0002);   /* C0B091 cmp #$0002 */
  S(0xB094, 1); t_branch(ss, a != 0x0002);  /* C0B094 bne loc_C0B0A5 */
  if(a == 0x0002) {
    S(0xB096, 3); t_index(ss);              /* C0B096 lda entity_x,X */
    a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_x + x)));
    ss_set_nz16(ss, a);
    S(0xB099, 3); alu_cmp16(ss, a, 0x0748); /* C0B099 cmp #$0748 */
    S(0xB09C, 1); t_branch(ss, !ss_c(ss));  /* C0B09C bcc loc_C0B0B2 */
    if(!ss_c(ss)) { far_variant = true; goto emit; }
    S(0xB09E, 3); alu_cmp16(ss, a, 0x07B0); /* C0B09E cmp #$07B0 */
    S(0xB0A1, 1); t_branch(ss, !ss_c(ss));  /* C0B0A1 bcc loc_C0B0B7 */
    if(!ss_c(ss)) { far_variant = false; goto emit; }
    S(0xB0A3, 1); t_branch(ss, true);       /* C0B0A3 bra loc_C0B0B2 */
    far_variant = true;
    goto emit;
  }
                                            /* loc_C0B0A5 */
  S(0xB0A5, 3); a = alu_and16(ss, a, 0xFFFF);  /* C0B0A5 and #$FFFF */
  S(0xB0A8, 1); t_branch(ss, a != 0);       /* C0B0A8 bne loc_C0B0B2 */
  if(a != 0) { far_variant = true; goto emit; }
  S(0xB0AA, 3); t_index(ss);                /* C0B0AA lda entity_x,X */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_x + x)));
  ss_set_nz16(ss, a);
  S(0xB0AD, 3); alu_cmp16(ss, a, 0x0090);   /* C0B0AD cmp #$0090 */
  S(0xB0B0, 1); t_branch(ss, !ss_c(ss));    /* C0B0B0 bcc loc_C0B0B7 */
  far_variant = ss_c(ss);

emit:
  if(far_variant) {
    S(0xB0B2, 3); a = 0x060E; ss_set_nz16(ss, a);  /* C0B0B2 lda #$060E */
    S(0xB0B5, 1); t_branch(ss, true);       /* C0B0B5 bra play_sound_effect */
  } else {
    S(0xB0B7, 3); a = 0x060D; ss_set_nz16(ss, a);  /* C0B0B7 lda #$060D */
    S(0xB0BA, 1); t_branch(ss, true);       /* C0B0BA bra play_sound_effect */
  }
  ss_set_a(ss, a);
  ss_set_pc(ss, pb, 0xB0C5);
}

/* ---------------------------------------------------------------------------
 * play_footstep_sound: $C0:B075, play_zone_transition_sound: $C0:B0BC
 *
 * Two-effect pairs. play_footstep_sound picks its pair from the walk-cycle
 * parity flag and is called from the main loop's 60-frame counter, not from a
 * script; play_zone_transition_sound plays one effect and then falls through
 * into play_sound_effect with the second, which is why its second call is not
 * a jsr.
 * ------------------------------------------------------------------------- */
void play_footstep_sound(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint16_t dp = ss_dp(ss);

  S(0xB075, 2);                             /* C0B075 lda walk_cycle_parity */
  a = t_read16(ss, dp + walk_cycle_parity);
  ss_set_nz16(ss, a);
  S(0xB077, 1); t_branch(ss, a == 0);       /* C0B077 beq loc_C0B084 */
  if(a != 0) {
    S(0xB079, 3); a = 0x050A; ss_set_nz16(ss, a);  /* C0B079 lda #$050A */
    S(0xB07C, 3);                           /* C0B07C jsr play_sound_effect */
    ss_set_a(ss, a);
    if(t_call_sub(ss, pb, 0xB0C5)) return;
    a = ss_a(ss); x = ss_x(ss); y = ss_y(ss);
    S(0xB07F, 3); a = 0x0710; ss_set_nz16(ss, a);  /* C0B07F lda #$0710 */
    S(0xB082, 1); t_branch(ss, true);       /* C0B082 bra play_sound_effect */
  } else {                                  /* loc_C0B084 */
    S(0xB084, 3); a = 0x0712; ss_set_nz16(ss, a);  /* C0B084 lda #$0712 */
    S(0xB087, 3);                           /* C0B087 jsr play_sound_effect */
    ss_set_a(ss, a);
    if(t_call_sub(ss, pb, 0xB0C5)) return;
    a = ss_a(ss); x = ss_x(ss); y = ss_y(ss);
    S(0xB08A, 3); a = 0x050B; ss_set_nz16(ss, a);  /* C0B08A lda #$050B */
    S(0xB08D, 1); t_branch(ss, true);       /* C0B08D bra play_sound_effect */
  }
  ss_set_a(ss, a);
  ss_set_pc(ss, pb, 0xB0C5);
}

void play_zone_transition_sound(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);

  S(0xB0BC, 3); a = 0x050F; ss_set_nz16(ss, a);  /* C0B0BC lda #$050F */
  S(0xB0BF, 3);                             /* C0B0BF jsr play_sound_effect */
  ss_set_a(ss, a);
  if(t_call_sub(ss, pb, 0xB0C5)) return;
  a = ss_a(ss); x = ss_x(ss); y = ss_y(ss);
  S(0xB0C2, 3); a = 0x0611; ss_set_nz16(ss, a);  /* C0B0C2 lda #$0611 */
  ss_set_a(ss, a);
  play_sound_effect_body(ss);               /* falls through into $B0C5 */
}

/* ---------------------------------------------------------------------------
 * anim_cb_hit_player: $C0:B0CE
 *
 * The attack frames of the type-$0C enemy: if the player (entity 0) is not
 * already in hitstun and is within $40 pixels on the side this entity faces
 * (bit 14 of entity_flags, tested with bit/V), hand entity 0 to
 * entity_hit_react with Y = 0. Both hand-overs are tail jmps.
 * ------------------------------------------------------------------------- */
void anim_cb_hit_player(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);

  S(0xB0CE, 3);                             /* C0B0CE lda entity_hitstun_timer */
  a = t_read16(ss, ss_abs(ss, entity_hitstun_timer));
  ss_set_nz16(ss, a);
  S(0xB0D1, 1); t_branch(ss, a != 0);       /* C0B0D1 bne loc_C0B0EC */
  if(a != 0) goto loc_C0B0EC;
  S(0xB0D3, 3); y = 0x0000;                 /* C0B0D3 ldy #$0000 */
  ss_set_y(ss, y); ss_set_nz16(ss, y);
  S(0xB0D6, 3); t_index(ss);                /* C0B0D6 bit entity_flags,X */
  alu_bit16(ss, a, t_read16(ss, ss_abs(ss, (uint16_t) (entity_flags + x))));
  S(0xB0D9, 1); t_branch(ss, !ss_v(ss));    /* C0B0D9 bvc loc_C0B0ED */
  if(ss_v(ss)) {
    S(0xB0DB, 3);                           /* C0B0DB lda entity_x (player) */
    a = t_read16(ss, ss_abs(ss, entity_x));
    ss_set_nz16(ss, a);
    SI(0xB0DE); ss_set_c(ss, true);         /* C0B0DE sec */
    S(0xB0DF, 3); t_index(ss);              /* C0B0DF sbc entity_x,X */
    a = alu_sbc16(ss, a, t_read16(ss, ss_abs(ss, (uint16_t) (entity_x + x))));
    S(0xB0E2, 1); t_branch(ss, (a & 0x8000) == 0);  /* C0B0E2 bpl loc_C0B0EC */
    if((a & 0x8000) == 0) goto loc_C0B0EC;
    S(0xB0E4, 3); alu_cmp16(ss, a, 0xFFC0); /* C0B0E4 cmp #$FFC0 */
    S(0xB0E7, 1); t_branch(ss, !ss_c(ss));  /* C0B0E7 bcc loc_C0B0EC */
    if(!ss_c(ss)) goto loc_C0B0EC;
    S(0xB0E9, 3);                           /* C0B0E9 jmp entity_hit_react */
    ss_set_a(ss, a);
    ss_set_pc(ss, pb, 0xB171);
    return;
  }
                                            /* loc_C0B0ED */
  S(0xB0ED, 3);                             /* C0B0ED lda entity_x (player) */
  a = t_read16(ss, ss_abs(ss, entity_x));
  ss_set_nz16(ss, a);
  SI(0xB0F0); ss_set_c(ss, true);           /* C0B0F0 sec */
  S(0xB0F1, 3); t_index(ss);                /* C0B0F1 sbc entity_x,X */
  a = alu_sbc16(ss, a, t_read16(ss, ss_abs(ss, (uint16_t) (entity_x + x))));
  S(0xB0F4, 1); t_branch(ss, (a & 0x8000) != 0);  /* C0B0F4 bmi loc_C0B0EC */
  if((a & 0x8000) != 0) goto loc_C0B0EC;
  S(0xB0F6, 3); alu_cmp16(ss, a, 0x0040);   /* C0B0F6 cmp #$0040 */
  S(0xB0F9, 1); t_branch(ss, ss_c(ss));     /* C0B0F9 bcs loc_C0B0EC */
  if(ss_c(ss)) goto loc_C0B0EC;
  S(0xB0FB, 3);                             /* C0B0FB jmp entity_hit_react */
  ss_set_a(ss, a);
  ss_set_pc(ss, pb, 0xB171);
  return;

loc_C0B0EC:
  ss_set_a(ss, a);
  S(0xB0EC, 1);                             /* C0B0EC rts */
  ss_rts(ss);
}

/* ---------------------------------------------------------------------------
 * anim_cb_hit_enemies: $C0:B0FE
 *
 * The player's attack frames: play effect $0703, then sweep every live entity
 * (Y = 4, 6, ... up to $A6) of type $0E..$11 and hand the ones within $48
 * pixels on the facing side to entity_hit_react. The two loops are the same
 * test mirrored: $B113 for a left-facing attacker, $B142 for a right-facing
 * one, chosen by the bit/V test on entity_flags before either runs.
 * ------------------------------------------------------------------------- */
void anim_cb_hit_enemies(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint16_t dp = ss_dp(ss);

  S(0xB0FE, 3); a = 0x0703; ss_set_nz16(ss, a);  /* C0B0FE lda #$0703 */
  S(0xB101, 3);                             /* C0B101 jsr play_sound_effect */
  ss_set_a(ss, a);
  if(t_call_sub(ss, pb, 0xB0C5)) return;
  a = ss_a(ss); x = ss_x(ss); y = ss_y(ss);
  S(0xB104, 3); y = 0x0004;                 /* C0B104 ldy #$0004 */
  ss_set_y(ss, y); ss_set_nz16(ss, y);
  S(0xB107, 2);                             /* C0B107 stx ptr_04 */
  t_write16(ss, dp + ptr_04, x);
  S(0xB109, 3); t_index(ss);                /* C0B109 lda entity_x,X */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_x + x)));
  ss_set_nz16(ss, a);
  S(0xB10C, 2);                             /* C0B10C sta $06 */
  t_write16(ss, dp + scratch_06, a);
  S(0xB10E, 3); t_index(ss);                /* C0B10E bit entity_flags,X */
  alu_bit16(ss, a, t_read16(ss, ss_abs(ss, (uint16_t) (entity_flags + x))));
  S(0xB111, 1); t_branch(ss, !ss_v(ss));    /* C0B111 bvc loc_C0B142 */

  /* The two sweeps differ only in the sign test and the reach constant, so the
   * body below is written once against the base address of the copy that is
   * running: $B113 (facing one way) or $B142 (the other), $2F bytes apart. */
  {
    const bool second = !ss_v(ss);
    const uint16_t base = second ? 0xB142 : 0xB113;
    for(;;) {
      S(base + 0, 2);                       /* C0B113 cpy ptr_04 */
      alu_cmp16(ss, y, t_read16(ss, dp + ptr_04));
      S(base + 2, 1); t_branch(ss, ss_z(ss));  /* C0B115 beq loc_C0B13B */
      if(!ss_z(ss)) {
        S(base + 4, 3); t_index(ss);        /* C0B117 lda entity_hitstun_timer,X */
        a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_hitstun_timer + x)));
        ss_set_nz16(ss, a);
        S(base + 7, 1); t_branch(ss, a != 0);  /* C0B11A bne loc_C0B13B */
        if(a == 0) {
          S(base + 9, 3); t_index(ss);      /* C0B11C lda entity_type,Y */
          a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_type + y)));
          ss_set_nz16(ss, a);
          S(base + 12, 3); alu_cmp16(ss, a, 0x000E);  /* C0B11F cmp #$000E */
          S(base + 15, 1); t_branch(ss, !ss_c(ss));   /* C0B122 bcc loc_C0B13B */
          if(ss_c(ss)) {
            S(base + 17, 3); alu_cmp16(ss, a, 0x0012);  /* C0B124 cmp #$0012 */
            S(base + 20, 1); t_branch(ss, ss_c(ss));    /* C0B127 bcs loc_C0B13B */
            if(!ss_c(ss)) {
              S(base + 22, 3); t_index(ss); /* C0B129 lda entity_x,Y */
              a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_x + y)));
              ss_set_nz16(ss, a);
              SI(base + 25); ss_set_c(ss, true);  /* C0B12C sec */
              S(base + 26, 2);              /* C0B12D sbc $06 */
              a = alu_sbc16(ss, a, t_read16(ss, dp + scratch_06));
              bool hit;
              if(!second) {
                S(base + 28, 1);            /* C0B12F bpl loc_C0B13B */
                t_branch(ss, (a & 0x8000) == 0);
                hit = (a & 0x8000) != 0;
                if(hit) {
                  S(base + 30, 3); alu_cmp16(ss, a, 0xFFB8);  /* C0B131 cmp #$FFB8 */
                  S(base + 33, 1); t_branch(ss, !ss_c(ss));   /* C0B134 bcc loc_C0B13B */
                  hit = ss_c(ss);
                }
              } else {
                S(base + 28, 1);            /* C0B15E bmi loc_C0B16A */
                t_branch(ss, (a & 0x8000) != 0);
                hit = (a & 0x8000) == 0;
                if(hit) {
                  S(base + 30, 3); alu_cmp16(ss, a, 0x0048);  /* C0B160 cmp #$0048 */
                  S(base + 33, 1); t_branch(ss, ss_c(ss));    /* C0B163 bcs loc_C0B16A */
                  hit = !ss_c(ss);
                }
              }
              if(hit) {
                S(base + 35, 1);            /* C0B136 phy */
                ss_idle(ss);
                t_push16(ss, y);
                S(base + 36, 3);            /* C0B137 jsr entity_hit_react */
                if(t_call_sub(ss, pb, 0xB171)) return;
                a = ss_a(ss); x = ss_x(ss); y = ss_y(ss);
                S(base + 39, 1);            /* C0B13A ply */
                ss_idle(ss);
                ss_idle(ss);
                y = t_pull16(ss);
                ss_set_y(ss, y); ss_set_nz16(ss, y);
              }
            }
          }
        }
      }
                                            /* loc_C0B13B / loc_C0B16A */
      SI(base + 40); y = (uint16_t) (y + 1); ss_set_y(ss, y); ss_set_nz16(ss, y);
      SI(base + 41); y = (uint16_t) (y + 1); ss_set_y(ss, y); ss_set_nz16(ss, y);
      S(base + 42, 2);                      /* C0B13D cpy $A6 */
      alu_cmp16(ss, y, t_read16(ss, dp + entity_count));
      S(base + 44, 1); t_branch(ss, !ss_c(ss));  /* C0B13F bcc loc_C0B113 */
      if(ss_c(ss)) break;
    }
    ss_set_a(ss, a);
    S(base + 46, 1);                        /* C0B141 rts */
    ss_rts(ss);
  }
}

static const RecompEntry kAnimScripts[] = {
  { 0xc0aec8, "anim_update", anim_update },
  { 0xc0b022, "anim_callback_dispatch", anim_callback_dispatch },
  { 0xc0b025, "anim_cb_sfx_0704", anim_cb_sfx_0704 },
  { 0xc0b052, "anim_cb_sfx_0506", anim_cb_sfx_0506 },
  { 0xc0b057, "anim_cb_sfx_0606", anim_cb_sfx_0606 },
  { 0xc0b05c, "anim_cb_sfx_0602", anim_cb_sfx_0602 },
  { 0xc0b061, "anim_cb_sfx_0507", anim_cb_sfx_0507 },
  { 0xc0b066, "anim_cb_sfx_050C", anim_cb_sfx_050C },
  { 0xc0b06b, "anim_cb_sfx_0508", anim_cb_sfx_0508 },
  { 0xc0b070, "anim_cb_sfx_0709", anim_cb_sfx_0709 },
  { 0xc0b075, "play_footstep_sound", play_footstep_sound },
  { 0xc0b08f, "anim_cb_sfx_060E", anim_cb_sfx_060E },
  { 0xc0b0bc, "play_zone_transition_sound", play_zone_transition_sound },
  { 0xc0b0c5, "play_sound_effect", play_sound_effect },
  { 0xc0b0ce, "anim_cb_hit_player", anim_cb_hit_player },
  { 0xc0b0fe, "anim_cb_hit_enemies", anim_cb_hit_enemies },
};
RECOMP_REGISTER(kAnimScripts)
