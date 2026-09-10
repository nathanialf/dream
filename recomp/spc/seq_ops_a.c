/* The first half of the sequence-command handlers, SPC $0B72-$0E44
 * (spc/driver.asm).
 *
 * These are the routines seq_cmd_table ($0FD8) points at: instrument, volume,
 * jump/call/return, note length, pitch slide, tempo and vibrato. The sequencer
 * reaches them from `jmp (seq_cmd_table+x)` at $085F with the event pointer in
 * $00/$01, Y = 0 and the slot index pushed on the stack, and they end either on
 * `ret` (A = 1 means "the pointer moved, fetch the next event") or by falling
 * into one of the shared tails that add the event's length to the slot's
 * sequence pointer.
 *
 * Three conventions carry through the file, all of them the driver's own shape:
 *
 *   A tail reached by `jmp` is left through S_GOTO, so the address owns itself
 *   and the routine registered there -- its own hook if it has one, the ROM's
 *   code if not -- runs next. loc_0B78/loc_0B7B (the pointer advance at the end
 *   of seq_instrument), loc_0D6A, loc_0DDF, loc_0DF2, loc_0F09 and seq_advance5
 *   are all entered that way. Where a handler *falls through* into the next
 *   routine (seq_read_volume into seq_read_volume_r) the same thing happens, so the
 *   routine that owns the address is still credited with the call.
 *
 *   A tail reached by a *branch* from inside two handlers belongs to both of
 *   them, and is a static helper here: loc_0C2E (the mono average shared by
 *   seq_volume_preset and orphan_volume_preset2) and loc_0DAA (the slide body
 *   shared by seq_slide_up and seq_slide_down).
 *
 *   A `call` runs on the emulator through sps_run_callee (call_sub below), so a
 *   callee that is converted gets its hook -- seq_load_srcn, seq_read_volume,
 *   seq_read_volume_r, seq_push_return, seq_read_vibrato and dsp_flg_20 are all in the
 *   table -- and one that is not (seq_read_adsr, seq_retrigger) runs as the
 *   ROM's own code. `call seq_pop_x` is the single exception, and the comment on
 *   call_seq_pop_x says why: it reaches its argument by pulling the return
 *   address off the stack, which is the one thing a stack-pointer threshold
 *   cannot bracket.
 */
#include <stdint.h>
#include <stdbool.h>

#include "spc_state.h"
#include "spc_time.h"

#define SEQ_POP_X       0x0B64   /* the shared prologue; falls into seq_retrigger */
#define SEQ_RETRIGGER   0x0B69   /* not converted: duration[x] = 1, gate[x] = 0 */
#define SEQ_LOAD_SRCN   0x0B8B
#define SEQ_READ_VOLUME 0x0BC2
#define SEQ_READ_VOLUME_R 0x0BCC
#define SEQ_PUSH_RETURN 0x0D1C
#define SEQ_READ_ADSR   0x0E4E   /* not converted yet */
#define SEQ_READ_VIB    0x0E25
#define DSP_FLG_20      0x1123
#define LOC_0B78        0x0B78   /* $00 = 2, then the pointer advance */
#define LOC_0B7B        0x0B7B   /* the pointer advance itself: ptr += $00 */
#define LOC_0CF1        0x0CF1   /* seq_call's tail: push the return, take the target */
#define LOC_0CF4        0x0CF4
#define LOC_0D6A        0x0D6A   /* $00 = 4, then the pointer advance */
#define LOC_0DDF        0x0DDF   /* retrigger with $00 = 1, then the advance */
#define LOC_0DF2        0x0DF2   /* seq_tempo's tail: retrigger, then $00 = 2 */
#define LOC_0F09        0x0F09   /* $00 = 1, then the pointer advance */
#define SEQ_ADVANCE5    0x0FA9   /* $00 = 5, then the pointer advance */

/* `call abs` (case 0x3f) to a callee the emulator runs: the five cycles the
 * opcode spends after its three fetches, then the callee itself, stopping when
 * the catch-up slice ends underneath it. Any hook the callee hits still fires.
 * Returns true when the body must return -- the address pushed is the routine's
 * real return address, so the driver finishes what is left. */
static bool call_sub(SpcState* sp, uint16_t ret_addr, uint16_t callee) {
  sps_idle(sp);
  uint8_t frame = sps_sp(sp);
  sps_push16(sp, ret_addr);
  sps_idle(sp);
  sps_idle(sp);
  sps_set_pc(sp, callee);
  return sps_run_callee(sp, frame);
}

/* One modelled instruction inside a helper that reports the yield to its caller
 * rather than returning from the body itself. */
#define SC(addr, n) do { if(s_step(sp, (uint16_t) (addr), (n), a, x, y)) return true; } while(0)

/* `call seq_pop_x` ($0B64), the prologue fourteen of these handlers share: it
 * recovers the slot index the sequencer pushed before the jump, and falls into
 * seq_retrigger, which returns A = 0 and Y = 1.
 *
 * It is the one callee here the emulator cannot be handed whole. seq_pop_x
 * reaches that index by pulling the return address off the stack first, so for
 * three instructions the stack pointer is *above* the frame the `call` built --
 * and that threshold is sps_run_callee's only stopping condition, so it would
 * end the callee at its second instruction. The five stack instructions are
 * modelled here at their own addresses instead; by $0B69 the stack is back
 * below the frame and the tail runs on the emulator like any other callee.
 *
 * `at` is the address of the `call`, whose three bytes this fetches. Returns
 * true when the caller must return. */
static bool call_seq_pop_x(SpcState* sp, uint16_t at,
                           uint8_t* pa, uint8_t* px, uint8_t* py) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  SC(at, 3);                                              /* call seq_pop_x */
  sps_idle(sp);
  uint8_t frame = sps_sp(sp);
  sps_push16(sp, (uint16_t) (at + 3));
  sps_idle(sp);
  sps_idle(sp);
  SC(0x0B64, 1); y = s_pop(sp);                           /* 0B64 pop y */
  SC(0x0B65, 1); a = s_pop(sp);                           /* 0B65 pop a */
  SC(0x0B66, 1); x = s_pop(sp);                           /* 0B66 pop x */
  SC(0x0B67, 1); s_push(sp, a);                           /* 0B67 push a */
  SC(0x0B68, 1); s_push(sp, y);                           /* 0B68 push y */
  S_PUB();
  sps_set_pc(sp, SEQ_RETRIGGER);                          /* falls into seq_retrigger */
  if(sps_run_callee(sp, (uint8_t) (frame + 1))) return true;
  *pa = sps_a(sp); *px = sps_x(sp); *py = sps_y(sp);
  return false;
}

/* ---------------------------------------------------------------------------
 * seq_instrument -- $0B72, seq command $01
 *
 * The instrument byte becomes the voice's SRCN, and the event is three bytes
 * long. loc_0B78 and loc_0B7B are its tail and the driver's most-jumped-to
 * address: $00/$01 is the length to add to the slot's sequence pointer.
 * ------------------------------------------------------------------------- */
/* Entered in its middle as well: loc_0B78, loc_0B7B. --no-cpu resolves every pc
 * through the registry, so an address the driver jumps into needs a body that
 * can start there. */
static void seq_instrument_at(SpcState* sp, uint16_t entry) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);
  if(entry == 0x0B78) goto loc_0B78;
  if(entry == 0x0B7B) goto loc_0B7B;

  if(call_seq_pop_x(sp, 0x0B72, &a, &x, &y)) return;      /* 0B72 call seq_pop_x */
  S(0x0B75, 3);                                           /* 0B75 call seq_load_srcn */
  if(call_sub(sp, 0x0B78, SEQ_LOAD_SRCN)) return;
  a = sps_a(sp); x = sps_x(sp); y = sps_y(sp);
loc_0B78:

  S(0x0B78, 3); s_movs(sp, sps_dp(sp, 0x00), 0x02);       /* 0B78 mov $00,#$02 */
loc_0B7B:
  S(0x0B7B, 3); s_movs(sp, sps_dp(sp, 0x01), 0x00);       /* 0B7B mov $01,#$00 */
  S(0x0B7E, 2); a = s_load(sp, s_adr_dpx(sp, 0x44, x));   /* 0B7E mov a,$44+x */
  S(0x0B80, 2); y = s_load(sp, s_adr_dpx(sp, 0x54, x));   /* 0B80 mov y,$54+x */
  S(0x0B82, 2);                                           /* 0B82 addw ya,$00 */
  { uint16_t ya = s_addw(sp, 0x00, (uint16_t) (a | (y << 8)));
    a = (uint8_t) ya; y = (uint8_t) (ya >> 8); }
  S(0x0B84, 2); s_movs(sp, s_adr_dpx(sp, 0x54, x), y);    /* 0B84 mov $54+x,y */
  S(0x0B86, 2); s_movs(sp, s_adr_dpx(sp, 0x44, x), a);    /* 0B86 mov $44+x,a */
  S(0x0B88, 2); a = 0x01; sps_set_zn(sp, a);              /* 0B88 mov a,#$01 */
  S(0x0B8A, 1); S_PUB(); sps_ret(sp);                     /* 0B8A ret */
}

static void seq_instrument(SpcState* sp) { seq_instrument_at(sp, 0x0B72); }
static void loc_0B78(SpcState* sp) { seq_instrument_at(sp, 0x0B78); }
static void loc_0B7B(SpcState* sp) { seq_instrument_at(sp, 0x0B7B); }

/* ---------------------------------------------------------------------------
 * seq_load_srcn -- $0B8B
 *
 * The sample number in the sequence goes through sample_remap ($0560, uploaded
 * by the 65816) to become the voice's directory slot.
 * ------------------------------------------------------------------------- */
static void seq_load_srcn(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  S(0x0B8B, 1); s_push(sp, x);                            /* 0B8B push x */
  S(0x0B8C, 2); a = s_load(sp, s_adr_idy(sp, 0x00, y));   /* 0B8C mov a,($00)+y */
  S(0x0B8E, 1); s_imp(sp); x = a; sps_set_zn(sp, x);      /* 0B8E mov x,a */
  S(0x0B8F, 3); s_idx(sp);                                /* 0B8F mov a,$0560+x */
  a = s_load(sp, (uint16_t) (0x0560 + x));
  S(0x0B92, 1); x = s_pop(sp);                            /* 0B92 pop x */
  S(0x0B93, 3); s_idx(sp);                                /* 0B93 mov $0244+x,a */
  s_movs(sp, (uint16_t) (0x0244 + x), a);
  S(0x0B96, 1); S_PUB(); sps_ret(sp);                     /* 0B96 ret */
}

/* ---------------------------------------------------------------------------
 * seq_instr_full -- $0B97, seq command $22
 *
 * Instrument, transpose, finetune, volume and ADSR in one eight-byte event.
 * ------------------------------------------------------------------------- */
static void seq_instr_full(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  if(call_seq_pop_x(sp, 0x0B97, &a, &x, &y)) return;      /* 0B97 call seq_pop_x */
  S(0x0B9A, 3);                                           /* 0B9A call seq_load_srcn */
  if(call_sub(sp, 0x0B9D, SEQ_LOAD_SRCN)) return;
  a = sps_a(sp); x = sps_x(sp); y = sps_y(sp);

  S(0x0B9D, 1); s_imp(sp); y++; sps_set_zn(sp, y);        /* 0B9D inc y */
  S(0x0B9E, 2); a = s_load(sp, s_adr_idy(sp, 0x00, y));   /* 0B9E mov a,($00)+y */
  S(0x0BA0, 3); s_idx(sp);                                /* 0BA0 mov $0140+x,a */
  s_movs(sp, (uint16_t) (0x0140 + x), a);
  S(0x0BA3, 1); s_imp(sp); y++; sps_set_zn(sp, y);        /* 0BA3 inc y */
  S(0x0BA4, 2); a = s_load(sp, s_adr_idy(sp, 0x00, y));   /* 0BA4 mov a,($00)+y */
  S(0x0BA6, 2); s_movs(sp, s_adr_dpx(sp, 0x64, x), a);    /* 0BA6 mov $64+x,a */
  S(0x0BA8, 1); s_imp(sp); y++; sps_set_zn(sp, y);        /* 0BA8 inc y */
  S(0x0BA9, 3);                                           /* 0BA9 call seq_read_volume */
  if(call_sub(sp, 0x0BAC, SEQ_READ_VOLUME)) return;
  a = sps_a(sp); x = sps_x(sp); y = sps_y(sp);
  S(0x0BAC, 1); s_imp(sp); y++; sps_set_zn(sp, y);        /* 0BAC inc y */
  S(0x0BAD, 3);                                           /* 0BAD call seq_read_adsr */
  if(call_sub(sp, 0x0BB0, SEQ_READ_ADSR)) return;
  a = sps_a(sp); x = sps_x(sp); y = sps_y(sp);
  S(0x0BB0, 3); s_movs(sp, sps_dp(sp, 0x00), 0x08);       /* 0BB0 mov $00,#$08 */
  S(0x0BB3, 3); S_GOTO(LOC_0B7B);                         /* 0BB3 jmp loc_0B7B */
}

/* ---------------------------------------------------------------------------
 * seq_volume -- $0BB6, seq command $02
 *
 * Two bytes, L and R. loc_0BBC (the three-byte event length) is its tail and
 * seq_adsr jumps into it.
 * ------------------------------------------------------------------------- */
/* Entered in its middle as well: loc_0BBC. --no-cpu resolves every pc
 * through the registry, so an address the driver jumps into needs a body that
 * can start there. */
static void seq_volume_at(SpcState* sp, uint16_t entry) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);
  if(entry == 0x0BBC) goto loc_0BBC;

  if(call_seq_pop_x(sp, 0x0BB6, &a, &x, &y)) return;      /* 0BB6 call seq_pop_x */
  S(0x0BB9, 3);                                           /* 0BB9 call seq_read_volume */
  if(call_sub(sp, 0x0BBC, SEQ_READ_VOLUME)) return;
  a = sps_a(sp); x = sps_x(sp); y = sps_y(sp);
loc_0BBC:
  S(0x0BBC, 3); s_movs(sp, sps_dp(sp, 0x00), 0x03);       /* 0BBC mov $00,#$03 */
  S(0x0BBF, 3); S_GOTO(LOC_0B7B);                         /* 0BBF jmp loc_0B7B */
}

static void seq_volume(SpcState* sp) { seq_volume_at(sp, 0x0BB6); }
static void loc_0BBC(SpcState* sp) { seq_volume_at(sp, 0x0BBC); }

/* ---------------------------------------------------------------------------
 * seq_read_volume -- $0BC2
 *
 * The L,R pair out of the sequence. With the mono flag set the two are halved
 * (as magnitudes, so a negative -- phase-inverted -- channel keeps its weight)
 * and summed into both; otherwise L is stored here and the routine falls
 * through into seq_read_volume_r for R.
 * ------------------------------------------------------------------------- */
static void seq_read_volume(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  S(0x0BC2, 2); a = s_load(sp, sps_dp(sp, 0x1D));         /* 0BC2 mov a,$1D */
  bool mono = !sps_z(sp);
  S(0x0BC4, 2); s_branch(sp, mono);                       /* 0BC4 bne loc_0BD2 */
  if(!mono) {
    S(0x0BC6, 2); a = s_load(sp, s_adr_idy(sp, 0x00, y)); /* 0BC6 mov a,($00)+y */
    S(0x0BC8, 3); s_idx(sp);                              /* 0BC8 mov $0254+x,a */
    s_movs(sp, (uint16_t) (0x0254 + x), a);
    S(0x0BCB, 1); s_imp(sp); y++; sps_set_zn(sp, y);      /* 0BCB inc y */
    S_GOTO(SEQ_READ_VOLUME_R);                            /* falls into seq_read_volume_r */
  }

  S(0x0BD2, 2); a = s_load(sp, s_adr_idy(sp, 0x00, y));   /* 0BD2 mov a,($00)+y */
  bool pos = !sps_n(sp);
  S(0x0BD4, 2); s_branch(sp, pos);                        /* 0BD4 bpl loc_0BD9 */
  if(!pos) {
    S(0x0BD6, 2); a = s_eor(sp, a, 0xFF);                 /* 0BD6 eor a,#$FF */
    S(0x0BD8, 1); s_imp(sp); a++; sps_set_zn(sp, a);      /* 0BD8 inc a */
  }
  S(0x0BD9, 1); a = s_lsr_a(sp, a);                       /* 0BD9 lsr a */
  S(0x0BDA, 2); s_movs(sp, sps_dp(sp, 0x03), a);          /* 0BDA mov $03,a */
  S(0x0BDC, 1); s_imp(sp); y++; sps_set_zn(sp, y);        /* 0BDC inc y */
  S(0x0BDD, 1); s_imp(sp); sps_set_c(sp, false);          /* 0BDD clrc */
  S(0x0BDE, 2); a = s_load(sp, s_adr_idy(sp, 0x00, y));   /* 0BDE mov a,($00)+y */
  pos = !sps_n(sp);
  S(0x0BE0, 2); s_branch(sp, pos);                        /* 0BE0 bpl loc_0BE5 */
  if(!pos) {
    S(0x0BE2, 2); a = s_eor(sp, a, 0xFF);                 /* 0BE2 eor a,#$FF */
    S(0x0BE4, 1); s_imp(sp); a++; sps_set_zn(sp, a);      /* 0BE4 inc a */
  }
  S(0x0BE5, 1); a = s_lsr_a(sp, a);                       /* 0BE5 lsr a */
  S(0x0BE6, 1); s_imp(sp); sps_set_c(sp, false);          /* 0BE6 clrc */
  S(0x0BE7, 2); a = s_adc(sp, a, s_read(sp, sps_dp(sp, 0x03)));  /* 0BE7 adc a,$03 */
  S(0x0BE9, 3); s_idx(sp);                                /* 0BE9 mov $0254+x,a */
  s_movs(sp, (uint16_t) (0x0254 + x), a);
  S(0x0BEC, 3); s_idx(sp);                                /* 0BEC mov $0264+x,a */
  s_movs(sp, (uint16_t) (0x0264 + x), a);
  S(0x0BEF, 1); S_PUB(); sps_ret(sp);                     /* 0BEF ret */
}

/* ---------------------------------------------------------------------------
 * seq_read_volume_r -- $0BCC
 *
 * One sequence byte into the slot's right volume. It is the second half of
 * seq_read_volume's stereo path, which falls into it, and seq_volume_mono calls
 * it on its own to read the single byte that then goes to both channels.
 * ------------------------------------------------------------------------- */
static void seq_read_volume_r(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  S(0x0BCC, 2); a = s_load(sp, s_adr_idy(sp, 0x00, y));   /* 0BCC mov a,($00)+y */
  S(0x0BCE, 3); s_idx(sp);                                /* 0BCE mov $0264+x,a */
  s_movs(sp, (uint16_t) (0x0264 + x), a);
  S(0x0BD1, 1); S_PUB(); sps_ret(sp);                     /* 0BD1 ret */
}

/* ---------------------------------------------------------------------------
 * seq_volume_mono -- $0BF0, seq command $23
 *
 * One byte to both channels. The store at $0BF3 writes the A seq_retrigger left
 * behind (zero) and is immediately overwritten from $0264+x, which is what
 * seq_read_volume_r has just read.
 * ------------------------------------------------------------------------- */
static void seq_volume_mono(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  if(call_seq_pop_x(sp, 0x0BF0, &a, &x, &y)) return;      /* 0BF0 call seq_pop_x */
  S(0x0BF3, 3); s_idx(sp);                                /* 0BF3 mov $0254+x,a */
  s_movs(sp, (uint16_t) (0x0254 + x), a);
  S(0x0BF6, 3);                                           /* 0BF6 call seq_read_volume_r */
  if(call_sub(sp, 0x0BF9, SEQ_READ_VOLUME_R)) return;
  a = sps_a(sp); x = sps_x(sp); y = sps_y(sp);
  S(0x0BF9, 3); s_idx(sp);                                /* 0BF9 mov a,$0264+x */
  a = s_load(sp, (uint16_t) (0x0264 + x));
  S(0x0BFC, 3); s_idx(sp);                                /* 0BFC mov $0254+x,a */
  s_movs(sp, (uint16_t) (0x0254 + x), a);
  S(0x0BFF, 3); S_GOTO(LOC_0B78);                         /* 0BFF jmp loc_0B78 */
}

/* loc_0C2E -- the mono average of the two preset volumes, shared by
 * seq_volume_preset and orphan_volume_preset2, which both branch here. */
static void tail_0C2E(SpcState* sp, uint8_t a, uint8_t x, uint8_t y) {
  S(0x0C2E, 3); s_idx(sp);                                /* 0C2E mov a,$0254+x */
  a = s_load(sp, (uint16_t) (0x0254 + x));
  bool pos = !sps_n(sp);
  S(0x0C31, 2); s_branch(sp, pos);                        /* 0C31 bpl loc_0C36 */
  if(!pos) {
    S(0x0C33, 2); a = s_eor(sp, a, 0xFF);                 /* 0C33 eor a,#$FF */
    S(0x0C35, 1); s_imp(sp); a++; sps_set_zn(sp, a);      /* 0C35 inc a */
  }
  S(0x0C36, 1); a = s_lsr_a(sp, a);                       /* 0C36 lsr a */
  S(0x0C37, 2); s_movs(sp, sps_dp(sp, 0x00), a);          /* 0C37 mov $00,a */
  S(0x0C39, 3); s_idx(sp);                                /* 0C39 mov a,$0264+x */
  a = s_load(sp, (uint16_t) (0x0264 + x));
  pos = !sps_n(sp);
  S(0x0C3C, 2); s_branch(sp, pos);                        /* 0C3C bpl loc_0C41 */
  if(!pos) {
    S(0x0C3E, 2); a = s_eor(sp, a, 0xFF);                 /* 0C3E eor a,#$FF */
    S(0x0C40, 1); s_imp(sp); a++; sps_set_zn(sp, a);      /* 0C40 inc a */
  }
  S(0x0C41, 1); a = s_lsr_a(sp, a);                       /* 0C41 lsr a */
  S(0x0C42, 1); s_imp(sp); sps_set_c(sp, false);          /* 0C42 clrc */
  S(0x0C43, 2); a = s_adc(sp, a, s_read(sp, sps_dp(sp, 0x00)));  /* 0C43 adc a,$00 */
  S(0x0C45, 3); s_idx(sp);                                /* 0C45 mov $0254+x,a */
  s_movs(sp, (uint16_t) (0x0254 + x), a);
  S(0x0C48, 3); s_idx(sp);                                /* 0C48 mov $0264+x,a */
  s_movs(sp, (uint16_t) (0x0264 + x), a);
  S(0x0C4B, 3); S_GOTO(LOC_0F09);                         /* 0C4B jmp loc_0F09 */
}

/* ---------------------------------------------------------------------------
 * seq_volume_preset -- $0C02, seq command $20
 *
 * The volume pair set aside by seq_volume_presets ($04B8/$04B9).
 * ------------------------------------------------------------------------- */
static void seq_volume_preset(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  if(call_seq_pop_x(sp, 0x0C02, &a, &x, &y)) return;      /* 0C02 call seq_pop_x */
  S(0x0C05, 3); a = s_load(sp, 0x04B8);                   /* 0C05 mov a,$04B8 */
  S(0x0C08, 3); s_idx(sp);                                /* 0C08 mov $0254+x,a */
  s_movs(sp, (uint16_t) (0x0254 + x), a);
  S(0x0C0B, 3); a = s_load(sp, 0x04B9);                   /* 0C0B mov a,$04B9 */
  S(0x0C0E, 3); s_idx(sp);                                /* 0C0E mov $0264+x,a */
  s_movs(sp, (uint16_t) (0x0264 + x), a);
  S(0x0C11, 2); a = s_load(sp, sps_dp(sp, 0x1D));         /* 0C11 mov a,$1D */
  bool mono = !sps_z(sp);
  S(0x0C13, 2); s_branch(sp, mono);                       /* 0C13 bne loc_0C2E */
  if(mono) { tail_0C2E(sp, a, x, y); return; }
  S(0x0C15, 3); S_GOTO(LOC_0F09);                         /* 0C15 jmp loc_0F09 */
}

/* ---------------------------------------------------------------------------
 * orphan_volume_preset2 -- $0C18
 *
 * seq_cmd_table entry $31, in the stale range past command $24: the same
 * routine over the second preset pair ($04BA/$04BB), which seq_volume_presets
 * still writes but nothing reads. No sequence in the ROM contains a $31 event;
 * it is converted because the table entry points at it.
 * ------------------------------------------------------------------------- */
static void orphan_volume_preset2(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  if(call_seq_pop_x(sp, 0x0C18, &a, &x, &y)) return;      /* 0C18 call seq_pop_x */
  S(0x0C1B, 3); a = s_load(sp, 0x04BA);                   /* 0C1B mov a,$04BA */
  S(0x0C1E, 3); s_idx(sp);                                /* 0C1E mov $0254+x,a */
  s_movs(sp, (uint16_t) (0x0254 + x), a);
  S(0x0C21, 3); a = s_load(sp, 0x04BB);                   /* 0C21 mov a,$04BB */
  S(0x0C24, 3); s_idx(sp);                                /* 0C24 mov $0264+x,a */
  s_movs(sp, (uint16_t) (0x0264 + x), a);
  S(0x0C27, 2); a = s_load(sp, sps_dp(sp, 0x1D));         /* 0C27 mov a,$1D */
  bool mono = !sps_z(sp);
  S(0x0C29, 2); s_branch(sp, mono);                       /* 0C29 bne loc_0C2E */
  if(mono) { tail_0C2E(sp, a, x, y); return; }
  S(0x0C2B, 3); S_GOTO(LOC_0F09);                         /* 0C2B jmp loc_0F09 */
}

/* ---------------------------------------------------------------------------
 * seq_master_percent -- $0C4E, seq command $24
 *
 * One byte into master_percent ($04B6), the divisor scale_volume applies to
 * every note volume.
 * ------------------------------------------------------------------------- */
static void seq_master_percent(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  if(call_seq_pop_x(sp, 0x0C4E, &a, &x, &y)) return;      /* 0C4E call seq_pop_x */
  S(0x0C51, 2); a = s_load(sp, s_adr_idy(sp, 0x00, y));   /* 0C51 mov a,($00)+y */
  S(0x0C53, 3); s_movs(sp, 0x04B6, a);                    /* 0C53 mov $04B6,a */
  S(0x0C56, 3); S_GOTO(LOC_0B78);                         /* 0C56 jmp loc_0B78 */
}

/* ---------------------------------------------------------------------------
 * scale_volume -- $0C59
 *
 * A = A * master_percent / 100, clamped to +/-127 and done on the magnitude so
 * that a negative (phase-inverted) volume scales the same way. X above 7 is an
 * sfx slot and is left alone. Called from channel_update and from cmd5.
 * ------------------------------------------------------------------------- */
static void scale_volume(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  S(0x0C59, 1); s_push(sp, x);                            /* 0C59 push x */
  S(0x0C5A, 3); y = s_load(sp, 0x04B6);                   /* 0C5A mov y,$04B6 */
  S(0x0C5D, 2); s_cmp(sp, x, 0x08);                       /* 0C5D cmp x,#$08 */
  bool sfx = sps_c(sp);
  S(0x0C5F, 2); s_branch(sp, sfx);                        /* 0C5F bcs loc_0C6F */
  if(sfx) goto loc_0C6F;
  S(0x0C61, 2); s_cmp(sp, a, 0x00);                       /* 0C61 cmp a,#$00 */
  bool neg = sps_n(sp);
  S(0x0C63, 2); s_branch(sp, neg);                        /* 0C63 bmi loc_0C71 */
  if(neg) goto loc_0C71;
  S(0x0C65, 1);                                           /* 0C65 mul ya */
  { uint16_t ya = s_mul(sp, a, y); a = (uint8_t) ya; y = (uint8_t) (ya >> 8); }
  S(0x0C66, 2); x = 0x64; sps_set_zn(sp, x);              /* 0C66 mov x,#$64 */
  S(0x0C68, 1);                                           /* 0C68 div ya,x */
  { uint16_t ya = s_div(sp, a, x, y); a = (uint8_t) ya; y = (uint8_t) (ya >> 8); }
  S(0x0C69, 2); s_cmp(sp, a, 0x7F);                       /* 0C69 cmp a,#$7F */
  bool small = sps_n(sp);
  S(0x0C6B, 2); s_branch(sp, small);                      /* 0C6B bmi loc_0C6F */
  if(!small) { S(0x0C6D, 2); a = 0x7F; sps_set_zn(sp, a); }  /* 0C6D mov a,#$7F */

loc_0C6F:
  S(0x0C6F, 1); x = s_pop(sp);                            /* 0C6F pop x */
  S(0x0C70, 1); S_PUB(); sps_ret(sp); return;             /* 0C70 ret */

loc_0C71:
  S(0x0C71, 2); a = s_eor(sp, a, 0xFF);                   /* 0C71 eor a,#$FF */
  S(0x0C73, 1); s_imp(sp); a++; sps_set_zn(sp, a);        /* 0C73 inc a */
  S(0x0C74, 1);                                           /* 0C74 mul ya */
  { uint16_t ya = s_mul(sp, a, y); a = (uint8_t) ya; y = (uint8_t) (ya >> 8); }
  S(0x0C75, 2); x = 0x64; sps_set_zn(sp, x);              /* 0C75 mov x,#$64 */
  S(0x0C77, 1);                                           /* 0C77 div ya,x */
  { uint16_t ya = s_div(sp, a, x, y); a = (uint8_t) ya; y = (uint8_t) (ya >> 8); }
  S(0x0C78, 2); s_cmp(sp, a, 0x7F);                       /* 0C78 cmp a,#$7F */
  bool small2 = sps_n(sp);
  S(0x0C7A, 2); s_branch(sp, small2);                     /* 0C7A bmi loc_0C7E */
  if(!small2) { S(0x0C7C, 2); a = 0x7F; sps_set_zn(sp, a); } /* 0C7C mov a,#$7F */
  S(0x0C7E, 2); a = s_eor(sp, a, 0xFF);                   /* 0C7E eor a,#$FF */
  S(0x0C80, 1); s_imp(sp); a++; sps_set_zn(sp, a);        /* 0C80 inc a */
  S(0x0C81, 1); x = s_pop(sp);                            /* 0C81 pop x */
  S(0x0C82, 1); S_PUB(); sps_ret(sp);                     /* 0C82 ret */
}

/* ---------------------------------------------------------------------------
 * seq_volume_presets -- $0C83, seq command $1E
 *
 * Four bytes into the two preset pairs seq_volume_preset and
 * orphan_volume_preset2 read back.
 * ------------------------------------------------------------------------- */
static void seq_volume_presets(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  if(call_seq_pop_x(sp, 0x0C83, &a, &x, &y)) return;      /* 0C83 call seq_pop_x */
  S(0x0C86, 2); a = s_load(sp, s_adr_idy(sp, 0x00, y));   /* 0C86 mov a,($00)+y */
  S(0x0C88, 3); s_movs(sp, 0x04B8, a);                    /* 0C88 mov $04B8,a */
  S(0x0C8B, 1); s_imp(sp); y++; sps_set_zn(sp, y);        /* 0C8B inc y */
  S(0x0C8C, 2); a = s_load(sp, s_adr_idy(sp, 0x00, y));   /* 0C8C mov a,($00)+y */
  S(0x0C8E, 3); s_movs(sp, 0x04B9, a);                    /* 0C8E mov $04B9,a */
  S(0x0C91, 1); s_imp(sp); y++; sps_set_zn(sp, y);        /* 0C91 inc y */
  S(0x0C92, 2); a = s_load(sp, s_adr_idy(sp, 0x00, y));   /* 0C92 mov a,($00)+y */
  S(0x0C94, 3); s_movs(sp, 0x04BA, a);                    /* 0C94 mov $04BA,a */
  S(0x0C97, 1); s_imp(sp); y++; sps_set_zn(sp, y);        /* 0C97 inc y */
  S(0x0C98, 2); a = s_load(sp, s_adr_idy(sp, 0x00, y));   /* 0C98 mov a,($00)+y */
  S(0x0C9A, 3); s_movs(sp, 0x04BB, a);                    /* 0C9A mov $04BB,a */
  S(0x0C9D, 3); S_GOTO(SEQ_ADVANCE5);                     /* 0C9D jmp seq_advance5 */
}

/* ---------------------------------------------------------------------------
 * seq_echo_delay -- $0CA0, seq command $1F
 *
 * EDL from one byte, ESA = $FF - EDL*8, and then the echo buffer that ESA opens
 * is cleared byte by byte up to $FFFF -- up to 32 KB of writes through the timed
 * path, which is the longest loop in the driver after the loader's.
 * ------------------------------------------------------------------------- */
static void seq_echo_delay(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  if(call_seq_pop_x(sp, 0x0CA0, &a, &x, &y)) return;      /* 0CA0 call seq_pop_x */
  S(0x0CA3, 2); a = s_load(sp, s_adr_idy(sp, 0x00, y));   /* 0CA3 mov a,($00)+y */
  S(0x0CA5, 3);                                           /* 0CA5 call dsp_flg_20 */
  if(call_sub(sp, 0x0CA8, DSP_FLG_20)) return;
  a = sps_a(sp); x = sps_x(sp); y = sps_y(sp);
  S(0x0CA8, 3); s_movs(sp, sps_dp(sp, SPS_DSPADDR), 0x7D);/* 0CA8 !DSPADDR = EDL */
  S(0x0CAB, 1); s_imp(sp); sps_set_c(sp, false);          /* 0CAB clrc */
  S(0x0CAC, 1); a = s_lsr_a(sp, a);                       /* 0CAC lsr a */
  S(0x0CAD, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), a);   /* 0CAD !DSPDATA = a */
  S(0x0CAF, 3); s_movs(sp, sps_dp(sp, SPS_DSPADDR), 0x6D);/* 0CAF !DSPADDR = ESA */
  S(0x0CB2, 1); a = s_rol_a(sp, a);                       /* 0CB2 rol a */
  S(0x0CB3, 1); a = s_rol_a(sp, a);                       /* 0CB3 rol a */
  S(0x0CB4, 1); a = s_rol_a(sp, a);                       /* 0CB4 rol a */
  S(0x0CB5, 2); s_movs(sp, sps_dp(sp, 0x00), a);          /* 0CB5 mov $00,a */
  S(0x0CB7, 2); a = 0xFF; sps_set_zn(sp, a);              /* 0CB7 mov a,#$FF */
  S(0x0CB9, 1); s_imp(sp); sps_set_c(sp, true);           /* 0CB9 setc */
  S(0x0CBA, 2); a = s_sbc(sp, a, s_read(sp, sps_dp(sp, 0x00)));  /* 0CBA sbc a,$00 */
  S(0x0CBC, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), a);   /* 0CBC !DSPDATA = a */
  S(0x0CBE, 3); s_movs(sp, 0x04B7, a);                    /* 0CBE mov $04B7,a */
  S(0x0CC1, 1); s_imp(sp); y = a; sps_set_zn(sp, y);      /* 0CC1 mov y,a */
  S(0x0CC2, 2); a = 0x00; sps_set_zn(sp, a);              /* 0CC2 mov a,#$00 */
  S(0x0CC4, 2);                                           /* 0CC4 movw $00,ya */
  s_movw_store(sp, 0x00, (uint16_t) (a | (y << 8)));

  for(;;) {                                               /* loc_0CC6 */
    S(0x0CC6, 1); s_imp(sp); y = a; sps_set_zn(sp, y);    /* 0CC6 mov y,a */
    for(;;) {                                             /* loc_0CC7 */
      S(0x0CC7, 2); s_movs(sp, s_adr_idy(sp, 0x00, y), a);/* 0CC7 mov ($00)+y,a */
      S(0x0CC9, 1); s_imp(sp); y++; sps_set_zn(sp, y);    /* 0CC9 inc y */
      bool more = y != 0;
      S(0x0CCA, 2); s_branch(sp, more);                   /* 0CCA bne loc_0CC7 */
      if(!more) break;
    }
    S(0x0CCC, 2); s_inc_mem(sp, sps_dp(sp, 0x01));        /* 0CCC inc $01 */
    S(0x0CCE, 2); y = s_load(sp, sps_dp(sp, 0x01));       /* 0CCE mov y,$01 */
    S(0x0CD0, 2); s_cmp(sp, y, 0x00);                     /* 0CD0 cmp y,#$00 */
    bool page = !sps_z(sp);
    S(0x0CD2, 2); s_branch(sp, page);                     /* 0CD2 bne loc_0CC6 */
    if(!page) break;
  }
  S(0x0CD4, 3); S_GOTO(LOC_0B78);                         /* 0CD4 jmp loc_0B78 */
}

/* ---------------------------------------------------------------------------
 * seq_jump -- $0CD7, seq command $03
 *
 * The slot's sequence pointer is replaced outright, so A = 1 sends the
 * sequencer straight back to seq_fetch without the pointer advance.
 * ------------------------------------------------------------------------- */
static void seq_jump(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  if(call_seq_pop_x(sp, 0x0CD7, &a, &x, &y)) return;      /* 0CD7 call seq_pop_x */
  S(0x0CDA, 2); a = s_load(sp, s_adr_idy(sp, 0x00, y));   /* 0CDA mov a,($00)+y */
  S(0x0CDC, 2); s_movs(sp, s_adr_dpx(sp, 0x44, x), a);    /* 0CDC mov $44+x,a */
  S(0x0CDE, 1); s_imp(sp); y++; sps_set_zn(sp, y);        /* 0CDE inc y */
  S(0x0CDF, 2); a = s_load(sp, s_adr_idy(sp, 0x00, y));   /* 0CDF mov a,($00)+y */
  S(0x0CE1, 2); s_movs(sp, s_adr_dpx(sp, 0x54, x), a);    /* 0CE1 mov $54+x,a */
  S(0x0CE3, 2); a = 0x01; sps_set_zn(sp, a);              /* 0CE3 mov a,#$01 */
  S(0x0CE5, 1); S_PUB(); sps_ret(sp);                     /* 0CE5 ret */
}

/* ---------------------------------------------------------------------------
 * seq_call -- $0CE6, seq command $04
 *
 * A repeat count and a target: the return address and the count go on the
 * slot's own eight-deep stack ($0334/$03B4/$0434, indexed by seq_sp $D4+x) and
 * the pointer takes the target.
 * ------------------------------------------------------------------------- */
/* Entered in its middle as well: loc_0CF1, loc_0CF4. --no-cpu resolves every pc
 * through the registry, so an address the driver jumps into needs a body that
 * can start there. */
static void seq_call_at(SpcState* sp, uint16_t entry) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);
  if(entry == 0x0CF1) goto loc_0CF1;
  if(entry == 0x0CF4) goto loc_0CF4;

  if(call_seq_pop_x(sp, 0x0CE6, &a, &x, &y)) return;      /* 0CE6 call seq_pop_x */
  S(0x0CE9, 2); a = s_load(sp, s_adr_idy(sp, 0x00, y));   /* 0CE9 mov a,($00)+y */
  S(0x0CEB, 2); s_movs(sp, sps_dp(sp, 0x04), a);          /* 0CEB mov $04,a */
  S(0x0CED, 1); s_imp(sp); y++; sps_set_zn(sp, y);        /* 0CED inc y */
  S(0x0CEE, 3);                                           /* 0CEE call seq_push_return */
  if(call_sub(sp, 0x0CF1, SEQ_PUSH_RETURN)) return;
  a = sps_a(sp); x = sps_x(sp); y = sps_y(sp);
loc_0CF1:
  S(0x0CF1, 3); s_idx(sp);                                /* 0CF1 mov $0334+y,a */
  s_movs(sp, (uint16_t) (0x0334 + y), a);
loc_0CF4:
  S(0x0CF4, 2); s_inc_mem(sp, s_adr_dpx(sp, 0xD4, x));    /* 0CF4 inc $D4+x */
  S(0x0CF6, 2);                                           /* 0CF6 movw ya,$02 */
  { uint16_t ya = s_movw_load(sp, 0x02); a = (uint8_t) ya; y = (uint8_t) (ya >> 8); }
  S(0x0CF8, 2); s_movs(sp, s_adr_dpx(sp, 0x44, x), a);    /* 0CF8 mov $44+x,a */
  S(0x0CFA, 2); s_movs(sp, s_adr_dpx(sp, 0x54, x), y);    /* 0CFA mov $54+x,y */
  S(0x0CFC, 2); a = 0x01; sps_set_zn(sp, a);              /* 0CFC mov a,#$01 */
  S(0x0CFE, 1); S_PUB(); sps_ret(sp);                     /* 0CFE ret */
}

static void seq_call(SpcState* sp) { seq_call_at(sp, 0x0CE6); }
static void loc_0CF1(SpcState* sp) { seq_call_at(sp, 0x0CF1); }
static void loc_0CF4(SpcState* sp) { seq_call_at(sp, 0x0CF4); }

/* ---------------------------------------------------------------------------
 * seq_call_once -- $0CFF, seq command $21
 *
 * A target with an implied count of one. seq_push_return leaves A = the return
 * address' low byte and Z set when it is zero, so the decrement that makes the
 * stored address point *at* the event borrows into the high byte here.
 * ------------------------------------------------------------------------- */
static void seq_call_once(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  if(call_seq_pop_x(sp, 0x0CFF, &a, &x, &y)) return;      /* 0CFF call seq_pop_x */
  S(0x0D02, 3); s_movs(sp, sps_dp(sp, 0x04), 0x01);       /* 0D02 mov $04,#$01 */
  S(0x0D05, 3);                                           /* 0D05 call seq_push_return */
  if(call_sub(sp, 0x0D08, SEQ_PUSH_RETURN)) return;
  a = sps_a(sp); x = sps_x(sp); y = sps_y(sp);
  bool wrap = sps_z(sp);
  S(0x0D08, 2); s_branch(sp, wrap);                       /* 0D08 beq loc_0D0E */
  if(!wrap) {
    S(0x0D0A, 1); s_imp(sp); a--; sps_set_zn(sp, a);      /* 0D0A dec a */
    S(0x0D0B, 3); S_GOTO(LOC_0CF1);                       /* 0D0B jmp loc_0CF1 */
  }

  S(0x0D0E, 1); s_imp(sp); a--; sps_set_zn(sp, a);        /* 0D0E dec a */
  S(0x0D0F, 3); s_idx(sp);                                /* 0D0F mov $0334+y,a */
  s_movs(sp, (uint16_t) (0x0334 + y), a);
  S(0x0D12, 3); s_idx(sp);                                /* 0D12 mov a,$03B4+y */
  a = s_load(sp, (uint16_t) (0x03B4 + y));
  S(0x0D15, 1); s_imp(sp); a--; sps_set_zn(sp, a);        /* 0D15 dec a */
  S(0x0D16, 3); s_idx(sp);                                /* 0D16 mov $03B4+y,a */
  s_movs(sp, (uint16_t) (0x03B4 + y), a);
  S(0x0D19, 3); S_GOTO(LOC_0CF4);                         /* 0D19 jmp loc_0CF4 */
}

/* ---------------------------------------------------------------------------
 * seq_push_return -- $0D1C
 *
 * Reads the target word into $02/$03 and writes the count and the caller's
 * sequence pointer to the slot's stack, leaving the pointer's low byte in A for
 * seq_call and seq_call_once to store and adjust.
 * ------------------------------------------------------------------------- */
static void seq_push_return(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  S(0x0D1C, 2); a = s_load(sp, s_adr_idy(sp, 0x00, y));   /* 0D1C mov a,($00)+y */
  S(0x0D1E, 2); s_movs(sp, sps_dp(sp, 0x02), a);          /* 0D1E mov $02,a */
  S(0x0D20, 1); s_imp(sp); y++; sps_set_zn(sp, y);        /* 0D20 inc y */
  S(0x0D21, 2); a = s_load(sp, s_adr_idy(sp, 0x00, y));   /* 0D21 mov a,($00)+y */
  S(0x0D23, 2); s_movs(sp, sps_dp(sp, 0x03), a);          /* 0D23 mov $03,a */
  S(0x0D25, 2); y = s_load(sp, s_adr_dpx(sp, 0xD4, x));   /* 0D25 mov y,$D4+x */
  S(0x0D27, 2); a = s_load(sp, sps_dp(sp, 0x04));         /* 0D27 mov a,$04 */
  S(0x0D29, 3); s_idx(sp);                                /* 0D29 mov $0434+y,a */
  s_movs(sp, (uint16_t) (0x0434 + y), a);
  S(0x0D2C, 2); a = s_load(sp, s_adr_dpx(sp, 0x54, x));   /* 0D2C mov a,$54+x */
  S(0x0D2E, 3); s_idx(sp);                                /* 0D2E mov $03B4+y,a */
  s_movs(sp, (uint16_t) (0x03B4 + y), a);
  S(0x0D31, 2); a = s_load(sp, s_adr_dpx(sp, 0x44, x));   /* 0D31 mov a,$44+x */
  S(0x0D33, 1); S_PUB(); sps_ret(sp);                     /* 0D33 ret */
}

/* ---------------------------------------------------------------------------
 * seq_return -- $0D34, seq command $05
 *
 * Pop the slot's stack. While the count is still non-zero the same call is
 * re-entered -- the stored pointer is re-read for its target word and pushed
 * again -- so this doubles as the loop end; when it reaches zero the sequence
 * carries on after the call through loc_0D6A.
 * ------------------------------------------------------------------------- */
/* Entered in its middle as well: loc_0D6A. --no-cpu resolves every pc
 * through the registry, so an address the driver jumps into needs a body that
 * can start there. */
static void seq_return_at(SpcState* sp, uint16_t entry) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);
  if(entry == 0x0D6A) goto loc_0D6A;

  if(call_seq_pop_x(sp, 0x0D34, &a, &x, &y)) return;      /* 0D34 call seq_pop_x */
  S(0x0D37, 2); s_dec_mem(sp, s_adr_dpx(sp, 0xD4, x));    /* 0D37 dec $D4+x */
  S(0x0D39, 2); y = s_load(sp, s_adr_dpx(sp, 0xD4, x));   /* 0D39 mov y,$D4+x */
  S(0x0D3B, 3); s_idx(sp);                                /* 0D3B mov a,$03B4+y */
  a = s_load(sp, (uint16_t) (0x03B4 + y));
  S(0x0D3E, 2); s_movs(sp, s_adr_dpx(sp, 0x54, x), a);    /* 0D3E mov $54+x,a */
  S(0x0D40, 3); s_idx(sp);                                /* 0D40 mov a,$0334+y */
  a = s_load(sp, (uint16_t) (0x0334 + y));
  S(0x0D43, 2); s_movs(sp, s_adr_dpx(sp, 0x44, x), a);    /* 0D43 mov $44+x,a */
  S(0x0D45, 3); s_idx(sp);                                /* 0D45 mov a,$0434+y */
  a = s_load(sp, (uint16_t) (0x0434 + y));
  S(0x0D48, 1); s_imp(sp); a--; sps_set_zn(sp, a);        /* 0D48 dec a */
  S(0x0D49, 3); s_idx(sp);                                /* 0D49 mov $0434+y,a */
  s_movs(sp, (uint16_t) (0x0434 + y), a);
  bool done = sps_z(sp);
  S(0x0D4C, 2); s_branch(sp, done);                       /* 0D4C beq loc_0D6A */
  if(done) {
loc_0D6A:
    S(0x0D6A, 3); s_movs(sp, sps_dp(sp, 0x00), 0x04);     /* 0D6A mov $00,#$04 */
    S(0x0D6D, 3); S_GOTO(LOC_0B7B);                       /* 0D6D jmp loc_0B7B */
  }

  S(0x0D4E, 2); a = s_load(sp, s_adr_dpx(sp, 0x44, x));   /* 0D4E mov a,$44+x */
  S(0x0D50, 2); y = s_load(sp, s_adr_dpx(sp, 0x54, x));   /* 0D50 mov y,$54+x */
  S(0x0D52, 2);                                           /* 0D52 movw $00,ya */
  s_movw_store(sp, 0x00, (uint16_t) (a | (y << 8)));
  S(0x0D54, 2); y = 0x02; sps_set_zn(sp, y);              /* 0D54 mov y,#$02 */
  S(0x0D56, 2); a = s_load(sp, s_adr_idy(sp, 0x00, y));   /* 0D56 mov a,($00)+y */
  S(0x0D58, 2); s_movs(sp, sps_dp(sp, 0x02), a);          /* 0D58 mov $02,a */
  S(0x0D5A, 1); s_imp(sp); y++; sps_set_zn(sp, y);        /* 0D5A inc y */
  S(0x0D5B, 2); a = s_load(sp, s_adr_idy(sp, 0x00, y));   /* 0D5B mov a,($00)+y */
  S(0x0D5D, 2); s_movs(sp, sps_dp(sp, 0x03), a);          /* 0D5D mov $03,a */
  S(0x0D5F, 2); s_inc_mem(sp, s_adr_dpx(sp, 0xD4, x));    /* 0D5F inc $D4+x */
  S(0x0D61, 2);                                           /* 0D61 movw ya,$02 */
  { uint16_t ya = s_movw_load(sp, 0x02); a = (uint8_t) ya; y = (uint8_t) (ya >> 8); }
  S(0x0D63, 2); s_movs(sp, s_adr_dpx(sp, 0x44, x), a);    /* 0D63 mov $44+x,a */
  S(0x0D65, 2); s_movs(sp, s_adr_dpx(sp, 0x54, x), y);    /* 0D65 mov $54+x,y */
  S(0x0D67, 2); a = 0x01; sps_set_zn(sp, a);              /* 0D67 mov a,#$01 */
  S(0x0D69, 1); S_PUB(); sps_ret(sp);                     /* 0D69 ret */
}

static void seq_return(SpcState* sp) { seq_return_at(sp, 0x0D34); }
static void loc_0D6A(SpcState* sp) { seq_return_at(sp, 0x0D6A); }

/* ---------------------------------------------------------------------------
 * seq_set_length -- $0D70, seq command $06
 *
 * The default note length, and with gate_mode set a second byte that separates
 * the sounding part from the whole. The event length is computed from Y rather
 * than being a constant, which is why the tail is entered at loc_0B7B.
 * ------------------------------------------------------------------------- */
static void seq_set_length(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  if(call_seq_pop_x(sp, 0x0D70, &a, &x, &y)) return;      /* 0D70 call seq_pop_x */
  S(0x0D73, 2); a = s_load(sp, s_adr_idy(sp, 0x00, y));   /* 0D73 mov a,($00)+y */
  S(0x0D75, 3); s_idx(sp);                                /* 0D75 mov $0120+x,a */
  s_movs(sp, (uint16_t) (0x0120 + x), a);
  S(0x0D78, 3); s_idx(sp);                                /* 0D78 mov a,$01D0+x */
  a = s_load(sp, (uint16_t) (0x01D0 + x));
  bool plain = sps_z(sp);
  S(0x0D7B, 2); s_branch(sp, plain);                      /* 0D7B beq loc_0D89 */
  if(!plain) {
    S(0x0D7D, 3); s_idx(sp);                              /* 0D7D mov a,$0120+x */
    a = s_load(sp, (uint16_t) (0x0120 + x));
    S(0x0D80, 3); s_idx(sp);                              /* 0D80 mov $0130+x,a */
    s_movs(sp, (uint16_t) (0x0130 + x), a);
    S(0x0D83, 1); s_imp(sp); y++; sps_set_zn(sp, y);      /* 0D83 inc y */
    S(0x0D84, 2); a = s_load(sp, s_adr_idy(sp, 0x00, y)); /* 0D84 mov a,($00)+y */
    S(0x0D86, 3); s_idx(sp);                              /* 0D86 mov $0120+x,a */
    s_movs(sp, (uint16_t) (0x0120 + x), a);
  }
  S(0x0D89, 1); s_imp(sp); y++; sps_set_zn(sp, y);        /* 0D89 inc y */
  S(0x0D8A, 2); s_movs(sp, sps_dp(sp, 0x00), y);          /* 0D8A mov $00,y */
  S(0x0D8C, 3); S_GOTO(LOC_0B7B);                         /* 0D8C jmp loc_0B7B */
}

/* ---------------------------------------------------------------------------
 * seq_clear_length -- $0D8F, seq command $07
 *
 * Back to lengths inline after each note. It takes X off the stack itself
 * rather than through seq_pop_x, so the retrigger is loc_0DDF's job.
 * ------------------------------------------------------------------------- */
static void seq_clear_length(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  S(0x0D8F, 1); x = s_pop(sp);                            /* 0D8F pop x */
  S(0x0D90, 2); a = 0x00; sps_set_zn(sp, a);              /* 0D90 mov a,#$00 */
  S(0x0D92, 3); s_idx(sp);                                /* 0D92 mov $0120+x,a */
  s_movs(sp, (uint16_t) (0x0120 + x), a);
  S(0x0D95, 3); s_idx(sp);                                /* 0D95 mov $0130+x,a */
  s_movs(sp, (uint16_t) (0x0130 + x), a);
  S(0x0D98, 3); S_GOTO(LOC_0DDF);                         /* 0D98 jmp loc_0DDF */
}

/* loc_0DAA -- the body of the pitch slide, which seq_slide_up branches into and
 * seq_slide_down falls into: delta, the slide flag, delay, rate, steps and hold
 * out of a six-byte event. seq_retrigger leaves Y = 1, which is what makes the
 * reads that follow start at the event's second byte. */
static void tail_0DAA(SpcState* sp, uint8_t a, uint8_t x, uint8_t y) {
  S(0x0DAA, 3); s_idx(sp);                                /* 0DAA mov $01B0+x,a */
  s_movs(sp, (uint16_t) (0x01B0 + x), a);
  S(0x0DAD, 3); s_idx(sp);                                /* 0DAD mov a,$0150+x */
  a = s_load(sp, (uint16_t) (0x0150 + x));
  S(0x0DB0, 2); a = s_or(sp, a, 0x01);                    /* 0DB0 or a,#$01 */
  S(0x0DB2, 3); s_idx(sp);                                /* 0DB2 mov $0150+x,a */
  s_movs(sp, (uint16_t) (0x0150 + x), a);
  S(0x0DB5, 3);                                           /* 0DB5 call seq_retrigger */
  if(call_sub(sp, 0x0DB8, SEQ_RETRIGGER)) return;
  a = sps_a(sp); x = sps_x(sp); y = sps_y(sp);
  S(0x0DB8, 2); a = s_load(sp, s_adr_idy(sp, 0x00, y));   /* 0DB8 mov a,($00)+y */
  S(0x0DBA, 3); s_idx(sp);                                /* 0DBA mov $0160+x,a */
  s_movs(sp, (uint16_t) (0x0160 + x), a);
  S(0x0DBD, 1); s_imp(sp); y++; sps_set_zn(sp, y);        /* 0DBD inc y */
  S(0x0DBE, 2); a = s_load(sp, s_adr_idy(sp, 0x00, y));   /* 0DBE mov a,($00)+y */
  S(0x0DC0, 3); s_idx(sp);                                /* 0DC0 mov $0170+x,a */
  s_movs(sp, (uint16_t) (0x0170 + x), a);
  S(0x0DC3, 1); s_imp(sp); y++; sps_set_zn(sp, y);        /* 0DC3 inc y */
  S(0x0DC4, 2); a = s_load(sp, s_adr_idy(sp, 0x00, y));   /* 0DC4 mov a,($00)+y */
  S(0x0DC6, 3); s_idx(sp);                                /* 0DC6 mov $0180+x,a */
  s_movs(sp, (uint16_t) (0x0180 + x), a);
  S(0x0DC9, 1); s_imp(sp); y++; sps_set_zn(sp, y);        /* 0DC9 inc y */
  S(0x0DCA, 1); s_imp(sp); y++; sps_set_zn(sp, y);        /* 0DCA inc y */
  S(0x0DCB, 2); a = s_load(sp, s_adr_idy(sp, 0x00, y));   /* 0DCB mov a,($00)+y */
  S(0x0DCD, 3); s_idx(sp);                                /* 0DCD mov $0190+x,a */
  s_movs(sp, (uint16_t) (0x0190 + x), a);
  S(0x0DD0, 3); s_movs(sp, sps_dp(sp, 0x00), 0x06);       /* 0DD0 mov $00,#$06 */
  S(0x0DD3, 3); S_GOTO(LOC_0B7B);                         /* 0DD3 jmp loc_0B7B */
}

/* ---------------------------------------------------------------------------
 * seq_slide_up -- $0D9B, seq command $08
 * ------------------------------------------------------------------------- */
static void seq_slide_up(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  S(0x0D9B, 1); x = s_pop(sp);                            /* 0D9B pop x */
  S(0x0D9C, 2); y = 0x04; sps_set_zn(sp, y);              /* 0D9C mov y,#$04 */
  S(0x0D9E, 2); a = s_load(sp, s_adr_idy(sp, 0x00, y));   /* 0D9E mov a,($00)+y */
  S(0x0DA0, 2); s_branch(sp, true);                       /* 0DA0 bra loc_0DAA */
  tail_0DAA(sp, a, x, y);
}

/* ---------------------------------------------------------------------------
 * seq_slide_down -- $0DA2, seq command $09
 *
 * The same event with the delta negated, falling into loc_0DAA.
 * ------------------------------------------------------------------------- */
static void seq_slide_down(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  S(0x0DA2, 1); x = s_pop(sp);                            /* 0DA2 pop x */
  S(0x0DA3, 2); y = 0x04; sps_set_zn(sp, y);              /* 0DA3 mov y,#$04 */
  S(0x0DA5, 2); a = s_load(sp, s_adr_idy(sp, 0x00, y));   /* 0DA5 mov a,($00)+y */
  S(0x0DA7, 2); a = s_eor(sp, a, 0xFF);                   /* 0DA7 eor a,#$FF */
  S(0x0DA9, 1); s_imp(sp); a++; sps_set_zn(sp, a);        /* 0DA9 inc a */
  tail_0DAA(sp, a, x, y);                                 /* falls into loc_0DAA */
}

/* ---------------------------------------------------------------------------
 * seq_slide_off -- $0DD6, seq command $0A
 *
 * Clears the slide flag and falls into loc_0DDF, the one-byte-event tail that
 * seq_clear_length and seq_vibrato_off jump to.
 * ------------------------------------------------------------------------- */
/* Entered in its middle as well: loc_0DDF. --no-cpu resolves every pc
 * through the registry, so an address the driver jumps into needs a body that
 * can start there. */
static void seq_slide_off_at(SpcState* sp, uint16_t entry) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);
  if(entry == 0x0DDF) goto loc_0DDF;

  S(0x0DD6, 1); x = s_pop(sp);                            /* 0DD6 pop x */
  S(0x0DD7, 3); s_idx(sp);                                /* 0DD7 mov a,$0150+x */
  a = s_load(sp, (uint16_t) (0x0150 + x));
  S(0x0DDA, 2); a = s_and(sp, a, 0xFE);                   /* 0DDA and a,#$FE */
  S(0x0DDC, 3); s_idx(sp);                                /* 0DDC mov $0150+x,a */
  s_movs(sp, (uint16_t) (0x0150 + x), a);
loc_0DDF:
  S(0x0DDF, 2); a = 0x01; sps_set_zn(sp, a);              /* 0DDF mov a,#$01 */
  S(0x0DE1, 2); s_movs(sp, sps_dp(sp, 0x00), a);          /* 0DE1 mov $00,a */
  S(0x0DE3, 2); s_movs(sp, s_adr_dpx(sp, 0x34, x), a);    /* 0DE3 mov $34+x,a */
  S(0x0DE5, 1); s_imp(sp); a--; sps_set_zn(sp, a);        /* 0DE5 dec a */
  S(0x0DE6, 2); s_movs(sp, s_adr_dpx(sp, 0x24, x), a);    /* 0DE6 mov $24+x,a */
  S(0x0DE8, 3); S_GOTO(LOC_0B7B);                         /* 0DE8 jmp loc_0B7B */
}

static void seq_slide_off(SpcState* sp) { seq_slide_off_at(sp, 0x0DD6); }
static void loc_0DDF(SpcState* sp) { seq_slide_off_at(sp, 0x0DDF); }

/* ---------------------------------------------------------------------------
 * seq_tempo -- $0DEB, seq command $0B
 *
 * The tick accumulator's increment ($1F), which tick_wait adds up eighty times
 * a second to decide when the sequencer advances. loc_0DF2 is its tail.
 * ------------------------------------------------------------------------- */
/* Entered in its middle as well: loc_0DF2. --no-cpu resolves every pc
 * through the registry, so an address the driver jumps into needs a body that
 * can start there. */
static void seq_tempo_at(SpcState* sp, uint16_t entry) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);
  if(entry == 0x0DF2) goto loc_0DF2;

  S(0x0DEB, 1); x = s_pop(sp);                            /* 0DEB pop x */
  S(0x0DEC, 2); y = 0x01; sps_set_zn(sp, y);              /* 0DEC mov y,#$01 */
  S(0x0DEE, 2); a = s_load(sp, s_adr_idy(sp, 0x00, y));   /* 0DEE mov a,($00)+y */
  S(0x0DF0, 2); s_movs(sp, sps_dp(sp, 0x1F), a);          /* 0DF0 mov $1F,a */
loc_0DF2:
  S(0x0DF2, 3);                                           /* 0DF2 call seq_retrigger */
  if(call_sub(sp, 0x0DF5, SEQ_RETRIGGER)) return;
  a = sps_a(sp); x = sps_x(sp); y = sps_y(sp);
  S(0x0DF5, 3); S_GOTO(LOC_0B78);                         /* 0DF5 jmp loc_0B78 */
}

static void seq_tempo(SpcState* sp) { seq_tempo_at(sp, 0x0DEB); }
static void loc_0DF2(SpcState* sp) { seq_tempo_at(sp, 0x0DF2); }

/* ---------------------------------------------------------------------------
 * seq_tempo_add -- $0DF8, seq command $0C
 * ------------------------------------------------------------------------- */
static void seq_tempo_add(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  S(0x0DF8, 1); x = s_pop(sp);                            /* 0DF8 pop x */
  S(0x0DF9, 2); y = 0x01; sps_set_zn(sp, y);              /* 0DF9 mov y,#$01 */
  S(0x0DFB, 2); a = s_load(sp, s_adr_idy(sp, 0x00, y));   /* 0DFB mov a,($00)+y */
  S(0x0DFD, 1); s_imp(sp); sps_set_c(sp, false);          /* 0DFD clrc */
  S(0x0DFE, 2); a = s_adc(sp, a, s_read(sp, sps_dp(sp, 0x1F)));  /* 0DFE adc a,$1F */
  S(0x0E00, 2); s_movs(sp, sps_dp(sp, 0x1F), a);          /* 0E00 mov $1F,a */
  S(0x0E02, 3); S_GOTO(LOC_0DF2);                         /* 0E02 jmp loc_0DF2 */
}

/* ---------------------------------------------------------------------------
 * seq_vibrato_off -- $0E05, seq command $0E
 * ------------------------------------------------------------------------- */
static void seq_vibrato_off(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  S(0x0E05, 1); x = s_pop(sp);                            /* 0E05 pop x */
  S(0x0E06, 3); s_idx(sp);                                /* 0E06 mov a,$0150+x */
  a = s_load(sp, (uint16_t) (0x0150 + x));
  S(0x0E09, 2); a = s_and(sp, a, 0xFD);                   /* 0E09 and a,#$FD */
  S(0x0E0B, 3); s_idx(sp);                                /* 0E0B mov $0150+x,a */
  s_movs(sp, (uint16_t) (0x0150 + x), a);
  S(0x0E0E, 3); S_GOTO(LOC_0DDF);                         /* 0E0E jmp loc_0DDF */
}

/* ---------------------------------------------------------------------------
 * seq_vibrato -- $0E11, seq command $0D
 *
 * Rate, speed and depth with no delay, so A = 0 goes in as the delay.
 * ------------------------------------------------------------------------- */
static void seq_vibrato(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  S(0x0E11, 1); x = s_pop(sp);                            /* 0E11 pop x */
  S(0x0E12, 2); a = 0x00; sps_set_zn(sp, a);              /* 0E12 mov a,#$00 */
  S(0x0E14, 3);                                           /* 0E14 call seq_read_vibrato */
  if(call_sub(sp, 0x0E17, SEQ_READ_VIB)) return;
  a = sps_a(sp); x = sps_x(sp); y = sps_y(sp);
  S(0x0E17, 3); S_GOTO(LOC_0D6A);                         /* 0E17 jmp loc_0D6A */
}

/* ---------------------------------------------------------------------------
 * seq_vibrato_delay -- $0E1A, seq command $0F
 *
 * The same with the delay as the event's fourth byte, read before the call.
 * ------------------------------------------------------------------------- */
static void seq_vibrato_delay(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  S(0x0E1A, 1); x = s_pop(sp);                            /* 0E1A pop x */
  S(0x0E1B, 2); y = 0x04; sps_set_zn(sp, y);              /* 0E1B mov y,#$04 */
  S(0x0E1D, 2); a = s_load(sp, s_adr_idy(sp, 0x00, y));   /* 0E1D mov a,($00)+y */
  S(0x0E1F, 3);                                           /* 0E1F call seq_read_vibrato */
  if(call_sub(sp, 0x0E22, SEQ_READ_VIB)) return;
  a = sps_a(sp); x = sps_x(sp); y = sps_y(sp);
  S(0x0E22, 3); S_GOTO(SEQ_ADVANCE5);                     /* 0E22 jmp seq_advance5 */
}

/* ---------------------------------------------------------------------------
 * seq_read_vibrato -- $0E25
 *
 * The delay in A, the vibrato flag, and then rate, speed and depth out of the
 * event -- again from its second byte, because seq_retrigger leaves Y = 1.
 * ------------------------------------------------------------------------- */
static void seq_read_vibrato(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  S(0x0E25, 3); s_idx(sp);                                /* 0E25 mov $0220+x,a */
  s_movs(sp, (uint16_t) (0x0220 + x), a);
  S(0x0E28, 3); s_idx(sp);                                /* 0E28 mov a,$0150+x */
  a = s_load(sp, (uint16_t) (0x0150 + x));
  S(0x0E2B, 2); a = s_or(sp, a, 0x02);                    /* 0E2B or a,#$02 */
  S(0x0E2D, 3); s_idx(sp);                                /* 0E2D mov $0150+x,a */
  s_movs(sp, (uint16_t) (0x0150 + x), a);
  S(0x0E30, 3);                                           /* 0E30 call seq_retrigger */
  if(call_sub(sp, 0x0E33, SEQ_RETRIGGER)) return;
  a = sps_a(sp); x = sps_x(sp); y = sps_y(sp);
  S(0x0E33, 2); a = s_load(sp, s_adr_idy(sp, 0x00, y));   /* 0E33 mov a,($00)+y */
  S(0x0E35, 3); s_idx(sp);                                /* 0E35 mov $0200+x,a */
  s_movs(sp, (uint16_t) (0x0200 + x), a);
  S(0x0E38, 1); s_imp(sp); y++; sps_set_zn(sp, y);        /* 0E38 inc y */
  S(0x0E39, 2); a = s_load(sp, s_adr_idy(sp, 0x00, y));   /* 0E39 mov a,($00)+y */
  S(0x0E3B, 3); s_idx(sp);                                /* 0E3B mov $0210+x,a */
  s_movs(sp, (uint16_t) (0x0210 + x), a);
  S(0x0E3E, 1); s_imp(sp); y++; sps_set_zn(sp, y);        /* 0E3E inc y */
  S(0x0E3F, 2); a = s_load(sp, s_adr_idy(sp, 0x00, y));   /* 0E3F mov a,($00)+y */
  S(0x0E41, 3); s_idx(sp);                                /* 0E41 mov $0234+x,a */
  s_movs(sp, (uint16_t) (0x0234 + x), a);
  S(0x0E44, 1); S_PUB(); sps_ret(sp);                     /* 0E44 ret */
}

static const SpcRecompEntry kSeqOpsA[] = {
  { 0x0b72, "seq_instrument",        seq_instrument },
  { 0x0b78, "loc_0B78",           loc_0B78 },
  { 0x0b7b, "loc_0B7B",           loc_0B7B },
  { 0x0b8b, "seq_load_srcn",         seq_load_srcn },
  { 0x0b97, "seq_instr_full",        seq_instr_full },
  { 0x0bb6, "seq_volume",            seq_volume },
  { 0x0bbc, "loc_0BBC",           loc_0BBC },
  { 0x0bc2, "seq_read_volume",       seq_read_volume },
  { 0x0bcc, "seq_read_volume_r",     seq_read_volume_r },
  { 0x0bf0, "seq_volume_mono",       seq_volume_mono },
  { 0x0c02, "seq_volume_preset",     seq_volume_preset },
  { 0x0c18, "orphan_volume_preset2", orphan_volume_preset2 },
  { 0x0c4e, "seq_master_percent",    seq_master_percent },
  { 0x0c59, "scale_volume",          scale_volume },
  { 0x0c83, "seq_volume_presets",    seq_volume_presets },
  { 0x0ca0, "seq_echo_delay",        seq_echo_delay },
  { 0x0cd7, "seq_jump",              seq_jump },
  { 0x0ce6, "seq_call",              seq_call },
  { 0x0cf1, "loc_0CF1",           loc_0CF1 },
  { 0x0cf4, "loc_0CF4",           loc_0CF4 },
  { 0x0cff, "seq_call_once",         seq_call_once },
  { 0x0d1c, "seq_push_return",       seq_push_return },
  { 0x0d34, "seq_return",            seq_return },
  { 0x0d6a, "loc_0D6A",           loc_0D6A },
  { 0x0d70, "seq_set_length",        seq_set_length },
  { 0x0d8f, "seq_clear_length",      seq_clear_length },
  { 0x0d9b, "seq_slide_up",          seq_slide_up },
  { 0x0da2, "seq_slide_down",        seq_slide_down },
  { 0x0dd6, "seq_slide_off",         seq_slide_off },
  { 0x0ddf, "loc_0DDF",           loc_0DDF },
  { 0x0deb, "seq_tempo",             seq_tempo },
  { 0x0df2, "loc_0DF2",           loc_0DF2 },
  { 0x0df8, "seq_tempo_add",         seq_tempo_add },
  { 0x0e05, "seq_vibrato_off",       seq_vibrato_off },
  { 0x0e11, "seq_vibrato",           seq_vibrato },
  { 0x0e1a, "seq_vibrato_delay",     seq_vibrato_delay },
  { 0x0e25, "seq_read_vibrato",      seq_read_vibrato },
};
RECOMP_SPC_REGISTER(kSeqOpsA)
