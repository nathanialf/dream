/* Background streaming: metatile column build, column DMA, tile generation and
 * the VRAM/CGRAM streaming-descriptor dispatcher, bank $C0.
 *
 * The two build_metatile_column_* routines are the metatile blitter documented
 * in docs/data_formats.md: the map at [$18] (base $7A/$7C plus the camera) holds
 * one 16-bit metatile index per cell, bits 15 and 14 selecting a vertical and a
 * horizontal flip, and the definition table at $7E holds the tile words. A cell
 * is expanded into four words in the $06C0 scratch buffer, in map order for the
 * $0500 (column-major, 8-word stride) build and column-major for the $0580 one,
 * with the flip applied by eor #$4000 / #$8000 and by reading the four words
 * backwards. The tail of each routine copies the scratch buffer into the
 * $0500 / $0580 staging buffer with the wrap the tilemap's 32-entry ring needs,
 * and vram_upload_column_500 / _580 DMA that buffer into VRAM in the next NMI.
 *
 * vram_stream_descriptor_dispatch reads the per-mode descriptor index at
 * $80:B24C keyed by the camera, and on a change starts the streaming descriptor
 * at $80:B208 (8 bytes: VRAM address, source, bank/flag, size) either as a
 * multi-frame VRAM stream ($0BC8/$0BCA) or as one queued CGRAM record for
 * cgram_upload_queue_flush.
 *
 * Everything runs with a 16-bit accumulator and 16-bit index registers except
 * the sep-bracketed register stores, exactly as out/dream.asm shows. The DMA
 * register traffic goes through the timed accessors, so arming MDMAEN performs
 * the transfer on the bus cycles of the instructions that follow the store, the
 * ones the ROM spends there too.
 */
#include <stdint.h>
#include <stdbool.h>

#include "snes_state.h"
#include "dream_ram.h"
#include "dream_alu.h"
#include "dream_time.h"

/* Converted elsewhere in the port; these are called as C, not run on the
 * reference CPU, so a hook table narrowed with --only still runs them. */
void dma_upload_to_vram(SnesState* ss);      /* recomp/src/ppu_dma.c, $C0:A46A */

/* ---------------------------------------------------------------------------
 * Instruction shapes dream_time.h does not cover, written against LakeSnes'
 * own implementations (cpu.c cases 0xeb, 0x66, 0xa7, 0x20).
 * ------------------------------------------------------------------------- */

/* xba: the flags come from the byte that moves into the low half, and the two
 * internal cycles straddle the interrupt latch. */
static uint16_t t_xba(SnesState* ss, uint16_t a) {
  uint16_t r = (uint16_t) ((a >> 8) | (a << 8));
  ss_set_nz8(ss, (uint8_t) r);
  ss_idle(ss);
  ss_check_int(ss);
  ss_idle(ss);
  return r;
}

/* ror dp, 16-bit: read both bytes with no latch between them, one internal
 * cycle, then write them back high byte first (cpu_writeWord reversed). */
static void t_ror_dp16(SnesState* ss, uint16_t adr) {
  uint8_t lo = ss_bus_r8(ss, adr);
  uint8_t hi = ss_bus_r8(ss, (uint16_t) (adr + 1));
  uint16_t v = (uint16_t) (lo | (hi << 8));
  ss_idle(ss);
  bool carry = (v & 1) != 0;
  uint16_t r = (uint16_t) ((v >> 1) | (ss_c(ss) ? 0x8000u : 0u));
  ss_bus_w8(ss, (uint16_t) (adr + 1), (uint8_t) (r >> 8));
  ss_check_int(ss);
  ss_bus_w8(ss, adr, (uint8_t) r);
  ss_set_nz16(ss, r);
  ss_set_c(ss, carry);
}

/* the pointer half of "lda [dp]": three bus reads of the 24-bit pointer, with
 * no interrupt latch between them (cpu_adrIdl) */
static uint32_t t_idl_ptr(SnesState* ss, uint16_t adr) {
  uint8_t lo = ss_bus_r8(ss, adr);
  uint8_t hi = ss_bus_r8(ss, (uint16_t) (adr + 1));
  uint8_t bk = ss_bus_r8(ss, (uint16_t) (adr + 2));
  return (uint32_t) (lo | (hi << 8) | (bk << 16));
}

/* The rest of a "jsr abs" whose callee is converted: the internal cycle and the
 * return address the opcode pushes, then the callee's C body, which ends in
 * ss_rts and pops that frame. The frame is the routine's real return address,
 * so a callee that hands the rest of itself back to the ROM (ss_yield_wanted
 * inside its own step macros) still returns to the right place; that shows up
 * here as the frame still being on the stack, and is reported as true so the
 * caller can return as well. Registers are the callee's on return, exactly as
 * after the real jsr, so the caller reloads its locals from the CPU.
 */
static bool t_jsr_c(SnesState* ss, uint8_t pb, uint16_t target, RecompFn fn) {
  ss_idle(ss);
  const uint16_t sp0 = ss_sp(ss);
  const uint16_t ret = (uint16_t) (ss_pc(ss) - 1);
  ss_push8(ss, (uint8_t) (ret >> 8));
  ss_check_int(ss);
  ss_push8(ss, (uint8_t) ret);
  ss_set_pc(ss, pb, target);
  fn(ss);
  return ss_sp(ss) != sp0;
}

/* ---------------------------------------------------------------------------
 * vram_write_tile_row_planes — $C0:9C28
 *
 * Writes 4bpp tile rows to VMDATAL by shifting the 16-bit mask in $04/$05 out
 * one bit at a time: each pass rotates two bits out, builds a word from them
 * with xba/ora and writes it, then pads seven zero words (planes 2 and 3, and
 * the rest of the row pair). Y counts the rows and the loop runs while Y < $10,
 * so the entry from vram_generate_particle_tile with Y = 1 does 15 passes and
 * the fall-through entry with Y = $F does one.
 * Entry: A ignored (tdc overwrites it), Y = first row, C = the bit shifted in.
 * Exit: A = 0 (the last "lda #$0007" loop ran down to zero), Y = $10, C set
 * (cpy #$0010 with Y = $10), Z set, N clear.
 * ------------------------------------------------------------------------- */
void vram_write_tile_row_planes(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint16_t dp = ss_dp(ss);

  SI(0x9C28); a = ss_dp(ss); ss_set_nz16(ss, a);   /* C09C28 tdc */

  for(;;) {
    S(0x9C29, 2);                           /* C09C29 sty ptr_04 */
    t_write16(ss, (uint16_t) (dp + ptr_04), y);
    S(0x9C2B, 2);                           /* C09C2B ror ptr_04 */
    t_ror_dp16(ss, (uint16_t) (dp + ptr_04));
    SI(0x9C2D); a = alu_ror16(ss, a);       /* C09C2D ror A */
    S(0x9C2E, 1); a = t_xba(ss, a);         /* C09C2E xba */
    S(0x9C2F, 2);                           /* C09C2F sta $06 */
    t_write16(ss, (uint16_t) (dp + ptr_04 + 2), a);
    SI(0x9C31); a = ss_dp(ss); ss_set_nz16(ss, a);  /* C09C31 tdc */
    S(0x9C32, 2);                           /* C09C32 ror ptr_04 */
    t_ror_dp16(ss, (uint16_t) (dp + ptr_04));
    SI(0x9C34); a = alu_ror16(ss, a);       /* C09C34 ror A */
    S(0x9C35, 2);                           /* C09C35 ora $06 */
    a = alu_ora16(ss, a, t_read16(ss, (uint16_t) (dp + ptr_04 + 2)));
    S(0x9C37, 3);                           /* C09C37 sta VMDATAL */
    t_write16(ss, ss_abs(ss, VMDATAL), a);
    S(0x9C3A, 3); a = 0x0007; ss_set_nz16(ss, a);   /* C09C3A lda #$0007 */
    do {
      S(0x9C3D, 3);                         /* C09C3D stz VMDATAL */
      t_write16(ss, ss_abs(ss, VMDATAL), 0);
      SI(0x9C40); a = alu_dec16(ss, a);     /* C09C40 dec A */
      S(0x9C41, 1); t_branch(ss, a != 0);   /* C09C41 bne loc_C09C3D */
    } while(a != 0);

    S(0x9C43, 2);                           /* C09C43 ror ptr_04 */
    t_ror_dp16(ss, (uint16_t) (dp + ptr_04));
    SI(0x9C45); a = alu_ror16(ss, a);       /* C09C45 ror A */
    S(0x9C46, 1); a = t_xba(ss, a);         /* C09C46 xba */
    S(0x9C47, 2);                           /* C09C47 sta $06 */
    t_write16(ss, (uint16_t) (dp + ptr_04 + 2), a);
    SI(0x9C49); a = ss_dp(ss); ss_set_nz16(ss, a);  /* C09C49 tdc */
    S(0x9C4A, 2);                           /* C09C4A ror ptr_04 */
    t_ror_dp16(ss, (uint16_t) (dp + ptr_04));
    SI(0x9C4C); a = alu_ror16(ss, a);       /* C09C4C ror A */
    S(0x9C4D, 2);                           /* C09C4D ora $06 */
    a = alu_ora16(ss, a, t_read16(ss, (uint16_t) (dp + ptr_04 + 2)));
    S(0x9C4F, 3);                           /* C09C4F sta VMDATAL */
    t_write16(ss, ss_abs(ss, VMDATAL), a);
    S(0x9C52, 3); a = 0x0007; ss_set_nz16(ss, a);   /* C09C52 lda #$0007 */
    do {
      S(0x9C55, 3);                         /* C09C55 stz VMDATAL */
      t_write16(ss, ss_abs(ss, VMDATAL), 0);
      SI(0x9C58); a = alu_dec16(ss, a);     /* C09C58 dec A */
      S(0x9C59, 1); t_branch(ss, a != 0);   /* C09C59 bne loc_C09C55 */
    } while(a != 0);

    SI(0x9C5B); y = (uint16_t) (y + 1); ss_set_nz16(ss, y);  /* C09C5B iny */
    S(0x9C5C, 3); alu_cmp16(ss, y, 0x0010); /* C09C5C cpy #$0010 */
    const bool again = !ss_c(ss);
    S(0x9C5F, 1); t_branch(ss, again);      /* C09C5F bcc loc_C09C29 */
    if(!again) break;
  }

  ss_set_a(ss, a);
  ss_set_y(ss, y);
  S(0x9C61, 1);                             /* C09C61 rts */
  ss_rts(ss);
}

/* ---------------------------------------------------------------------------
 * vram_generate_particle_tile — $C0:9C16
 *
 * Entry: A = VRAM word address (the callers pass $1F00). Sets VMAIN to
 * increment on the high byte, points VMADDL at A, writes row 0 through
 * vram_write_tile_row_planes with Y = 1 and then falls *into* the same routine
 * with Y = $F for the last row, so the tail's rts returns to this routine's
 * caller. The two entries of that routine are therefore both live: the jsr here
 * and the fall-through below, which is why the hook has to run the second one
 * itself rather than stop at $C09C28.
 * Exit: as vram_write_tile_row_planes.
 * ------------------------------------------------------------------------- */
void vram_generate_particle_tile(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);

  S(0x9C16, 3); y = 0x0080; ss_set_nz16(ss, y);   /* C09C16 ldy #$0080 */
  S(0x9C19, 3);                             /* C09C19 sty VMAIN */
  t_write16(ss, ss_abs(ss, VMAIN), y);
  S(0x9C1C, 3);                             /* C09C1C sta VMADDL */
  t_write16(ss, ss_abs(ss, VMADDL), a);
  S(0x9C1F, 3); y = 0x0001; ss_set_nz16(ss, y);   /* C09C1F ldy #$0001 */

  S(0x9C22, 3);                             /* C09C22 jsr vram_write_tile_row_planes */
  if(t_jsr_c(ss, pb, 0x9C28, vram_write_tile_row_planes)) return;
  a = ss_a(ss); x = ss_x(ss); y = ss_y(ss);

  S(0x9C25, 3); y = 0x000F; ss_set_nz16(ss, y);   /* C09C25 ldy #$000F */

  /* falls through into vram_write_tile_row_planes at $C09C28 */
  ss_set_a(ss, a);
  ss_set_x(ss, x);
  ss_set_y(ss, y);
  vram_write_tile_row_planes(ss);
}

/* ---------------------------------------------------------------------------
 * vram_upload_shared_tileset_c5 — $C0:9234
 *
 * Uploads the 96 shared 4bpp OBJ tiles at $C5:02C0 (docs/data_formats.md,
 * region 0502C0) to VRAM word address 0, then falls through into
 * particle_table_clear at $C0:9246, whose rts returns to this routine's caller.
 * The hook therefore ends by leaving the pc at $9246 rather than returning:
 * the fall-through is the routine's tail, and particle_table_clear runs next
 * (as its own hook, or as the ROM's code when the table is narrowed).
 * ------------------------------------------------------------------------- */
void vram_upload_shared_tileset_c5(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);

  S(0x9234, 3); a = 0x0000; ss_set_nz16(ss, a);   /* C09234 lda #$0000 */
  S(0x9237, 3);                             /* C09237 sta VMADDL */
  t_write16(ss, ss_abs(ss, VMADDL), a);
  S(0x923A, 3); x = 0x00C5; ss_set_nz16(ss, x);   /* C0923A ldx #$00C5 */
  S(0x923D, 3); a = 0x02C0; ss_set_nz16(ss, a);   /* C0923D lda #$02C0 */
  S(0x9240, 3); y = 0x0C00; ss_set_nz16(ss, y);   /* C09240 ldy #$0C00 */

  S(0x9243, 3);                             /* C09243 jsr dma_upload_to_vram */
  (void) t_jsr_c(ss, pb, 0xA46A, dma_upload_to_vram);
  /* returns (or yields) with the pc at $9246 = particle_table_clear */
}

/* ---------------------------------------------------------------------------
 * vram_stream_descriptor_dispatch — $C0:9C62
 *
 * Entry: A = descriptor sub-index (the only caller, mode0_weather_zone_update,
 * passes 0), DB = $80.
 *
 * $0BC6 negative means "no stream selected": the camera's high byte doubled
 * plus A indexes data_C0B24C, and a non-negative entry that differs from $0BC4
 * becomes the new descriptor. From there X is the descriptor offset into the
 * $80:B208 table. $0BCA (words still to send) decides between starting a
 * descriptor and continuing one: a continuation fills one 8-byte job at $0A8A,
 * advances the source and VRAM addresses, and re-clamps the remaining count to
 * $400 words per frame; a fresh descriptor is either a VRAM stream (bit 15 of
 * the $B20C word set) or a CGRAM record queued at $0B8A for
 * cgram_upload_queue_flush.
 * ------------------------------------------------------------------------- */
void vram_stream_descriptor_dispatch(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint16_t dp = ss_dp(ss);

  S(0x9C62, 2);                             /* C09C62 sta ptr_04 */
  t_write16(ss, (uint16_t) (dp + ptr_04), a);
  S(0x9C64, 3);                             /* C09C64 lda $0BC6 */
  a = t_read16(ss, ss_abs(ss, 0x0BC6));
  ss_set_nz16(ss, a);
  bool selected = (a & 0x8000) == 0;
  S(0x9C67, 1); t_branch(ss, selected);     /* C09C67 bpl loc_C09C84 */

  if(!selected) {
    S(0x9C69, 2);                           /* C09C69 lda camera_x */
    a = t_read16(ss, (uint16_t) (dp + camera_x));
    ss_set_nz16(ss, a);
    S(0x9C6B, 1); a = t_xba(ss, a);         /* C09C6B xba */
    S(0x9C6C, 3); a = alu_and16(ss, a, 0x00FF);  /* C09C6C and #$00FF */
    SI(0x9C6F); a = alu_asl16(ss, a);       /* C09C6F asl A */
    S(0x9C70, 2);                           /* C09C70 adc ptr_04 */
    a = alu_adc16(ss, a, t_read16(ss, (uint16_t) (dp + ptr_04)));
    SI(0x9C72); x = a; ss_set_nz16(ss, x);  /* C09C72 tax */
    S(0x9C73, 4);                           /* C09C73 lda data_C0B24C,X */
    a = t_read16(ss, (0x80B24Cu + x) & 0xffffff);
    ss_set_nz16(ss, a);
    const bool none = (a & 0x8000) != 0;
    S(0x9C77, 1); t_branch(ss, none);       /* C09C77 bmi loc_C09C93 */
    if(none) { ss_set_a(ss, a); ss_set_x(ss, x);
               S(0x9C93, 1); ss_rts(ss); return; }   /* C09C93 rts */
    S(0x9C79, 3);                           /* C09C79 cmp $0BC4 */
    alu_cmp16(ss, a, t_read16(ss, ss_abs(ss, 0x0BC4)));
    const bool same = ss_z(ss);
    S(0x9C7C, 1); t_branch(ss, same);       /* C09C7C beq loc_C09C93 */
    if(same) { ss_set_a(ss, a); ss_set_x(ss, x);
               S(0x9C93, 1); ss_rts(ss); return; }   /* C09C93 rts */
    S(0x9C7E, 3); t_write16(ss, ss_abs(ss, 0x0BC4), a);  /* C09C7E sta $0BC4 */
    S(0x9C81, 3); t_write16(ss, ss_abs(ss, 0x0BC6), a);  /* C09C81 sta $0BC6 */
  }

  /* loc_C09C84 */
  SI(0x9C84); x = a; ss_set_nz16(ss, x);    /* C09C84 tax */
  S(0x9C85, 3);                             /* C09C85 lda $0BCA */
  a = t_read16(ss, ss_abs(ss, 0x0BCA));
  ss_set_nz16(ss, a);
  const bool continuing = a != 0;
  S(0x9C88, 1); t_branch(ss, continuing);   /* C09C88 bne loc_C09CB8 */

  if(!continuing) {
    S(0x9C8A, 4);                           /* C09C8A lda data_C0B208,X */
    a = t_read16(ss, (0x80B208u + x) & 0xffffff);
    ss_set_nz16(ss, a);
    const bool live = (a & 0x8000) == 0;
    S(0x9C8E, 1); t_branch(ss, live);       /* C09C8E bpl loc_C09C94 */
    if(!live) {
      S(0x9C90, 3); t_write16(ss, ss_abs(ss, 0x0BC6), a);  /* C09C90 sta $0BC6 */
      ss_set_a(ss, a); ss_set_x(ss, x);
      S(0x9C93, 1); ss_rts(ss);             /* C09C93 rts */
      return;
    }

    /* loc_C09C94 */
    S(0x9C94, 3); t_write16(ss, ss_abs(ss, 0x0BCC), a);    /* C09C94 sta $0BCC */
    S(0x9C97, 4);                           /* C09C97 lda data_C0B20C,X */
    a = t_read16(ss, (0x80B20Cu + x) & 0xffffff);
    ss_set_nz16(ss, a);
    const bool cgram = (a & 0x8000) == 0;
    S(0x9C9B, 1); t_branch(ss, cgram);      /* C09C9B bpl loc_C09D02 */

    if(cgram) {
      /* loc_C09D02: one queued CGRAM record for cgram_upload_queue_flush */
      S(0x9D02, 3);                         /* C09D02 ldy $0B8A */
      y = t_read16(ss, ss_abs(ss, cgram_queue_index));
      ss_set_nz16(ss, y);
      S(0x9D05, 3); t_index(ss);            /* C09D05 sta $0B92,Y */
      t_write16(ss, ss_abs(ss, (uint16_t) (0x0B92 + y)), a);
      S(0x9D08, 3);                         /* C09D08 lda $0BCC */
      a = t_read16(ss, ss_abs(ss, 0x0BCC));
      ss_set_nz16(ss, a);
      S(0x9D0B, 3); t_index(ss);            /* C09D0B sta $0B8E,Y */
      t_write16(ss, ss_abs(ss, (uint16_t) (0x0B8E + y)), a);
      S(0x9D0E, 4);                         /* C09D0E lda data_C0B20E,X */
      a = t_read16(ss, (0x80B20Eu + x) & 0xffffff);
      ss_set_nz16(ss, a);
      S(0x9D12, 3); t_index(ss);            /* C09D12 sta $0B8C,Y */
      t_write16(ss, ss_abs(ss, (uint16_t) (0x0B8C + y)), a);
      S(0x9D15, 4);                         /* C09D15 lda data_C0B20A,X */
      a = t_read16(ss, (0x80B20Au + x) & 0xffffff);
      ss_set_nz16(ss, a);
      S(0x9D19, 3); t_index(ss);            /* C09D19 sta $0B90,Y */
      t_write16(ss, ss_abs(ss, (uint16_t) (0x0B90 + y)), a);
      SI(0x9D1C); a = y; ss_set_nz16(ss, a);     /* C09D1C tya */
      SI(0x9D1D); ss_set_c(ss, false);           /* C09D1D clc */
      S(0x9D1E, 3); a = alu_adc16(ss, a, 0x0008);/* C09D1E adc #$0008 */
      S(0x9D21, 3);                         /* C09D21 sta $0B8A */
      t_write16(ss, ss_abs(ss, cgram_queue_index), a);
      SI(0x9D24); a = x; ss_set_nz16(ss, a);     /* C09D24 txa */
      S(0x9D25, 3); a = alu_adc16(ss, a, 0x0008);/* C09D25 adc #$0008 */
      S(0x9D28, 3); t_write16(ss, ss_abs(ss, 0x0BC6), a);  /* C09D28 sta $0BC6 */
      S(0x9D2B, 3); t_write16(ss, ss_abs(ss, 0x0BC8), 0);  /* C09D2B stz $0BC8 */
      S(0x9D2E, 3); t_write16(ss, ss_abs(ss, 0x0BCA), 0);  /* C09D2E stz $0BCA */
      ss_set_a(ss, a); ss_set_x(ss, x); ss_set_y(ss, y);
      S(0x9D31, 1); ss_rts(ss);             /* C09D31 rts */
      return;
    }

    /* the VRAM-stream descriptor */
    S(0x9C9D, 3); t_write16(ss, ss_abs(ss, 0x0BD0), a);    /* C09C9D sta $0BD0 */
    S(0x9CA0, 4);                           /* C09CA0 lda data_C0B20A,X */
    a = t_read16(ss, (0x80B20Au + x) & 0xffffff);
    ss_set_nz16(ss, a);
    S(0x9CA4, 3); t_write16(ss, ss_abs(ss, 0x0BCE), a);    /* C09CA4 sta $0BCE */
    S(0x9CA7, 4);                           /* C09CA7 lda data_C0B20E,X */
    a = t_read16(ss, (0x80B20Eu + x) & 0xffffff);
    ss_set_nz16(ss, a);
    S(0x9CAB, 3); t_write16(ss, ss_abs(ss, 0x0BC8), a);    /* C09CAB sta $0BC8 */
    SI(0x9CAE); a = x; ss_set_nz16(ss, a);       /* C09CAE txa */
    SI(0x9CAF); ss_set_c(ss, false);             /* C09CAF clc */
    S(0x9CB0, 3); a = alu_adc16(ss, a, 0x0008);  /* C09CB0 adc #$0008 */
    S(0x9CB3, 3); t_write16(ss, ss_abs(ss, 0x0BC6), a);    /* C09CB3 sta $0BC6 */
    S(0x9CB6, 1); t_branch(ss, true);            /* C09CB6 bra loc_C09CE7 */
  } else {
    /* loc_C09CB8: one more 8-byte upload job for the stream in progress */
    S(0x9CB8, 3);                           /* C09CB8 ldx $0A88 */
    x = t_read16(ss, ss_abs(ss, 0x0A88));
    ss_set_nz16(ss, x);
    S(0x9CBB, 3);                           /* C09CBB lda $0BD0 */
    a = t_read16(ss, ss_abs(ss, 0x0BD0));
    ss_set_nz16(ss, a);
    S(0x9CBE, 3); t_index(ss);              /* C09CBE sta $0A90,X */
    t_write16(ss, ss_abs(ss, (uint16_t) (entity_tile_job + 6 + x)), a);
    S(0x9CC1, 3);                           /* C09CC1 lda $0BCC */
    a = t_read16(ss, ss_abs(ss, 0x0BCC));
    ss_set_nz16(ss, a);
    S(0x9CC4, 3); t_index(ss);              /* C09CC4 sta $0A8C,X */
    t_write16(ss, ss_abs(ss, (uint16_t) (entity_tile_job + 2 + x)), a);
    S(0x9CC7, 3);                           /* C09CC7 lda $0BCA */
    a = t_read16(ss, ss_abs(ss, 0x0BCA));
    ss_set_nz16(ss, a);
    S(0x9CCA, 3); t_index(ss);              /* C09CCA sta $0A8A,X */
    t_write16(ss, ss_abs(ss, (uint16_t) (entity_tile_job + 0 + x)), a);
    SI(0x9CCD); a = alu_lsr16(ss, a);       /* C09CCD lsr A */
    S(0x9CCE, 3);                           /* C09CCE adc $0BCC */
    a = alu_adc16(ss, a, t_read16(ss, ss_abs(ss, 0x0BCC)));
    S(0x9CD1, 3); t_write16(ss, ss_abs(ss, 0x0BCC), a);   /* C09CD1 sta $0BCC */
    S(0x9CD4, 3);                           /* C09CD4 lda $0BCE */
    a = t_read16(ss, ss_abs(ss, 0x0BCE));
    ss_set_nz16(ss, a);
    S(0x9CD7, 3); t_index(ss);              /* C09CD7 sta $0A8E,X */
    t_write16(ss, ss_abs(ss, (uint16_t) (entity_tile_job + 4 + x)), a);
    S(0x9CDA, 3);                           /* C09CDA adc $0BCA */
    a = alu_adc16(ss, a, t_read16(ss, ss_abs(ss, 0x0BCA)));
    S(0x9CDD, 3); t_write16(ss, ss_abs(ss, 0x0BCE), a);   /* C09CDD sta $0BCE */
    SI(0x9CE0); a = x; ss_set_nz16(ss, a);       /* C09CE0 txa */
    S(0x9CE1, 3); a = alu_adc16(ss, a, 0x0008);  /* C09CE1 adc #$0008 */
    S(0x9CE4, 3); t_write16(ss, ss_abs(ss, 0x0A88), a);   /* C09CE4 sta $0A88 */
  }

  /* loc_C09CE7: clamp what is left of the stream to $400 words for next frame */
  S(0x9CE7, 3); y = 0x0400; ss_set_nz16(ss, y);   /* C09CE7 ldy #$0400 */
  S(0x9CEA, 3);                             /* C09CEA lda $0BC8 */
  a = t_read16(ss, ss_abs(ss, 0x0BC8));
  ss_set_nz16(ss, a);
  const bool done = a == 0;
  S(0x9CED, 1); t_branch(ss, done);         /* C09CED beq loc_C09CF9 */
  bool tail = done;
  if(!done) {
    SI(0x9CEF); ss_set_c(ss, true);         /* C09CEF sec */
    S(0x9CF0, 3); a = alu_sbc16(ss, a, 0x0400);  /* C09CF0 sbc #$0400 */
    const bool more = (a & 0x8000) == 0;
    S(0x9CF3, 1); t_branch(ss, more);       /* C09CF3 bpl loc_C09CFB */
    if(!more) {
      S(0x9CF5, 3); a = alu_eor16(ss, a, 0xFFFF);  /* C09CF5 eor #$FFFF */
      SI(0x9CF8); a = alu_inc16(ss, a);            /* C09CF8 inc A */
      tail = true;
    }
  }
  if(tail) {
    /* loc_C09CF9 */
    SI(0x9CF9); y = a; ss_set_nz16(ss, y);  /* C09CF9 tay */
    SI(0x9CFA); a = ss_dp(ss); ss_set_nz16(ss, a);  /* C09CFA tdc */
  }
  /* loc_C09CFB */
  S(0x9CFB, 3); t_write16(ss, ss_abs(ss, 0x0BC8), a);   /* C09CFB sta $0BC8 */
  S(0x9CFE, 3); t_write16(ss, ss_abs(ss, 0x0BCA), y);   /* C09CFE sty $0BCA */
  ss_set_a(ss, a); ss_set_x(ss, x); ss_set_y(ss, y);
  S(0x9D01, 1); ss_rts(ss);                 /* C09D01 rts */
}

/* ---------------------------------------------------------------------------
 * build_metatile_column_500 — $C0:9E83
 *
 * Builds the 32-word column staged at $0500. The map pointer $18/$1A is
 * tilemap_a_addr/_bank plus the camera X (halved, or scaled by 1.5, according
 * to the sign of the layer-2 parallax word at $82) and the camera Y row; $1C
 * and $1E are the two metatile definition bases the flip bits choose between.
 * Each of the 54 cells is read as "lda [$18]" with DB set to
 * metatile_data_bank for the definition reads, expanded into four words at
 * $06C0,X, and the map pointer advanced by the row stride at $84.
 *
 * The phk at $9ECD is what the plb at $9F0D restores: the loop pushes and pulls
 * the data bank on every iteration and the routine's own bank is the one it
 * ends with. A yield inside the loop is safe for the same reason — every push
 * is balanced by the two pulls of the same iteration.
 * Exit: A = $0708 (the cmp that ended the loop), X = the copy cursor, Y = the
 * $0500 write cursor, C set, Z set from the closing cpx.
 * ------------------------------------------------------------------------- */
void build_metatile_column_500(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint16_t dp = ss_dp(ss);

  S(0x9E83, 2);                             /* C09E83 lda camera_x */
  a = t_read16(ss, (uint16_t) (dp + camera_x));
  ss_set_nz16(ss, a);
  S(0x9E85, 3); a = alu_and16(ss, a, 0xFFE0);   /* C09E85 and #$FFE0 */
  S(0x9E88, 2);                             /* C09E88 ldy $82 */
  y = t_read16(ss, (uint16_t) (dp + 0x0082));
  ss_set_nz16(ss, y);
  const bool wide = (y & 0x8000) != 0;
  S(0x9E8A, 1); t_branch(ss, wide);         /* C09E8A bmi loc_C09E91 */
  if(!wide) {
    const bool flat = y == 0;
    S(0x9E8C, 1); t_branch(ss, flat);       /* C09E8C beq loc_C09E97 */
    if(!flat) {
      SI(0x9E8E); a = alu_lsr16(ss, a);     /* C09E8E lsr A */
      S(0x9E8F, 1); t_branch(ss, true);     /* C09E8F bra loc_C09E97 */
    }
  } else {
    /* loc_C09E91 */
    S(0x9E91, 2);                           /* C09E91 sta ptr_04 */
    t_write16(ss, (uint16_t) (dp + ptr_04), a);
    SI(0x9E93); a = alu_lsr16(ss, a);       /* C09E93 lsr A */
    SI(0x9E94); ss_set_c(ss, false);        /* C09E94 clc */
    S(0x9E95, 2);                           /* C09E95 adc ptr_04 */
    a = alu_adc16(ss, a, t_read16(ss, (uint16_t) (dp + ptr_04)));
  }

  /* loc_C09E97 */
  SI(0x9E97); ss_set_c(ss, false);          /* C09E97 clc */
  S(0x9E98, 2);                             /* C09E98 adc tilemap_a_addr */
  a = alu_adc16(ss, a, t_read16(ss, (uint16_t) (dp + tilemap_a_addr)));
  S(0x9E9A, 2);                             /* C09E9A sta $18 */
  t_write16(ss, (uint16_t) (dp + scratch_18), a);
  S(0x9E9C, 2);                             /* C09E9C lda tilemap_a_bank */
  a = t_read16(ss, (uint16_t) (dp + tilemap_a_bank));
  ss_set_nz16(ss, a);
  S(0x9E9E, 2);                             /* C09E9E sta $1A */
  t_write16(ss, (uint16_t) (dp + scratch_18 + 2), a);
  S(0x9EA0, 2);                             /* C09EA0 lda camera_y_lookahead */
  a = t_read16(ss, (uint16_t) (dp + camera_y_lookahead));
  ss_set_nz16(ss, a);
  const bool ahead = (a & 0x8000) == 0;
  S(0x9EA2, 1); t_branch(ss, ahead);        /* C09EA2 bpl loc_C09EA8 */
  if(!ahead) {
    S(0x9EA4, 2);                           /* C09EA4 lda camera_y */
    a = t_read16(ss, (uint16_t) (dp + camera_y));
    ss_set_nz16(ss, a);
    S(0x9EA6, 1); t_branch(ss, true);       /* C09EA6 bra loc_C09EAE */
  } else {
    S(0x9EA8, 2);                           /* C09EA8 lda camera_y */
    a = t_read16(ss, (uint16_t) (dp + camera_y));
    ss_set_nz16(ss, a);
    SI(0x9EAA); ss_set_c(ss, false);        /* C09EAA clc */
    S(0x9EAB, 3); a = alu_adc16(ss, a, 0x00E0);  /* C09EAB adc #$00E0 */
  }

  /* loc_C09EAE */
  SI(0x9EAE); y = a; ss_set_nz16(ss, y);    /* C09EAE tay */
  S(0x9EAF, 3); a = alu_and16(ss, a, 0xFFE0);  /* C09EAF and #$FFE0 */
  SI(0x9EB2); a = alu_lsr16(ss, a);         /* C09EB2 lsr A */
  SI(0x9EB3); a = alu_lsr16(ss, a);         /* C09EB3 lsr A */
  SI(0x9EB4); a = alu_lsr16(ss, a);         /* C09EB4 lsr A */
  SI(0x9EB5); a = alu_lsr16(ss, a);         /* C09EB5 lsr A */
  S(0x9EB6, 2);                             /* C09EB6 adc $18 */
  a = alu_adc16(ss, a, t_read16(ss, (uint16_t) (dp + scratch_18)));
  S(0x9EB8, 2);                             /* C09EB8 sta $18 */
  t_write16(ss, (uint16_t) (dp + scratch_18), a);
  SI(0x9EBA); a = y; ss_set_nz16(ss, a);    /* C09EBA tya */
  S(0x9EBB, 3); a = alu_and16(ss, a, 0x0018);  /* C09EBB and #$0018 */
  S(0x9EBE, 2);                             /* C09EBE adc tilemap_b_addr */
  a = alu_adc16(ss, a, t_read16(ss, (uint16_t) (dp + tilemap_b_addr)));
  S(0x9EC0, 2);                             /* C09EC0 sta $1C */
  t_write16(ss, (uint16_t) (dp + scratch_18 + 4), a);
  SI(0x9EC2); a = y; ss_set_nz16(ss, a);    /* C09EC2 tya */
  S(0x9EC3, 3); a = alu_and16(ss, a, 0x0018);  /* C09EC3 and #$0018 */
  S(0x9EC6, 3); a = alu_eor16(ss, a, 0x0018);  /* C09EC6 eor #$0018 */
  S(0x9EC9, 2);                             /* C09EC9 adc tilemap_b_addr */
  a = alu_adc16(ss, a, t_read16(ss, (uint16_t) (dp + tilemap_b_addr)));
  S(0x9ECB, 2);                             /* C09ECB sta $1E */
  t_write16(ss, (uint16_t) (dp + scratch_18 + 6), a);

  S(0x9ECD, 1);                             /* C09ECD phk */
  ss_idle(ss);
  ss_check_int(ss);
  ss_push8(ss, pb);
  S(0x9ECE, 3); x = 0x06C0; ss_set_nz16(ss, x);  /* C09ECE ldx #$06C0 */

  for(;;) {
    /* loc_C09ED1 */
    S(0x9ED1, 2);                           /* C09ED1 lda metatile_data_bank */
    a = t_read16(ss, (uint16_t) (dp + metatile_data_bank));
    ss_set_nz16(ss, a);
    S(0x9ED3, 1);                           /* C09ED3 pha */
    ss_idle(ss);
    ss_push8(ss, (uint8_t) (a >> 8));
    ss_check_int(ss);
    ss_push8(ss, (uint8_t) a);
    S(0x9ED4, 1);                           /* C09ED4 plb */
    ss_idle(ss); ss_idle(ss); ss_check_int(ss);
    { uint8_t b = ss_pull8(ss); ss_set_db(ss, b); ss_set_nz8(ss, b); }
    S(0x9ED5, 1);                           /* C09ED5 plb */
    ss_idle(ss); ss_idle(ss); ss_check_int(ss);
    { uint8_t b = ss_pull8(ss); ss_set_db(ss, b); ss_set_nz8(ss, b); }

    S(0x9ED6, 2);                           /* C09ED6 lda [$18] */
    a = t_read16(ss, t_idl_ptr(ss, (uint16_t) (dp + scratch_18)));
    ss_set_nz16(ss, a);
    const bool vflip = (a & 0x8000) != 0;
    S(0x9ED8, 1); t_branch(ss, vflip);      /* C09ED8 bmi loc_C09F39 */

    if(!vflip) {
      S(0x9EDA, 3); alu_bit_imm16(ss, a, 0x4000);  /* C09EDA bit #$4000 */
      const bool hflip = !ss_z(ss);
      S(0x9EDD, 1); t_branch(ss, hflip);    /* C09EDD bne loc_C09F11 */
      if(!hflip) {
        SI(0x9EDF); a = alu_asl16(ss, a);   /* C09EDF asl A */
        SI(0x9EE0); a = alu_asl16(ss, a);   /* C09EE0 asl A */
        SI(0x9EE1); a = alu_asl16(ss, a);   /* C09EE1 asl A */
        SI(0x9EE2); a = alu_asl16(ss, a);   /* C09EE2 asl A */
        SI(0x9EE3); a = alu_asl16(ss, a);   /* C09EE3 asl A */
        S(0x9EE4, 2);                       /* C09EE4 adc $1C */
        a = alu_adc16(ss, a, t_read16(ss, (uint16_t) (dp + scratch_18 + 4)));
        SI(0x9EE6); y = a; ss_set_nz16(ss, y);  /* C09EE6 tay */
        S(0x9EE7, 3); t_index(ss);          /* C09EE7 lda nmi_handler_ptr,Y */
        a = t_read16(ss, ss_abs(ss, (uint16_t) (nmi_handler_ptr + y)));
        ss_set_nz16(ss, a);
        S(0x9EEA, 2); t_index(ss);          /* C09EEA sta nmi_handler_ptr,X */
        t_write16(ss, (uint16_t) (dp + nmi_handler_ptr + x), a);
        S(0x9EEC, 3); t_index(ss);          /* C09EEC lda dma_pending_mask,Y */
        a = t_read16(ss, ss_abs(ss, (uint16_t) (dma_pending_mask + y)));
        ss_set_nz16(ss, a);
        S(0x9EEF, 2); t_index(ss);          /* C09EEF sta dma_pending_mask,X */
        t_write16(ss, (uint16_t) (dp + dma_pending_mask + x), a);
        S(0x9EF1, 3); t_index(ss);          /* C09EF1 lda ptr_04,Y */
        a = t_read16(ss, ss_abs(ss, (uint16_t) (ptr_04 + y)));
        ss_set_nz16(ss, a);
        S(0x9EF4, 2); t_index(ss);          /* C09EF4 sta ptr_04,X */
        t_write16(ss, (uint16_t) (dp + ptr_04 + x), a);
        S(0x9EF6, 3); t_index(ss);          /* C09EF6 lda $0006,Y */
        a = t_read16(ss, ss_abs(ss, (uint16_t) (0x0006 + y)));
        ss_set_nz16(ss, a);
      } else {
        /* loc_C09F11: horizontal flip, the four words backwards */
        SI(0x9F11); a = alu_asl16(ss, a);   /* C09F11 asl A */
        SI(0x9F12); a = alu_asl16(ss, a);   /* C09F12 asl A */
        SI(0x9F13); a = alu_asl16(ss, a);   /* C09F13 asl A */
        SI(0x9F14); a = alu_asl16(ss, a);   /* C09F14 asl A */
        SI(0x9F15); a = alu_asl16(ss, a);   /* C09F15 asl A */
        S(0x9F16, 2);                       /* C09F16 adc $1C */
        a = alu_adc16(ss, a, t_read16(ss, (uint16_t) (dp + scratch_18 + 4)));
        SI(0x9F18); y = a; ss_set_nz16(ss, y);  /* C09F18 tay */
        S(0x9F19, 3); t_index(ss);          /* C09F19 lda $0006,Y */
        a = t_read16(ss, ss_abs(ss, (uint16_t) (0x0006 + y)));
        ss_set_nz16(ss, a);
        S(0x9F1C, 3); a = alu_eor16(ss, a, 0x4000);  /* C09F1C eor #$4000 */
        S(0x9F1F, 2); t_index(ss);          /* C09F1F sta nmi_handler_ptr,X */
        t_write16(ss, (uint16_t) (dp + nmi_handler_ptr + x), a);
        S(0x9F21, 3); t_index(ss);          /* C09F21 lda ptr_04,Y */
        a = t_read16(ss, ss_abs(ss, (uint16_t) (ptr_04 + y)));
        ss_set_nz16(ss, a);
        S(0x9F24, 3); a = alu_eor16(ss, a, 0x4000);  /* C09F24 eor #$4000 */
        S(0x9F27, 2); t_index(ss);          /* C09F27 sta dma_pending_mask,X */
        t_write16(ss, (uint16_t) (dp + dma_pending_mask + x), a);
        S(0x9F29, 3); t_index(ss);          /* C09F29 lda dma_pending_mask,Y */
        a = t_read16(ss, ss_abs(ss, (uint16_t) (dma_pending_mask + y)));
        ss_set_nz16(ss, a);
        S(0x9F2C, 3); a = alu_eor16(ss, a, 0x4000);  /* C09F2C eor #$4000 */
        S(0x9F2F, 2); t_index(ss);          /* C09F2F sta ptr_04,X */
        t_write16(ss, (uint16_t) (dp + ptr_04 + x), a);
        S(0x9F31, 3); t_index(ss);          /* C09F31 lda nmi_handler_ptr,Y */
        a = t_read16(ss, ss_abs(ss, (uint16_t) (nmi_handler_ptr + y)));
        ss_set_nz16(ss, a);
        S(0x9F34, 3); a = alu_eor16(ss, a, 0x4000);  /* C09F34 eor #$4000 */
        S(0x9F37, 1); t_branch(ss, true);   /* C09F37 bra loc_C09EF9 */
      }
    } else {
      /* loc_C09F39 */
      S(0x9F39, 3); alu_bit_imm16(ss, a, 0x4000);  /* C09F39 bit #$4000 */
      const bool hflip = !ss_z(ss);
      S(0x9F3C, 1); t_branch(ss, hflip);    /* C09F3C bne loc_C09F66 */
      if(!hflip) {
        SI(0x9F3E); a = alu_asl16(ss, a);   /* C09F3E asl A */
        SI(0x9F3F); a = alu_asl16(ss, a);   /* C09F3F asl A */
        SI(0x9F40); a = alu_asl16(ss, a);   /* C09F40 asl A */
        SI(0x9F41); a = alu_asl16(ss, a);   /* C09F41 asl A */
        SI(0x9F42); a = alu_asl16(ss, a);   /* C09F42 asl A */
        S(0x9F43, 2);                       /* C09F43 adc $1E */
        a = alu_adc16(ss, a, t_read16(ss, (uint16_t) (dp + scratch_18 + 6)));
        SI(0x9F45); y = a; ss_set_nz16(ss, y);  /* C09F45 tay */
        S(0x9F46, 3); t_index(ss);          /* C09F46 lda nmi_handler_ptr,Y */
        a = t_read16(ss, ss_abs(ss, (uint16_t) (nmi_handler_ptr + y)));
        ss_set_nz16(ss, a);
        S(0x9F49, 3); a = alu_eor16(ss, a, 0x8000);  /* C09F49 eor #$8000 */
        S(0x9F4C, 2); t_index(ss);          /* C09F4C sta nmi_handler_ptr,X */
        t_write16(ss, (uint16_t) (dp + nmi_handler_ptr + x), a);
        S(0x9F4E, 3); t_index(ss);          /* C09F4E lda dma_pending_mask,Y */
        a = t_read16(ss, ss_abs(ss, (uint16_t) (dma_pending_mask + y)));
        ss_set_nz16(ss, a);
        S(0x9F51, 3); a = alu_eor16(ss, a, 0x8000);  /* C09F51 eor #$8000 */
        S(0x9F54, 2); t_index(ss);          /* C09F54 sta dma_pending_mask,X */
        t_write16(ss, (uint16_t) (dp + dma_pending_mask + x), a);
        S(0x9F56, 3); t_index(ss);          /* C09F56 lda ptr_04,Y */
        a = t_read16(ss, ss_abs(ss, (uint16_t) (ptr_04 + y)));
        ss_set_nz16(ss, a);
        S(0x9F59, 3); a = alu_eor16(ss, a, 0x8000);  /* C09F59 eor #$8000 */
        S(0x9F5C, 2); t_index(ss);          /* C09F5C sta ptr_04,X */
        t_write16(ss, (uint16_t) (dp + ptr_04 + x), a);
        S(0x9F5E, 3); t_index(ss);          /* C09F5E lda $0006,Y */
        a = t_read16(ss, ss_abs(ss, (uint16_t) (0x0006 + y)));
        ss_set_nz16(ss, a);
        S(0x9F61, 3); a = alu_eor16(ss, a, 0x8000);  /* C09F61 eor #$8000 */
        S(0x9F64, 1); t_branch(ss, true);   /* C09F64 bra loc_C09EF9 */
      } else {
        /* loc_C09F66: both flips */
        SI(0x9F66); a = alu_asl16(ss, a);   /* C09F66 asl A */
        SI(0x9F67); a = alu_asl16(ss, a);   /* C09F67 asl A */
        SI(0x9F68); a = alu_asl16(ss, a);   /* C09F68 asl A */
        SI(0x9F69); a = alu_asl16(ss, a);   /* C09F69 asl A */
        SI(0x9F6A); a = alu_asl16(ss, a);   /* C09F6A asl A */
        S(0x9F6B, 2);                       /* C09F6B adc $1E */
        a = alu_adc16(ss, a, t_read16(ss, (uint16_t) (dp + scratch_18 + 6)));
        SI(0x9F6D); y = a; ss_set_nz16(ss, y);  /* C09F6D tay */
        S(0x9F6E, 3); t_index(ss);          /* C09F6E lda $0006,Y */
        a = t_read16(ss, ss_abs(ss, (uint16_t) (0x0006 + y)));
        ss_set_nz16(ss, a);
        S(0x9F71, 3); a = alu_eor16(ss, a, 0xC000);  /* C09F71 eor #$C000 */
        S(0x9F74, 2); t_index(ss);          /* C09F74 sta nmi_handler_ptr,X */
        t_write16(ss, (uint16_t) (dp + nmi_handler_ptr + x), a);
        S(0x9F76, 3); t_index(ss);          /* C09F76 lda ptr_04,Y */
        a = t_read16(ss, ss_abs(ss, (uint16_t) (ptr_04 + y)));
        ss_set_nz16(ss, a);
        S(0x9F79, 3); a = alu_eor16(ss, a, 0xC000);  /* C09F79 eor #$C000 */
        S(0x9F7C, 2); t_index(ss);          /* C09F7C sta dma_pending_mask,X */
        t_write16(ss, (uint16_t) (dp + dma_pending_mask + x), a);
        S(0x9F7E, 3); t_index(ss);          /* C09F7E lda dma_pending_mask,Y */
        a = t_read16(ss, ss_abs(ss, (uint16_t) (dma_pending_mask + y)));
        ss_set_nz16(ss, a);
        S(0x9F81, 3); a = alu_eor16(ss, a, 0xC000);  /* C09F81 eor #$C000 */
        S(0x9F84, 2); t_index(ss);          /* C09F84 sta ptr_04,X */
        t_write16(ss, (uint16_t) (dp + ptr_04 + x), a);
        S(0x9F86, 3); t_index(ss);          /* C09F86 lda nmi_handler_ptr,Y */
        a = t_read16(ss, ss_abs(ss, (uint16_t) (nmi_handler_ptr + y)));
        ss_set_nz16(ss, a);
        S(0x9F89, 3); a = alu_eor16(ss, a, 0xC000);  /* C09F89 eor #$C000 */
        S(0x9F8C, 3);                       /* C09F8C jmp loc_C09EF9 */
      }
    }

    /* loc_C09EF9 */
    S(0x9EF9, 2); t_index(ss);              /* C09EF9 sta $06,X */
    t_write16(ss, (uint16_t) (dp + ptr_04 + 2 + x), a);
    S(0x9EFB, 2);                           /* C09EFB lda $18 */
    a = t_read16(ss, (uint16_t) (dp + scratch_18));
    ss_set_nz16(ss, a);
    SI(0x9EFD); ss_set_c(ss, false);        /* C09EFD clc */
    S(0x9EFE, 2);                           /* C09EFE adc $84 */
    a = alu_adc16(ss, a, t_read16(ss, (uint16_t) (dp + 0x0084)));
    S(0x9F00, 2);                           /* C09F00 sta $18 */
    t_write16(ss, (uint16_t) (dp + scratch_18), a);
    SI(0x9F02); a = x; ss_set_nz16(ss, a);  /* C09F02 txa */
    SI(0x9F03); ss_set_c(ss, false);        /* C09F03 clc */
    S(0x9F04, 3); a = alu_adc16(ss, a, 0x0008);  /* C09F04 adc #$0008 */
    SI(0x9F07); x = a; ss_set_nz16(ss, x);  /* C09F07 tax */
    S(0x9F08, 3); alu_cmp16(ss, a, 0x0708); /* C09F08 cmp #$0708 */
    const bool more = a != 0x0708;
    S(0x9F0B, 1); t_branch(ss, more);       /* C09F0B bne loc_C09ED1 */
    if(!more) break;
  }

  S(0x9F0D, 1);                             /* C09F0D plb */
  ss_idle(ss); ss_idle(ss); ss_check_int(ss);
  { uint8_t b = ss_pull8(ss); ss_set_db(ss, b); ss_set_nz8(ss, b); }
  S(0x9F0E, 3);                             /* C09F0E jmp loc_C09F8F */

  /* loc_C09F8F: copy the scratch column into the $0500 staging buffer */
  S(0x9F8F, 2);                             /* C09F8F lda camera_x */
  a = t_read16(ss, (uint16_t) (dp + camera_x));
  ss_set_nz16(ss, a);
  S(0x9F91, 3); a = alu_and16(ss, a, 0x01F8);  /* C09F91 and #$01F8 */
  SI(0x9F94); a = alu_lsr16(ss, a);         /* C09F94 lsr A */
  SI(0x9F95); a = alu_lsr16(ss, a);         /* C09F95 lsr A */
  SI(0x9F96); y = a; ss_set_nz16(ss, y);    /* C09F96 tay */
  S(0x9F97, 3); a = alu_and16(ss, a, 0x0006);  /* C09F97 and #$0006 */
  SI(0x9F9A); x = a; ss_set_nz16(ss, x);    /* C09F9A tax */
  SI(0x9F9B); ss_set_c(ss, false);          /* C09F9B clc */
  S(0x9F9C, 3); a = alu_adc16(ss, a, 0x0042);  /* C09F9C adc #$0042 */
  S(0x9F9F, 2);                             /* C09F9F sta $1C */
  t_write16(ss, (uint16_t) (dp + scratch_18 + 4), a);

  do {
    S(0x9FA1, 3); t_index(ss);              /* C09FA1 lda $06C0,X */
    a = t_read16(ss, ss_abs(ss, (uint16_t) (0x06C0 + x)));
    ss_set_nz16(ss, a);
    S(0x9FA4, 3); t_index(ss);              /* C09FA4 sta $0500,Y */
    t_write16(ss, ss_abs(ss, (uint16_t) (0x0500 + y)), a);
    SI(0x9FA7); a = y; ss_set_nz16(ss, a);  /* C09FA7 tya */
    SI(0x9FA8); ss_set_c(ss, false);        /* C09FA8 clc */
    S(0x9FA9, 3); a = alu_adc16(ss, a, 0x0002);  /* C09FA9 adc #$0002 */
    S(0x9FAC, 3); a = alu_and16(ss, a, 0x007E);  /* C09FAC and #$007E */
    SI(0x9FAF); y = a; ss_set_nz16(ss, y);  /* C09FAF tay */
    SI(0x9FB0); x = (uint16_t) (x + 1); ss_set_nz16(ss, x);  /* C09FB0 inx */
    SI(0x9FB1); x = (uint16_t) (x + 1); ss_set_nz16(ss, x);  /* C09FB1 inx */
    S(0x9FB2, 2);                           /* C09FB2 cpx $1C */
    alu_cpx16(ss, x, t_read16(ss, (uint16_t) (dp + scratch_18 + 4)));
    S(0x9FB4, 1); t_branch(ss, !ss_z(ss));  /* C09FB4 bne loc_C09FA1 */
  } while(!ss_z(ss));

  ss_set_a(ss, a);
  ss_set_x(ss, x);
  ss_set_y(ss, y);
  S(0x9FB6, 1);                             /* C09FB6 rts */
  ss_rts(ss);
}

/* ---------------------------------------------------------------------------
 * build_metatile_column_580 — $C0:9FB7
 *
 * The same blitter for the $0580 column: the map pointer is keyed off the
 * camera X plus a screen ($100) when the parallax word at $98 is non-negative,
 * the map advances by 2 (one cell down the column) instead of by the row stride
 * at $84, and the definition words are read column-major, at $00/$08/$10/$18
 * from the metatile base, as docs/data_formats.md describes. The flip cases
 * pick the alternate definition base in the opposite order from the $0500 copy;
 * that asymmetry is the ROM's, and is transcribed as it stands.
 * ------------------------------------------------------------------------- */
void build_metatile_column_580(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint16_t dp = ss_dp(ss);

  S(0x9FB7, 2);                             /* C09FB7 lda layer_parallax_mode */
  a = t_read16(ss, (uint16_t) (dp + layer_parallax_mode));
  ss_set_nz16(ss, a);
  const bool ahead = (a & 0x8000) == 0;
  S(0x9FB9, 1); t_branch(ss, ahead);        /* C09FB9 bpl loc_C09FBF */
  if(!ahead) {
    S(0x9FBB, 2);                           /* C09FBB lda camera_x */
    a = t_read16(ss, (uint16_t) (dp + camera_x));
    ss_set_nz16(ss, a);
    S(0x9FBD, 1); t_branch(ss, true);       /* C09FBD bra loc_C09FC5 */
  } else {
    S(0x9FBF, 2);                           /* C09FBF lda camera_x */
    a = t_read16(ss, (uint16_t) (dp + camera_x));
    ss_set_nz16(ss, a);
    SI(0x9FC1); ss_set_c(ss, false);        /* C09FC1 clc */
    S(0x9FC2, 3); a = alu_adc16(ss, a, 0x0100);  /* C09FC2 adc #$0100 */
  }

  /* loc_C09FC5 */
  SI(0x9FC5); y = a; ss_set_nz16(ss, y);    /* C09FC5 tay */
  S(0x9FC6, 3); a = alu_and16(ss, a, 0xFFE0);  /* C09FC6 and #$FFE0 */
  S(0x9FC9, 2);                             /* C09FC9 ldx $82 */
  x = t_read16(ss, (uint16_t) (dp + 0x0082));
  ss_set_nz16(ss, x);
  const bool wide = (x & 0x8000) != 0;
  S(0x9FCB, 1); t_branch(ss, wide);         /* C09FCB bmi loc_C09FD2 */
  if(!wide) {
    const bool flat = x == 0;
    S(0x9FCD, 1); t_branch(ss, flat);       /* C09FCD beq loc_C09FD8 */
    if(!flat) {
      SI(0x9FCF); a = alu_lsr16(ss, a);     /* C09FCF lsr A */
      S(0x9FD0, 1); t_branch(ss, true);     /* C09FD0 bra loc_C09FD8 */
    }
  } else {
    /* loc_C09FD2 */
    S(0x9FD2, 2);                           /* C09FD2 sta ptr_04 */
    t_write16(ss, (uint16_t) (dp + ptr_04), a);
    SI(0x9FD4); a = alu_lsr16(ss, a);       /* C09FD4 lsr A */
    SI(0x9FD5); ss_set_c(ss, false);        /* C09FD5 clc */
    S(0x9FD6, 2);                           /* C09FD6 adc ptr_04 */
    a = alu_adc16(ss, a, t_read16(ss, (uint16_t) (dp + ptr_04)));
  }

  /* loc_C09FD8 */
  SI(0x9FD8); ss_set_c(ss, false);          /* C09FD8 clc */
  S(0x9FD9, 2);                             /* C09FD9 adc tilemap_a_addr */
  a = alu_adc16(ss, a, t_read16(ss, (uint16_t) (dp + tilemap_a_addr)));
  S(0x9FDB, 2);                             /* C09FDB sta $18 */
  t_write16(ss, (uint16_t) (dp + scratch_18), a);
  S(0x9FDD, 2);                             /* C09FDD lda tilemap_a_bank */
  a = t_read16(ss, (uint16_t) (dp + tilemap_a_bank));
  ss_set_nz16(ss, a);
  S(0x9FDF, 2);                             /* C09FDF sta $1A */
  t_write16(ss, (uint16_t) (dp + scratch_18 + 2), a);
  S(0x9FE1, 2);                             /* C09FE1 lda camera_y */
  a = t_read16(ss, (uint16_t) (dp + camera_y));
  ss_set_nz16(ss, a);
  S(0x9FE3, 3); a = alu_and16(ss, a, 0xFFE0);  /* C09FE3 and #$FFE0 */
  SI(0x9FE6); a = alu_lsr16(ss, a);         /* C09FE6 lsr A */
  SI(0x9FE7); a = alu_lsr16(ss, a);         /* C09FE7 lsr A */
  SI(0x9FE8); a = alu_lsr16(ss, a);         /* C09FE8 lsr A */
  SI(0x9FE9); a = alu_lsr16(ss, a);         /* C09FE9 lsr A */
  SI(0x9FEA); ss_set_c(ss, false);          /* C09FEA clc */
  S(0x9FEB, 2);                             /* C09FEB adc $18 */
  a = alu_adc16(ss, a, t_read16(ss, (uint16_t) (dp + scratch_18)));
  S(0x9FED, 2);                             /* C09FED sta $18 */
  t_write16(ss, (uint16_t) (dp + scratch_18), a);
  SI(0x9FEF); a = y; ss_set_nz16(ss, a);    /* C09FEF tya */
  S(0x9FF0, 3); a = alu_and16(ss, a, 0x0018);  /* C09FF0 and #$0018 */
  SI(0x9FF3); a = alu_lsr16(ss, a);         /* C09FF3 lsr A */
  SI(0x9FF4); a = alu_lsr16(ss, a);         /* C09FF4 lsr A */
  S(0x9FF5, 2);                             /* C09FF5 adc tilemap_b_addr */
  a = alu_adc16(ss, a, t_read16(ss, (uint16_t) (dp + tilemap_b_addr)));
  S(0x9FF7, 2);                             /* C09FF7 sta $1C */
  t_write16(ss, (uint16_t) (dp + scratch_18 + 4), a);
  SI(0x9FF9); a = y; ss_set_nz16(ss, a);    /* C09FF9 tya */
  S(0x9FFA, 3); a = alu_and16(ss, a, 0x0018);  /* C09FFA and #$0018 */
  S(0x9FFD, 3); a = alu_eor16(ss, a, 0x0018);  /* C09FFD eor #$0018 */
  SI(0xA000); a = alu_lsr16(ss, a);         /* C0A000 lsr A */
  SI(0xA001); a = alu_lsr16(ss, a);         /* C0A001 lsr A */
  S(0xA002, 2);                             /* C0A002 adc tilemap_b_addr */
  a = alu_adc16(ss, a, t_read16(ss, (uint16_t) (dp + tilemap_b_addr)));
  S(0xA004, 2);                             /* C0A004 sta $1E */
  t_write16(ss, (uint16_t) (dp + scratch_18 + 6), a);

  S(0xA006, 1);                             /* C0A006 phk */
  ss_idle(ss);
  ss_check_int(ss);
  ss_push8(ss, pb);
  S(0xA007, 3); x = 0x06C0; ss_set_nz16(ss, x);  /* C0A007 ldx #$06C0 */

  for(;;) {
    /* loc_C0A00A */
    S(0xA00A, 2);                           /* C0A00A lda metatile_data_bank */
    a = t_read16(ss, (uint16_t) (dp + metatile_data_bank));
    ss_set_nz16(ss, a);
    S(0xA00C, 1);                           /* C0A00C pha */
    ss_idle(ss);
    ss_push8(ss, (uint8_t) (a >> 8));
    ss_check_int(ss);
    ss_push8(ss, (uint8_t) a);
    S(0xA00D, 1);                           /* C0A00D plb */
    ss_idle(ss); ss_idle(ss); ss_check_int(ss);
    { uint8_t b = ss_pull8(ss); ss_set_db(ss, b); ss_set_nz8(ss, b); }
    S(0xA00E, 1);                           /* C0A00E plb */
    ss_idle(ss); ss_idle(ss); ss_check_int(ss);
    { uint8_t b = ss_pull8(ss); ss_set_db(ss, b); ss_set_nz8(ss, b); }

    S(0xA00F, 2);                           /* C0A00F lda [$18] */
    a = t_read16(ss, t_idl_ptr(ss, (uint16_t) (dp + scratch_18)));
    ss_set_nz16(ss, a);
    const bool vflip = (a & 0x8000) != 0;
    S(0xA011, 1); t_branch(ss, vflip);      /* C0A011 bmi loc_C0A073 */

    if(!vflip) {
      S(0xA013, 3); alu_bit_imm16(ss, a, 0x4000);  /* C0A013 bit #$4000 */
      const bool hflip = !ss_z(ss);
      S(0xA016, 1); t_branch(ss, hflip);    /* C0A016 bne loc_C0A04B */
      if(!hflip) {
        SI(0xA018); a = alu_asl16(ss, a);   /* C0A018 asl A */
        SI(0xA019); a = alu_asl16(ss, a);   /* C0A019 asl A */
        SI(0xA01A); a = alu_asl16(ss, a);   /* C0A01A asl A */
        SI(0xA01B); a = alu_asl16(ss, a);   /* C0A01B asl A */
        SI(0xA01C); a = alu_asl16(ss, a);   /* C0A01C asl A */
        S(0xA01D, 2);                       /* C0A01D adc $1C */
        a = alu_adc16(ss, a, t_read16(ss, (uint16_t) (dp + scratch_18 + 4)));
        SI(0xA01F); y = a; ss_set_nz16(ss, y);  /* C0A01F tay */
        S(0xA020, 3); t_index(ss);          /* C0A020 lda nmi_handler_ptr,Y */
        a = t_read16(ss, ss_abs(ss, (uint16_t) (nmi_handler_ptr + y)));
        ss_set_nz16(ss, a);
        S(0xA023, 2); t_index(ss);          /* C0A023 sta nmi_handler_ptr,X */
        t_write16(ss, (uint16_t) (dp + nmi_handler_ptr + x), a);
        S(0xA025, 3); t_index(ss);          /* C0A025 lda $0008,Y */
        a = t_read16(ss, ss_abs(ss, (uint16_t) (0x0008 + y)));
        ss_set_nz16(ss, a);
        S(0xA028, 2); t_index(ss);          /* C0A028 sta dma_pending_mask,X */
        t_write16(ss, (uint16_t) (dp + dma_pending_mask + x), a);
        S(0xA02A, 3); t_index(ss);          /* C0A02A lda $0010,Y */
        a = t_read16(ss, ss_abs(ss, (uint16_t) (0x0010 + y)));
        ss_set_nz16(ss, a);
        S(0xA02D, 2); t_index(ss);          /* C0A02D sta ptr_04,X */
        t_write16(ss, (uint16_t) (dp + ptr_04 + x), a);
        S(0xA02F, 3); t_index(ss);          /* C0A02F lda $0018,Y */
        a = t_read16(ss, ss_abs(ss, (uint16_t) (0x0018 + y)));
        ss_set_nz16(ss, a);
      } else {
        /* loc_C0A04B */
        SI(0xA04B); a = alu_asl16(ss, a);   /* C0A04B asl A */
        SI(0xA04C); a = alu_asl16(ss, a);   /* C0A04C asl A */
        SI(0xA04D); a = alu_asl16(ss, a);   /* C0A04D asl A */
        SI(0xA04E); a = alu_asl16(ss, a);   /* C0A04E asl A */
        SI(0xA04F); a = alu_asl16(ss, a);   /* C0A04F asl A */
        S(0xA050, 2);                       /* C0A050 adc $1E */
        a = alu_adc16(ss, a, t_read16(ss, (uint16_t) (dp + scratch_18 + 6)));
        SI(0xA052); y = a; ss_set_nz16(ss, y);  /* C0A052 tay */
        S(0xA053, 3); t_index(ss);          /* C0A053 lda nmi_handler_ptr,Y */
        a = t_read16(ss, ss_abs(ss, (uint16_t) (nmi_handler_ptr + y)));
        ss_set_nz16(ss, a);
        S(0xA056, 3); a = alu_eor16(ss, a, 0x4000);  /* C0A056 eor #$4000 */
        S(0xA059, 2); t_index(ss);          /* C0A059 sta nmi_handler_ptr,X */
        t_write16(ss, (uint16_t) (dp + nmi_handler_ptr + x), a);
        S(0xA05B, 3); t_index(ss);          /* C0A05B lda $0008,Y */
        a = t_read16(ss, ss_abs(ss, (uint16_t) (0x0008 + y)));
        ss_set_nz16(ss, a);
        S(0xA05E, 3); a = alu_eor16(ss, a, 0x4000);  /* C0A05E eor #$4000 */
        S(0xA061, 2); t_index(ss);          /* C0A061 sta dma_pending_mask,X */
        t_write16(ss, (uint16_t) (dp + dma_pending_mask + x), a);
        S(0xA063, 3); t_index(ss);          /* C0A063 lda $0010,Y */
        a = t_read16(ss, ss_abs(ss, (uint16_t) (0x0010 + y)));
        ss_set_nz16(ss, a);
        S(0xA066, 3); a = alu_eor16(ss, a, 0x4000);  /* C0A066 eor #$4000 */
        S(0xA069, 2); t_index(ss);          /* C0A069 sta ptr_04,X */
        t_write16(ss, (uint16_t) (dp + ptr_04 + x), a);
        S(0xA06B, 3); t_index(ss);          /* C0A06B lda $0018,Y */
        a = t_read16(ss, ss_abs(ss, (uint16_t) (0x0018 + y)));
        ss_set_nz16(ss, a);
        S(0xA06E, 3); a = alu_eor16(ss, a, 0x4000);  /* C0A06E eor #$4000 */
        S(0xA071, 1); t_branch(ss, true);   /* C0A071 bra loc_C0A032 */
      }
    } else {
      /* loc_C0A073 */
      S(0xA073, 3); alu_bit_imm16(ss, a, 0x4000);  /* C0A073 bit #$4000 */
      const bool hflip = !ss_z(ss);
      S(0xA076, 1); t_branch(ss, hflip);    /* C0A076 bne loc_C0A0A0 */
      if(!hflip) {
        SI(0xA078); a = alu_asl16(ss, a);   /* C0A078 asl A */
        SI(0xA079); a = alu_asl16(ss, a);   /* C0A079 asl A */
        SI(0xA07A); a = alu_asl16(ss, a);   /* C0A07A asl A */
        SI(0xA07B); a = alu_asl16(ss, a);   /* C0A07B asl A */
        SI(0xA07C); a = alu_asl16(ss, a);   /* C0A07C asl A */
        S(0xA07D, 2);                       /* C0A07D adc $1C */
        a = alu_adc16(ss, a, t_read16(ss, (uint16_t) (dp + scratch_18 + 4)));
        SI(0xA07F); y = a; ss_set_nz16(ss, y);  /* C0A07F tay */
        S(0xA080, 3); t_index(ss);          /* C0A080 lda $0018,Y */
        a = t_read16(ss, ss_abs(ss, (uint16_t) (0x0018 + y)));
        ss_set_nz16(ss, a);
        S(0xA083, 3); a = alu_eor16(ss, a, 0x8000);  /* C0A083 eor #$8000 */
        S(0xA086, 2); t_index(ss);          /* C0A086 sta nmi_handler_ptr,X */
        t_write16(ss, (uint16_t) (dp + nmi_handler_ptr + x), a);
        S(0xA088, 3); t_index(ss);          /* C0A088 lda $0010,Y */
        a = t_read16(ss, ss_abs(ss, (uint16_t) (0x0010 + y)));
        ss_set_nz16(ss, a);
        S(0xA08B, 3); a = alu_eor16(ss, a, 0x8000);  /* C0A08B eor #$8000 */
        S(0xA08E, 2); t_index(ss);          /* C0A08E sta dma_pending_mask,X */
        t_write16(ss, (uint16_t) (dp + dma_pending_mask + x), a);
        S(0xA090, 3); t_index(ss);          /* C0A090 lda $0008,Y */
        a = t_read16(ss, ss_abs(ss, (uint16_t) (0x0008 + y)));
        ss_set_nz16(ss, a);
        S(0xA093, 3); a = alu_eor16(ss, a, 0x8000);  /* C0A093 eor #$8000 */
        S(0xA096, 2); t_index(ss);          /* C0A096 sta ptr_04,X */
        t_write16(ss, (uint16_t) (dp + ptr_04 + x), a);
        S(0xA098, 3); t_index(ss);          /* C0A098 lda nmi_handler_ptr,Y */
        a = t_read16(ss, ss_abs(ss, (uint16_t) (nmi_handler_ptr + y)));
        ss_set_nz16(ss, a);
        S(0xA09B, 3); a = alu_eor16(ss, a, 0x8000);  /* C0A09B eor #$8000 */
        S(0xA09E, 1); t_branch(ss, true);   /* C0A09E bra loc_C0A032 */
      } else {
        /* loc_C0A0A0 */
        SI(0xA0A0); a = alu_asl16(ss, a);   /* C0A0A0 asl A */
        SI(0xA0A1); a = alu_asl16(ss, a);   /* C0A0A1 asl A */
        SI(0xA0A2); a = alu_asl16(ss, a);   /* C0A0A2 asl A */
        SI(0xA0A3); a = alu_asl16(ss, a);   /* C0A0A3 asl A */
        SI(0xA0A4); a = alu_asl16(ss, a);   /* C0A0A4 asl A */
        S(0xA0A5, 2);                       /* C0A0A5 adc $1E */
        a = alu_adc16(ss, a, t_read16(ss, (uint16_t) (dp + scratch_18 + 6)));
        SI(0xA0A7); y = a; ss_set_nz16(ss, y);  /* C0A0A7 tay */
        S(0xA0A8, 3); t_index(ss);          /* C0A0A8 lda $0018,Y */
        a = t_read16(ss, ss_abs(ss, (uint16_t) (0x0018 + y)));
        ss_set_nz16(ss, a);
        S(0xA0AB, 3); a = alu_eor16(ss, a, 0xC000);  /* C0A0AB eor #$C000 */
        S(0xA0AE, 2); t_index(ss);          /* C0A0AE sta nmi_handler_ptr,X */
        t_write16(ss, (uint16_t) (dp + nmi_handler_ptr + x), a);
        S(0xA0B0, 3); t_index(ss);          /* C0A0B0 lda $0010,Y */
        a = t_read16(ss, ss_abs(ss, (uint16_t) (0x0010 + y)));
        ss_set_nz16(ss, a);
        S(0xA0B3, 3); a = alu_eor16(ss, a, 0xC000);  /* C0A0B3 eor #$C000 */
        S(0xA0B6, 2); t_index(ss);          /* C0A0B6 sta dma_pending_mask,X */
        t_write16(ss, (uint16_t) (dp + dma_pending_mask + x), a);
        S(0xA0B8, 3); t_index(ss);          /* C0A0B8 lda $0008,Y */
        a = t_read16(ss, ss_abs(ss, (uint16_t) (0x0008 + y)));
        ss_set_nz16(ss, a);
        S(0xA0BB, 3); a = alu_eor16(ss, a, 0xC000);  /* C0A0BB eor #$C000 */
        S(0xA0BE, 2); t_index(ss);          /* C0A0BE sta ptr_04,X */
        t_write16(ss, (uint16_t) (dp + ptr_04 + x), a);
        S(0xA0C0, 3); t_index(ss);          /* C0A0C0 lda nmi_handler_ptr,Y */
        a = t_read16(ss, ss_abs(ss, (uint16_t) (nmi_handler_ptr + y)));
        ss_set_nz16(ss, a);
        S(0xA0C3, 3); a = alu_eor16(ss, a, 0xC000);  /* C0A0C3 eor #$C000 */
        S(0xA0C6, 3);                       /* C0A0C6 jmp loc_C0A032 */
      }
    }

    /* loc_C0A032 */
    S(0xA032, 2); t_index(ss);              /* C0A032 sta $06,X */
    t_write16(ss, (uint16_t) (dp + ptr_04 + 2 + x), a);
    S(0xA034, 2);                           /* C0A034 lda $18 */
    a = t_read16(ss, (uint16_t) (dp + scratch_18));
    ss_set_nz16(ss, a);
    SI(0xA036); ss_set_c(ss, false);        /* C0A036 clc */
    S(0xA037, 3); a = alu_adc16(ss, a, 0x0002);  /* C0A037 adc #$0002 */
    S(0xA03A, 2);                           /* C0A03A sta $18 */
    t_write16(ss, (uint16_t) (dp + scratch_18), a);
    SI(0xA03C); a = x; ss_set_nz16(ss, a);  /* C0A03C txa */
    SI(0xA03D); ss_set_c(ss, false);        /* C0A03D clc */
    S(0xA03E, 3); a = alu_adc16(ss, a, 0x0008);  /* C0A03E adc #$0008 */
    SI(0xA041); x = a; ss_set_nz16(ss, x);  /* C0A041 tax */
    S(0xA042, 3); alu_cmp16(ss, a, 0x0708); /* C0A042 cmp #$0708 */
    const bool more = a != 0x0708;
    S(0xA045, 1); t_branch(ss, more);       /* C0A045 bne loc_C0A00A */
    if(!more) break;
  }

  S(0xA047, 1);                             /* C0A047 plb */
  ss_idle(ss); ss_idle(ss); ss_check_int(ss);
  { uint8_t b = ss_pull8(ss); ss_set_db(ss, b); ss_set_nz8(ss, b); }
  S(0xA048, 3);                             /* C0A048 jmp loc_C0A0C9 */

  /* loc_C0A0C9: copy the scratch column into the $0580 staging buffer */
  S(0xA0C9, 2);                             /* C0A0C9 lda camera_y */
  a = t_read16(ss, (uint16_t) (dp + camera_y));
  ss_set_nz16(ss, a);
  S(0xA0CB, 3); a = alu_and16(ss, a, 0x00F8);  /* C0A0CB and #$00F8 */
  SI(0xA0CE); a = alu_lsr16(ss, a);         /* C0A0CE lsr A */
  SI(0xA0CF); a = alu_lsr16(ss, a);         /* C0A0CF lsr A */
  SI(0xA0D0); y = a; ss_set_nz16(ss, y);    /* C0A0D0 tay */
  S(0xA0D1, 3); a = alu_and16(ss, a, 0x0006);  /* C0A0D1 and #$0006 */
  SI(0xA0D4); x = a; ss_set_nz16(ss, x);    /* C0A0D4 tax */
  SI(0xA0D5); ss_set_c(ss, false);          /* C0A0D5 clc */
  S(0xA0D6, 3); a = alu_adc16(ss, a, 0x0040);  /* C0A0D6 adc #$0040 */
  S(0xA0D9, 2);                             /* C0A0D9 sta $1C */
  t_write16(ss, (uint16_t) (dp + scratch_18 + 4), a);

  do {
    S(0xA0DB, 3); t_index(ss);              /* C0A0DB lda $06C0,X */
    a = t_read16(ss, ss_abs(ss, (uint16_t) (0x06C0 + x)));
    ss_set_nz16(ss, a);
    S(0xA0DE, 3); t_index(ss);              /* C0A0DE sta $0580,Y */
    t_write16(ss, ss_abs(ss, (uint16_t) (0x0580 + y)), a);
    SI(0xA0E1); a = y; ss_set_nz16(ss, a);  /* C0A0E1 tya */
    SI(0xA0E2); ss_set_c(ss, false);        /* C0A0E2 clc */
    S(0xA0E3, 3); a = alu_adc16(ss, a, 0x0002);  /* C0A0E3 adc #$0002 */
    S(0xA0E6, 3); a = alu_and16(ss, a, 0x003E);  /* C0A0E6 and #$003E */
    SI(0xA0E9); y = a; ss_set_nz16(ss, y);  /* C0A0E9 tay */
    SI(0xA0EA); x = (uint16_t) (x + 1); ss_set_nz16(ss, x);  /* C0A0EA inx */
    SI(0xA0EB); x = (uint16_t) (x + 1); ss_set_nz16(ss, x);  /* C0A0EB inx */
    S(0xA0EC, 2);                           /* C0A0EC cpx $1C */
    alu_cpx16(ss, x, t_read16(ss, (uint16_t) (dp + scratch_18 + 4)));
    S(0xA0EE, 1); t_branch(ss, !ss_z(ss));  /* C0A0EE bne loc_C0A0DB */
  } while(!ss_z(ss));

  ss_set_a(ss, a);
  ss_set_x(ss, x);
  ss_set_y(ss, y);
  S(0xA0F0, 1);                             /* C0A0F0 rts */
  ss_rts(ss);
}

/* ---------------------------------------------------------------------------
 * vram_upload_column_580 — $C0:A0F1
 *
 * DMAs the 64-byte $0580 column into the BG tilemap at VRAM $7800, at the
 * column the camera X (plus a screen when $98 says so) selects, with VMAIN set
 * to increment on the high byte ($81) so consecutive words land one tilemap row
 * apart; the second half of the 64-entry ring is reached by adding $03E0.
 * Exit: A low byte = $80, m = 0, the transfer armed and run.
 * ------------------------------------------------------------------------- */
void vram_upload_column_580(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint16_t dp = ss_dp(ss);

  SEP(0xA0F1, 0x20);                        /* C0A0F1 sep #$20 */
  S(0xA0F3, 2);                             /* C0A0F3 lda #$81 */
  a = (uint16_t) ((a & 0xff00) | 0x81);
  ss_set_nz8(ss, 0x81);
  S(0xA0F5, 3); t_write8(ss, ss_abs(ss, VMAIN), 0x81);  /* C0A0F5 sta VMAIN */
  REP(0xA0F8, 0x20);                        /* C0A0F8 rep #$20 */

  S(0xA0FA, 2);                             /* C0A0FA lda layer_parallax_mode */
  a = t_read16(ss, (uint16_t) (dp + layer_parallax_mode));
  ss_set_nz16(ss, a);
  const bool ahead = (a & 0x8000) == 0;
  S(0xA0FC, 1); t_branch(ss, ahead);        /* C0A0FC bpl loc_C0A102 */
  if(!ahead) {
    S(0xA0FE, 2);                           /* C0A0FE lda camera_x */
    a = t_read16(ss, (uint16_t) (dp + camera_x));
    ss_set_nz16(ss, a);
    S(0xA100, 1); t_branch(ss, true);       /* C0A100 bra loc_C0A108 */
  } else {
    S(0xA102, 2);                           /* C0A102 lda camera_x */
    a = t_read16(ss, (uint16_t) (dp + camera_x));
    ss_set_nz16(ss, a);
    SI(0xA104); ss_set_c(ss, false);        /* C0A104 clc */
    S(0xA105, 3); a = alu_adc16(ss, a, 0x0100);  /* C0A105 adc #$0100 */
  }

  /* loc_C0A108 */
  SI(0xA108); a = alu_lsr16(ss, a);         /* C0A108 lsr A */
  SI(0xA109); a = alu_lsr16(ss, a);         /* C0A109 lsr A */
  SI(0xA10A); a = alu_lsr16(ss, a);         /* C0A10A lsr A */
  S(0xA10B, 3); a = alu_and16(ss, a, 0x003F);  /* C0A10B and #$003F */
  S(0xA10E, 3); alu_bit_imm16(ss, a, 0x0020);  /* C0A10E bit #$0020 */
  const bool second = !ss_z(ss);
  SI(0xA111); ss_set_c(ss, false);          /* C0A111 clc */
  S(0xA112, 1); t_branch(ss, !second);      /* C0A112 beq loc_C0A117 */
  if(second) {
    S(0xA114, 3); a = alu_adc16(ss, a, 0x03E0);  /* C0A114 adc #$03E0 */
  }
  /* loc_C0A117 */
  S(0xA117, 3); a = alu_adc16(ss, a, 0x7800);  /* C0A117 adc #$7800 */
  S(0xA11A, 3); t_write16(ss, ss_abs(ss, VMADDL), a);  /* C0A11A sta VMADDL */
  S(0xA11D, 3); a = 0x0580; ss_set_nz16(ss, a);        /* C0A11D lda #$0580 */
  S(0xA120, 3); t_write16(ss, ss_abs(ss, A1TL0), a);   /* C0A120 sta A1TL0 */
  S(0xA123, 3); t_write16(ss, ss_abs(ss, A2AL0), a);   /* C0A123 sta A2AL0 */
  S(0xA126, 3); a = 0x0040; ss_set_nz16(ss, a);        /* C0A126 lda #$0040 */
  S(0xA129, 3); t_write16(ss, ss_abs(ss, DASL0), a);   /* C0A129 sta DASL0 */
  S(0xA12C, 3); a = 0x1801; ss_set_nz16(ss, a);        /* C0A12C lda #$1801 */
  S(0xA12F, 3); t_write16(ss, ss_abs(ss, DMAP0), a);   /* C0A12F sta DMAP0 */

  SEP(0xA132, 0x20);                        /* C0A132 sep #$20 */
  S(0xA134, 3); t_write8(ss, ss_abs(ss, A1B0), 0x00);  /* C0A134 stz A1B0 */
  S(0xA137, 2);                             /* C0A137 lda #$01 */
  a = (uint16_t) ((a & 0xff00) | 0x01);
  ss_set_nz8(ss, 0x01);
  S(0xA139, 3); t_write8(ss, ss_abs(ss, MDMAEN), 0x01);/* C0A139 sta MDMAEN */
  /* the transfer runs on the bus cycles of the instructions below, the ROM's */
  REP(0xA13C, 0x20);                        /* C0A13C rep #$20 */

  SEP(0xA13E, 0x20);                        /* C0A13E sep #$20 */
  S(0xA140, 2);                             /* C0A140 lda #$80 */
  a = (uint16_t) ((a & 0xff00) | 0x80);
  ss_set_nz8(ss, 0x80);
  S(0xA142, 3); t_write8(ss, ss_abs(ss, VMAIN), 0x80); /* C0A142 sta VMAIN */
  REP(0xA145, 0x20);                        /* C0A145 rep #$20 */

  ss_set_a(ss, a);
  S(0xA147, 1);                             /* C0A147 rts */
  ss_rts(ss);
}

/* ---------------------------------------------------------------------------
 * vram_upload_column_500 — $C0:A148
 *
 * DMAs the $0500 column as two 64-byte halves, to VRAM $7800 + the camera Y row
 * and to that address plus $400 (the second tilemap screen). The row comes from
 * camera_y, or from camera_y plus $E0 when camera_y_lookahead is non-negative,
 * the same choice build_metatile_column_500 made when it filled the buffer.
 * Exit: A low byte = $01, m = 0, both transfers armed and run.
 * ------------------------------------------------------------------------- */
void vram_upload_column_500(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint16_t dp = ss_dp(ss);

  S(0xA148, 2);                             /* C0A148 lda camera_y_lookahead */
  a = t_read16(ss, (uint16_t) (dp + camera_y_lookahead));
  ss_set_nz16(ss, a);
  const bool ahead = (a & 0x8000) == 0;
  S(0xA14A, 1); t_branch(ss, ahead);        /* C0A14A bpl loc_C0A150 */
  if(!ahead) {
    S(0xA14C, 2);                           /* C0A14C lda camera_y */
    a = t_read16(ss, (uint16_t) (dp + camera_y));
    ss_set_nz16(ss, a);
    S(0xA14E, 1); t_branch(ss, true);       /* C0A14E bra loc_C0A156 */
  } else {
    S(0xA150, 2);                           /* C0A150 lda camera_y */
    a = t_read16(ss, (uint16_t) (dp + camera_y));
    ss_set_nz16(ss, a);
    SI(0xA152); ss_set_c(ss, false);        /* C0A152 clc */
    S(0xA153, 3); a = alu_adc16(ss, a, 0x00E0);  /* C0A153 adc #$00E0 */
  }

  /* loc_C0A156 */
  SI(0xA156); a = alu_asl16(ss, a);         /* C0A156 asl A */
  SI(0xA157); a = alu_asl16(ss, a);         /* C0A157 asl A */
  S(0xA158, 3); a = alu_and16(ss, a, 0x03E0);  /* C0A158 and #$03E0 */
  SI(0xA15B); ss_set_c(ss, false);          /* C0A15B clc */
  S(0xA15C, 3); a = alu_adc16(ss, a, 0x7800);  /* C0A15C adc #$7800 */
  S(0xA15F, 2);                             /* C0A15F sta $18 */
  t_write16(ss, (uint16_t) (dp + scratch_18), a);
  S(0xA161, 3); t_write16(ss, ss_abs(ss, VMADDL), a);  /* C0A161 sta VMADDL */
  S(0xA164, 3); a = 0x0500; ss_set_nz16(ss, a);        /* C0A164 lda #$0500 */
  S(0xA167, 3); t_write16(ss, ss_abs(ss, A1TL0), a);   /* C0A167 sta A1TL0 */
  S(0xA16A, 3); t_write16(ss, ss_abs(ss, A2AL0), a);   /* C0A16A sta A2AL0 */
  S(0xA16D, 3); a = 0x0040; ss_set_nz16(ss, a);        /* C0A16D lda #$0040 */
  S(0xA170, 3); t_write16(ss, ss_abs(ss, DASL0), a);   /* C0A170 sta DASL0 */
  S(0xA173, 3); a = 0x1801; ss_set_nz16(ss, a);        /* C0A173 lda #$1801 */
  S(0xA176, 3); t_write16(ss, ss_abs(ss, DMAP0), a);   /* C0A176 sta DMAP0 */

  SEP(0xA179, 0x20);                        /* C0A179 sep #$20 */
  S(0xA17B, 3); t_write8(ss, ss_abs(ss, A1B0), 0x00);  /* C0A17B stz A1B0 */
  S(0xA17E, 2);                             /* C0A17E lda #$01 */
  a = (uint16_t) ((a & 0xff00) | 0x01);
  ss_set_nz8(ss, 0x01);
  S(0xA180, 3); t_write8(ss, ss_abs(ss, MDMAEN), 0x01);/* C0A180 sta MDMAEN */
  REP(0xA183, 0x20);                        /* C0A183 rep #$20 */

  S(0xA185, 2);                             /* C0A185 lda $18 */
  a = t_read16(ss, (uint16_t) (dp + scratch_18));
  ss_set_nz16(ss, a);
  SI(0xA187); ss_set_c(ss, false);          /* C0A187 clc */
  S(0xA188, 3); a = alu_adc16(ss, a, 0x0400);  /* C0A188 adc #$0400 */
  S(0xA18B, 3); t_write16(ss, ss_abs(ss, VMADDL), a);  /* C0A18B sta VMADDL */
  S(0xA18E, 3); a = 0x0540; ss_set_nz16(ss, a);        /* C0A18E lda #$0540 */
  S(0xA191, 3); t_write16(ss, ss_abs(ss, A1TL0), a);   /* C0A191 sta A1TL0 */
  S(0xA194, 3); t_write16(ss, ss_abs(ss, A2AL0), a);   /* C0A194 sta A2AL0 */
  S(0xA197, 3); a = 0x0040; ss_set_nz16(ss, a);        /* C0A197 lda #$0040 */
  S(0xA19A, 3); t_write16(ss, ss_abs(ss, DASL0), a);   /* C0A19A sta DASL0 */
  S(0xA19D, 3); a = 0x1801; ss_set_nz16(ss, a);        /* C0A19D lda #$1801 */
  S(0xA1A0, 3); t_write16(ss, ss_abs(ss, DMAP0), a);   /* C0A1A0 sta DMAP0 */

  SEP(0xA1A3, 0x20);                        /* C0A1A3 sep #$20 */
  S(0xA1A5, 3); t_write8(ss, ss_abs(ss, A1B0), 0x00);  /* C0A1A5 stz A1B0 */
  S(0xA1A8, 2);                             /* C0A1A8 lda #$01 */
  a = (uint16_t) ((a & 0xff00) | 0x01);
  ss_set_nz8(ss, 0x01);
  S(0xA1AA, 3); t_write8(ss, ss_abs(ss, MDMAEN), 0x01);/* C0A1AA sta MDMAEN */
  REP(0xA1AD, 0x20);                        /* C0A1AD rep #$20 */

  ss_set_a(ss, a);
  S(0xA1AF, 1);                             /* C0A1AF rts */
  ss_rts(ss);
}

static const RecompEntry kVramStream[] = {
  { 0xc09234, "vram_upload_shared_tileset_c5", vram_upload_shared_tileset_c5 },
  { 0xc09c16, "vram_generate_particle_tile", vram_generate_particle_tile },
  { 0xc09c28, "vram_write_tile_row_planes", vram_write_tile_row_planes },
  { 0xc09c62, "vram_stream_descriptor_dispatch", vram_stream_descriptor_dispatch },
  { 0xc09e83, "build_metatile_column_500", build_metatile_column_500 },
  { 0xc09fb7, "build_metatile_column_580", build_metatile_column_580 },
  { 0xc0a0f1, "vram_upload_column_580", vram_upload_column_580 },
  { 0xc0a148, "vram_upload_column_500", vram_upload_column_500 },
};
RECOMP_REGISTER(kVramStream)
