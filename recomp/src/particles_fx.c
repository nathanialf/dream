/* Particle / weather effects: spawn, integrate, cull, draw. Bank $C0.
 *
 * docs/naming_proposals.md section 5 describes the subsystem. The records live
 * in the upper WRAM bank as twenty $80-byte columns from $7F:0906, indexed by
 * X (or Y) = slot * 2, forty slots, $00..$4E. The columns keep the addresses
 * the listing uses (nothing outside this cluster names them, and section 5
 * names the subsystem but not the fields), so a body here reads against
 * out/dream.asm line for line. $7F0906 is dream_ram.h's `particle_table`: zero
 * means the slot is free, and particle_table_clear (recomp/src/particles.c)
 * is what frees them all.
 *
 * Three variants of the same integrate/cull/emit loop exist, one per game mode;
 * this file has the mode-0 one (particle_update_and_draw_mode0), the smaller
 * mode-1 "sparkle" array at $7F:0E86 (sparkle_update_and_draw), and the mode-1
 * and mode-2 loops themselves at loc_C09679 and loc_C097DD, which the two
 * dispatchers here tail-jump to.
 *
 * Two details matter before reading a body:
 *
 *  - particle_update_and_draw_mode0 uses the *stack pointer* as scratch. It
 *    saves S in $18 at entry, parks the slot index in S across a tax/long-read
 *    pair (txs at $9408, tsx at $9439), and later parks a partial OAM
 *    coordinate there (tcs/tsc at $949A/$94A1 and $94B6/$94BD) before
 *    restoring S from $18 on the way out. So does the hook, at the same
 *    instructions: for the stretch in between, an interrupt would push into
 *    $00xx, exactly as it would in the ROM.
 *
 *  - $C0:9363 is an inline copy of random_next (recomp/src/random.c) that uses
 *    $28 instead of the stack for its temporary. It is transliterated here
 *    rather than shared, because the two differ instruction for instruction and
 *    the cycle model has to match the copy that actually runs.
 */
#include <stdint.h>
#include <stdbool.h>

#include "snes_state.h"
#include "dream_ram.h"
#include "dream_alu.h"
#include "dream_time.h"

/* Routines the port has already converted, called here as C. The frame this
 * file pushes is the routine's real return address, so a callee that hands the
 * rest of itself back to the ROM (ss_yield_wanted) still returns to the right
 * place; the caller notices by the pc and stops as well. */
void clear_sprite_table(SnesState* ss);       /* oam.c    ($C0:A500) */
void oam_hide_unused_sprites(SnesState* ss);  /* oam.c    ($C0:ADE7) */
void oam_dma_upload(SnesState* ss);           /* oam.c    ($C0:ADFD) */
void random_next(SnesState* ss);              /* random.c ($C0:A212) */

void particle_update_and_draw_mode0(SnesState* ss);
void sparkle_update_and_draw(SnesState* ss);

/* dec abs / dec dp with a 16-bit accumulator: both bytes read, an internal
 * cycle, then the write back, high byte first (LakeSnes cpu_dec). */
static void t_dec16(SnesState* ss, uint32_t adr) {
  const uint32_t hi_adr = (adr + 1) & 0xffffff;
  const uint8_t lo = ss_bus_r8(ss, adr);
  const uint8_t hi = ss_bus_r8(ss, hi_adr);
  const uint16_t v = (uint16_t) ((lo | (hi << 8)) - 1);
  ss_idle(ss);
  ss_bus_w8(ss, hi_adr, (uint8_t) (v >> 8));
  ss_check_int(ss);
  ss_bus_w8(ss, adr, (uint8_t) v);
  ss_set_nz16(ss, v);
}

/* rol dp with an 8-bit accumulator (LakeSnes cpu_rol, mf branch). */
static void t_rol8_dp(SnesState* ss, uint16_t off) {
  const uint32_t adr = (uint32_t) (uint16_t) (ss_dp(ss) + off);
  const int result = (ss_bus_r8(ss, adr) << 1) | (ss_c(ss) ? 1 : 0);
  ss_idle(ss);
  ss_set_c(ss, (result & 0x100) != 0);
  ss_check_int(ss);
  ss_bus_w8(ss, adr, (uint8_t) result);
  ss_set_nz8(ss, (uint8_t) result);
}

/* The second half of a `jsr abs` whose callee is converted: the internal cycle
 * and the return frame the opcode pushes, then the C body. Returns true when
 * the callee did not come back: it yielded, the ROM owns the rest of it, and
 * its rts will land on `ret` without this body's help. */
static bool call_converted(SnesState* ss, uint8_t pb, uint16_t ret,
                           uint16_t target, void (*fn)(SnesState*)) {
  ss_idle(ss);
  const uint16_t frame = (uint16_t) (ss_pc(ss) - 1);
  ss_push8(ss, (uint8_t) (frame >> 8));
  ss_check_int(ss);
  ss_push8(ss, (uint8_t) frame);
  ss_set_pc(ss, pb, target);
  fn(ss);
  return ss_pc(ss) != ret;
}

/* One `jsr abs` to a converted callee, opcode fetch included. */
#define JSRC(at, target, fn)                                                  \
  do {                                                                        \
    S((at), 3);                                                               \
    if(call_converted(ss, pb, (uint16_t) ((at) + 3), (target), (fn))) return;  \
    a = ss_a(ss); x = ss_x(ss); y = ss_y(ss);                                  \
  } while(0)

/* ---------------------------------------------------------------------------
 * mode1_particle_dispatch: $C0:922A
 *
 * jtbl_C08282[1]: draw the sparkle array, then tail-jump into loc_C09679, the
 * mode-1 spawn/cull loop below. The jmp leaves no return of its own, so
 * loc_C09679's rts is what returns to the dispatch site.
 *
 * sparkle_update_and_draw is converted, a few lines down, but the jsr goes
 * through the registry rather than straight to the C: the harness's hook
 * dispatch is what counts a routine as entered, and tools/recomp_verify.py
 * credits a routine only when some script entered it. Reached only this way, a
 * direct call would leave it registered, exercised and yet never credited.
 * ------------------------------------------------------------------------- */
void mode1_particle_dispatch(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);

  {                                         /* C0922A jsr sparkle_update_and_draw */
    const uint16_t sp0 = ss_sp(ss);
    S(0x922A, 3);
    ss_idle(ss);
    const uint16_t frame = (uint16_t) (ss_pc(ss) - 1);
    ss_push8(ss, (uint8_t) (frame >> 8));
    ss_check_int(ss);
    ss_push8(ss, (uint8_t) frame);
    ss_set_pc(ss, pb, 0x9521);
    /* The frame just pushed is the real one, so the callee can be left running
     * at a boundary: its rts lands on $C0922D either way. */
    if(ss_run_callee(ss, sp0)) return;
    a = ss_a(ss); x = ss_x(ss); y = ss_y(ss);
  }

  SJMP(0x922D);                             /* C0922D jmp loc_C09679 */
  ss_set_pc(ss, pb, 0x9679);
}

/* ---------------------------------------------------------------------------
 * mode2_particle_dispatch: $C0:9230
 *
 * jtbl_C08282[2]: a bare tail jump into loc_C097DD, the mode-2 variant of the
 * same loop, below. Registers and flags pass through.
 * ------------------------------------------------------------------------- */
void mode2_particle_dispatch(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);

  SJMP(0x9230);                             /* C09230 jmp loc_C097DD */
  ss_set_pc(ss, pb, 0x97DD);
}

/* ---------------------------------------------------------------------------
 * particle_spawn_mode0_weather: $C0:9253
 *
 * Walks all forty slots backwards; every free one ($7F0906 zero) is seeded from
 * random_next around the weather-zone anchor $0C17 and the camera. Occupied
 * slots are skipped. $0C15 is cleared first, which is the "spawn burst done"
 * flag mode0_weather_zone_update sets.
 * Exit: X = $FFFE, N set, Z clear (the dex pair that ended the loop).
 * ------------------------------------------------------------------------- */
void particle_spawn_mode0_weather(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint16_t dp = ss_dp(ss);
  bool taken;

  S(0x9253, 3);                             /* C09253 stz $0C15 */
  t_write16(ss, ss_abs(ss, 0x0C15), 0);
  SIMM16(0x9256); x = 0x004E; ss_set_nz16(ss, x);  /* C09256 ldx #$004E */

  for(;;) {
    S(0x9259, 4);                           /* C09259 lda particle_table,X */
    a = t_read16(ss, (particle_table + x) & 0xffffff);
    ss_set_nz16(ss, a);
    taken = ss_z(ss);
    S(0x925D, 1); t_branch(ss, taken);      /* C0925D beq loc_C09262 */

    if(!taken) {
      SJMP(0x925F);                         /* C0925F jmp loc_C092EB */
      goto next_slot;
    }

    /* loc_C09262 */
    JSRC(0x9262, 0xA212, random_next);      /* C09262 jsr random_next */

    S(0x9265, 2);                           /* C09265 lda init_magic_AA55 */
    a = t_read16_dp(ss, dp, init_magic_AA55); ss_set_nz16(ss, a);
    SIMM16(0x9267); a = alu_and16(ss, a, 0x003F);  /* C09267 and #$003F */
    S(0x926A, 2); t_write16_dp(ss, dp, ptr_04, a);   /* C0926A sta ptr_04 */
    SIMM16(0x926C); a = alu_eor16(ss, a, 0xFFFF);  /* C0926C eor #$FFFF */
    SI(0x926F); a = alu_inc16(ss, a);              /* C0926F inc A */
    SI(0x9270); ss_set_c(ss, false);               /* C09270 clc */
    S(0x9271, 3);                           /* C09271 adc $0C17 */
    a = alu_adc16(ss, a, t_read16(ss, ss_abs(ss, 0x0C17)));
    S(0x9274, 4);                           /* C09274 sta $7F0B06,X */
    t_write16(ss, (0x7F0B06 + x) & 0xffffff, a);

    S(0x9278, 2);                           /* C09278 lda ptr_04 */
    a = t_read16_dp(ss, dp, ptr_04); ss_set_nz16(ss, a);
    SI(0x927A); a = alu_lsr16(ss, a);              /* C0927A lsr A */
    SI(0x927B); a = alu_lsr16(ss, a);              /* C0927B lsr A */
    SIMM16(0x927C); a = alu_eor16(ss, a, 0xFFFF);  /* C0927C eor #$FFFF */
    SI(0x927F); a = alu_inc16(ss, a);              /* C0927F inc A */
    S(0x9280, 2); t_write16_dp(ss, dp, ptr_04, a);   /* C09280 sta ptr_04 */

    S(0x9282, 2);                           /* C09282 lda ptr_04 */
    a = t_read16_dp(ss, dp, ptr_04); ss_set_nz16(ss, a);
    SI(0x9284); ss_set_c(ss, false);               /* C09284 clc */
    SIMM16(0x9285); a = alu_adc16(ss, a, 0x0080);  /* C09285 adc #$0080 */
    S(0x9288, 4);                           /* C09288 sta $7F0B86,X */
    t_write16(ss, (0x7F0B86 + x) & 0xffffff, a);

    S(0x928C, 2);                           /* C0928C lda camera_y */
    a = t_read16_dp(ss, dp, camera_y); ss_set_nz16(ss, a);
    SIMM16(0x928E); a = alu_adc16(ss, a, 0x0090);  /* C0928E adc #$0090 */
    S(0x9291, 4);                           /* C09291 sta $7F0A86,X */
    t_write16(ss, (0x7F0A86 + x) & 0xffffff, a);

    S(0x9295, 2);                           /* C09295 lda $9D */
    a = t_read16_dp(ss, dp, 0x9D); ss_set_nz16(ss, a);
    SIMM16(0x9297); a = alu_and16(ss, a, 0x00FF);  /* C09297 and #$00FF */
    SIMM16(0x929A); a = alu_adc16(ss, a, 0x00C0);  /* C0929A adc #$00C0 */
    S(0x929D, 4);                           /* C0929D sta $7F0D06,X */
    t_write16(ss, (0x7F0D06 + x) & 0xffffff, a);
    SI(0x92A1); a = alu_lsr16(ss, a);              /* C092A1 lsr A */
    SIMM16(0x92A2); a = alu_adc16(ss, a, 0x0040);  /* C092A2 adc #$0040 */
    S(0x92A5, 4);                           /* C092A5 sta $7F0E06,X */
    t_write16(ss, (0x7F0E06 + x) & 0xffffff, a);

    SIMM16(0x92A9); a = 0x2D60; ss_set_nz16(ss, a);  /* C092A9 lda #$2D60 */
    S(0x92AC, 4);                           /* C092AC sta particle_table,X */
    t_write16(ss, (particle_table + x) & 0xffffff, a);

    SI(0x92B0); a = ss_dp(ss); ss_set_nz16(ss, a); /* C092B0 tdc */
    S(0x92B1, 4);                           /* C092B1 sta $7F0C06,X */
    t_write16(ss, (0x7F0C06 + x) & 0xffffff, a);
    S(0x92B5, 4);                           /* C092B5 sta $7F0C86,X */
    t_write16(ss, (0x7F0C86 + x) & 0xffffff, a);

    S(0x92B9, 2);                           /* C092B9 lda init_magic_AA55 */
    a = t_read16_dp(ss, dp, init_magic_AA55); ss_set_nz16(ss, a);
    SIMM16(0x92BB); a = alu_and16(ss, a, 0x7FFF);  /* C092BB and #$7FFF */
    S(0x92BE, 2);                           /* C092BE bit init_magic_FFFF */
    alu_bit16(ss, a, t_read16_dp(ss, dp, init_magic_FFFF));
    taken = !ss_n(ss);
    S(0x92C0, 1); t_branch(ss, taken);      /* C092C0 bpl loc_C092C6 */
    if(!taken) {
      SIMM16(0x92C2); a = alu_eor16(ss, a, 0xFFFF);  /* C092C2 eor #$FFFF */
      SI(0x92C5); a = alu_inc16(ss, a);            /* C092C5 inc A */
    }

    /* loc_C092C6 */
    S(0x92C6, 4);                           /* C092C6 sta $7F0D86,X */
    t_write16(ss, (0x7F0D86 + x) & 0xffffff, a);
    taken = ss_z(ss);                       /* Z is the bit/inc above, not the sta */
    S(0x92CA, 1); t_branch(ss, taken);      /* C092CA beq loc_C092DA */
    if(!taken) {
      S(0x92CC, 2);                         /* C092CC lda $9D */
      a = t_read16_dp(ss, dp, 0x9D); ss_set_nz16(ss, a);
      SIMM16(0x92CE); a = alu_and16(ss, a, 0x0003);  /* C092CE and #$0003 */
      SI(0x92D1); a = alu_inc16(ss, a);            /* C092D1 inc A */
      S(0x92D2, 2);                         /* C092D2 bit init_magic_FFFF */
      alu_bit16(ss, a, t_read16_dp(ss, dp, init_magic_FFFF));
      taken = !ss_n(ss);
      S(0x92D4, 1); t_branch(ss, taken);    /* C092D4 bpl loc_C092DA */
      if(!taken) {
        SIMM16(0x92D6); a = alu_eor16(ss, a, 0xFFFF);/* C092D6 eor #$FFFF */
        SI(0x92D9); a = alu_inc16(ss, a);          /* C092D9 inc A */
      }
    }

    /* loc_C092DA */
    S(0x92DA, 4);                           /* C092DA sta $7F0A06,X */
    t_write16(ss, (0x7F0A06 + x) & 0xffffff, a);
    S(0x92DE, 2);                           /* C092DE lda init_magic_AA55 */
    a = t_read16_dp(ss, dp, init_magic_AA55); ss_set_nz16(ss, a);
    SIMM16(0x92E0); a = alu_and16(ss, a, 0x0010);  /* C092E0 and #$0010 */
    SI(0x92E3); ss_set_c(ss, false);               /* C092E3 clc */
    SIMM16(0x92E4); a = alu_adc16(ss, a, 0x0010);  /* C092E4 adc #$0010 */
    S(0x92E7, 4);                           /* C092E7 sta $7F0986,X */
    t_write16(ss, (0x7F0986 + x) & 0xffffff, a);

  next_slot:                                /* loc_C092EB */
    SI(0x92EB); x = alu_dec16(ss, x);       /* C092EB dex */
    SI(0x92EC); x = alu_dec16(ss, x);       /* C092EC dex */
    taken = ss_n(ss);
    S(0x92ED, 1); t_branch(ss, taken);      /* C092ED bmi loc_C092F2 */
    if(taken) break;
    SJMP(0x92EF);                           /* C092EF jmp loc_C09259 */
  }

  /* loc_C092F2 */
  S(0x92F2, 1);                             /* C092F2 rts */
  ss_rts(ss);
}

/* ---------------------------------------------------------------------------
 * mode1_reset_particles_and_oam: $C0:92F3
 *
 * Mode 1's scene reset, called from $C0:BE93: zero the camera and the height
 * mask, reload the $0C1F-$0C2D weather-parameter block with mode 1's constants
 * (the same block mode0_weather_zone_update drives in mode 0), then run one
 * whole particle frame by hand: clear the sprite table, update and draw, hide
 * the unused sprites, DMA the buffer out.
 * Exit: whatever oam_dma_upload left; A = $0400, X and Y its own.
 * ------------------------------------------------------------------------- */
void mode1_reset_particles_and_oam(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint16_t dp = ss_dp(ss);

  REP(0x92F3, 0x30);                        /* C092F3 rep #$30 */
  S(0x92F5, 2); t_write16_dp(ss, dp, camera_x, 0);          /* C092F5 stz camera_x */
  S(0x92F7, 2); t_write16_dp(ss, dp, camera_y, 0);          /* C092F7 stz camera_y */
  S(0x92F9, 2); t_write16_dp(ss, dp, level_height_mask, 0); /* C092F9 stz level_height_mask */
  S(0x92FB, 3); t_write16(ss, ss_abs(ss, 0x0C1F), 0);     /* C092FB stz $0C1F */

  SIMM16(0x92FE); a = 0x0001; ss_set_nz16(ss, a);         /* C092FE lda #$0001 */
  S(0x9301, 3); t_write16(ss, ss_abs(ss, 0x0C21), a);     /* C09301 sta $0C21 */
  SIMM16(0x9304); a = 0x00FF; ss_set_nz16(ss, a);         /* C09304 lda #$00FF */
  S(0x9307, 3); t_write16(ss, ss_abs(ss, 0x0C23), a);     /* C09307 sta $0C23 */
  S(0x930A, 3); t_write16(ss, ss_abs(ss, 0x0C29), a);     /* C0930A sta $0C29 */
  SIMM16(0x930D); a = 0x0020; ss_set_nz16(ss, a);         /* C0930D lda #$0020 */
  S(0x9310, 3); t_write16(ss, ss_abs(ss, 0x0C25), a);     /* C09310 sta $0C25 */
  S(0x9313, 3); t_write16(ss, ss_abs(ss, 0x0C27), 0);     /* C09313 stz $0C27 */
  S(0x9316, 3); t_write16(ss, ss_abs(ss, 0x0C2B), 0);     /* C09316 stz $0C2B */

  S(0x9319, 2);                             /* C09319 lda init_magic_AA55 */
  a = t_read16_dp(ss, dp, init_magic_AA55); ss_set_nz16(ss, a);
  SIMM16(0x931B); a = alu_and16(ss, a, 0x3000);           /* C0931B and #$3000 */
  SIMM16(0x931E); a = alu_ora16(ss, a, 0x0E00);           /* C0931E ora #$0E00 */
  S(0x9321, 3); t_write16(ss, ss_abs(ss, 0x0C2D), a);     /* C09321 sta $0C2D */

  JSRC(0x9324, 0xA500, clear_sprite_table);               /* C09324 jsr clear_sprite_table */
  JSRC(0x9327, 0x9331, particle_update_and_draw_mode0);   /* C09327 jsr particle_update_and_draw_mode0 */
  JSRC(0x932A, 0xADE7, oam_hide_unused_sprites);          /* C0932A jsr oam_hide_unused_sprites */
  JSRC(0x932D, 0xADFD, oam_dma_upload);                   /* C0932D jsr oam_dma_upload */

  S(0x9330, 1);                             /* C09330 rts */
  ss_rts(ss);
}

/* The shared exit of particle_update_and_draw_mode0, at $C0:93F4 and again at
 * $C0:94DE: publish the OAM write pointer and put the stack pointer back from
 * $18. Two copies, identical byte for byte, so the addresses are relative to
 * whichever one the body fell into (dream_time.h, "a body reached at two
 * addresses"). */
static void particle_draw_finish(SnesState* ss, uint8_t pb, uint16_t base,
                                 uint16_t a, uint16_t x, uint16_t y) {
  const uint16_t dp = ss_dp(ss);

  S(base + 0x00, 2);                        /* C093F4 sty oam_write_ptr */
  t_write16_dp(ss, dp, oam_write_ptr, y);
  S(base + 0x02, 2);                        /* C093F6 lda $18 */
  a = t_read16_dp(ss, dp, scratch_18); ss_set_nz16(ss, a);
  SI(base + 0x04); ss_set_sp(ss, a);        /* C093F8 tcs */
  S(base + 0x05, 1);                        /* C093F9 rts */
  ss_rts(ss);
}

/* ---------------------------------------------------------------------------
 * particle_update_and_draw_mode0: $C0:9331
 *
 * One frame of the mode-0 particle field. Reached two ways: as the tail of
 * mode0_particle_draw_dispatch (jtbl_C0828A[0]) and by jsr from
 * mode1_reset_particles_and_oam.
 *
 * Per slot, backwards from $4E: a free slot ($7F0906 zero) is respawned from
 * the inline PRNG, subject to the $0C1F/$0C21 gate and shaped by the
 * $0C23-$0C2D parameter block; an occupied one is integrated, culled against
 * the $1C floor and the camera window, and emitted as one 8-byte OAM record at
 * Y. $18 holds the entry stack pointer for the whole routine because S is used
 * as scratch twice over (see the file header).
 * Exit: X = $FFFE, S restored, oam_write_ptr advanced by 8 per drawn particle.
 * ------------------------------------------------------------------------- */
void particle_update_and_draw_mode0(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint16_t dp = ss_dp(ss);
  bool taken;
  uint8_t b;

  SI(0x9331); a = ss_sp(ss); ss_set_nz16(ss, a);   /* C09331 tsc */
  S(0x9332, 2); t_write16_dp(ss, dp, scratch_18, a); /* C09332 sta $18 */
  S(0x9334, 2);                             /* C09334 lda camera_y */
  a = t_read16_dp(ss, dp, camera_y); ss_set_nz16(ss, a);
  SIMM16(0x9336); a = alu_and16(ss, a, 0x00FF);    /* C09336 and #$00FF */
  S(0x9339, 2); t_write16_dp(ss, dp, 0x1A, a);       /* C09339 sta $1A */
  S(0x933B, 2);                             /* C0933B lda level_height_mask */
  a = t_read16_dp(ss, dp, level_height_mask); ss_set_nz16(ss, a);
  SI(0x933D); ss_set_c(ss, false);                 /* C0933D clc */
  SIMM16(0x933E); a = alu_adc16(ss, a, 0x00F0);    /* C0933E adc #$00F0 */
  S(0x9341, 2); t_write16_dp(ss, dp, 0x1C, a);       /* C09341 sta $1C */
  SIMM16(0x9343); x = 0x004E; ss_set_nz16(ss, x);  /* C09343 ldx #$004E */
  S(0x9346, 2);                             /* C09346 ldy oam_write_ptr */
  y = t_read16_dp(ss, dp, oam_write_ptr); ss_set_nz16(ss, y);

  for(;;) {
    /* loc_C09348 */
    S(0x9348, 4);                           /* C09348 lda particle_table,X */
    a = t_read16(ss, (particle_table + x) & 0xffffff);
    ss_set_nz16(ss, a);
    taken = ss_z(ss);
    S(0x934C, 1); t_branch(ss, taken);      /* C0934C beq loc_C09351 */
    if(!taken) {
      SJMP(0x934E);                         /* C0934E jmp loc_C093FA */
      goto integrate;
    }

    /* loc_C09351: the respawn gate */
    S(0x9351, 3);                           /* C09351 lda $0C1F */
    a = t_read16(ss, ss_abs(ss, 0x0C1F)); ss_set_nz16(ss, a);
    taken = !ss_z(ss);
    S(0x9354, 1); t_branch(ss, taken);      /* C09354 bne loc_C0935B */
    if(!taken) {
      S(0x9356, 3);                         /* C09356 lda $0C21 */
      a = t_read16(ss, ss_abs(ss, 0x0C21)); ss_set_nz16(ss, a);
      taken = !ss_z(ss);
      S(0x9359, 1); t_branch(ss, taken);    /* C09359 bne loc_C0935E */
    } else {
      taken = false;
    }
    if(!taken) {
      SJMP(0x935B);                         /* C0935B jmp loc_C093ED */
      goto next_slot;
    }

    /* loc_C0935E */
    S(0x935E, 3); t_dec16(ss, ss_abs(ss, 0x0C21));  /* C0935E dec $0C21 */

    /* $9361-$9382: random_next, inlined with $28 for the temporary. */
    SEP(0x9361, 0x20);                      /* C09361 sep #$20 */
    S(0x9363, 2);                           /* C09363 lda $9D */
    b = t_read8_dp(ss, dp, 0x9D);
    a = (uint16_t) ((a & 0xff00) | b); ss_set_nz8(ss, b);
    S(0x9365, 2);                           /* C09365 sta sprite_frame_bank */
    t_write8_dp(ss, dp, sprite_frame_bank, (uint8_t) a);
    SI(0x9367);                             /* C09367 asl A */
    ss_set_c(ss, (a & 0x80) != 0);
    a = (uint16_t) ((a & 0xff00) | ((a << 1) & 0xff));
    ss_set_nz8(ss, (uint8_t) a);
    S(0x9368, 2);                           /* C09368 lda init_magic_FFFF */
    b = t_read8_dp(ss, dp, init_magic_FFFF);
    a = (uint16_t) ((a & 0xff00) | b); ss_set_nz8(ss, b);
    S(0x936A, 2); t_rol8_dp(ss, init_magic_FFFF);   /* C0936A rol init_magic_FFFF */
    S(0x936C, 2); t_rol8_dp(ss, init_magic_FFFF);   /* C0936C rol init_magic_FFFF */
    S(0x936E, 2);                           /* C0936E eor $9F */
    b = (uint8_t) (a ^ t_read8_dp(ss, dp, 0x9F));
    a = (uint16_t) ((a & 0xff00) | b); ss_set_nz8(ss, b);
    S(0x9370, 2); t_write8_dp(ss, dp, 0x9D, (uint8_t) a);  /* C09370 sta $9D */
    S(0x9372, 2);                           /* C09372 lda sprite_frame_bank */
    b = t_read8_dp(ss, dp, sprite_frame_bank);
    a = (uint16_t) ((a & 0xff00) | b); ss_set_nz8(ss, b);
    S(0x9374, 2); t_write8_dp(ss, dp, 0x9F, (uint8_t) a);  /* C09374 sta $9F */
    S(0x9376, 2);                           /* C09376 eor init_magic_FFFF */
    b = (uint8_t) (a ^ t_read8_dp(ss, dp, init_magic_FFFF));
    a = (uint16_t) ((a & 0xff00) | b); ss_set_nz8(ss, b);
    S(0x9378, 2);                           /* C09378 sta sprite_frame_bank */
    t_write8_dp(ss, dp, sprite_frame_bank, (uint8_t) a);
    S(0x937A, 2);                           /* C0937A lda init_magic_AA55 */
    b = t_read8_dp(ss, dp, init_magic_AA55);
    a = (uint16_t) ((a & 0xff00) | b); ss_set_nz8(ss, b);
    S(0x937C, 2);                           /* C0937C sta init_magic_FFFF */
    t_write8_dp(ss, dp, init_magic_FFFF, (uint8_t) a);
    S(0x937E, 2);                           /* C0937E lda sprite_frame_bank */
    b = t_read8_dp(ss, dp, sprite_frame_bank);
    a = (uint16_t) ((a & 0xff00) | b); ss_set_nz8(ss, b);
    S(0x9380, 2);                           /* C09380 sta init_magic_AA55 */
    t_write8_dp(ss, dp, init_magic_AA55, (uint8_t) a);
    REP(0x9382, 0x20);                      /* C09382 rep #$20 */

    S(0x9384, 2);                           /* C09384 lda init_magic_AA55 */
    a = t_read16_dp(ss, dp, init_magic_AA55); ss_set_nz16(ss, a);
    S(0x9386, 3);                           /* C09386 and $0C29 */
    a = alu_and16(ss, a, t_read16(ss, ss_abs(ss, 0x0C29)));
    S(0x9389, 3);                           /* C09389 adc $0C27 */
    a = alu_adc16(ss, a, t_read16(ss, ss_abs(ss, 0x0C27)));
    S(0x938C, 4);                           /* C0938C sta $7F0B06,X */
    t_write16(ss, (0x7F0B06 + x) & 0xffffff, a);

    S(0x9390, 2);                           /* C09390 lda init_magic_FFFF */
    a = t_read16_dp(ss, dp, init_magic_FFFF); ss_set_nz16(ss, a);
    S(0x9392, 3);                           /* C09392 and $0C23 */
    a = alu_and16(ss, a, t_read16(ss, ss_abs(ss, 0x0C23)));
    S(0x9395, 3);                           /* C09395 adc $0C25 */
    a = alu_adc16(ss, a, t_read16(ss, ss_abs(ss, 0x0C25)));
    S(0x9398, 4);                           /* C09398 sta $7F0D06,X */
    t_write16(ss, (0x7F0D06 + x) & 0xffffff, a);
    SI(0x939C); a = alu_lsr16(ss, a);       /* C0939C lsr A */
    S(0x939D, 4);                           /* C0939D sta $7F0E06,X */
    t_write16(ss, (0x7F0E06 + x) & 0xffffff, a);

    S(0x93A1, 3);                           /* C093A1 lda $0C2D */
    a = t_read16(ss, ss_abs(ss, 0x0C2D)); ss_set_nz16(ss, a);
    S(0x93A4, 4);                           /* C093A4 sta particle_table,X */
    t_write16(ss, (particle_table + x) & 0xffffff, a);

    S(0x93A8, 2);                           /* C093A8 lda $9F */
    a = t_read16_dp(ss, dp, 0x9F); ss_set_nz16(ss, a);
    SIMM16(0x93AA); a = alu_and16(ss, a, 0x0030);   /* C093AA and #$0030 */
    SIMM16(0x93AD); alu_cmp16(ss, a, 0x0030);       /* C093AD cmp #$0030 */
    taken = !ss_c(ss);
    S(0x93B0, 1); t_branch(ss, taken);      /* C093B0 bcc loc_C093B3 */
    if(!taken) {
      SI(0x93B2); a = ss_dp(ss); ss_set_nz16(ss, a);/* C093B2 tdc */
    }

    /* loc_C093B3 */
    S(0x93B3, 4);                           /* C093B3 sta $7F0986,X */
    t_write16(ss, (0x7F0986 + x) & 0xffffff, a);
    S(0x93B7, 4);                           /* C093B7 sta $7F0C86,X */
    t_write16(ss, (0x7F0C86 + x) & 0xffffff, a);
    S(0x93BB, 4);                           /* C093BB sta $7F0B86,X */
    t_write16(ss, (0x7F0B86 + x) & 0xffffff, a);

    S(0x93BF, 2);                           /* C093BF lda init_magic_FFFF */
    a = t_read16_dp(ss, dp, init_magic_FFFF); ss_set_nz16(ss, a);
    S(0x93C1, 3);                           /* C093C1 and $0C2B */
    a = alu_and16(ss, a, t_read16(ss, ss_abs(ss, 0x0C2B)));
    S(0x93C4, 4);                           /* C093C4 sta $7F0A06,X */
    t_write16(ss, (0x7F0A06 + x) & 0xffffff, a);

    S(0x93C8, 2);                           /* C093C8 lda init_magic_AA55 */
    a = t_read16_dp(ss, dp, init_magic_AA55); ss_set_nz16(ss, a);
    SIMM16(0x93CA); a = alu_and16(ss, a, 0x7FFF);   /* C093CA and #$7FFF */
    S(0x93CD, 4);                           /* C093CD sta $7F0D86,X */
    t_write16(ss, (0x7F0D86 + x) & 0xffffff, a);
    S(0x93D1, 2);                           /* C093D1 bit init_magic_FFFF */
    alu_bit16(ss, a, t_read16_dp(ss, dp, init_magic_FFFF));
    taken = !ss_n(ss);
    S(0x93D3, 1); t_branch(ss, taken);      /* C093D3 bpl loc_C093E7 */
    if(!taken) {
      SIMM16(0x93D5); a = alu_eor16(ss, a, 0xFFFF);   /* C093D5 eor #$FFFF */
      S(0x93D8, 4);                         /* C093D8 sta $7F0D86,X */
      t_write16(ss, (0x7F0D86 + x) & 0xffffff, a);
      S(0x93DC, 4);                         /* C093DC lda $7F0A06,X */
      a = t_read16(ss, (0x7F0A06 + x) & 0xffffff); ss_set_nz16(ss, a);
      SIMM16(0x93E0); a = alu_eor16(ss, a, 0xFFFF);   /* C093E0 eor #$FFFF */
      S(0x93E3, 4);                         /* C093E3 sta $7F0A06,X */
      t_write16(ss, (0x7F0A06 + x) & 0xffffff, a);
    }

    /* loc_C093E7 */
    S(0x93E7, 2);                           /* C093E7 lda camera_y */
    a = t_read16_dp(ss, dp, camera_y); ss_set_nz16(ss, a);
    S(0x93E9, 4);                           /* C093E9 sta $7F0A86,X */
    t_write16(ss, (0x7F0A86 + x) & 0xffffff, a);

  next_slot:                                /* loc_C093ED */
    SI(0x93ED); x = alu_dec16(ss, x);       /* C093ED dex */
    SI(0x93EE); x = alu_dec16(ss, x);       /* C093EE dex */
    taken = ss_n(ss);
    S(0x93EF, 1); t_branch(ss, taken);      /* C093EF bmi loc_C093F4 */
    if(taken) { particle_draw_finish(ss, pb, 0x93F4, a, x, y); return; }
    SJMP(0x93F1);                           /* C093F1 jmp loc_C09348 */
    continue;

  integrate:                                /* loc_C093FA */
    S(0x93FA, 4);                           /* C093FA lda $7F0C06,X */
    a = t_read16(ss, (0x7F0C06 + x) & 0xffffff); ss_set_nz16(ss, a);
    SI(0x93FE); ss_set_c(ss, false);        /* C093FE clc */
    S(0x93FF, 4);                           /* C093FF adc $7F0E06,X */
    a = alu_adc16(ss, a, t_read16(ss, (0x7F0E06 + x) & 0xffffff));
    S(0x9403, 4);                           /* C09403 sta $7F0C06,X */
    t_write16(ss, (0x7F0C06 + x) & 0xffffff, a);

    S(0x9407, 1);                           /* C09407 xba */
    a = (uint16_t) ((a << 8) | (a >> 8));
    ss_set_nz8(ss, (uint8_t) a);
    ss_idle(ss);
    ss_check_int(ss);
    ss_idle(ss);

    SI(0x9408); ss_set_sp(ss, x);           /* C09408 txs: the slot index parks in S */
    SIMM16(0x9409); a = alu_and16(ss, a, 0x001F);   /* C09409 and #$001F */
    SIMM16(0x940C); alu_cmp16(ss, a, 0x0010);       /* C0940C cmp #$0010 */
    taken = !ss_c(ss);
    S(0x940F, 1); t_branch(ss, taken);      /* C0940F bcc loc_C09425 */
    if(!taken) {
      S(0x9411, 2); t_write16_dp(ss, dp, ptr_04, a);  /* C09411 sta ptr_04 */
      S(0x9413, 4);                         /* C09413 lda $7F0986,X */
      a = t_read16(ss, (0x7F0986 + x) & 0xffffff); ss_set_nz16(ss, a);
      taken = !ss_z(ss);
      S(0x9417, 1); t_branch(ss, taken);    /* C09417 bne loc_C09420 */
      if(!taken) {
        S(0x9419, 2);                       /* C09419 lda ptr_04 */
        a = t_read16_dp(ss, dp, ptr_04); ss_set_nz16(ss, a);
        SIMM16(0x941B); a = alu_eor16(ss, a, 0x001F); /* C0941B eor #$001F */
        S(0x941E, 2); t_write16_dp(ss, dp, ptr_04, a);/* C0941E sta ptr_04 */
      }
      /* loc_C09420 */
      S(0x9420, 2);                         /* C09420 lda ptr_04 */
      a = t_read16_dp(ss, dp, ptr_04); ss_set_nz16(ss, a);
      SIMM16(0x9422); a = alu_and16(ss, a, 0x000F);   /* C09422 and #$000F */
    }

    /* loc_C09425 */
    S(0x9425, 4);                           /* C09425 ora $7F0986,X */
    a = alu_ora16(ss, a, t_read16(ss, (0x7F0986 + x) & 0xffffff));
    SI(0x9429); a = alu_asl16(ss, a);       /* C09429 asl A */
    S(0x942A, 2); t_write16_dp(ss, dp, 0x08, a);      /* C0942A sta $08 */
    SI(0x942C); x = a; ss_set_nz16(ss, x);  /* C0942C tax */
    S(0x942D, 4);                           /* C0942D lda data_C50200,X */
    a = t_read16(ss, (0xC50200 + x) & 0xffffff); ss_set_nz16(ss, a);
    S(0x9431, 2); t_write16_dp(ss, dp, ptr_04, a);    /* C09431 sta ptr_04 */
    S(0x9433, 4);                           /* C09433 lda data_C50260,X */
    a = t_read16(ss, (0xC50260 + x) & 0xffffff); ss_set_nz16(ss, a);
    S(0x9437, 2); t_write16_dp(ss, dp, 0x06, a);      /* C09437 sta $06 */
    SI(0x9439); x = ss_sp(ss); ss_set_nz16(ss, x);  /* C09439 tsx: slot index back */

    S(0x943A, 4);                           /* C0943A lda $7F0C86,X */
    a = t_read16(ss, (0x7F0C86 + x) & 0xffffff); ss_set_nz16(ss, a);
    SI(0x943E); ss_set_c(ss, false);        /* C0943E clc */
    S(0x943F, 4);                           /* C0943F adc $7F0D86,X */
    a = alu_adc16(ss, a, t_read16(ss, (0x7F0D86 + x) & 0xffffff));
    S(0x9443, 4);                           /* C09443 sta $7F0C86,X */
    t_write16(ss, (0x7F0C86 + x) & 0xffffff, a);
    S(0x9447, 4);                           /* C09447 lda $7F0D86,X */
    a = t_read16(ss, (0x7F0D86 + x) & 0xffffff); ss_set_nz16(ss, a);
    S(0x944B, 4);                           /* C0944B lda $7F0B06,X */
    a = t_read16(ss, (0x7F0B06 + x) & 0xffffff); ss_set_nz16(ss, a);
    S(0x944F, 4);                           /* C0944F adc $7F0A06,X */
    a = alu_adc16(ss, a, t_read16(ss, (0x7F0A06 + x) & 0xffffff));
    S(0x9453, 4);                           /* C09453 sta $7F0B06,X */
    t_write16(ss, (0x7F0B06 + x) & 0xffffff, a);

    S(0x9457, 4);                           /* C09457 lda $7F0B86,X */
    a = t_read16(ss, (0x7F0B86 + x) & 0xffffff); ss_set_nz16(ss, a);
    SI(0x945B); ss_set_c(ss, false);        /* C0945B clc */
    S(0x945C, 4);                           /* C0945C adc $7F0D06,X */
    a = alu_adc16(ss, a, t_read16(ss, (0x7F0D06 + x) & 0xffffff));
    S(0x9460, 4);                           /* C09460 sta $7F0B86,X */
    t_write16(ss, (0x7F0B86 + x) & 0xffffff, a);

    S(0x9464, 4);                           /* C09464 lda $7F0A87,X */
    a = t_read16(ss, (0x7F0A87 + x) & 0xffffff); ss_set_nz16(ss, a);
    SIMM16(0x9468); a = alu_adc16(ss, a, 0x0000);   /* C09468 adc #$0000 */
    S(0x946B, 4);                           /* C0946B sta $7F0A87,X */
    t_write16(ss, (0x7F0A87 + x) & 0xffffff, a);

    S(0x946F, 4);                           /* C0946F lda $7F0B87,X */
    a = t_read16(ss, (0x7F0B87 + x) & 0xffffff); ss_set_nz16(ss, a);
    SIMM16(0x9473); a = alu_and16(ss, a, 0x00FF);   /* C09473 and #$00FF */
    SI(0x9476); ss_set_c(ss, false);        /* C09476 clc */
    S(0x9477, 4);                           /* C09477 adc $7F0A86,X */
    a = alu_adc16(ss, a, t_read16(ss, (0x7F0A86 + x) & 0xffffff));
    S(0x947B, 2); t_write16_dp(ss, dp, 0x1E, a);      /* C0947B sta $1E */
    S(0x947D, 2);                           /* C0947D cmp $1C */
    alu_cmp16(ss, a, t_read16_dp(ss, dp, 0x1C));
    taken = !ss_c(ss);
    S(0x947F, 1); t_branch(ss, taken);      /* C0947F bcc loc_C09488 */
    if(!taken) {
      SI(0x9481); a = ss_dp(ss); ss_set_nz16(ss, a);/* C09481 tdc */
      S(0x9482, 4);                         /* C09482 sta particle_table,X */
      t_write16(ss, (particle_table + x) & 0xffffff, a);
      S(0x9486, 1); t_branch(ss, true);     /* C09486 bra loc_C094D7 */
      goto next_slot2;
    }

    /* loc_C09488 */
    S(0x9488, 4);                           /* C09488 lda $7F0B06,X */
    a = t_read16(ss, (0x7F0B06 + x) & 0xffffff); ss_set_nz16(ss, a);
    SI(0x948C); ss_set_c(ss, true);         /* C0948C sec */
    S(0x948D, 2);                           /* C0948D sbc camera_x */
    a = alu_sbc16(ss, a, t_read16_dp(ss, dp, camera_x));
    taken = ss_n(ss);
    S(0x948F, 1); t_branch(ss, taken);      /* C0948F bmi loc_C094D7 */
    if(taken) goto next_slot2;
    SIMM16(0x9491); alu_cmp16(ss, a, 0x00F0); /* C09491 cmp #$00F0 */
    taken = ss_c(ss);
    S(0x9494, 1); t_branch(ss, taken);      /* C09494 bcs loc_C094D7 */
    if(taken) goto next_slot2;

    SI(0x9496); ss_set_c(ss, true);         /* C09496 sec */
    SIMM16(0x9497); a = alu_sbc16(ss, a, 0x0080);   /* C09497 sbc #$0080 */
    SI(0x949A); ss_set_sp(ss, a);           /* C0949A tcs: the OAM x parks in S */
    SI(0x949B); ss_set_c(ss, false);        /* C0949B clc */
    S(0x949C, 2);                           /* C0949C adc ptr_04 */
    a = alu_adc16(ss, a, t_read16_dp(ss, dp, ptr_04));
    S(0x949E, 3); t_index(ss);              /* C0949E sta nmi_handler_ptr,Y */
    t_write16(ss, ss_abs(ss, (nmi_handler_ptr + y)), a);
    SI(0x94A1); a = ss_sp(ss); ss_set_nz16(ss, a);  /* C094A1 tsc */
    SI(0x94A2); ss_set_c(ss, false);        /* C094A2 clc */
    S(0x94A3, 2);                           /* C094A3 adc $06 */
    a = alu_adc16(ss, a, t_read16_dp(ss, dp, 0x06));
    S(0x94A5, 3); t_index(ss);              /* C094A5 sta ptr_04,Y */
    t_write16(ss, ss_abs(ss, (ptr_04 + y)), a);

    S(0x94A8, 2);                           /* C094A8 lda $1E */
    a = t_read16_dp(ss, dp, 0x1E); ss_set_nz16(ss, a);
    SI(0x94AA); ss_set_c(ss, true);         /* C094AA sec */
    S(0x94AB, 2);                           /* C094AB sbc $1A */
    a = alu_sbc16(ss, a, t_read16_dp(ss, dp, 0x1A));
    SIMM16(0x94AD); alu_cmp16(ss, a, 0x00F0); /* C094AD cmp #$00F0 */
    taken = ss_c(ss);
    S(0x94B0, 1); t_branch(ss, taken);      /* C094B0 bcs loc_C094D7 */
    if(taken) goto next_slot2;

    SI(0x94B2); ss_set_c(ss, true);         /* C094B2 sec */
    SIMM16(0x94B3); a = alu_sbc16(ss, a, 0x0080);   /* C094B3 sbc #$0080 */
    SI(0x94B6); ss_set_sp(ss, a);           /* C094B6 tcs: the OAM y parks in S */
    SI(0x94B7); ss_set_c(ss, false);        /* C094B7 clc */
    S(0x94B8, 2);                           /* C094B8 adc $05 */
    a = alu_adc16(ss, a, t_read16_dp(ss, dp, 0x05));
    S(0x94BA, 3); t_index(ss);              /* C094BA sta $0001,Y */
    t_write16(ss, ss_abs(ss, (0x0001 + y)), a);
    SI(0x94BD); a = ss_sp(ss); ss_set_nz16(ss, a);  /* C094BD tsc */
    SI(0x94BE); ss_set_c(ss, false);        /* C094BE clc */
    S(0x94BF, 2);                           /* C094BF adc spc_dest_addr */
    a = alu_adc16(ss, a, t_read16_dp(ss, dp, spc_dest_addr));
    S(0x94C1, 3); t_index(ss);              /* C094C1 sta $0005,Y */
    t_write16(ss, ss_abs(ss, (0x0005 + y)), a);

    S(0x94C4, 2);                           /* C094C4 lda $08 */
    a = t_read16_dp(ss, dp, 0x08); ss_set_nz16(ss, a);
    SI(0x94C6); ss_set_c(ss, false);        /* C094C6 clc */
    S(0x94C7, 4);                           /* C094C7 adc particle_table,X */
    a = alu_adc16(ss, a, t_read16(ss, (particle_table + x) & 0xffffff));
    S(0x94CB, 3); t_index(ss);              /* C094CB sta dma_pending_mask,Y */
    t_write16(ss, ss_abs(ss, (dma_pending_mask + y)), a);
    SI(0x94CE); a = alu_inc16(ss, a);       /* C094CE inc A */
    S(0x94CF, 3); t_index(ss);              /* C094CF sta $0006,Y */
    t_write16(ss, ss_abs(ss, (0x0006 + y)), a);
    SI(0x94D2); a = y; ss_set_nz16(ss, a);  /* C094D2 tya */
    SIMM16(0x94D3); a = alu_adc16(ss, a, 0x0008);   /* C094D3 adc #$0008 */
    SI(0x94D6); y = a; ss_set_nz16(ss, y);  /* C094D6 tay */

  next_slot2:                               /* loc_C094D7 */
    SI(0x94D7); x = alu_dec16(ss, x);       /* C094D7 dex */
    SI(0x94D8); x = alu_dec16(ss, x);       /* C094D8 dex */
    taken = ss_n(ss);
    S(0x94D9, 1); t_branch(ss, taken);      /* C094D9 bmi loc_C094DE */
    if(taken) { particle_draw_finish(ss, pb, 0x94DE, a, x, y); return; }
    SJMP(0x94DB);                           /* C094DB jmp loc_C09348 */
  }
}

/* ---------------------------------------------------------------------------
 * sparkle_array_init: $C0:94E4
 *
 * Called from mode2_level_init. Seeds the thirty-two 8-byte "sparkle" records
 * at $7F:0E86 (index $F8 down to 0, step 8) with random_next values: position
 * $0E87 masked to $07FF, $0E89 a $60-$DF band, $0E8A a signed speed.
 * Exit: A = $FFF8, N set, X = $0000.
 * ------------------------------------------------------------------------- */
void sparkle_array_init(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint16_t dp = ss_dp(ss);
  bool taken;

  SIMM16(0x94E4); a = 0x00F8; ss_set_nz16(ss, a);   /* C094E4 lda #$00F8 */

  do {
    /* loc_C094E7 */
    SI(0x94E7); x = a; ss_set_nz16(ss, x);        /* C094E7 tax */
    SI(0x94E8); a = ss_dp(ss); ss_set_nz16(ss, a);/* C094E8 tdc */
    S(0x94E9, 4);                           /* C094E9 sta $7F0E86,X */
    t_write16(ss, (0x7F0E86 + x) & 0xffffff, a);

    JSRC(0x94ED, 0xA212, random_next);      /* C094ED jsr random_next */

    S(0x94F0, 2);                           /* C094F0 lda init_magic_AA55 */
    a = t_read16_dp(ss, dp, init_magic_AA55); ss_set_nz16(ss, a);
    SIMM16(0x94F2); a = alu_and16(ss, a, 0x07FF);   /* C094F2 and #$07FF */
    S(0x94F5, 4);                           /* C094F5 sta $7F0E87,X */
    t_write16(ss, (0x7F0E87 + x) & 0xffffff, a);

    S(0x94F9, 2);                           /* C094F9 lda init_magic_FFFF */
    a = t_read16_dp(ss, dp, init_magic_FFFF); ss_set_nz16(ss, a);
    SIMM16(0x94FB); a = alu_and16(ss, a, 0x007F);   /* C094FB and #$007F */
    SIMM16(0x94FE); a = alu_adc16(ss, a, 0x0060);   /* C094FE adc #$0060 */
    S(0x9501, 4);                           /* C09501 sta $7F0E89,X */
    t_write16(ss, (0x7F0E89 + x) & 0xffffff, a);

    S(0x9505, 2);                           /* C09505 lda init_magic_FFFF */
    a = t_read16_dp(ss, dp, init_magic_FFFF); ss_set_nz16(ss, a);
    SIMM16(0x9507); a = alu_and16(ss, a, 0x007F);   /* C09507 and #$007F */
    SIMM16(0x950A); a = alu_adc16(ss, a, 0x0040);   /* C0950A adc #$0040 */
    S(0x950D, 2);                           /* C0950D bit init_magic_FFFF */
    alu_bit16(ss, a, t_read16_dp(ss, dp, init_magic_FFFF));
    taken = !ss_n(ss);
    S(0x950F, 1); t_branch(ss, taken);      /* C0950F bpl loc_C09515 */
    if(!taken) {
      SIMM16(0x9511); a = alu_eor16(ss, a, 0xFFFF); /* C09511 eor #$FFFF */
      SI(0x9514); a = alu_inc16(ss, a);           /* C09514 inc A */
    }

    /* loc_C09515 */
    S(0x9515, 4);                           /* C09515 sta $7F0E8A,X */
    t_write16(ss, (0x7F0E8A + x) & 0xffffff, a);
    SI(0x9519); a = x; ss_set_nz16(ss, a);        /* C09519 txa */
    SI(0x951A); ss_set_c(ss, true);               /* C0951A sec */
    SIMM16(0x951B); a = alu_sbc16(ss, a, 0x0008);   /* C0951B sbc #$0008 */
    taken = !ss_n(ss);
    S(0x951E, 1); t_branch(ss, taken);      /* C0951E bpl loc_C094E7 */
  } while(taken);

  S(0x9520, 1);                             /* C09520 rts */
  ss_rts(ss);
}

/* ---------------------------------------------------------------------------
 * sparkle_update_and_draw: $C0:9521
 *
 * The mode-1 half of mode1_particle_dispatch. Walks the $7F:0E86 records from
 * $F8 down, emits an 8-byte OAM record for each one inside the camera window
 * (two sprites, the second $08 to one side depending on the sign of the speed
 * in $06), integrates the 16.16 position in $0E86/$0E88 and flips the speed at
 * the $0580 wrap. Stops early when oam_write_ptr reaches $0400, which is also
 * the entry precondition.
 * Exit: oam_write_ptr updated; C set on the early-out at $9528.
 * ------------------------------------------------------------------------- */
void sparkle_update_and_draw(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint16_t dp = ss_dp(ss);
  bool taken;

  S(0x9521, 2);                             /* C09521 ldy oam_write_ptr */
  y = t_read16_dp(ss, dp, oam_write_ptr); ss_set_nz16(ss, y);
  SIMM16(0x9523); alu_cpx16(ss, y, 0x0400);   /* C09523 cpy #$0400 */
  taken = !ss_c(ss);
  S(0x9526, 1); t_branch(ss, taken);        /* C09526 bcc loc_C09529 */
  if(!taken) {
    S(0x9528, 1);                           /* C09528 rts */
    ss_rts(ss);
    return;
  }

  /* loc_C09529 */
  SIMM16(0x9529); a = 0x00F8; ss_set_nz16(ss, a);   /* C09529 lda #$00F8 */

  for(;;) {
    /* loc_C0952C */
    SI(0x952C); x = a; ss_set_nz16(ss, x);  /* C0952C tax */
    S(0x952D, 4);                           /* C0952D lda $7F0E8A,X */
    a = t_read16(ss, (0x7F0E8A + x) & 0xffffff); ss_set_nz16(ss, a);
    S(0x9531, 2); t_write16_dp(ss, dp, 0x06, a);    /* C09531 sta $06 */
    S(0x9533, 4);                           /* C09533 lda $7F0E87,X */
    a = t_read16(ss, (0x7F0E87 + x) & 0xffffff); ss_set_nz16(ss, a);
    SI(0x9537); ss_set_c(ss, true);         /* C09537 sec */
    S(0x9538, 2);                           /* C09538 sbc camera_x */
    a = alu_sbc16(ss, a, t_read16_dp(ss, dp, camera_x));
    taken = ss_n(ss);
    S(0x953A, 1); t_branch(ss, taken);      /* C0953A bmi loc_C09593 */
    if(taken) goto integrate;
    SIMM16(0x953C); alu_cmp16(ss, a, 0x0100); /* C0953C cmp #$0100 */
    taken = ss_c(ss);
    S(0x953F, 1); t_branch(ss, taken);      /* C0953F bcs loc_C09593 */
    if(taken) goto integrate;

    S(0x9541, 3); t_index(ss);              /* C09541 sta nmi_handler_ptr,Y */
    t_write16(ss, ss_abs(ss, (nmi_handler_ptr + y)), a);
    SIMM16(0x9544); a = alu_adc16(ss, a, 0x0008);   /* C09544 adc #$0008 */
    S(0x9547, 2);                           /* C09547 bit $06 */
    alu_bit16(ss, a, t_read16_dp(ss, dp, 0x06));
    taken = ss_n(ss);
    S(0x9549, 1); t_branch(ss, taken);      /* C09549 bmi loc_C0954E */
    if(!taken) {
      SIMM16(0x954B); a = alu_adc16(ss, a, 0xFFF0); /* C0954B adc #$FFF0 */
    }

    /* loc_C0954E */
    S(0x954E, 3); t_index(ss);              /* C0954E sta ptr_04,Y */
    t_write16(ss, ss_abs(ss, (ptr_04 + y)), a);

    S(0x9551, 1);                           /* C09551 phx */
    ss_idle(ss);
    ss_push8(ss, (uint8_t) (x >> 8));
    ss_check_int(ss);
    ss_push8(ss, (uint8_t) x);

    S(0x9552, 4);                           /* C09552 lda $7F0E87,X */
    a = t_read16(ss, (0x7F0E87 + x) & 0xffffff); ss_set_nz16(ss, a);
    SIMM16(0x9556); a = alu_and16(ss, a, 0x001E);   /* C09556 and #$001E */
    SI(0x9559); x = a; ss_set_nz16(ss, x);        /* C09559 tax */
    S(0x955A, 4);                           /* C0955A lda data_C084B5,X */
    a = t_read16(ss, (0x8084B5 + x) & 0xffffff); ss_set_nz16(ss, a);

    S(0x955E, 1);                           /* C0955E plx */
    ss_idle(ss);
    ss_idle(ss);
    {
      const uint8_t lo = ss_pull8(ss);
      ss_check_int(ss);
      const uint8_t hi = ss_pull8(ss);
      x = (uint16_t) (lo | (hi << 8));
    }
    ss_set_nz16(ss, x);

    SI(0x955F); ss_set_c(ss, false);        /* C0955F clc */
    S(0x9560, 4);                           /* C09560 adc $7F0E89,X */
    a = alu_adc16(ss, a, t_read16(ss, (0x7F0E89 + x) & 0xffffff));
    S(0x9564, 3); t_index(ss);              /* C09564 sta $0001,Y */
    t_write16(ss, ss_abs(ss, (0x0001 + y)), a);
    S(0x9567, 3); t_index(ss);              /* C09567 sta $0005,Y */
    t_write16(ss, ss_abs(ss, (0x0005 + y)), a);

    S(0x956A, 4);                           /* C0956A lda $7F0E8C,X */
    a = t_read16(ss, (0x7F0E8C + x) & 0xffffff); ss_set_nz16(ss, a);
    SI(0x956E); ss_set_c(ss, false);        /* C0956E clc */
    SIMM16(0x956F); a = alu_adc16(ss, a, 0x0040);   /* C0956F adc #$0040 */
    S(0x9572, 4);                           /* C09572 sta $7F0E8C,X */
    t_write16(ss, (0x7F0E8C + x) & 0xffffff, a);

    S(0x9576, 1);                           /* C09576 xba */
    a = (uint16_t) ((a << 8) | (a >> 8));
    ss_set_nz8(ss, (uint8_t) a);
    ss_idle(ss);
    ss_check_int(ss);
    ss_idle(ss);

    SIMM16(0x9577); a = alu_and16(ss, a, 0x0007);   /* C09577 and #$0007 */
    SI(0x957A); a = alu_asl16(ss, a);             /* C0957A asl A */
    SI(0x957B); ss_set_c(ss, false);              /* C0957B clc */
    SIMM16(0x957C); a = alu_adc16(ss, a, 0x17E0);   /* C0957C adc #$17E0 */
    S(0x957F, 2);                           /* C0957F bit $06 */
    alu_bit16(ss, a, t_read16_dp(ss, dp, 0x06));
    taken = !ss_n(ss);
    S(0x9581, 1); t_branch(ss, taken);      /* C09581 bpl loc_C09586 */
    if(!taken) {
      SIMM16(0x9583); a = alu_ora16(ss, a, 0x4000); /* C09583 ora #$4000 */
    }

    /* loc_C09586 */
    S(0x9586, 3); t_index(ss);              /* C09586 sta dma_pending_mask,Y */
    t_write16(ss, ss_abs(ss, (dma_pending_mask + y)), a);
    SI(0x9589); a = alu_inc16(ss, a);       /* C09589 inc A */
    S(0x958A, 3); t_index(ss);              /* C0958A sta $0006,Y */
    t_write16(ss, ss_abs(ss, (0x0006 + y)), a);
    SI(0x958D); a = y; ss_set_nz16(ss, a);  /* C0958D tya */
    SI(0x958E); ss_set_c(ss, false);        /* C0958E clc */
    SIMM16(0x958F); a = alu_adc16(ss, a, 0x0008);   /* C0958F adc #$0008 */
    SI(0x9592); y = a; ss_set_nz16(ss, y);  /* C09592 tay */

  integrate:                                /* loc_C09593 */
    S(0x9593, 2); t_write16_dp(ss, dp, ptr_04, 0);  /* C09593 stz ptr_04 */
    S(0x9595, 2);                           /* C09595 bit $06 */
    alu_bit16(ss, a, t_read16_dp(ss, dp, 0x06));
    taken = !ss_n(ss);
    S(0x9597, 1); t_branch(ss, taken);      /* C09597 bpl loc_C0959B */
    if(!taken) {
      S(0x9599, 2);                         /* C09599 dec ptr_04 */
      t_dec16(ss, (uint32_t) (uint16_t) (dp + ptr_04));
    }

    /* loc_C0959B */
    S(0x959B, 4);                           /* C0959B lda $7F0E86,X */
    a = t_read16(ss, (0x7F0E86 + x) & 0xffffff); ss_set_nz16(ss, a);
    S(0x959F, 4);                           /* C0959F adc $7F0E8A,X */
    a = alu_adc16(ss, a, t_read16(ss, (0x7F0E8A + x) & 0xffffff));
    S(0x95A3, 4);                           /* C095A3 sta $7F0E86,X */
    t_write16(ss, (0x7F0E86 + x) & 0xffffff, a);
    S(0x95A7, 4);                           /* C095A7 lda $7F0E88,X */
    a = t_read16(ss, (0x7F0E88 + x) & 0xffffff); ss_set_nz16(ss, a);
    S(0x95AB, 2);                           /* C095AB adc ptr_04 */
    a = alu_adc16(ss, a, t_read16_dp(ss, dp, ptr_04));
    S(0x95AD, 4);                           /* C095AD sta $7F0E88,X */
    t_write16(ss, (0x7F0E88 + x) & 0xffffff, a);

    S(0x95B1, 4);                           /* C095B1 lda $7F0E87,X */
    a = t_read16(ss, (0x7F0E87 + x) & 0xffffff); ss_set_nz16(ss, a);
    S(0x95B5, 2);                           /* C095B5 bit $06 */
    alu_bit16(ss, a, t_read16_dp(ss, dp, 0x06));
    taken = !ss_n(ss);
    S(0x95B7, 1); t_branch(ss, taken);      /* C095B7 bpl loc_C095C0 */
    if(!taken) {
      SIMM16(0x95B9); a = alu_and16(ss, a, 0xFFFF); /* C095B9 and #$FFFF */
      taken = ss_n(ss);
      S(0x95BC, 1); t_branch(ss, taken);    /* C095BC bmi loc_C095C5 */
      if(!taken) {
        S(0x95BE, 1); t_branch(ss, true);   /* C095BE bra loc_C095D1 */
        goto advance;
      }
    } else {
      /* loc_C095C0 */
      SIMM16(0x95C0); alu_cmp16(ss, a, 0x0580);   /* C095C0 cmp #$0580 */
      taken = !ss_c(ss);
      S(0x95C3, 1); t_branch(ss, taken);    /* C095C3 bcc loc_C095D1 */
      if(taken) goto advance;
    }

    /* loc_C095C5 */
    S(0x95C5, 4);                           /* C095C5 lda $7F0E8A,X */
    a = t_read16(ss, (0x7F0E8A + x) & 0xffffff); ss_set_nz16(ss, a);
    SIMM16(0x95C9); a = alu_eor16(ss, a, 0xFFFF);   /* C095C9 eor #$FFFF */
    SI(0x95CC); a = alu_inc16(ss, a);             /* C095CC inc A */
    S(0x95CD, 4);                           /* C095CD sta $7F0E8A,X */
    t_write16(ss, (0x7F0E8A + x) & 0xffffff, a);

  advance:                                  /* loc_C095D1 */
    SIMM16(0x95D1); alu_cpx16(ss, y, 0x0400); /* C095D1 cpy #$0400 */
    taken = ss_c(ss);
    S(0x95D4, 1); t_branch(ss, taken);      /* C095D4 bcs loc_C095E0 */
    if(taken) break;
    SI(0x95D6); a = x; ss_set_nz16(ss, a);  /* C095D6 txa */
    SI(0x95D7); ss_set_c(ss, true);         /* C095D7 sec */
    SIMM16(0x95D8); a = alu_sbc16(ss, a, 0x0008);   /* C095D8 sbc #$0008 */
    taken = ss_n(ss);
    S(0x95DB, 1); t_branch(ss, taken);      /* C095DB bmi loc_C095E0 */
    if(taken) break;
    SJMP(0x95DD);                           /* C095DD jmp loc_C0952C */
  }

  /* loc_C095E0 */
  S(0x95E0, 2); t_write16_dp(ss, dp, oam_write_ptr, y);  /* C095E0 sty oam_write_ptr */
  S(0x95E2, 1);                             /* C095E2 rts */
  ss_rts(ss);
}

/* ---------------------------------------------------------------------------
 * particle_spawn_from_table: $C0:95E3
 *
 * Called from mode2_level_init. Builds the solid particle tile at VRAM $1F00,
 * then seeds slots from the per-mode spawn list at data_C0B26C (4 bytes per
 * entry, x then y, terminated by a negative x), mixing in random_next for
 * velocity, phase and lifetime. Slots the list did not reach are marked $FFFF.
 *
 * DB is $7F for the body (pea $807F / plb), which makes the $0A06,Y stores
 * land in the particle table; the closing plb pulls the $80 back.
 * vram_generate_particle_tile is another agent's routine and is not converted
 * here, so the hook builds its jsr frame and lets the reference CPU run it.
 * Exit: A = the last random value, Y = $FFFE, DB = $80.
 * ------------------------------------------------------------------------- */
void particle_spawn_from_table(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint16_t dp = ss_dp(ss);
  bool taken;

  SIMM16(0x95E3); a = 0x1F00; ss_set_nz16(ss, a);   /* C095E3 lda #$1F00 */

  {                                         /* C095E6 jsr vram_generate_particle_tile */
    const uint16_t sp0 = ss_sp(ss);
    S(0x95E6, 3);
    ss_idle(ss);
    const uint16_t frame = (uint16_t) (ss_pc(ss) - 1);
    ss_push8(ss, (uint8_t) (frame >> 8));
    ss_check_int(ss);
    ss_push8(ss, (uint8_t) frame);
    ss_set_pc(ss, pb, 0x9C16);
    /* Not converted here (recomp/src/vram_stream.c). The frame just pushed is
     * the real one, so leaving the callee running at a frame boundary is safe:
     * its rts lands on $C095E9 either way. */
    if(ss_run_callee(ss, sp0)) return;
    a = ss_a(ss); x = ss_x(ss); y = ss_y(ss);
  }

  S(0x95E9, 2);                             /* C095E9 lda game_mode */
  a = t_read16_dp(ss, dp, game_mode); ss_set_nz16(ss, a);
  SI(0x95EB); a = alu_asl16(ss, a);         /* C095EB asl A */
  SI(0x95EC); x = a; ss_set_nz16(ss, x);    /* C095EC tax */
  S(0x95ED, 4);                             /* C095ED lda data_C0B26C,X */
  a = t_read16(ss, (0x80B26C + x) & 0xffffff); ss_set_nz16(ss, a);
  SI(0x95F1); x = a; ss_set_nz16(ss, x);    /* C095F1 tax */

  S(0x95F2, 3);                             /* C095F2 pea $807F */
  ss_push8(ss, 0x80);
  ss_check_int(ss);
  ss_push8(ss, 0x7F);

  S(0x95F5, 1);                             /* C095F5 plb */
  ss_idle(ss);
  ss_idle(ss);
  ss_check_int(ss);
  {
    const uint8_t b = ss_pull8(ss);
    ss_set_db(ss, b);
    ss_set_nz8(ss, b);
  }

  SIMM16(0x95F6); y = 0x003A; ss_set_nz16(ss, y);   /* C095F6 ldy #$003A */

  for(;;) {
    /* loc_C095F9 */
    S(0x95F9, 4);                           /* C095F9 lda data_C0B26C,X */
    a = t_read16(ss, (0x80B26C + x) & 0xffffff); ss_set_nz16(ss, a);
    taken = ss_n(ss);
    S(0x95FD, 1); t_branch(ss, taken);      /* C095FD bmi loc_C0966A */
    if(taken) break;

    SI(0x95FF); ss_set_c(ss, true);               /* C095FF sec */
    SIMM16(0x9600); a = alu_sbc16(ss, a, 0x0080);   /* C09600 sbc #$0080 */
    S(0x9603, 3); t_index(ss);              /* C09603 sta $0A06,Y */
    t_write16(ss, ss_abs(ss, (0x0A06 + y)), a);

    S(0x9606, 4);                           /* C09606 lda data_C0B26E,X */
    a = t_read16(ss, (0x80B26E + x) & 0xffffff); ss_set_nz16(ss, a);
    SI(0x960A); ss_set_c(ss, true);               /* C0960A sec */
    SIMM16(0x960B); a = alu_sbc16(ss, a, 0x0080);   /* C0960B sbc #$0080 */
    S(0x960E, 3); t_index(ss);              /* C0960E sta $0A86,Y */
    t_write16(ss, ss_abs(ss, (0x0A86 + y)), a);

    SIMM16(0x9611); a = 0x8000; ss_set_nz16(ss, a); /* C09611 lda #$8000 */
    S(0x9614, 3); t_index(ss);              /* C09614 sta $0B06,Y */
    t_write16(ss, ss_abs(ss, (0x0B06 + y)), a);
    S(0x9617, 3); t_index(ss);              /* C09617 sta $0B86,Y */
    t_write16(ss, ss_abs(ss, (0x0B86 + y)), a);

    JSRC(0x961A, 0xA212, random_next);      /* C0961A jsr random_next */
    S(0x961D, 2);                           /* C0961D lda init_magic_AA55 */
    a = t_read16_dp(ss, dp, init_magic_AA55); ss_set_nz16(ss, a);
    SIMM16(0x961F); a = alu_and16(ss, a, 0x1FFF);   /* C0961F and #$1FFF */
    S(0x9622, 3); t_index(ss);              /* C09622 sta $0C06,Y */
    t_write16(ss, ss_abs(ss, (0x0C06 + y)), a);

    JSRC(0x9625, 0xA212, random_next);      /* C09625 jsr random_next */
    S(0x9628, 2);                           /* C09628 lda init_magic_AA55 */
    a = t_read16_dp(ss, dp, init_magic_AA55); ss_set_nz16(ss, a);
    SIMM16(0x962A); a = alu_and16(ss, a, 0x01FF);   /* C0962A and #$01FF */
    SI(0x962D); ss_set_c(ss, false);              /* C0962D clc */
    SIMM16(0x962E); a = alu_adc16(ss, a, 0x0100);   /* C0962E adc #$0100 */
    S(0x9631, 3); t_index(ss);              /* C09631 sta $0C86,Y */
    t_write16(ss, ss_abs(ss, (0x0C86 + y)), a);

    JSRC(0x9634, 0xA212, random_next);      /* C09634 jsr random_next */
    S(0x9637, 2);                           /* C09637 lda init_magic_AA55 */
    a = t_read16_dp(ss, dp, init_magic_AA55); ss_set_nz16(ss, a);
    SIMM16(0x9639); a = alu_and16(ss, a, 0x01FF);   /* C09639 and #$01FF */
    SIMM16(0x963C); a = alu_adc16(ss, a, 0x0100);   /* C0963C adc #$0100 */
    S(0x963F, 3); t_index(ss);              /* C0963F sta $0D06,Y */
    t_write16(ss, ss_abs(ss, (0x0D06 + y)), a);

    SI(0x9642); a = ss_dp(ss); ss_set_nz16(ss, a);/* C09642 tdc */
    S(0x9643, 3); t_index(ss);              /* C09643 sta $0D86,Y */
    t_write16(ss, ss_abs(ss, (0x0D86 + y)), a);
    S(0x9646, 3); t_index(ss);              /* C09646 sta $0E06,Y */
    t_write16(ss, ss_abs(ss, (0x0E06 + y)), a);

    JSRC(0x9649, 0xA212, random_next);      /* C09649 jsr random_next */
    S(0x964C, 2);                           /* C0964C lda init_magic_AA55 */
    a = t_read16_dp(ss, dp, init_magic_AA55); ss_set_nz16(ss, a);
    SIMM16(0x964E); a = alu_and16(ss, a, 0x00FF);   /* C0964E and #$00FF */
    SIMM16(0x9651); a = alu_adc16(ss, a, 0x0080);   /* C09651 adc #$0080 */
    S(0x9654, 3); t_index(ss);              /* C09654 sta $0906,Y */
    t_write16(ss, ss_abs(ss, (0x0906 + y)), a);

    S(0x9657, 2);                           /* C09657 lda init_magic_FFFF */
    a = t_read16_dp(ss, dp, init_magic_FFFF); ss_set_nz16(ss, a);
    SIMM16(0x9659); a = alu_and16(ss, a, 0x007F);   /* C09659 and #$007F */
    SIMM16(0x965C); a = alu_adc16(ss, a, 0x0080);   /* C0965C adc #$0080 */
    S(0x965F, 3); t_index(ss);              /* C0965F sta $0986,Y */
    t_write16(ss, ss_abs(ss, (0x0986 + y)), a);

    SI(0x9662); x = alu_inc16(ss, x);       /* C09662 inx */
    SI(0x9663); x = alu_inc16(ss, x);       /* C09663 inx */
    SI(0x9664); x = alu_inc16(ss, x);       /* C09664 inx */
    SI(0x9665); x = alu_inc16(ss, x);       /* C09665 inx */
    SI(0x9666); y = alu_dec16(ss, y);       /* C09666 dey */
    SI(0x9667); y = alu_dec16(ss, y);       /* C09667 dey */
    taken = !ss_n(ss);
    S(0x9668, 1); t_branch(ss, taken);      /* C09668 bpl loc_C095F9 */
    if(!taken) break;
  }

  /* loc_C0966A */
  SI(0x966A); a = y; ss_set_nz16(ss, a);    /* C0966A tya */
  taken = ss_n(ss);
  S(0x966B, 1); t_branch(ss, taken);        /* C0966B bmi loc_C09677 */
  if(!taken) {
    SIMM16(0x966D); a = 0xFFFF; ss_set_nz16(ss, a); /* C0966D lda #$FFFF */
    do {
      /* loc_C09670 */
      S(0x9670, 3); t_index(ss);            /* C09670 sta $0906,Y */
      t_write16(ss, ss_abs(ss, (0x0906 + y)), a);
      SI(0x9673); y = alu_dec16(ss, y);     /* C09673 dey */
      SI(0x9674); y = alu_dec16(ss, y);     /* C09674 dey */
      taken = !ss_n(ss);
      S(0x9675, 1); t_branch(ss, taken);    /* C09675 bpl loc_C09670 */
    } while(taken);
  }

  /* loc_C09677 */
  S(0x9677, 1);                             /* C09677 plb */
  ss_idle(ss);
  ss_idle(ss);
  ss_check_int(ss);
  {
    const uint8_t b = ss_pull8(ss);
    ss_set_db(ss, b);
    ss_set_nz8(ss, b);
  }

  S(0x9678, 1);                             /* C09678 rts */
  ss_rts(ss);
}

/* ---------------------------------------------------------------------------
 * particle_spawn_random: $C0:9781
 *
 * Called from title_screen_init. The same tile build as
 * particle_spawn_from_table, then all forty slots seeded entirely from
 * random_next, with no spawn table, and fixed velocity constants ($FC00, $0800)
 * and a per-slot delay in $7F0986.
 * Exit: X = $FFFE, N set.
 * ------------------------------------------------------------------------- */
void particle_spawn_random(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint16_t dp = ss_dp(ss);
  bool taken;

  SIMM16(0x9781); a = 0x1F00; ss_set_nz16(ss, a);   /* C09781 lda #$1F00 */

  {                                         /* C09784 jsr vram_generate_particle_tile */
    const uint16_t sp0 = ss_sp(ss);
    S(0x9784, 3);
    ss_idle(ss);
    const uint16_t frame = (uint16_t) (ss_pc(ss) - 1);
    ss_push8(ss, (uint8_t) (frame >> 8));
    ss_check_int(ss);
    ss_push8(ss, (uint8_t) frame);
    ss_set_pc(ss, pb, 0x9C16);
    if(ss_run_callee(ss, sp0)) return;      /* see particle_spawn_from_table */
    a = ss_a(ss); x = ss_x(ss); y = ss_y(ss);
  }

  SIMM16(0x9787); x = 0x003A; ss_set_nz16(ss, x);   /* C09787 ldx #$003A */

  do {
    /* loc_C0978A */
    JSRC(0x978A, 0xA212, random_next);      /* C0978A jsr random_next */

    S(0x978D, 2);                           /* C0978D lda init_magic_AA55 */
    a = t_read16_dp(ss, dp, init_magic_AA55); ss_set_nz16(ss, a);
    SIMM16(0x978F); a = alu_and16(ss, a, 0x07FF);   /* C0978F and #$07FF */
    S(0x9792, 4);                           /* C09792 sta $7F0A06,X */
    t_write16(ss, (0x7F0A06 + x) & 0xffffff, a);

    S(0x9796, 2);                           /* C09796 lda init_magic_FFFF */
    a = t_read16_dp(ss, dp, init_magic_FFFF); ss_set_nz16(ss, a);
    SIMM16(0x9798); a = alu_and16(ss, a, 0x003F);   /* C09798 and #$003F */
    SIMM16(0x979B); a = alu_adc16(ss, a, 0x0060);   /* C0979B adc #$0060 */
    S(0x979E, 4);                           /* C0979E sta $7F0A86,X */
    t_write16(ss, (0x7F0A86 + x) & 0xffffff, a);

    SIMM16(0x97A2); a = 0x8000; ss_set_nz16(ss, a); /* C097A2 lda #$8000 */
    S(0x97A5, 4);                           /* C097A5 sta $7F0B06,X */
    t_write16(ss, (0x7F0B06 + x) & 0xffffff, a);
    S(0x97A9, 4);                           /* C097A9 sta $7F0B86,X */
    t_write16(ss, (0x7F0B86 + x) & 0xffffff, a);

    SIMM16(0x97AD); a = 0xFC00; ss_set_nz16(ss, a); /* C097AD lda #$FC00 */
    S(0x97B0, 4);                           /* C097B0 sta $7F0C86,X */
    t_write16(ss, (0x7F0C86 + x) & 0xffffff, a);

    SIMM16(0x97B4); a = 0x0800; ss_set_nz16(ss, a); /* C097B4 lda #$0800 */
    S(0x97B7, 4);                           /* C097B7 sta $7F0D06,X */
    t_write16(ss, (0x7F0D06 + x) & 0xffffff, a);

    SI(0x97BB); a = ss_dp(ss); ss_set_nz16(ss, a);/* C097BB tdc */
    S(0x97BC, 4);                           /* C097BC sta $7F0D86,X */
    t_write16(ss, (0x7F0D86 + x) & 0xffffff, a);
    S(0x97C0, 4);                           /* C097C0 sta $7F0E06,X */
    t_write16(ss, (0x7F0E06 + x) & 0xffffff, a);
    S(0x97C4, 4);                           /* C097C4 sta particle_table,X */
    t_write16(ss, (particle_table + x) & 0xffffff, a);

    SIMM16(0x97C8); a = 0x0700; ss_set_nz16(ss, a); /* C097C8 lda #$0700 */
    S(0x97CB, 4);                           /* C097CB sta $7F0C06,X */
    t_write16(ss, (0x7F0C06 + x) & 0xffffff, a);

    S(0x97CF, 2);                           /* C097CF lda init_magic_FFFF */
    a = t_read16_dp(ss, dp, init_magic_FFFF); ss_set_nz16(ss, a);
    SIMM16(0x97D1); a = alu_and16(ss, a, 0x00FF);   /* C097D1 and #$00FF */
    S(0x97D4, 4);                           /* C097D4 sta $7F0986,X */
    t_write16(ss, (0x7F0986 + x) & 0xffffff, a);

    SI(0x97D8); x = alu_dec16(ss, x);       /* C097D8 dex */
    SI(0x97D9); x = alu_dec16(ss, x);       /* C097D9 dex */
    taken = !ss_n(ss);
    S(0x97DA, 1); t_branch(ss, taken);      /* C097DA bpl loc_C0978A */
  } while(taken);

  S(0x97DC, 1);                             /* C097DC rts */
  ss_rts(ss);
}

/* ---------------------------------------------------------------------------
 * loc_C09679: the mode-1 spawn / integrate / cull / emit loop
 *
 * mode1_particle_dispatch tail-jumps here, so the routine's rts is what returns
 * to the `jsr (jtbl_C08282,X)` that reached the dispatcher. It walks the slot
 * table from the highest slot that still has room in the OAM buffer:
 *
 *   $7F0906 < 0 (bit 15 set)   the slot is skipped
 *   $7F0906 > 0               a live particle: age it, add the two velocity
 *                             table entries out of data_C46588 to the position,
 *                             integrate the two sub-pixel accumulators
 *   $7F0906 = 0               a free slot: when its respawn timer $7F0986 has
 *                             run out, seed both from random_next, otherwise
 *                             count the timer down and drift $7F0C06
 *
 * and then emits one 4-byte OAM entry per particle whose screen position is on
 * screen, with the tile index taken from a 32-step triangle of $7F0C07.
 *
 * The routine was the ROM's own code until --no-cpu: the dispatcher above
 * pointed the pc at it and the 65816 ran it. Written from out/dream.asm
 * $9679-$9780, instruction for instruction.
 * ------------------------------------------------------------------------- */
void particle_update_mode1(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint16_t dp = ss_dp(ss);
  uint16_t v;
  bool t;

  S(0x9679, 2);                             /* C09679 lda oam_write_ptr */
  a = t_read16_dp(ss, dp, oam_write_ptr); ss_set_nz16(ss, a);
  SI(0x967B); y = a; ss_set_nz16(ss, y);    /* C0967B tay */
  SI(0x967C); ss_set_c(ss, true);           /* C0967C sec */
  SIMM16(0x967D); a = alu_sbc16(ss, a, 0x0400);  /* C0967D sbc #$0400 */
  t = ss_n(ss);
  S(0x9680, 1); t_branch(ss, t);            /* C09680 bmi loc_C09683 */
  if(!t) {
    ss_set_a(ss, a); ss_set_x(ss, x); ss_set_y(ss, y);
    S(0x9682, 1); ss_rts(ss);               /* C09682 rts */
    return;
  }

  /* loc_C09683: the buffer's free space, in slots, capped at the last one */
  SIMM16(0x9683); a = alu_eor16(ss, a, 0xFFFF);  /* C09683 eor #$FFFF */
  SI(0x9686); a = alu_inc16(ss, a);            /* C09686 inc A */
  SI(0x9687); a = alu_lsr16(ss, a);            /* C09687 lsr A */
  SIMM16(0x9688); alu_cmp16(ss, a, 0x003C);    /* C09688 cmp #$003C */
  t = !ss_c(ss);
  S(0x968B, 1); t_branch(ss, t);               /* C0968B bcc loc_C09690 */
  if(!t) { SIMM16(0x968D); a = 0x003A; ss_set_nz16(ss, a); }  /* lda #$003A */
  /* loc_C09690 */
  SI(0x9690); x = a; ss_set_nz16(ss, x);       /* C09690 tax */

loc_9691: ;
  S(0x9691, 4);                             /* C09691 lda $7F0906,X */
  a = t_read16(ss, (0x7F0906u + x) & 0xffffff); ss_set_nz16(ss, a);
  t = ss_n(ss);
  S(0x9695, 1); t_branch(ss, t);            /* C09695 bmi loc_C096C8 */
  if(t) goto loc_96C8;
  t = !ss_z(ss);
  S(0x9697, 1); t_branch(ss, t);            /* C09697 bne loc_C096CF */
  if(t) goto loc_96CF;

  S(0x9699, 4);                             /* C09699 lda $7F0986,X */
  a = t_read16(ss, (0x7F0986u + x) & 0xffffff); ss_set_nz16(ss, a);
  t = !ss_z(ss);
  S(0x969D, 1); t_branch(ss, t);            /* C0969D bne loc_C096B6 */
  if(!t) {
    JSRC(0x969F, 0xA212, random_next);      /* C0969F jsr random_next */
    S(0x96A2, 2);                           /* C096A2 lda init_magic_AA55 */
    a = t_read16_dp(ss, dp, init_magic_AA55); ss_set_nz16(ss, a);
    SIMM16(0x96A4); a = alu_and16(ss, a, 0x00FF);  /* C096A4 and #$00FF */
    SIMM16(0x96A7); a = alu_adc16(ss, a, 0x0080);  /* C096A7 adc #$0080 */
    S(0x96AA, 4); t_write16(ss, (0x7F0906u + x) & 0xffffff, a);
    S(0x96AE, 2);                           /* C096AE lda init_magic_FFFF */
    a = t_read16_dp(ss, dp, init_magic_FFFF); ss_set_nz16(ss, a);
    SIMM16(0x96B0); a = alu_and16(ss, a, 0x007F);  /* C096B0 and #$007F */
    SIMM16(0x96B3); a = alu_adc16(ss, a, 0x0080);  /* C096B3 adc #$0080 */
  }
  /* loc_C096B6 */
  SI(0x96B6); a = alu_dec16(ss, a);         /* C096B6 dec A */
  S(0x96B7, 4); t_write16(ss, (0x7F0986u + x) & 0xffffff, a);
  S(0x96BB, 4);                             /* C096BB lda $7F0C06,X */
  a = t_read16(ss, (0x7F0C06u + x) & 0xffffff); ss_set_nz16(ss, a);
  SIMM16(0x96BF); a = alu_adc16(ss, a, 0x0080);  /* C096BF adc #$0080 */
  S(0x96C2, 4); t_write16(ss, (0x7F0C06u + x) & 0xffffff, a);
  S(0x96C6, 1); t_branch(ss, true);         /* C096C6 bra loc_C09729 */
  goto loc_9729;

loc_96C8: ;
  SI(0x96C8); x = alu_dec16(ss, x);         /* C096C8 dex */
  SI(0x96C9); x = alu_dec16(ss, x);         /* C096C9 dex */
  t = !ss_n(ss);
  S(0x96CA, 1); t_branch(ss, t);            /* C096CA bpl loc_C09691 */
  if(t) goto loc_9691;
  SJMP(0x96CC);                             /* C096CC jmp loc_C0977E */
  goto loc_977E;

loc_96CF: ;
  SI(0x96CF); a = alu_dec16(ss, a);         /* C096CF dec A */
  S(0x96D0, 4); t_write16(ss, (0x7F0906u + x) & 0xffffff, a);
  S(0x96D4, 4);                             /* C096D4 lda $7F0C06,X */
  a = t_read16(ss, (0x7F0C06u + x) & 0xffffff); ss_set_nz16(ss, a);
  SIMM16(0x96D8); a = alu_adc16(ss, a, 0x0100);  /* C096D8 adc #$0100 */
  S(0x96DB, 4); t_write16(ss, (0x7F0C06u + x) & 0xffffff, a);
  S(0x96DF, 4);                             /* C096DF lda $7F0D87,X */
  a = t_read16(ss, (0x7F0D87u + x) & 0xffffff); ss_set_nz16(ss, a);
  SIMM16(0x96E3); a = alu_and16(ss, a, 0x00FF);  /* C096E3 and #$00FF */
  SI(0x96E6); a = alu_asl16(ss, a);         /* C096E6 asl A */
  S(0x96E7, 2); t_write16_dp(ss, dp, 0x0006, x); /* C096E7 stx $06 */
  SI(0x96E9); x = a; ss_set_nz16(ss, x);    /* C096E9 tax */
  S(0x96EA, 4);                             /* C096EA lda data_C46588,X */
  a = t_read16(ss, (0xC46588u + x) & 0xffffff); ss_set_nz16(ss, a);
  S(0x96EE, 2);                             /* C096EE ldx $06 */
  x = t_read16_dp(ss, dp, 0x0006); ss_set_nz16(ss, x);
  S(0x96F0, 4);                             /* C096F0 adc $7F0B06,X */
  v = t_read16(ss, (0x7F0B06u + x) & 0xffffff); a = alu_adc16(ss, a, v);
  S(0x96F4, 4); t_write16(ss, (0x7F0B06u + x) & 0xffffff, a);
  S(0x96F8, 4);                             /* C096F8 lda $7F0E07,X */
  a = t_read16(ss, (0x7F0E07u + x) & 0xffffff); ss_set_nz16(ss, a);
  SIMM16(0x96FC); a = alu_and16(ss, a, 0x00FF);  /* C096FC and #$00FF */
  SI(0x96FF); a = alu_asl16(ss, a);         /* C096FF asl A */
  SI(0x9700); x = a; ss_set_nz16(ss, x);    /* C09700 tax */
  S(0x9701, 4);                             /* C09701 lda data_C46588,X */
  a = t_read16(ss, (0xC46588u + x) & 0xffffff); ss_set_nz16(ss, a);
  S(0x9705, 2);                             /* C09705 ldx $06 */
  x = t_read16_dp(ss, dp, 0x0006); ss_set_nz16(ss, x);
  S(0x9707, 4);                             /* C09707 adc $7F0B86,X */
  v = t_read16(ss, (0x7F0B86u + x) & 0xffffff); a = alu_adc16(ss, a, v);
  S(0x970B, 4); t_write16(ss, (0x7F0B86u + x) & 0xffffff, a);
  S(0x970F, 4);                             /* C0970F lda $7F0D86,X */
  a = t_read16(ss, (0x7F0D86u + x) & 0xffffff); ss_set_nz16(ss, a);
  SI(0x9713); ss_set_c(ss, false);          /* C09713 clc */
  S(0x9714, 4);                             /* C09714 adc $7F0C86,X */
  v = t_read16(ss, (0x7F0C86u + x) & 0xffffff); a = alu_adc16(ss, a, v);
  S(0x9718, 4); t_write16(ss, (0x7F0D86u + x) & 0xffffff, a);
  S(0x971C, 4);                             /* C0971C lda $7F0E06,X */
  a = t_read16(ss, (0x7F0E06u + x) & 0xffffff); ss_set_nz16(ss, a);
  SI(0x9720); ss_set_c(ss, false);          /* C09720 clc */
  S(0x9721, 4);                             /* C09721 adc $7F0D06,X */
  v = t_read16(ss, (0x7F0D06u + x) & 0xffffff); a = alu_adc16(ss, a, v);
  S(0x9725, 4); t_write16(ss, (0x7F0E06u + x) & 0xffffff, a);

loc_9729: ;
  S(0x9729, 4);                             /* C09729 lda $7F0B07,X */
  a = t_read16(ss, (0x7F0B07u + x) & 0xffffff); ss_set_nz16(ss, a);
  SIMM16(0x972D); a = alu_and16(ss, a, 0x00FF);  /* C0972D and #$00FF */
  SI(0x9730); ss_set_c(ss, false);          /* C09730 clc */
  S(0x9731, 4);                             /* C09731 adc $7F0A06,X */
  v = t_read16(ss, (0x7F0A06u + x) & 0xffffff); a = alu_adc16(ss, a, v);
  SI(0x9735); ss_set_c(ss, true);           /* C09735 sec */
  S(0x9736, 2);                             /* C09736 sbc camera_x */
  v = t_read16_dp(ss, dp, camera_x); a = alu_sbc16(ss, a, v);
  t = ss_n(ss);
  S(0x9738, 1); t_branch(ss, t);            /* C09738 bmi loc_C096C8 */
  if(t) goto loc_96C8;
  SIMM16(0x973A); alu_cmp16(ss, a, 0x0100);   /* C0973A cmp #$0100 */
  t = ss_c(ss);
  S(0x973D, 1); t_branch(ss, t);            /* C0973D bcs loc_C096C8 */
  if(t) goto loc_96C8;
  S(0x973F, 3); t_index(ss);                /* C0973F sta nmi_handler_ptr,Y */
  t_write16(ss, ss_abs(ss, (nmi_handler_ptr + y)), a);
  S(0x9742, 4);                             /* C09742 lda $7F0B87,X */
  a = t_read16(ss, (0x7F0B87u + x) & 0xffffff); ss_set_nz16(ss, a);
  SIMM16(0x9746); a = alu_and16(ss, a, 0x00FF);  /* C09746 and #$00FF */
  S(0x9749, 4);                             /* C09749 adc $7F0A86,X */
  v = t_read16(ss, (0x7F0A86u + x) & 0xffffff); a = alu_adc16(ss, a, v);
  SI(0x974D); ss_set_c(ss, true);           /* C0974D sec */
  S(0x974E, 2);                             /* C0974E sbc camera_y */
  v = t_read16_dp(ss, dp, camera_y); a = alu_sbc16(ss, a, v);
  t = ss_n(ss);
  S(0x9750, 1); t_branch(ss, t);            /* C09750 bmi loc_C09777 */
  if(t) goto loc_9777;
  SIMM16(0x9752); alu_cmp16(ss, a, 0x00E0);   /* C09752 cmp #$00E0 */
  t = ss_c(ss);
  S(0x9755, 1); t_branch(ss, t);            /* C09755 bcs loc_C09777 */
  if(t) goto loc_9777;
  S(0x9757, 3); t_index(ss);                /* C09757 sta $0001,Y */
  t_write16(ss, ss_abs(ss, (0x0001 + y)), a);
  S(0x975A, 4);                             /* C0975A lda $7F0C07,X */
  a = t_read16(ss, (0x7F0C07u + x) & 0xffffff); ss_set_nz16(ss, a);
  SIMM16(0x975E); a = alu_and16(ss, a, 0x001F);  /* C0975E and #$001F */
  SIMM16(0x9761); alu_cmp16(ss, a, 0x0010);   /* C09761 cmp #$0010 */
  t = !ss_c(ss);
  S(0x9764, 1); t_branch(ss, t);            /* C09764 bcc loc_C0976C */
  if(!t) {
    SIMM16(0x9766); a = alu_eor16(ss, a, 0x000F);  /* C09766 eor #$000F */
    SIMM16(0x9769); a = alu_and16(ss, a, 0x000F);  /* C09769 and #$000F */
  }
  /* loc_C0976C */
  SI(0x976C); ss_set_c(ss, false);          /* C0976C clc */
  SIMM16(0x976D); a = alu_adc16(ss, a, 0x2FF0);  /* C0976D adc #$2FF0 */
  S(0x9770, 3); t_index(ss);                /* C09770 sta dma_pending_mask,Y */
  t_write16(ss, ss_abs(ss, (dma_pending_mask + y)), a);
  SI(0x9773); y = alu_inc16(ss, y);         /* C09773 iny */
  SI(0x9774); y = alu_inc16(ss, y);         /* C09774 iny */
  SI(0x9775); y = alu_inc16(ss, y);         /* C09775 iny */
  SI(0x9776); y = alu_inc16(ss, y);         /* C09776 iny */

loc_9777: ;
  SI(0x9777); x = alu_dec16(ss, x);         /* C09777 dex */
  SI(0x9778); x = alu_dec16(ss, x);         /* C09778 dex */
  t = ss_n(ss);
  S(0x9779, 1); t_branch(ss, t);            /* C09779 bmi loc_C0977E */
  if(!t) { SJMP(0x977B); goto loc_9691; }   /* C0977B jmp loc_C09691 */

loc_977E: ;
  S(0x977E, 2); t_write16_dp(ss, dp, oam_write_ptr, y);  /* C0977E sty oam_write_ptr */
  ss_set_a(ss, a); ss_set_x(ss, x); ss_set_y(ss, y);
  S(0x9780, 1); ss_rts(ss);                 /* C09780 rts */
}

/* ---------------------------------------------------------------------------
 * loc_C097DD: the mode-2 variant of the same loop
 *
 * mode2_particle_dispatch is a bare tail jump to here. Same shape as
 * loc_C09679 with three differences: there is no "skip the slot" test on bit 15
 * (every non-zero slot is live), a free slot is respawned with a fixed $0400
 * velocity whose sign comes from bit 15 of the RNG word rather than a random
 * magnitude, and the tile index is a plain 16-step wrap of $7F0C07 added to
 * $29F0 instead of the triangle. Written from out/dream.asm $97DD-$98D9.
 * ------------------------------------------------------------------------- */
void particle_update_mode2(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint16_t dp = ss_dp(ss);
  uint16_t v;
  bool t;

  S(0x97DD, 2);                             /* C097DD lda oam_write_ptr */
  a = t_read16_dp(ss, dp, oam_write_ptr); ss_set_nz16(ss, a);
  SI(0x97DF); y = a; ss_set_nz16(ss, y);    /* C097DF tay */
  SI(0x97E0); ss_set_c(ss, true);           /* C097E0 sec */
  SIMM16(0x97E1); a = alu_sbc16(ss, a, 0x0400);  /* C097E1 sbc #$0400 */
  t = ss_n(ss);
  S(0x97E4, 1); t_branch(ss, t);            /* C097E4 bmi loc_C097EE */
  if(!t) {
    ss_set_a(ss, a); ss_set_x(ss, x); ss_set_y(ss, y);
    S(0x97E6, 1); ss_rts(ss);               /* C097E6 rts */
    return;
  }

  /* loc_C097EE */
  SIMM16(0x97EE); a = alu_eor16(ss, a, 0xFFFF);  /* C097EE eor #$FFFF */
  SI(0x97F1); a = alu_inc16(ss, a);            /* C097F1 inc A */
  SI(0x97F2); a = alu_lsr16(ss, a);            /* C097F2 lsr A */
  SIMM16(0x97F3); alu_cmp16(ss, a, 0x003C);    /* C097F3 cmp #$003C */
  t = !ss_c(ss);
  S(0x97F6, 1); t_branch(ss, t);               /* C097F6 bcc loc_C097FB */
  if(!t) { SIMM16(0x97F8); a = 0x003A; ss_set_nz16(ss, a); }  /* lda #$003A */
  /* loc_C097FB */
  SI(0x97FB); x = a; ss_set_nz16(ss, x);       /* C097FB tax */

loc_97FC: ;
  S(0x97FC, 4);                             /* C097FC lda $7F0906,X */
  a = t_read16(ss, (0x7F0906u + x) & 0xffffff); ss_set_nz16(ss, a);
  t = !ss_z(ss);
  S(0x9800, 1); t_branch(ss, t);            /* C09800 bne loc_C0983E */
  if(t) goto loc_983E;
  S(0x9802, 4);                             /* C09802 lda $7F0986,X */
  a = t_read16(ss, (0x7F0986u + x) & 0xffffff); ss_set_nz16(ss, a);
  t = ss_z(ss);
  S(0x9806, 1); t_branch(ss, t);            /* C09806 beq loc_C09810 */
  if(!t) {
    SI(0x9808); a = alu_dec16(ss, a);       /* C09808 dec A */
    S(0x9809, 4); t_write16(ss, (0x7F0986u + x) & 0xffffff, a);
    SJMP(0x980D);                           /* C0980D jmp loc_C0988D */
    goto loc_988D;
  }

  /* loc_C09810 */
  JSRC(0x9810, 0xA212, random_next);        /* C09810 jsr random_next */
  S(0x9813, 2);                             /* C09813 lda init_magic_AA55 */
  a = t_read16_dp(ss, dp, init_magic_AA55); ss_set_nz16(ss, a);
  SIMM16(0x9815); a = alu_and16(ss, a, 0x00FF);  /* C09815 and #$00FF */
  S(0x9818, 4); t_write16(ss, (0x7F0986u + x) & 0xffffff, a);
  SIMM16(0x981C); a = 0x0400; ss_set_nz16(ss, a);  /* C0981C lda #$0400 */
  S(0x981F, 2);                             /* C0981F bit init_magic_AA55 */
  v = t_read16_dp(ss, dp, init_magic_AA55); alu_bit16(ss, a, v);
  t = !ss_n(ss);
  S(0x9821, 1); t_branch(ss, t);            /* C09821 bpl loc_C09827 */
  if(!t) {
    SIMM16(0x9823); a = alu_eor16(ss, a, 0xFFFF);  /* C09823 eor #$FFFF */
    SI(0x9826); a = alu_inc16(ss, a);            /* C09826 inc A */
  }
  /* loc_C09827 */
  S(0x9827, 4); t_write16(ss, (0x7F0C86u + x) & 0xffffff, a);
  SIMM16(0x982B); a = 0x0800; ss_set_nz16(ss, a);  /* C0982B lda #$0800 */
  S(0x982E, 4); t_write16(ss, (0x7F0D06u + x) & 0xffffff, a);
  SI(0x9832); a = ss_dp(ss); ss_set_nz16(ss, a); /* C09832 tdc */
  S(0x9833, 4); t_write16(ss, (0x7F0D86u + x) & 0xffffff, a);
  S(0x9837, 4); t_write16(ss, (0x7F0E06u + x) & 0xffffff, a);
  SIMM16(0x983B); a = 0x0021; ss_set_nz16(ss, a);  /* C0983B lda #$0021 */

loc_983E: ;
  SI(0x983E); a = alu_dec16(ss, a);         /* C0983E dec A */
  S(0x983F, 4); t_write16(ss, (0x7F0906u + x) & 0xffffff, a);
  S(0x9843, 4);                             /* C09843 lda $7F0D87,X */
  a = t_read16(ss, (0x7F0D87u + x) & 0xffffff); ss_set_nz16(ss, a);
  SIMM16(0x9847); a = alu_and16(ss, a, 0x00FF);  /* C09847 and #$00FF */
  SI(0x984A); a = alu_asl16(ss, a);         /* C0984A asl A */
  S(0x984B, 2); t_write16_dp(ss, dp, 0x0006, x); /* C0984B stx $06 */
  SI(0x984D); x = a; ss_set_nz16(ss, x);    /* C0984D tax */
  S(0x984E, 4);                             /* C0984E lda data_C46588,X */
  a = t_read16(ss, (0xC46588u + x) & 0xffffff); ss_set_nz16(ss, a);
  S(0x9852, 2);                             /* C09852 ldx $06 */
  x = t_read16_dp(ss, dp, 0x0006); ss_set_nz16(ss, x);
  S(0x9854, 4);                             /* C09854 adc $7F0B06,X */
  v = t_read16(ss, (0x7F0B06u + x) & 0xffffff); a = alu_adc16(ss, a, v);
  S(0x9858, 4); t_write16(ss, (0x7F0B06u + x) & 0xffffff, a);
  S(0x985C, 4);                             /* C0985C lda $7F0E07,X */
  a = t_read16(ss, (0x7F0E07u + x) & 0xffffff); ss_set_nz16(ss, a);
  SIMM16(0x9860); a = alu_and16(ss, a, 0x00FF);  /* C09860 and #$00FF */
  SI(0x9863); a = alu_asl16(ss, a);         /* C09863 asl A */
  SI(0x9864); x = a; ss_set_nz16(ss, x);    /* C09864 tax */
  S(0x9865, 4);                             /* C09865 lda data_C46588,X */
  a = t_read16(ss, (0xC46588u + x) & 0xffffff); ss_set_nz16(ss, a);
  S(0x9869, 2);                             /* C09869 ldx $06 */
  x = t_read16_dp(ss, dp, 0x0006); ss_set_nz16(ss, x);
  S(0x986B, 4);                             /* C0986B adc $7F0B86,X */
  v = t_read16(ss, (0x7F0B86u + x) & 0xffffff); a = alu_adc16(ss, a, v);
  S(0x986F, 4); t_write16(ss, (0x7F0B86u + x) & 0xffffff, a);
  S(0x9873, 4);                             /* C09873 lda $7F0D86,X */
  a = t_read16(ss, (0x7F0D86u + x) & 0xffffff); ss_set_nz16(ss, a);
  SI(0x9877); ss_set_c(ss, false);          /* C09877 clc */
  S(0x9878, 4);                             /* C09878 adc $7F0C86,X */
  v = t_read16(ss, (0x7F0C86u + x) & 0xffffff); a = alu_adc16(ss, a, v);
  S(0x987C, 4); t_write16(ss, (0x7F0D86u + x) & 0xffffff, a);
  S(0x9880, 4);                             /* C09880 lda $7F0E06,X */
  a = t_read16(ss, (0x7F0E06u + x) & 0xffffff); ss_set_nz16(ss, a);
  SI(0x9884); ss_set_c(ss, false);          /* C09884 clc */
  S(0x9885, 4);                             /* C09885 adc $7F0D06,X */
  v = t_read16(ss, (0x7F0D06u + x) & 0xffffff); a = alu_adc16(ss, a, v);
  S(0x9889, 4); t_write16(ss, (0x7F0E06u + x) & 0xffffff, a);

loc_988D: ;
  S(0x988D, 4);                             /* C0988D lda $7F0B07,X */
  a = t_read16(ss, (0x7F0B07u + x) & 0xffffff); ss_set_nz16(ss, a);
  SIMM16(0x9891); a = alu_and16(ss, a, 0x00FF);  /* C09891 and #$00FF */
  SI(0x9894); ss_set_c(ss, false);          /* C09894 clc */
  S(0x9895, 4);                             /* C09895 adc $7F0A06,X */
  v = t_read16(ss, (0x7F0A06u + x) & 0xffffff); a = alu_adc16(ss, a, v);
  SI(0x9899); ss_set_c(ss, true);           /* C09899 sec */
  S(0x989A, 2);                             /* C0989A sbc camera_x */
  v = t_read16_dp(ss, dp, camera_x); a = alu_sbc16(ss, a, v);
  t = ss_n(ss);
  S(0x989C, 1); t_branch(ss, t);            /* C0989C bmi loc_C098D0 */
  if(t) goto loc_98D0;
  SIMM16(0x989E); alu_cmp16(ss, a, 0x0100);   /* C0989E cmp #$0100 */
  t = ss_c(ss);
  S(0x98A1, 1); t_branch(ss, t);            /* C098A1 bcs loc_C098D0 */
  if(t) goto loc_98D0;
  S(0x98A3, 3); t_index(ss);                /* C098A3 sta nmi_handler_ptr,Y */
  t_write16(ss, ss_abs(ss, (nmi_handler_ptr + y)), a);
  S(0x98A6, 4);                             /* C098A6 lda $7F0B87,X */
  a = t_read16(ss, (0x7F0B87u + x) & 0xffffff); ss_set_nz16(ss, a);
  SIMM16(0x98AA); a = alu_and16(ss, a, 0x00FF);  /* C098AA and #$00FF */
  S(0x98AD, 4);                             /* C098AD adc $7F0A86,X */
  v = t_read16(ss, (0x7F0A86u + x) & 0xffffff); a = alu_adc16(ss, a, v);
  SI(0x98B1); ss_set_c(ss, true);           /* C098B1 sec */
  S(0x98B2, 2);                             /* C098B2 sbc camera_y */
  v = t_read16_dp(ss, dp, camera_y); a = alu_sbc16(ss, a, v);
  t = ss_n(ss);
  S(0x98B4, 1); t_branch(ss, t);            /* C098B4 bmi loc_C098D0 */
  if(t) goto loc_98D0;
  SIMM16(0x98B6); alu_cmp16(ss, a, 0x00E0);   /* C098B6 cmp #$00E0 */
  t = ss_c(ss);
  S(0x98B9, 1); t_branch(ss, t);            /* C098B9 bcs loc_C098D0 */
  if(t) goto loc_98D0;
  S(0x98BB, 3); t_index(ss);                /* C098BB sta $0001,Y */
  t_write16(ss, ss_abs(ss, (0x0001 + y)), a);
  S(0x98BE, 4);                             /* C098BE lda $7F0C07,X */
  a = t_read16(ss, (0x7F0C07u + x) & 0xffffff); ss_set_nz16(ss, a);
  SIMM16(0x98C2); a = alu_and16(ss, a, 0x000F);  /* C098C2 and #$000F */
  SI(0x98C5); ss_set_c(ss, false);          /* C098C5 clc */
  SIMM16(0x98C6); a = alu_adc16(ss, a, 0x29F0);  /* C098C6 adc #$29F0 */
  S(0x98C9, 3); t_index(ss);                /* C098C9 sta dma_pending_mask,Y */
  t_write16(ss, ss_abs(ss, (dma_pending_mask + y)), a);
  SI(0x98CC); y = alu_inc16(ss, y);         /* C098CC iny */
  SI(0x98CD); y = alu_inc16(ss, y);         /* C098CD iny */
  SI(0x98CE); y = alu_inc16(ss, y);         /* C098CE iny */
  SI(0x98CF); y = alu_inc16(ss, y);         /* C098CF iny */

loc_98D0: ;
  SI(0x98D0); x = alu_dec16(ss, x);         /* C098D0 dex */
  SI(0x98D1); x = alu_dec16(ss, x);         /* C098D1 dex */
  t = ss_n(ss);
  S(0x98D2, 1); t_branch(ss, t);            /* C098D2 bmi loc_C098D7 */
  if(!t) { SJMP(0x98D4); goto loc_97FC; }   /* C098D4 jmp loc_C097FC */

  /* loc_C098D7 */
  S(0x98D7, 2); t_write16_dp(ss, dp, oam_write_ptr, y);  /* C098D7 sty oam_write_ptr */
  ss_set_a(ss, a); ss_set_x(ss, x); ss_set_y(ss, y);
  S(0x98D9, 1); ss_rts(ss);                 /* C098D9 rts */
}

static const RecompEntry kParticlesFx[] = {
  { 0xc0922a, "mode1_particle_dispatch", mode1_particle_dispatch },
  { 0xc09230, "mode2_particle_dispatch", mode2_particle_dispatch },
  { 0xc09253, "particle_spawn_mode0_weather", particle_spawn_mode0_weather },
  { 0xc092f3, "mode1_reset_particles_and_oam", mode1_reset_particles_and_oam },
  { 0xc09331, "particle_update_and_draw_mode0", particle_update_and_draw_mode0 },
  { 0xc094e4, "sparkle_array_init", sparkle_array_init },
  { 0xc09521, "sparkle_update_and_draw", sparkle_update_and_draw },
  { 0xc095e3, "particle_spawn_from_table", particle_spawn_from_table },
  { 0xc09679, "loc_C09679", particle_update_mode1 },
  { 0xc09781, "particle_spawn_random", particle_spawn_random },
  { 0xc097dd, "loc_C097DD", particle_update_mode2 },
};
RECOMP_REGISTER(kParticlesFx)
