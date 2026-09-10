/* OAM assembly and upload, bank $C0.
 *
 * $0200..$03FF is the 128-entry low OAM table and $0400..$041F the high table;
 * oam_write_ptr ($94) is a live pointer into the low table, not a count.
 */
#include <stdint.h>
#include <stdbool.h>

#include "snes_state.h"
#include "dream_ram.h"
#include "dream_alu.h"
#include "dream_time.h"

/* ---------------------------------------------------------------------------
 * clear_sprite_table: $C0:A500
 *
 * Called once per main-loop iteration from $C08235 and once from $C09324.
 * Clears the sixteen high-OAM words and rewinds the two write cursors.
 * Exit: A = $0200, N clear, Z clear.
 * ------------------------------------------------------------------------- */
void clear_sprite_table(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint16_t dp = ss_dp(ss);

  for(int i = 0; i < 16; i++) {             /* C0A500..C0A52D stz $0400,+2 */
    S((uint16_t) (0xA500 + i * 3), 3);
    t_write16(ss, ss_abs(ss, (uint16_t) (oam_buffer_upper + i * 2)), 0);
  }
  S(0xA530, 3);                             /* C0A530 lda #$0200 */
  a = 0x0200;
  ss_set_nz16(ss, a);
  S(0xA533, 2);                             /* C0A533 sta oam_write_ptr */
  t_write16(ss, dp + oam_write_ptr, a);
  S(0xA535, 2);                             /* C0A535 stz entity_render_index */
  t_write16(ss, dp + entity_render_index, 0);
  ss_set_a(ss, a);
  S(0xA537, 1);                             /* C0A537 rts */
  ss_rts(ss);
}

/* ---------------------------------------------------------------------------
 * oam_hide_unused_sprites: $C0:ADE7
 *
 * Fills the tail of the low OAM table, from oam_write_ptr to $0400, with
 * $F0FF: Y = $F0, off the bottom of the screen. Four bytes per entry, so the
 * word store covers the Y/tile pair and the next iteration the attribute pair.
 * The store is direct-page indexed with DP zero, so "sta $00,X" with X in
 * $0200..$03FF addresses the buffer directly.
 * Exit: X = $0400, Z set, N clear.
 * ------------------------------------------------------------------------- */
void oam_hide_unused_sprites(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint16_t dp = ss_dp(ss);

  S(0xADE7, 2);                             /* C0ADE7 ldx oam_write_ptr */
  x = t_read16(ss, dp + oam_write_ptr);
  ss_set_nz16(ss, x);
  S(0xADE9, 3); alu_cpx16(ss, x, 0x0400); /* C0ADE9 cpx #$0400 */
  S(0xADEC, 1); t_branch(ss, x == 0x0400);/* C0ADEC beq loc_C0ADFC */
  if(x != 0x0400) {
    S(0xADEE, 3);                           /* C0ADEE lda #$F0FF */
    a = 0xF0FF;
    ss_set_nz16(ss, a);
    do {
      S(0xADF1, 2); t_index(ss);            /* C0ADF1 sta $00,X */
      t_write16(ss, (uint16_t) (dp + nmi_handler_ptr + x), a);
      SI(0xADF3); x = (uint16_t) (x + 1); ss_set_nz16(ss, x);  /* C0ADF3 inx */
      SI(0xADF4); x = (uint16_t) (x + 1); ss_set_nz16(ss, x);  /* C0ADF4 inx */
      SI(0xADF5); x = (uint16_t) (x + 1); ss_set_nz16(ss, x);  /* C0ADF5 inx */
      SI(0xADF6); x = (uint16_t) (x + 1); ss_set_nz16(ss, x);  /* C0ADF6 inx */
      S(0xADF7, 3); alu_cpx16(ss, x, 0x0400);  /* C0ADF7 cpx #$0400 */
      S(0xADFA, 1); t_branch(ss, x != 0x0400);  /* C0ADFA bne */
    } while(x != 0x0400);
    ss_set_a(ss, a);
  }
  ss_set_x(ss, x);
  S(0xADFC, 1);                             /* C0ADFC rts */
  ss_rts(ss);
}

/* ---------------------------------------------------------------------------
 * oam_dma_upload: $C0:ADFD
 *
 * Programs channel 0 for the $220-byte OAM buffer at $00:0200 but does not start
 * it: the channel bit goes into dma_pending_mask ($02), which whichever NMI
 * scroll setter runs this frame ors into its own mask and writes to MDMAEN.
 * Exit: A = $0001, N clear, Z clear.
 * ------------------------------------------------------------------------- */
void oam_dma_upload(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint16_t dp = ss_dp(ss);

  S(0xADFD, 3);                             /* C0ADFD lda #$0200 */
  a = 0x0200; ss_set_nz16(ss, a);
  S(0xAE00, 3); t_write16(ss, ss_abs(ss, A1TL0), a);  /* C0AE00 sta A1TL0 */
  S(0xAE03, 3); t_write16(ss, ss_abs(ss, A2AL0), a);  /* C0AE03 sta A2AL0 */
  S(0xAE06, 3); a = 0x0220; ss_set_nz16(ss, a);  /* C0AE06 lda #$0220 */
  S(0xAE09, 3); t_write16(ss, ss_abs(ss, DASL0), a);  /* C0AE09 sta DASL0 */
  S(0xAE0C, 3); a = 0x0400; ss_set_nz16(ss, a);  /* C0AE0C lda #$0400 */
  S(0xAE0F, 3); t_write16(ss, ss_abs(ss, DMAP0), a);  /* C0AE0F sta DMAP0 */

  SEP(0xAE12, 0x20);                        /* C0AE12 sep #$20 */
  S(0xAE14, 3); t_write8(ss, ss_abs(ss, A1B0), 0x00);  /* C0AE14 stz A1B0 */
  REP(0xAE17, 0x20);                        /* C0AE17 rep #$20 */

  S(0xAE19, 3); a = 0x0001; ss_set_nz16(ss, a);  /* C0AE19 lda #$0001 */
  S(0xAE1C, 2); t_write16(ss, dp + dma_pending_mask, a); /* C0AE1C sta $02 */
  ss_set_a(ss, a);
  S(0xAE1E, 1);                             /* C0AE1E rts */
  ss_rts(ss);
}

/* ---------------------------------------------------------------------------
 * entity_sort_draw_order: $C0:AE1F
 *
 * One forward and one backward pass of a bubble sort over the sixteen entity
 * indices in entity_render_order ($09A8..$09C7), keyed on entity_depth_key.
 *
 * The addressing is the trick worth reading twice: X walks $09A6..$09C4 and the
 * accesses are *direct page* indexed, so "ldy $04,X" is $0004 + X, i.e. the
 * element after the one "lda $02,X" reads and two after "lda $00,X". With DP 0
 * the three offsets name three consecutive entries of the array, and the pass
 * swaps a pair by writing through two of them. $52 carries the previous key.
 *
 * A tie in the key (beq at $AE36) goes to loc_C0AE5E, which breaks it on
 * entity_substate ($07A8): substate 4 or 2 sorts in front unless the other
 * entity has one too. Exit: X = $09A6, Z set.
 * ------------------------------------------------------------------------- */
void entity_sort_draw_order(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint16_t dp = ss_dp(ss);

  S(0xAE1F, 3);                             /* C0AE1F lda entity_render_order */
  a = t_read16(ss, ss_abs(ss, entity_render_order));
  ss_set_nz16(ss, a);
  SI(0xAE22); y = a; ss_set_nz16(ss, y);    /* C0AE22 tay */
  S(0xAE23, 3); t_index(ss);                /* C0AE23 lda entity_depth_key,Y */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_depth_key + y)));
  ss_set_nz16(ss, a);
  S(0xAE26, 2); t_write16(ss, dp + depth_sort_key, a); /* C0AE26 sta $52 */
  S(0xAE28, 3);                             /* C0AE28 ldx #$09A6 */
  x = 0x09A6;
  ss_set_nz16(ss, x);

  for(;;) {
    /* the pass is thousands of cycles long, so let the ROM take over if the
     * frame ends or an interrupt is latched part-way through it */
    S(0xAE2B, 2); t_index(ss);              /* C0AE2B ldy $04,X */
    y = t_read16(ss, (uint16_t) (dp + ptr_04 + x));
    ss_set_nz16(ss, y);
    S(0xAE2D, 3); t_index(ss);              /* C0AE2D lda entity_depth_key,Y */
    a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_depth_key + y)));
    ss_set_nz16(ss, a);
    S(0xAE30, 2);                           /* C0AE30 cmp $52 */
    alu_cmp16(ss, a, t_read16(ss, dp + depth_sort_key));
    S(0xAE32, 2); t_write16(ss, dp + depth_sort_key, a); /* C0AE32 sta $52 */
    const bool ge = ss_c(ss), eq = ss_z(ss);
    bool swap = false;
    S(0xAE34, 1); t_branch(ss, !ge);        /* C0AE34 bcc loc_C0AE3E */
    if(ge) {
      S(0xAE36, 1); t_branch(ss, eq);       /* C0AE36 beq loc_C0AE5E */
      if(eq) {
        S(0xAE5E, 3); t_index(ss);          /* C0AE5E lda $07A8,Y */
        a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_substate + y)));
        ss_set_nz16(ss, a);
        S(0xAE61, 3); alu_cmp16(ss, a, 0x0004);  /* C0AE61 cmp #$0004 */
        S(0xAE64, 1); t_branch(ss, a == 0x0004);  /* C0AE64 beq loc_C0AE6B */
        bool front = a == 0x0004;
        if(!front) {
          S(0xAE66, 3); alu_cmp16(ss, a, 0x0002);  /* C0AE66 cmp #$0002 */
          S(0xAE69, 1); t_branch(ss, a != 0x0002);  /* C0AE69 bne loc_C0AE3E */
          front = a == 0x0002;
        }
        if(front) {
          S(0xAE6B, 2); t_index(ss);        /* C0AE6B ldy $02,X */
          y = t_read16(ss, (uint16_t) (dp + dma_pending_mask + x));
          ss_set_nz16(ss, y);
          S(0xAE6D, 3); t_index(ss);        /* C0AE6D lda $07A8,Y */
          a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_substate + y)));
          ss_set_nz16(ss, a);
          S(0xAE70, 2); t_index(ss);        /* C0AE70 ldy $04,X */
          y = t_read16(ss, (uint16_t) (dp + ptr_04 + x));
          ss_set_nz16(ss, y);
          S(0xAE72, 3); alu_cmp16(ss, a, 0x0004);  /* C0AE72 cmp #$0004 */
          S(0xAE75, 1); t_branch(ss, a == 0x0004);  /* C0AE75 beq loc_C0AE3E */
          if(a != 0x0004) {
            S(0xAE77, 3); alu_cmp16(ss, a, 0x0002);  /* C0AE77 cmp #$0002 */
            S(0xAE7A, 1); t_branch(ss, a == 0x0002);  /* C0AE7A beq loc_C0AE3E */
            if(a != 0x0002) {
              S(0xAE7C, 1); t_branch(ss, true);  /* C0AE7C bra loc_C0AE38 */
              swap = true;
            }
          }
        }
      } else {
        swap = true;
      }
    }
    if(swap) {
      S(0xAE38, 2); t_index(ss);            /* C0AE38 lda $02,X */
      a = t_read16(ss, (uint16_t) (dp + dma_pending_mask + x));
      ss_set_nz16(ss, a);
      S(0xAE3A, 2); t_index(ss);            /* C0AE3A sta $04,X */
      t_write16(ss, (uint16_t) (dp + ptr_04 + x), a);
      S(0xAE3C, 2); t_index(ss);            /* C0AE3C sty $02,X */
      t_write16(ss, (uint16_t) (dp + dma_pending_mask + x), y);
    }
    SI(0xAE3E); x = (uint16_t) (x + 1); ss_set_nz16(ss, x);  /* C0AE3E inx */
    SI(0xAE3F); x = (uint16_t) (x + 1); ss_set_nz16(ss, x);  /* C0AE3F inx */
    S(0xAE40, 3); alu_cpx16(ss, x, 0x09C4);  /* C0AE40 cpx #$09C4 */
    S(0xAE43, 1); t_branch(ss, x != 0x09C4);  /* C0AE43 bne loc_C0AE2B */
    if(x == 0x09C4) break;
  }

  for(;;) {
    S(0xAE45, 2); t_index(ss);              /* C0AE45 ldy $00,X */
    y = t_read16(ss, (uint16_t) (dp + nmi_handler_ptr + x));
    ss_set_nz16(ss, y);
    S(0xAE47, 3); t_index(ss);              /* C0AE47 lda entity_depth_key,Y */
    a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_depth_key + y)));
    ss_set_nz16(ss, a);
    S(0xAE4A, 2);                           /* C0AE4A cmp $52 */
    alu_cmp16(ss, a, t_read16(ss, dp + depth_sort_key));
    S(0xAE4C, 2); t_write16(ss, dp + depth_sort_key, a); /* C0AE4C sta $52 */
    const bool ge = ss_c(ss);
    S(0xAE4E, 1); t_branch(ss, ge);         /* C0AE4E bcs loc_C0AE56 */
    if(!ge) {
      S(0xAE50, 2); t_index(ss);            /* C0AE50 lda $02,X */
      a = t_read16(ss, (uint16_t) (dp + dma_pending_mask + x));
      ss_set_nz16(ss, a);
      S(0xAE52, 2); t_index(ss);            /* C0AE52 sta $00,X */
      t_write16(ss, (uint16_t) (dp + nmi_handler_ptr + x), a);
      S(0xAE54, 2); t_index(ss);            /* C0AE54 sty $02,X */
      t_write16(ss, (uint16_t) (dp + dma_pending_mask + x), y);
    }
    SI(0xAE56); x = (uint16_t) (x - 1); ss_set_nz16(ss, x);  /* C0AE56 dex */
    SI(0xAE57); x = (uint16_t) (x - 1); ss_set_nz16(ss, x);  /* C0AE57 dex */
    S(0xAE58, 3); alu_cpx16(ss, x, 0x09A6);  /* C0AE58 cpx #$09A6 */
    S(0xAE5B, 1); t_branch(ss, x != 0x09A6);  /* C0AE5B bne loc_C0AE45 */
    if(x == 0x09A6) break;
  }

  ss_set_a(ss, a);
  ss_set_x(ss, x);
  ss_set_y(ss, y);
  S(0xAE5D, 1);                             /* C0AE5D rts */
  ss_rts(ss);
}

/* ---------------------------------------------------------------------------
 * entity_upload_pending_tiles: $C0:AE7E
 *
 * Runs the queue of per-entity VRAM tile uploads: 8-byte records from $0A8A,
 * flagged pending by bit 15 of the word at $0A90. Each pending record is one
 * DMA to VRAM, then the flag word is cleared.
 *
 * sep #$10 narrows the index registers for the whole routine, so X wraps at $FF
 * and the record index is an 8-bit quantity; the closing rep #$10 widens them
 * again but does not restore the high bytes it dropped, so X and Y really are
 * their low bytes on return (X = the index that stopped the walk, Y = 1).
 * The 8-bit index also changes the *timing* of the four reads: absolute-indexed
 * reads only pay the extra internal cycle when the index crosses a page, which
 * $0A90,X does once X reaches $70.
 * The first sta A1B0 writes a whole word: the low byte is the source bank, the
 * high byte lands in DASL0 and is overwritten by the very next store.
 * ------------------------------------------------------------------------- */
void entity_upload_pending_tiles(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);

  S(0xAE7E, 3);                             /* C0AE7E lda #$1801 */
  a = 0x1801; ss_set_nz16(ss, a);
  S(0xAE81, 3); t_write16(ss, ss_abs(ss, DMAP0), a);  /* C0AE81 sta DMAP0 */

  SEP(0xAE84, 0x10);                        /* C0AE84 sep #$10 */
  x = (uint16_t) (ss_x(ss) & 0xff);
  y = (uint16_t) (ss_y(ss) & 0xff);

  S(0xAE86, 2); y = 0x80; ss_set_nz8(ss, (uint8_t) y); /* C0AE86 ldy #$80 */
  S(0xAE88, 3);                             /* C0AE88 sty VMAIN */
  t_write8(ss, ss_abs(ss, VMAIN), (uint8_t) y);
  S(0xAE8B, 2); y = 0x01; ss_set_nz8(ss, (uint8_t) y); /* C0AE8B ldy #$01 */
  SI(0xAE8D); a = ss_dp(ss); ss_set_nz16(ss, a);  /* C0AE8D tdc */
  SI(0xAE8E); ss_set_c(ss, false);          /* C0AE8E clc */

  for(;;) {
    SI(0xAE8F); x = (uint16_t) (a & 0xff); ss_set_nz8(ss, (uint8_t) x);  /* C0AE8F tax */

    S(0xAE90, 3);                           /* C0AE90 lda $0A90,X */
    if(((entity_tile_job + 6) >> 8) != ((entity_tile_job + 6 + x) >> 8)) t_index(ss);
    a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_tile_job + 6 + x)));
    ss_set_nz16(ss, a);
    S(0xAE93, 1); t_branch(ss, (a & 0x8000) == 0);  /* C0AE93 bpl */
    if((a & 0x8000) == 0) break;

    S(0xAE95, 3); t_write16(ss, ss_abs(ss, A1B0), a);  /* C0AE95 sta A1B0 */
    S(0xAE98, 3);                           /* C0AE98 lda $0A8A,X */
    if(((entity_tile_job + 0) >> 8) != ((entity_tile_job + 0 + x) >> 8)) t_index(ss);
    a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_tile_job + 0 + x)));
    ss_set_nz16(ss, a);
    S(0xAE9B, 3); t_write16(ss, ss_abs(ss, DASL0), a);  /* C0AE9B sta DASL0 */
    S(0xAE9E, 3);                           /* C0AE9E lda $0A8C,X */
    if(((entity_tile_job + 2) >> 8) != ((entity_tile_job + 2 + x) >> 8)) t_index(ss);
    a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_tile_job + 2 + x)));
    ss_set_nz16(ss, a);
    S(0xAEA1, 3); t_write16(ss, ss_abs(ss, VMADDL), a); /* C0AEA1 sta VMADDL */
    S(0xAEA4, 3);                           /* C0AEA4 lda $0A8E,X */
    if(((entity_tile_job + 4) >> 8) != ((entity_tile_job + 4 + x) >> 8)) t_index(ss);
    a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_tile_job + 4 + x)));
    ss_set_nz16(ss, a);
    S(0xAEA7, 3); t_write16(ss, ss_abs(ss, A1TL0), a);  /* C0AEA7 sta A1TL0 */
    S(0xAEAA, 3);                           /* C0AEAA sty MDMAEN */
    t_write8(ss, ss_abs(ss, MDMAEN), (uint8_t) y);
    /* the transfer runs on the next bus cycles, which are the stz's own fetches */
    S(0xAEAD, 3); t_index(ss);              /* C0AEAD stz $0A90,X */
    t_write16(ss, ss_abs(ss, (uint16_t) (entity_tile_job + 6 + x)), 0);

    SI(0xAEB0); a = x; ss_set_nz16(ss, a);  /* C0AEB0 txa */
    S(0xAEB1, 3); a = alu_adc16(ss, a, 0x0008);  /* C0AEB1 adc #$0008 */
    S(0xAEB4, 1); t_branch(ss, true);       /* C0AEB4 bra loc_C0AE8F */
  }

  REP(0xAEB6, 0x10);                        /* C0AEB6 rep #$10 */
  ss_set_a(ss, a);
  ss_set_x(ss, x);
  ss_set_y(ss, y);
  S(0xAEB8, 1);                             /* C0AEB8 rts */
  ss_rts(ss);
}

static const RecompEntry kOam[] = {
  { 0xc0a500, "clear_sprite_table", clear_sprite_table },
  { 0xc0ade7, "oam_hide_unused_sprites", oam_hide_unused_sprites },
  { 0xc0adfd, "oam_dma_upload", oam_dma_upload },
  { 0xc0ae1f, "entity_sort_draw_order", entity_sort_draw_order },
  { 0xc0ae7e, "entity_upload_pending_tiles", entity_upload_pending_tiles },
};
RECOMP_REGISTER(kOam)
