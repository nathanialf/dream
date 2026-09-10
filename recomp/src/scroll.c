/* Per-mode NMI scroll handlers, bank $C0.
 *
 * jtbl_C08272 (docs/naming_proposals.md section 2) has one of these per
 * game_mode, and nmi_handler_gameplay calls it with "jsr (jtbl_C08272,X)" as
 * the first call it makes in vblank. Each one does the same three jobs in the
 * same order: kick the DMA channels the frame's code armed (the 16-bit store to
 * MDMAEN writes HDMAEN as well, the bits the "ora #$3E00" / "#$FE00" adds),
 * upload the two metatile columns that build_metatile_column_500/_580 staged,
 * and then write the BG scroll registers for the mode: straight from the
 * camera in mode 0, through the $C4:6588 sine table for the parallax layers in
 * modes 1 and 2, and into the $0BE0-$0BEE layer-offset words the title's HDMA
 * tables read in mode 3.
 *
 * The scroll registers are write-twice-per-register ports, so the bodies run
 * with an 8-bit accumulator across them and use xba to present the high byte;
 * that half of each routine is 8-bit arithmetic, which dream_alu.h does not
 * cover, so the two 8-bit forms these routines need are here.
 *
 * The two column uploads the jsr pair reaches are converted as well
 * (recomp/src/vram_stream.c), but the call site builds the real jsr frame and
 * hands the CPU the callee rather than calling the C function: the callee's own
 * hook then fires at its own entry address, which is the only way either of
 * them is ever entered, and the routine is left running when the machine moves
 * on underneath, so this hook is not atomic across the two DMA transfers.
 */
#include <stdint.h>
#include <stdbool.h>

#include "snes_state.h"
#include "dream_ram.h"
#include "dream_alu.h"
#include "dream_time.h"

/* ---- 8-bit arithmetic, m = 1 (LakeSnes cpu_adc / cpu_cmp, decimal off) ---- */
static inline uint16_t alu_adc8(SnesState* ss, uint16_t a, uint8_t v) {
  unsigned r = (a & 0xff) + v + (ss_c(ss) ? 1u : 0u);
  ss_set_v(ss, ((a & 0x80) == (v & 0x80)) && ((v & 0x80) != (r & 0x80)));
  ss_set_c(ss, r > 0xff);
  ss_set_nz8(ss, (uint8_t) r);
  return (uint16_t) ((a & 0xff00) | (r & 0xff));
}

static inline void alu_cmp8(SnesState* ss, uint16_t a, uint8_t v) {
  unsigned r = (a & 0xff) + (uint8_t) ~v + 1u;
  ss_set_c(ss, r > 0xff);
  ss_set_nz8(ss, (uint8_t) r);
}

/* ---- instruction shapes dream_time.h does not cover ---------------------- */
/* xba: the flags come from the byte that moves into the low half, and the two
 * internal cycles straddle the interrupt latch (LakeSnes cpu.c case 0xeb). */
static uint16_t t_xba(SnesState* ss, uint16_t a) {
  uint16_t r = (uint16_t) ((a >> 8) | (a << 8));
  ss_set_nz8(ss, (uint8_t) r);
  ss_idle(ss);
  ss_check_int(ss);
  ss_idle(ss);
  return r;
}

/* lsr dp, 16-bit: both bytes read with no latch between them, one internal
 * cycle, then written back high byte first (cpu_lsr with cpu_writeWord
 * reversed). */
static void t_lsr_dp16(SnesState* ss, uint16_t adr) {
  uint8_t lo = ss_bus_r8(ss, adr);
  uint8_t hi = ss_bus_r8(ss, (uint16_t) (adr + 1));
  uint16_t v = (uint16_t) (lo | (hi << 8));
  ss_idle(ss);
  ss_set_c(ss, (v & 1) != 0);
  uint16_t r = (uint16_t) (v >> 1);
  ss_bus_w8(ss, (uint16_t) (adr + 1), (uint8_t) (r >> 8));
  ss_check_int(ss);
  ss_bus_w8(ss, adr, (uint8_t) r);
  ss_set_nz16(ss, r);
}

/* The rest of a "jsr abs": the internal cycle and the return address the opcode
 * pushes (high byte, interrupt latch, low byte), then the callee run on the
 * reference CPU. Any hook the callee has still fires, so a converted callee
 * stays converted and is credited with the call. ss_run_callee stops if the
 * machine moves on underneath, and reports that by returning true: the frame
 * that was pushed is the routine's real return address, so the callee's own rts
 * lands where the ROM expects and the caller can return. Registers are the
 * callee's afterwards, exactly as after the real jsr. */
static bool t_jsr(SnesState* ss, uint8_t pb, uint16_t target) {
  ss_idle(ss);
  const uint16_t sp0 = ss_sp(ss);
  const uint16_t ret = (uint16_t) (ss_pc(ss) - 1);
  ss_push8(ss, (uint8_t) (ret >> 8));
  ss_check_int(ss);
  ss_push8(ss, (uint8_t) ret);
  ss_set_pc(ss, pb, target);
  return ss_run_callee(ss, sp0);
}

/* ---------------------------------------------------------------------------
 * nmi_scroll_mode0: $C0:8E9B
 *
 * BG2 takes the camera directly; BG1 takes the pre-computed offsets the main
 * loop left in $0BE4/$0BE5 and $76 + $0BE6. A pending palette-cycle byte in
 * $0C08 is written to CGRAM through CGADD/CGDATA on the way past. The 16-bit
 * tail republishes the frame's layer offsets ($0BD4-$0BF8) and the three HDMA
 * table words in bank $7F for the next frame.
 * Exit: A = $0C04's value plus nothing (the last 16-bit load), Y unchanged,
 * m = 0.
 * ------------------------------------------------------------------------- */
void nmi_scroll_mode0(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint16_t dp = ss_dp(ss);

  S(0x8E9B, 2);                             /* C08E9B lda dma_pending_mask */
  a = t_read16(ss, (uint16_t) (dp + dma_pending_mask));
  ss_set_nz16(ss, a);
  S(0x8E9D, 3); a = alu_ora16(ss, a, 0x3E00);  /* C08E9D ora #$3E00 */
  S(0x8EA0, 3);                             /* C08EA0 sta MDMAEN (and HDMAEN) */
  t_write16(ss, ss_abs(ss, MDMAEN), a);
  S(0x8EA3, 2);                             /* C08EA3 stz dma_pending_mask */
  t_write16(ss, (uint16_t) (dp + dma_pending_mask), 0);

  S(0x8EA5, 3);                             /* C08EA5 jsr vram_upload_column_580 */
  if(t_jsr(ss, pb, 0xA0F1)) return;
  a = ss_a(ss); x = ss_x(ss); y = ss_y(ss);
  S(0x8EA8, 3);                             /* C08EA8 jsr vram_upload_column_500 */
  if(t_jsr(ss, pb, 0xA148)) return;
  a = ss_a(ss); x = ss_x(ss); y = ss_y(ss);

  S(0x8EAB, 2);                             /* C08EAB lda $76 */
  a = t_read16(ss, (uint16_t) (dp + 0x0076));
  ss_set_nz16(ss, a);
  SI(0x8EAD); ss_set_c(ss, false);          /* C08EAD clc */
  S(0x8EAE, 3);                             /* C08EAE adc $0BE6 */
  a = alu_adc16(ss, a, t_read16(ss, ss_abs(ss, 0x0BE6)));
  S(0x8EB1, 2);                             /* C08EB1 sta ptr_04 */
  t_write16(ss, (uint16_t) (dp + ptr_04), a);
  S(0x8EB3, 2);                             /* C08EB3 lda camera_x */
  a = t_read16(ss, (uint16_t) (dp + camera_x));
  ss_set_nz16(ss, a);

  SEP(0x8EB5, 0x20);                        /* C08EB5 sep #$20 */
  S(0x8EB7, 3);                             /* C08EB7 sta BG2HOFS */
  t_write8(ss, ss_abs(ss, BG2HOFS), (uint8_t) a);
  S(0x8EBA, 1); a = t_xba(ss, a);           /* C08EBA xba */
  S(0x8EBB, 3);                             /* C08EBB sta BG2HOFS */
  t_write8(ss, ss_abs(ss, BG2HOFS), (uint8_t) a);
  S(0x8EBE, 2);                             /* C08EBE lda camera_y */
  { uint8_t b = t_read8(ss, (uint16_t) (dp + camera_y));
    a = (uint16_t) ((a & 0xff00) | b); ss_set_nz8(ss, b); }
  S(0x8EC0, 3);                             /* C08EC0 sta BG2VOFS */
  t_write8(ss, ss_abs(ss, BG2VOFS), (uint8_t) a);
  S(0x8EC3, 3);                             /* C08EC3 sta BG2VOFS */
  t_write8(ss, ss_abs(ss, BG2VOFS), (uint8_t) a);
  S(0x8EC6, 3);                             /* C08EC6 lda $0BE4 */
  { uint8_t b = t_read8(ss, ss_abs(ss, 0x0BE4));
    a = (uint16_t) ((a & 0xff00) | b); ss_set_nz8(ss, b); }
  S(0x8EC9, 3);                             /* C08EC9 sta BG1HOFS */
  t_write8(ss, ss_abs(ss, BG1HOFS), (uint8_t) a);
  S(0x8ECC, 3);                             /* C08ECC lda $0BE5 */
  { uint8_t b = t_read8(ss, ss_abs(ss, 0x0BE5));
    a = (uint16_t) ((a & 0xff00) | b); ss_set_nz8(ss, b); }
  S(0x8ECF, 3);                             /* C08ECF sta BG1HOFS */
  t_write8(ss, ss_abs(ss, BG1HOFS), (uint8_t) a);
  S(0x8ED2, 2);                             /* C08ED2 lda ptr_04 */
  { uint8_t b = t_read8(ss, (uint16_t) (dp + ptr_04));
    a = (uint16_t) ((a & 0xff00) | b); ss_set_nz8(ss, b); }
  S(0x8ED4, 3);                             /* C08ED4 sta BG1VOFS */
  t_write8(ss, ss_abs(ss, BG1VOFS), (uint8_t) a);
  S(0x8ED7, 2);                             /* C08ED7 lda $05 */
  { uint8_t b = t_read8(ss, (uint16_t) (dp + ptr_04 + 1));
    a = (uint16_t) ((a & 0xff00) | b); ss_set_nz8(ss, b); }
  S(0x8ED9, 3);                             /* C08ED9 sta BG1VOFS */
  t_write8(ss, ss_abs(ss, BG1VOFS), (uint8_t) a);

  S(0x8EDC, 3);                             /* C08EDC lda $0C08 */
  { uint8_t b = t_read8(ss, ss_abs(ss, 0x0C08));
    a = (uint16_t) ((a & 0xff00) | b); ss_set_nz8(ss, b); }
  const bool idle_palette = (a & 0xff) == 0;
  S(0x8EDF, 1); t_branch(ss, idle_palette); /* C08EDF beq loc_C08EF6 */
  if(!idle_palette) {
    S(0x8EE1, 3);                           /* C08EE1 sta $0C08 */
    t_write8(ss, ss_abs(ss, 0x0C08), (uint8_t) a);
    S(0x8EE4, 3);                           /* C08EE4 sta CGADD */
    t_write8(ss, ss_abs(ss, CGADD), (uint8_t) a);
    S(0x8EE7, 3);                           /* C08EE7 sta $0C08 */
    t_write8(ss, ss_abs(ss, 0x0C08), (uint8_t) a);
    S(0x8EEA, 3);                           /* C08EEA lda $0C0A */
    { uint8_t b = t_read8(ss, ss_abs(ss, 0x0C0A));
      a = (uint16_t) ((a & 0xff00) | b); ss_set_nz8(ss, b); }
    S(0x8EED, 3);                           /* C08EED sta CGDATA */
    t_write8(ss, ss_abs(ss, CGDATA), (uint8_t) a);
    S(0x8EF0, 3);                           /* C08EF0 lda $0C0B */
    { uint8_t b = t_read8(ss, ss_abs(ss, 0x0C0B));
      a = (uint16_t) ((a & 0xff00) | b); ss_set_nz8(ss, b); }
    S(0x8EF3, 3);                           /* C08EF3 sta CGDATA */
    t_write8(ss, ss_abs(ss, CGDATA), (uint8_t) a);
  }

  /* loc_C08EF6 */
  REP(0x8EF6, 0x20);                        /* C08EF6 rep #$20 */

  static const uint16_t kCopy[7][2] = {     /* C08EF8..C08F21, load/store pairs */
    { 0x0BD8, 0x0BD4 }, { 0x0BDA, 0x0BD6 }, { 0x0BFC, 0x0BF8 },
    { 0x0BF0, 0x0BE8 }, { 0x0BF2, 0x0BEA }, { 0x0BF4, 0x0BEC },
    { 0x0BF6, 0x0BEE },
  };
  for(int i = 0; i < 7; i++) {
    S((uint16_t) (0x8EF8 + i * 6), 3);      /* lda $0Bxx */
    a = t_read16(ss, ss_abs(ss, kCopy[i][0]));
    ss_set_nz16(ss, a);
    S((uint16_t) (0x8EFB + i * 6), 3);      /* sta $0Bxx */
    t_write16(ss, ss_abs(ss, kCopy[i][1]), a);
  }

  S(0x8F22, 3);                             /* C08F22 lda $0C00 */
  a = t_read16(ss, ss_abs(ss, 0x0C00));
  ss_set_nz16(ss, a);
  S(0x8F25, 4); t_write16(ss, 0x7F00CF, a); /* C08F25 sta $7F00CF */
  S(0x8F29, 3);                             /* C08F29 lda $0C02 */
  a = t_read16(ss, ss_abs(ss, 0x0C02));
  ss_set_nz16(ss, a);
  S(0x8F2C, 4); t_write16(ss, 0x7F00D3, a); /* C08F2C sta $7F00D3 */
  S(0x8F30, 3);                             /* C08F30 lda $0C06 */
  a = t_read16(ss, ss_abs(ss, 0x0C06));
  ss_set_nz16(ss, a);
  S(0x8F33, 4); t_write16(ss, 0x7F0541, a); /* C08F33 sta $7F0541 */
  SI(0x8F37); ss_set_c(ss, false);          /* C08F37 clc */
  S(0x8F38, 3); a = alu_adc16(ss, a, 0x00FE);  /* C08F38 adc #$00FE */
  S(0x8F3B, 4); t_write16(ss, 0x7F0544, a); /* C08F3B sta $7F0544 */
  S(0x8F3F, 3);                             /* C08F3F lda $0C04 */
  a = t_read16(ss, ss_abs(ss, 0x0C04));
  ss_set_nz16(ss, a);
  S(0x8F42, 4); t_write16(ss, 0x7F053F, a); /* C08F42 sta $7F053F */

  ss_set_a(ss, a);
  S(0x8F46, 1);                             /* C08F46 rts */
  ss_rts(ss);
}

/* ---------------------------------------------------------------------------
 * nmi_scroll_mode1: $C0:8F47
 *
 * Mode 1's parallax: BG3 gets half the camera X, BG1's vertical offset comes
 * straight from camera_y and its negated form seeds the $7F:0900 HDMA table,
 * and two lookups into the $C4:6588 sine table (docs/data_formats.md, region
 * 046588) produce the wave offsets at $7F:00C4 and $A8. The sign-extending
 * "cmp #$8000 / ror A" pairs are an arithmetic shift right: the compare puts
 * the sign bit into carry and the ror shifts it back in.
 *
 * The $AC,X table (16 words from data_C084B5 plus the camera X) is the
 * per-scanline horizontal offset list the HDMA channel reads.
 * ------------------------------------------------------------------------- */
void nmi_scroll_mode1(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint16_t dp = ss_dp(ss);

  S(0x8F47, 2);                             /* C08F47 lda dma_pending_mask */
  a = t_read16(ss, (uint16_t) (dp + dma_pending_mask));
  ss_set_nz16(ss, a);
  S(0x8F49, 3); a = alu_ora16(ss, a, 0xFE00);  /* C08F49 ora #$FE00 */
  S(0x8F4C, 3);                             /* C08F4C sta MDMAEN (and HDMAEN) */
  t_write16(ss, ss_abs(ss, MDMAEN), a);
  S(0x8F4F, 2);                             /* C08F4F stz dma_pending_mask */
  t_write16(ss, (uint16_t) (dp + dma_pending_mask), 0);

  S(0x8F51, 3);                             /* C08F51 jsr vram_upload_column_580 */
  if(t_jsr(ss, pb, 0xA0F1)) return;
  a = ss_a(ss); x = ss_x(ss); y = ss_y(ss);
  S(0x8F54, 3);                             /* C08F54 jsr vram_upload_column_500 */
  if(t_jsr(ss, pb, 0xA148)) return;
  a = ss_a(ss); x = ss_x(ss); y = ss_y(ss);

  S(0x8F57, 2);                             /* C08F57 lda camera_x */
  a = t_read16(ss, (uint16_t) (dp + camera_x));
  ss_set_nz16(ss, a);
  SI(0x8F59); a = alu_lsr16(ss, a);         /* C08F59 lsr A */
  S(0x8F5A, 2);                             /* C08F5A sta ptr_04 */
  t_write16(ss, (uint16_t) (dp + ptr_04), a);
  S(0x8F5C, 2);                             /* C08F5C lda $60 */
  a = t_read16(ss, (uint16_t) (dp + 0x0060));
  ss_set_nz16(ss, a);
  SI(0x8F5E); a = alu_ror16(ss, a);         /* C08F5E ror A */
  SI(0x8F5F); ss_set_c(ss, false);          /* C08F5F clc */
  S(0x8F60, 2);                             /* C08F60 adc $60 */
  a = alu_adc16(ss, a, t_read16(ss, (uint16_t) (dp + 0x0060)));
  S(0x8F62, 2);                             /* C08F62 lda camera_x */
  a = t_read16(ss, (uint16_t) (dp + camera_x));
  ss_set_nz16(ss, a);
  S(0x8F64, 2);                             /* C08F64 adc ptr_04 */
  a = alu_adc16(ss, a, t_read16(ss, (uint16_t) (dp + ptr_04)));
  S(0x8F66, 4); t_write16(ss, 0x7F0087, a); /* C08F66 sta $7F0087 */
  S(0x8F6A, 2);                             /* C08F6A lda camera_x */
  a = t_read16(ss, (uint16_t) (dp + camera_x));
  ss_set_nz16(ss, a);
  S(0x8F6C, 4); t_write16(ss, 0x7F0081, a); /* C08F6C sta $7F0081 */
  S(0x8F70, 4); t_write16(ss, 0x7F0084, a); /* C08F70 sta $7F0084 */
  SI(0x8F74); a = alu_lsr16(ss, a);         /* C08F74 lsr A */

  SEP(0x8F75, 0x20);                        /* C08F75 sep #$20 */
  S(0x8F77, 3);                             /* C08F77 sta BG3HOFS */
  t_write8(ss, ss_abs(ss, BG3HOFS), (uint8_t) a);
  S(0x8F7A, 3);                             /* C08F7A stz BG3HOFS */
  t_write8(ss, ss_abs(ss, BG3HOFS), 0x00);
  S(0x8F7D, 2);                             /* C08F7D lda camera_y */
  { uint8_t b = t_read8(ss, (uint16_t) (dp + camera_y));
    a = (uint16_t) ((a & 0xff00) | b); ss_set_nz8(ss, b); }
  S(0x8F7F, 3);                             /* C08F7F sta BG1VOFS */
  t_write8(ss, ss_abs(ss, BG1VOFS), (uint8_t) a);
  S(0x8F82, 3);                             /* C08F82 sta BG1VOFS */
  t_write8(ss, ss_abs(ss, BG1VOFS), (uint8_t) a);
  S(0x8F85, 2);                             /* C08F85 eor #$FF */
  a = (uint16_t) ((a & 0xff00) | ((a ^ 0xFF) & 0xff));
  ss_set_nz8(ss, (uint8_t) a);
  SI(0x8F87);                               /* C08F87 inc A */
  a = (uint16_t) ((a & 0xff00) | ((a + 1) & 0xff));
  ss_set_nz8(ss, (uint8_t) a);
  SI(0x8F88); ss_set_c(ss, false);          /* C08F88 clc */
  S(0x8F89, 2); a = alu_adc8(ss, a, 0x50);  /* C08F89 adc #$50 */
  S(0x8F8B, 4);                             /* C08F8B sta $7F0900 */
  t_write8(ss, 0x7F0900, (uint8_t) a);
  S(0x8F8F, 4);                             /* C08F8F sta $7F0083 */
  t_write8(ss, 0x7F0083, (uint8_t) a);
  REP(0x8F93, 0x20);                        /* C08F93 rep #$20 */

  S(0x8F95, 2);                             /* C08F95 lda $5E */
  a = t_read16(ss, (uint16_t) (dp + 0x005E));
  ss_set_nz16(ss, a);
  SI(0x8F97); a = alu_lsr16(ss, a);         /* C08F97 lsr A */
  SI(0x8F98); a = alu_lsr16(ss, a);         /* C08F98 lsr A */
  SI(0x8F99); a = alu_lsr16(ss, a);         /* C08F99 lsr A */
  SI(0x8F9A); ss_set_c(ss, false);          /* C08F9A clc */
  S(0x8F9B, 2);                             /* C08F9B adc $5E */
  a = alu_adc16(ss, a, t_read16(ss, (uint16_t) (dp + 0x005E)));
  S(0x8F9D, 3); a = alu_and16(ss, a, 0x00FF);  /* C08F9D and #$00FF */
  SI(0x8FA0); a = alu_asl16(ss, a);         /* C08FA0 asl A */
  SI(0x8FA1); x = a; ss_set_nz16(ss, x);    /* C08FA1 tax */
  S(0x8FA2, 4);                             /* C08FA2 lda data_C46588,X */
  a = t_read16(ss, (0xC46588u + x) & 0xffffff);
  ss_set_nz16(ss, a);
  for(int i = 0; i < 7; i++) {              /* C08FA6..C08FC1 cmp #$8000 / ror A */
    S((uint16_t) (0x8FA6 + i * 4), 3); alu_cmp16(ss, a, 0x8000);
    SI((uint16_t) (0x8FA9 + i * 4)); a = alu_ror16(ss, a);
  }
  SI(0x8FC2); ss_set_c(ss, false);          /* C08FC2 clc */
  S(0x8FC3, 3); a = alu_adc16(ss, a, 0x0003);  /* C08FC3 adc #$0003 */
  S(0x8FC6, 4); t_write16(ss, 0x7F00C4, a); /* C08FC6 sta $7F00C4 */

  S(0x8FCA, 2);                             /* C08FCA lda $60 */
  a = t_read16(ss, (uint16_t) (dp + 0x0060));
  ss_set_nz16(ss, a);
  SI(0x8FCC); a = alu_asl16(ss, a);         /* C08FCC asl A */
  S(0x8FCD, 2);                             /* C08FCD sta $06 */
  t_write16(ss, (uint16_t) (dp + ptr_04 + 2), a);
  S(0x8FCF, 2);                             /* C08FCF lda camera_x */
  a = t_read16(ss, (uint16_t) (dp + camera_x));
  ss_set_nz16(ss, a);
  SI(0x8FD1); a = alu_rol16(ss, a);         /* C08FD1 rol A */
  S(0x8FD2, 2);                             /* C08FD2 sta $08 */
  t_write16(ss, (uint16_t) (dp + ptr_04 + 4), a);
  S(0x8FD4, 2);                             /* C08FD4 lda $5E */
  a = t_read16(ss, (uint16_t) (dp + 0x005E));
  ss_set_nz16(ss, a);
  S(0x8FD6, 3); a = alu_and16(ss, a, 0x00FF);  /* C08FD6 and #$00FF */
  SI(0x8FD9); a = alu_asl16(ss, a);         /* C08FD9 asl A */
  SI(0x8FDA); x = a; ss_set_nz16(ss, x);    /* C08FDA tax */
  S(0x8FDB, 4);                             /* C08FDB lda data_C46588,X */
  a = t_read16(ss, (0xC46588u + x) & 0xffffff);
  ss_set_nz16(ss, a);
  S(0x8FDF, 2);                             /* C08FDF sta ptr_04 */
  t_write16(ss, (uint16_t) (dp + ptr_04), a);
  S(0x8FE1, 3); a = 0x0000; ss_set_nz16(ss, a);  /* C08FE1 lda #$0000 */
  for(int i = 0; i < 5; i++) {              /* C08FE4..C08FF2 lsr ptr_04 / ror A */
    S((uint16_t) (0x8FE4 + i * 3), 2);
    t_lsr_dp16(ss, (uint16_t) (dp + ptr_04));
    SI((uint16_t) (0x8FE6 + i * 3)); a = alu_ror16(ss, a);
  }
  SI(0x8FF3); ss_set_c(ss, false);          /* C08FF3 clc */
  S(0x8FF4, 2);                             /* C08FF4 adc $06 */
  a = alu_adc16(ss, a, t_read16(ss, (uint16_t) (dp + ptr_04 + 2)));
  S(0x8FF6, 2);                             /* C08FF6 lda ptr_04 */
  a = t_read16(ss, (uint16_t) (dp + ptr_04));
  ss_set_nz16(ss, a);
  S(0x8FF8, 2);                             /* C08FF8 adc $08 */
  a = alu_adc16(ss, a, t_read16(ss, (uint16_t) (dp + ptr_04 + 4)));
  S(0x8FFA, 2);                             /* C08FFA sta $A8 */
  t_write16(ss, (uint16_t) (dp + 0x00A8), a);
  S(0x8FFC, 3); x = 0x0000; ss_set_nz16(ss, x);  /* C08FFC ldx #$0000 */

  do {
    S(0x8FFF, 4);                           /* C08FFF lda data_C084B5,X */
    a = t_read16(ss, (0x8084B5u + x) & 0xffffff);
    ss_set_nz16(ss, a);
    SI(0x9003); ss_set_c(ss, false);        /* C09003 clc */
    S(0x9004, 2);                           /* C09004 adc camera_x */
    a = alu_adc16(ss, a, t_read16(ss, (uint16_t) (dp + camera_x)));
    S(0x9006, 2); t_index(ss);              /* C09006 sta $AC,X */
    t_write16(ss, (uint16_t) (dp + 0x00AC + x), a);
    SI(0x9008); x = (uint16_t) (x + 1); ss_set_nz16(ss, x);  /* C09008 inx */
    SI(0x9009); x = (uint16_t) (x + 1); ss_set_nz16(ss, x);  /* C09009 inx */
    S(0x900A, 3); alu_cpx16(ss, x, 0x0020); /* C0900A cpx #$0020 */
    S(0x900D, 1); t_branch(ss, x != 0x0020);/* C0900D bne loc_C08FFF */
  } while(x != 0x0020);

  S(0x900F, 2);                             /* C0900F lda $5E */
  a = t_read16(ss, (uint16_t) (dp + 0x005E));
  ss_set_nz16(ss, a);
  SI(0x9011); a = alu_lsr16(ss, a);         /* C09011 lsr A */
  SI(0x9012); a = alu_lsr16(ss, a);         /* C09012 lsr A */
  S(0x9013, 3); a = alu_and16(ss, a, 0x000F);  /* C09013 and #$000F */
  SI(0x9016); ss_set_c(ss, true);           /* C09016 sec */
  S(0x9017, 3); a = alu_adc16(ss, a, 0x0080);  /* C09017 adc #$0080 */
  SEP(0x901A, 0x20);                        /* C0901A sep #$20 */
  S(0x901C, 4);                             /* C0901C sta $7F0003 */
  t_write8(ss, 0x7F0003, (uint8_t) a);
  REP(0x9020, 0x20);                        /* C09020 rep #$20 */

  S(0x9022, 2);                             /* C09022 lda $5E */
  a = t_read16(ss, (uint16_t) (dp + 0x005E));
  ss_set_nz16(ss, a);
  SI(0x9024); a = alu_lsr16(ss, a);         /* C09024 lsr A */
  SI(0x9025); a = alu_lsr16(ss, a);         /* C09025 lsr A */
  S(0x9026, 3); a = alu_eor16(ss, a, 0x000F);  /* C09026 eor #$000F */
  S(0x9029, 3); a = alu_and16(ss, a, 0x000F);  /* C09029 and #$000F */
  S(0x902C, 2);                             /* C0902C sta ptr_04 */
  t_write16(ss, (uint16_t) (dp + ptr_04), a);
  SI(0x902E); ss_set_c(ss, true);           /* C0902E sec */
  S(0x902F, 3); a = alu_adc16(ss, a, 0x0080);  /* C0902F adc #$0080 */
  SEP(0x9032, 0x20);                        /* C09032 sep #$20 */
  S(0x9034, 4);                             /* C09034 sta $7F0043 */
  t_write8(ss, 0x7F0043, (uint8_t) a);
  REP(0x9038, 0x20);                        /* C09038 rep #$20 */

  S(0x903A, 2);                             /* C0903A lda ptr_04 */
  a = t_read16(ss, (uint16_t) (dp + ptr_04));
  ss_set_nz16(ss, a);
  S(0x903C, 3); a = alu_eor16(ss, a, 0x000F);  /* C0903C eor #$000F */
  SI(0x903F); a = alu_asl16(ss, a);         /* C0903F asl A */
  SI(0x9040); ss_set_c(ss, false);          /* C09040 clc */
  S(0x9041, 3); a = alu_adc16(ss, a, 0x84B5);  /* C09041 adc #$84B5 */
  S(0x9044, 4); t_write16(ss, 0x7F0044, a); /* C09044 sta $7F0044 */

  ss_set_a(ss, a);
  ss_set_x(ss, x);
  S(0x9048, 1);                             /* C09048 rts */
  ss_rts(ss);
}

/* ---------------------------------------------------------------------------
 * nmi_scroll_mode2: $C0:9049
 *
 * BG2 follows the camera (Y one line up, as $04 = camera_y - 1); BG3 takes the
 * $C4:6588 wave, scaled down by five sign-extending shifts, added to the camera
 * X, and its vertical offset is the negated wave plus $04. BG1's vertical
 * offset is that sum clamped to $0000..$006F (below zero it becomes $FFFF), and
 * its horizontal offset is 1.5 camera X plus half the wave.
 * ------------------------------------------------------------------------- */
void nmi_scroll_mode2(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint16_t dp = ss_dp(ss);

  S(0x9049, 2);                             /* C09049 lda dma_pending_mask */
  a = t_read16(ss, (uint16_t) (dp + dma_pending_mask));
  ss_set_nz16(ss, a);
  S(0x904B, 3);                             /* C0904B sta MDMAEN (and HDMAEN) */
  t_write16(ss, ss_abs(ss, MDMAEN), a);
  S(0x904E, 2);                             /* C0904E stz dma_pending_mask */
  t_write16(ss, (uint16_t) (dp + dma_pending_mask), 0);

  S(0x9050, 3);                             /* C09050 jsr vram_upload_column_580 */
  if(t_jsr(ss, pb, 0xA0F1)) return;
  a = ss_a(ss); x = ss_x(ss); y = ss_y(ss);
  S(0x9053, 3);                             /* C09053 jsr vram_upload_column_500 */
  if(t_jsr(ss, pb, 0xA148)) return;
  a = ss_a(ss); x = ss_x(ss); y = ss_y(ss);

  S(0x9056, 2);                             /* C09056 lda camera_x */
  a = t_read16(ss, (uint16_t) (dp + camera_x));
  ss_set_nz16(ss, a);
  S(0x9058, 2);                             /* C09058 ldy camera_y */
  y = t_read16(ss, (uint16_t) (dp + camera_y));
  ss_set_nz16(ss, y);
  SI(0x905A); y = (uint16_t) (y - 1); ss_set_nz16(ss, y);  /* C0905A dey */
  S(0x905B, 2);                             /* C0905B sty ptr_04 */
  t_write16(ss, (uint16_t) (dp + ptr_04), y);

  SEP(0x905D, 0x20);                        /* C0905D sep #$20 */
  S(0x905F, 3);                             /* C0905F sta BG2HOFS */
  t_write8(ss, ss_abs(ss, BG2HOFS), (uint8_t) a);
  S(0x9062, 1); a = t_xba(ss, a);           /* C09062 xba */
  S(0x9063, 3);                             /* C09063 sta BG2HOFS */
  t_write8(ss, ss_abs(ss, BG2HOFS), (uint8_t) a);
  S(0x9066, 1); a = t_xba(ss, a);           /* C09066 xba */
  SI(0x9067);                               /* C09067 tya */
  a = (uint16_t) ((a & 0xff00) | (y & 0xff));
  ss_set_nz8(ss, (uint8_t) y);
  S(0x9068, 3);                             /* C09068 sta BG2VOFS */
  t_write8(ss, ss_abs(ss, BG2VOFS), (uint8_t) a);
  S(0x906B, 1); a = t_xba(ss, a);           /* C0906B xba */
  S(0x906C, 3);                             /* C0906C sta BG2VOFS */
  t_write8(ss, ss_abs(ss, BG2VOFS), (uint8_t) a);
  REP(0x906F, 0x20);                        /* C0906F rep #$20 */

  S(0x9071, 2);                             /* C09071 lda $5E */
  a = t_read16(ss, (uint16_t) (dp + 0x005E));
  ss_set_nz16(ss, a);
  S(0x9073, 3); a = alu_and16(ss, a, 0x00FF);  /* C09073 and #$00FF */
  SI(0x9076); a = alu_asl16(ss, a);         /* C09076 asl A */
  SI(0x9077); x = a; ss_set_nz16(ss, x);    /* C09077 tax */
  S(0x9078, 4);                             /* C09078 lda data_C46588,X */
  a = t_read16(ss, (0xC46588u + x) & 0xffffff);
  ss_set_nz16(ss, a);
  for(int i = 0; i < 5; i++) {              /* C0907C..C0908F cmp #$8000 / ror A */
    S((uint16_t) (0x907C + i * 4), 3); alu_cmp16(ss, a, 0x8000);
    SI((uint16_t) (0x907F + i * 4)); a = alu_ror16(ss, a);
  }
  S(0x9090, 2);                             /* C09090 sta $06 */
  t_write16(ss, (uint16_t) (dp + ptr_04 + 2), a);
  SI(0x9092); ss_set_c(ss, false);          /* C09092 clc */
  S(0x9093, 2);                             /* C09093 adc camera_x */
  a = alu_adc16(ss, a, t_read16(ss, (uint16_t) (dp + camera_x)));

  SEP(0x9095, 0x20);                        /* C09095 sep #$20 */
  S(0x9097, 3);                             /* C09097 sta BG3HOFS */
  t_write8(ss, ss_abs(ss, BG3HOFS), (uint8_t) a);
  S(0x909A, 1); a = t_xba(ss, a);           /* C0909A xba */
  S(0x909B, 3);                             /* C0909B sta BG3HOFS */
  t_write8(ss, ss_abs(ss, BG3HOFS), (uint8_t) a);
  REP(0x909E, 0x20);                        /* C0909E rep #$20 */

  S(0x90A0, 2);                             /* C090A0 lda $06 */
  a = t_read16(ss, (uint16_t) (dp + ptr_04 + 2));
  ss_set_nz16(ss, a);
  S(0x90A2, 3); alu_cmp16(ss, a, 0x8000);   /* C090A2 cmp #$8000 */
  SI(0x90A5); a = alu_ror16(ss, a);         /* C090A5 ror A */
  S(0x90A6, 3); a = alu_eor16(ss, a, 0xFFFF);  /* C090A6 eor #$FFFF */
  SI(0x90A9); a = alu_inc16(ss, a);         /* C090A9 inc A */
  S(0x90AA, 2);                             /* C090AA sta $08 */
  t_write16(ss, (uint16_t) (dp + ptr_04 + 4), a);
  SI(0x90AC); ss_set_c(ss, false);          /* C090AC clc */
  S(0x90AD, 2);                             /* C090AD adc ptr_04 */
  a = alu_adc16(ss, a, t_read16(ss, (uint16_t) (dp + ptr_04)));

  SEP(0x90AF, 0x20);                        /* C090AF sep #$20 */
  S(0x90B1, 3);                             /* C090B1 sta BG3VOFS */
  t_write8(ss, ss_abs(ss, BG3VOFS), (uint8_t) a);
  S(0x90B4, 1); a = t_xba(ss, a);           /* C090B4 xba */
  S(0x90B5, 3);                             /* C090B5 sta BG3VOFS */
  t_write8(ss, ss_abs(ss, BG3VOFS), (uint8_t) a);
  REP(0x90B8, 0x20);                        /* C090B8 rep #$20 */

  S(0x90BA, 2);                             /* C090BA lda ptr_04 */
  a = t_read16(ss, (uint16_t) (dp + ptr_04));
  ss_set_nz16(ss, a);
  SI(0x90BC); ss_set_c(ss, true);           /* C090BC sec */
  S(0x90BD, 3); a = alu_sbc16(ss, a, 0x0010);  /* C090BD sbc #$0010 */
  SI(0x90C0); ss_set_c(ss, false);          /* C090C0 clc */
  S(0x90C1, 2);                             /* C090C1 adc $08 */
  a = alu_adc16(ss, a, t_read16(ss, (uint16_t) (dp + ptr_04 + 4)));
  const bool positive = (a & 0x8000) == 0;
  S(0x90C3, 1); t_branch(ss, positive);     /* C090C3 bpl loc_C090CA */
  if(!positive) {
    S(0x90C5, 3); a = 0xFFFF; ss_set_nz16(ss, a);  /* C090C5 lda #$FFFF */
    S(0x90C8, 1); t_branch(ss, true);       /* C090C8 bra loc_C090D2 */
  } else {
    /* loc_C090CA */
    S(0x90CA, 3); alu_cmp16(ss, a, 0x0070); /* C090CA cmp #$0070 */
    const bool below = !ss_c(ss);
    S(0x90CD, 1); t_branch(ss, below);      /* C090CD bcc loc_C090D2 */
    if(!below) {
      S(0x90CF, 3); a = 0x0070; ss_set_nz16(ss, a);  /* C090CF lda #$0070 */
    }
  }

  /* loc_C090D2 */
  SEP(0x90D2, 0x20);                        /* C090D2 sep #$20 */
  S(0x90D4, 3);                             /* C090D4 sta BG1VOFS */
  t_write8(ss, ss_abs(ss, BG1VOFS), (uint8_t) a);
  S(0x90D7, 1); a = t_xba(ss, a);           /* C090D7 xba */
  S(0x90D8, 3);                             /* C090D8 sta BG1VOFS */
  t_write8(ss, ss_abs(ss, BG1VOFS), (uint8_t) a);
  REP(0x90DB, 0x20);                        /* C090DB rep #$20 */

  S(0x90DD, 2);                             /* C090DD lda $06 */
  a = t_read16(ss, (uint16_t) (dp + ptr_04 + 2));
  ss_set_nz16(ss, a);
  S(0x90DF, 3); alu_cmp16(ss, a, 0x8000);   /* C090DF cmp #$8000 */
  SI(0x90E2); a = alu_ror16(ss, a);         /* C090E2 ror A */
  S(0x90E3, 2);                             /* C090E3 sta $06 */
  t_write16(ss, (uint16_t) (dp + ptr_04 + 2), a);
  S(0x90E5, 2);                             /* C090E5 lda camera_x */
  a = t_read16(ss, (uint16_t) (dp + camera_x));
  ss_set_nz16(ss, a);
  SI(0x90E7); a = alu_lsr16(ss, a);         /* C090E7 lsr A */
  SI(0x90E8); ss_set_c(ss, false);          /* C090E8 clc */
  S(0x90E9, 2);                             /* C090E9 adc camera_x */
  a = alu_adc16(ss, a, t_read16(ss, (uint16_t) (dp + camera_x)));
  SI(0x90EB); ss_set_c(ss, false);          /* C090EB clc */
  S(0x90EC, 2);                             /* C090EC adc $06 */
  a = alu_adc16(ss, a, t_read16(ss, (uint16_t) (dp + ptr_04 + 2)));

  SEP(0x90EE, 0x20);                        /* C090EE sep #$20 */
  S(0x90F0, 3);                             /* C090F0 sta BG1HOFS */
  t_write8(ss, ss_abs(ss, BG1HOFS), (uint8_t) a);
  S(0x90F3, 1); a = t_xba(ss, a);           /* C090F3 xba */
  S(0x90F4, 3);                             /* C090F4 sta BG1HOFS */
  t_write8(ss, ss_abs(ss, BG1HOFS), (uint8_t) a);
  REP(0x90F7, 0x20);                        /* C090F7 rep #$20 */

  ss_set_a(ss, a);
  ss_set_x(ss, x);
  ss_set_y(ss, y);
  S(0x90F9, 1);                             /* C090F9 rts */
  ss_rts(ss);
}

/* ---------------------------------------------------------------------------
 * nmi_scroll_title: $C0:90FA
 *
 * BG2 follows the camera like mode 2's; the rest of the routine computes the
 * title screen's layer offsets into $0BDC-$0BEE, which the HDMA tables set up
 * by title_screen_init read, plus the two brightness/parallax bytes at
 * $7F:00D0 and $7F:00D3. The two clamps at the end are the ROM's own: the
 * vertical offset is held between $FF92 and 0, and the byte at $7F:00D3
 * between $38 and $7F.
 * ------------------------------------------------------------------------- */
void nmi_scroll_title(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint16_t dp = ss_dp(ss);

  S(0x90FA, 2);                             /* C090FA lda dma_pending_mask */
  a = t_read16(ss, (uint16_t) (dp + dma_pending_mask));
  ss_set_nz16(ss, a);
  S(0x90FC, 3); a = alu_ora16(ss, a, 0x3E00);  /* C090FC ora #$3E00 */
  S(0x90FF, 3);                             /* C090FF sta MDMAEN (and HDMAEN) */
  t_write16(ss, ss_abs(ss, MDMAEN), a);
  S(0x9102, 2);                             /* C09102 stz dma_pending_mask */
  t_write16(ss, (uint16_t) (dp + dma_pending_mask), 0);

  S(0x9104, 3);                             /* C09104 jsr vram_upload_column_580 */
  if(t_jsr(ss, pb, 0xA0F1)) return;
  a = ss_a(ss); x = ss_x(ss); y = ss_y(ss);
  S(0x9107, 3);                             /* C09107 jsr vram_upload_column_500 */
  if(t_jsr(ss, pb, 0xA148)) return;
  a = ss_a(ss); x = ss_x(ss); y = ss_y(ss);

  S(0x910A, 2);                             /* C0910A lda camera_x */
  a = t_read16(ss, (uint16_t) (dp + camera_x));
  ss_set_nz16(ss, a);
  S(0x910C, 2);                             /* C0910C ldy camera_y */
  y = t_read16(ss, (uint16_t) (dp + camera_y));
  ss_set_nz16(ss, y);
  SI(0x910E); y = (uint16_t) (y - 1); ss_set_nz16(ss, y);  /* C0910E dey */
  S(0x910F, 2);                             /* C0910F sty ptr_04 */
  t_write16(ss, (uint16_t) (dp + ptr_04), y);

  SEP(0x9111, 0x20);                        /* C09111 sep #$20 */
  S(0x9113, 3);                             /* C09113 sta BG2HOFS */
  t_write8(ss, ss_abs(ss, BG2HOFS), (uint8_t) a);
  S(0x9116, 1); a = t_xba(ss, a);           /* C09116 xba */
  S(0x9117, 3);                             /* C09117 sta BG2HOFS */
  t_write8(ss, ss_abs(ss, BG2HOFS), (uint8_t) a);
  S(0x911A, 1); a = t_xba(ss, a);           /* C0911A xba */
  SI(0x911B);                               /* C0911B tya */
  a = (uint16_t) ((a & 0xff00) | (y & 0xff));
  ss_set_nz8(ss, (uint8_t) y);
  S(0x911C, 3);                             /* C0911C sta BG2VOFS */
  t_write8(ss, ss_abs(ss, BG2VOFS), (uint8_t) a);
  S(0x911F, 1); a = t_xba(ss, a);           /* C0911F xba */
  S(0x9120, 3);                             /* C09120 sta BG2VOFS */
  t_write8(ss, ss_abs(ss, BG2VOFS), (uint8_t) a);
  REP(0x9123, 0x20);                        /* C09123 rep #$20 */

  S(0x9125, 2);                             /* C09125 lda camera_x */
  a = t_read16(ss, (uint16_t) (dp + camera_x));
  ss_set_nz16(ss, a);
  SI(0x9127); a = alu_lsr16(ss, a);         /* C09127 lsr A */
  S(0x9128, 3); t_write16(ss, ss_abs(ss, 0x0BE8), a);   /* C09128 sta $0BE8 */
  S(0x912B, 2);                             /* C0912B lda camera_y */
  a = t_read16(ss, (uint16_t) (dp + camera_y));
  ss_set_nz16(ss, a);
  SI(0x912D); a = alu_lsr16(ss, a);         /* C0912D lsr A */
  S(0x912E, 3); t_write16(ss, ss_abs(ss, 0x0BEA), a);   /* C0912E sta $0BEA */
  S(0x9131, 2);                             /* C09131 lda $5E */
  a = t_read16(ss, (uint16_t) (dp + 0x005E));
  ss_set_nz16(ss, a);
  SI(0x9133); a = alu_lsr16(ss, a);         /* C09133 lsr A */
  SI(0x9134); a = alu_lsr16(ss, a);         /* C09134 lsr A */
  SI(0x9135); a = alu_lsr16(ss, a);         /* C09135 lsr A */
  SI(0x9136); ss_set_c(ss, false);          /* C09136 clc */
  S(0x9137, 2);                             /* C09137 adc camera_x */
  a = alu_adc16(ss, a, t_read16(ss, (uint16_t) (dp + camera_x)));
  S(0x9139, 3); t_write16(ss, ss_abs(ss, 0x0BEC), a);   /* C09139 sta $0BEC */
  S(0x913C, 2);                             /* C0913C lda camera_y */
  a = t_read16(ss, (uint16_t) (dp + camera_y));
  ss_set_nz16(ss, a);
  SI(0x913E); a = alu_lsr16(ss, a);         /* C0913E lsr A */
  S(0x913F, 2);                             /* C0913F sta $18 */
  t_write16(ss, (uint16_t) (dp + scratch_18), a);
  SI(0x9141); a = alu_lsr16(ss, a);         /* C09141 lsr A */
  SI(0x9142); ss_set_c(ss, false);          /* C09142 clc */
  S(0x9143, 2);                             /* C09143 adc $18 */
  a = alu_adc16(ss, a, t_read16(ss, (uint16_t) (dp + scratch_18)));
  SI(0x9145); ss_set_c(ss, true);           /* C09145 sec */
  S(0x9146, 3); a = alu_sbc16(ss, a, 0x006C);  /* C09146 sbc #$006C */
  S(0x9149, 3); t_write16(ss, ss_abs(ss, 0x0BEE), a);   /* C09149 sta $0BEE */
  S(0x914C, 2);                             /* C0914C lda camera_x */
  a = t_read16(ss, (uint16_t) (dp + camera_x));
  ss_set_nz16(ss, a);
  SI(0x914E); a = alu_lsr16(ss, a);         /* C0914E lsr A */
  SI(0x914F); ss_set_c(ss, false);          /* C0914F clc */
  S(0x9150, 2);                             /* C09150 adc camera_x */
  a = alu_adc16(ss, a, t_read16(ss, (uint16_t) (dp + camera_x)));
  S(0x9152, 3); t_write16(ss, ss_abs(ss, 0x0BE0), a);   /* C09152 sta $0BE0 */
  S(0x9155, 2);                             /* C09155 lda camera_x */
  a = t_read16(ss, (uint16_t) (dp + camera_x));
  ss_set_nz16(ss, a);
  SI(0x9157); a = alu_asl16(ss, a);         /* C09157 asl A */
  S(0x9158, 3); t_write16(ss, ss_abs(ss, 0x0BDC), a);   /* C09158 sta $0BDC */
  S(0x915B, 2);                             /* C0915B lda camera_y */
  a = t_read16(ss, (uint16_t) (dp + camera_y));
  ss_set_nz16(ss, a);
  SI(0x915D); ss_set_c(ss, true);           /* C0915D sec */
  S(0x915E, 3); a = alu_sbc16(ss, a, 0x0030);  /* C0915E sbc #$0030 */
  SI(0x9161); a = alu_asl16(ss, a);         /* C09161 asl A */
  const bool above = (a & 0x8000) == 0;
  S(0x9162, 1); t_branch(ss, above);        /* C09162 bpl loc_C09165 */
  if(!above) {
    SI(0x9164); a = ss_dp(ss); ss_set_nz16(ss, a);  /* C09164 tdc */
  }
  /* loc_C09165 */
  S(0x9165, 3); alu_cmp16(ss, a, 0x0040);   /* C09165 cmp #$0040 */
  const bool small = !ss_c(ss);
  S(0x9168, 1); t_branch(ss, small);        /* C09168 bcc loc_C0916D */
  if(!small) {
    S(0x916A, 3); a = 0x003F; ss_set_nz16(ss, a);   /* C0916A lda #$003F */
  }
  /* loc_C0916D */
  S(0x916D, 3); t_write16(ss, ss_abs(ss, 0x0BDE), a);   /* C0916D sta $0BDE */

  SEP(0x9170, 0x20);                        /* C09170 sep #$20 */
  S(0x9172, 2); alu_cmp8(ss, a, 0x08);      /* C09172 cmp #$08 */
  const bool dim = !ss_c(ss);
  S(0x9174, 1); t_branch(ss, dim);          /* C09174 bcc loc_C09178 */
  if(!dim) {
    S(0x9176, 2);                           /* C09176 lda #$08 */
    a = (uint16_t) ((a & 0xff00) | 0x08);
    ss_set_nz8(ss, 0x08);
  }
  /* loc_C09178 */
  S(0x9178, 2);                             /* C09178 sta $18 */
  t_write8(ss, (uint16_t) (dp + scratch_18), (uint8_t) a);
  S(0x917A, 2);                             /* C0917A eor #$3F */
  a = (uint16_t) ((a & 0xff00) | ((a ^ 0x3F) & 0xff));
  ss_set_nz8(ss, (uint8_t) a);
  SI(0x917C);                               /* C0917C inc A */
  a = (uint16_t) ((a & 0xff00) | ((a + 1) & 0xff));
  ss_set_nz8(ss, (uint8_t) a);
  S(0x917D, 4);                             /* C0917D sta $7F00D0 */
  t_write8(ss, 0x7F00D0, (uint8_t) a);
  REP(0x9181, 0x20);                        /* C09181 rep #$20 */

  S(0x9183, 2);                             /* C09183 lda camera_y */
  a = t_read16(ss, (uint16_t) (dp + camera_y));
  ss_set_nz16(ss, a);
  SI(0x9185); ss_set_c(ss, true);           /* C09185 sec */
  S(0x9186, 3); a = alu_sbc16(ss, a, 0x0091);  /* C09186 sbc #$0091 */
  const bool negative = (a & 0x8000) != 0;
  S(0x9189, 1); t_branch(ss, negative);     /* C09189 bmi loc_C09190 */
  if(!negative) {
    const bool zero = a == 0;
    S(0x918B, 1); t_branch(ss, zero);       /* C0918B beq loc_C09198 */
    if(!zero) {
      SI(0x918D); a = ss_dp(ss); ss_set_nz16(ss, a);  /* C0918D tdc */
      S(0x918E, 1); t_branch(ss, true);     /* C0918E bra loc_C09198 */
    }
  } else {
    /* loc_C09190 */
    S(0x9190, 3); alu_cmp16(ss, a, 0xFF92); /* C09190 cmp #$FF92 */
    const bool inRange = ss_c(ss);
    S(0x9193, 1); t_branch(ss, inRange);    /* C09193 bcs loc_C09198 */
    if(!inRange) {
      S(0x9195, 3); a = 0xFF92; ss_set_nz16(ss, a);  /* C09195 lda #$FF92 */
    }
  }

  /* loc_C09198 */
  SI(0x9198); a = alu_dec16(ss, a);         /* C09198 dec A */
  S(0x9199, 3); t_write16(ss, ss_abs(ss, 0x0BE2), a);   /* C09199 sta $0BE2 */
  S(0x919C, 3); a = alu_eor16(ss, a, 0xFFFF);  /* C0919C eor #$FFFF */
  SI(0x919F); a = alu_inc16(ss, a);         /* C0919F inc A */

  SEP(0x91A0, 0x20);                        /* C091A0 sep #$20 */
  SI(0x91A2); ss_set_c(ss, false);          /* C091A2 clc */
  S(0x91A3, 2); a = alu_adc8(ss, a, 0x38);  /* C091A3 adc #$38 */
  SI(0x91A5); ss_set_c(ss, false);          /* C091A5 clc */
  S(0x91A6, 2);                             /* C091A6 adc $18 */
  a = alu_adc8(ss, a, t_read8(ss, (uint16_t) (dp + scratch_18)));
  S(0x91A8, 2); alu_cmp8(ss, a, 0x80);      /* C091A8 cmp #$80 */
  const bool under = !ss_c(ss);
  S(0x91AA, 1); t_branch(ss, under);        /* C091AA bcc loc_C091AE */
  if(!under) {
    S(0x91AC, 2);                           /* C091AC lda #$7F */
    a = (uint16_t) ((a & 0xff00) | 0x7F);
    ss_set_nz8(ss, 0x7F);
  }
  /* loc_C091AE */
  S(0x91AE, 2); alu_cmp8(ss, a, 0x38);      /* C091AE cmp #$38 */
  const bool over = ss_c(ss);
  S(0x91B0, 1); t_branch(ss, over);         /* C091B0 bcs loc_C091B4 */
  if(!over) {
    S(0x91B2, 2);                           /* C091B2 lda #$38 */
    a = (uint16_t) ((a & 0xff00) | 0x38);
    ss_set_nz8(ss, 0x38);
  }
  /* loc_C091B4 */
  S(0x91B4, 4);                             /* C091B4 sta $7F00D3 */
  t_write8(ss, 0x7F00D3, (uint8_t) a);
  REP(0x91B8, 0x20);                        /* C091B8 rep #$20 */

  ss_set_a(ss, a);
  ss_set_x(ss, x);
  ss_set_y(ss, y);
  S(0x91BA, 1);                             /* C091BA rts */
  ss_rts(ss);
}

static const RecompEntry kScroll[] = {
  { 0xc08e9b, "nmi_scroll_mode0", nmi_scroll_mode0 },
  { 0xc08f47, "nmi_scroll_mode1", nmi_scroll_mode1 },
  { 0xc09049, "nmi_scroll_mode2", nmi_scroll_mode2 },
  { 0xc090fa, "nmi_scroll_title", nmi_scroll_title },
};
RECOMP_REGISTER(kScroll)
