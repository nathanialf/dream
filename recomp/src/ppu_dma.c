/* PPU and DMA helpers, bank $C0.
 *
 * Every routine here programs DMA channel 0 (or, for dma_setup_channel_step, the
 * channel X selects) and kicks it with MDMAEN. All register traffic goes through
 * the timed accessors: an untimed write to $420B would only mark the transfer
 * pending instead of running it, and two of these routines reprogram the channel
 * immediately afterwards. Because the bodies model the instruction stream, the
 * bus cycles that arm and then perform the transfer are the fetches of the
 * instructions after the store, exactly where the ROM's are.
 *
 * The absolute addressing is data-bank relative in the ROM, so it is here too
 * (ss_abs); the callers reach these with DB = $80 or DB = $00, both of which map
 * $2100/$42xx/$43xx to the registers.
 */
#include <stdint.h>
#include <stdbool.h>

#include "snes_state.h"
#include "dream_ram.h"
#include "dream_alu.h"
#include "dream_time.h"

/* ---------------------------------------------------------------------------
 * dma_setup_channel_step: $C0:848C
 *
 * Entry: A = A1Tx source address, Y = DMAPx | BBADx, X = channel byte offset
 * ($00, $10, ... $70), ptr_04 = source bank, $05 = DASBx bank.
 * Exit: A = (Y & $FF00) | $05, N/Z from the 8-bit load of $05, X/Y unchanged.
 * The sep/rep pair around the two bank stores cancels out, so the m flag is
 * unchanged on return and the hook does not have to touch it.
 * ------------------------------------------------------------------------- */
void dma_setup_channel_step(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint16_t dp = ss_dp(ss);

  S(0x848C, 3); t_index(ss);                /* C0848C sta A1TL0,X */
  t_write16(ss, ss_abs(ss, (uint16_t) (A1TL0 + x)), a);
  SI(0x848F); a = y; ss_set_nz16(ss, a);    /* C0848F tya */
  S(0x8490, 3); t_index(ss);                /* C08490 sta DMAP0,X */
  t_write16(ss, ss_abs(ss, (uint16_t) (DMAP0 + x)), a);

  SEP(0x8493, 0x20); /* C08493 sep #$20 */
  S(0x8495, 2);                             /* C08495 lda ptr_04 */
  uint8_t b = t_read8(ss, dp + ptr_04);
  a = (uint16_t) ((a & 0xff00) | b);
  ss_set_nz8(ss, b);
  S(0x8497, 3); t_index(ss);                /* C08497 sta A1B0,X */
  t_write8(ss, ss_abs(ss, (uint16_t) (A1B0 + x)), b);

  S(0x849A, 2);                             /* C0849A lda $05 */
  b = t_read8(ss, dp + ptr_04 + 1);
  a = (uint16_t) ((a & 0xff00) | b);
  ss_set_nz8(ss, b);
  S(0x849C, 3); t_index(ss);                /* C0849C sta DASB0,X */
  t_write8(ss, ss_abs(ss, (uint16_t) (DASB0 + x)), b);
  REP(0x849F, 0x20); /* C0849F rep #$20 */

  ss_set_a(ss, a);
  S(0x84A1, 1);                             /* C084A1 rts */
  ss_rts(ss);
}

/* ---------------------------------------------------------------------------
 * cgram_upload_queue_flush: $C0:9D32
 *
 * Walks the queue of pending palette uploads backwards in 8-byte records
 * ($0B84 size, $0B86 CGRAM address, $0B88 source address, $0B8A source bank),
 * $0B8A doubling as the queue depth. Each record is one DMA to CGRAM.
 * Exit: queue emptied, A = X = 0, Z set, N clear (both on the empty-queue path,
 * where the entry ldx already set them, and on the drained path, where the
 * closing tax does).
 * ------------------------------------------------------------------------- */
void cgram_upload_queue_flush(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);

  S(0x9D32, 3);                             /* C09D32 ldx $0B8A */
  x = t_read16(ss, ss_abs(ss, cgram_queue_index));
  ss_set_nz16(ss, x);
  S(0x9D35, 1); t_branch(ss, x == 0);       /* C09D35 beq loc_C09D69 */
  if(x == 0) {
    ss_set_x(ss, x);
    S(0x9D69, 1);                           /* C09D69 rts */
    ss_rts(ss);
    return;
  }

  S(0x9D37, 3); a = 0x2202; ss_set_nz16(ss, a);  /* C09D37 lda #$2202 */
  S(0x9D3A, 3); t_write16(ss, ss_abs(ss, DMAP0), a);  /* C09D3A sta DMAP0 */

  do {
    S(0x9D3D, 3); t_index(ss);              /* C09D3D lda $0B84,X */
    a = t_read16(ss, ss_abs(ss, (uint16_t) (0x0B84 + x)));
    ss_set_nz16(ss, a);
    S(0x9D40, 3); t_write16(ss, ss_abs(ss, DASL0), a); /* C09D40 sta DASL0 */
    S(0x9D43, 3); t_index(ss);              /* C09D43 lda $0B88,X */
    a = t_read16(ss, ss_abs(ss, (uint16_t) (0x0B88 + x)));
    ss_set_nz16(ss, a);
    S(0x9D46, 3); t_write16(ss, ss_abs(ss, A1TL0), a); /* C09D46 sta A1TL0 */

    SEP(0x9D49, 0x20);                      /* C09D49 sep #$20 */
    S(0x9D4B, 3); t_index(ss);              /* C09D4B lda $0B8A,X */
    uint8_t b = t_read8(ss, ss_abs(ss, (uint16_t) (cgram_queue_index + x)));
    a = (uint16_t) ((a & 0xff00) | b);
    ss_set_nz8(ss, b);
    S(0x9D4E, 3); t_write8(ss, ss_abs(ss, A1B0), b);  /* C09D4E sta A1B0 */
    S(0x9D51, 3); t_index(ss);              /* C09D51 lda $0B86,X */
    b = t_read8(ss, ss_abs(ss, (uint16_t) (0x0B86 + x)));
    a = (uint16_t) ((a & 0xff00) | b);
    ss_set_nz8(ss, b);
    S(0x9D54, 3); t_write8(ss, ss_abs(ss, CGADD), b);  /* C09D54 sta CGADD */
    S(0x9D57, 2);                           /* C09D57 lda #$01 */
    a = (uint16_t) ((a & 0xff00) | 0x01);
    ss_set_nz8(ss, 0x01);
    S(0x9D59, 3); t_write8(ss, ss_abs(ss, MDMAEN), 0x01); /* C09D59 sta MDMAEN */
    REP(0x9D5C, 0x20);                      /* C09D5C rep #$20 */

    SI(0x9D5E); a = x; ss_set_nz16(ss, a);  /* C09D5E txa */
    SI(0x9D5F); ss_set_c(ss, true);         /* C09D5F sec */
    S(0x9D60, 3); a = alu_sbc16(ss, a, 0x0008);/* C09D60 sbc #$0008 */
    SI(0x9D63); x = a; ss_set_nz16(ss, x);  /* C09D63 tax */
    S(0x9D64, 1); t_branch(ss, x != 0);     /* C09D64 bne loc_C09D3D */
  } while(x != 0);

  S(0x9D66, 3);                             /* C09D66 stz $0B8A */
  t_write16(ss, ss_abs(ss, cgram_queue_index), 0);
  ss_set_a(ss, a);
  ss_set_x(ss, x);
  S(0x9D69, 1);                             /* C09D69 rts */
  ss_rts(ss);
}

/* ---------------------------------------------------------------------------
 * dma_fill_vram_zero: $C0:A445
 *
 * Entry: A = VRAM word address. Fills $0800 bytes from the two ROM bytes at
 * $00:A443 with the A-bus address fixed (DMAP0 = $1809), i.e. a constant fill.
 * Exit: A = $1801 (the 8-bit lda #$01 leaves the high byte of #$1809 behind),
 * N clear, Z clear.
 * ------------------------------------------------------------------------- */
void dma_fill_vram_zero(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);

  S(0xA445, 3); t_write16(ss, ss_abs(ss, VMADDL), a);  /* C0A445 sta VMADDL */
  S(0xA448, 3); a = 0xA443; ss_set_nz16(ss, a);  /* C0A448 lda #$A443 */
  S(0xA44B, 3); t_write16(ss, ss_abs(ss, A1TL0), a);  /* C0A44B sta A1TL0 */
  S(0xA44E, 3); t_write16(ss, ss_abs(ss, A2AL0), a);  /* C0A44E sta A2AL0 */
  S(0xA451, 3); a = 0x0800; ss_set_nz16(ss, a);  /* C0A451 lda #$0800 */
  S(0xA454, 3); t_write16(ss, ss_abs(ss, DASL0), a);  /* C0A454 sta DASL0 */
  S(0xA457, 3); a = 0x1809; ss_set_nz16(ss, a);  /* C0A457 lda #$1809 */
  S(0xA45A, 3); t_write16(ss, ss_abs(ss, DMAP0), a);  /* C0A45A sta DMAP0 */

  SEP(0xA45D, 0x20);                        /* C0A45D sep #$20 */
  S(0xA45F, 3); t_write8(ss, ss_abs(ss, A1B0), 0x00);  /* C0A45F stz A1B0 */
  S(0xA462, 2);                             /* C0A462 lda #$01 */
  a = (uint16_t) ((a & 0xff00) | 0x01);
  ss_set_nz8(ss, 0x01);
  S(0xA464, 3); t_write8(ss, ss_abs(ss, MDMAEN), 0x01);/* C0A464 sta MDMAEN */
  REP(0xA467, 0x20);                        /* C0A467 rep #$20 */

  ss_set_a(ss, a);
  S(0xA469, 1);                             /* C0A469 rts */
  ss_rts(ss);
}

/* ---------------------------------------------------------------------------
 * dma_upload_to_vram: $C0:A46A
 *
 * Entry: A = source address, Y = byte count, X = source bank.
 * Exit: A = $1801; X and Y truncated to their low bytes. That truncation is a
 * real, observable effect: the sep #$30 at $A476 narrows the index registers,
 * which clears the high halves of X and Y, and the rep #$30 at $A480 widens them
 * again without restoring what was dropped. Callers reload X/Y before reusing
 * them (checked at every jsr site in out/dream.asm), but the state has to match.
 * ------------------------------------------------------------------------- */
void dma_upload_to_vram(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);

  S(0xA46A, 3); t_write16(ss, ss_abs(ss, A1TL0), a);  /* C0A46A sta A1TL0 */
  S(0xA46D, 3); t_write16(ss, ss_abs(ss, DASL0), y);  /* C0A46D sty DASL0 */
  S(0xA470, 3); a = 0x1801; ss_set_nz16(ss, a);  /* C0A470 lda #$1801 */
  S(0xA473, 3); t_write16(ss, ss_abs(ss, DMAP0), a);  /* C0A473 sta DMAP0 */

  SEP(0xA476, 0x30);                        /* C0A476 sep #$30 */
  x = (uint16_t) (x & 0xff);
  y = (uint16_t) (y & 0xff);

  S(0xA478, 3);                             /* C0A478 stx A1B0 */
  t_write8(ss, ss_abs(ss, A1B0), (uint8_t) x);
  S(0xA47B, 2);                             /* C0A47B lda #$01 */
  a = (uint16_t) ((a & 0xff00) | 0x01);
  ss_set_nz8(ss, 0x01);
  S(0xA47D, 3); t_write8(ss, ss_abs(ss, MDMAEN), 0x01);/* C0A47D sta MDMAEN */
  REP(0xA480, 0x30);                        /* C0A480 rep #$30 */

  ss_set_a(ss, a);
  ss_set_x(ss, x);
  ss_set_y(ss, y);
  S(0xA482, 1);                             /* C0A482 rts */
  ss_rts(ss);
}

/* ---------------------------------------------------------------------------
 * dma_upload_to_cgram: $C0:A483
 *
 * Entry: A = source address in bank $C4, X = palette count (bytes = X * 8),
 * Y = CGRAM word address.
 * Exit: A = ($01 over the high byte of the shifted count), N/Z from lda #$01,
 * C from the last of the three asl.
 * ------------------------------------------------------------------------- */
void dma_upload_to_cgram(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);

  S(0xA483, 3); t_write16(ss, ss_abs(ss, A1TL0), a);  /* C0A483 sta A1TL0 */
  SI(0xA486); a = x; ss_set_nz16(ss, a);    /* C0A486 txa */
  SI(0xA487); a = alu_asl16(ss, a);         /* C0A487 asl A */
  SI(0xA488); a = alu_asl16(ss, a);         /* C0A488 asl A */
  SI(0xA489); a = alu_asl16(ss, a);         /* C0A489 asl A */
  S(0xA48A, 3); t_write16(ss, ss_abs(ss, DASL0), a);  /* C0A48A sta DASL0 */
  S(0xA48D, 3); a = 0x2200; ss_set_nz16(ss, a);  /* C0A48D lda #$2200 */
  S(0xA490, 3); t_write16(ss, ss_abs(ss, DMAP0), a);  /* C0A490 sta DMAP0 */

  SEP(0xA493, 0x20);                        /* C0A493 sep #$20 */
  S(0xA495, 2);                             /* C0A495 lda #$C4 */
  a = (uint16_t) ((a & 0xff00) | 0xC4);
  ss_set_nz8(ss, 0xC4);
  S(0xA497, 3); t_write8(ss, ss_abs(ss, A1B0), 0xC4);  /* C0A497 sta A1B0 */
  SI(0xA49A);                               /* C0A49A tya */
  a = (uint16_t) ((a & 0xff00) | (y & 0xff));
  ss_set_nz8(ss, (uint8_t) y);
  S(0xA49B, 3);                             /* C0A49B sta CGADD */
  t_write8(ss, ss_abs(ss, CGADD), (uint8_t) y);
  S(0xA49E, 2);                             /* C0A49E lda #$01 */
  a = (uint16_t) ((a & 0xff00) | 0x01);
  ss_set_nz8(ss, 0x01);
  S(0xA4A0, 3); t_write8(ss, ss_abs(ss, MDMAEN), 0x01);/* C0A4A0 sta MDMAEN */
  REP(0xA4A3, 0x20);                        /* C0A4A3 rep #$20 */

  ss_set_a(ss, a);
  S(0xA4A5, 1);                             /* C0A4A5 rts */
  ss_rts(ss);
}

/* ---------------------------------------------------------------------------
 * set_bg_scroll_prep: $C0:A4A6, set_bg_scroll: $C0:A4A8
 *
 * The body writes both halves of every BG scroll register: the three H offsets
 * to 0 and the three V offsets to $FF (so the tilemap sits one line up). It runs
 * with an 8-bit accumulator, which is the whole difference between the two entry
 * points: set_bg_scroll_prep is the sep #$20 in front, for the one caller that
 * arrives with a 16-bit A ($C082B9); the other three callers ($C08511, $C087C5,
 * $C088D8) enter at set_bg_scroll already in 8-bit mode.
 *
 * Either way the body ends with rep #$20, so the m flag is 0 on return. That is
 * a state change for the set_bg_scroll callers and a no-op for the prep caller.
 * Exit: A low byte = $FF, N set, Z clear.
 * ------------------------------------------------------------------------- */
static void set_bg_scroll_body(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  static const uint16_t kHofs[6] = { BG1HOFS, BG1HOFS, BG2HOFS, BG2HOFS, BG3HOFS, BG3HOFS };
  static const uint16_t kVofs[6] = { BG1VOFS, BG1VOFS, BG2VOFS, BG2VOFS, BG3VOFS, BG3VOFS };

  for(int i = 0; i < 6; i++) {              /* C0A4A8..C0A4B7 stz BGnHOFS */
    S((uint16_t) (0xA4A8 + i * 3), 3);
    t_write8(ss, ss_abs(ss, kHofs[i]), 0x00);
  }
  S(0xA4BA, 2);                             /* C0A4BA lda #$FF */
  a = (uint16_t) ((a & 0xff00) | 0xFF);
  ss_set_nz8(ss, 0xFF);
  for(int i = 0; i < 6; i++) {              /* C0A4BC..C0A4CB sta BGnVOFS */
    S((uint16_t) (0xA4BC + i * 3), 3);
    t_write8(ss, ss_abs(ss, kVofs[i]), 0xFF);
  }

  ss_set_a(ss, a);
  REP(0xA4CE, 0x20);                        /* C0A4CE rep #$20 */
  S(0xA4D0, 1);                             /* C0A4D0 rts */
  ss_rts(ss);
}

void set_bg_scroll_prep(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  SEP(0xA4A6, 0x20);                        /* C0A4A6 sep #$20 */
  set_bg_scroll_body(ss);
}

void set_bg_scroll(SnesState* ss) {
  set_bg_scroll_body(ss);
}

static const RecompEntry kPpuDma[] = {
  { 0xc0848c, "dma_setup_channel_step", dma_setup_channel_step },
  { 0xc09d32, "cgram_upload_queue_flush", cgram_upload_queue_flush },
  { 0xc0a445, "dma_fill_vram_zero", dma_fill_vram_zero },
  { 0xc0a46a, "dma_upload_to_vram", dma_upload_to_vram },
  { 0xc0a483, "dma_upload_to_cgram", dma_upload_to_cgram },
  { 0xc0a4a6, "set_bg_scroll_prep", set_bg_scroll_prep },
  { 0xc0a4a8, "set_bg_scroll", set_bg_scroll },
};
RECOMP_REGISTER(kPpuDma)
