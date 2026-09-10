/* Sprite-frame OAM emission, bank $C0.
 *
 * entity_build_oam_frame ($C0:A538) walks entity_render_order, resolves each
 * entity's sprite frame through the {ptr16, bank, y-bias} table at $C4:0000
 * (docs/data_formats.md), culls it against the screen, and hands the frame to
 * one of six emitters chosen by the two flip bits of entity_flags and by the
 * frame id being below or above 4:
 *
 *     entity_flags        frame id     emitter
 *     ---------------------------------------------------------------
 *     $8000 clear, $4000 clear   < 4   oam_emit_frame_1row      $A757
 *                               >= 4   oam_emit_frame_2row      $A772
 *     $8000 clear, $4000 set     < 4   oam_emit_frame_1row_flip $A8F6
 *                               >= 4   oam_emit_frame_2row_flip $A911
 *     $8000 set,   $4000 clear   any   oam_emit_frame_3row      $AAAA
 *     $8000 set,   $4000 set     any   oam_emit_frame_3row_flip $AC40
 *
 * The ROM holds four copies of the emit loop, one per flip combination, and
 * pairs of entry points share a copy: oam_emit_frame_1row jumps into
 * oam_emit_frame_2row's loop and the two flip 1-row/2-row entries likewise. The
 * four copies are the same instruction stream apart from two complements and one
 * screen-position adjustment, so this file transliterates the loop once and
 * names its addresses relative to the copy's base, the way dream_time.h's header
 * describes for a body reached at more than one address. Every offset below was
 * checked against all four copies in out/dream.asm; the copies' bases are:
 *
 *     variant          entry(s)        header  loop 1  loop 2  loop 3  exit
 *     ------------------------------------------------------------------------
 *     plain            $A757 $A772     $A791   $A7B5   $A82D   $A89C   $A8F1
 *     h-flip           $A8F6 $A911     $A930   $A954   $A9DB   $AA4D   $AAA5
 *     v-flip           $AAAA           $AAC9   $AAED   $AB73   $ABE4   $AC3B
 *     both             $AC40           $AC5F   $AC83   $AD14   $AD88   $ADE2
 *
 * An emitter has three exits, not one: the two "OAM is full" tests inside it
 * throw the jsr's return address away (pla) and jump straight to the shared
 * $C0:A6CD tail of entity_build_oam_frame. That is why the bodies here carry a
 * three-way result instead of void: entity_build_oam_frame has to know which
 * exit its callee took, and a yield inside an emitter has to reach it too.
 * The dispatch itself goes through the emulator (t_jsr_emitter below) rather
 * than being a C call, so the emitter's own hook is what runs its C body and
 * the harness counts the call against the emitter's entry address.
 *
 * The whole routine runs with DB = $80 (phk/plb at $A553 after the jsl from
 * $80:823F), so the entity arrays, the OAM buffer and the two ROM tables at
 * $A6D3/$A6D7 are all reached through ss_abs().
 */
#include <stdint.h>
#include <stdbool.h>

#include "snes_state.h"
#include "dream_ram.h"
#include "dream_alu.h"
#include "dream_time.h"

/* RAM names from tools/names.txt and docs/naming_proposals.md sections 7 and 8
 * that dream_ram.h does not carry yet. Guarded because that header is shared
 * and gains names as other subsystems are converted. */
#ifndef sprite_frame_ptr
#define sprite_frame_ptr     0x0026   /* $26/$28: 24-bit pointer to the frame */
#endif
#ifndef sprite_frame_bank
#define sprite_frame_bank    0x0028
#endif
#ifndef sprite_frame_ptr2
#define sprite_frame_ptr2    0x002A   /* $2A/$2C: the same pointer plus one */
#endif
#ifndef sprite_frame_bank2
#define sprite_frame_bank2   0x002C
#endif
#ifndef oam_entry_ptr
#define oam_entry_ptr        0x0054   /* $54/$55: $0400 + the high-OAM index */
#endif

/* The generic direct-page scratch the OAM builder uses: $18/$1A carry the
 * entity's flag word (the second one is advanced per sprite), $1C..$23 the four
 * frame-header words the emitter reads out of the frame data, $24 the rotating
 * size-bit pattern from data_C0A6D3. docs/naming_proposals.md section 8 leaves
 * these unnamed on purpose - a dozen unrelated routines reuse them. */
#define oam_flag_word        0x0018   /* $18: entity_flags, held for the frame */
#define oam_flag_cursor      0x001A   /* $1A: the same word, advanced per sprite */
#define oam_rows_0           0x001C   /* $1C: row 0 sprite count (counts down) */
#define oam_rows_1           0x001D   /* $1D: row 1 sprite count */
#define oam_row_1_base       0x001E   /* $1E: row 1 tile/attribute base */
#define oam_rows_2           0x001F   /* $1F: row 2 sprite count */
#define oam_row_2_base       0x0020   /* $20: row 2 tile/attribute base */
#define oam_tile_src_lo      0x0021   /* $21: read as the word at $21/$22 */
#define oam_row_3_base       0x0022   /* $22: row 3 tile/attribute base */
#define oam_tile_src_hi      0x0023   /* $23: read as the word at $23/$24 */
#define oam_size_bits        0x0024   /* $24: the high-OAM size/x9 bit pattern */
#define bg2_scroll_bias      0x0076   /* $76: subtracted from the screen Y */
#define screen_y_bias_92     0x0092   /* $92: added to the screen Y */

#ifndef entity_frame_shown
#define entity_frame_shown   0x07E8   /* $07E8: the frame id actually drawn */
#endif
#ifndef entity_frame_loaded
#define entity_frame_loaded  0x0808   /* $0808: the frame id whose tiles are in VRAM */
#endif
#ifndef entity_z_dead
#define entity_z_dead        0x08E8
#endif
#ifndef entity_tile_job_ptr
#define entity_tile_job_ptr  0x0A88   /* $0A88: write cursor into entity_tile_job */
#endif

#define sprite_frame_table   0xC40000 /* data_C40000/data_C40002: {ptr16, bank:bias} */
#define oam_size_pattern     0xA6D3   /* data_C0A6D3: 4 bytes */
#define oam_x9_pattern       0xA6D7   /* data_C0A6D7: 128 bytes */

/* ---------------------------------------------------------------------------
 * dream_time.h's step macros, with the emitters' three-way return value: an
 * emitter body has to say which of its three exits it took, because the two
 * "OAM is full" ones do not return to their caller at all.
 * ------------------------------------------------------------------------- */
typedef enum {
  EMIT_RETURNED,  /* the emitter's own rts ran; the pc is back at the caller */
  EMIT_TAIL,      /* "OAM full": the return address was pulled, pc = $A6CD */
  EMIT_YIELD      /* the rest of the routine was handed back to the ROM */
} EmitResult;

#define ES(addr, n)   do { if(t_step(ss, pb, (uint16_t) (addr), (n), a, x, y)) return EMIT_YIELD; } while(0)
#define ESI(addr)     do { if(t_step_imp(ss, pb, (uint16_t) (addr), a, x, y)) return EMIT_YIELD; } while(0)
#define ESEP(addr, b) do { if(t_step_sep(ss, pb, (uint16_t) (addr), (b), a, x, y)) return EMIT_YIELD; } while(0)
#define EREP(addr, b) do { if(t_step_rep(ss, pb, (uint16_t) (addr), (b), a, x, y)) return EMIT_YIELD; } while(0)

/* ---------------------------------------------------------------------------
 * 65816 pieces dream_alu.h and dream_time.h do not carry yet: the 8-bit (m = 1)
 * arithmetic these routines switch into for every register-sized store, the
 * three addressing modes they use that no converted routine used before, and
 * the two read-modify-write forms. All written against LakeSnes' own opcode and
 * addressing implementations (cpu_adrIly, cpu_adrIdp, cpu_inc, cpu_dec, cpu_adc
 * and friends), like the helpers in dream_time.h.
 * ------------------------------------------------------------------------- */

/* 8-bit arithmetic keeps the accumulator's high byte, which the body carries in
 * `a` because the step macros publish the whole 16-bit register. */
static inline uint16_t alu8_adc(SnesState* ss, uint16_t a, uint8_t v) {
  const unsigned lo = a & 0xffu;
  const unsigned r = lo + v + (ss_c(ss) ? 1u : 0u);
  ss_set_v(ss, ((lo & 0x80u) == (v & 0x80u)) && ((v & 0x80u) != (r & 0x80u)));
  ss_set_c(ss, r > 0xff);
  a = (uint16_t) ((a & 0xff00u) | (r & 0xffu));
  ss_set_nz8(ss, (uint8_t) r);
  return a;
}

static inline uint16_t alu8_and(SnesState* ss, uint16_t a, uint8_t v) {
  a = (uint16_t) ((a & 0xff00u) | ((a & v) & 0xffu));
  ss_set_nz8(ss, (uint8_t) a);
  return a;
}

static inline uint16_t alu8_ora(SnesState* ss, uint16_t a, uint8_t v) {
  a = (uint16_t) ((a & 0xff00u) | ((a | v) & 0xffu));
  ss_set_nz8(ss, (uint8_t) a);
  return a;
}

static inline uint16_t alu8_eor(SnesState* ss, uint16_t a, uint8_t v) {
  a = (uint16_t) ((a & 0xff00u) | ((a ^ v) & 0xffu));
  ss_set_nz8(ss, (uint8_t) a);
  return a;
}

static inline uint16_t alu8_lsr(SnesState* ss, uint16_t a) {
  ss_set_c(ss, (a & 1u) != 0);
  a = (uint16_t) ((a & 0xff00u) | ((a >> 1) & 0x7fu));
  ss_set_nz8(ss, (uint8_t) a);
  return a;
}

static inline uint16_t alu8_asl(SnesState* ss, uint16_t a) {
  ss_set_c(ss, (a & 0x80u) != 0);
  a = (uint16_t) ((a & 0xff00u) | ((a << 1) & 0xffu));
  ss_set_nz8(ss, (uint8_t) a);
  return a;
}

/* 8-bit load into the accumulator, high byte kept. */
static inline uint16_t alu8_set(SnesState* ss, uint16_t a, uint8_t v) {
  a = (uint16_t) ((a & 0xff00u) | v);
  ss_set_nz8(ss, v);
  return a;
}

/* lda [dp],Y — the pointer half: three reads of the 24-bit pointer, no latch
 * between them, then Y added to the whole 24-bit value (cpu_adrIly). The data
 * half is t_read8/t_read16, as for any other operand. */
static inline uint32_t t_ily(SnesState* ss, uint16_t off, uint16_t y) {
  const uint16_t dp = ss_dp(ss);
  const uint8_t p0 = ss_bus_r8(ss, (uint16_t) (dp + off));
  const uint8_t p1 = ss_bus_r8(ss, (uint16_t) (dp + off + 1));
  const uint8_t p2 = ss_bus_r8(ss, (uint16_t) (dp + off + 2));
  const uint32_t ptr = (uint32_t) p0 | ((uint32_t) p1 << 8) | ((uint32_t) p2 << 16);
  return (ptr + y) & 0xffffff;
}

/* ora ($54) / sta ($54) — the pointer half: two reads, no latch, then the data
 * bank supplies the high byte (cpu_adrIdp). */
static inline uint32_t t_idp(SnesState* ss, uint16_t off) {
  const uint16_t dp = ss_dp(ss);
  const uint8_t lo = ss_bus_r8(ss, (uint16_t) (dp + off));
  const uint8_t hi = ss_bus_r8(ss, (uint16_t) (dp + off + 1));
  return (((uint32_t) ss_db(ss) << 16) + (uint16_t) (lo | (hi << 8))) & 0xffffff;
}

/* dec dp / inc dp with an 8-bit accumulator: read, internal cycle, latch, write. */
static inline uint8_t t_rmw8(SnesState* ss, uint32_t adr, int delta) {
  const uint8_t v = (uint8_t) (ss_bus_r8(ss, adr) + delta);
  ss_idle(ss);
  ss_check_int(ss);
  ss_bus_w8(ss, adr, v);
  ss_set_nz8(ss, v);
  return v;
}

/* inc dp with a 16-bit accumulator: the write-back is reversed, high byte first. */
static inline uint16_t t_rmw16(SnesState* ss, uint32_t adr, int delta) {
  const uint8_t lo = ss_bus_r8(ss, adr);
  const uint8_t hi = ss_bus_r8(ss, (adr + 1) & 0xffffff);
  const uint16_t v = (uint16_t) ((lo | (hi << 8)) + delta);
  ss_idle(ss);
  ss_bus_w8(ss, (adr + 1) & 0xffffff, (uint8_t) (v >> 8));
  ss_check_int(ss);
  ss_bus_w8(ss, adr, (uint8_t) v);
  ss_set_nz16(ss, v);
  return v;
}

/* pla, 16-bit: two internal cycles, then the word with a latch between bytes. */
static inline uint16_t t_pla16(SnesState* ss) {
  ss_idle(ss);
  ss_idle(ss);
  const uint8_t lo = ss_pull8(ss);
  ss_check_int(ss);
  const uint8_t hi = ss_pull8(ss);
  const uint16_t v = (uint16_t) (lo | (hi << 8));
  ss_set_nz16(ss, v);
  return v;
}

/* plb: two internal cycles, latch, one byte, N/Z from it as an 8-bit value. */
static inline void t_plb(SnesState* ss) {
  ss_idle(ss);
  ss_idle(ss);
  ss_check_int(ss);
  const uint8_t b = ss_pull8(ss);
  ss_set_db(ss, b);
  ss_set_nz8(ss, b);
}

/* pea imm: the operand word is the step's own fetch; this is the push half. */
static inline void t_pea(SnesState* ss, uint16_t v) {
  ss_push8(ss, (uint8_t) (v >> 8));
  ss_check_int(ss);
  ss_push8(ss, (uint8_t) v);
}

/* jsr abs, after the step that fetched the target: an internal cycle, then the
 * return address (the last byte of the jsr) with a latch between its bytes, and
 * then the callee itself.
 *
 * entity_build_oam_frame reaches the six emitters this way rather than by
 * calling their C bodies as functions, so that the emitter's *own* entry address
 * is entered: its hook fires if it is installed (which is what makes the C body
 * run, and what the gate counts as the routine being exercised), and the ROM's
 * copy runs if it is not, which is what --only entity_build_oam_frame needs.
 * The frame pushed is the routine's real return address either way, so a yield
 * inside the emitter can leave it running and its rts still lands here.
 *
 * Three ways out, because an emitter has three: its own rts, the "OAM is full"
 * exit that pulls the return address and jumps to this routine's tail, and a
 * yield. The middle one is spotted by the stack coming back without the pc being
 * the return address; the ROM's copy stops on the pla itself, one instruction
 * short of the jmp, so that jmp is modelled here. */
typedef enum { CALL_RETURNED, CALL_TAIL, CALL_YIELD } CallResult;

static CallResult t_jsr_emitter(SnesState* ss, uint8_t pb, uint16_t target) {
  const uint16_t sp0 = ss_sp(ss);
  ss_idle(ss);
  const uint16_t ret = (uint16_t) (ss_pc(ss) - 1);
  ss_push8(ss, (uint8_t) (ret >> 8));
  ss_check_int(ss);
  ss_push8(ss, (uint8_t) ret);
  ss_set_pc(ss, pb, target);

  if(ss_run_callee(ss, sp0)) return CALL_YIELD;
  if(ss_pc(ss) == (uint16_t) (ret + 1)) return CALL_RETURNED;
  if(ss_pc(ss) != 0xA6CD) {                 /* the emitter's jmp loc_C0A6CD */
    if(ss_yield_wanted(ss)) return CALL_YIELD;
    ss_fetch(ss, 3);
    ss_set_pc(ss, pb, 0xA6CD);
  }
  return CALL_TAIL;
}

/* ---------------------------------------------------------------------------
 * The one emit loop the ROM holds four copies of.
 *
 * A copy is described by its header address plus the three differences between
 * the copies: whether the row byte and the column byte are complemented, and
 * which of the two screen coordinates gets +8 between row block 0 and row
 * block 1. The complements are 2 and 3 bytes of instruction stream, so they
 * also shift every later address in the block; `dy` and `dxy` below are those
 * shifts, and every offset in this file is written against the plain copy.
 * ------------------------------------------------------------------------- */
typedef struct {
  uint16_t hdr;       /* the copy's first instruction, "lda oam_write_ptr" */
  bool eor_y;         /* the row byte is complemented (v-flip) */
  bool eor_x;         /* the column byte is complemented (h-flip) */
  bool adj_x, adj_y;  /* entity_screen_x / _y gain 8 between block 0 and 1 */
} EmitVariant;

static const EmitVariant kEmitPlain  = { 0xA791, false, false, false, false };
static const EmitVariant kEmitHFlip  = { 0xA930, false, true,  true,  false };
static const EmitVariant kEmitVFlip  = { 0xAAC9, true,  false, false, true  };
static const EmitVariant kEmitBoth   = { 0xAC5F, true,  true,  true,  true  };

static uint16_t emit_dy(const EmitVariant* v)  { return (uint16_t) (v->eor_y ? 2 : 0); }
static uint16_t emit_dxy(const EmitVariant* v) {
  return (uint16_t) (emit_dy(v) + (v->eor_x ? 3 : 0));
}
static uint16_t emit_adjlen(const EmitVariant* v) {
  if(!v->adj_x && !v->adj_y) return 0;
  return (uint16_t) (4 + 8 * ((v->adj_x ? 1 : 0) + (v->adj_y ? 1 : 0)));
}

/* Row block 0's loop — $A7B5 in the plain copy. Runs until the row counter goes
 * negative; the caller carries on at the block's exit address. */
static EmitResult emit_loop_first(SnesState* ss, const EmitVariant* v, uint16_t L) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint16_t dp = ss_dp(ss);
  const uint16_t dy = emit_dy(v), dxy = emit_dxy(v);

  for(;;) {
    ES(L + 0x00, 2);                        /* dec $1C */
    const bool last = (t_rmw8(ss, dp + oam_rows_0, -1) & 0x80) != 0;
    ES(L + 0x02, 1); t_branch(ss, last);    /* bmi (block 1's header) */
    if(last) return EMIT_RETURNED;

    ES(L + 0x04, 2);                        /* lda [sprite_frame_ptr2],Y */
    a = alu8_set(ss, a, t_read8(ss, t_ily(ss, sprite_frame_ptr2, y)));
    if(v->eor_y) { ES(L + 0x06, 2); a = alu8_eor(ss, a, 0xFF); }  /* eor #$FF */
    EREP(L + 0x06 + dy, 0x20);              /* rep #$20 */
    ES(L + 0x08 + dy, 3); a = alu_and16(ss, a, 0x00FF);           /* and #$00FF */
    ES(L + 0x0B + dy, 2);                   /* adc entity_screen_y (no clc here) */
    a = alu_adc16(ss, a, t_read16(ss, dp + entity_screen_y));
    ES(L + 0x0D + dy, 3); alu_cmp16(ss, a, 0x00F0);               /* cmp #$00F0 */
    const bool offscreen = ss_c(ss);
    ES(L + 0x10 + dy, 1); t_branch(ss, offscreen);                /* bcs */
    if(!offscreen) {
      ES(L + 0x12 + dy, 3); a = alu_sbc16(ss, a, 0x000F);         /* sbc #$000F */
      ES(L + 0x15 + dy, 2); t_index(ss);    /* sta $01,X — the Y and tile bytes */
      t_write16(ss, (uint16_t) (dp + 0x0001 + x), a);
      ES(L + 0x17 + dy, 2);                 /* lda [sprite_frame_ptr],Y */
      a = t_read16(ss, t_ily(ss, sprite_frame_ptr, y));
      ss_set_nz16(ss, a);
      if(v->eor_x) { ES(L + 0x19 + dy, 3); a = alu_eor16(ss, a, 0x00FF); }
      ES(L + 0x19 + dxy, 3); a = alu_and16(ss, a, 0x00FF);        /* and #$00FF */
      ESI(L + 0x1C + dxy); ss_set_c(ss, false);                   /* clc */
      ES(L + 0x1D + dxy, 2);                /* adc entity_screen_x */
      a = alu_adc16(ss, a, t_read16(ss, dp + entity_screen_x));
      ES(L + 0x1F + dxy, 3); alu_cmp16(ss, a, 0x0100);            /* cmp #$0100 */
      ESEP(L + 0x22 + dxy, 0x20);           /* sep #$20 */
      ES(L + 0x24 + dxy, 2); t_index(ss);   /* sta $00,X — the X byte */
      t_write8(ss, (uint16_t) (dp + nmi_handler_ptr + x), (uint8_t) a);
      ES(L + 0x26 + dxy, 2);                /* lda $24 */
      a = alu8_set(ss, a, t_read8(ss, dp + oam_size_bits));
      const bool wide = ss_c(ss);           /* the cmp #$0100 above: bit 8 of X */
      ES(L + 0x28 + dxy, 1); t_branch(ss, wide);                  /* bcs */
      if(!wide) { ES(L + 0x2A + dxy, 2); a = alu8_and(ss, a, 0xAA); }
      ES(L + 0x2C + dxy, 2);                /* ora (oam_entry_ptr) */
      a = alu8_ora(ss, a, t_read8(ss, t_idp(ss, oam_entry_ptr)));
      ES(L + 0x2E + dxy, 2);                /* sta (oam_entry_ptr) */
      t_write8(ss, t_idp(ss, oam_entry_ptr), (uint8_t) a);
      ES(L + 0x30 + dxy, 2);                /* lda $24 */
      a = alu8_set(ss, a, t_read8(ss, dp + oam_size_bits));
      const bool rolled = ss_n(ss);         /* the pattern has walked off the byte */
      ES(L + 0x32 + dxy, 1); t_branch(ss, !rolled);               /* bpl */
      if(rolled) {
        ES(L + 0x34 + dxy, 2); t_rmw8(ss, dp + oam_entry_ptr, +1); /* inc $54 */
        ES(L + 0x36 + dxy, 2); a = alu8_set(ss, a, 0x03);          /* lda #$03 */
        ESI(L + 0x38 + dxy); ss_set_c(ss, false);                  /* clc */
        ES(L + 0x39 + dxy, 1); t_branch(ss, true);                 /* bra */
      } else {
        ESI(L + 0x3B + dxy); a = alu8_asl(ss, a);                  /* asl A */
        ESI(L + 0x3C + dxy); a = alu8_asl(ss, a);                  /* asl A */
      }
      ES(L + 0x3D + dxy, 2); t_write8(ss, dp + oam_size_bits, (uint8_t) a);
      EREP(L + 0x3F + dxy, 0x20);           /* rep #$20 */
      ES(L + 0x41 + dxy, 2);                /* lda $1A */
      a = t_read16(ss, dp + oam_flag_cursor);
      ss_set_nz16(ss, a);
      ES(L + 0x43 + dxy, 2); t_index(ss);   /* sta $02,X — tile and attribute */
      t_write16(ss, (uint16_t) (dp + dma_pending_mask + x), a);
      ESI(L + 0x45 + dxy); x = (uint16_t) (x + 1); ss_set_nz16(ss, x);
      ESI(L + 0x46 + dxy); x = (uint16_t) (x + 1); ss_set_nz16(ss, x);
      ESI(L + 0x47 + dxy); x = (uint16_t) (x + 1); ss_set_nz16(ss, x);
      ESI(L + 0x48 + dxy); x = (uint16_t) (x + 1); ss_set_nz16(ss, x);
    }
    /* the "next sprite" tail, reached either way */
    ESI(L + 0x49 + dxy); ss_set_c(ss, false);                     /* clc */
    ES(L + 0x4A + dxy, 2);                  /* lda $1A */
    a = t_read16(ss, dp + oam_flag_cursor);
    ss_set_nz16(ss, a);
    ESI(L + 0x4C + dxy); a = alu_inc16(ss, a);                    /* inc A */
    ESI(L + 0x4D + dxy); a = alu_inc16(ss, a);                    /* inc A */
    ES(L + 0x4E + dxy, 3); alu_bit_imm16(ss, a, 0x0010);          /* bit #$0010 */
    const bool nowrap = ss_z(ss);
    ES(L + 0x51 + dxy, 1); t_branch(ss, nowrap);                  /* beq */
    if(!nowrap) { ES(L + 0x53 + dxy, 3); a = alu_adc16(ss, a, 0x0010); }
    ES(L + 0x56 + dxy, 2); t_write16(ss, dp + oam_flag_cursor, a);
    ESEP(L + 0x58 + dxy, 0x20);             /* sep #$20 */
    ESI(L + 0x5A + dxy); y = (uint16_t) (y + 1); ss_set_nz16(ss, y);
    ESI(L + 0x5B + dxy); y = (uint16_t) (y + 1); ss_set_nz16(ss, y);
    ES(L + 0x5C + dxy, 1); t_branch(ss, true);                    /* bra */
  }
}

/* Row blocks 1 and 2 share a second loop shape — $A82D and $A89C in the plain
 * copy. It differs from the first only in what it does with the column byte:
 * instead of folding the "x >= 256" bit into the rotating pattern in $24, it
 * rebuilds oam_entry_ptr from X and ors a bit out of the 128-byte table at
 * data_C0A6D7. `cnt` is the row counter this block walks down. */
static EmitResult emit_loop_rest(SnesState* ss, const EmitVariant* v,
                                 uint16_t L, uint16_t cnt) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint16_t dp = ss_dp(ss);
  const uint16_t dy = emit_dy(v), dxy = emit_dxy(v);

  for(;;) {
    ES(L + 0x00, 2);                        /* dec $1D / dec $1F */
    const bool last = (t_rmw8(ss, dp + cnt, -1) & 0x80) != 0;
    ES(L + 0x02, 1); t_branch(ss, last);    /* bmi (the next block's header) */
    if(last) return EMIT_RETURNED;

    ES(L + 0x04, 2);                        /* lda [sprite_frame_ptr2],Y */
    a = alu8_set(ss, a, t_read8(ss, t_ily(ss, sprite_frame_ptr2, y)));
    if(v->eor_y) { ES(L + 0x06, 2); a = alu8_eor(ss, a, 0xFF); }  /* eor #$FF */
    EREP(L + 0x06 + dy, 0x20);              /* rep #$20 */
    ES(L + 0x08 + dy, 3); a = alu_and16(ss, a, 0x00FF);           /* and #$00FF */
    ESI(L + 0x0B + dy); ss_set_c(ss, false);                      /* clc */
    ES(L + 0x0C + dy, 2);                   /* adc entity_screen_y */
    a = alu_adc16(ss, a, t_read16(ss, dp + entity_screen_y));
    ES(L + 0x0E + dy, 3); alu_cmp16(ss, a, 0x00F0);               /* cmp #$00F0 */
    const bool offscreen = ss_c(ss);
    ES(L + 0x11 + dy, 1); t_branch(ss, offscreen);                /* bcs */
    if(!offscreen) {
      ES(L + 0x13 + dy, 3); a = alu_sbc16(ss, a, 0x000F);         /* sbc #$000F */
      ES(L + 0x16 + dy, 2); t_index(ss);    /* sta $01,X */
      t_write16(ss, (uint16_t) (dp + 0x0001 + x), a);
      ES(L + 0x18 + dy, 2);                 /* lda [sprite_frame_ptr],Y */
      a = t_read16(ss, t_ily(ss, sprite_frame_ptr, y));
      ss_set_nz16(ss, a);
      if(v->eor_x) { ES(L + 0x1A + dy, 3); a = alu_eor16(ss, a, 0x00FF); }
      ES(L + 0x1A + dxy, 3); a = alu_and16(ss, a, 0x00FF);        /* and #$00FF */
      ESI(L + 0x1D + dxy); ss_set_c(ss, false);                   /* clc */
      ES(L + 0x1E + dxy, 2);                /* adc entity_screen_x */
      a = alu_adc16(ss, a, t_read16(ss, dp + entity_screen_x));
      ES(L + 0x20 + dxy, 3); alu_bit_imm16(ss, a, 0x0100);        /* bit #$0100 */
      ESEP(L + 0x23 + dxy, 0x20);           /* sep #$20 */
      ES(L + 0x25 + dxy, 2); t_index(ss);   /* sta $00,X */
      t_write8(ss, (uint16_t) (dp + nmi_handler_ptr + x), (uint8_t) a);
      const bool narrow = ss_z(ss);         /* bit #$0100: X fits in 8 bits */
      ES(L + 0x27 + dxy, 1); t_branch(ss, narrow);                /* beq */
      if(!narrow) {
        ES(L + 0x29 + dxy, 2);              /* stx $52 (16-bit: x is 16-bit) */
        t_write16(ss, dp + depth_sort_key, x);
        EREP(L + 0x2B + dxy, 0x20);         /* rep #$20 */
        ESI(L + 0x2D + dxy); a = x; ss_set_nz16(ss, a);           /* txa */
        ES(L + 0x2E + dxy, 3); a = alu_and16(ss, a, 0x01FC);      /* and #$01FC */
        ESI(L + 0x31 + dxy); a = alu_lsr16(ss, a);                /* lsr A */
        ESI(L + 0x32 + dxy); a = alu_lsr16(ss, a);                /* lsr A */
        ESI(L + 0x33 + dxy); x = a; ss_set_nz16(ss, x);           /* tax */
        ESI(L + 0x34 + dxy); a = alu_lsr16(ss, a);                /* lsr A */
        ESI(L + 0x35 + dxy); a = alu_lsr16(ss, a);                /* lsr A */
        ESEP(L + 0x36 + dxy, 0x20);         /* sep #$20 */
        ES(L + 0x38 + dxy, 2); t_write8(ss, dp + oam_entry_ptr, (uint8_t) a);
        ES(L + 0x3A + dxy, 3); t_index(ss); /* lda data_C0A6D7,X */
        a = alu8_set(ss, a, t_read8(ss, ss_abs(ss, (uint16_t) (oam_x9_pattern + x))));
        ES(L + 0x3D + dxy, 2);              /* ora (oam_entry_ptr) */
        a = alu8_ora(ss, a, t_read8(ss, t_idp(ss, oam_entry_ptr)));
        ES(L + 0x3F + dxy, 2);              /* sta (oam_entry_ptr) */
        t_write8(ss, t_idp(ss, oam_entry_ptr), (uint8_t) a);
        ES(L + 0x41 + dxy, 2);              /* ldx $52 (16-bit) */
        x = t_read16(ss, dp + depth_sort_key);
        ss_set_nz16(ss, x);
      }
      EREP(L + 0x43 + dxy, 0x20);           /* rep #$20 */
      ES(L + 0x45 + dxy, 2);                /* lda $1A */
      a = t_read16(ss, dp + oam_flag_cursor);
      ss_set_nz16(ss, a);
      ES(L + 0x47 + dxy, 2); t_index(ss);   /* sta $02,X */
      t_write16(ss, (uint16_t) (dp + dma_pending_mask + x), a);
      ESI(L + 0x49 + dxy); x = (uint16_t) (x + 1); ss_set_nz16(ss, x);
      ESI(L + 0x4A + dxy); x = (uint16_t) (x + 1); ss_set_nz16(ss, x);
      ESI(L + 0x4B + dxy); x = (uint16_t) (x + 1); ss_set_nz16(ss, x);
      ESI(L + 0x4C + dxy); x = (uint16_t) (x + 1); ss_set_nz16(ss, x);
    }
    ES(L + 0x4D + dxy, 2); t_rmw16(ss, dp + oam_flag_cursor, +1); /* inc $1A */
    ESEP(L + 0x4F + dxy, 0x20);             /* sep #$20 */
    ESI(L + 0x51 + dxy); y = (uint16_t) (y + 1); ss_set_nz16(ss, y);
    ESI(L + 0x52 + dxy); y = (uint16_t) (y + 1); ss_set_nz16(ss, y);
    ES(L + 0x53 + dxy, 1); t_branch(ss, true);                    /* bra */
  }
}

/* The "OAM is full" exit both header forms take: the jsr's return address is
 * pulled and the routine continues at entity_build_oam_frame's own tail. */
static EmitResult emit_full_exit(SnesState* ss, uint16_t pla_addr) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  ES(pla_addr, 1); a = t_pla16(ss);         /* pla */
  ES(pla_addr + 1, 3);                      /* jmp loc_C0A6CD */
  ss_set_pc(ss, pb, 0xA6CD);
  return EMIT_TAIL;
}

/* One copy of the emit loop, all three row blocks. */
static EmitResult emit_run(SnesState* ss, const EmitVariant* v) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint16_t dp = ss_dp(ss);
  const uint16_t dxy = emit_dxy(v);

  const uint16_t H0 = v->hdr;                                  /* $A791 */
  const uint16_t L0 = (uint16_t) (H0 + 0x24);                  /* $A7B5 */
  const uint16_t H1 = (uint16_t) (L0 + 0x5E + dxy);            /* $A813 */
  const uint16_t L1 = (uint16_t) (H1 + 0x1A + emit_adjlen(v)); /* $A82D */
  const uint16_t H2 = (uint16_t) (L1 + 0x55 + dxy);            /* $A882 */
  const uint16_t L2 = (uint16_t) (H2 + 0x1A);                  /* $A89C */
  const uint16_t X2 = (uint16_t) (L2 + 0x55 + dxy);            /* $A8F1 */

  /* ---- row block 0 header — $A791 ------------------------------------- */
  ES(H0 + 0x00, 2);                         /* lda oam_write_ptr */
  a = t_read16(ss, dp + oam_write_ptr);
  ss_set_nz16(ss, a);
  ESI(H0 + 0x02); a = alu_lsr16(ss, a);     /* lsr A */
  ESI(H0 + 0x03); a = alu_lsr16(ss, a);     /* lsr A */
  ESEP(H0 + 0x04, 0x20);                    /* sep #$20 */
  ESI(H0 + 0x06); x = a; ss_set_nz16(ss, x);/* tax (index is 16-bit: the whole A) */
  ES(H0 + 0x07, 2);                         /* adc $1C — carry from the lsr above */
  a = alu8_adc(ss, a, t_read8(ss, dp + oam_rows_0));
  const bool room0 = ss_n(ss);
  ES(H0 + 0x09, 1); t_branch(ss, room0);    /* bmi */
  if(!room0) {
    EREP(H0 + 0x0B, 0x20);                  /* rep #$20 */
    return emit_full_exit(ss, (uint16_t) (H0 + 0x0D));
  }
  ESI(H0 + 0x11); a = alu8_set(ss, a, (uint8_t) x);             /* txa */
  ESI(H0 + 0x12); a = alu8_lsr(ss, a);      /* lsr A */
  ESI(H0 + 0x13); a = alu8_lsr(ss, a);      /* lsr A */
  ES(H0 + 0x14, 2); a = alu8_and(ss, a, 0x1F);                  /* and #$1F */
  ES(H0 + 0x16, 2); t_write8(ss, dp + oam_entry_ptr, (uint8_t) a);
  ESI(H0 + 0x18); a = alu8_set(ss, a, (uint8_t) x);             /* txa */
  ES(H0 + 0x19, 2); a = alu8_and(ss, a, 0x03);                  /* and #$03 */
  ESI(H0 + 0x1B); x = a; ss_set_nz16(ss, x);/* tax */
  ES(H0 + 0x1C, 3); t_index(ss);            /* lda data_C0A6D3,X */
  a = alu8_set(ss, a, t_read8(ss, ss_abs(ss, (uint16_t) (oam_size_pattern + x))));
  ES(H0 + 0x1F, 2); t_write8(ss, dp + oam_size_bits, (uint8_t) a);
  ES(H0 + 0x21, 2);                         /* ldx oam_write_ptr (16-bit) */
  x = t_read16(ss, dp + oam_write_ptr);
  ss_set_nz16(ss, x);
  ESI(H0 + 0x23); ss_set_c(ss, false);      /* clc */

  ss_set_a(ss, a); ss_set_x(ss, x); ss_set_y(ss, y);
  {
    const EmitResult r = emit_loop_first(ss, v, L0);
    if(r != EMIT_RETURNED) return r;
  }
  a = ss_a(ss); x = ss_x(ss); y = ss_y(ss);

  /* ---- row block 1 header — $A813 ------------------------------------- */
  EREP(H1 + 0x00, 0x20);                    /* rep #$20 */
  ESI(H1 + 0x02); a = x; ss_set_nz16(ss, a);/* txa */
  ESI(H1 + 0x03); a = alu_lsr16(ss, a);     /* lsr A */
  ESI(H1 + 0x04); a = alu_lsr16(ss, a);     /* lsr A */
  ESEP(H1 + 0x05, 0x20);                    /* sep #$20 */
  ES(H1 + 0x07, 2);                         /* adc $1D */
  a = alu8_adc(ss, a, t_read8(ss, dp + oam_rows_1));
  const bool room1 = ss_n(ss);
  ES(H1 + 0x09, 1); t_branch(ss, room1);    /* bmi */
  if(!room1) {
    EREP(H1 + 0x0B, 0x20);                  /* rep #$20 */
    ES(H1 + 0x0D, 2);                       /* stx oam_write_ptr */
    t_write16(ss, dp + oam_write_ptr, x);
    return emit_full_exit(ss, (uint16_t) (H1 + 0x0F));
  }
  ES(H1 + 0x13, 2);                         /* lda $1E */
  a = alu8_set(ss, a, t_read8(ss, dp + oam_row_1_base));
  ESI(H1 + 0x15); ss_set_c(ss, false);      /* clc */
  ES(H1 + 0x16, 2);                         /* adc $18 */
  a = alu8_adc(ss, a, t_read8(ss, dp + oam_flag_word));
  ES(H1 + 0x18, 2); t_write8(ss, dp + oam_flag_cursor, (uint8_t) a);
  if(v->adj_x || v->adj_y) {
    uint16_t k = (uint16_t) (H1 + 0x1A);
    EREP(k, 0x20); k = (uint16_t) (k + 2);  /* rep #$20 */
    if(v->adj_x) {
      ES(k, 2); a = t_read16(ss, dp + entity_screen_x); ss_set_nz16(ss, a);
      ESI(k + 2); ss_set_c(ss, false);
      ES(k + 3, 3); a = alu_adc16(ss, a, 0x0008);
      ES(k + 6, 2); t_write16(ss, dp + entity_screen_x, a);
      k = (uint16_t) (k + 8);
    }
    if(v->adj_y) {
      ES(k, 2); a = t_read16(ss, dp + entity_screen_y); ss_set_nz16(ss, a);
      ESI(k + 2); ss_set_c(ss, false);
      ES(k + 3, 3); a = alu_adc16(ss, a, 0x0008);
      ES(k + 6, 2); t_write16(ss, dp + entity_screen_y, a);
      k = (uint16_t) (k + 8);
    }
    ESEP(k, 0x20);                          /* sep #$20 */
  }

  ss_set_a(ss, a); ss_set_x(ss, x); ss_set_y(ss, y);
  {
    const EmitResult r = emit_loop_rest(ss, v, L1, oam_rows_1);
    if(r != EMIT_RETURNED) return r;
  }
  a = ss_a(ss); x = ss_x(ss); y = ss_y(ss);

  /* ---- row block 2 header — $A882 ------------------------------------- */
  EREP(H2 + 0x00, 0x20);                    /* rep #$20 */
  ESI(H2 + 0x02); a = x; ss_set_nz16(ss, a);/* txa */
  ESI(H2 + 0x03); a = alu_lsr16(ss, a);     /* lsr A */
  ESI(H2 + 0x04); a = alu_lsr16(ss, a);     /* lsr A */
  ESEP(H2 + 0x05, 0x20);                    /* sep #$20 */
  ES(H2 + 0x07, 2);                         /* adc $1F */
  a = alu8_adc(ss, a, t_read8(ss, dp + oam_rows_2));
  const bool room2 = ss_n(ss);
  ES(H2 + 0x09, 1); t_branch(ss, room2);    /* bmi */
  if(!room2) {
    EREP(H2 + 0x0B, 0x20);                  /* rep #$20 */
    ES(H2 + 0x0D, 2);                       /* stx oam_write_ptr */
    t_write16(ss, dp + oam_write_ptr, x);
    return emit_full_exit(ss, (uint16_t) (H2 + 0x0F));
  }
  ES(H2 + 0x13, 2);                         /* lda $20 */
  a = alu8_set(ss, a, t_read8(ss, dp + oam_row_2_base));
  ESI(H2 + 0x15); ss_set_c(ss, false);      /* clc */
  ES(H2 + 0x16, 2);                         /* adc $18 */
  a = alu8_adc(ss, a, t_read8(ss, dp + oam_flag_word));
  ES(H2 + 0x18, 2); t_write8(ss, dp + oam_flag_cursor, (uint8_t) a);

  ss_set_a(ss, a); ss_set_x(ss, x); ss_set_y(ss, y);
  {
    const EmitResult r = emit_loop_rest(ss, v, L2, oam_rows_2);
    if(r != EMIT_RETURNED) return r;
  }
  a = ss_a(ss); x = ss_x(ss); y = ss_y(ss);

  /* ---- the emitter's own return — $A8F1 ------------------------------- */
  EREP(X2 + 0x00, 0x20);                    /* rep #$20 */
  ES(X2 + 0x02, 2); t_write16(ss, dp + oam_write_ptr, x);       /* stx oam_write_ptr */
  ss_set_a(ss, a); ss_set_x(ss, x); ss_set_y(ss, y);
  ES(X2 + 0x04, 1);                         /* rts */
  ss_rts(ss);
  return EMIT_RETURNED;
}

/* The four frame-header words an emitter reads out of the frame data before the
 * loop: three for the 1-row entries (which then jmp into the 2-row copy's
 * header) and four for every other entry (which fall into it). */
static EmitResult emit_prologue(SnesState* ss, uint16_t P, bool four,
                                const EmitVariant* v) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint16_t dp = ss_dp(ss);

  ES(P + 0x00, 3); y = 0x0000; ss_set_nz16(ss, y);              /* ldy #$0000 */
  ES(P + 0x03, 2);                                              /* lda [$26],Y */
  a = t_read16(ss, t_ily(ss, sprite_frame_ptr, y));
  ss_set_nz16(ss, a);
  ES(P + 0x05, 2); t_write16(ss, dp + oam_rows_0, a);           /* sta $1C */
  ES(P + 0x07, 3); y = 0x0002; ss_set_nz16(ss, y);              /* ldy #$0002 */
  ES(P + 0x0A, 2);
  a = t_read16(ss, t_ily(ss, sprite_frame_ptr, y));
  ss_set_nz16(ss, a);
  ES(P + 0x0C, 2); t_write16(ss, dp + oam_row_1_base, a);       /* sta $1E */
  ES(P + 0x0E, 3); y = 0x0004; ss_set_nz16(ss, y);              /* ldy #$0004 */
  ES(P + 0x11, 2);
  a = t_read16(ss, t_ily(ss, sprite_frame_ptr, y));
  ss_set_nz16(ss, a);
  ES(P + 0x13, 2); t_write16(ss, dp + oam_row_2_base, a);       /* sta $20 */
  if(!four) {
    ES(P + 0x15, 3); y = 0x0005; ss_set_nz16(ss, y);            /* ldy #$0005 */
    ES(P + 0x18, 3);                                            /* jmp <header> */
    ss_set_a(ss, a); ss_set_x(ss, x); ss_set_y(ss, y);
    ss_set_pc(ss, pb, v->hdr);
    return emit_run(ss, v);
  }
  ES(P + 0x15, 3); y = 0x0006; ss_set_nz16(ss, y);              /* ldy #$0006 */
  ES(P + 0x18, 2);
  a = t_read16(ss, t_ily(ss, sprite_frame_ptr, y));
  ss_set_nz16(ss, a);
  ES(P + 0x1A, 2); t_write16(ss, dp + oam_row_3_base, a);       /* sta $22 */
  ES(P + 0x1C, 3); y = 0x0008; ss_set_nz16(ss, y);              /* ldy #$0008 */
  ss_set_a(ss, a); ss_set_x(ss, x); ss_set_y(ss, y);
  return emit_run(ss, v);                   /* falls through into the header */
}

/* ---------------------------------------------------------------------------
 * The six emitters. Each is the prologue above plus the copy of the loop its
 * entry belongs to; the void wrappers are what the registry installs, and the
 * EmitResult forms are what entity_build_oam_frame calls.
 * ------------------------------------------------------------------------- */
static EmitResult emit_1row(SnesState* ss)      { return emit_prologue(ss, 0xA757, false, &kEmitPlain); }
static EmitResult emit_2row(SnesState* ss)      { return emit_prologue(ss, 0xA772, true,  &kEmitPlain); }
static EmitResult emit_1row_flip(SnesState* ss) { return emit_prologue(ss, 0xA8F6, false, &kEmitHFlip); }
static EmitResult emit_2row_flip(SnesState* ss) { return emit_prologue(ss, 0xA911, true,  &kEmitHFlip); }
static EmitResult emit_3row(SnesState* ss)      { return emit_prologue(ss, 0xAAAA, true,  &kEmitVFlip); }
static EmitResult emit_3row_flip(SnesState* ss) { return emit_prologue(ss, 0xAC40, true,  &kEmitBoth);  }

void oam_emit_frame_1row(SnesState* ss)      { (void) emit_1row(ss); }
void oam_emit_frame_2row(SnesState* ss)      { (void) emit_2row(ss); }
void oam_emit_frame_1row_flip(SnesState* ss) { (void) emit_1row_flip(ss); }
void oam_emit_frame_2row_flip(SnesState* ss) { (void) emit_2row_flip(ss); }
void oam_emit_frame_3row(SnesState* ss)      { (void) emit_3row(ss); }
void oam_emit_frame_3row_flip(SnesState* ss) { (void) emit_3row_flip(ss); }

/* ---------------------------------------------------------------------------
 * entity_build_oam_frame — $C0:A538
 *
 * Called once per main-loop iteration with jsl from $80:823F, so the program
 * bank is $80 and the routine returns with rtl. It walks all sixteen entries of
 * entity_render_order (entity_render_index counts 0..$1E), and for each entity
 * whose entity_substate and entity_frame_id are both non-zero:
 *
 *   - resolves the frame through data_C40000/data_C40002: a 16-bit pointer plus
 *     a word whose low byte is the data bank and whose high byte is a Y bias.
 *     The bias goes into entity_depth_key, which entity_sort_draw_order keys on;
 *   - derives entity_screen_x/_y from entity_x, entity_y and entity_z_dead minus
 *     the camera, and culls the entity if either axis leaves a 304x352 window;
 *   - picks one of the six emitters from the two flip bits of entity_flags and
 *     from entity_frame_id being below or above 4, then queues the frame's VRAM
 *     tile upload into entity_tile_job if the frame id changed since last time.
 *
 * The two "OAM is full" exits inside an emitter throw away the jsr's return
 * address and land on loc_C0A6CD, this routine's own tail; the C emitters report
 * that as EMIT_TAIL, and a yield inside one as EMIT_YIELD, in which case the ROM
 * finishes the emitter and its rts lands on the real return address the jsr
 * modelled below pushed.
 *
 * Exit: DB = $80 (the pea $8080 / plb / plb at both tails), A = $8080.
 * ------------------------------------------------------------------------- */

/* pea $8080 ; plb ; plb ; rtl — the identical tails at $A54D and $A6CD. */
static void oam_frame_exit(SnesState* ss, uint16_t base) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  S(base + 0, 3); t_pea(ss, 0x8080);        /* pea $8080 */
  S(base + 3, 1); t_plb(ss);                /* plb */
  S(base + 4, 1); t_plb(ss);                /* plb */
  S(base + 5, 1);                           /* rtl */
  ss_rtl(ss);
}

void entity_build_oam_frame(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint16_t dp = ss_dp(ss);

  S(0xA538, 3); a = 0x0400; ss_set_nz16(ss, a);   /* C0A538 lda #$0400 */
  S(0xA53B, 2); t_write16(ss, dp + oam_entry_ptr, a);  /* C0A53B sta oam_entry_ptr */

loc_A53D: ;
  S(0xA53D, 2);                             /* C0A53D lda oam_write_ptr */
  a = t_read16(ss, dp + oam_write_ptr);
  ss_set_nz16(ss, a);
  S(0xA53F, 3); alu_cmp16(ss, a, 0x0400);   /* C0A53F cmp #$0400 */
  {
    const bool room = !ss_z(ss);
    S(0xA542, 1); t_branch(ss, room);       /* C0A542 bne loc_C0A553 */
    if(!room) {
      /* the buffer is already full: force blank at brightness 7 and give up */
      SEP(0xA544, 0x20);                    /* C0A544 sep #$20 */
      S(0xA546, 2); a = alu8_set(ss, a, 0x07);  /* C0A546 lda #$07 */
      S(0xA548, 3);                         /* C0A548 sta INIDISP */
      t_write8(ss, ss_abs(ss, INIDISP), (uint8_t) a);
      REP(0xA54B, 0x20);                    /* C0A54B rep #$20 */
      ss_set_a(ss, a); ss_set_x(ss, x); ss_set_y(ss, y);
      oam_frame_exit(ss, 0xA54D);
      return;
    }
  }

  /* loc_C0A553 — DB = the program bank for the rest of the iteration */
  S(0xA553, 1);                             /* C0A553 phk */
  ss_idle(ss); ss_check_int(ss); ss_push8(ss, pb);
  S(0xA554, 1); t_plb(ss);                  /* C0A554 plb */
  S(0xA555, 2);                             /* C0A555 ldy entity_render_index */
  y = t_read16(ss, dp + entity_render_index);
  ss_set_nz16(ss, y);
  S(0xA557, 3); t_index(ss);                /* C0A557 lda entity_render_order,Y */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_render_order + y)));
  ss_set_nz16(ss, a);
  SI(0xA55A); y = a; ss_set_nz16(ss, y);    /* C0A55A tay */
  S(0xA55B, 3); t_index(ss);                /* C0A55B lda entity_substate,Y */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_substate + y)));
  ss_set_nz16(ss, a);
  S(0xA55E, 1); t_branch(ss, a != 0);       /* C0A55E bne loc_C0A563 */
  if(a == 0) goto loc_A560;

  /* loc_C0A563 */
  S(0xA563, 3); t_index(ss);                /* C0A563 ldx entity_frame_id,Y */
  x = t_read16(ss, ss_abs(ss, (uint16_t) (entity_frame_id + y)));
  ss_set_nz16(ss, x);
  SI(0xA566); a = x; ss_set_nz16(ss, a);    /* C0A566 txa */
  S(0xA567, 3); t_index(ss);                /* C0A567 sta entity_frame_shown,Y */
  t_write16(ss, ss_abs(ss, (uint16_t) (entity_frame_shown + y)), a);
  S(0xA56A, 1); t_branch(ss, a == 0);       /* C0A56A beq loc_C0A560 */
  if(a == 0) goto loc_A560;

  S(0xA56C, 4);                             /* C0A56C lda data_C40000,X */
  a = t_read16(ss, (sprite_frame_table + x) & 0xffffff);
  ss_set_nz16(ss, a);
  S(0xA570, 2); t_write16(ss, dp + sprite_frame_ptr, a);   /* C0A570 sta sprite_frame_ptr */
  SI(0xA572); a = alu_inc16(ss, a);         /* C0A572 inc A */
  S(0xA573, 2); t_write16(ss, dp + sprite_frame_ptr2, a);  /* C0A573 sta sprite_frame_ptr2 */
  S(0xA575, 4);                             /* C0A575 lda data_C40002,X */
  a = t_read16(ss, (sprite_frame_table + 2 + x) & 0xffffff);
  ss_set_nz16(ss, a);
  S(0xA579, 2); t_write16(ss, dp + sprite_frame_bank, a);  /* C0A579 sta sprite_frame_bank */
  S(0xA57B, 2); t_write16(ss, dp + sprite_frame_bank2, a); /* C0A57B sta sprite_frame_bank2 */
  S(0xA57D, 1);                             /* C0A57D xba — the frame's Y bias */
  {
    /* xba is three cycles, not two: the opcode fetch and two internal cycles
     * with the interrupt latch between them, so it is not an SI(). */
    const uint8_t hi = (uint8_t) (a >> 8);
    a = (uint16_t) ((a << 8) | hi);
    ss_set_nz8(ss, hi);
    ss_idle(ss);
    ss_check_int(ss);
    ss_idle(ss);
  }
  S(0xA57E, 3); a = alu_and16(ss, a, 0x00FF);  /* C0A57E and #$00FF */
  SI(0xA581); ss_set_c(ss, false);          /* C0A581 clc */
  S(0xA582, 3); t_index(ss);                /* C0A582 adc entity_y,Y */
  a = alu_adc16(ss, a, t_read16(ss, ss_abs(ss, (uint16_t) (entity_y + y))));
  S(0xA585, 3); t_index(ss);                /* C0A585 sta entity_depth_key,Y */
  t_write16(ss, ss_abs(ss, (uint16_t) (entity_depth_key + y)), a);
  S(0xA588, 3); t_index(ss);                /* C0A588 lda entity_z_dead,Y */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_z_dead + y)));
  ss_set_nz16(ss, a);
  SI(0xA58B); ss_set_c(ss, false);          /* C0A58B clc */
  S(0xA58C, 3); t_index(ss);                /* C0A58C adc entity_y,Y */
  a = alu_adc16(ss, a, t_read16(ss, ss_abs(ss, (uint16_t) (entity_y + y))));
  SI(0xA58F); ss_set_c(ss, true);           /* C0A58F sec */
  S(0xA590, 3); a = alu_sbc16(ss, a, 0x0100);  /* C0A590 sbc #$0100 */
  SI(0xA593); ss_set_c(ss, true);           /* C0A593 sec */
  S(0xA594, 2);                             /* C0A594 sbc camera_y */
  a = alu_sbc16(ss, a, t_read16(ss, dp + camera_y));
  SI(0xA596); ss_set_c(ss, true);           /* C0A596 sec */
  S(0xA597, 2);                             /* C0A597 sbc $76 */
  a = alu_sbc16(ss, a, t_read16(ss, dp + bg2_scroll_bias));
  SI(0xA599); ss_set_c(ss, false);          /* C0A599 clc */
  S(0xA59A, 2);                             /* C0A59A adc $92 */
  a = alu_adc16(ss, a, t_read16(ss, dp + screen_y_bias_92));
  S(0xA59C, 2); t_write16(ss, dp + entity_screen_y, a);  /* C0A59C sta entity_screen_y */
  S(0xA59E, 3); a = alu_adc16(ss, a, 0x0090);  /* C0A59E adc #$0090 (plus that carry) */
  S(0xA5A1, 3); alu_cmp16(ss, a, 0x0130);   /* C0A5A1 cmp #$0130 */
  {
    const bool culled = ss_c(ss);
    S(0xA5A4, 1); t_branch(ss, culled);     /* C0A5A4 bcs loc_C0A560 */
    if(culled) goto loc_A560;
  }
  S(0xA5A6, 3); t_index(ss);                /* C0A5A6 lda entity_x,Y */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_x + y)));
  ss_set_nz16(ss, a);
  S(0xA5A9, 2);                             /* C0A5A9 sbc camera_x (C clear: see cmp) */
  a = alu_sbc16(ss, a, t_read16(ss, dp + camera_x));
  S(0xA5AB, 2); t_write16(ss, dp + entity_screen_x, a);  /* C0A5AB sta entity_screen_x */
  SI(0xA5AD); ss_set_c(ss, false);          /* C0A5AD clc */
  S(0xA5AE, 3); a = alu_adc16(ss, a, 0x0030);  /* C0A5AE adc #$0030 */
  S(0xA5B1, 3); alu_cmp16(ss, a, 0x0160);   /* C0A5B1 cmp #$0160 */
  {
    const bool onscreen = !ss_c(ss);
    S(0xA5B4, 1); t_branch(ss, onscreen);   /* C0A5B4 bcc loc_C0A5B9 */
    if(!onscreen) { S(0xA5B6, 3); goto loc_A6BF; }  /* C0A5B6 jmp loc_C0A6BF */
  }

  /* loc_C0A5B9 — pick the emitter from the two flip bits */
  S(0xA5B9, 3); t_index(ss);                /* C0A5B9 lda entity_flags,Y */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_flags + y)));
  ss_set_nz16(ss, a);
  S(0xA5BC, 2); t_write16(ss, dp + oam_flag_word, a);    /* C0A5BC sta $18 */
  S(0xA5BE, 2); t_write16(ss, dp + oam_flag_cursor, a);  /* C0A5BE sta $1A */
  S(0xA5C0, 3); alu_bit_imm16(ss, a, 0x8000);  /* C0A5C0 bit #$8000 */
  {
    const bool vflip = !ss_z(ss);
    S(0xA5C3, 1); t_branch(ss, vflip);      /* C0A5C3 bne loc_C0A60A */
    if(vflip) goto loc_A60A;
  }
  S(0xA5C5, 3); alu_bit_imm16(ss, a, 0x4000);  /* C0A5C5 bit #$4000 */
  {
    const bool hflip = !ss_z(ss);
    S(0xA5C8, 1); t_branch(ss, hflip);      /* C0A5C8 bne loc_C0A5EA */
    if(hflip) goto loc_A5EA;
  }
  S(0xA5CA, 2); a = t_read16(ss, dp + entity_screen_x); ss_set_nz16(ss, a);
  SI(0xA5CC); ss_set_c(ss, true);           /* C0A5CC sec */
  S(0xA5CD, 3); a = alu_sbc16(ss, a, 0x0080);  /* C0A5CD sbc #$0080 */
  S(0xA5D0, 2); t_write16(ss, dp + entity_screen_x, a);
  S(0xA5D2, 2); a = t_read16(ss, dp + entity_screen_y); ss_set_nz16(ss, a);
  SI(0xA5D4); ss_set_c(ss, false);          /* C0A5D4 clc */
  S(0xA5D5, 3); a = alu_adc16(ss, a, 0x0010);  /* C0A5D5 adc #$0010 */
  S(0xA5D8, 2); t_write16(ss, dp + entity_screen_y, a);
  S(0xA5DA, 3); alu_cpx16(ss, x, 0x0004);   /* C0A5DA cpx #$0004 */
  {
    const bool big = ss_c(ss);
    S(0xA5DD, 1); t_branch(ss, big);        /* C0A5DD bcs loc_C0A5E5 */
    if(!big) {
      S(0xA5DF, 3);                         /* C0A5DF jsr oam_emit_frame_1row */
      const CallResult r = t_jsr_emitter(ss, pb, 0xA757);
      if(r == CALL_YIELD) return;
      a = ss_a(ss); x = ss_x(ss); y = ss_y(ss);
      if(r == CALL_TAIL) goto loc_A6CD;
      S(0xA5E2, 3); goto loc_A6BF;          /* C0A5E2 jmp loc_C0A6BF */
    }
  }
  /* loc_C0A5E5 */
  S(0xA5E5, 3);                             /* C0A5E5 jsr oam_emit_frame_2row */
  {
    const CallResult r = t_jsr_emitter(ss, pb, 0xA772);
    if(r == CALL_YIELD) return;
    a = ss_a(ss); x = ss_x(ss); y = ss_y(ss);
    if(r == CALL_TAIL) goto loc_A6CD;
  }
  S(0xA5E8, 1); t_branch(ss, true);         /* C0A5E8 bra loc_C0A62B */
  goto loc_A62B;

loc_A5EA: ;
  S(0xA5EA, 2); a = t_read16(ss, dp + entity_screen_x); ss_set_nz16(ss, a);
  SI(0xA5EC); ss_set_c(ss, true);           /* C0A5EC sec */
  S(0xA5ED, 3); a = alu_sbc16(ss, a, 0x008F);  /* C0A5ED sbc #$008F */
  S(0xA5F0, 2); t_write16(ss, dp + entity_screen_x, a);
  S(0xA5F2, 2); a = t_read16(ss, dp + entity_screen_y); ss_set_nz16(ss, a);
  SI(0xA5F4); ss_set_c(ss, false);          /* C0A5F4 clc */
  S(0xA5F5, 3); a = alu_adc16(ss, a, 0x0010);  /* C0A5F5 adc #$0010 */
  S(0xA5F8, 2); t_write16(ss, dp + entity_screen_y, a);
  S(0xA5FA, 3); alu_cpx16(ss, x, 0x0004);   /* C0A5FA cpx #$0004 */
  {
    const bool big = ss_c(ss);
    S(0xA5FD, 1); t_branch(ss, big);        /* C0A5FD bcs loc_C0A605 */
    if(!big) {
      S(0xA5FF, 3);                         /* C0A5FF jsr oam_emit_frame_1row_flip */
      const CallResult r = t_jsr_emitter(ss, pb, 0xA8F6);
      if(r == CALL_YIELD) return;
      a = ss_a(ss); x = ss_x(ss); y = ss_y(ss);
      if(r == CALL_TAIL) goto loc_A6CD;
      S(0xA602, 3); goto loc_A6BF;          /* C0A602 jmp loc_C0A6BF */
    }
  }
  /* loc_C0A605 */
  S(0xA605, 3);                             /* C0A605 jsr oam_emit_frame_2row_flip */
  {
    const CallResult r = t_jsr_emitter(ss, pb, 0xA911);
    if(r == CALL_YIELD) return;
    a = ss_a(ss); x = ss_x(ss); y = ss_y(ss);
    if(r == CALL_TAIL) goto loc_A6CD;
  }
  S(0xA608, 1); t_branch(ss, true);         /* C0A608 bra loc_C0A62B */
  goto loc_A62B;

loc_A60A: ;
  S(0xA60A, 3); alu_bit_imm16(ss, a, 0x4000);  /* C0A60A bit #$4000 */
  {
    const bool hflip = !ss_z(ss);
    S(0xA60D, 1); t_branch(ss, hflip);      /* C0A60D bne loc_C0A61E */
    if(!hflip) {
      S(0xA60F, 2); a = t_read16(ss, dp + entity_screen_x); ss_set_nz16(ss, a);
      SI(0xA611); ss_set_c(ss, true);       /* C0A611 sec */
      S(0xA612, 3); a = alu_sbc16(ss, a, 0x0080);  /* C0A612 sbc #$0080 */
      S(0xA615, 2); t_write16(ss, dp + entity_screen_x, a);
      S(0xA617, 2); t_rmw16(ss, dp + entity_screen_y, +1);  /* C0A617 inc entity_screen_y */
      S(0xA619, 3);                         /* C0A619 jsr oam_emit_frame_3row */
      const CallResult r = t_jsr_emitter(ss, pb, 0xAAAA);
      if(r == CALL_YIELD) return;
      a = ss_a(ss); x = ss_x(ss); y = ss_y(ss);
      if(r == CALL_TAIL) goto loc_A6CD;
      S(0xA61C, 1); t_branch(ss, true);     /* C0A61C bra loc_C0A62B */
      goto loc_A62B;
    }
  }
  /* loc_C0A61E */
  S(0xA61E, 2); a = t_read16(ss, dp + entity_screen_x); ss_set_nz16(ss, a);
  SI(0xA620); ss_set_c(ss, true);           /* C0A620 sec */
  S(0xA621, 3); a = alu_sbc16(ss, a, 0x008F);  /* C0A621 sbc #$008F */
  S(0xA624, 2); t_write16(ss, dp + entity_screen_x, a);
  S(0xA626, 2); t_rmw16(ss, dp + entity_screen_y, +1);  /* C0A626 inc entity_screen_y */
  S(0xA628, 3);                             /* C0A628 jsr oam_emit_frame_3row_flip */
  {
    const CallResult r = t_jsr_emitter(ss, pb, 0xAC40);
    if(r == CALL_YIELD) return;
    a = ss_a(ss); x = ss_x(ss); y = ss_y(ss);
    if(r == CALL_TAIL) goto loc_A6CD;
  }
  /* falls into loc_C0A62B */

loc_A62B: ;
  /* Queue this frame's VRAM tile upload, unless the same frame id is already
   * loaded. Two 8-byte entity_tile_job records can come out of one frame: the
   * second one only when the nibble at $23 is non-zero. */
  S(0xA62B, 2);                             /* C0A62B ldx entity_render_index */
  x = t_read16(ss, dp + entity_render_index);
  ss_set_nz16(ss, x);
  S(0xA62D, 3); t_index(ss);                /* C0A62D lda entity_render_order,X */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_render_order + x)));
  ss_set_nz16(ss, a);
  SI(0xA630); x = a; ss_set_nz16(ss, x);    /* C0A630 tax */
  S(0xA631, 3); t_index(ss);                /* C0A631 lda entity_frame_id,X */
  a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_frame_id + x)));
  ss_set_nz16(ss, a);
  S(0xA634, 3); t_index(ss);                /* C0A634 cmp entity_frame_loaded,X */
  alu_cmp16(ss, a, t_read16(ss, ss_abs(ss, (uint16_t) (entity_frame_loaded + x))));
  {
    const bool changed = !ss_z(ss);
    S(0xA637, 1); t_branch(ss, changed);    /* C0A637 bne loc_C0A63C */
    if(!changed) { S(0xA639, 3); goto loc_A6BF; }  /* C0A639 jmp loc_C0A6BF */
  }
  /* loc_C0A63C */
  S(0xA63C, 3); t_index(ss);                /* C0A63C sta entity_frame_loaded,X */
  t_write16(ss, ss_abs(ss, (uint16_t) (entity_frame_loaded + x)), a);
  S(0xA63F, 3);                             /* C0A63F ldx entity_tile_job_ptr */
  x = t_read16(ss, ss_abs(ss, entity_tile_job_ptr));
  ss_set_nz16(ss, x);
  SI(0xA642); a = y; ss_set_nz16(ss, a);    /* C0A642 tya */
  SI(0xA643); ss_set_c(ss, false);          /* C0A643 clc */
  S(0xA644, 2);                             /* C0A644 adc sprite_frame_ptr */
  a = alu_adc16(ss, a, t_read16(ss, dp + sprite_frame_ptr));
  SI(0xA646); y = a; ss_set_nz16(ss, y);    /* C0A646 tay */
  S(0xA647, 3); t_index(ss);                /* C0A647 sta $0A8E,X — source addr */
  t_write16(ss, ss_abs(ss, (uint16_t) (entity_tile_job + 4 + x)), a);
  S(0xA64A, 2);                             /* C0A64A lda $21 — the word at $21/$22 */
  a = t_read16(ss, dp + oam_tile_src_lo);
  ss_set_nz16(ss, a);
  S(0xA64C, 3); a = alu_and16(ss, a, 0x00FF);  /* C0A64C and #$00FF */
  SI(0xA64F); a = alu_asl16(ss, a);         /* C0A64F asl A */
  SI(0xA650); a = alu_asl16(ss, a);         /* C0A650 asl A */
  SI(0xA651); a = alu_asl16(ss, a);         /* C0A651 asl A */
  SI(0xA652); a = alu_asl16(ss, a);         /* C0A652 asl A */
  SI(0xA653); a = alu_asl16(ss, a);         /* C0A653 asl A */
  S(0xA654, 3); t_index(ss);                /* C0A654 sta $0A8A,X — byte count */
  t_write16(ss, ss_abs(ss, (uint16_t) (entity_tile_job + 0 + x)), a);
  S(0xA657, 2); t_write16(ss, dp + depth_sort_key, a);  /* C0A657 sta $52 */
  SI(0xA659); a = y; ss_set_nz16(ss, a);    /* C0A659 tya */
  SI(0xA65A); ss_set_c(ss, false);          /* C0A65A clc */
  S(0xA65B, 2);                             /* C0A65B adc $52 */
  a = alu_adc16(ss, a, t_read16(ss, dp + depth_sort_key));
  SI(0xA65D); y = a; ss_set_nz16(ss, y);    /* C0A65D tay */
  S(0xA65E, 2);                             /* C0A65E lda $18 */
  a = t_read16(ss, dp + oam_flag_word);
  ss_set_nz16(ss, a);
  S(0xA660, 3); a = alu_and16(ss, a, 0x01FF);  /* C0A660 and #$01FF */
  SI(0xA663); a = alu_asl16(ss, a);         /* C0A663 asl A */
  SI(0xA664); a = alu_asl16(ss, a);         /* C0A664 asl A */
  SI(0xA665); a = alu_asl16(ss, a);         /* C0A665 asl A */
  SI(0xA666); a = alu_asl16(ss, a);         /* C0A666 asl A */
  S(0xA667, 3); t_index(ss);                /* C0A667 sta $0A8C,X — VRAM address */
  t_write16(ss, ss_abs(ss, (uint16_t) (entity_tile_job + 2 + x)), a);
  S(0xA66A, 2);                             /* C0A66A lda sprite_frame_bank */
  a = t_read16(ss, dp + sprite_frame_bank);
  ss_set_nz16(ss, a);
  S(0xA66C, 3); a = alu_ora16(ss, a, 0xFF00);  /* C0A66C ora #$FF00 — the pending flag */
  S(0xA66F, 3); t_index(ss);                /* C0A66F sta $0A90,X */
  t_write16(ss, ss_abs(ss, (uint16_t) (entity_tile_job + 6 + x)), a);
  SI(0xA672); a = x; ss_set_nz16(ss, a);    /* C0A672 txa */
  SI(0xA673); ss_set_c(ss, false);          /* C0A673 clc */
  S(0xA674, 3); a = alu_adc16(ss, a, 0x0008);  /* C0A674 adc #$0008 */
  SI(0xA677); x = a; ss_set_nz16(ss, x);    /* C0A677 tax */
  S(0xA678, 3); t_index(ss);                /* C0A678 stz $0A90,X */
  t_write16(ss, ss_abs(ss, (uint16_t) (entity_tile_job + 6 + x)), 0);
  S(0xA67B, 2);                             /* C0A67B lda $23 — the word at $23/$24 */
  a = t_read16(ss, dp + oam_tile_src_hi);
  ss_set_nz16(ss, a);
  S(0xA67D, 3); a = alu_and16(ss, a, 0x000F);  /* C0A67D and #$000F */
  S(0xA680, 3); alu_cmp16(ss, a, 0x0000);   /* C0A680 cmp #$0000 */
  {
    const bool second = !ss_z(ss);
    S(0xA683, 1); t_branch(ss, !second);    /* C0A683 beq loc_C0A6B9 */
    if(second) {
      SI(0xA685); a = alu_asl16(ss, a);     /* C0A685 asl A */
      SI(0xA686); a = alu_asl16(ss, a);     /* C0A686 asl A */
      SI(0xA687); a = alu_asl16(ss, a);     /* C0A687 asl A */
      SI(0xA688); a = alu_asl16(ss, a);     /* C0A688 asl A */
      SI(0xA689); a = alu_asl16(ss, a);     /* C0A689 asl A */
      S(0xA68A, 3); t_index(ss);            /* C0A68A sta $0A8A,X */
      t_write16(ss, ss_abs(ss, (uint16_t) (entity_tile_job + 0 + x)), a);
      S(0xA68D, 2);                         /* C0A68D lda $22 */
      a = t_read16(ss, dp + oam_row_3_base);
      ss_set_nz16(ss, a);
      S(0xA68F, 3); a = alu_and16(ss, a, 0x00FF);  /* C0A68F and #$00FF */
      SI(0xA692); a = alu_asl16(ss, a);     /* C0A692 asl A */
      SI(0xA693); a = alu_asl16(ss, a);     /* C0A693 asl A */
      SI(0xA694); a = alu_asl16(ss, a);     /* C0A694 asl A */
      SI(0xA695); a = alu_asl16(ss, a);     /* C0A695 asl A */
      S(0xA696, 2); t_write16(ss, dp + depth_sort_key, a);  /* C0A696 sta $52 */
      S(0xA698, 2);                         /* C0A698 lda $18 */
      a = t_read16(ss, dp + oam_flag_word);
      ss_set_nz16(ss, a);
      S(0xA69A, 3); a = alu_and16(ss, a, 0x01FF);  /* C0A69A and #$01FF */
      SI(0xA69D); a = alu_asl16(ss, a);     /* C0A69D asl A */
      SI(0xA69E); a = alu_asl16(ss, a);     /* C0A69E asl A */
      SI(0xA69F); a = alu_asl16(ss, a);     /* C0A69F asl A */
      SI(0xA6A0); a = alu_asl16(ss, a);     /* C0A6A0 asl A */
      SI(0xA6A1); ss_set_c(ss, false);      /* C0A6A1 clc */
      S(0xA6A2, 2);                         /* C0A6A2 adc $52 */
      a = alu_adc16(ss, a, t_read16(ss, dp + depth_sort_key));
      S(0xA6A4, 3); t_index(ss);            /* C0A6A4 sta $0A8C,X */
      t_write16(ss, ss_abs(ss, (uint16_t) (entity_tile_job + 2 + x)), a);
      SI(0xA6A7); a = y; ss_set_nz16(ss, a);/* C0A6A7 tya */
      S(0xA6A8, 3); t_index(ss);            /* C0A6A8 sta $0A8E,X */
      t_write16(ss, ss_abs(ss, (uint16_t) (entity_tile_job + 4 + x)), a);
      S(0xA6AB, 2);                         /* C0A6AB lda sprite_frame_bank */
      a = t_read16(ss, dp + sprite_frame_bank);
      ss_set_nz16(ss, a);
      S(0xA6AD, 3); a = alu_ora16(ss, a, 0xFF00);  /* C0A6AD ora #$FF00 */
      S(0xA6B0, 3); t_index(ss);            /* C0A6B0 sta $0A90,X */
      t_write16(ss, ss_abs(ss, (uint16_t) (entity_tile_job + 6 + x)), a);
      SI(0xA6B3); a = x; ss_set_nz16(ss, a);/* C0A6B3 txa */
      SI(0xA6B4); ss_set_c(ss, false);      /* C0A6B4 clc */
      S(0xA6B5, 3); a = alu_adc16(ss, a, 0x0008);  /* C0A6B5 adc #$0008 */
      SI(0xA6B8); x = a; ss_set_nz16(ss, x);/* C0A6B8 tax */
    }
  }
  /* loc_C0A6B9 */
  S(0xA6B9, 3); t_write16(ss, ss_abs(ss, entity_tile_job_ptr), x);  /* C0A6B9 stx $0A88 */
  S(0xA6BC, 3); t_index(ss);                /* C0A6BC stz $0A90,X */
  t_write16(ss, ss_abs(ss, (uint16_t) (entity_tile_job + 6 + x)), 0);
  goto loc_A6BF;

loc_A560: ;
  S(0xA560, 3);                             /* C0A560 jmp loc_C0A6BF */
  /* fall through */

loc_A6BF: ;
  S(0xA6BF, 2); t_rmw16(ss, dp + entity_render_index, +1);  /* C0A6BF inc entity_render_index */
  S(0xA6C1, 2); t_rmw16(ss, dp + entity_render_index, +1);  /* C0A6C1 inc entity_render_index */
  S(0xA6C3, 2);                             /* C0A6C3 lda entity_render_index */
  a = t_read16(ss, dp + entity_render_index);
  ss_set_nz16(ss, a);
  S(0xA6C5, 3); alu_cmp16(ss, a, 0x0020);   /* C0A6C5 cmp #$0020 */
  {
    const bool done = ss_z(ss);
    S(0xA6C8, 1); t_branch(ss, done);       /* C0A6C8 beq loc_C0A6CD */
    if(!done) { S(0xA6CA, 3); goto loc_A53D; }  /* C0A6CA jmp loc_C0A53D */
  }

loc_A6CD: ;
  ss_set_a(ss, a); ss_set_x(ss, x); ss_set_y(ss, y);
  oam_frame_exit(ss, 0xA6CD);
}

/* ---------------------------------------------------------------------------
 * entity_render_order_reset — $C0:AEB9
 *
 * Called once from the post-reset bring-up at $C0:8073, before the first sort:
 * fills entity_render_order with the identity permutation, entry n = n * 2, so
 * entity_sort_draw_order has sixteen valid entity indices to permute.
 * Exit: A = $001E, X = $0020, C set, Z set.
 * ------------------------------------------------------------------------- */
void entity_render_order_reset(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);

  S(0xAEB9, 3); x = 0x0000; ss_set_nz16(ss, x);  /* C0AEB9 ldx #$0000 */
  for(;;) {
    SI(0xAEBC); a = x; ss_set_nz16(ss, a);  /* C0AEBC txa */
    S(0xAEBD, 3); t_index(ss);              /* C0AEBD sta entity_render_order,X */
    t_write16(ss, ss_abs(ss, (uint16_t) (entity_render_order + x)), a);
    SI(0xAEC0); x = (uint16_t) (x + 1); ss_set_nz16(ss, x);  /* C0AEC0 inx */
    SI(0xAEC1); x = (uint16_t) (x + 1); ss_set_nz16(ss, x);  /* C0AEC1 inx */
    S(0xAEC2, 3); alu_cpx16(ss, x, 0x0020); /* C0AEC2 cpx #$0020 */
    S(0xAEC5, 1); t_branch(ss, x != 0x0020);/* C0AEC5 bne loc_C0AEBC */
    if(x == 0x0020) break;
  }
  ss_set_a(ss, a);
  ss_set_x(ss, x);
  S(0xAEC7, 1);                             /* C0AEC7 rts */
  ss_rts(ss);
}

static const RecompEntry kOamEmit[] = {
  { 0xc0a538, "entity_build_oam_frame",   entity_build_oam_frame },
  { 0xc0a757, "oam_emit_frame_1row",      oam_emit_frame_1row },
  { 0xc0a772, "oam_emit_frame_2row",      oam_emit_frame_2row },
  { 0xc0a8f6, "oam_emit_frame_1row_flip", oam_emit_frame_1row_flip },
  { 0xc0a911, "oam_emit_frame_2row_flip", oam_emit_frame_2row_flip },
  { 0xc0aaaa, "oam_emit_frame_3row",      oam_emit_frame_3row },
  { 0xc0ac40, "oam_emit_frame_3row_flip", oam_emit_frame_3row_flip },
  { 0xc0aeb9, "entity_render_order_reset", entity_render_order_reset },
};
RECOMP_REGISTER(kOamEmit)
