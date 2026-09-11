/* SPC700 upload / command interface, bank $C1.
 *
 * Everything the 65816 side of the sound system does lives in this bank: the
 * IPL handshake that gets the SPC700 to accept a loader, the block streamer the
 * loader then speaks, the sample-directory builder, and the per-song uploads
 * that run on every sound command. `docs/NOTES.md` ("SPC700 sound driver") and
 * `spc/spc_map.txt` describe the other end of the wire; the argument
 * conventions come from p4plus2's DKC2 disassembly by way of
 * `docs/dkc_crossref.md` section 3.2, which is also where the names are from.
 *
 * Of every routine in the port these are the ones whose timing is most visibly
 * observed. $2140 is a running handshake counter: the 65816 waits until the
 * SPC echoes the value it last wrote, writes the next word to $2141/$2142, and
 * bumps $2140. Both sides are counting against their own clocks, so how many
 * times a busy-wait spins is a function of how long the 65816 takes to get back
 * around the loop. Every port access therefore goes through the timed register
 * accessors (ss_reg_*), never the untimed ones, and every loop polls the
 * emulated port exactly as often as the ROM does, so the SPC700 sees the same
 * traffic at the same master cycles.
 *
 * The uploads are also the longest routines in the game (the driver alone is
 * $699 words, each one a busy-wait), so a hook that ran one atomically would
 * move every NMI inside it. The step macros in dream_time.h offer the routine
 * back to the ROM before every single instruction, which keeps NMI timing
 * during an upload identical (--lockstep reports zero master-cycle
 * drift).
 *
 * Direct page is 0 throughout (reset's tcd) and the data bank is $00 at reset
 * and $80 on the sound-effect path, so the absolute forms below reach WRAM and
 * the $21xx registers either way; ss_abs() keeps the ROM's own addressing.
 *
 * The bodies are static: the registry table below is their only caller, and
 * the vendored SPC700 core already exports a symbol called spc_init.
 *
 * unused_spc_set_e7_and_play is converted (below) even though nothing reaches it; it is
 * credited by the unit gate rather than by a script (config/recomp_units.txt,
 * `dream_harness --unit`). The unlabelled dead twin unused_spc_execute
 * ($C1804C) is not: the tracer never gave it a label, so it is a byte run in
 * out/dream.asm rather than a routine, and there is nothing for the registry
 * to name (see docs/dkc_crossref.md 3.2, where p4plus2's DKC2 comment marks
 * the twin "Dead code, would crash SPC engine").
 */
#include <stdint.h>
#include <stdbool.h>

#include "snes_state.h"
#include "dream_ram.h"
#include "dream_alu.h"
#include "dream_time.h"

/* ---- names (tools/names.txt) ------------------------------------------- */
#define spc_dest_addr        0x0007   /* ram 0007: SPC destination of a block */
#define spc_word_count       0x0009   /* ram 0009: words left in a block */
#define spc_port0_counter    0x0046   /* ram 0046: our side of the $2140 count */

/* The rest of the scratch this bank uses is unnamed in tools/names.txt, so it
 * keeps the listing's raw addresses. From docs/dkc_crossref.md 3.2:
 *   $0004/$0006  ptr_04, the 24-bit block/source pointer
 *   $000B        word count saved for the return value
 *   $000E        sample id scratch (x3 into the sample table)
 *   $0010/$0012  sample-map cursor
 *   $0014/$0016  the entry values of $36 / $3A, kept for the second pass
 *   $0036/$0038  directory build cursor / its saved global end
 *   $003A/$003C  sample-data ARAM cursor / its saved global end
 *   $003E/$0040  running sample index / its saved global end
 *   $0042/$0044  sample-map pointer
 *   $0048        song number  */

#define APUIO0 0x2140
#define APUIO1 0x2141
#define APUIO2 0x2142

/* ---- instruction shapes this bank needs that dream_time.h lacks --------- */

/* SIMM16 and SIMM8 (immediate operands) live in dream_time.h now; this file used
 * to carry its own identical copy. */

/* jsr abs to a routine this file also converts: the operand word (in the step),
 * an internal cycle, then the return address, high byte first with the latch in
 * between. The callee is entered on the emulator, so its own hook fires; if it
 * yields, the ROM finishes it and returns to the frame pushed here, which is
 * the routine's real one, so this body just returns too. */
#define JSR(addr, target) do {                                                \
    const uint16_t sp0_ = ss_sp(ss);                                          \
    S((addr), 3);                                                             \
    ss_idle(ss);                                                              \
    { const uint16_t ret_ = (uint16_t) (ss_pc(ss) - 1);                       \
      ss_push8(ss, (uint8_t) (ret_ >> 8));                                    \
      ss_check_int(ss);                                                       \
      ss_push8(ss, (uint8_t) ret_); }                                         \
    ss_set_pc(ss, pb, (uint16_t) (target));                                   \
    if(ss_run_callee(ss, sp0_)) return;                                       \
    a = ss_a(ss); x = ss_x(ss); y = ss_y(ss);                                 \
  } while(0)

/* Hardware-register traffic. Identical in shape to t_read8/t_write16 and
 * friends, but through ss_reg_* as the API requires for $2100-$21FF. */
static uint8_t t_reg_read8(SnesState* ss, uint32_t adr) {
  ss_check_int(ss);
  return ss_reg_r8(ss, adr);
}

static uint16_t t_reg_read16(SnesState* ss, uint32_t adr) {
  uint8_t lo = ss_reg_r8(ss, adr);
  ss_check_int(ss);
  uint8_t hi = ss_reg_r8(ss, (adr + 1) & 0xffffff);
  return (uint16_t) (lo | (hi << 8));
}

static void t_reg_write8(SnesState* ss, uint32_t adr, uint8_t v) {
  ss_check_int(ss);
  ss_reg_w8(ss, adr, v);
}

static void t_reg_write16(SnesState* ss, uint32_t adr, uint16_t v) {
  ss_reg_w8(ss, adr, (uint8_t) v);
  ss_check_int(ss);
  ss_reg_w8(ss, (adr + 1) & 0xffffff, (uint8_t) (v >> 8));
}

/* A read-modify-write reads its word without a latch in between and writes it
 * back high byte first (cpu_writeWord's "reversed" form). */
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

static uint16_t t_inc16_abs(SnesState* ss, uint32_t adr) {
  uint16_t v = (uint16_t) (t_rmw_r16(ss, adr) + 1);
  ss_idle(ss);
  t_rmw_w16(ss, adr, v);
  ss_set_nz16(ss, v);
  return v;
}

static uint16_t t_dec16_abs(SnesState* ss, uint32_t adr) {
  uint16_t v = (uint16_t) (t_rmw_r16(ss, adr) - 1);
  ss_idle(ss);
  t_rmw_w16(ss, adr, v);
  ss_set_nz16(ss, v);
  return v;
}

static uint16_t t_lsr16_abs(SnesState* ss, uint32_t adr) {
  uint16_t v = t_rmw_r16(ss, adr);
  ss_idle(ss);
  ss_set_c(ss, (v & 1) != 0);
  v = (uint16_t) (v >> 1);
  t_rmw_w16(ss, adr, v);
  ss_set_nz16(ss, v);
  return v;
}

/* [dp]: the three pointer bytes come out of bank 0 with no latch between them. */
static uint32_t t_idl_ptr(SnesState* ss, uint16_t dpoff) {
  const uint16_t dp = ss_dp(ss);
  if(dp & 0xff) ss_idle(ss);      /* dpr low byte not 0: 1 extra cycle */
  uint32_t p = ss_bus_r8(ss, t_dp(dp, dpoff));
  p |= (uint32_t) ss_bus_r8(ss, t_dp(dp, dpoff + 1)) << 8;
  p |= (uint32_t) ss_bus_r8(ss, t_dp(dp, dpoff + 2)) << 16;
  return p;
}

/* cpx with an 8-bit index. */
static void alu_cpx8(SnesState* ss, uint8_t xv, uint8_t v) {
  unsigned r = (unsigned) xv + (uint8_t) ~v + 1u;
  ss_set_c(ss, r > 0xff);
  ss_set_nz8(ss, (uint8_t) r);
}

/* ---------------------------------------------------------------------------
 * upload_spc_block: $C1:8324 (docs/dkc_crossref.md 3.2: .upload_spc_block)
 *
 * Entry: $07 = SPC destination, $09 = word count, ptr_04 = 24-bit source.
 * Sends destination and count through the $2140 handshake, then `count` words,
 * bumping the counter for each and stepping the source pointer a page at a time
 * so the 8-bit Y index can address it (loc_C18377 is the page/bank carry).
 * Exit: A = 2 x count, C from the closing asl, X = the new counter value with
 * the index registers 16-bit again.
 *
 * upload_inline_spc_block falls in here through `bra`, so the body is shared;
 * the addresses are absolute either way because the fall-through target is this
 * routine's own first instruction.
 * ------------------------------------------------------------------------- */
static void upload_spc_block_body(SnesState* ss, uint8_t pb, uint16_t a) {
  uint16_t x = ss_x(ss), y = ss_y(ss);

  SEP(0x8324, 0x10);                        /* C18324 sep #$10 */
  x = (uint16_t) (x & 0xff);
  y = (uint16_t) (y & 0xff);
  S(0x8326, 3);                             /* C18326 ldx spc_port0_counter */
  { uint8_t v = t_read8(ss, ss_abs(ss, spc_port0_counter));
    x = v; ss_set_nz8(ss, v); }

  for(;;) {                                 /* loc_C18329 */
    S(0x8329, 3);                           /* C18329 cpx APUIO0 */
    uint8_t p0 = t_reg_read8(ss, ss_abs(ss, APUIO0));
    alu_cpx8(ss, (uint8_t) x, p0);
    const bool ne = (uint8_t) x != p0;
    S(0x832C, 1); t_branch(ss, ne);         /* C1832C bne loc_C18329 */
    if(!ne) break;
  }

  S(0x832E, 4);                             /* C1832E lda spc_dest_addr */
  a = t_read16(ss, spc_dest_addr); ss_set_nz16(ss, a);
  S(0x8332, 3);                             /* C18332 sta APUIO1 */
  t_reg_write16(ss, ss_abs(ss, APUIO1), a);
  SI(0x8335);                               /* C18335 inx */
  x = (uint16_t) ((x + 1) & 0xff); ss_set_nz8(ss, (uint8_t) x);
  S(0x8336, 3);                             /* C18336 stx APUIO0 */
  t_reg_write8(ss, ss_abs(ss, APUIO0), (uint8_t) x);
  S(0x8339, 4);                             /* C18339 lda spc_word_count */
  a = t_read16(ss, spc_word_count); ss_set_nz16(ss, a);
  S(0x833D, 4);                             /* C1833D sta $00000B */
  t_write16(ss, 0x00000B, a);

  for(;;) {                                 /* loc_C18341 */
    S(0x8341, 3);                           /* C18341 cpx APUIO0 */
    uint8_t p0 = t_reg_read8(ss, ss_abs(ss, APUIO0));
    alu_cpx8(ss, (uint8_t) x, p0);
    const bool ne = (uint8_t) x != p0;
    S(0x8344, 1); t_branch(ss, ne);         /* C18344 bne loc_C18341 */
    if(!ne) break;
  }

  S(0x8346, 3);                             /* C18346 sta APUIO1 */
  t_reg_write16(ss, ss_abs(ss, APUIO1), a);
  SI(0x8349);                               /* C18349 inx */
  x = (uint16_t) ((x + 1) & 0xff); ss_set_nz8(ss, (uint8_t) x);
  S(0x834A, 3);                             /* C1834A stx APUIO0 */
  t_reg_write8(ss, ss_abs(ss, APUIO0), (uint8_t) x);
  S(0x834D, 4);                             /* C1834D lda spc_word_count */
  a = t_read16(ss, spc_word_count); ss_set_nz16(ss, a);
  S(0x8351, 1); t_branch(ss, a == 0);       /* C18351 beq loc_C1836C */

  if(a != 0) {
    SIMM8(0x8353);                          /* C18353 ldy #$00 */
    y = 0; ss_set_nz8(ss, 0);

    for(;;) {                               /* loc_C18355 */
      S(0x8355, 2);                         /* C18355 lda [ptr_04],Y */
      { const uint32_t p = t_idl_ptr(ss, ptr_04);
        a = t_read16(ss, (p + y) & 0xffffff); ss_set_nz16(ss, a); }
      SI(0x8357);                           /* C18357 iny */
      y = (uint16_t) ((y + 1) & 0xff); ss_set_nz8(ss, (uint8_t) y);
      SI(0x8358);                           /* C18358 iny */
      y = (uint16_t) ((y + 1) & 0xff); ss_set_nz8(ss, (uint8_t) y);
      S(0x8359, 1); t_branch(ss, y == 0);   /* C18359 beq loc_C18377 */

      if(y == 0) {
        /* loc_C18377: Y wrapped, so step the source pointer on by a page. */
        S(0x8377, 3);                       /* C18377 ldy $0005 */
        { uint8_t v = t_read8(ss, ss_abs(ss, 0x0005)); y = v; ss_set_nz8(ss, v); }
        SI(0x837A);                         /* C1837A iny */
        y = (uint16_t) ((y + 1) & 0xff); ss_set_nz8(ss, (uint8_t) y);
        S(0x837B, 1); t_branch(ss, y != 0); /* C1837B bne loc_C1838B */
        if(y != 0) {
          S(0x838B, 3);                     /* C1838B sty $0005 */
          t_write8(ss, ss_abs(ss, 0x0005), (uint8_t) y);
          SIMM8(0x838E);                    /* C1838E ldy #$00 */
          y = 0; ss_set_nz8(ss, 0);
          S(0x8390, 1); t_branch(ss, true); /* C18390 bra loc_C1835B */
        } else {
          S(0x837D, 3);                     /* C1837D sty $0005 */
          t_write8(ss, ss_abs(ss, 0x0005), (uint8_t) y);
          S(0x8380, 3);                     /* C18380 ldy $0006 */
          { uint8_t v = t_read8(ss, ss_abs(ss, 0x0006)); y = v; ss_set_nz8(ss, v); }
          SI(0x8383);                       /* C18383 iny */
          y = (uint16_t) ((y + 1) & 0xff); ss_set_nz8(ss, (uint8_t) y);
          S(0x8384, 3);                     /* C18384 sty $0006 */
          t_write8(ss, ss_abs(ss, 0x0006), (uint8_t) y);
          SIMM8(0x8387);                    /* C18387 ldy #$00 */
          y = 0; ss_set_nz8(ss, 0);
          S(0x8389, 1); t_branch(ss, true); /* C18389 bra loc_C1835B */
        }
      }

      for(;;) {                             /* loc_C1835B */
        S(0x835B, 3);                       /* C1835B cpx APUIO0 */
        uint8_t p0 = t_reg_read8(ss, ss_abs(ss, APUIO0));
        alu_cpx8(ss, (uint8_t) x, p0);
        const bool ne = (uint8_t) x != p0;
        S(0x835E, 1); t_branch(ss, ne);     /* C1835E bne loc_C1835B */
        if(!ne) break;
      }
      SI(0x8360);                           /* C18360 inx */
      x = (uint16_t) ((x + 1) & 0xff); ss_set_nz8(ss, (uint8_t) x);
      S(0x8361, 3);                         /* C18361 sta APUIO1 */
      t_reg_write16(ss, ss_abs(ss, APUIO1), a);
      S(0x8364, 3);                         /* C18364 stx APUIO0 */
      t_reg_write8(ss, ss_abs(ss, APUIO0), (uint8_t) x);
      S(0x8367, 3);                         /* C18367 dec spc_word_count */
      const uint16_t wc = t_dec16_abs(ss, ss_abs(ss, spc_word_count));
      S(0x836A, 1); t_branch(ss, wc != 0);  /* C1836A bne loc_C18355 */
      if(wc == 0) break;
    }
  }

  /* loc_C1836C */
  S(0x836C, 3);                             /* C1836C stx spc_port0_counter */
  t_write8(ss, ss_abs(ss, spc_port0_counter), (uint8_t) x);
  REP(0x836F, 0x30);                        /* C1836F rep #$30 */
  S(0x8371, 4);                             /* C18371 lda $00000B */
  a = t_read16(ss, 0x00000B); ss_set_nz16(ss, a);
  SI(0x8375); a = alu_asl16(ss, a);         /* C18375 asl A */
  ss_set_a(ss, a);
  S(0x8376, 1);                             /* C18376 rts */
  ss_rts(ss);
}

static void upload_spc_block(SnesState* ss) {
  upload_spc_block_body(ss, ss_pb(ss), ss_a(ss));
}

/* ---------------------------------------------------------------------------
 * upload_inline_spc_block: $C1:830A (.upload_inline_spc_block)
 *
 * Entry: ptr_04 points at a `{dest, count}` header followed by the words.
 * Reads the header into $07/$09, steps ptr_04 past it and falls through into
 * upload_spc_block. Exit is upload_spc_block's.
 * ------------------------------------------------------------------------- */
static void upload_inline_spc_block(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);

  S(0x830A, 2);                             /* C1830A lda [ptr_04] */
  a = t_read16(ss, t_idl_ptr(ss, ptr_04)); ss_set_nz16(ss, a);
  S(0x830C, 4);                             /* C1830C sta spc_dest_addr */
  t_write16(ss, spc_dest_addr, a);
  S(0x8310, 3); t_inc16_abs(ss, ss_abs(ss, ptr_04));  /* C18310 inc ptr_04 */
  S(0x8313, 3); t_inc16_abs(ss, ss_abs(ss, ptr_04));  /* C18313 inc ptr_04 */
  S(0x8316, 2);                             /* C18316 lda [ptr_04] */
  a = t_read16(ss, t_idl_ptr(ss, ptr_04)); ss_set_nz16(ss, a);
  S(0x8318, 4);                             /* C18318 sta spc_word_count */
  t_write16(ss, spc_word_count, a);
  S(0x831C, 3); t_inc16_abs(ss, ss_abs(ss, ptr_04));  /* C1831C inc ptr_04 */
  S(0x831F, 3); t_inc16_abs(ss, ss_abs(ss, ptr_04));  /* C1831F inc ptr_04 */
  S(0x8322, 1); t_branch(ss, true);         /* C18322 bra upload_spc_block */

  upload_spc_block_body(ss, pb, a);
}

/* ---------------------------------------------------------------------------
 * spc_ipl_upload_loader: $C1:805A (.upload_spc_base_engine)
 *
 * The Nintendo IPL handshake: wait for $BBAA on $2140/$2141, answer with the
 * entry address $04D8 in $2142/$2143 and $CC in $2140, then feed the $88-byte
 * loader image at $C2:0000 one byte at a time, each byte in $2141 with its
 * index in $2140 and a wait for the SPC to echo the index back. The closing
 * $89 in $2140 is the "no more data, run it" word.
 * Exit: $46 zeroed, m and x flags both 16-bit again, X = $89, A = $0089.
 * ------------------------------------------------------------------------- */
static void spc_ipl_upload_loader(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);

  REP(0x805A, 0x20);                        /* C1805A rep #$20 */
  SEP(0x805C, 0x10);                        /* C1805C sep #$10 */
  x = (uint16_t) (x & 0xff);
  y = (uint16_t) (y & 0xff);
  SIMM16(0x805E); a = 0xBBAA; ss_set_nz16(ss, a);   /* C1805E lda #$BBAA */

  for(;;) {                                 /* loc_C18061 */
    S(0x8061, 3);                           /* C18061 cmp APUIO0 */
    const uint16_t p = t_reg_read16(ss, ss_abs(ss, APUIO0));
    alu_cmp16(ss, a, p);
    const bool ne = a != p;
    S(0x8064, 1); t_branch(ss, ne);         /* C18064 bne loc_C18061 */
    if(!ne) break;
  }

  SIMM16(0x8066); a = 0x04D8; ss_set_nz16(ss, a);   /* C18066 lda #$04D8 */
  S(0x8069, 3);                             /* C18069 sta APUIO2 */
  t_reg_write16(ss, ss_abs(ss, APUIO2), a);
  SIMM16(0x806C); a = 0x01CC; ss_set_nz16(ss, a);   /* C1806C lda #$01CC */
  S(0x806F, 3);                             /* C1806F sta APUIO0 */
  t_reg_write16(ss, ss_abs(ss, APUIO0), a);
  SI(0x8072);                               /* C18072 tax */
  x = (uint16_t) (a & 0xff); ss_set_nz8(ss, (uint8_t) x);

  for(;;) {                                 /* loc_C18073 */
    S(0x8073, 3);                           /* C18073 cpx APUIO0 */
    uint8_t p0 = t_reg_read8(ss, ss_abs(ss, APUIO0));
    alu_cpx8(ss, (uint8_t) x, p0);
    const bool ne = (uint8_t) x != p0;
    S(0x8076, 1); t_branch(ss, ne);         /* C18076 bne loc_C18073 */
    if(!ne) break;
  }

  SIMM8(0x8078); x = 0; ss_set_nz8(ss, 0);  /* C18078 ldx #$00 */

  for(;;) {                                 /* loc_C1807A */
    S(0x807A, 4);                           /* C1807A lda spc_loader_image,X */
    a = t_read16(ss, (0xC20000 + x) & 0xffffff); ss_set_nz16(ss, a);
    SI(0x807E);                             /* C1807E tay */
    y = (uint16_t) (a & 0xff); ss_set_nz8(ss, (uint8_t) y);
    S(0x807F, 3);                           /* C1807F sty APUIO1 */
    t_reg_write8(ss, ss_abs(ss, APUIO1), (uint8_t) y);
    S(0x8082, 3);                           /* C18082 stx APUIO0 */
    t_reg_write8(ss, ss_abs(ss, APUIO0), (uint8_t) x);

    for(;;) {                               /* loc_C18085 */
      S(0x8085, 3);                         /* C18085 cpx APUIO0 */
      uint8_t p0 = t_reg_read8(ss, ss_abs(ss, APUIO0));
      alu_cpx8(ss, (uint8_t) x, p0);
      const bool ne = (uint8_t) x != p0;
      S(0x8088, 1); t_branch(ss, ne);       /* C18088 bne loc_C18085 */
      if(!ne) break;
    }

    SI(0x808A);                             /* C1808A inx */
    x = (uint16_t) ((x + 1) & 0xff); ss_set_nz8(ss, (uint8_t) x);
    SIMM8(0x808B);                          /* C1808B cpx #$88 */
    alu_cpx8(ss, (uint8_t) x, 0x88);
    const bool more = (uint8_t) x != 0x88;
    S(0x808D, 1); t_branch(ss, more);       /* C1808D bne loc_C1807A */
    if(!more) break;
  }

  SI(0x808F);                               /* C1808F inx */
  x = (uint16_t) ((x + 1) & 0xff); ss_set_nz8(ss, (uint8_t) x);
  SI(0x8090); a = x; ss_set_nz16(ss, a);    /* C18090 txa */
  S(0x8091, 3);                             /* C18091 sta APUIO0 */
  t_reg_write16(ss, ss_abs(ss, APUIO0), a);

  for(;;) {                                 /* loc_C18094 */
    S(0x8094, 3);                           /* C18094 cpx APUIO0 */
    uint8_t p0 = t_reg_read8(ss, ss_abs(ss, APUIO0));
    alu_cpx8(ss, (uint8_t) x, p0);
    const bool ne = (uint8_t) x != p0;
    S(0x8097, 1); t_branch(ss, ne);         /* C18097 bne loc_C18094 */
    if(!ne) break;
  }

  S(0x8099, 3);                             /* C18099 stz spc_port0_counter */
  t_write16(ss, ss_abs(ss, spc_port0_counter), 0);
  REP(0x809C, 0x30);                        /* C1809C rep #$30 */
  S(0x809E, 1);                             /* C1809E rts */
  ss_rts(ss);
}

/* ---------------------------------------------------------------------------
 * spc_upload_driver: $C1:809F (.upload_spc_sound_engine)
 *
 * Points ptr_04 at $C2:0088 and streams $699 words to SPC $0560 through
 * upload_spc_block. Exit is upload_spc_block's (A = $0D32).
 * ------------------------------------------------------------------------- */
static void spc_upload_driver(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);

  REP(0x809F, 0x30);                        /* C1809F rep #$30 */
  SIMM16(0x80A1); a = 0x0088; ss_set_nz16(ss, a);   /* C180A1 lda #$0088 */
  S(0x80A4, 4); t_write16(ss, ptr_04, a);   /* C180A4 sta $000004 */
  SIMM16(0x80A8); a = 0x00C2; ss_set_nz16(ss, a);   /* C180A8 lda #$00C2 */
  S(0x80AB, 4); t_write16(ss, 0x000006, a); /* C180AB sta $000006 */
  SIMM16(0x80AF); a = 0x0560; ss_set_nz16(ss, a);   /* C180AF lda #$0560 */
  S(0x80B2, 4); t_write16(ss, spc_dest_addr, a);    /* C180B2 sta $000007 */
  SIMM16(0x80B6); a = 0x0699; ss_set_nz16(ss, a);   /* C180B6 lda #$0699 */
  S(0x80B9, 4); t_write16(ss, spc_word_count, a);   /* C180B9 sta $000009 */
  JSR(0x80BD, 0x8324);                      /* C180BD jsr upload_spc_block */
  S(0x80C0, 1);                             /* C180C0 rts */
  ss_rts(ss);
}

/* ---------------------------------------------------------------------------
 * upload_global_samples: $C1:80C1 (.upload_global_samples)
 *
 * Runs sample_uploader over the fixed global sample map at $C2:1109, building
 * the directory from ARAM $3100 and the sample data from $3400, then saves the
 * three cursors it left ($36/$3A/$3E) into $38/$3C/$40 so every later per-song
 * sample set restarts from the end of the global ones.
 * Exit: A = the saved $3E, flags from the last lda.
 * ------------------------------------------------------------------------- */
static void upload_global_samples(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);

  SIMM16(0x80C1); a = 0x1109; ss_set_nz16(ss, a);   /* C180C1 lda #$1109 */
  S(0x80C4, 4); t_write16(ss, 0x000042, a); /* C180C4 sta $000042 */
  SIMM16(0x80C8); a = 0x00C2; ss_set_nz16(ss, a);   /* C180C8 lda #$00C2 */
  S(0x80CB, 4); t_write16(ss, 0x000044, a); /* C180CB sta $000044 */
  SIMM16(0x80CF); a = 0x3100; ss_set_nz16(ss, a);   /* C180CF lda #$3100 */
  S(0x80D2, 4); t_write16(ss, 0x000036, a); /* C180D2 sta $000036 */
  S(0x80D6, 3); t_write16(ss, ss_abs(ss, 0x003E), 0);  /* C180D6 stz $003E */
  SIMM16(0x80D9); a = 0x3400; ss_set_nz16(ss, a);   /* C180D9 lda #$3400 */
  S(0x80DC, 4); t_write16(ss, 0x00003A, a); /* C180DC sta $00003A */
  S(0x80E0, 3); t_write16(ss, ss_abs(ss, 0x003E), 0);  /* C180E0 stz $003E */
  JSR(0x80E3, 0x815F);                      /* C180E3 jsr sample_uploader */

  S(0x80E6, 4);                             /* C180E6 lda $000036 */
  a = t_read16(ss, 0x000036); ss_set_nz16(ss, a);
  S(0x80EA, 4); t_write16(ss, 0x000038, a); /* C180EA sta $000038 */
  S(0x80EE, 4);                             /* C180EE lda $00003A */
  a = t_read16(ss, 0x00003A); ss_set_nz16(ss, a);
  S(0x80F2, 4); t_write16(ss, 0x00003C, a); /* C180F2 sta $00003C */
  S(0x80F6, 4);                             /* C180F6 lda $00003E */
  a = t_read16(ss, 0x00003E); ss_set_nz16(ss, a);
  S(0x80FA, 4); t_write16(ss, 0x000040, a); /* C180FA sta $000040 */
  ss_set_a(ss, a);
  S(0x80FE, 1);                             /* C180FE rts */
  ss_rts(ss);
}

/* ---------------------------------------------------------------------------
 * write_spc_command: $C1:80FF (.write_spc_command)
 *
 * Entry: X = the packed param:cmd word. Waits for the SPC to echo the current
 * handshake count, drops the word into $2141/$2142 as one 16-bit store, then
 * bumps the count in both $2140 and $46.
 * Exit: A = the word that was sent, X = the new count, both index registers
 * 16-bit again.
 * ------------------------------------------------------------------------- */
static void write_spc_command(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);

  REP(0x80FF, 0x30);                        /* C180FF rep #$30 */
  SI(0x8101); a = x; ss_set_nz16(ss, a);    /* C18101 txa */
  SEP(0x8102, 0x10);                        /* C18102 sep #$10 */
  x = (uint16_t) (x & 0xff);
  y = (uint16_t) (y & 0xff);
  S(0x8104, 3);                             /* C18104 ldx spc_port0_counter */
  { uint8_t v = t_read8(ss, ss_abs(ss, spc_port0_counter));
    x = v; ss_set_nz8(ss, v); }

  for(;;) {                                 /* loc_C18107 */
    S(0x8107, 3);                           /* C18107 cpx APUIO0 */
    uint8_t p0 = t_reg_read8(ss, ss_abs(ss, APUIO0));
    alu_cpx8(ss, (uint8_t) x, p0);
    const bool ne = (uint8_t) x != p0;
    S(0x810A, 1); t_branch(ss, ne);         /* C1810A bne loc_C18107 */
    if(!ne) break;
  }

  S(0x810C, 3);                             /* C1810C sta APUIO1 */
  t_reg_write16(ss, ss_abs(ss, APUIO1), a);
  SI(0x810F);                               /* C1810F inx */
  x = (uint16_t) ((x + 1) & 0xff); ss_set_nz8(ss, (uint8_t) x);
  S(0x8110, 3);                             /* C18110 stx APUIO0 */
  t_reg_write8(ss, ss_abs(ss, APUIO0), (uint8_t) x);
  S(0x8113, 3);                             /* C18113 stx spc_port0_counter */
  t_write8(ss, ss_abs(ss, spc_port0_counter), (uint8_t) x);
  REP(0x8116, 0x30);                        /* C18116 rep #$30 */
  S(0x8118, 1);                             /* C18118 rts */
  ss_rts(ss);
}

/* ---------------------------------------------------------------------------
 * upload_song_data: $C1:8119 (.upload_song_data)
 *
 * Entry: $48 = song number. Indexes the song x 6 table at $C2:10B9 for the
 * song's sequence-data pointer and uploads the block it heads.
 * Exit: upload_inline_spc_block's.
 * ------------------------------------------------------------------------- */
static void upload_song_data(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);

  S(0x8119, 4);                             /* C18119 lda $000048 */
  a = t_read16(ss, 0x000048); ss_set_nz16(ss, a);
  SI(0x811D); ss_set_c(ss, false);          /* C1811D clc */
  SI(0x811E); a = alu_asl16(ss, a);         /* C1811E asl A */
  S(0x811F, 4); t_write16(ss, ptr_04, a);   /* C1811F sta $000004 */
  SI(0x8123); a = alu_asl16(ss, a);         /* C18123 asl A */
  S(0x8124, 4);                             /* C18124 adc $000004 */
  a = alu_adc16(ss, a, t_read16(ss, ptr_04));
  SI(0x8128); x = a; ss_set_nz16(ss, x);    /* C18128 tax */
  S(0x8129, 4);                             /* C18129 lda data_C210B9,X */
  a = t_read16(ss, (0xC210B9 + x) & 0xffffff); ss_set_nz16(ss, a);
  S(0x812D, 4); t_write16(ss, ptr_04, a);   /* C1812D sta $000004 */
  S(0x8131, 4);                             /* C18131 lda data_C210BB,X */
  a = t_read16(ss, (0xC210BB + x) & 0xffffff); ss_set_nz16(ss, a);
  S(0x8135, 4); t_write16(ss, 0x000006, a); /* C18135 sta $000006 */
  JSR(0x8139, 0x830A);                      /* C18139 jsr upload_inline_spc_block */
  S(0x813C, 1);                             /* C1813C rts */
  ss_rts(ss);
}

/* ---------------------------------------------------------------------------
 * upload_song_sound_effects: $C1:813D (.upload_song_sound_effects)
 *
 * Entry: $48 = song number. Same shape as upload_song_data but the index is
 * song x 3 built out of two adc, into the table at $C2:10EE.
 * Exit: upload_inline_spc_block's.
 * ------------------------------------------------------------------------- */
static void upload_song_sound_effects(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);

  S(0x813D, 4);                             /* C1813D lda $000048 */
  a = t_read16(ss, 0x000048); ss_set_nz16(ss, a);
  SI(0x8141); ss_set_c(ss, false);          /* C18141 clc */
  S(0x8142, 4);                             /* C18142 adc $000048 */
  a = alu_adc16(ss, a, t_read16(ss, 0x000048));
  S(0x8146, 4);                             /* C18146 adc $000048 */
  a = alu_adc16(ss, a, t_read16(ss, 0x000048));
  SI(0x814A); x = a; ss_set_nz16(ss, x);    /* C1814A tax */
  S(0x814B, 4);                             /* C1814B lda data_C210EE,X */
  a = t_read16(ss, (0xC210EE + x) & 0xffffff); ss_set_nz16(ss, a);
  S(0x814F, 4); t_write16(ss, ptr_04, a);   /* C1814F sta $000004 */
  S(0x8153, 4);                             /* C18153 lda data_C210F0,X */
  a = t_read16(ss, (0xC210F0 + x) & 0xffffff); ss_set_nz16(ss, a);
  S(0x8157, 4); t_write16(ss, 0x000006, a); /* C18157 sta $000006 */
  JSR(0x815B, 0x830A);                      /* C1815B jsr upload_inline_spc_block */
  S(0x815E, 1);                             /* C1815E rts */
  ss_rts(ss);
}

/* ---------------------------------------------------------------------------
 * sample_uploader: $C1:815F (.sample_uploader)
 *
 * Entry: $42/$44 = the sample map (a word list of sample ids, $FFFF terminated,
 * followed by a second $FFFF-terminated list of remap slots), $36 = the ARAM
 * address the DSP sample directory is being built at, $3A = the ARAM address
 * the sample data goes to, $3E = the running sample index.
 *
 * Three passes:
 *   loc_C1818D  for each sample id, look its {start, loop} pointer pair up in
 *               the table at $C2:0DB9 and write the two ARAM directory words
 *               into the $7E2000 staging buffer, advancing $36 by 4 and $3A by
 *               the sample's length.
 *   loc_C181F0  upload the staged directory to ARAM, then (unless this is the
 *               global map, which the $1109/$00C2 compare detects) rebuild the
 *               $0560 remap table in $7E2000 from the second list and upload it.
 *   loc_C182AF  walk the map again and stream each sample's raw data, one
 *               upload_spc_block per sample, the word count being half the byte
 *               length rounded up (inc/lsr on $09).
 * Exit: A/flags as the closing branch left them; rts.
 * ------------------------------------------------------------------------- */
static void sample_uploader(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);

  S(0x815F, 3); t_write16(ss, ss_abs(ss, 0x003E), 0);  /* C1815F stz $003E */
  SIMM16(0x8162); x = 0x0000; ss_set_nz16(ss, x);      /* C18162 ldx #$0000 */
  S(0x8165, 4);                             /* C18165 lda $00003E */
  a = t_read16(ss, 0x00003E); ss_set_nz16(ss, a);
  S(0x8169, 4); t_write16(ss, 0x00000C, a); /* C18169 sta $00000C */
  S(0x816D, 4);                             /* C1816D lda $000042 */
  a = t_read16(ss, 0x000042); ss_set_nz16(ss, a);
  S(0x8171, 4); t_write16(ss, 0x000010, a); /* C18171 sta $000010 */
  S(0x8175, 4);                             /* C18175 lda $000044 */
  a = t_read16(ss, 0x000044); ss_set_nz16(ss, a);
  S(0x8179, 4); t_write16(ss, 0x000012, a); /* C18179 sta $000012 */
  S(0x817D, 4);                             /* C1817D lda $000036 */
  a = t_read16(ss, 0x000036); ss_set_nz16(ss, a);
  S(0x8181, 4); t_write16(ss, 0x000014, a); /* C18181 sta $000014 */
  S(0x8185, 4);                             /* C18185 lda $00003A */
  a = t_read16(ss, 0x00003A); ss_set_nz16(ss, a);
  S(0x8189, 4); t_write16(ss, 0x000016, a); /* C18189 sta $000016 */

  for(;;) {                                 /* loc_C1818D */
    S(0x818D, 2);                           /* C1818D lda [$10] */
    a = t_read16(ss, t_idl_ptr(ss, 0x0010)); ss_set_nz16(ss, a);
    S(0x818F, 3); t_inc16_abs(ss, ss_abs(ss, 0x0010));  /* C1818F inc $0010 */
    S(0x8192, 3); t_inc16_abs(ss, ss_abs(ss, 0x0010));  /* C18192 inc $0010 */
    SIMM16(0x8195); alu_cmp16(ss, a, 0xFFFF);           /* C18195 cmp #$FFFF */
    const bool done = a == 0xFFFF;
    S(0x8198, 1); t_branch(ss, done);       /* C18198 beq loc_C181F0 */
    if(done) break;

    S(0x819A, 4); t_write16(ss, 0x00000E, a);           /* C1819A sta $00000E */
    SI(0x819E); ss_set_c(ss, false);        /* C1819E clc */
    S(0x819F, 4);                           /* C1819F adc $00000E */
    a = alu_adc16(ss, a, t_read16(ss, 0x00000E));
    S(0x81A3, 4);                           /* C181A3 adc $00000E */
    a = alu_adc16(ss, a, t_read16(ss, 0x00000E));
    SI(0x81A7); y = x; ss_set_nz16(ss, y);  /* C181A7 txy */
    SI(0x81A8); x = a; ss_set_nz16(ss, x);  /* C181A8 tax */
    S(0x81A9, 4);                           /* C181A9 lda data_C20DB9,X */
    a = t_read16(ss, (0xC20DB9 + x) & 0xffffff); ss_set_nz16(ss, a);
    S(0x81AD, 4); t_write16(ss, ptr_04, a); /* C181AD sta $000004 */
    S(0x81B1, 4);                           /* C181B1 lda data_C20DBB,X */
    a = t_read16(ss, (0xC20DBB + x) & 0xffffff); ss_set_nz16(ss, a);
    S(0x81B5, 4); t_write16(ss, 0x000006, a);           /* C181B5 sta $000006 */
    SI(0x81B9); x = y; ss_set_nz16(ss, x);  /* C181B9 tyx */
    S(0x81BA, 4);                           /* C181BA lda $00003A */
    a = t_read16(ss, 0x00003A); ss_set_nz16(ss, a);
    S(0x81BE, 4);                           /* C181BE sta $7E2000,X */
    t_write16(ss, (0x7E2000 + x) & 0xffffff, a);
    SI(0x81C2); x = (uint16_t) (x + 1); ss_set_nz16(ss, x);  /* C181C2 inx */
    SI(0x81C3); x = (uint16_t) (x + 1); ss_set_nz16(ss, x);  /* C181C3 inx */
    S(0x81C4, 2);                           /* C181C4 lda [ptr_04] */
    a = t_read16(ss, t_idl_ptr(ss, ptr_04)); ss_set_nz16(ss, a);
    SI(0x81C6); ss_set_c(ss, false);        /* C181C6 clc */
    S(0x81C7, 4);                           /* C181C7 adc $00003A */
    a = alu_adc16(ss, a, t_read16(ss, 0x00003A));
    S(0x81CB, 4);                           /* C181CB sta $7E2000,X */
    t_write16(ss, (0x7E2000 + x) & 0xffffff, a);
    SI(0x81CF); x = (uint16_t) (x + 1); ss_set_nz16(ss, x);  /* C181CF inx */
    SI(0x81D0); x = (uint16_t) (x + 1); ss_set_nz16(ss, x);  /* C181D0 inx */
    S(0x81D1, 3); t_inc16_abs(ss, ss_abs(ss, 0x0036));  /* C181D1 inc $0036 */
    S(0x81D4, 3); t_inc16_abs(ss, ss_abs(ss, 0x0036));  /* C181D4 inc $0036 */
    S(0x81D7, 3); t_inc16_abs(ss, ss_abs(ss, 0x0036));  /* C181D7 inc $0036 */
    S(0x81DA, 3); t_inc16_abs(ss, ss_abs(ss, 0x0036));  /* C181DA inc $0036 */
    S(0x81DD, 3); t_inc16_abs(ss, ss_abs(ss, ptr_04));  /* C181DD inc ptr_04 */
    S(0x81E0, 3); t_inc16_abs(ss, ss_abs(ss, ptr_04));  /* C181E0 inc ptr_04 */
    S(0x81E3, 2);                           /* C181E3 lda [ptr_04] */
    a = t_read16(ss, t_idl_ptr(ss, ptr_04)); ss_set_nz16(ss, a);
    SI(0x81E5); ss_set_c(ss, false);        /* C181E5 clc */
    S(0x81E6, 4);                           /* C181E6 adc $00003A */
    a = alu_adc16(ss, a, t_read16(ss, 0x00003A));
    S(0x81EA, 4); t_write16(ss, 0x00003A, a);           /* C181EA sta $00003A */
    S(0x81EE, 1); t_branch(ss, true);       /* C181EE bra loc_C1818D */
  }

  /* loc_C181F0: upload the staged directory. */
  SIMM16(0x81F0); a = 0x2000; ss_set_nz16(ss, a);      /* C181F0 lda #$2000 */
  S(0x81F3, 4); t_write16(ss, ptr_04, a);   /* C181F3 sta $000004 */
  SIMM16(0x81F7); a = 0x007E; ss_set_nz16(ss, a);      /* C181F7 lda #$007E */
  S(0x81FA, 4); t_write16(ss, 0x000006, a); /* C181FA sta $000006 */
  S(0x81FE, 4);                             /* C181FE lda $000014 */
  a = t_read16(ss, 0x000014); ss_set_nz16(ss, a);
  S(0x8202, 4); t_write16(ss, spc_dest_addr, a);       /* C18202 sta $000007 */
  SIMM16(0x8206); a = 0x3400; ss_set_nz16(ss, a);      /* C18206 lda #$3400 */
  SI(0x8209); ss_set_c(ss, true);           /* C18209 sec */
  S(0x820A, 4);                             /* C1820A sbc $000014 */
  a = alu_sbc16(ss, a, t_read16(ss, 0x000014));
  SI(0x820E); ss_set_c(ss, false);          /* C1820E clc */
  SI(0x820F); a = alu_inc16(ss, a);         /* C1820F inc A */
  SI(0x8210); a = alu_lsr16(ss, a);         /* C18210 lsr A */
  S(0x8211, 4); t_write16(ss, spc_word_count, a);      /* C18211 sta $000009 */
  JSR(0x8215, 0x8324);                      /* C18215 jsr upload_spc_block */

  SIMM16(0x8218); a = 0x1109; ss_set_nz16(ss, a);      /* C18218 lda #$1109 */
  S(0x821B, 4); t_write16(ss, 0x000010, a); /* C1821B sta $000010 */
  SIMM16(0x821F); a = 0x00C2; ss_set_nz16(ss, a);      /* C1821F lda #$00C2 */
  S(0x8222, 4); t_write16(ss, 0x000012, a); /* C18222 sta $000012 */
  S(0x8226, 4);                             /* C18226 lda $000042 */
  a = t_read16(ss, 0x000042); ss_set_nz16(ss, a);
  S(0x822A, 4);                             /* C1822A cmp $000010 */
  { const uint16_t v = t_read16(ss, 0x000010); alu_cmp16(ss, a, v);
    const bool ne = a != v;
    S(0x822E, 1); t_branch(ss, ne);         /* C1822E bne loc_C1823A */
    if(!ne) {
      S(0x8230, 4);                         /* C18230 lda $000044 */
      a = t_read16(ss, 0x000044); ss_set_nz16(ss, a);
      S(0x8234, 4);                         /* C18234 cmp $000012 */
      const uint16_t v2 = t_read16(ss, 0x000012); alu_cmp16(ss, a, v2);
      const bool eq = a == v2;
      S(0x8238, 1); t_branch(ss, eq);       /* C18238 beq loc_C182A7 */
      if(eq) goto loc_C182A7;
    }
  }

  for(;;) {                                 /* loc_C1823A */
    S(0x823A, 2);                           /* C1823A lda [$10] */
    a = t_read16(ss, t_idl_ptr(ss, 0x0010)); ss_set_nz16(ss, a);
    SIMM16(0x823C); alu_cmp16(ss, a, 0xFFFF);          /* C1823C cmp #$FFFF */
    const bool done = a == 0xFFFF;
    S(0x823F, 1); t_branch(ss, done);       /* C1823F beq loc_C18259 */
    if(done) break;
    S(0x8241, 3); t_inc16_abs(ss, ss_abs(ss, 0x0010)); /* C18241 inc $0010 */
    S(0x8244, 3); t_inc16_abs(ss, ss_abs(ss, 0x0010)); /* C18244 inc $0010 */
    SI(0x8247); x = a; ss_set_nz16(ss, x);  /* C18247 tax */
    S(0x8248, 4);                           /* C18248 lda $00003E */
    a = t_read16(ss, 0x00003E); ss_set_nz16(ss, a);
    SEP(0x824C, 0x20);                      /* C1824C sep #$20 */
    S(0x824E, 4);                           /* C1824E sta $7E2000,X */
    t_write8(ss, (0x7E2000 + x) & 0xffffff, (uint8_t) a);
    REP(0x8252, 0x20);                      /* C18252 rep #$20 */
    S(0x8254, 3); t_inc16_abs(ss, ss_abs(ss, 0x003E)); /* C18254 inc $003E */
    S(0x8257, 1); t_branch(ss, true);       /* C18257 bra loc_C1823A */
  }

  /* loc_C18259 */
  S(0x8259, 4);                             /* C18259 lda $000042 */
  a = t_read16(ss, 0x000042); ss_set_nz16(ss, a);
  S(0x825D, 4); t_write16(ss, 0x000010, a); /* C1825D sta $000010 */
  S(0x8261, 4);                             /* C18261 lda $000044 */
  a = t_read16(ss, 0x000044); ss_set_nz16(ss, a);
  S(0x8265, 4); t_write16(ss, 0x000012, a); /* C18265 sta $000012 */

  for(;;) {                                 /* loc_C18269 */
    S(0x8269, 2);                           /* C18269 lda [$10] */
    a = t_read16(ss, t_idl_ptr(ss, 0x0010)); ss_set_nz16(ss, a);
    SIMM16(0x826B); alu_cmp16(ss, a, 0xFFFF);          /* C1826B cmp #$FFFF */
    const bool done = a == 0xFFFF;
    S(0x826E, 1); t_branch(ss, done);       /* C1826E beq loc_C18288 */
    if(done) break;
    S(0x8270, 3); t_inc16_abs(ss, ss_abs(ss, 0x0010)); /* C18270 inc $0010 */
    S(0x8273, 3); t_inc16_abs(ss, ss_abs(ss, 0x0010)); /* C18273 inc $0010 */
    SI(0x8276); x = a; ss_set_nz16(ss, x);  /* C18276 tax */
    S(0x8277, 4);                           /* C18277 lda $00003E */
    a = t_read16(ss, 0x00003E); ss_set_nz16(ss, a);
    SEP(0x827B, 0x20);                      /* C1827B sep #$20 */
    S(0x827D, 4);                           /* C1827D sta $7E2000,X */
    t_write8(ss, (0x7E2000 + x) & 0xffffff, (uint8_t) a);
    REP(0x8281, 0x20);                      /* C18281 rep #$20 */
    S(0x8283, 3); t_inc16_abs(ss, ss_abs(ss, 0x003E)); /* C18283 inc $003E */
    S(0x8286, 1); t_branch(ss, true);       /* C18286 bra loc_C18269 */
  }

  /* loc_C18288: upload the rebuilt $0560 remap table. */
  SIMM16(0x8288); a = 0x2000; ss_set_nz16(ss, a);      /* C18288 lda #$2000 */
  S(0x828B, 4); t_write16(ss, ptr_04, a);   /* C1828B sta $000004 */
  SIMM16(0x828F); a = 0x007E; ss_set_nz16(ss, a);      /* C1828F lda #$007E */
  S(0x8292, 4); t_write16(ss, 0x000006, a); /* C18292 sta $000006 */
  SIMM16(0x8296); a = 0x0560; ss_set_nz16(ss, a);      /* C18296 lda #$0560 */
  S(0x8299, 4); t_write16(ss, spc_dest_addr, a);       /* C18299 sta $000007 */
  SIMM16(0x829D); a = 0x0080; ss_set_nz16(ss, a);      /* C1829D lda #$0080 */
  S(0x82A0, 4); t_write16(ss, spc_word_count, a);      /* C182A0 sta $000009 */
  JSR(0x82A4, 0x8324);                      /* C182A4 jsr upload_spc_block */

 loc_C182A7:
  S(0x82A7, 4);                             /* C182A7 lda $000016 */
  a = t_read16(ss, 0x000016); ss_set_nz16(ss, a);
  S(0x82AB, 4); t_write16(ss, 0x00003A, a); /* C182AB sta $00003A */

  for(;;) {                                 /* loc_C182AF */
    S(0x82AF, 2);                           /* C182AF lda [$42] */
    a = t_read16(ss, t_idl_ptr(ss, 0x0042)); ss_set_nz16(ss, a);
    SIMM16(0x82B1); alu_cmp16(ss, a, 0xFFFF);          /* C182B1 cmp #$FFFF */
    const bool done = a == 0xFFFF;
    S(0x82B4, 1); t_branch(ss, done);       /* C182B4 beq loc_C18309 */
    if(done) break;
    S(0x82B6, 3); t_inc16_abs(ss, ss_abs(ss, 0x0042)); /* C182B6 inc $0042 */
    S(0x82B9, 3); t_inc16_abs(ss, ss_abs(ss, 0x0042)); /* C182B9 inc $0042 */
    S(0x82BC, 4); t_write16(ss, 0x00000E, a);          /* C182BC sta $00000E */
    SI(0x82C0); ss_set_c(ss, false);        /* C182C0 clc */
    S(0x82C1, 4);                           /* C182C1 adc $00000E */
    a = alu_adc16(ss, a, t_read16(ss, 0x00000E));
    S(0x82C5, 4);                           /* C182C5 adc $00000E */
    a = alu_adc16(ss, a, t_read16(ss, 0x00000E));
    SI(0x82C9); x = a; ss_set_nz16(ss, x);  /* C182C9 tax */
    S(0x82CA, 4);                           /* C182CA lda data_C20DB9,X */
    a = t_read16(ss, (0xC20DB9 + x) & 0xffffff); ss_set_nz16(ss, a);
    S(0x82CE, 4); t_write16(ss, ptr_04, a); /* C182CE sta $000004 */
    S(0x82D2, 4);                           /* C182D2 lda data_C20DBB,X */
    a = t_read16(ss, (0xC20DBB + x) & 0xffffff); ss_set_nz16(ss, a);
    S(0x82D6, 4); t_write16(ss, 0x000006, a);          /* C182D6 sta $000006 */
    S(0x82DA, 3); t_inc16_abs(ss, ss_abs(ss, ptr_04)); /* C182DA inc ptr_04 */
    S(0x82DD, 3); t_inc16_abs(ss, ss_abs(ss, ptr_04)); /* C182DD inc ptr_04 */
    S(0x82E0, 4);                           /* C182E0 lda $00003A */
    a = t_read16(ss, 0x00003A); ss_set_nz16(ss, a);
    S(0x82E4, 4); t_write16(ss, spc_dest_addr, a);     /* C182E4 sta $000007 */
    S(0x82E8, 2);                           /* C182E8 lda [ptr_04] */
    a = t_read16(ss, t_idl_ptr(ss, ptr_04)); ss_set_nz16(ss, a);
    S(0x82EA, 4); t_write16(ss, spc_word_count, a);    /* C182EA sta $000009 */
    SI(0x82EE); ss_set_c(ss, false);        /* C182EE clc */
    S(0x82EF, 4);                           /* C182EF adc $000007 */
    a = alu_adc16(ss, a, t_read16(ss, spc_dest_addr));
    S(0x82F3, 4); t_write16(ss, 0x00003A, a);          /* C182F3 sta $00003A */
    SI(0x82F7); ss_set_c(ss, false);        /* C182F7 clc */
    S(0x82F8, 3); t_inc16_abs(ss, ss_abs(ss, spc_word_count));  /* C182F8 inc spc_word_count */
    S(0x82FB, 3); t_lsr16_abs(ss, ss_abs(ss, spc_word_count));  /* C182FB lsr spc_word_count */
    S(0x82FE, 3); t_inc16_abs(ss, ss_abs(ss, ptr_04)); /* C182FE inc ptr_04 */
    S(0x8301, 3); t_inc16_abs(ss, ss_abs(ss, ptr_04)); /* C18301 inc ptr_04 */
    JSR(0x8304, 0x8324);                    /* C18304 jsr upload_spc_block */
    S(0x8307, 1); t_branch(ss, true);       /* C18307 bra loc_C182AF */
  }

  /* loc_C18309 */
  ss_set_a(ss, a);
  ss_set_x(ss, x);
  ss_set_y(ss, y);
  S(0x8309, 1);                             /* C18309 rts */
  ss_rts(ss);
}

/* ---------------------------------------------------------------------------
 * upload_song_sample_set: $C1:8392 (.upload_song_sample_set)
 *
 * Entry: $48 = song number. Takes the song's sample-map pointer out of the
 * song x 6 table at $C2:10BC, restores the three cursors the global upload
 * saved, and rebuilds the directory on top of them.
 * Exit: sample_uploader's.
 * ------------------------------------------------------------------------- */
static void upload_song_sample_set(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);

  S(0x8392, 4);                             /* C18392 lda $000048 */
  a = t_read16(ss, 0x000048); ss_set_nz16(ss, a);
  SI(0x8396); ss_set_c(ss, false);          /* C18396 clc */
  SI(0x8397); a = alu_asl16(ss, a);         /* C18397 asl A */
  S(0x8398, 4); t_write16(ss, ptr_04, a);   /* C18398 sta $000004 */
  SI(0x839C); a = alu_asl16(ss, a);         /* C1839C asl A */
  S(0x839D, 4);                             /* C1839D adc $000004 */
  a = alu_adc16(ss, a, t_read16(ss, ptr_04));
  SI(0x83A1); x = a; ss_set_nz16(ss, x);    /* C183A1 tax */
  S(0x83A2, 4);                             /* C183A2 lda data_C210BC,X */
  a = t_read16(ss, (0xC210BC + x) & 0xffffff); ss_set_nz16(ss, a);
  S(0x83A6, 4); t_write16(ss, 0x000042, a); /* C183A6 sta $000042 */
  S(0x83AA, 4);                             /* C183AA lda data_C210BE,X */
  a = t_read16(ss, (0xC210BE + x) & 0xffffff); ss_set_nz16(ss, a);
  S(0x83AE, 4); t_write16(ss, 0x000044, a); /* C183AE sta $000044 */
  S(0x83B2, 4);                             /* C183B2 lda $000038 */
  a = t_read16(ss, 0x000038); ss_set_nz16(ss, a);
  S(0x83B6, 4); t_write16(ss, 0x000036, a); /* C183B6 sta $000036 */
  S(0x83BA, 4);                             /* C183BA lda $00003C */
  a = t_read16(ss, 0x00003C); ss_set_nz16(ss, a);
  S(0x83BE, 4); t_write16(ss, 0x00003A, a); /* C183BE sta $00003A */
  S(0x83C2, 4);                             /* C183C2 lda $000040 */
  a = t_read16(ss, 0x000040); ss_set_nz16(ss, a);
  S(0x83C6, 4); t_write16(ss, 0x00003E, a); /* C183C6 sta $00003E */
  JSR(0x83CA, 0x815F);                      /* C183CA jsr sample_uploader */
  S(0x83CD, 1);                             /* C183CD rts */
  ss_rts(ss);
}

/* ---------------------------------------------------------------------------
 * execute_spc_sound_engine: $C1:803E (.execute_spc_sound_engine)
 *
 * A zero-length block whose destination is the driver entry $0672: the loader
 * on the SPC side reads a count of 0 as "jump there".
 * Exit: upload_spc_block's (A = 0, Z set, C clear).
 * ------------------------------------------------------------------------- */
static void execute_spc_sound_engine(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);

  SIMM16(0x803E); a = 0x0672; ss_set_nz16(ss, a);      /* C1803E lda #$0672 */
  S(0x8041, 4); t_write16(ss, spc_dest_addr, a);       /* C18041 sta $000007 */
  S(0x8045, 3);                             /* C18045 stz spc_word_count */
  t_write16(ss, ss_abs(ss, spc_word_count), 0);
  JSR(0x8048, 0x8324);                      /* C18048 jsr upload_spc_block */
  S(0x804B, 1);                             /* C1804B rts */
  ss_rts(ss);
}

/* ---------------------------------------------------------------------------
 * spc_init: $C1:8000 (.upload_spc_engine)
 *
 * The whole reset-time sound bring-up: clear the sample/directory cursors and
 * the handshake count, run the IPL handshake, upload the driver and the global
 * samples, then upload the inline block headed at $C2:2E5C and start the
 * engine. Reached once, by jsl from reset ($C0:801E).
 * Exit: rtl, A/flags as execute_spc_sound_engine left them.
 * ------------------------------------------------------------------------- */
static void spc_init(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);

  REP(0x8000, 0x30);                        /* C18000 rep #$30 */
  S(0x8002, 3); t_write16(ss, ss_abs(ss, 0x0036), 0);  /* C18002 stz $0036 */
  S(0x8005, 3); t_write16(ss, ss_abs(ss, 0x0038), 0);  /* C18005 stz $0038 */
  S(0x8008, 3); t_write16(ss, ss_abs(ss, 0x003A), 0);  /* C18008 stz $003A */
  S(0x800B, 3); t_write16(ss, ss_abs(ss, 0x003C), 0);  /* C1800B stz $003C */
  S(0x800E, 3); t_write16(ss, ss_abs(ss, 0x003E), 0);  /* C1800E stz $003E */
  S(0x8011, 3); t_write16(ss, ss_abs(ss, 0x0040), 0);  /* C18011 stz $0040 */
  S(0x8014, 3); t_write16(ss, ss_abs(ss, 0x0042), 0);  /* C18014 stz $0042 */
  S(0x8017, 3); t_write16(ss, ss_abs(ss, 0x0044), 0);  /* C18017 stz $0044 */
  S(0x801A, 3); t_write16(ss, ss_abs(ss, 0x0048), 0);  /* C1801A stz $0048 */
  S(0x801D, 3);                             /* C1801D stz spc_port0_counter */
  t_write16(ss, ss_abs(ss, spc_port0_counter), 0);

  JSR(0x8020, 0x805A);                      /* C18020 jsr spc_ipl_upload_loader */
  JSR(0x8023, 0x809F);                      /* C18023 jsr spc_upload_driver */
  JSR(0x8026, 0x80C1);                      /* C18026 jsr upload_global_samples */

  SIMM16(0x8029); a = 0x2E5C; ss_set_nz16(ss, a);      /* C18029 lda #$2E5C */
  S(0x802C, 4); t_write16(ss, ptr_04, a);   /* C1802C sta $000004 */
  SIMM16(0x8030); a = 0x00C2; ss_set_nz16(ss, a);      /* C18030 lda #$00C2 */
  S(0x8033, 4); t_write16(ss, 0x000006, a); /* C18033 sta $000006 */

  JSR(0x8037, 0x830A);                      /* C18037 jsr upload_inline_spc_block */
  JSR(0x803A, 0x803E);                      /* C1803A jsr execute_spc_sound_engine */
  S(0x803D, 1);                             /* C1803D rtl */
  ss_rtl(ss);
}

/* ---------------------------------------------------------------------------
 * spc_command: $C1:83CE (closest DKC2 analogue: .play_song)
 *
 * Entry: A = the song/command number. Splits it into $48 (the song number, low
 * byte) and the command word `param:$FF` ($FF is the driver's "stop and
 * return to the loader" command, which puts the SPC back in a state that
 * accepts uploads). Then the three per-song uploads, a restart, and $FE
 * ("play"). Reached by jsl from reset and from the mode-init path.
 * Exit: rtl, A/flags as the closing write_spc_command left them.
 * ------------------------------------------------------------------------- */
static void spc_command(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);

  SI(0x83CE); x = a; ss_set_nz16(ss, x);    /* C183CE tax */
  SIMM16(0x83CF); a = alu_and16(ss, a, 0x00FF);        /* C183CF and #$00FF */
  S(0x83D2, 4); t_write16(ss, 0x000048, a); /* C183D2 sta $000048 */
  SI(0x83D6); a = x; ss_set_nz16(ss, a);    /* C183D6 txa */
  SIMM16(0x83D7); a = alu_ora16(ss, a, 0x00FF);        /* C183D7 ora #$00FF */
  SI(0x83DA); x = a; ss_set_nz16(ss, x);    /* C183DA tax */

  JSR(0x83DB, 0x80FF);                      /* C183DB jsr write_spc_command */
  JSR(0x83DE, 0x8392);                      /* C183DE jsr upload_song_sample_set */
  JSR(0x83E1, 0x8119);                      /* C183E1 jsr upload_song_data */
  JSR(0x83E4, 0x813D);                      /* C183E4 jsr upload_song_sound_effects */
  JSR(0x83E7, 0x803E);                      /* C183E7 jsr execute_spc_sound_engine */

  SIMM16(0x83EA); x = 0x00FE; ss_set_nz16(ss, x);      /* C183EA ldx #$00FE */
  JSR(0x83ED, 0x80FF);                      /* C183ED jsr write_spc_command */
  S(0x83F0, 1);                             /* C183F0 rtl */
  ss_rtl(ss);
}

/* ---------------------------------------------------------------------------
 * unused_spc_set_e7_and_play: $C1:83F1, unused_spc_set_fb_and_play: $C1:8403
 *
 * Two dead siblings of spc_command's tail, eighteen bytes each and identical
 * apart from one immediate byte. Each builds a command word out of the
 * accumulator's *high* byte (the xba puts the caller's high byte in the
 * parameter), sends it, then sends $FE ("play"), skipping every upload step
 * spc_command does first.
 *
 * The commands are $F9 and $FB: the driver takes `cmd & 7` as the index into
 * cmd_table, so $F9 is cmd1_set_E7 (parameter into $E7, which nothing else in
 * the traced driver reads, per spc/spc_map.txt) and $FB is cmd3_fade_and_song.
 * Nothing calls either: no jsr, jsl or table word anywhere in out/dream.asm
 * names either address. Only one
 * of the two is traced as code at a time (the tracer's orphan sweep reaches
 * whichever the label file forces), so both are converted and both are
 * credited by the unit gate rather than by a script (config/recomp_units.txt,
 * `dream_harness --unit`).
 *
 * Entry: A high byte = the parameter. Exit: rtl, A/flags as the closing
 * write_spc_command left them.
 * ------------------------------------------------------------------------- */
static void unused_spc_set_and_play(SnesState* ss, uint16_t base, uint16_t cmd) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);

  S(base + 0, 1);                           /* C183F1 xba */
  { const uint16_t r = (uint16_t) ((a >> 8) | (a << 8));
    ss_set_nz8(ss, (uint8_t) r);            /* the byte that moved into the low half */
    ss_idle(ss);
    ss_check_int(ss);
    ss_idle(ss);
    a = r; }
  SIMM16(base + 1); a = alu_and16(ss, a, 0xFF00);      /* C183F2 and #$FF00 */
  SIMM16(base + 4); a = alu_ora16(ss, a, cmd);         /* C183F5 ora #$00F9 */
  SI(base + 7); x = a; ss_set_nz16(ss, x);  /* C183F8 tax */

  JSR(base + 8, 0x80FF);                    /* C183F9 jsr write_spc_command */

  SIMM16(base + 11); x = 0x00FE; ss_set_nz16(ss, x);   /* C183FC ldx #$00FE */
  JSR(base + 14, 0x80FF);                   /* C183FF jsr write_spc_command */

  S(base + 17, 1);                          /* C18402 rtl */
  ss_rtl(ss);
}

static void unused_spc_set_e7_and_play(SnesState* ss) {
  unused_spc_set_and_play(ss, 0x83F1, 0x00F9);
}

static void unused_spc_set_fb_and_play(SnesState* ss) {
  unused_spc_set_and_play(ss, 0x8403, 0x00FB);
}

/* ---------------------------------------------------------------------------
 * sfx_command_dispatch: $C1:8415
 *
 * Entry: A = the packed channel:sfx_id word, from play_sound_effect
 * ($C0:B0C5). A raw pass-through to write_spc_command; DKC2 kept the same
 * shape as dead code and queues sound effects instead
 * (docs/dkc_crossref.md 3.2).
 * Exit: rtl, A/flags as write_spc_command left them.
 * ------------------------------------------------------------------------- */
static void sfx_command_dispatch(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);

  SI(0x8415); x = a; ss_set_nz16(ss, x);    /* C18415 tax */
  JSR(0x8416, 0x80FF);                      /* C18416 jsr write_spc_command */
  S(0x8419, 1);                             /* C18419 rtl */
  ss_rtl(ss);
}

static const RecompEntry kSoundIface[] = {
  { 0xc18000, "spc_init",                  spc_init },
  { 0xc1803e, "execute_spc_sound_engine",  execute_spc_sound_engine },
  { 0xc1805a, "spc_ipl_upload_loader",     spc_ipl_upload_loader },
  { 0xc1809f, "spc_upload_driver",         spc_upload_driver },
  { 0xc180c1, "upload_global_samples",     upload_global_samples },
  { 0xc180ff, "write_spc_command",         write_spc_command },
  { 0xc18119, "upload_song_data",          upload_song_data },
  { 0xc1813d, "upload_song_sound_effects", upload_song_sound_effects },
  { 0xc1815f, "sample_uploader",           sample_uploader },
  { 0xc1830a, "upload_inline_spc_block",   upload_inline_spc_block },
  { 0xc18324, "upload_spc_block",          upload_spc_block },
  { 0xc18392, "upload_song_sample_set",    upload_song_sample_set },
  { 0xc183ce, "spc_command",               spc_command },
  { 0xc183f1, "unused_spc_set_e7_and_play", unused_spc_set_e7_and_play },
  { 0xc18403, "unused_spc_set_fb_and_play", unused_spc_set_fb_and_play },
  { 0xc18415, "sfx_command_dispatch",      sfx_command_dispatch },
};
RECOMP_REGISTER(kSoundIface)
