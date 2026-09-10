/* Per-mode scene setup and the mode-0 per-frame zone script, bank $C0.
 *
 * `game_mode_table` at $826A dispatches one of four init routines from the
 * scene-start path at $8070, which reset falls into and which the mode switch
 * at $8226 jumps back to: mode0_level_init ($8292), mode1_level_init ($84D7),
 * mode2_level_init ($8798) and title_screen_init ($88AB). All four have the
 * same shape, which docs/naming_proposals.md section 1 describes and
 * docs/data_formats.md fills in: program BGMODE/TM/CGWSEL/BG1SC/BG3SC/BG12NBA
 * for the scene, seed the metatile-map pointers and the level bounds in direct
 * page ($7A/$7C/$7E/$80/$82/$84/$86/$88/$98/$9A), walk the camera across the
 * level once so build_metatile_column_580 + vram_upload_column_580 fill the
 * tilemap, then DMA the tilesets, the palettes and the HDMA tables into place
 * through dma_upload_to_vram / dma_fill_vram_zero / dma_upload_to_cgram /
 * dma_setup_channel_step.
 *
 * The lockstep gate compares VRAM, CGRAM and OAM as well as WRAM, so the order
 * of the DMA channel programming and its interleaving with the column loop has
 * to be exactly the ROM's. It is: every register access goes through the timed
 * accessors and every body models the instruction stream, so arming a channel
 * costs the same bus cycles here as there, and the transfer runs on the same
 * instruction that ran it before.
 *
 * mode0_level_init has no rts of its own: its last instruction is the
 * `ldx #$0050` at $8489 and it falls straight into dma_setup_channel_step at
 * $848C, which supplies the return. The hook therefore just leaves the pc at
 * $848C, and the ROM -- or that routine's own hook -- picks it up.
 *
 * The two per-frame routines are the other half of the file. mode0_camera_zone_update is
 * jtbl_C0827A[0], the mode-0 entry of the per-mode update nmi_handler_gameplay
 * runs once a frame at $815B -- that handler resets the stack at $80F4 and
 * drives the whole frame, so the game's main loop *is* the NMI path. It is the
 * head of the camera-X-driven zone state machine that continues past $8BDB, so
 * every one of its exits is a jmp or a fall-through into that continuation
 * rather than an rts. cgram_palette_ramp_step is called from the same handler at $81B7
 * whenever $0C1B is non-zero: it walks $0C1D one step in the direction $0C1B's
 * sign gives and queues the palette bank that step selects for
 * cgram_upload_queue_flush.
 */
#include <stdint.h>
#include <stdbool.h>

#include "snes_state.h"
#include "dream_ram.h"
#include "dream_alu.h"
#include "dream_time.h"

/* ---- names dream_ram.h does not carry yet -------------------------------- */
/* PPU registers only this file writes. */
#define BGMODE    0x2105
#define BG1SC     0x2107
#define BG3SC     0x2109
#define BG12NBA   0x210B
#define TM        0x212C
#define CGWSEL    0x2130
#define COLDATA   0x2132

/* DMA channels 1..7. Channel 0 is dream_ram.h's; the mode inits program the
 * rest by hand for the HDMA tables they have just built in $7F00xx/$7F05xx. */
#define DMAP1  0x4310
#define A1TL1  0x4312
#define A1B1   0x4314
#define DASB1  0x4317
#define DMAP2  0x4320
#define A1TL2  0x4322
#define A1B2   0x4324
#define DASB2  0x4327
#define DMAP3  0x4330
#define A1TL3  0x4332
#define A1B3   0x4334
#define DASB3  0x4337
#define DMAP4  0x4340
#define A1TL4  0x4342
#define A1B4   0x4344
#define DASB4  0x4347
#define DMAP5  0x4350
#define A1TL5  0x4352
#define A1B5   0x4354
#define DASB5  0x4357
#define DMAP6  0x4360
#define A1TL6  0x4362
#define A1B6   0x4364
#define DASB6  0x4367
#define DMAP7  0x4370
#define A1TL7  0x4372
#define A1B7   0x4374

/* RAM the listing still prints as a bare address. $0BAC is the mode-1 row
 * offset entities_ai.c already names; the $0Bxx/$0Cxx words below are the zone
 * script's own state, described in docs/naming_proposals.md section 5. */
#ifndef state_row_offset
#define state_row_offset  0x0BAC
#endif
#define layer2_parallax_shift 0x0082   /* $82/$84: build_metatile_column_500 inputs */
#define layer2_row_height     0x0084
#define scroll_scratch_60     0x0060
#define zone_settle_timer     0x0078   /* $78: counts down while a zone settles */
#define scratch_1A            0x001A
#define scratch_20            0x0020
#define walk_cycle_parity     0x0072

/* Callees the mode inits reach with jsr; they run on the reference CPU through
 * t_call_sub, so a converted one still fires its own hook and is credited. */
#define set_bg_scroll_prep_addr         0xA4A6
#define set_bg_scroll_addr              0xA4A8
#define build_metatile_column_580_addr  0x9FB7
#define vram_upload_column_580_addr     0xA0F1
#define dma_fill_vram_zero_addr         0xA445
#define dma_upload_to_vram_addr         0xA46A
#define dma_upload_to_cgram_addr        0xA483
#define dma_setup_channel_step_addr     0x848C
#define particle_table_clear_addr       0x9246
#define particle_spawn_from_table_addr  0x95E3
#define particle_spawn_random_addr      0x9781
#define sparkle_array_init_addr         0x94E4

/* ---- 65816 shapes dream_time.h does not cover --------------------------- */
/* push/pull with the interrupt latch the 65816 takes between the two bytes */
static void t_push16(SnesState* ss, uint16_t v) {
  ss_push8(ss, (uint8_t) (v >> 8));
  ss_check_int(ss);
  ss_push8(ss, (uint8_t) v);
}

/* The second half of `jsr abs`: the caller's S() has already fetched the opcode
 * and the two address bytes (cpu_adrAbs takes no latch between them), so what
 * is left is the internal cycle, the return-address push and the transfer of
 * control. The callee then runs on the reference CPU, which is what keeps a
 * converted callee converted -- its own hook fires from inside this loop. The
 * frame pushed here is the routine's real return address, so the callee can be
 * left running when the machine moves on underneath the hook: true is returned
 * then and the ROM finishes both the callee and the caller. */
static bool t_call_sub(SnesState* ss, uint8_t bank, uint16_t target) {
  const uint16_t sp0 = ss_sp(ss);
  const uint16_t ret = (uint16_t) (ss_pc(ss) - 1);   /* rts adds 1 */
  ss_idle(ss);
  t_push16(ss, ret);
  ss_set_pc(ss, bank, target);
  return ss_run_callee(ss, sp0);
}

/* `jmp abs` is the one absolute operand the core fetches with an interrupt
 * latch between its two bytes (cpu_readOpcodeWord(.., true) in case 0x4c), so
 * it cannot use the plain three-byte step. Returns true when it yielded. */
static bool t_jmp_abs(SnesState* ss, uint8_t pb, uint16_t addr, uint16_t target,
                      uint16_t a, uint16_t x, uint16_t y) {
  if(t_step(ss, pb, addr, 2, a, x, y)) return true;
  ss_check_int(ss);
  ss_fetch(ss, 1);
  ss_set_pc(ss, pb, target);
  return false;
}

#define JSR(addr, target) do {                                          \
    S((addr), 3);                                                       \
    if(t_call_sub(ss, pb, (uint16_t) (target))) return;                 \
    a = ss_a(ss); x = ss_x(ss); y = ss_y(ss);                           \
  } while(0)

#define JMP(addr, target) do {                                          \
    if(t_jmp_abs(ss, pb, (uint16_t) (addr), (uint16_t) (target), a, x, y)) return; \
  } while(0)

/* Hand the routine over at `to`, an address past the end of this hook's own
 * block: everything the 65816 would have left is already in place. */
#define HANDOFF(to) do {                                                \
    ss_set_a(ss, a); ss_set_x(ss, x); ss_set_y(ss, y);                  \
    ss_set_pc(ss, pb, (uint16_t) (to));                                 \
    return;                                                             \
  } while(0)

/* ---------------------------------------------------------------------------
 * mode0_level_init — $C0:8292
 *
 * game_mode_table[0]. BGMODE 1, TM $1417, CGWSEL $8202; the map lives at
 * $CA:4860 with its definitions at $C9:DCE0, the level is $0DFF by $011F, and
 * the camera walks $0000..$00F8 in steps of 8 laying down the tilemap before
 * the tileset and palette DMAs. The tail builds two HDMA tables ($7F00D0..D9
 * and $7F0540..49), clears the $0C31 window buffer and programs channels 1..5
 * for them.
 *
 * It has no rts: `ldx #$0050` at $8489 is the last instruction and the routine
 * falls into dma_setup_channel_step at $848C, whose rts returns to
 * `jsr (game_mode_table,X)`. The hook leaves the pc there and returns.
 * ------------------------------------------------------------------------- */
void mode0_level_init(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint16_t dp = ss_dp(ss);

  S(0x8292, 3);                             /* C08292 stz $0BAC */
  t_write16(ss, ss_abs(ss, state_row_offset), 0);
  S(0x8295, 3); a = 0x0001; ss_set_nz16(ss, a);   /* C08295 lda #$0001 */
  S(0x8298, 3); t_write16(ss, ss_abs(ss, BGMODE), a);   /* C08298 sta BGMODE */
  S(0x829B, 3); a = 0x1417; ss_set_nz16(ss, a);   /* C0829B lda #$1417 */
  S(0x829E, 3); t_write16(ss, ss_abs(ss, TM), a);       /* C0829E sta TM */
  S(0x82A1, 3); a = 0x8202; ss_set_nz16(ss, a);   /* C082A1 lda #$8202 */
  S(0x82A4, 3); t_write16(ss, ss_abs(ss, CGWSEL), a);   /* C082A4 sta CGWSEL */
  S(0x82A7, 3); a = 0x0525; ss_set_nz16(ss, a);   /* C082A7 lda #$0525 */
  S(0x82AA, 3); t_write16(ss, ss_abs(ss, BG12NBA), a);  /* C082AA sta BG12NBA */
  S(0x82AD, 3); a = 0x7969; ss_set_nz16(ss, a);   /* C082AD lda #$7969 */
  S(0x82B0, 3); t_write16(ss, ss_abs(ss, BG1SC), a);    /* C082B0 sta BG1SC */
  S(0x82B3, 3); a = 0x001C; ss_set_nz16(ss, a);   /* C082B3 lda #$001C */
  S(0x82B6, 3); t_write16(ss, ss_abs(ss, BG3SC), a);    /* C082B6 sta BG3SC */
  JSR(0x82B9, set_bg_scroll_prep_addr);     /* C082B9 jsr set_bg_scroll_prep */

  S(0x82BC, 3); a = 0x0DFF; ss_set_nz16(ss, a);   /* C082BC lda #$0DFF */
  S(0x82BF, 2);                             /* C082BF sta level_width_mask */
  t_write16(ss, (uint16_t) (dp + level_width_mask), a);
  S(0x82C1, 3); a = 0x011F; ss_set_nz16(ss, a);   /* C082C1 lda #$011F */
  S(0x82C4, 2);                             /* C082C4 sta level_height_mask */
  t_write16(ss, (uint16_t) (dp + level_height_mask), a);
  S(0x82C6, 2);                             /* C082C6 stz $82 */
  t_write16(ss, (uint16_t) (dp + layer2_parallax_shift), 0);
  S(0x82C8, 3); a = 0x0020; ss_set_nz16(ss, a);   /* C082C8 lda #$0020 */
  S(0x82CB, 2);                             /* C082CB sta $84 */
  t_write16(ss, (uint16_t) (dp + layer2_row_height), a);
  S(0x82CD, 3); a = 0x4860; ss_set_nz16(ss, a);   /* C082CD lda #$4860 */
  S(0x82D0, 2);                             /* C082D0 sta tilemap_a_addr */
  t_write16(ss, (uint16_t) (dp + tilemap_a_addr), a);
  S(0x82D2, 3); a = 0xCACA; ss_set_nz16(ss, a);   /* C082D2 lda #$CACA */
  S(0x82D5, 2);                             /* C082D5 sta tilemap_a_bank */
  t_write16(ss, (uint16_t) (dp + tilemap_a_bank), a);
  S(0x82D7, 3); a = 0xDCE0; ss_set_nz16(ss, a);   /* C082D7 lda #$DCE0 */
  S(0x82DA, 2);                             /* C082DA sta tilemap_b_addr */
  t_write16(ss, (uint16_t) (dp + tilemap_b_addr), a);
  S(0x82DC, 3); a = 0xC9C9; ss_set_nz16(ss, a);   /* C082DC lda #$C9C9 */
  S(0x82DF, 2);                             /* C082DF sta metatile_data_bank */
  t_write16(ss, (uint16_t) (dp + metatile_data_bank), a);
  S(0x82E1, 3); a = 0xFFFF; ss_set_nz16(ss, a);   /* C082E1 lda #$FFFF */
  S(0x82E4, 2);                             /* C082E4 sta layer_parallax_mode */
  t_write16(ss, (uint16_t) (dp + layer_parallax_mode), a);
  S(0x82E6, 2);                             /* C082E6 stz $60 */
  t_write16(ss, (uint16_t) (dp + scroll_scratch_60), 0);
  S(0x82E8, 3); a = 0x0048; ss_set_nz16(ss, a);   /* C082E8 lda #$0048 */
  S(0x82EB, 2);                             /* C082EB sta camera_y */
  t_write16(ss, (uint16_t) (dp + camera_y), a);
  S(0x82ED, 3); a = 0x0000; ss_set_nz16(ss, a);   /* C082ED lda #$0000 */

  for(;;) {                                 /* loc_C082F0 */
    S(0x82F0, 2);                           /* C082F0 sta camera_x */
    t_write16(ss, (uint16_t) (dp + camera_x), a);
    JSR(0x82F2, build_metatile_column_580_addr);  /* C082F2 jsr build_metatile_column_580 */
    JSR(0x82F5, vram_upload_column_580_addr);     /* C082F5 jsr vram_upload_column_580 */
    S(0x82F8, 2);                           /* C082F8 lda camera_x */
    a = t_read16(ss, (uint16_t) (dp + camera_x));
    ss_set_nz16(ss, a);
    SI(0x82FA); ss_set_c(ss, false);        /* C082FA clc */
    S(0x82FB, 3); a = alu_adc16(ss, a, 0x0008);   /* C082FB adc #$0008 */
    S(0x82FE, 3); alu_cmp16(ss, a, 0x0100); /* C082FE cmp #$0100 */
    const bool again = !ss_z(ss);
    S(0x8301, 1); t_branch(ss, again);      /* C08301 bne loc_C082F0 */
    if(!again) break;
  }

  S(0x8303, 2);                             /* C08303 sta camera_x */
  t_write16(ss, (uint16_t) (dp + camera_x), a);

  S(0x8305, 3); a = 0x1600; ss_set_nz16(ss, a);   /* C08305 lda #$1600 */
  S(0x8308, 3); t_write16(ss, ss_abs(ss, VMADDL), a);  /* C08308 sta VMADDL */
  S(0x830B, 3); x = 0x00C5; ss_set_nz16(ss, x);   /* C0830B ldx #$00C5 */
  S(0x830E, 3); a = 0x02C0; ss_set_nz16(ss, a);   /* C0830E lda #$02C0 */
  S(0x8311, 3); y = 0x0C00; ss_set_nz16(ss, y);   /* C08311 ldy #$0C00 */
  JSR(0x8314, dma_upload_to_vram_addr);     /* C08314 jsr dma_upload_to_vram */

  S(0x8317, 3); a = 0x1C00; ss_set_nz16(ss, a);   /* C08317 lda #$1C00 */
  S(0x831A, 3); t_write16(ss, ss_abs(ss, VMADDL), a);  /* C0831A sta VMADDL */
  S(0x831D, 3); x = 0x00CA; ss_set_nz16(ss, x);   /* C0831D ldx #$00CA */
  S(0x8320, 3); a = 0xE38E; ss_set_nz16(ss, a);   /* C08320 lda #$E38E */
  S(0x8323, 3); y = 0x0800; ss_set_nz16(ss, y);   /* C08323 ldy #$0800 */
  JSR(0x8326, dma_upload_to_vram_addr);     /* C08326 jsr dma_upload_to_vram */

  S(0x8329, 3); a = 0x2000; ss_set_nz16(ss, a);   /* C08329 lda #$2000 */
  S(0x832C, 3); t_write16(ss, ss_abs(ss, VMADDL), a);  /* C0832C sta VMADDL */
  S(0x832F, 3); x = 0x00C9; ss_set_nz16(ss, x);   /* C0832F ldx #$00C9 */
  S(0x8332, 3); a = 0x0000; ss_set_nz16(ss, a);   /* C08332 lda #$0000 */
  S(0x8335, 3); y = 0x5AC0; ss_set_nz16(ss, y);   /* C08335 ldy #$5AC0 */
  JSR(0x8338, dma_upload_to_vram_addr);     /* C08338 jsr dma_upload_to_vram */

  S(0x833B, 3); a = 0x5000; ss_set_nz16(ss, a);   /* C0833B lda #$5000 */
  S(0x833E, 3); t_write16(ss, ss_abs(ss, VMADDL), a);  /* C0833E sta VMADDL */
  S(0x8341, 3); x = 0x00C9; ss_set_nz16(ss, x);   /* C08341 ldx #$00C9 */
  S(0x8344, 3); a = 0x8AC0; ss_set_nz16(ss, a);   /* C08344 lda #$8AC0 */
  S(0x8347, 3); y = 0x2AA0; ss_set_nz16(ss, y);   /* C08347 ldy #$2AA0 */
  JSR(0x834A, dma_upload_to_vram_addr);     /* C0834A jsr dma_upload_to_vram */

  S(0x834D, 3); a = 0x6800; ss_set_nz16(ss, a);   /* C0834D lda #$6800 */
  S(0x8350, 3); t_write16(ss, ss_abs(ss, VMADDL), a);  /* C08350 sta VMADDL */
  S(0x8353, 3); x = 0x00CA; ss_set_nz16(ss, x);   /* C08353 ldx #$00CA */
  S(0x8356, 3); a = 0xF38E; ss_set_nz16(ss, a);   /* C08356 lda #$F38E */
  S(0x8359, 3); y = 0x0800; ss_set_nz16(ss, y);   /* C08359 ldy #$0800 */
  JSR(0x835C, dma_upload_to_vram_addr);     /* C0835C jsr dma_upload_to_vram */

  S(0x835F, 3); a = 0x6C00; ss_set_nz16(ss, a);   /* C0835F lda #$6C00 */
  JSR(0x8362, dma_fill_vram_zero_addr);     /* C08362 jsr dma_fill_vram_zero */

  S(0x8365, 3); a = 0x7000; ss_set_nz16(ss, a);   /* C08365 lda #$7000 */
  S(0x8368, 3); t_write16(ss, ss_abs(ss, VMADDL), a);  /* C08368 sta VMADDL */
  S(0x836B, 3); x = 0x00CA; ss_set_nz16(ss, x);   /* C0836B ldx #$00CA */
  S(0x836E, 3); a = 0xEB8E; ss_set_nz16(ss, a);   /* C0836E lda #$EB8E */
  S(0x8371, 3); y = 0x0800; ss_set_nz16(ss, y);   /* C08371 ldy #$0800 */
  JSR(0x8374, dma_upload_to_vram_addr);     /* C08374 jsr dma_upload_to_vram */

  S(0x8377, 3); a = 0x7400; ss_set_nz16(ss, a);   /* C08377 lda #$7400 */
  JSR(0x837A, dma_fill_vram_zero_addr);     /* C0837A jsr dma_fill_vram_zero */

  S(0x837D, 3); y = 0x0000; ss_set_nz16(ss, y);   /* C0837D ldy #$0000 */
  S(0x8380, 3); x = 0x0020; ss_set_nz16(ss, x);   /* C08380 ldx #$0020 */
  S(0x8383, 3); a = 0x6DA8; ss_set_nz16(ss, a);   /* C08383 lda #$6DA8 */
  JSR(0x8386, dma_upload_to_cgram_addr);    /* C08386 jsr dma_upload_to_cgram */

  S(0x8389, 3); y = 0x0080; ss_set_nz16(ss, y);   /* C08389 ldy #$0080 */
  S(0x838C, 3); x = 0x0020; ss_set_nz16(ss, x);   /* C0838C ldx #$0020 */
  S(0x838F, 3); a = 0x6C48; ss_set_nz16(ss, a);   /* C0838F lda #$6C48 */
  JSR(0x8392, dma_upload_to_cgram_addr);    /* C08392 jsr dma_upload_to_cgram */

  S(0x8395, 3); y = 0x00E0; ss_set_nz16(ss, y);   /* C08395 ldy #$00E0 */
  S(0x8398, 3); x = 0x0004; ss_set_nz16(ss, x);   /* C08398 ldx #$0004 */
  S(0x839B, 3); a = 0x6D68; ss_set_nz16(ss, a);   /* C0839B lda #$6D68 */
  JSR(0x839E, dma_upload_to_cgram_addr);    /* C0839E jsr dma_upload_to_cgram */

  S(0x83A1, 3); y = 0x00F0; ss_set_nz16(ss, y);   /* C083A1 ldy #$00F0 */
  S(0x83A4, 3); x = 0x0004; ss_set_nz16(ss, x);   /* C083A4 ldx #$0004 */
  S(0x83A7, 3); a = 0x6D48; ss_set_nz16(ss, a);   /* C083A7 lda #$6D48 */
  JSR(0x83AA, dma_upload_to_cgram_addr);    /* C083AA jsr dma_upload_to_cgram */

  JSR(0x83AD, particle_table_clear_addr);   /* C083AD jsr particle_table_clear */

  S(0x83B0, 3); a = 0x1016; ss_set_nz16(ss, a);   /* C083B0 lda #$1016 */
  S(0x83B3, 3); t_write16(ss, ss_abs(ss, 0x0BD4), a); /* C083B3 sta $0BD4 */
  SI(0x83B6); a = alu_inc16(ss, a);         /* C083B6 inc A */
  S(0x83B7, 3); t_write16(ss, ss_abs(ss, 0x0BD6), a); /* C083B7 sta $0BD6 */
  S(0x83BA, 3); t_write16(ss, ss_abs(ss, 0x0BD2), 0); /* C083BA stz $0BD2 */

  S(0x83BD, 3); a = 0x0080; ss_set_nz16(ss, a);   /* C083BD lda #$0080 */
  S(0x83C0, 2); t_write16(ss, (uint16_t) (dp + ptr_04), a);  /* C083C0 sta ptr_04 */
  S(0x83C2, 3); a = 0x84A2; ss_set_nz16(ss, a);   /* C083C2 lda #$84A2 */
  S(0x83C5, 3); y = 0x2C41; ss_set_nz16(ss, y);   /* C083C5 ldy #$2C41 */
  S(0x83C8, 3); x = 0x0010; ss_set_nz16(ss, x);   /* C083C8 ldx #$0010 */
  JSR(0x83CB, dma_setup_channel_step_addr); /* C083CB jsr dma_setup_channel_step */

  S(0x83CE, 3); a = 0x0080; ss_set_nz16(ss, a);   /* C083CE lda #$0080 */
  S(0x83D1, 2); t_write16(ss, (uint16_t) (dp + ptr_04), a);  /* C083D1 sta ptr_04 */
  S(0x83D3, 3); a = 0x84A9; ss_set_nz16(ss, a);   /* C083D3 lda #$84A9 */
  S(0x83D6, 3); y = 0x1143; ss_set_nz16(ss, y);   /* C083D6 ldy #$1143 */
  S(0x83D9, 3); x = 0x0020; ss_set_nz16(ss, x);   /* C083D9 ldx #$0020 */
  JSR(0x83DC, dma_setup_channel_step_addr); /* C083DC jsr dma_setup_channel_step */

  S(0x83DF, 3); a = 0x0080; ss_set_nz16(ss, a);   /* C083DF lda #$0080 */
  S(0x83E2, 2); t_write16(ss, (uint16_t) (dp + ptr_04), a);  /* C083E2 sta ptr_04 */
  S(0x83E4, 3); a = 0x84B0; ss_set_nz16(ss, a);   /* C083E4 lda #$84B0 */
  S(0x83E7, 3); y = 0x0900; ss_set_nz16(ss, y);   /* C083E7 ldy #$0900 */
  S(0x83EA, 3); x = 0x0030; ss_set_nz16(ss, x);   /* C083EA ldx #$0030 */
  JSR(0x83ED, dma_setup_channel_step_addr); /* C083ED jsr dma_setup_channel_step */

  S(0x83F0, 3); a = 0x7F00; ss_set_nz16(ss, a);   /* C083F0 lda #$7F00 */
  S(0x83F3, 3); t_write16(ss, ss_abs(ss, 0x0C00), a); /* C083F3 sta $0C00 */
  S(0x83F6, 3); a = 0x007F; ss_set_nz16(ss, a);   /* C083F6 lda #$007F */
  S(0x83F9, 4); t_write16(ss, 0x7F00D0, a); /* C083F9 sta $7F00D0 */
  S(0x83FD, 3); a = 0x003D; ss_set_nz16(ss, a);   /* C083FD lda #$003D */
  S(0x8400, 4); t_write16(ss, 0x7F00D3, a); /* C08400 sta $7F00D3 */
  S(0x8404, 3); a = 0x0001; ss_set_nz16(ss, a);   /* C08404 lda #$0001 */
  S(0x8407, 4); t_write16(ss, 0x7F00D6, a); /* C08407 sta $7F00D6 */
  S(0x840B, 3); a = 0x0BF8; ss_set_nz16(ss, a);   /* C0840B lda #$0BF8 */
  S(0x840E, 4); t_write16(ss, 0x7F00D1, a); /* C0840E sta $7F00D1 */
  S(0x8412, 4); t_write16(ss, 0x7F00D4, a); /* C08412 sta $7F00D4 */
  SI(0x8416); a = alu_inc16(ss, a);         /* C08416 inc A */
  S(0x8417, 4); t_write16(ss, 0x7F00D7, a); /* C08417 sta $7F00D7 */
  SI(0x841B); a = ss_dp(ss); ss_set_nz16(ss, a);  /* C0841B tdc */
  S(0x841C, 4); t_write16(ss, 0x7F00D9, a); /* C0841C sta $7F00D9 */
  S(0x8420, 4);                             /* C08420 lda $7F00D3 */
  a = t_read16(ss, 0x7F00D3); ss_set_nz16(ss, a);
  S(0x8424, 3); t_write16(ss, ss_abs(ss, 0x0C02), a); /* C08424 sta $0C02 */
  S(0x8427, 3); a = 0x6969; ss_set_nz16(ss, a);   /* C08427 lda #$6969 */
  S(0x842A, 3); t_write16(ss, ss_abs(ss, 0x0BF8), a); /* C0842A sta $0BF8 */

  S(0x842D, 3); a = 0x007F; ss_set_nz16(ss, a);   /* C0842D lda #$007F */
  S(0x8430, 2); t_write16(ss, (uint16_t) (dp + ptr_04), a);  /* C08430 sta ptr_04 */
  S(0x8432, 3); a = 0x00D0; ss_set_nz16(ss, a);   /* C08432 lda #$00D0 */
  S(0x8435, 3); y = 0x0740; ss_set_nz16(ss, y);   /* C08435 ldy #$0740 */
  S(0x8438, 3); x = 0x0040; ss_set_nz16(ss, x);   /* C08438 ldx #$0040 */
  JSR(0x843B, dma_setup_channel_step_addr); /* C0843B jsr dma_setup_channel_step */

  S(0x843E, 3); a = 0xFF00; ss_set_nz16(ss, a);   /* C0843E lda #$FF00 */
  S(0x8441, 3); t_write16(ss, ss_abs(ss, 0x0C04), a); /* C08441 sta $0C04 */
  S(0x8444, 3); a = 0x00FF; ss_set_nz16(ss, a);   /* C08444 lda #$00FF */
  S(0x8447, 4); t_write16(ss, 0x7F0540, a); /* C08447 sta $7F0540 */
  S(0x844B, 3); a = 0x003D; ss_set_nz16(ss, a);   /* C0844B lda #$003D */
  S(0x844E, 4); t_write16(ss, 0x7F0543, a); /* C0844E sta $7F0543 */
  S(0x8452, 3); a = 0x0001; ss_set_nz16(ss, a);   /* C08452 lda #$0001 */
  S(0x8455, 4); t_write16(ss, 0x7F0546, a); /* C08455 sta $7F0546 */
  S(0x8459, 3); a = 0x0C31; ss_set_nz16(ss, a);   /* C08459 lda #$0C31 */
  S(0x845C, 4); t_write16(ss, 0x7F0541, a); /* C0845C sta $7F0541 */
  S(0x8460, 3); a = 0x0D2F; ss_set_nz16(ss, a);   /* C08460 lda #$0D2F */
  S(0x8463, 4); t_write16(ss, 0x7F0544, a); /* C08463 sta $7F0544 */
  S(0x8467, 3); a = 0x0DB5; ss_set_nz16(ss, a);   /* C08467 lda #$0DB5 */
  S(0x846A, 4); t_write16(ss, 0x7F0547, a); /* C0846A sta $7F0547 */
  SI(0x846E); a = ss_dp(ss); ss_set_nz16(ss, a);  /* C0846E tdc */
  S(0x846F, 4); t_write16(ss, 0x7F0549, a); /* C0846F sta $7F0549 */

  S(0x8473, 3); x = 0x0186; ss_set_nz16(ss, x);   /* C08473 ldx #$0186 */
  SI(0x8476); a = ss_dp(ss); ss_set_nz16(ss, a);  /* C08476 tdc */
  for(;;) {                                 /* loc_C08477 */
    S(0x8477, 3); t_index(ss);              /* C08477 sta $0C31,X */
    t_write16(ss, ss_abs(ss, (uint16_t) (0x0C31 + x)), a);
    SI(0x847A); x = alu_dec16(ss, x);       /* C0847A dex */
    SI(0x847B); x = alu_dec16(ss, x);       /* C0847B dex */
    const bool again = (x & 0x8000) == 0;
    S(0x847C, 1); t_branch(ss, again);      /* C0847C bpl loc_C08477 */
    if(!again) break;
  }

  S(0x847E, 3); a = 0x007F; ss_set_nz16(ss, a);   /* C0847E lda #$007F */
  S(0x8481, 2); t_write16(ss, (uint16_t) (dp + ptr_04), a);  /* C08481 sta ptr_04 */
  S(0x8483, 3); a = 0x0540; ss_set_nz16(ss, a);   /* C08483 lda #$0540 */
  S(0x8486, 3); y = 0x0D42; ss_set_nz16(ss, y);   /* C08486 ldy #$0D42 */
  S(0x8489, 3); x = 0x0050; ss_set_nz16(ss, x);   /* C08489 ldx #$0050 */

  /* falls through into dma_setup_channel_step at $C0:848C, which supplies the
   * rts; the pc is already there. */
  HANDOFF(dma_setup_channel_step_addr);
}

/* ---------------------------------------------------------------------------
 * mode1_level_init — $C0:84D7
 *
 * game_mode_table[1], the one mode that runs with $0BAC = $0018 (the row offset
 * entity_update_tick adds to entity_state, docs/handler_tables.md). CGWSEL
 * $2202 and three COLDATA writes set up the colour-maths backdrop, the map is
 * at $C9:FD80 with definitions at $CA:26A0, and the camera walks $0300..$03F8.
 *
 * The tail is the longest DMA block in the file: it builds the HDMA tables in
 * $7F0000..$7F008F and $7F00C0..$7F0906 by hand and then programs channels
 * 1..7 with a 8-bit accumulator and 16-bit index registers, so every DMAPn /
 * A1TLn store is `stx` writing two bytes and every bank store is an 8-bit
 * `sta`. Exit: 8-bit A = the second CGDATA byte, m widened again by the closing
 * rep #$20.
 * ------------------------------------------------------------------------- */
void mode1_level_init(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint16_t dp = ss_dp(ss);

  S(0x84D7, 3); a = 0x0018; ss_set_nz16(ss, a);   /* C084D7 lda #$0018 */
  S(0x84DA, 3);                             /* C084DA sta $0BAC */
  t_write16(ss, ss_abs(ss, state_row_offset), a);
  S(0x84DD, 3); a = 0x0001; ss_set_nz16(ss, a);   /* C084DD lda #$0001 */
  S(0x84E0, 3); t_write16(ss, ss_abs(ss, BGMODE), a);   /* C084E0 sta BGMODE */
  S(0x84E3, 3); a = 0x1417; ss_set_nz16(ss, a);   /* C084E3 lda #$1417 */
  S(0x84E6, 3); t_write16(ss, ss_abs(ss, TM), a);       /* C084E6 sta TM */
  S(0x84E9, 3); a = 0x2202; ss_set_nz16(ss, a);   /* C084E9 lda #$2202 */
  S(0x84EC, 3); t_write16(ss, ss_abs(ss, CGWSEL), a);   /* C084EC sta CGWSEL */
  S(0x84EF, 3); a = 0x0552; ss_set_nz16(ss, a);   /* C084EF lda #$0552 */
  S(0x84F2, 3); t_write16(ss, ss_abs(ss, BG12NBA), a);  /* C084F2 sta BG12NBA */
  S(0x84F5, 3); a = 0x7079; ss_set_nz16(ss, a);   /* C084F5 lda #$7079 */
  S(0x84F8, 3); t_write16(ss, ss_abs(ss, BG1SC), a);    /* C084F8 sta BG1SC */

  SEP(0x84FB, 0x20);                        /* C084FB sep #$20 */
  S(0x84FD, 2);                             /* C084FD lda #$74 */
  a = (uint16_t) ((a & 0xff00) | 0x74); ss_set_nz8(ss, 0x74);
  S(0x84FF, 3); t_write8(ss, ss_abs(ss, BG3SC), 0x74);  /* C084FF sta BG3SC */
  S(0x8502, 2);                             /* C08502 lda #$20 */
  a = (uint16_t) ((a & 0xff00) | 0x20); ss_set_nz8(ss, 0x20);
  S(0x8504, 3); t_write8(ss, ss_abs(ss, COLDATA), 0x20);/* C08504 sta COLDATA */
  S(0x8507, 2);                             /* C08507 lda #$44 */
  a = (uint16_t) ((a & 0xff00) | 0x44); ss_set_nz8(ss, 0x44);
  S(0x8509, 3); t_write8(ss, ss_abs(ss, COLDATA), 0x44);/* C08509 sta COLDATA */
  S(0x850C, 2);                             /* C0850C lda #$81 */
  a = (uint16_t) ((a & 0xff00) | 0x81); ss_set_nz8(ss, 0x81);
  S(0x850E, 3); t_write8(ss, ss_abs(ss, COLDATA), 0x81);/* C0850E sta COLDATA */
  JSR(0x8511, set_bg_scroll_addr);          /* C08511 jsr set_bg_scroll */

  S(0x8514, 3); a = 0x03FF; ss_set_nz16(ss, a);   /* C08514 lda #$03FF */
  S(0x8517, 2);                             /* C08517 sta level_width_mask */
  t_write16(ss, (uint16_t) (dp + level_width_mask), a);
  S(0x8519, 3); a = 0x0030; ss_set_nz16(ss, a);   /* C08519 lda #$0030 */
  S(0x851C, 2);                             /* C0851C sta level_height_mask */
  t_write16(ss, (uint16_t) (dp + level_height_mask), a);
  S(0x851E, 3); a = 0x0001; ss_set_nz16(ss, a);   /* C0851E lda #$0001 */
  S(0x8521, 2);                             /* C08521 sta $82 */
  t_write16(ss, (uint16_t) (dp + layer2_parallax_shift), a);
  S(0x8523, 3); a = 0x0010; ss_set_nz16(ss, a);   /* C08523 lda #$0010 */
  S(0x8526, 2);                             /* C08526 sta $84 */
  t_write16(ss, (uint16_t) (dp + layer2_row_height), a);
  S(0x8528, 3); a = 0xFD80; ss_set_nz16(ss, a);   /* C08528 lda #$FD80 */
  S(0x852B, 2);                             /* C0852B sta tilemap_a_addr */
  t_write16(ss, (uint16_t) (dp + tilemap_a_addr), a);
  S(0x852D, 3); a = 0xC9C9; ss_set_nz16(ss, a);   /* C0852D lda #$C9C9 */
  S(0x8530, 2);                             /* C08530 sta tilemap_a_bank */
  t_write16(ss, (uint16_t) (dp + tilemap_a_bank), a);
  S(0x8532, 3); a = 0x26A0; ss_set_nz16(ss, a);   /* C08532 lda #$26A0 */
  S(0x8535, 2);                             /* C08535 sta tilemap_b_addr */
  t_write16(ss, (uint16_t) (dp + tilemap_b_addr), a);
  S(0x8537, 3); a = 0xCACA; ss_set_nz16(ss, a);   /* C08537 lda #$CACA */
  S(0x853A, 2);                             /* C0853A sta metatile_data_bank */
  t_write16(ss, (uint16_t) (dp + metatile_data_bank), a);
  S(0x853C, 2);                             /* C0853C stz $60 */
  t_write16(ss, (uint16_t) (dp + scroll_scratch_60), 0);
  S(0x853E, 3); a = 0x0018; ss_set_nz16(ss, a);   /* C0853E lda #$0018 */
  S(0x8541, 2);                             /* C08541 sta camera_y */
  t_write16(ss, (uint16_t) (dp + camera_y), a);
  S(0x8543, 3); a = 0x0300; ss_set_nz16(ss, a);   /* C08543 lda #$0300 */

  for(;;) {                                 /* loc_C08546 */
    S(0x8546, 2);                           /* C08546 sta camera_x */
    t_write16(ss, (uint16_t) (dp + camera_x), a);
    JSR(0x8548, build_metatile_column_580_addr);  /* C08548 jsr build_metatile_column_580 */
    JSR(0x854B, vram_upload_column_580_addr);     /* C0854B jsr vram_upload_column_580 */
    S(0x854E, 2);                           /* C0854E lda camera_x */
    a = t_read16(ss, (uint16_t) (dp + camera_x));
    ss_set_nz16(ss, a);
    SI(0x8550); ss_set_c(ss, false);        /* C08550 clc */
    S(0x8551, 3); a = alu_adc16(ss, a, 0x0008);   /* C08551 adc #$0008 */
    S(0x8554, 3); alu_cmp16(ss, a, 0x0400); /* C08554 cmp #$0400 */
    const bool again = !ss_z(ss);
    S(0x8557, 1); t_branch(ss, again);      /* C08557 bne loc_C08546 */
    if(!again) break;
  }

  S(0x8559, 2);                             /* C08559 sta camera_x */
  t_write16(ss, (uint16_t) (dp + camera_x), a);

  S(0x855B, 3); a = 0x2000; ss_set_nz16(ss, a);   /* C0855B lda #$2000 */
  S(0x855E, 3); t_write16(ss, ss_abs(ss, VMADDL), a);  /* C0855E sta VMADDL */
  S(0x8561, 3); x = 0x00C8; ss_set_nz16(ss, x);   /* C08561 ldx #$00C8 */
  S(0x8564, 3); a = 0x6AC0; ss_set_nz16(ss, a);   /* C08564 lda #$6AC0 */
  S(0x8567, 3); y = 0x6000; ss_set_nz16(ss, y);   /* C08567 ldy #$6000 */
  JSR(0x856A, dma_upload_to_vram_addr);     /* C0856A jsr dma_upload_to_vram */

  S(0x856D, 3); a = 0x5000; ss_set_nz16(ss, a);   /* C0856D lda #$5000 */
  S(0x8570, 3); t_write16(ss, ss_abs(ss, VMADDL), a);  /* C08570 sta VMADDL */
  S(0x8573, 3); x = 0x00C8; ss_set_nz16(ss, x);   /* C08573 ldx #$00C8 */
  S(0x8576, 3); a = 0xC980; ss_set_nz16(ss, a);   /* C08576 lda #$C980 */
  S(0x8579, 3); y = 0x3620; ss_set_nz16(ss, y);   /* C08579 ldy #$3620 */
  JSR(0x857C, dma_upload_to_vram_addr);     /* C0857C jsr dma_upload_to_vram */

  S(0x857F, 3); a = 0x6C00; ss_set_nz16(ss, a);   /* C0857F lda #$6C00 */
  JSR(0x8582, dma_fill_vram_zero_addr);     /* C08582 jsr dma_fill_vram_zero */

  S(0x8585, 3); a = 0x6C40; ss_set_nz16(ss, a);   /* C08585 lda #$6C40 */
  S(0x8588, 3); t_write16(ss, ss_abs(ss, VMADDL), a);  /* C08588 sta VMADDL */
  S(0x858B, 3); x = 0x00CB; ss_set_nz16(ss, x);   /* C0858B ldx #$00CB */
  S(0x858E, 3); a = 0x0000; ss_set_nz16(ss, a);   /* C0858E lda #$0000 */
  S(0x8591, 3); y = 0x0800; ss_set_nz16(ss, y);   /* C08591 ldy #$0800 */
  JSR(0x8594, dma_upload_to_vram_addr);     /* C08594 jsr dma_upload_to_vram */

  S(0x8597, 3); a = 0x7020; ss_set_nz16(ss, a);   /* C08597 lda #$7020 */
  S(0x859A, 3); t_write16(ss, ss_abs(ss, VMADDL), a);  /* C0859A sta VMADDL */
  S(0x859D, 3); x = 0x00CC; ss_set_nz16(ss, x);   /* C0859D ldx #$00CC */
  S(0x85A0, 3); a = 0xAB02; ss_set_nz16(ss, a);   /* C085A0 lda #$AB02 */
  S(0x85A3, 3); y = 0x0700; ss_set_nz16(ss, y);   /* C085A3 ldy #$0700 */
  JSR(0x85A6, dma_upload_to_vram_addr);     /* C085A6 jsr dma_upload_to_vram */

  S(0x85A9, 3); a = 0x7000; ss_set_nz16(ss, a);   /* C085A9 lda #$7000 */
  S(0x85AC, 3); t_write16(ss, ss_abs(ss, VMADDL), a);  /* C085AC sta VMADDL */
  S(0x85AF, 3); x = 0x00CC; ss_set_nz16(ss, x);   /* C085AF ldx #$00CC */
  S(0x85B2, 3); a = 0xAB02; ss_set_nz16(ss, a);   /* C085B2 lda #$AB02 */
  S(0x85B5, 3); y = 0x0700; ss_set_nz16(ss, y);   /* C085B5 ldy #$0700 */
  JSR(0x85B8, dma_upload_to_vram_addr);     /* C085B8 jsr dma_upload_to_vram */

  S(0x85BB, 3); a = 0x7420; ss_set_nz16(ss, a);   /* C085BB lda #$7420 */
  S(0x85BE, 3); t_write16(ss, ss_abs(ss, VMADDL), a);  /* C085BE sta VMADDL */
  S(0x85C1, 3); x = 0x00CC; ss_set_nz16(ss, x);   /* C085C1 ldx #$00CC */
  S(0x85C4, 3); a = 0xA402; ss_set_nz16(ss, a);   /* C085C4 lda #$A402 */
  S(0x85C7, 3); y = 0x0700; ss_set_nz16(ss, y);   /* C085C7 ldy #$0700 */
  JSR(0x85CA, dma_upload_to_vram_addr);     /* C085CA jsr dma_upload_to_vram */

  S(0x85CD, 3); a = 0x7400; ss_set_nz16(ss, a);   /* C085CD lda #$7400 */
  S(0x85D0, 3); t_write16(ss, ss_abs(ss, VMADDL), a);  /* C085D0 sta VMADDL */
  S(0x85D3, 3); x = 0x00CC; ss_set_nz16(ss, x);   /* C085D3 ldx #$00CC */
  S(0x85D6, 3); a = 0xA402; ss_set_nz16(ss, a);   /* C085D6 lda #$A402 */
  S(0x85D9, 3); y = 0x0700; ss_set_nz16(ss, y);   /* C085D9 ldy #$0700 */
  JSR(0x85DC, dma_upload_to_vram_addr);     /* C085DC jsr dma_upload_to_vram */

  S(0x85DF, 3); x = 0x0000; ss_set_nz16(ss, x);   /* C085DF ldx #$0000 */
  S(0x85E2, 3); a = 0x0068; ss_set_nz16(ss, a);   /* C085E2 lda #$0068 */
  S(0x85E5, 4);                             /* C085E5 sta $7F0000,X */
  t_write16(ss, (0x7F0000 + x) & 0xffffff, a);
  S(0x85E9, 3); a = 0x00A8; ss_set_nz16(ss, a);   /* C085E9 lda #$00A8 */
  S(0x85EC, 4);                             /* C085EC sta $7F0001,X */
  t_write16(ss, (0x7F0001 + x) & 0xffffff, a);
  SI(0x85F0); x = alu_inc16(ss, x);         /* C085F0 inx */
  SI(0x85F1); x = alu_inc16(ss, x);         /* C085F1 inx */
  SI(0x85F2); x = alu_inc16(ss, x);         /* C085F2 inx */

  for(;;) {                                 /* loc_C085F3 */
    S(0x85F3, 3); a = 0x0090; ss_set_nz16(ss, a); /* C085F3 lda #$0090 */
    S(0x85F6, 4);                           /* C085F6 sta $7F0000,X */
    t_write16(ss, (0x7F0000 + x) & 0xffffff, a);
    S(0x85FA, 3); a = 0x00AC; ss_set_nz16(ss, a); /* C085FA lda #$00AC */
    S(0x85FD, 4);                           /* C085FD sta $7F0001,X */
    t_write16(ss, (0x7F0001 + x) & 0xffffff, a);
    SI(0x8601); x = alu_inc16(ss, x);       /* C08601 inx */
    SI(0x8602); x = alu_inc16(ss, x);       /* C08602 inx */
    SI(0x8603); x = alu_inc16(ss, x);       /* C08603 inx */
    S(0x8604, 3); alu_cpx16(ss, x, 0x0033); /* C08604 cpx #$0033 */
    const bool again = !ss_z(ss);
    S(0x8607, 1); t_branch(ss, again);      /* C08607 bne loc_C085F3 */
    if(!again) break;
  }

  S(0x8609, 3); x = 0x0040; ss_set_nz16(ss, x);   /* C08609 ldx #$0040 */
  S(0x860C, 3); a = 0x0062; ss_set_nz16(ss, a);   /* C0860C lda #$0062 */
  S(0x860F, 4);                             /* C0860F sta $7F0000,X */
  t_write16(ss, (0x7F0000 + x) & 0xffffff, a);
  S(0x8613, 3); a = 0x84B5; ss_set_nz16(ss, a);   /* C08613 lda #$84B5 */
  S(0x8616, 4);                             /* C08616 sta $7F0001,X */
  t_write16(ss, (0x7F0001 + x) & 0xffffff, a);
  SI(0x861A); x = alu_inc16(ss, x);         /* C0861A inx */
  SI(0x861B); x = alu_inc16(ss, x);         /* C0861B inx */
  SI(0x861C); x = alu_inc16(ss, x);         /* C0861C inx */

  for(;;) {                                 /* loc_C0861D */
    S(0x861D, 3); a = 0x0090; ss_set_nz16(ss, a); /* C0861D lda #$0090 */
    S(0x8620, 4);                           /* C08620 sta $7F0000,X */
    t_write16(ss, (0x7F0000 + x) & 0xffffff, a);
    S(0x8624, 3); a = 0x84B5; ss_set_nz16(ss, a); /* C08624 lda #$84B5 */
    S(0x8627, 4);                           /* C08627 sta $7F0001,X */
    t_write16(ss, (0x7F0001 + x) & 0xffffff, a);
    SI(0x862B); x = alu_inc16(ss, x);       /* C0862B inx */
    SI(0x862C); x = alu_inc16(ss, x);       /* C0862C inx */
    SI(0x862D); x = alu_inc16(ss, x);       /* C0862D inx */
    S(0x862E, 3); alu_cpx16(ss, x, 0x0070); /* C0862E cpx #$0070 */
    const bool again = !ss_z(ss);
    S(0x8631, 1); t_branch(ss, again);      /* C08631 bne loc_C0861D */
    if(!again) break;
  }

  S(0x8633, 3); x = 0x0080; ss_set_nz16(ss, x);   /* C08633 ldx #$0080 */
  S(0x8636, 3); a = 0x0070; ss_set_nz16(ss, a);   /* C08636 lda #$0070 */
  S(0x8639, 4);                             /* C08639 sta $7F0000,X */
  t_write16(ss, (0x7F0000 + x) & 0xffffff, a);
  S(0x863D, 3); a = 0x0000; ss_set_nz16(ss, a);   /* C0863D lda #$0000 */
  S(0x8640, 4);                             /* C08640 sta $7F0001,X */
  t_write16(ss, (0x7F0001 + x) & 0xffffff, a);
  S(0x8644, 3); a = 0x0040; ss_set_nz16(ss, a);   /* C08644 lda #$0040 */
  S(0x8647, 4);                             /* C08647 sta $7F0003,X */
  t_write16(ss, (0x7F0003 + x) & 0xffffff, a);
  S(0x864B, 3); a = 0x0000; ss_set_nz16(ss, a);   /* C0864B lda #$0000 */
  S(0x864E, 4);                             /* C0864E sta $7F0004,X */
  t_write16(ss, (0x7F0004 + x) & 0xffffff, a);
  S(0x8652, 3); a = 0x0001; ss_set_nz16(ss, a);   /* C08652 lda #$0001 */
  S(0x8655, 4);                             /* C08655 sta $7F0006,X */
  t_write16(ss, (0x7F0006 + x) & 0xffffff, a);
  S(0x8659, 3); a = 0x0000; ss_set_nz16(ss, a);   /* C08659 lda #$0000 */
  S(0x865C, 4);                             /* C0865C sta $7F0007,X */
  t_write16(ss, (0x7F0007 + x) & 0xffffff, a);
  S(0x8660, 4);                             /* C08660 sta $7F0009,X */
  t_write16(ss, (0x7F0009 + x) & 0xffffff, a);

  SEP(0x8664, 0x20);                        /* C08664 sep #$20 */
  S(0x8666, 3); x = 0x2103; ss_set_nz16(ss, x);   /* C08666 ldx #$2103 */
  S(0x8669, 3); t_write16(ss, ss_abs(ss, DMAP1), x);   /* C08669 stx DMAP1 */
  S(0x866C, 3); x = 0x6FA8; ss_set_nz16(ss, x);   /* C0866C ldx #$6FA8 */
  S(0x866F, 3); t_write16(ss, ss_abs(ss, A1TL1), x);   /* C0866F stx A1TL1 */
  S(0x8672, 2);                             /* C08672 lda #$C4 */
  a = (uint16_t) ((a & 0xff00) | 0xC4); ss_set_nz8(ss, 0xC4);
  S(0x8674, 3); t_write8(ss, ss_abs(ss, A1B1), 0xC4); /* C08674 sta A1B1 */
  S(0x8677, 3); t_write8(ss, ss_abs(ss, DASB1), 0xC4);/* C08677 sta DASB1 */
  S(0x867A, 3); x = 0x0F42; ss_set_nz16(ss, x);   /* C0867A ldx #$0F42 */
  S(0x867D, 3); t_write16(ss, ss_abs(ss, DMAP2), x);   /* C0867D stx DMAP2 */
  S(0x8680, 3); x = 0x0000; ss_set_nz16(ss, x);   /* C08680 ldx #$0000 */
  S(0x8683, 3); t_write16(ss, ss_abs(ss, A1TL2), x);   /* C08683 stx A1TL2 */
  S(0x8686, 2);                             /* C08686 lda #$7F */
  a = (uint16_t) ((a & 0xff00) | 0x7F); ss_set_nz8(ss, 0x7F);
  S(0x8688, 3); t_write8(ss, ss_abs(ss, A1B2), 0x7F); /* C08688 sta A1B2 */
  S(0x868B, 2);                             /* C0868B lda #$00 */
  a = (uint16_t) (a & 0xff00); ss_set_nz8(ss, 0x00);
  S(0x868D, 3); t_write8(ss, ss_abs(ss, DASB2), 0x00);/* C0868D sta DASB2 */
  S(0x8690, 3); x = 0x1242; ss_set_nz16(ss, x);   /* C08690 ldx #$1242 */
  S(0x8693, 3); t_write16(ss, ss_abs(ss, DMAP3), x);   /* C08693 stx DMAP3 */
  S(0x8696, 3); x = 0x0040; ss_set_nz16(ss, x);   /* C08696 ldx #$0040 */
  S(0x8699, 3); t_write16(ss, ss_abs(ss, A1TL3), x);   /* C08699 stx A1TL3 */
  S(0x869C, 2);                             /* C0869C lda #$7F */
  a = (uint16_t) ((a & 0xff00) | 0x7F); ss_set_nz8(ss, 0x7F);
  S(0x869E, 3); t_write8(ss, ss_abs(ss, A1B3), 0x7F); /* C0869E sta A1B3 */
  S(0x86A1, 2);                             /* C086A1 lda #$00 */
  a = (uint16_t) (a & 0xff00); ss_set_nz8(ss, 0x00);
  S(0x86A3, 3); t_write8(ss, ss_abs(ss, DASB3), 0x00);/* C086A3 sta DASB3 */
  S(0x86A6, 3); x = 0x0D02; ss_set_nz16(ss, x);   /* C086A6 ldx #$0D02 */
  S(0x86A9, 3); t_write16(ss, ss_abs(ss, DMAP4), x);   /* C086A9 stx DMAP4 */
  S(0x86AC, 3); x = 0x0080; ss_set_nz16(ss, x);   /* C086AC ldx #$0080 */
  S(0x86AF, 3); t_write16(ss, ss_abs(ss, A1TL4), x);   /* C086AF stx A1TL4 */
  S(0x86B2, 2);                             /* C086B2 lda #$7F */
  a = (uint16_t) ((a & 0xff00) | 0x7F); ss_set_nz8(ss, 0x7F);
  S(0x86B4, 3); t_write8(ss, ss_abs(ss, A1B4), 0x7F); /* C086B4 sta A1B4 */
  S(0x86B7, 3); t_write8(ss, ss_abs(ss, DASB4), 0x7F);/* C086B7 sta DASB4 */
  S(0x86BA, 3); x = 0x3100; ss_set_nz16(ss, x);   /* C086BA ldx #$3100 */
  S(0x86BD, 3); t_write16(ss, ss_abs(ss, DMAP5), x);   /* C086BD stx DMAP5 */
  S(0x86C0, 3); x = 0x8793; ss_set_nz16(ss, x);   /* C086C0 ldx #$8793 */
  S(0x86C3, 3); t_write16(ss, ss_abs(ss, A1TL5), x);   /* C086C3 stx A1TL5 */
  S(0x86C6, 2);                             /* C086C6 lda #$80 */
  a = (uint16_t) ((a & 0xff00) | 0x80); ss_set_nz8(ss, 0x80);
  S(0x86C8, 3); t_write8(ss, ss_abs(ss, A1B5), 0x80); /* C086C8 sta A1B5 */
  S(0x86CB, 3); t_write8(ss, ss_abs(ss, DASB5), 0x80);/* C086CB sta DASB5 */
  S(0x86CE, 3); x = 0x1002; ss_set_nz16(ss, x);   /* C086CE ldx #$1002 */
  S(0x86D1, 3); t_write16(ss, ss_abs(ss, DMAP6), x);   /* C086D1 stx DMAP6 */
  S(0x86D4, 3); x = 0x00C0; ss_set_nz16(ss, x);   /* C086D4 ldx #$00C0 */
  S(0x86D7, 3); t_write16(ss, ss_abs(ss, A1TL6), x);   /* C086D7 stx A1TL6 */
  S(0x86DA, 2);                             /* C086DA lda #$7F */
  a = (uint16_t) ((a & 0xff00) | 0x7F); ss_set_nz8(ss, 0x7F);
  S(0x86DC, 3); t_write8(ss, ss_abs(ss, A1B6), 0x7F); /* C086DC sta A1B6 */
  S(0x86DF, 2);                             /* C086DF lda #$00 */
  a = (uint16_t) (a & 0xff00); ss_set_nz8(ss, 0x00);
  S(0x86E1, 3); t_write8(ss, ss_abs(ss, DASB6), 0x00);/* C086E1 sta DASB6 */
  S(0x86E4, 3); x = 0x0700; ss_set_nz16(ss, x);   /* C086E4 ldx #$0700 */
  S(0x86E7, 3); t_write16(ss, ss_abs(ss, DMAP7), x);   /* C086E7 stx DMAP7 */
  S(0x86EA, 3); x = 0x08FE; ss_set_nz16(ss, x);   /* C086EA ldx #$08FE */
  S(0x86ED, 3); t_write16(ss, ss_abs(ss, A1TL7), x);   /* C086ED stx A1TL7 */
  S(0x86F0, 2);                             /* C086F0 lda #$7F */
  a = (uint16_t) ((a & 0xff00) | 0x7F); ss_set_nz8(ss, 0x7F);
  S(0x86F2, 3); t_write8(ss, ss_abs(ss, A1B7), 0x7F); /* C086F2 sta A1B7 */
  REP(0x86F5, 0x20);                        /* C086F5 rep #$20 */

  S(0x86F7, 3); a = 0x7970; ss_set_nz16(ss, a);   /* C086F7 lda #$7970 */
  S(0x86FA, 4); t_write16(ss, 0x7F08FE, a); /* C086FA sta $7F08FE */
  S(0x86FE, 3); a = 0x7940; ss_set_nz16(ss, a);   /* C086FE lda #$7940 */
  S(0x8701, 4); t_write16(ss, 0x7F0900, a); /* C08701 sta $7F0900 */
  S(0x8705, 3); a = 0x6C01; ss_set_nz16(ss, a);   /* C08705 lda #$6C01 */
  S(0x8708, 4); t_write16(ss, 0x7F0902, a); /* C08708 sta $7F0902 */
  SI(0x870C); a = ss_dp(ss); ss_set_nz16(ss, a);  /* C0870C tdc */
  S(0x870D, 4); t_write16(ss, 0x7F0904, a); /* C0870D sta $7F0904 */
  S(0x8711, 3); a = 0x0068; ss_set_nz16(ss, a);   /* C08711 lda #$0068 */
  S(0x8714, 4); t_write16(ss, 0x7F00C0, a); /* C08714 sta $7F00C0 */
  S(0x8718, 3); a = 0x0000; ss_set_nz16(ss, a);   /* C08718 lda #$0000 */
  S(0x871B, 4); t_write16(ss, 0x7F00C1, a); /* C0871B sta $7F00C1 */
  S(0x871F, 3); a = 0x0002; ss_set_nz16(ss, a);   /* C0871F lda #$0002 */
  S(0x8722, 4); t_write16(ss, 0x7F00C3, a); /* C08722 sta $7F00C3 */
  S(0x8726, 3); a = 0x0000; ss_set_nz16(ss, a);   /* C08726 lda #$0000 */
  S(0x8729, 4); t_write16(ss, 0x7F00C4, a); /* C08729 sta $7F00C4 */
  S(0x872D, 4); t_write16(ss, 0x7F00C6, a); /* C0872D sta $7F00C6 */

  S(0x8731, 3); y = 0x0080; ss_set_nz16(ss, y);   /* C08731 ldy #$0080 */
  S(0x8734, 3); x = 0x0020; ss_set_nz16(ss, x);   /* C08734 ldx #$0020 */
  S(0x8737, 3); a = 0x6C48; ss_set_nz16(ss, a);   /* C08737 lda #$6C48 */
  JSR(0x873A, dma_upload_to_cgram_addr);    /* C0873A jsr dma_upload_to_cgram */

  S(0x873D, 3); y = 0x00A0; ss_set_nz16(ss, y);   /* C0873D ldy #$00A0 */
  S(0x8740, 3); x = 0x0004; ss_set_nz16(ss, x);   /* C08740 ldx #$0004 */
  S(0x8743, 3); a = 0x6D08; ss_set_nz16(ss, a);   /* C08743 lda #$6D08 */
  JSR(0x8746, dma_upload_to_cgram_addr);    /* C08746 jsr dma_upload_to_cgram */

  S(0x8749, 3); y = 0x0000; ss_set_nz16(ss, y);   /* C08749 ldy #$0000 */
  S(0x874C, 3); x = 0x0020; ss_set_nz16(ss, x);   /* C0874C ldx #$0020 */
  S(0x874F, 3); a = 0x6EA8; ss_set_nz16(ss, a);   /* C0874F lda #$6EA8 */
  JSR(0x8752, dma_upload_to_cgram_addr);    /* C08752 jsr dma_upload_to_cgram */

  JSR(0x8755, particle_spawn_from_table_addr);  /* C08755 jsr particle_spawn_from_table */

  S(0x8758, 3); a = 0x1E00; ss_set_nz16(ss, a);   /* C08758 lda #$1E00 */
  S(0x875B, 3); t_write16(ss, ss_abs(ss, VMADDL), a);  /* C0875B sta VMADDL */
  S(0x875E, 3); x = 0x00C5; ss_set_nz16(ss, x);   /* C0875E ldx #$00C5 */
  S(0x8761, 3); a = 0x0000; ss_set_nz16(ss, a);   /* C08761 lda #$0000 */
  S(0x8764, 3); y = 0x0200; ss_set_nz16(ss, y);   /* C08764 ldy #$0200 */
  JSR(0x8767, dma_upload_to_vram_addr);     /* C08767 jsr dma_upload_to_vram */

  JSR(0x876A, sparkle_array_init_addr);     /* C0876A jsr sparkle_array_init */

  S(0x876D, 3); y = 0x00B0; ss_set_nz16(ss, y);   /* C0876D ldy #$00B0 */
  S(0x8770, 3); x = 0x0004; ss_set_nz16(ss, x);   /* C08770 ldx #$0004 */
  S(0x8773, 3); a = 0x6D88; ss_set_nz16(ss, a);   /* C08773 lda #$6D88 */
  JSR(0x8776, dma_upload_to_cgram_addr);    /* C08776 jsr dma_upload_to_cgram */

  SEP(0x8779, 0x20);                        /* C08779 sep #$20 */
  S(0x877B, 2);                             /* C0877B lda #$E1 */
  a = (uint16_t) ((a & 0xff00) | 0xE1); ss_set_nz8(ss, 0xE1);
  S(0x877D, 3); t_write8(ss, ss_abs(ss, CGADD), 0xE1);/* C0877D sta CGADD */
  S(0x8780, 4);                             /* C08780 lda data_C08791 */
  { uint8_t b = t_read8(ss, 0x808791);
    a = (uint16_t) ((a & 0xff00) | b); ss_set_nz8(ss, b);
    S(0x8784, 3); t_write8(ss, ss_abs(ss, CGDATA), b); } /* C08784 sta CGDATA */
  S(0x8787, 4);                             /* C08787 lda data_C08792 */
  { uint8_t b = t_read8(ss, 0x808792);
    a = (uint16_t) ((a & 0xff00) | b); ss_set_nz8(ss, b);
    S(0x878B, 3); t_write8(ss, ss_abs(ss, CGDATA), b); } /* C0878B sta CGDATA */
  REP(0x878E, 0x20);                        /* C0878E rep #$20 */

  ss_set_a(ss, a);
  S(0x8790, 1);                             /* C08790 rts */
  ss_rts(ss);
}

/* ---------------------------------------------------------------------------
 * mode2_level_init — $C0:8798
 *
 * game_mode_table[2]. TM $0413 (no BG3 on the main screen), CGWSEL $B402 and a
 * single COLDATA write; the map is at $CA:5760 with definitions at $C9:B560 --
 * the pointer pair docs/data_formats.md cites for game mode 2 -- and the camera
 * walks $0600..$06F8. $82 = $FFFF turns the layer-2 parallax off. The tail
 * seeds the particle field with particle_spawn_random and programs one HDMA
 * channel from the five bytes at $88A6.
 * ------------------------------------------------------------------------- */
void mode2_level_init(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint16_t dp = ss_dp(ss);

  S(0x8798, 3);                             /* C08798 stz $0BAC */
  t_write16(ss, ss_abs(ss, state_row_offset), 0);
  S(0x879B, 3); a = 0x0001; ss_set_nz16(ss, a);   /* C0879B lda #$0001 */
  S(0x879E, 3); t_write16(ss, ss_abs(ss, BGMODE), a);   /* C0879E sta BGMODE */
  S(0x87A1, 3); a = 0x0413; ss_set_nz16(ss, a);   /* C087A1 lda #$0413 */
  S(0x87A4, 3); t_write16(ss, ss_abs(ss, TM), a);       /* C087A4 sta TM */
  S(0x87A7, 3); a = 0xB402; ss_set_nz16(ss, a);   /* C087A7 lda #$B402 */
  S(0x87AA, 3); t_write16(ss, ss_abs(ss, CGWSEL), a);   /* C087AA sta CGWSEL */
  S(0x87AD, 3); a = 0x795A; ss_set_nz16(ss, a);   /* C087AD lda #$795A */
  S(0x87B0, 3); t_write16(ss, ss_abs(ss, BG1SC), a);    /* C087B0 sta BG1SC */
  S(0x87B3, 3); a = 0x0626; ss_set_nz16(ss, a);   /* C087B3 lda #$0626 */
  S(0x87B6, 3); t_write16(ss, ss_abs(ss, BG12NBA), a);  /* C087B6 sta BG12NBA */

  SEP(0x87B9, 0x20);                        /* C087B9 sep #$20 */
  S(0x87BB, 2);                             /* C087BB lda #$74 */
  a = (uint16_t) ((a & 0xff00) | 0x74); ss_set_nz8(ss, 0x74);
  S(0x87BD, 3); t_write8(ss, ss_abs(ss, BG3SC), 0x74);  /* C087BD sta BG3SC */
  S(0x87C0, 2);                             /* C087C0 lda #$E0 */
  a = (uint16_t) ((a & 0xff00) | 0xE0); ss_set_nz8(ss, 0xE0);
  S(0x87C2, 3); t_write8(ss, ss_abs(ss, COLDATA), 0xE0);/* C087C2 sta COLDATA */
  JSR(0x87C5, set_bg_scroll_addr);          /* C087C5 jsr set_bg_scroll */

  S(0x87C8, 3); a = 0x06FF; ss_set_nz16(ss, a);   /* C087C8 lda #$06FF */
  S(0x87CB, 2);                             /* C087CB sta level_width_mask */
  t_write16(ss, (uint16_t) (dp + level_width_mask), a);
  S(0x87CD, 3); a = 0x02A0; ss_set_nz16(ss, a);   /* C087CD lda #$02A0 */
  S(0x87D0, 2);                             /* C087D0 sta level_height_mask */
  t_write16(ss, (uint16_t) (dp + level_height_mask), a);
  S(0x87D2, 3); a = 0xFFFF; ss_set_nz16(ss, a);   /* C087D2 lda #$FFFF */
  S(0x87D5, 2);                             /* C087D5 sta $82 */
  t_write16(ss, (uint16_t) (dp + layer2_parallax_shift), a);
  S(0x87D7, 3); a = 0x0030; ss_set_nz16(ss, a);   /* C087D7 lda #$0030 */
  S(0x87DA, 2);                             /* C087DA sta $84 */
  t_write16(ss, (uint16_t) (dp + layer2_row_height), a);
  S(0x87DC, 3); a = 0x5760; ss_set_nz16(ss, a);   /* C087DC lda #$5760 */
  S(0x87DF, 2);                             /* C087DF sta tilemap_a_addr */
  t_write16(ss, (uint16_t) (dp + tilemap_a_addr), a);
  S(0x87E1, 3); a = 0xCACA; ss_set_nz16(ss, a);   /* C087E1 lda #$CACA */
  S(0x87E4, 2);                             /* C087E4 sta tilemap_a_bank */
  t_write16(ss, (uint16_t) (dp + tilemap_a_bank), a);
  S(0x87E6, 3); a = 0xB560; ss_set_nz16(ss, a);   /* C087E6 lda #$B560 */
  S(0x87E9, 2);                             /* C087E9 sta tilemap_b_addr */
  t_write16(ss, (uint16_t) (dp + tilemap_b_addr), a);
  S(0x87EB, 3); a = 0xC9C9; ss_set_nz16(ss, a);   /* C087EB lda #$C9C9 */
  S(0x87EE, 2);                             /* C087EE sta metatile_data_bank */
  t_write16(ss, (uint16_t) (dp + metatile_data_bank), a);
  S(0x87F0, 2);                             /* C087F0 stz layer_parallax_mode */
  t_write16(ss, (uint16_t) (dp + layer_parallax_mode), 0);
  S(0x87F2, 2);                             /* C087F2 stz camera_y_lookahead */
  t_write16(ss, (uint16_t) (dp + camera_y_lookahead), 0);
  S(0x87F4, 2);                             /* C087F4 stz $60 */
  t_write16(ss, (uint16_t) (dp + scroll_scratch_60), 0);
  S(0x87F6, 3); a = 0x0015; ss_set_nz16(ss, a);   /* C087F6 lda #$0015 */
  S(0x87F9, 2);                             /* C087F9 sta camera_y */
  t_write16(ss, (uint16_t) (dp + camera_y), a);
  S(0x87FB, 3); a = 0x0600; ss_set_nz16(ss, a);   /* C087FB lda #$0600 */

  for(;;) {                                 /* loc_C087FE */
    S(0x87FE, 2);                           /* C087FE sta camera_x */
    t_write16(ss, (uint16_t) (dp + camera_x), a);
    JSR(0x8800, build_metatile_column_580_addr);  /* C08800 jsr build_metatile_column_580 */
    JSR(0x8803, vram_upload_column_580_addr);     /* C08803 jsr vram_upload_column_580 */
    S(0x8806, 2);                           /* C08806 lda camera_x */
    a = t_read16(ss, (uint16_t) (dp + camera_x));
    ss_set_nz16(ss, a);
    SI(0x8808); ss_set_c(ss, false);        /* C08808 clc */
    S(0x8809, 3); a = alu_adc16(ss, a, 0x0008);   /* C08809 adc #$0008 */
    S(0x880C, 3); alu_cmp16(ss, a, 0x0700); /* C0880C cmp #$0700 */
    const bool again = !ss_z(ss);
    S(0x880F, 1); t_branch(ss, again);      /* C0880F bne loc_C087FE */
    if(!again) break;
  }

  S(0x8811, 2);                             /* C08811 sta camera_x */
  t_write16(ss, (uint16_t) (dp + camera_x), a);

  S(0x8813, 3); a = 0x2000; ss_set_nz16(ss, a);   /* C08813 lda #$2000 */
  S(0x8816, 3); t_write16(ss, ss_abs(ss, VMADDL), a);  /* C08816 sta VMADDL */
  S(0x8819, 3); x = 0x00C7; ss_set_nz16(ss, x);   /* C08819 ldx #$00C7 */
  S(0x881C, 3); a = 0x0342; ss_set_nz16(ss, a);   /* C0881C lda #$0342 */
  S(0x881F, 3); y = 0x6FC0; ss_set_nz16(ss, y);   /* C0881F ldy #$6FC0 */
  JSR(0x8822, dma_upload_to_vram_addr);     /* C08822 jsr dma_upload_to_vram */

  S(0x8825, 3); a = 0x5800; ss_set_nz16(ss, a);   /* C08825 lda #$5800 */
  S(0x8828, 3); t_write16(ss, ss_abs(ss, VMADDL), a);  /* C08828 sta VMADDL */
  S(0x882B, 3); x = 0x00CB; ss_set_nz16(ss, x);   /* C0882B ldx #$00CB */
  S(0x882E, 3); a = 0x1000; ss_set_nz16(ss, a);   /* C0882E lda #$1000 */
  S(0x8831, 3); y = 0x0800; ss_set_nz16(ss, y);   /* C08831 ldy #$0800 */
  JSR(0x8834, dma_upload_to_vram_addr);     /* C08834 jsr dma_upload_to_vram */

  S(0x8837, 3); a = 0x5C00; ss_set_nz16(ss, a);   /* C08837 lda #$5C00 */
  JSR(0x883A, dma_fill_vram_zero_addr);     /* C0883A jsr dma_fill_vram_zero */

  S(0x883D, 3); a = 0x6000; ss_set_nz16(ss, a);   /* C0883D lda #$6000 */
  S(0x8840, 3); t_write16(ss, ss_abs(ss, VMADDL), a);  /* C08840 sta VMADDL */
  S(0x8843, 3); x = 0x00C7; ss_set_nz16(ss, x);   /* C08843 ldx #$00C7 */
  S(0x8846, 3); a = 0xDF82; ss_set_nz16(ss, a);   /* C08846 lda #$DF82 */
  S(0x8849, 3); y = 0x19E0; ss_set_nz16(ss, y);   /* C08849 ldy #$19E0 */
  JSR(0x884C, dma_upload_to_vram_addr);     /* C0884C jsr dma_upload_to_vram */

  S(0x884F, 3); a = 0x7400; ss_set_nz16(ss, a);   /* C0884F lda #$7400 */
  S(0x8852, 3); t_write16(ss, ss_abs(ss, VMADDL), a);  /* C08852 sta VMADDL */
  S(0x8855, 3); x = 0x00CB; ss_set_nz16(ss, x);   /* C08855 ldx #$00CB */
  S(0x8858, 3); a = 0x0800; ss_set_nz16(ss, a);   /* C08858 lda #$0800 */
  S(0x885B, 3); y = 0x0800; ss_set_nz16(ss, y);   /* C0885B ldy #$0800 */
  JSR(0x885E, dma_upload_to_vram_addr);     /* C0885E jsr dma_upload_to_vram */

  S(0x8861, 3); y = 0x0080; ss_set_nz16(ss, y);   /* C08861 ldy #$0080 */
  S(0x8864, 3); x = 0x0020; ss_set_nz16(ss, x);   /* C08864 ldx #$0020 */
  S(0x8867, 3); a = 0x6C48; ss_set_nz16(ss, a);   /* C08867 lda #$6C48 */
  JSR(0x886A, dma_upload_to_cgram_addr);    /* C0886A jsr dma_upload_to_cgram */

  S(0x886D, 3); y = 0x00C0; ss_set_nz16(ss, y);   /* C0886D ldy #$00C0 */
  S(0x8870, 3); x = 0x0010; ss_set_nz16(ss, x);   /* C08870 ldx #$0010 */
  S(0x8873, 3); a = 0x6C48; ss_set_nz16(ss, a);   /* C08873 lda #$6C48 */
  JSR(0x8876, dma_upload_to_cgram_addr);    /* C08876 jsr dma_upload_to_cgram */

  S(0x8879, 3); y = 0x00E0; ss_set_nz16(ss, y);   /* C08879 ldy #$00E0 */
  S(0x887C, 3); x = 0x0004; ss_set_nz16(ss, x);   /* C0887C ldx #$0004 */
  S(0x887F, 3); a = 0x6CC8; ss_set_nz16(ss, a);   /* C0887F lda #$6CC8 */
  JSR(0x8882, dma_upload_to_cgram_addr);    /* C08882 jsr dma_upload_to_cgram */

  S(0x8885, 3); y = 0x0000; ss_set_nz16(ss, y);   /* C08885 ldy #$0000 */
  S(0x8888, 3); x = 0x0020; ss_set_nz16(ss, x);   /* C08888 ldx #$0020 */
  S(0x888B, 3); a = 0x6FE3; ss_set_nz16(ss, a);   /* C0888B lda #$6FE3 */
  JSR(0x888E, dma_upload_to_cgram_addr);    /* C0888E jsr dma_upload_to_cgram */

  JSR(0x8891, particle_spawn_random_addr);  /* C08891 jsr particle_spawn_random */

  S(0x8894, 3); a = 0x0080; ss_set_nz16(ss, a);   /* C08894 lda #$0080 */
  S(0x8897, 2); t_write16(ss, (uint16_t) (dp + ptr_04), a);  /* C08897 sta ptr_04 */
  S(0x8899, 3); a = 0x88A6; ss_set_nz16(ss, a);   /* C08899 lda #$88A6 */
  S(0x889C, 3); y = 0x2C00; ss_set_nz16(ss, y);   /* C0889C ldy #$2C00 */
  S(0x889F, 3); x = 0x0010; ss_set_nz16(ss, x);   /* C0889F ldx #$0010 */
  JSR(0x88A2, dma_setup_channel_step_addr); /* C088A2 jsr dma_setup_channel_step */

  ss_set_a(ss, a);
  ss_set_x(ss, x);
  ss_set_y(ss, y);
  S(0x88A5, 1);                             /* C088A5 rts */
  ss_rts(ss);
}

/* ---------------------------------------------------------------------------
 * title_screen_init — $C0:88AB
 *
 * game_mode_table[3]. The only mode with BGMODE 9 (mode 1 with BG3 priority),
 * TM $0013 and the map at $CE:8714 / $CA:37A0; the camera walks $0000..$00F8.
 * After the tileset and palette uploads it builds the $7F00D0..D9 HDMA table,
 * seeds the four $0BF8..$0BFE layer offsets nmi_scroll_title reads, and
 * programs channels 1..5 from the descriptor bytes at $8A5A.
 * ------------------------------------------------------------------------- */
void title_screen_init(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint16_t dp = ss_dp(ss);

  S(0x88AB, 3);                             /* C088AB stz $0BAC */
  t_write16(ss, ss_abs(ss, state_row_offset), 0);
  S(0x88AE, 3); a = 0x0009; ss_set_nz16(ss, a);   /* C088AE lda #$0009 */
  S(0x88B1, 3); t_write16(ss, ss_abs(ss, BGMODE), a);   /* C088B1 sta BGMODE */
  S(0x88B4, 3); a = 0x0013; ss_set_nz16(ss, a);   /* C088B4 lda #$0013 */
  S(0x88B7, 3); t_write16(ss, ss_abs(ss, TM), a);       /* C088B7 sta TM */
  S(0x88BA, 3); a = 0x1202; ss_set_nz16(ss, a);   /* C088BA lda #$1202 */
  S(0x88BD, 3); t_write16(ss, ss_abs(ss, CGWSEL), a);   /* C088BD sta CGWSEL */
  S(0x88C0, 3); a = 0x7958; ss_set_nz16(ss, a);   /* C088C0 lda #$7958 */
  S(0x88C3, 3); t_write16(ss, ss_abs(ss, BG1SC), a);    /* C088C3 sta BG1SC */
  S(0x88C6, 3); a = 0x0626; ss_set_nz16(ss, a);   /* C088C6 lda #$0626 */
  S(0x88C9, 3); t_write16(ss, ss_abs(ss, BG12NBA), a);  /* C088C9 sta BG12NBA */

  SEP(0x88CC, 0x20);                        /* C088CC sep #$20 */
  S(0x88CE, 2);                             /* C088CE lda #$5C */
  a = (uint16_t) ((a & 0xff00) | 0x5C); ss_set_nz8(ss, 0x5C);
  S(0x88D0, 3); t_write8(ss, ss_abs(ss, BG3SC), 0x5C);  /* C088D0 sta BG3SC */
  S(0x88D3, 2);                             /* C088D3 lda #$E0 */
  a = (uint16_t) ((a & 0xff00) | 0xE0); ss_set_nz8(ss, 0xE0);
  S(0x88D5, 3); t_write8(ss, ss_abs(ss, COLDATA), 0xE0);/* C088D5 sta COLDATA */
  JSR(0x88D8, set_bg_scroll_addr);          /* C088D8 jsr set_bg_scroll */

  S(0x88DB, 3); a = 0x02FF; ss_set_nz16(ss, a);   /* C088DB lda #$02FF */
  S(0x88DE, 2);                             /* C088DE sta level_width_mask */
  t_write16(ss, (uint16_t) (dp + level_width_mask), a);
  S(0x88E0, 3); a = 0x02A0; ss_set_nz16(ss, a);   /* C088E0 lda #$02A0 */
  S(0x88E3, 2);                             /* C088E3 sta level_height_mask */
  t_write16(ss, (uint16_t) (dp + level_height_mask), a);
  S(0x88E5, 3); a = 0xFFFF; ss_set_nz16(ss, a);   /* C088E5 lda #$FFFF */
  S(0x88E8, 2);                             /* C088E8 sta $82 */
  t_write16(ss, (uint16_t) (dp + layer2_parallax_shift), a);
  S(0x88EA, 3); a = 0x0030; ss_set_nz16(ss, a);   /* C088EA lda #$0030 */
  S(0x88ED, 2);                             /* C088ED sta $84 */
  t_write16(ss, (uint16_t) (dp + layer2_row_height), a);
  S(0x88EF, 3); a = 0x8714; ss_set_nz16(ss, a);   /* C088EF lda #$8714 */
  S(0x88F2, 2);                             /* C088F2 sta tilemap_a_addr */
  t_write16(ss, (uint16_t) (dp + tilemap_a_addr), a);
  S(0x88F4, 3); a = 0xCECE; ss_set_nz16(ss, a);   /* C088F4 lda #$CECE */
  S(0x88F7, 2);                             /* C088F7 sta tilemap_a_bank */
  t_write16(ss, (uint16_t) (dp + tilemap_a_bank), a);
  S(0x88F9, 3); a = 0x37A0; ss_set_nz16(ss, a);   /* C088F9 lda #$37A0 */
  S(0x88FC, 2);                             /* C088FC sta tilemap_b_addr */
  t_write16(ss, (uint16_t) (dp + tilemap_b_addr), a);
  S(0x88FE, 3); a = 0xCACA; ss_set_nz16(ss, a);   /* C088FE lda #$CACA */
  S(0x8901, 2);                             /* C08901 sta metatile_data_bank */
  t_write16(ss, (uint16_t) (dp + metatile_data_bank), a);
  S(0x8903, 3); a = 0xFFFF; ss_set_nz16(ss, a);   /* C08903 lda #$FFFF */
  S(0x8906, 2);                             /* C08906 sta layer_parallax_mode */
  t_write16(ss, (uint16_t) (dp + layer_parallax_mode), a);
  S(0x8908, 2);                             /* C08908 stz camera_y_lookahead */
  t_write16(ss, (uint16_t) (dp + camera_y_lookahead), 0);
  S(0x890A, 2);                             /* C0890A stz $60 */
  t_write16(ss, (uint16_t) (dp + scroll_scratch_60), 0);
  S(0x890C, 3); a = 0x0088; ss_set_nz16(ss, a);   /* C0890C lda #$0088 */
  S(0x890F, 2);                             /* C0890F sta camera_y */
  t_write16(ss, (uint16_t) (dp + camera_y), a);
  S(0x8911, 3); a = 0x0000; ss_set_nz16(ss, a);   /* C08911 lda #$0000 */

  for(;;) {                                 /* loc_C08914 */
    S(0x8914, 2);                           /* C08914 sta camera_x */
    t_write16(ss, (uint16_t) (dp + camera_x), a);
    JSR(0x8916, build_metatile_column_580_addr);  /* C08916 jsr build_metatile_column_580 */
    JSR(0x8919, vram_upload_column_580_addr);     /* C08919 jsr vram_upload_column_580 */
    S(0x891C, 2);                           /* C0891C lda camera_x */
    a = t_read16(ss, (uint16_t) (dp + camera_x));
    ss_set_nz16(ss, a);
    SI(0x891E); ss_set_c(ss, false);        /* C0891E clc */
    S(0x891F, 3); a = alu_adc16(ss, a, 0x0008);   /* C0891F adc #$0008 */
    S(0x8922, 3); alu_cmp16(ss, a, 0x0100); /* C08922 cmp #$0100 */
    const bool again = !ss_z(ss);
    S(0x8925, 1); t_branch(ss, again);      /* C08925 bne loc_C08914 */
    if(!again) break;
  }

  S(0x8927, 2);                             /* C08927 sta camera_x */
  t_write16(ss, (uint16_t) (dp + camera_x), a);

  S(0x8929, 3); a = 0x1800; ss_set_nz16(ss, a);   /* C08929 lda #$1800 */
  JSR(0x892C, dma_fill_vram_zero_addr);     /* C0892C jsr dma_fill_vram_zero */

  S(0x892F, 3); a = 0x1C00; ss_set_nz16(ss, a);   /* C0892F lda #$1C00 */
  S(0x8932, 3); t_write16(ss, ss_abs(ss, VMADDL), a);  /* C08932 sta VMADDL */
  S(0x8935, 3); x = 0x00CB; ss_set_nz16(ss, x);   /* C08935 ldx #$00CB */
  S(0x8938, 3); a = 0x3000; ss_set_nz16(ss, a);   /* C08938 lda #$3000 */
  S(0x893B, 3); y = 0x0800; ss_set_nz16(ss, y);   /* C0893B ldy #$0800 */
  JSR(0x893E, dma_upload_to_vram_addr);     /* C0893E jsr dma_upload_to_vram */

  S(0x8941, 3); a = 0x2000; ss_set_nz16(ss, a);   /* C08941 lda #$2000 */
  S(0x8944, 3); t_write16(ss, ss_abs(ss, VMADDL), a);  /* C08944 sta VMADDL */
  S(0x8947, 3); x = 0x00C8; ss_set_nz16(ss, x);   /* C08947 ldx #$00C8 */
  S(0x894A, 3); a = 0x0000; ss_set_nz16(ss, a);   /* C0894A lda #$0000 */
  S(0x894D, 3); y = 0x6AC0; ss_set_nz16(ss, y);   /* C0894D ldy #$6AC0 */
  JSR(0x8950, dma_upload_to_vram_addr);     /* C08950 jsr dma_upload_to_vram */

  S(0x8953, 3); a = 0x5800; ss_set_nz16(ss, a);   /* C08953 lda #$5800 */
  S(0x8956, 3); t_write16(ss, ss_abs(ss, VMADDL), a);  /* C08956 sta VMADDL */
  S(0x8959, 3); x = 0x00CB; ss_set_nz16(ss, x);   /* C08959 ldx #$00CB */
  S(0x895C, 3); a = 0x3800; ss_set_nz16(ss, a);   /* C0895C lda #$3800 */
  S(0x895F, 3); y = 0x0800; ss_set_nz16(ss, y);   /* C0895F ldy #$0800 */
  JSR(0x8962, dma_upload_to_vram_addr);     /* C08962 jsr dma_upload_to_vram */

  S(0x8965, 3); a = 0x5C00; ss_set_nz16(ss, a);   /* C08965 lda #$5C00 */
  S(0x8968, 3); t_write16(ss, ss_abs(ss, VMADDL), a);  /* C08968 sta VMADDL */
  S(0x896B, 3); x = 0x00CB; ss_set_nz16(ss, x);   /* C0896B ldx #$00CB */
  S(0x896E, 3); a = 0x2800; ss_set_nz16(ss, a);   /* C0896E lda #$2800 */
  S(0x8971, 3); y = 0x0800; ss_set_nz16(ss, y);   /* C08971 ldy #$0800 */
  JSR(0x8974, dma_upload_to_vram_addr);     /* C08974 jsr dma_upload_to_vram */

  S(0x8977, 3); a = 0x6000; ss_set_nz16(ss, a);   /* C08977 lda #$6000 */
  S(0x897A, 3); t_write16(ss, ss_abs(ss, VMADDL), a);  /* C0897A sta VMADDL */
  S(0x897D, 3); x = 0x00C9; ss_set_nz16(ss, x);   /* C0897D ldx #$00C9 */
  S(0x8980, 3); a = 0x5AC0; ss_set_nz16(ss, a);   /* C08980 lda #$5AC0 */
  S(0x8983, 3); y = 0x3000; ss_set_nz16(ss, y);   /* C08983 ldy #$3000 */
  JSR(0x8986, dma_upload_to_vram_addr);     /* C08986 jsr dma_upload_to_vram */

  S(0x8989, 3); y = 0x0080; ss_set_nz16(ss, y);   /* C08989 ldy #$0080 */
  S(0x898C, 3); x = 0x0020; ss_set_nz16(ss, x);   /* C0898C ldx #$0020 */
  S(0x898F, 3); a = 0x6C48; ss_set_nz16(ss, a);   /* C0898F lda #$6C48 */
  JSR(0x8992, dma_upload_to_cgram_addr);    /* C08992 jsr dma_upload_to_cgram */

  S(0x8995, 3); y = 0x00C0; ss_set_nz16(ss, y);   /* C08995 ldy #$00C0 */
  S(0x8998, 3); x = 0x0010; ss_set_nz16(ss, x);   /* C08998 ldx #$0010 */
  S(0x899B, 3); a = 0x6C48; ss_set_nz16(ss, a);   /* C0899B lda #$6C48 */
  JSR(0x899E, dma_upload_to_cgram_addr);    /* C0899E jsr dma_upload_to_cgram */

  S(0x89A1, 3); y = 0x00A0; ss_set_nz16(ss, y);   /* C089A1 ldy #$00A0 */
  S(0x89A4, 3); x = 0x0004; ss_set_nz16(ss, x);   /* C089A4 ldx #$0004 */
  S(0x89A7, 3); a = 0x7443; ss_set_nz16(ss, a);   /* C089A7 lda #$7443 */
  JSR(0x89AA, dma_upload_to_cgram_addr);    /* C089AA jsr dma_upload_to_cgram */

  S(0x89AD, 3); y = 0x0000; ss_set_nz16(ss, y);   /* C089AD ldy #$0000 */
  S(0x89B0, 3); x = 0x0020; ss_set_nz16(ss, x);   /* C089B0 ldx #$0020 */
  S(0x89B3, 3); a = 0x7343; ss_set_nz16(ss, a);   /* C089B3 lda #$7343 */
  JSR(0x89B6, dma_upload_to_cgram_addr);    /* C089B6 jsr dma_upload_to_cgram */

  S(0x89B9, 3); a = 0x0040; ss_set_nz16(ss, a);   /* C089B9 lda #$0040 */
  S(0x89BC, 4); t_write16(ss, 0x7F00D0, a); /* C089BC sta $7F00D0 */
  S(0x89C0, 3); a = 0x0038; ss_set_nz16(ss, a);   /* C089C0 lda #$0038 */
  S(0x89C3, 4); t_write16(ss, 0x7F00D3, a); /* C089C3 sta $7F00D3 */
  S(0x89C7, 3); a = 0x0001; ss_set_nz16(ss, a);   /* C089C7 lda #$0001 */
  S(0x89CA, 4); t_write16(ss, 0x7F00D6, a); /* C089CA sta $7F00D6 */
  S(0x89CE, 3); a = 0x0BF8; ss_set_nz16(ss, a);   /* C089CE lda #$0BF8 */
  S(0x89D1, 4); t_write16(ss, 0x7F00D1, a); /* C089D1 sta $7F00D1 */
  SI(0x89D5); a = alu_inc16(ss, a);         /* C089D5 inc A */
  S(0x89D6, 4); t_write16(ss, 0x7F00D4, a); /* C089D6 sta $7F00D4 */
  SI(0x89DA); a = alu_inc16(ss, a);         /* C089DA inc A */
  S(0x89DB, 4); t_write16(ss, 0x7F00D7, a); /* C089DB sta $7F00D7 */
  SI(0x89DF); a = ss_dp(ss); ss_set_nz16(ss, a);  /* C089DF tdc */
  S(0x89E0, 4); t_write16(ss, 0x7F00D9, a); /* C089E0 sta $7F00D9 */
  S(0x89E4, 4);                             /* C089E4 lda $7F00D0 */
  a = t_read16(ss, 0x7F00D0); ss_set_nz16(ss, a);
  S(0x89E8, 3); t_write16(ss, ss_abs(ss, 0x0C00), a); /* C089E8 sta $0C00 */
  S(0x89EB, 4);                             /* C089EB lda $7F00D3 */
  a = t_read16(ss, 0x7F00D3); ss_set_nz16(ss, a);
  S(0x89EF, 3); t_write16(ss, ss_abs(ss, 0x0C02), a); /* C089EF sta $0C02 */
  S(0x89F2, 3); a = 0x1858; ss_set_nz16(ss, a);   /* C089F2 lda #$1858 */
  S(0x89F5, 3); t_write16(ss, ss_abs(ss, 0x0BF8), a); /* C089F5 sta $0BF8 */
  S(0x89F8, 3); t_write16(ss, ss_abs(ss, 0x0BFC), a); /* C089F8 sta $0BFC */
  S(0x89FB, 3); a = 0x0058; ss_set_nz16(ss, a);   /* C089FB lda #$0058 */
  S(0x89FE, 3); t_write16(ss, ss_abs(ss, 0x0BFA), a); /* C089FE sta $0BFA */
  S(0x8A01, 3); t_write16(ss, ss_abs(ss, 0x0BFE), a); /* C08A01 sta $0BFE */

  S(0x8A04, 3); a = 0x007F; ss_set_nz16(ss, a);   /* C08A04 lda #$007F */
  S(0x8A07, 2); t_write16(ss, (uint16_t) (dp + ptr_04), a);  /* C08A07 sta ptr_04 */
  S(0x8A09, 3); a = 0x00D0; ss_set_nz16(ss, a);   /* C08A09 lda #$00D0 */
  S(0x8A0C, 3); y = 0x0740; ss_set_nz16(ss, y);   /* C08A0C ldy #$0740 */
  S(0x8A0F, 3); x = 0x0010; ss_set_nz16(ss, x);   /* C08A0F ldx #$0010 */
  JSR(0x8A12, dma_setup_channel_step_addr); /* C08A12 jsr dma_setup_channel_step */

  S(0x8A15, 3); a = 0x0080; ss_set_nz16(ss, a);   /* C08A15 lda #$0080 */
  S(0x8A18, 2); t_write16(ss, (uint16_t) (dp + ptr_04), a);  /* C08A18 sta ptr_04 */
  S(0x8A1A, 3); a = 0x8A5A; ss_set_nz16(ss, a);   /* C08A1A lda #$8A5A */
  S(0x8A1D, 3); y = 0x2C01; ss_set_nz16(ss, y);   /* C08A1D ldy #$2C01 */
  S(0x8A20, 3); x = 0x0020; ss_set_nz16(ss, x);   /* C08A20 ldx #$0020 */
  JSR(0x8A23, dma_setup_channel_step_addr); /* C08A23 jsr dma_setup_channel_step */

  S(0x8A26, 3); a = 0x0080; ss_set_nz16(ss, a);   /* C08A26 lda #$0080 */
  S(0x8A29, 2); t_write16(ss, (uint16_t) (dp + ptr_04), a);  /* C08A29 sta ptr_04 */
  S(0x8A2B, 3); a = 0x8A61; ss_set_nz16(ss, a);   /* C08A2B lda #$8A61 */
  S(0x8A2E, 3); y = 0x0900; ss_set_nz16(ss, y);   /* C08A2E ldy #$0900 */
  S(0x8A31, 3); x = 0x0030; ss_set_nz16(ss, x);   /* C08A31 ldx #$0030 */
  JSR(0x8A34, dma_setup_channel_step_addr); /* C08A34 jsr dma_setup_channel_step */

  S(0x8A37, 3); a = 0x0080; ss_set_nz16(ss, a);   /* C08A37 lda #$0080 */
  S(0x8A3A, 2); t_write16(ss, (uint16_t) (dp + ptr_04), a);  /* C08A3A sta ptr_04 */
  S(0x8A3C, 3); a = 0x8A66; ss_set_nz16(ss, a);   /* C08A3C lda #$8A66 */
  S(0x8A3F, 3); y = 0x1143; ss_set_nz16(ss, y);   /* C08A3F ldy #$1143 */
  S(0x8A42, 3); x = 0x0040; ss_set_nz16(ss, x);   /* C08A42 ldx #$0040 */
  JSR(0x8A45, dma_setup_channel_step_addr); /* C08A45 jsr dma_setup_channel_step */

  S(0x8A48, 3); a = 0x0080; ss_set_nz16(ss, a);   /* C08A48 lda #$0080 */
  S(0x8A4B, 2); t_write16(ss, (uint16_t) (dp + ptr_04), a);  /* C08A4B sta ptr_04 */
  S(0x8A4D, 3); a = 0x8A6D; ss_set_nz16(ss, a);   /* C08A4D lda #$8A6D */
  S(0x8A50, 3); y = 0x0D43; ss_set_nz16(ss, y);   /* C08A50 ldy #$0D43 */
  S(0x8A53, 3); x = 0x0050; ss_set_nz16(ss, x);   /* C08A53 ldx #$0050 */
  JSR(0x8A56, dma_setup_channel_step_addr); /* C08A56 jsr dma_setup_channel_step */

  ss_set_a(ss, a);
  ss_set_x(ss, x);
  ss_set_y(ss, y);
  S(0x8A59, 1);                             /* C08A59 rts */
  ss_rts(ss);
}

/* ---------------------------------------------------------------------------
 * mode0_camera_zone_update — $C0:8A74   (proposed: mode0_camera_zone_update)
 *
 * jtbl_C0827A[0], so the mode-0 arm of the per-mode update nmi_handler_gameplay
 * calls once a frame at $815B (the other three modes get
 * check_pending_player_attack).
 * docs/naming_proposals.md section 5 proposes `mode0_weather_zone_update` for
 * it at low confidence; reading the block, what it actually does is script the
 * level by camera position:
 *
 *  - It runs a free-running 180-frame cycle in $0C1F. Each time the counter
 *    reaches zero it re-arms it and re-seeds the six-word effect record at
 *    $0C21..$0C2B with kind 3, $FF/$FF, $0070, the current camera_x and 0 --
 *    the same record nmi_handler_gameplay re-seeds with kind 4 at $816A when the
 *    walk cycle fires, which is what ties the record to a spawned effect rather
 *    than to the weather alone.
 *  - Below camera_x $0100 it clears the settle timer $78 outright; past it, and
 *    only while $78 and $0C0C are both idle, it turns the distance from
 *    camera_x $0400 into a new $78 ( (0x400-x)>>2 + 0x78, clamped to $5A when
 *    more than a screen past ) -- a countdown that scales with how far the
 *    camera still is from the boundary.
 *  - It publishes camera_x >> 2 and camera_y >> 2 to $0BF0/$0BF2 (the layer
 *    offsets nmi_scroll_mode0 consumes) and then runs a nine-way ladder on
 *    camera_x: the boundaries $0090, $0180, $02E0, $0438, $05E0, $0800, $0980,
 *    $0B00 and $0E00 each select a zone body that programs $0BD8/$0BDA, the
 *    per-scanline TM/TS pair that $8EF8 copies into the $0BD4/$0BD6 HDMA source
 *    mode0_level_init points channel 1 at, plus $0C00/$0C02, the entity-flag
 *    priority bits and the $0C0F..$0C13 fade state.
 *
 * The listing splits at the next curated label, so the hook covers $8A74..$8BDA
 * only. Every exit is therefore a hand-over rather than an rts: the three
 * shared zone bodies live at $8E2B, $8E39 and $8E7F, and the last block falls
 * out either to $8BE4 or straight through to $8BDB.
 * Entry: A/X/Y are the caller's (`jsr (jtbl_C0827A,X)` with X = game_mode * 2).
 * ------------------------------------------------------------------------- */

/* a 16-bit read-modify-write (dec abs): the read takes no interrupt latch
 * between its bytes and the write-back goes high byte first */
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

void mode0_camera_zone_update(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint16_t dp = ss_dp(ss);

  S(0x8A74, 3); t_write16(ss, ss_abs(ss, 0x0C04), 0);  /* C08A74 stz $0C04 */
  S(0x8A77, 3);                             /* C08A77 lda $0C1F */
  a = t_read16(ss, ss_abs(ss, 0x0C1F)); ss_set_nz16(ss, a);
  {
    const bool armed = a != 0;
    S(0x8A7A, 1); t_branch(ss, armed);      /* C08A7A bne loc_C08A9F */
    if(!armed) {
      S(0x8A7C, 3); a = 0x00B4; ss_set_nz16(ss, a);   /* C08A7C lda #$00B4 */
      S(0x8A7F, 3); t_write16(ss, ss_abs(ss, 0x0C1F), a);  /* C08A7F sta $0C1F */
      S(0x8A82, 3); a = 0x0003; ss_set_nz16(ss, a);   /* C08A82 lda #$0003 */
      S(0x8A85, 3); t_write16(ss, ss_abs(ss, 0x0C21), a);  /* C08A85 sta $0C21 */
      S(0x8A88, 3); a = 0x00FF; ss_set_nz16(ss, a);   /* C08A88 lda #$00FF */
      S(0x8A8B, 3); t_write16(ss, ss_abs(ss, 0x0C23), a);  /* C08A8B sta $0C23 */
      S(0x8A8E, 3); t_write16(ss, ss_abs(ss, 0x0C29), a);  /* C08A8E sta $0C29 */
      S(0x8A91, 3); a = 0x0070; ss_set_nz16(ss, a);   /* C08A91 lda #$0070 */
      S(0x8A94, 3); t_write16(ss, ss_abs(ss, 0x0C25), a);  /* C08A94 sta $0C25 */
      S(0x8A97, 2);                         /* C08A97 lda camera_x */
      a = t_read16(ss, (uint16_t) (dp + camera_x)); ss_set_nz16(ss, a);
      S(0x8A99, 3); t_write16(ss, ss_abs(ss, 0x0C27), a);  /* C08A99 sta $0C27 */
      S(0x8A9C, 3); t_write16(ss, ss_abs(ss, 0x0C2B), 0);  /* C08A9C stz $0C2B */
    }
  }

  /* loc_C08A9F */
  S(0x8A9F, 3);                             /* C08A9F dec $0C1F */
  {
    const uint32_t adr = ss_abs(ss, 0x0C1F);
    uint16_t v = (uint16_t) (t_rmw_r16(ss, adr) - 1);
    ss_idle(ss);
    t_rmw_w16(ss, adr, v);
    ss_set_nz16(ss, v);
  }
  S(0x8AA2, 2);                             /* C08AA2 lda camera_x */
  a = t_read16(ss, (uint16_t) (dp + camera_x)); ss_set_nz16(ss, a);
  S(0x8AA4, 3); alu_cmp16(ss, a, 0x0100);   /* C08AA4 cmp #$0100 */
  {
    const bool past = ss_c(ss);
    S(0x8AA7, 1); t_branch(ss, past);       /* C08AA7 bcs loc_C08AAD */
    if(!past) {
      S(0x8AA9, 2);                         /* C08AA9 stz $78 */
      t_write16(ss, (uint16_t) (dp + zone_settle_timer), 0);
      S(0x8AAB, 1); t_branch(ss, true);     /* C08AAB bra loc_C08AD3 */
      goto loc_8AD3;
    }
  }

  /* loc_C08AAD */
  S(0x8AAD, 2);                             /* C08AAD ldy $78 */
  y = t_read16(ss, (uint16_t) (dp + zone_settle_timer)); ss_set_nz16(ss, y);
  { const bool busy = y != 0;
    S(0x8AAF, 1); t_branch(ss, busy);       /* C08AAF bne loc_C08AD5 */
    if(busy) goto loc_8AD5; }
  S(0x8AB1, 3);                             /* C08AB1 ldy $0C0C */
  y = t_read16(ss, ss_abs(ss, 0x0C0C)); ss_set_nz16(ss, y);
  { const bool busy = y != 0;
    S(0x8AB4, 1); t_branch(ss, busy);       /* C08AB4 bne loc_C08AD5 */
    if(busy) goto loc_8AD5; }

  SI(0x8AB6); ss_set_c(ss, true);           /* C08AB6 sec */
  S(0x8AB7, 3); a = alu_sbc16(ss, a, 0x0400);  /* C08AB7 sbc #$0400 */
  {
    const bool before = (a & 0x8000) != 0;
    S(0x8ABA, 1); t_branch(ss, before);     /* C08ABA bmi loc_C08AC7 */
    if(!before) {
      S(0x8ABC, 3); alu_cmp16(ss, a, 0x0100);  /* C08ABC cmp #$0100 */
      const bool near = !ss_c(ss);
      S(0x8ABF, 1); t_branch(ss, near);     /* C08ABF bcc loc_C08AC6 */
      if(!near) {
        S(0x8AC1, 3); a = 0x005A; ss_set_nz16(ss, a);  /* C08AC1 lda #$005A */
        S(0x8AC4, 1); t_branch(ss, true);   /* C08AC4 bra loc_C08AD1 */
        goto loc_8AD1;
      }
      SI(0x8AC6); a = ss_dp(ss); ss_set_nz16(ss, a);   /* C08AC6 tdc */
    }
  }

  /* loc_C08AC7 */
  S(0x8AC7, 3); a = alu_eor16(ss, a, 0xFFFF);  /* C08AC7 eor #$FFFF */
  SI(0x8ACA); a = alu_inc16(ss, a);         /* C08ACA inc A */
  SI(0x8ACB); a = alu_lsr16(ss, a);         /* C08ACB lsr A */
  SI(0x8ACC); a = alu_lsr16(ss, a);         /* C08ACC lsr A */
  SI(0x8ACD); ss_set_c(ss, false);          /* C08ACD clc */
  S(0x8ACE, 3); a = alu_adc16(ss, a, 0x0078);  /* C08ACE adc #$0078 */

 loc_8AD1:
  S(0x8AD1, 2);                             /* C08AD1 sta $78 */
  t_write16(ss, (uint16_t) (dp + zone_settle_timer), a);
 loc_8AD3:
  S(0x8AD3, 2);                             /* C08AD3 stz walk_cycle_parity */
  t_write16(ss, (uint16_t) (dp + walk_cycle_parity), 0);
 loc_8AD5:
  S(0x8AD5, 2);                             /* C08AD5 lda camera_x */
  a = t_read16(ss, (uint16_t) (dp + camera_x)); ss_set_nz16(ss, a);
  SI(0x8AD7); x = a; ss_set_nz16(ss, x);    /* C08AD7 tax */
  SI(0x8AD8); a = alu_lsr16(ss, a);         /* C08AD8 lsr A */
  SI(0x8AD9); a = alu_lsr16(ss, a);         /* C08AD9 lsr A */
  S(0x8ADA, 3); t_write16(ss, ss_abs(ss, 0x0BF0), a);  /* C08ADA sta $0BF0 */
  S(0x8ADD, 2);                             /* C08ADD lda camera_y */
  a = t_read16(ss, (uint16_t) (dp + camera_y)); ss_set_nz16(ss, a);
  SI(0x8ADF); a = alu_lsr16(ss, a);         /* C08ADF lsr A */
  SI(0x8AE0); a = alu_lsr16(ss, a);         /* C08AE0 lsr A */
  S(0x8AE1, 3); t_write16(ss, ss_abs(ss, 0x0BF2), a);  /* C08AE1 sta $0BF2 */

  /* the camera-X ladder: each rung either jumps to a zone body or falls to the
   * next comparison. $8E2B, $8E39 and $8E7F are past this hook's block. */
  S(0x8AE4, 3); alu_cpx16(ss, x, 0x0090);   /* C08AE4 cpx #$0090 */
  { const bool ge = ss_c(ss);
    S(0x8AE7, 1); t_branch(ss, ge);         /* C08AE7 bcs loc_C08AEC */
    if(!ge) { JMP(0x8AE9, 0x8B80); goto loc_8B80; } }   /* C08AE9 jmp loc_C08B80 */

  S(0x8AEC, 3); alu_cpx16(ss, x, 0x0180);   /* C08AEC cpx #$0180 */
  { const bool ge = ss_c(ss);
    S(0x8AEF, 1); t_branch(ss, ge);         /* C08AEF bcs loc_C08AF4 */
    if(!ge) { JMP(0x8AF1, 0x8E2B); return; } }         /* C08AF1 jmp loc_C08E2B */

  S(0x8AF4, 3); alu_cpx16(ss, x, 0x02E0);   /* C08AF4 cpx #$02E0 */
  { const bool ge = ss_c(ss);
    S(0x8AF7, 1); t_branch(ss, ge);         /* C08AF7 bcs loc_C08AFC */
    if(!ge) { JMP(0x8AF9, 0x8E39); return; } }         /* C08AF9 jmp loc_C08E39 */

  S(0x8AFC, 3); alu_cpx16(ss, x, 0x0438);   /* C08AFC cpx #$0438 */
  { const bool ge = ss_c(ss);
    S(0x8AFF, 1); t_branch(ss, ge);         /* C08AFF bcs loc_C08B04 */
    if(!ge) { JMP(0x8B01, 0x8B2C); goto loc_8B2C; } }  /* C08B01 jmp loc_C08B2C */

  S(0x8B04, 3); alu_cpx16(ss, x, 0x05E0);   /* C08B04 cpx #$05E0 */
  { const bool ge = ss_c(ss);
    S(0x8B07, 1); t_branch(ss, ge);         /* C08B07 bcs loc_C08B0C */
    if(!ge) { JMP(0x8B09, 0x8B9D); goto loc_8B9D; } }  /* C08B09 jmp loc_C08B9D */

  S(0x8B0C, 3); alu_cpx16(ss, x, 0x0800);   /* C08B0C cpx #$0800 */
  { const bool ge = ss_c(ss);
    S(0x8B0F, 1); t_branch(ss, ge);         /* C08B0F bcs loc_C08B14 */
    if(!ge) { JMP(0x8B11, 0x8B98); goto loc_8B98; } }  /* C08B11 jmp loc_C08B98 */

  S(0x8B14, 3); alu_cpx16(ss, x, 0x0980);   /* C08B14 cpx #$0980 */
  { const bool ge = ss_c(ss);
    S(0x8B17, 1); t_branch(ss, ge);         /* C08B17 bcs loc_C08B1C */
    if(!ge) { JMP(0x8B19, 0x8B2C); goto loc_8B2C; } }  /* C08B19 jmp loc_C08B2C */

  S(0x8B1C, 3); alu_cpx16(ss, x, 0x0B00);   /* C08B1C cpx #$0B00 */
  { const bool ge = ss_c(ss);
    S(0x8B1F, 1); t_branch(ss, ge);         /* C08B1F bcs loc_C08B24 */
    if(!ge) { JMP(0x8B21, 0x8E2B); return; } }         /* C08B21 jmp loc_C08E2B */

  S(0x8B24, 3); alu_cpx16(ss, x, 0x0E00);   /* C08B24 cpx #$0E00 */
  { const bool ge = ss_c(ss);
    S(0x8B27, 1); t_branch(ss, ge);         /* C08B27 bcs loc_C08B2C */
    if(!ge) { JMP(0x8B29, 0x8E39); return; } }         /* C08B29 jmp loc_C08E39 */

 loc_8B2C:
  S(0x8B2C, 3);                             /* C08B2C lda entity_flags */
  a = t_read16(ss, ss_abs(ss, entity_flags)); ss_set_nz16(ss, a);
  S(0x8B2F, 3); a = alu_and16(ss, a, 0xCFFF);  /* C08B2F and #$CFFF */
  S(0x8B32, 3); a = alu_ora16(ss, a, 0x2000);  /* C08B32 ora #$2000 */
  S(0x8B35, 3);                             /* C08B35 sta entity_flags */
  t_write16(ss, ss_abs(ss, entity_flags), a);
  S(0x8B38, 3);                             /* C08B38 lda $0C1B */
  a = t_read16(ss, ss_abs(ss, 0x0C1B)); ss_set_nz16(ss, a);
  { const bool idle = a == 0;
    S(0x8B3B, 1); t_branch(ss, idle);       /* C08B3B beq loc_C08B49 */
    if(!idle) {
      S(0x8B3D, 3); a = 0xFFFF; ss_set_nz16(ss, a);   /* C08B3D lda #$FFFF */
      S(0x8B40, 3); t_write16(ss, ss_abs(ss, 0x0C1B), a);  /* C08B40 sta $0C1B */
      S(0x8B43, 3); a = 0x0080; ss_set_nz16(ss, a);   /* C08B43 lda #$0080 */
      S(0x8B46, 3); t_write16(ss, ss_abs(ss, 0x0C1D), a);  /* C08B46 sta $0C1D */
    } }

  /* loc_C08B49 */
  S(0x8B49, 3); a = 0x1016; ss_set_nz16(ss, a);   /* C08B49 lda #$1016 */
  S(0x8B4C, 3); t_write16(ss, ss_abs(ss, 0x0BD8), a);  /* C08B4C sta $0BD8 */
  S(0x8B4F, 3); a = 0x1012; ss_set_nz16(ss, a);   /* C08B4F lda #$1012 */
  S(0x8B52, 3); t_write16(ss, ss_abs(ss, 0x0BDA), a);  /* C08B52 sta $0BDA */
  S(0x8B55, 3); a = 0x0100; ss_set_nz16(ss, a);   /* C08B55 lda #$0100 */
  S(0x8B58, 3); t_write16(ss, ss_abs(ss, 0x0C00), a);  /* C08B58 sta $0C00 */
  S(0x8B5B, 4);                             /* C08B5B lda $7F00D3 */
  a = t_read16(ss, 0x7F00D3); ss_set_nz16(ss, a);
  S(0x8B5F, 3); a = alu_and16(ss, a, 0xFF00);  /* C08B5F and #$FF00 */
  S(0x8B62, 3); a = alu_ora16(ss, a, 0x003C);  /* C08B62 ora #$003C */
  S(0x8B65, 3); t_write16(ss, ss_abs(ss, 0x0C02), a);  /* C08B65 sta $0C02 */
  S(0x8B68, 3); t_write16(ss, ss_abs(ss, 0x0C04), 0);  /* C08B68 stz $0C04 */
  S(0x8B6B, 3); y = 0x00C1; ss_set_nz16(ss, y);   /* C08B6B ldy #$00C1 */
  S(0x8B6E, 3); x = 0x0000; ss_set_nz16(ss, x);   /* C08B6E ldx #$0000 */
  S(0x8B71, 3); a = 0x6C6C; ss_set_nz16(ss, a);   /* C08B71 lda #$6C6C */
  S(0x8B74, 3); t_write16(ss, ss_abs(ss, 0x0C0C), 0);  /* C08B74 stz $0C0C */
  S(0x8B77, 3); t_write16(ss, ss_abs(ss, 0x0C11), 0);  /* C08B77 stz $0C11 */
  S(0x8B7A, 3); t_write16(ss, ss_abs(ss, 0x0C13), 0);  /* C08B7A stz $0C13 */
  JMP(0x8B7D, 0x8E7F);                      /* C08B7D jmp loc_C08E7F */
  return;

 loc_8B80:
  S(0x8B80, 3); a = 0x1017; ss_set_nz16(ss, a);   /* C08B80 lda #$1017 */
  S(0x8B83, 3); t_write16(ss, ss_abs(ss, 0x0BD8), a);  /* C08B83 sta $0BD8 */
  S(0x8B86, 3); a = 0x1013; ss_set_nz16(ss, a);   /* C08B86 lda #$1013 */
  S(0x8B89, 3); t_write16(ss, ss_abs(ss, 0x0BDA), a);  /* C08B89 sta $0BDA */
  S(0x8B8C, 2);                             /* C08B8C lda camera_y */
  a = t_read16(ss, (uint16_t) (dp + camera_y)); ss_set_nz16(ss, a);
  /* no clc: the carry is the one the cpx #$0090 that branched here left. */
  S(0x8B8E, 3); a = alu_adc16(ss, a, 0x0088);  /* C08B8E adc #$0088 */
  SI(0x8B91); y = a; ss_set_nz16(ss, y);    /* C08B91 tay */
  S(0x8B92, 3); a = 0x6969; ss_set_nz16(ss, a);   /* C08B92 lda #$6969 */
  JMP(0x8B95, 0x8E7F);                      /* C08B95 jmp loc_C08E7F */
  return;

 loc_8B98:
  S(0x8B98, 3); a = 0x0700; ss_set_nz16(ss, a);   /* C08B98 lda #$0700 */
  S(0x8B9B, 1); t_branch(ss, true);         /* C08B9B bra loc_C08BA0 */
  goto loc_8BA0;

 loc_8B9D:
  S(0x8B9D, 3); a = 0x0500; ss_set_nz16(ss, a);   /* C08B9D lda #$0500 */

 loc_8BA0:
  S(0x8BA0, 3); t_write16(ss, ss_abs(ss, 0x0C0F), a);  /* C08BA0 sta $0C0F */
  S(0x8BA3, 3); a = 0x1017; ss_set_nz16(ss, a);   /* C08BA3 lda #$1017 */
  S(0x8BA6, 3); t_write16(ss, ss_abs(ss, 0x0BD8), a);  /* C08BA6 sta $0BD8 */
  S(0x8BA9, 3); a = 0x1417; ss_set_nz16(ss, a);   /* C08BA9 lda #$1417 */
  S(0x8BAC, 3); t_write16(ss, ss_abs(ss, 0x0BDA), a);  /* C08BAC sta $0BDA */
  S(0x8BAF, 3); y = 0x0020; ss_set_nz16(ss, y);   /* C08BAF ldy #$0020 */
  S(0x8BB2, 3);                             /* C08BB2 lda entity_state */
  a = t_read16(ss, ss_abs(ss, entity_state)); ss_set_nz16(ss, a);
  S(0x8BB5, 3); a = alu_and16(ss, a, 0xFFFC);  /* C08BB5 and #$FFFC */
  S(0x8BB8, 3); alu_cmp16(ss, a, 0x0008);   /* C08BB8 cmp #$0008 */
  { const bool other = !ss_z(ss);
    S(0x8BBB, 1); t_branch(ss, other);      /* C08BBB bne loc_C08BC0 */
    if(!other) { S(0x8BBD, 3); y = 0x0040; ss_set_nz16(ss, y); } }  /* C08BBD ldy #$0040 */

  /* loc_C08BC0 */
  S(0x8BC0, 2);                             /* C08BC0 sty $18 */
  t_write16(ss, (uint16_t) (dp + scratch_18), y);
  S(0x8BC2, 2);                             /* C08BC2 lda camera_x */
  a = t_read16(ss, (uint16_t) (dp + camera_x)); ss_set_nz16(ss, a);
  SI(0x8BC4); ss_set_c(ss, true);           /* C08BC4 sec */
  S(0x8BC5, 3);                             /* C08BC5 sbc $0C0F */
  a = alu_sbc16(ss, a, t_read16(ss, ss_abs(ss, 0x0C0F)));
  S(0x8BC8, 2);                             /* C08BC8 sta $20 */
  t_write16(ss, (uint16_t) (dp + scratch_20), a);
  { const bool ahead = (a & 0x8000) == 0;
    S(0x8BCA, 1); t_branch(ss, ahead);      /* C08BCA bpl loc_C08BD0 */
    if(!ahead) {
      S(0x8BCC, 3); a = alu_eor16(ss, a, 0xFFFF);   /* C08BCC eor #$FFFF */
      SI(0x8BCF); a = alu_inc16(ss, a);     /* C08BCF inc A */
    } }

  /* loc_C08BD0 */
  S(0x8BD0, 2);                             /* C08BD0 sta $1A */
  t_write16(ss, (uint16_t) (dp + scratch_1A), a);
  S(0x8BD2, 2);                             /* C08BD2 cmp $18 */
  alu_cmp16(ss, a, t_read16(ss, (uint16_t) (dp + scratch_18)));
  { const bool far = ss_c(ss);
    S(0x8BD4, 1); t_branch(ss, far);        /* C08BD4 bcs loc_C08BE4 */
    if(far) HANDOFF(0x8BE4); }
  S(0x8BD6, 3);                             /* C08BD6 lda $0C11 */
  a = t_read16(ss, ss_abs(ss, 0x0C11)); ss_set_nz16(ss, a);
  { const bool busy = a != 0;
    S(0x8BD9, 1); t_branch(ss, busy);       /* C08BD9 bne loc_C08BE4 */
    if(busy) HANDOFF(0x8BE4); }

  /* falls through to $C0:8BDB, the next curated label (ppu_regs_default) */
  HANDOFF(0x8BDB);
}

/* ---------------------------------------------------------------------------
 * cgram_palette_ramp_step — $C0:91BB   (proposed: cgram_palette_ramp_step)
 *
 * Called from nmi_handler_gameplay at $81B7 whenever $0C1B is non-zero, with
 * that word still in A -- which is why the routine's first instruction is a bare `bmi`:
 * the sign of $0C1B is the ramp's direction, set to $FFFF by the zone bodies at
 * $8B3D and $8D19 and to a positive value at $8D45.
 *
 * One step is $0080 of $0C1D, up or down; the ramp ends (and clears $0C1B) when
 * the counter passes $0800 going up or reaches zero going down. The step's high
 * byte then picks a 64-byte palette out of bank $C4 -- ($0C1D & $FF00) >> 2 plus
 * $7103, i.e. one 32-colour bank per $0100 of the counter -- and appends it to
 * the CGRAM upload queue as an 8-byte record ({$0040 bytes, CGADD $80, that
 * source, bank $C4}) at $0B8C..$0B92 + $0B8A, bumping $0B8A by 8.
 * cgram_upload_queue_flush performs the DMA later in the frame.
 *
 * docs/naming_proposals.md section 5 proposes `spawn_screen_flash_effect` at low
 * confidence; the record it writes is a palette-bank swap on a fixed 16-step
 * ramp, so a name naming the ramp is the more defensible one.
 * Entry: A = $0C1B, N set from the caller's load. Exit: A = the new $0B8A,
 * X = the old one, C clear (the $7103 add never carries).
 * ------------------------------------------------------------------------- */
void cgram_palette_ramp_step(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);

  {
    const bool down = ss_n(ss);
    S(0x91BB, 1); t_branch(ss, down);       /* C091BB bmi loc_C091D1 */
    if(!down) {
      S(0x91BD, 3);                         /* C091BD lda $0C1D */
      a = t_read16(ss, ss_abs(ss, 0x0C1D)); ss_set_nz16(ss, a);
      SI(0x91C0); ss_set_c(ss, false);      /* C091C0 clc */
      S(0x91C1, 3); a = alu_adc16(ss, a, 0x0080);  /* C091C1 adc #$0080 */
      S(0x91C4, 3); t_write16(ss, ss_abs(ss, 0x0C1D), a);  /* C091C4 sta $0C1D */
      S(0x91C7, 3); alu_cmp16(ss, a, 0x0800);      /* C091C7 cmp #$0800 */
      const bool more = !ss_c(ss);
      S(0x91CA, 1); t_branch(ss, more);     /* C091CA bcc loc_C091CF */
      if(more) goto loc_91CF;
      goto loc_91CC;
    }
  }

  /* loc_C091D1 */
  S(0x91D1, 3);                             /* C091D1 lda $0C1D */
  a = t_read16(ss, ss_abs(ss, 0x0C1D)); ss_set_nz16(ss, a);
  SI(0x91D4); ss_set_c(ss, true);           /* C091D4 sec */
  S(0x91D5, 3); a = alu_sbc16(ss, a, 0x0080);  /* C091D5 sbc #$0080 */
  S(0x91D8, 3); t_write16(ss, ss_abs(ss, 0x0C1D), a);  /* C091D8 sta $0C1D */
  { const bool done = a == 0;
    S(0x91DB, 1); t_branch(ss, done);       /* C091DB beq loc_C091CC */
    if(!done) goto loc_91DD; }

 loc_91CC:
  S(0x91CC, 3); t_write16(ss, ss_abs(ss, 0x0C1B), 0);  /* C091CC stz $0C1B */
 loc_91CF:
  S(0x91CF, 1); t_branch(ss, true);         /* C091CF bra loc_C091DD */

 loc_91DD:
  S(0x91DD, 3);                             /* C091DD ldx $0B8A */
  x = t_read16(ss, ss_abs(ss, cgram_queue_index)); ss_set_nz16(ss, x);
  S(0x91E0, 3); a = alu_and16(ss, a, 0xFF00);  /* C091E0 and #$FF00 */
  SI(0x91E3); a = alu_lsr16(ss, a);         /* C091E3 lsr A */
  SI(0x91E4); a = alu_lsr16(ss, a);         /* C091E4 lsr A */
  SI(0x91E5); ss_set_c(ss, false);          /* C091E5 clc */
  S(0x91E6, 3); a = alu_adc16(ss, a, 0x7103);  /* C091E6 adc #$7103 */
  S(0x91E9, 3); t_index(ss);                /* C091E9 sta $0B90,X */
  t_write16(ss, ss_abs(ss, (uint16_t) (0x0B90 + x)), a);
  S(0x91EC, 3); a = 0x00C4; ss_set_nz16(ss, a);   /* C091EC lda #$00C4 */
  S(0x91EF, 3); t_index(ss);                /* C091EF sta $0B92,X */
  t_write16(ss, ss_abs(ss, (uint16_t) (0x0B92 + x)), a);
  S(0x91F2, 3); a = 0x0040; ss_set_nz16(ss, a);   /* C091F2 lda #$0040 */
  S(0x91F5, 3); t_index(ss);                /* C091F5 sta $0B8C,X */
  t_write16(ss, ss_abs(ss, (uint16_t) (0x0B8C + x)), a);
  S(0x91F8, 3); a = 0x0080; ss_set_nz16(ss, a);   /* C091F8 lda #$0080 */
  S(0x91FB, 3); t_index(ss);                /* C091FB sta $0B8E,X */
  t_write16(ss, ss_abs(ss, (uint16_t) (0x0B8E + x)), a);
  SI(0x91FE); a = x; ss_set_nz16(ss, a);    /* C091FE txa */
  S(0x91FF, 3); a = alu_adc16(ss, a, 0x0008);  /* C091FF adc #$0008 */
  S(0x9202, 3);                             /* C09202 sta $0B8A */
  t_write16(ss, ss_abs(ss, cgram_queue_index), a);

  ss_set_a(ss, a);
  ss_set_x(ss, x);
  S(0x9205, 1);                             /* C09205 rts */
  ss_rts(ss);
}

static const RecompEntry kModeInit[] = {
  { 0xc08292, "mode0_level_init", mode0_level_init },
  { 0xc084d7, "mode1_level_init", mode1_level_init },
  { 0xc08798, "mode2_level_init", mode2_level_init },
  { 0xc088ab, "title_screen_init", title_screen_init },
  { 0xc08a74, "mode0_camera_zone_update", mode0_camera_zone_update },
  { 0xc091bb, "cgram_palette_ramp_step", cgram_palette_ramp_step },
};
RECOMP_REGISTER(kModeInit)
