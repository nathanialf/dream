/* The tick, the channel walk and the sequencer, SPC $0781-$0B71 and $112A
 * (spc/driver.asm).
 *
 * tick_wait is the driver's heartbeat: with the play flag set it programs timer
 * 0 from $E4 and spins on $FD until the tick lands, then folds the two tempo
 * accumulators into the music and sfx tick flags and walks the eight channels.
 * Each channel gets either channel_update (the per-tick slide/vibrato/tremolo
 * engine) or seq_step, and every channel that carries a sound effect gets the
 * same pair again on voice x|8. seq_step counts the note down, keys the voice
 * off at the gate and, when the note runs out, hands over to seq_fetch, which
 * reads the next event byte: a byte below $80 is a sequence command dispatched
 * through `jmp (seq_cmd_table+x)`, anything else is a note for seq_note.
 *
 * The dispatch is modelled rather than shortcut -- the vector is read out of
 * ARAM where the SPC700's own operand fetch reads it, and the body leaves by
 * pointing the pc at it, so the handler's hook fires and is credited exactly as
 * the ROM's `jmp` would have reached it. Every `call` here goes the same way:
 * the return address the opcode would have pushed is pushed by hand and the
 * callee runs on the reference SPC, so channel_update, seq_step, seq_note and
 * scale_volume ($0C59) reach their own C bodies through the registry, exactly
 * as the ROM's `call` would have reached the routine.
 *
 * Timer 0's period is the whole point of the routine, so every instruction is
 * modelled: a body that computed the tick instead of spending it would move the
 * spin loop and with it every note in the song.
 */
#include <stdint.h>
#include <stdbool.h>

#include "spc_state.h"
#include "spc_time.h"

#define MAIN_LOOP        0x0683
#define SEQ_STEP         0x0813
#define SEQ_FETCH        0x0850
#define SEQ_NOTE         0x0867
#define SEQ_NOTE_LENGTH  0x0983
#define CHANNEL_UPDATE   0x09BC
#define SCALE_VOLUME     0x0C59   /* seq_ops_a.c; reached through the registry */
#define SEQ_CMD_TABLE    0x0FD8   /* the 51-entry jump table seq_fetch indexes */

/* `call abs` (case 0x3f) minus the three fetches the step supplies: an idle,
 * the return address pushed, two more idles, then the callee runs on the
 * reference SPC so any hook it hits still fires. Returns true when the catch-up
 * slice ended inside the callee, which leaves the callee running -- safe
 * because the address pushed is the real return address, so its own `ret` lands
 * where the driver expects. */
static bool call_hooked(SpcState* sp, uint16_t ret_addr, uint16_t callee) {
  sps_idle(sp);
  uint8_t sp0 = sps_sp(sp);
  sps_push16(sp, ret_addr);
  sps_idle(sp);
  sps_idle(sp);
  sps_set_pc(sp, callee);
  return sps_run_callee(sp, sp0);
}

/* ---------------------------------------------------------------------------
 * tick_wait — $0781
 *
 * The tail every command handler jumps back to. Not playing: straight back to
 * main_loop. Playing: timer 0 gets the song's period from $E4, and $FD is
 * polled until it fires. The two tempo accumulators ($1E+$1F and $21+$22) are
 * added into themselves and the carry rotated into $20 / $23, which is how one
 * timer tick becomes a music tick and an sfx tick at different rates. Falls
 * into channel_loop with X = 0.
 * ------------------------------------------------------------------------- */
/* Entered in its middle as well: loc_078E. --no-cpu resolves every pc
 * through the registry, so an address the driver jumps into needs a body that
 * can start there. */
static void tick_wait_at(SpcState* sp, uint16_t entry) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);
  if(entry == 0x078E) goto loc_078E;

  S(0x0781, 2); a = s_load(sp, sps_dp(sp, 0x1C));       /* 0781 mov a,$1C */
  bool playing = !sps_z(sp);
  S(0x0783, 2); s_branch(sp, playing);                  /* 0783 bne loc_0788 */
  if(!playing) {
    S(0x0785, 3);                                       /* 0785 jmp main_loop */
    S_GOTO(MAIN_LOOP);
  }
  /* loc_0788 */
  S(0x0788, 2);                                         /* 0788 mov !T0TARGET,$E4 */
  { uint8_t v = s_dpdp_src(sp, 0xE4);
    sps_write8(sp, sps_dp(sp, SPS_T0TARGET), v); }
  S(0x078B, 3); s_movs(sp, sps_dp(sp, SPS_CONTROL), 0x01); /* 078B mov !CONTROL,#$01 */
  for(;;) {                                             /* loc_078E */
loc_078E:
    S(0x078E, 2); a = s_load(sp, sps_dp(sp, SPS_T0OUT));/* 078E mov a,!T0OUT */
    bool wait = sps_z(sp);
    S(0x0790, 2); s_branch(sp, wait);                   /* 0790 beq loc_078E */
    if(!wait) break;
  }
  S(0x0792, 3); s_movs(sp, sps_dp(sp, SPS_CONTROL), 0x01); /* 0792 mov !CONTROL,#$01 */
  S(0x0795, 3); s_movs(sp, sps_dp(sp, 0x20), 0x00);     /* 0795 mov $20,#$00 */
  S(0x0798, 1); s_imp(sp); sps_set_c(sp, false);        /* 0798 clrc */
  S(0x0799, 2);                                         /* 0799 adc $1E,$1F */
  { uint8_t src = s_dpdp_src(sp, 0x1F); s_adcm(sp, sps_dp(sp, 0x1E), src); }
  S(0x079C, 2); s_ror_mem(sp, sps_dp(sp, 0x20));        /* 079C ror $20 */
  S(0x079E, 3); s_movs(sp, sps_dp(sp, 0x23), 0x00);     /* 079E mov $23,#$00 */
  S(0x07A1, 1); s_imp(sp); sps_set_c(sp, false);        /* 07A1 clrc */
  S(0x07A2, 2);                                         /* 07A2 adc $21,$22 */
  { uint8_t src = s_dpdp_src(sp, 0x22); s_adcm(sp, sps_dp(sp, 0x21), src); }
  S(0x07A5, 2); s_ror_mem(sp, sps_dp(sp, 0x23));        /* 07A5 ror $23 */
  S(0x07A7, 2); x = 0x00; sps_set_zn(sp, x);            /* 07A7 mov x,#$00 */
  S_GOTO(0x07A9);                                       /* falls into channel_loop */
}

static void tick_wait(SpcState* sp) { tick_wait_at(sp, 0x0781); }
static void loc_078E(SpcState* sp) { tick_wait_at(sp, 0x078E); }

/* ---------------------------------------------------------------------------
 * channel_loop — $07A9
 *
 * X walks 0..7. On a music tick the channel is stepped through seq_step until
 * the sequencer stops returning 1 (a command handler returns 1 so that several
 * commands can be consumed in one tick); otherwise it only gets the per-tick
 * update. A channel whose sfx_override ($01E0+x) is set repeats the whole thing
 * on voice x|8 against the sfx tick flag.
 * ------------------------------------------------------------------------- */
static void channel_loop(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  for(;;) {                                             /* channel_loop */
    S(0x07A9, 2); a = s_load(sp, sps_dp(sp, 0x20));     /* 07A9 mov a,$20 */
    bool music_tick = !sps_z(sp);
    S(0x07AB, 2); s_branch(sp, music_tick);             /* 07AB bne loc_07B2 */
    if(!music_tick) {
      S(0x07AD, 3);                                     /* 07AD call channel_update */
      if(call_hooked(sp, 0x07B0, CHANNEL_UPDATE)) return;
      a = sps_a(sp); x = sps_x(sp); y = sps_y(sp);
      S(0x07B0, 2); s_branch(sp, true);                 /* 07B0 bra loc_07B7 */
    } else {
      for(;;) {                                         /* loc_07B2 */
        S(0x07B2, 3);                                   /* 07B2 call seq_step */
        if(call_hooked(sp, 0x07B5, SEQ_STEP)) return;
        a = sps_a(sp); x = sps_x(sp); y = sps_y(sp);
        bool again = !sps_z(sp);
        S(0x07B5, 2); s_branch(sp, again);              /* 07B5 bne loc_07B2 */
        if(!again) break;
      }
    }
    /* loc_07B7 */
    S(0x07B7, 3); s_idx(sp);                            /* 07B7 mov a,$01E0+x */
    a = s_load(sp, (uint16_t) (0x01E0 + x));
    bool sfx = !sps_z(sp);
    S(0x07BA, 2); s_branch(sp, !sfx);                   /* 07BA beq loc_07D0 */
    if(sfx) {
      S(0x07BC, 1); s_push(sp, x);                      /* 07BC push x */
      S(0x07BD, 1); s_imp(sp); a = x; sps_set_zn(sp, a);/* 07BD mov a,x */
      S(0x07BE, 2); a = s_or(sp, a, 0x08);              /* 07BE or a,#$08 */
      S(0x07C0, 1); s_imp(sp); x = a; sps_set_zn(sp, x);/* 07C0 mov x,a */
      S(0x07C1, 2); a = s_load(sp, sps_dp(sp, 0x23));   /* 07C1 mov a,$23 */
      bool sfx_tick = !sps_z(sp);
      S(0x07C3, 2); s_branch(sp, sfx_tick);             /* 07C3 bne loc_07CA */
      if(!sfx_tick) {
        S(0x07C5, 3);                                   /* 07C5 call channel_update */
        if(call_hooked(sp, 0x07C8, CHANNEL_UPDATE)) return;
        a = sps_a(sp); x = sps_x(sp); y = sps_y(sp);
        S(0x07C8, 2); s_branch(sp, true);               /* 07C8 bra loc_07CF */
      } else {
        for(;;) {                                       /* loc_07CA */
          S(0x07CA, 3);                                 /* 07CA call seq_step */
          if(call_hooked(sp, 0x07CD, SEQ_STEP)) return;
          a = sps_a(sp); x = sps_x(sp); y = sps_y(sp);
          bool again = !sps_z(sp);
          S(0x07CD, 2); s_branch(sp, again);            /* 07CD bne loc_07CA */
          if(!again) break;
        }
      }
      S(0x07CF, 1); x = s_pop(sp);                      /* 07CF pop x */
    }
    /* loc_07D0 */
    S(0x07D0, 1); s_imp(sp); x++; sps_set_zn(sp, x);    /* 07D0 inc x */
    S(0x07D1, 2); s_cmp(sp, x, 0x08);                   /* 07D1 cmp x,#$08 */
    bool done = sps_z(sp);
    S(0x07D3, 2); s_branch(sp, done);                   /* 07D3 beq loc_07D8 */
    if(done) break;
    S(0x07D5, 3);                                       /* 07D5 jmp channel_loop */
  }
  S(0x07D8, 3);                                         /* 07D8 jmp main_loop */
  S_GOTO(MAIN_LOOP);
}

/* ---------------------------------------------------------------------------
 * seq_step — $0813
 *
 * One sequencer tick for channel X. An inactive channel returns 0 at once.
 * Otherwise the note duration $34+x counts down: at 1 the voice is keyed off if
 * its gate has expired, at 0 or $FF the next event is fetched (or the gate
 * counter ticks instead). Every path ends through loc_084A, which runs the
 * per-tick engine and returns 0 -- "nothing more to consume this tick".
 * ------------------------------------------------------------------------- */
static void seq_step(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  S(0x0813, 3); s_idx(sp);                              /* 0813 mov a,$0110+x */
  a = s_load(sp, (uint16_t) (0x0110 + x));
  bool active = !sps_z(sp);
  S(0x0816, 2); s_branch(sp, active);                   /* 0816 bne loc_081B */
  if(!active) {
    S(0x0818, 2); a = 0x00; sps_set_zn(sp, a);          /* 0818 mov a,#$00 */
    S(0x081A, 1); S_PUB(); sps_ret(sp);                 /* 081A ret */
    return;
  }
  /* loc_081B */
  S(0x081B, 2); s_dec_mem(sp, s_adr_dpx(sp, 0x34, x));  /* 081B dec $34+x */
  S(0x081D, 2); a = s_load(sp, s_adr_dpx(sp, 0x34, x)); /* 081D mov a,$34+x */
  S(0x081F, 2); s_cmp(sp, a, 0x01);                     /* 081F cmp a,#$01 */
  bool at_gate = sps_z(sp);
  S(0x0821, 2); s_branch(sp, at_gate);                  /* 0821 beq loc_0839 */
  if(!at_gate) {
    S(0x0823, 2); s_cmp(sp, a, 0xFF);                   /* 0823 cmp a,#$FF */
    bool not_ff = !sps_z(sp);
    S(0x0825, 2); s_branch(sp, not_ff);                 /* 0825 bne loc_082F */
    if(!not_ff) {
      S(0x0827, 2); a = s_load(sp, s_adr_dpx(sp, 0x24, x)); /* 0827 mov a,$24+x */
      bool fetch = sps_z(sp);
      S(0x0829, 2); s_branch(sp, fetch);                /* 0829 beq seq_fetch */
      if(fetch) S_GOTO(SEQ_FETCH);
      S(0x082B, 2); s_dec_mem(sp, s_adr_dpx(sp, 0x24, x)); /* 082B dec $24+x */
      S(0x082D, 2); s_branch(sp, true);                 /* 082D bra loc_084A */
    } else {
      /* loc_082F */
      S(0x082F, 2); s_cmp(sp, a, 0x00);                 /* 082F cmp a,#$00 */
      bool running = !sps_z(sp);
      S(0x0831, 2); s_branch(sp, running);              /* 0831 bne loc_084A */
      if(!running) {
        S(0x0833, 2); a = s_load(sp, s_adr_dpx(sp, 0x24, x)); /* 0833 mov a,$24+x */
        bool fetch = sps_z(sp);
        S(0x0835, 2); s_branch(sp, fetch);              /* 0835 beq seq_fetch */
        if(fetch) S_GOTO(SEQ_FETCH);
        S(0x0837, 2); s_branch(sp, true);               /* 0837 bra loc_084A */
      }
    }
  } else {
    /* loc_0839 */
    S(0x0839, 2); a = s_load(sp, s_adr_dpx(sp, 0x24, x));  /* 0839 mov a,$24+x */
    bool held = !sps_z(sp);
    S(0x083B, 2); s_branch(sp, held);                   /* 083B bne loc_084A */
    if(!held) {
      S(0x083D, 3); s_idx(sp);                          /* 083D mov a,$01E0+x */
      a = s_load(sp, (uint16_t) (0x01E0 + x));
      bool overridden = !sps_z(sp);
      S(0x0840, 2); s_branch(sp, overridden);           /* 0840 bne loc_084A */
      if(!overridden) {
        S(0x0842, 3); s_idx(sp);                        /* 0842 mov a,$0FC8+x */
        a = s_load(sp, (uint16_t) (0x0FC8 + x));
        S(0x0845, 3); s_movs(sp, sps_dp(sp, SPS_DSPADDR), 0x5C); /* 0845 KOFF */
        S(0x0848, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), a);    /* 0848 */
      }
    }
  }
  /* loc_084A */
  S(0x084A, 3);                                         /* 084A call channel_update */
  if(call_hooked(sp, 0x084D, CHANNEL_UPDATE)) return;
  a = sps_a(sp); x = sps_x(sp); y = sps_y(sp);
  S(0x084D, 2); a = 0x00; sps_set_zn(sp, a);            /* 084D mov a,#$00 */
  S(0x084F, 1); S_PUB(); sps_ret(sp);                   /* 084F ret */
}

/* ---------------------------------------------------------------------------
 * seq_fetch — $0850
 *
 * Read the event byte at the channel's sequence pointer ($44/$54+x, copied into
 * the $00 pointer pair). A byte with bit 7 clear is a sequence command: X is
 * saved, the byte doubled and `jmp (seq_cmd_table+x)` reads the handler address
 * out of the table in ARAM. Anything else is a note, and after seq_note the
 * body finishes on seq_step's own tail at loc_084A -- which is where the ROM's
 * `bra loc_0865` lands.
 * ------------------------------------------------------------------------- */
static void seq_fetch(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  S(0x0850, 2); a = s_load(sp, s_adr_dpx(sp, 0x44, x)); /* 0850 mov a,$44+x */
  S(0x0852, 2); y = s_load(sp, s_adr_dpx(sp, 0x54, x)); /* 0852 mov y,$54+x */
  S(0x0854, 2);                                         /* 0854 movw $00,ya */
  s_movw_store(sp, 0x00, (uint16_t) (a | (y << 8)));
  S(0x0856, 2); y = 0x00; sps_set_zn(sp, y);            /* 0856 mov y,#$00 */
  S(0x0858, 2);                                         /* 0858 mov a,($00)+y */
  a = s_load(sp, s_adr_idy(sp, 0x00, y));
  bool note = sps_n(sp);
  S(0x085A, 2); s_branch(sp, note);                     /* 085A bmi loc_0862 */
  if(!note) {
    S(0x085C, 1); s_push(sp, x);                        /* 085C push x */
    S(0x085D, 1); a = s_asl_a(sp, a);                   /* 085D asl a */
    S(0x085E, 1); s_imp(sp); x = a; sps_set_zn(sp, x);  /* 085E mov x,a */
    S(0x085F, 3);                                       /* 085F jmp (seq_cmd_table+x) */
    uint16_t target = s_jmp_iax(sp, SEQ_CMD_TABLE, x);
    S_GOTO(target);
  }
  /* loc_0862 */
  S(0x0862, 3);                                         /* 0862 call seq_note */
  if(call_hooked(sp, 0x0865, SEQ_NOTE)) return;
  a = sps_a(sp); x = sps_x(sp); y = sps_y(sp);
  S(0x0865, 2); s_branch(sp, true);                     /* 0865 bra loc_084A */
  /* loc_084A — seq_step's tail, reached by the branch above */
  S(0x084A, 3);                                         /* 084A call channel_update */
  if(call_hooked(sp, 0x084D, CHANNEL_UPDATE)) return;
  a = sps_a(sp); x = sps_x(sp); y = sps_y(sp);
  S(0x084D, 2); a = 0x00; sps_set_zn(sp, a);            /* 084D mov a,#$00 */
  S(0x084F, 1); S_PUB(); sps_ret(sp);                   /* 084F ret */
}

/* ---------------------------------------------------------------------------
 * seq_note — $0867
 *
 * A note event. $80 is a rest: key the voice off and zero its pitch. $E0 and
 * $E1 replay the two notes the channel has stored; anything else is a note
 * number. The number plus $24 plus the channel transpose indexes pitch_table
 * ($11CC) for a 16-bit DSP pitch, and a non-zero finetune ($64+x) interpolates
 * toward the next table entry with two 8x8 multiplies. The pitch, the volumes
 * (through scale_volume), SRCN, ADSR and GAIN are written to the voice's eight
 * DSP registers in order and the voice is keyed on; the slide and vibrato state
 * is reseeded from the channel's flag byte on the way. Falls into
 * seq_note_length.
 * ------------------------------------------------------------------------- */
static void seq_note(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);
  uint16_t ya;

  S(0x0867, 2); s_cmp(sp, a, 0x80);                     /* 0867 cmp a,#$80 */
  bool not_rest = !sps_z(sp);
  S(0x0869, 2); s_branch(sp, not_rest);                 /* 0869 bne loc_088B */
  if(!not_rest) {
    S(0x086B, 3); s_idx(sp);                            /* 086B mov a,$01E0+x */
    a = s_load(sp, (uint16_t) (0x01E0 + x));
    bool overridden = !sps_z(sp);
    S(0x086E, 2); s_branch(sp, overridden);             /* 086E bne loc_0888 */
    if(!overridden) {
      S(0x0870, 3); s_idx(sp);                          /* 0870 mov a,$0FC8+x */
      a = s_load(sp, (uint16_t) (0x0FC8 + x));
      S(0x0873, 3); s_movs(sp, sps_dp(sp, SPS_DSPADDR), 0x5C); /* 0873 KOFF */
      S(0x0876, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), a);    /* 0876 */
      S(0x0878, 1); s_imp(sp); a = x; sps_set_zn(sp, a);/* 0878 mov a,x */
      S(0x0879, 2); a = s_and(sp, a, 0x07);             /* 0879 and a,#$07 */
      S(0x087B, 1); a = s_xcn(sp, a);                   /* 087B xcn a */
      S(0x087C, 2); a = s_or(sp, a, 0x02);              /* 087C or a,#$02 */
      S(0x087E, 2); s_movs(sp, sps_dp(sp, SPS_DSPADDR), a);    /* 087E */
      S(0x0880, 2); a = 0x00; sps_set_zn(sp, a);        /* 0880 mov a,#$00 */
      S(0x0882, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), a);    /* 0882 */
      S(0x0884, 2); s_inc_mem(sp, sps_dp(sp, SPS_DSPADDR));    /* 0884 */
      S(0x0886, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), a);    /* 0886 */
    }
    /* loc_0888 */
    S(0x0888, 3);                                       /* 0888 jmp seq_note_length */
    S_GOTO(SEQ_NOTE_LENGTH);
  }
  /* loc_088B */
  S(0x088B, 2); s_cmp(sp, a, 0xE0);                     /* 088B cmp a,#$E0 */
  bool plain = sps_n(sp);
  S(0x088D, 2); s_branch(sp, plain);                    /* 088D bmi loc_0899 */
  if(!plain) {
    S(0x088F, 2); s_cmp(sp, a, 0xE1);                   /* 088F cmp a,#$E1 */
    bool second = sps_z(sp);
    S(0x0891, 2); s_branch(sp, second);                 /* 0891 beq loc_0897 */
    if(!second) {
      S(0x0893, 2); a = s_load(sp, s_adr_dpx(sp, 0x0C, x)); /* 0893 mov a,$0C+x */
      S(0x0895, 2); s_branch(sp, true);                 /* 0895 bra loc_0899 */
    } else {
      S(0x0897, 2); a = s_load(sp, s_adr_dpx(sp, 0x14, x)); /* 0897 mov a,$14+x */
    }
  }
  /* loc_0899 */
  S(0x0899, 1); s_imp(sp); sps_set_c(sp, false);        /* 0899 clrc */
  S(0x089A, 2); a = s_adc(sp, a, 0x24);                 /* 089A adc a,#$24 */
  S(0x089C, 3); s_idx(sp);                              /* 089C adc a,$0140+x */
  a = s_adc(sp, a, s_read(sp, (uint16_t) (0x0140 + x)));
  S(0x089F, 1); a = s_asl_a(sp, a);                     /* 089F asl a */
  S(0x08A0, 1); s_push(sp, x);                          /* 08A0 push x */
  S(0x08A1, 2); y = s_load(sp, s_adr_dpx(sp, 0x64, x)); /* 08A1 mov y,$64+x */
  bool no_finetune = sps_z(sp);
  S(0x08A3, 2); s_branch(sp, no_finetune);              /* 08A3 beq loc_08DF */
  if(!no_finetune) {
    S(0x08A5, 1); s_imp(sp); x = a; sps_set_zn(sp, x);  /* 08A5 mov x,a */
    S(0x08A6, 2); s_movs(sp, sps_dp(sp, 0x04), y);      /* 08A6 mov $04,y */
    S(0x08A8, 1); s_imp(sp); a = y; sps_set_zn(sp, a);  /* 08A8 mov a,y */
    bool positive = !sps_n(sp);
    S(0x08A9, 2); s_branch(sp, positive);               /* 08A9 bpl loc_08AE */
    if(!positive) {
      S(0x08AB, 2); a = s_eor(sp, a, 0xFF);             /* 08AB eor a,#$FF */
      S(0x08AD, 1); s_imp(sp); a++; sps_set_zn(sp, a);  /* 08AD inc a */
    }
    /* loc_08AE */
    S(0x08AE, 1); s_imp(sp); y = a; sps_set_zn(sp, y);  /* 08AE mov y,a */
    S(0x08AF, 1); s_push(sp, y);                        /* 08AF push y */
    S(0x08B0, 3); s_idx(sp);                            /* 08B0 mov a,$11CC+x */
    a = s_load(sp, (uint16_t) (0x11CC + x));
    S(0x08B3, 1);                                       /* 08B3 mul ya */
    ya = s_mul(sp, a, y); a = (uint8_t) ya; y = (uint8_t) (ya >> 8);
    S(0x08B4, 2); s_movs(sp, sps_dp(sp, 0x02), y);      /* 08B4 mov $02,y */
    S(0x08B6, 3); s_movs(sp, sps_dp(sp, 0x03), 0x00);   /* 08B6 mov $03,#$00 */
    S(0x08B9, 1); y = s_pop(sp);                        /* 08B9 pop y */
    S(0x08BA, 3); s_idx(sp);                            /* 08BA mov a,$11CD+x */
    a = s_load(sp, (uint16_t) (0x11CD + x));
    S(0x08BD, 1);                                       /* 08BD mul ya */
    ya = s_mul(sp, a, y); a = (uint8_t) ya; y = (uint8_t) (ya >> 8);
    S(0x08BE, 2);                                       /* 08BE addw ya,$02 */
    ya = s_addw(sp, 0x02, ya); a = (uint8_t) ya; y = (uint8_t) (ya >> 8);
    S(0x08C0, 2); s_movs(sp, sps_dp(sp, 0x03), y);      /* 08C0 mov $03,y */
    S(0x08C2, 2); s_lsr_mem(sp, sps_dp(sp, 0x03));      /* 08C2 lsr $03 */
    S(0x08C4, 1); a = s_ror_a(sp, a);                   /* 08C4 ror a */
    S(0x08C5, 2); s_lsr_mem(sp, sps_dp(sp, 0x03));      /* 08C5 lsr $03 */
    S(0x08C7, 1); a = s_ror_a(sp, a);                   /* 08C7 ror a */
    S(0x08C8, 2); s_movs(sp, sps_dp(sp, 0x02), a);      /* 08C8 mov $02,a */
    S(0x08CA, 3); s_idx(sp);                            /* 08CA mov a,$11CD+x */
    a = s_load(sp, (uint16_t) (0x11CD + x));
    S(0x08CD, 1); s_imp(sp); y = a; sps_set_zn(sp, y);  /* 08CD mov y,a */
    S(0x08CE, 3); s_idx(sp);                            /* 08CE mov a,$11CC+x */
    a = s_load(sp, (uint16_t) (0x11CC + x));
    S(0x08D1, 2); x = s_load(sp, sps_dp(sp, 0x04));     /* 08D1 mov x,$04 */
    bool down = sps_n(sp);
    S(0x08D3, 2); s_branch(sp, down);                   /* 08D3 bmi loc_08D9 */
    ya = (uint16_t) (a | (y << 8));
    if(!down) {
      S(0x08D5, 2);                                     /* 08D5 addw ya,$02 */
      ya = s_addw(sp, 0x02, ya); a = (uint8_t) ya; y = (uint8_t) (ya >> 8);
      S(0x08D7, 2); s_branch(sp, true);                 /* 08D7 bra loc_08DB */
    } else {
      S(0x08D9, 2);                                     /* 08D9 subw ya,$02 */
      ya = s_subw(sp, 0x02, ya); a = (uint8_t) ya; y = (uint8_t) (ya >> 8);
    }
    /* loc_08DB */
    S(0x08DB, 2); s_movw_store(sp, 0x02, ya);           /* 08DB movw $02,ya */
    S(0x08DD, 2); s_branch(sp, true);                   /* 08DD bra loc_08EA */
  } else {
    /* loc_08DF */
    S(0x08DF, 1); s_imp(sp); x = a; sps_set_zn(sp, x);  /* 08DF mov x,a */
    S(0x08E0, 3); s_idx(sp);                            /* 08E0 mov a,$11CC+x */
    a = s_load(sp, (uint16_t) (0x11CC + x));
    S(0x08E3, 2); s_movs(sp, sps_dp(sp, 0x02), a);      /* 08E3 mov $02,a */
    S(0x08E5, 3); s_idx(sp);                            /* 08E5 mov a,$11CD+x */
    a = s_load(sp, (uint16_t) (0x11CD + x));
    S(0x08E8, 2); s_movs(sp, sps_dp(sp, 0x03), a);      /* 08E8 mov $03,a */
  }
  /* loc_08EA */
  S(0x08EA, 1); a = s_pop(sp);                          /* 08EA pop a */
  S(0x08EB, 1); s_imp(sp); x = a; sps_set_zn(sp, x);    /* 08EB mov x,a */
  S(0x08EC, 2); a = s_and(sp, a, 0x07);                 /* 08EC and a,#$07 */
  S(0x08EE, 1); a = s_xcn(sp, a);                       /* 08EE xcn a */
  S(0x08EF, 2); s_movs(sp, sps_dp(sp, SPS_DSPADDR), a); /* 08EF mov !DSPADDR,a */
  S(0x08F1, 3); s_idx(sp);                              /* 08F1 mov a,$01E0+x */
  a = s_load(sp, (uint16_t) (0x01E0 + x));
  bool own_voice = sps_z(sp);
  S(0x08F4, 2); s_branch(sp, own_voice);                /* 08F4 beq loc_08F9 */
  if(!own_voice) {
    S(0x08F6, 3);                                       /* 08F6 jmp seq_note_length */
    S_GOTO(SEQ_NOTE_LENGTH);
  }
  /* loc_08F9 */
  S(0x08F9, 3); s_idx(sp);                              /* 08F9 mov a,$0254+x */
  a = s_load(sp, (uint16_t) (0x0254 + x));
  S(0x08FC, 3);                                         /* 08FC call scale_volume */
  if(call_hooked(sp, 0x08FF, SCALE_VOLUME)) return;
  a = sps_a(sp); x = sps_x(sp); y = sps_y(sp);
  S(0x08FF, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), a); /* 08FF */
  S(0x0901, 2); s_inc_mem(sp, sps_dp(sp, SPS_DSPADDR)); /* 0901 inc !DSPADDR */
  S(0x0903, 3); s_idx(sp);                              /* 0903 mov a,$0264+x */
  a = s_load(sp, (uint16_t) (0x0264 + x));
  S(0x0906, 3);                                         /* 0906 call scale_volume */
  if(call_hooked(sp, 0x0909, SCALE_VOLUME)) return;
  a = sps_a(sp); x = sps_x(sp); y = sps_y(sp);
  S(0x0909, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), a); /* 0909 */
  S(0x090B, 2); s_inc_mem(sp, sps_dp(sp, SPS_DSPADDR)); /* 090B inc !DSPADDR */
  S(0x090D, 3); s_idx(sp);                              /* 090D mov a,$0150+x */
  a = s_load(sp, (uint16_t) (0x0150 + x));
  S(0x0910, 2); a = s_and(sp, a, 0x01);                 /* 0910 and a,#$01 */
  bool slide = !sps_z(sp);
  S(0x0912, 2); s_branch(sp, !slide);                   /* 0912 beq loc_092B */
  if(slide) {
    S(0x0914, 3); s_idx(sp);                            /* 0914 mov a,$0160+x */
    a = s_load(sp, (uint16_t) (0x0160 + x));
    S(0x0917, 3); s_idx(sp);                            /* 0917 mov $01A0+x,a */
    s_movs(sp, (uint16_t) (0x01A0 + x), a);
    S(0x091A, 3); s_idx(sp);                            /* 091A mov a,$0170+x */
    a = s_load(sp, (uint16_t) (0x0170 + x));
    S(0x091D, 3); s_idx(sp);                            /* 091D mov $0100+x,a */
    s_movs(sp, (uint16_t) (0x0100 + x), a);
    S(0x0920, 3); s_idx(sp);                            /* 0920 mov a,$0180+x */
    a = s_load(sp, (uint16_t) (0x0180 + x));
    S(0x0923, 2); s_movs(sp, s_adr_dpx(sp, 0x94, x), a);/* 0923 mov $94+x,a */
    S(0x0925, 3); s_idx(sp);                            /* 0925 mov a,$0190+x */
    a = s_load(sp, (uint16_t) (0x0190 + x));
    S(0x0928, 3); s_idx(sp);                            /* 0928 mov $01C0+x,a */
    s_movs(sp, (uint16_t) (0x01C0 + x), a);
  }
  /* loc_092B */
  S(0x092B, 3); s_idx(sp);                              /* 092B mov a,$0150+x */
  a = s_load(sp, (uint16_t) (0x0150 + x));
  S(0x092E, 2); a = s_and(sp, a, 0x02);                 /* 092E and a,#$02 */
  bool vibrato = !sps_z(sp);
  S(0x0930, 2); s_branch(sp, !vibrato);                 /* 0930 beq loc_094D */
  if(vibrato) {
    S(0x0932, 3); s_idx(sp);                            /* 0932 mov a,$0234+x */
    a = s_load(sp, (uint16_t) (0x0234 + x));
    bool positive = !sps_n(sp);
    S(0x0935, 2); s_branch(sp, positive);               /* 0935 bpl loc_093D */
    if(!positive) {
      S(0x0937, 2); a = s_eor(sp, a, 0xFF);             /* 0937 eor a,#$FF */
      S(0x0939, 1); s_imp(sp); a++; sps_set_zn(sp, a);  /* 0939 inc a */
      S(0x093A, 3); s_idx(sp);                          /* 093A mov $0234+x,a */
      s_movs(sp, (uint16_t) (0x0234 + x), a);
    }
    /* loc_093D */
    S(0x093D, 3); s_idx(sp);                            /* 093D mov a,$0200+x */
    a = s_load(sp, (uint16_t) (0x0200 + x));
    S(0x0940, 1); a = s_lsr_a(sp, a);                   /* 0940 lsr a */
    S(0x0941, 2); s_movs(sp, s_adr_dpx(sp, 0xA4, x), a);/* 0941 mov $A4+x,a */
    S(0x0943, 3); s_idx(sp);                            /* 0943 mov a,$0210+x */
    a = s_load(sp, (uint16_t) (0x0210 + x));
    S(0x0946, 2); s_movs(sp, s_adr_dpx(sp, 0xB4, x), a);/* 0946 mov $B4+x,a */
    S(0x0948, 3); s_idx(sp);                            /* 0948 mov a,$0220+x */
    a = s_load(sp, (uint16_t) (0x0220 + x));
    S(0x094B, 2); s_movs(sp, s_adr_dpx(sp, 0xC4, x), a);/* 094B mov $C4+x,a */
  }
  /* loc_094D */
  S(0x094D, 2); a = s_load(sp, sps_dp(sp, 0x02));       /* 094D mov a,$02 */
  S(0x094F, 2); s_movs(sp, s_adr_dpx(sp, 0x84, x), a);  /* 094F mov $84+x,a */
  S(0x0951, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), a); /* 0951 */
  S(0x0953, 2); s_inc_mem(sp, sps_dp(sp, SPS_DSPADDR)); /* 0953 inc !DSPADDR */
  S(0x0955, 2); a = s_load(sp, sps_dp(sp, 0x03));       /* 0955 mov a,$03 */
  S(0x0957, 2); s_movs(sp, s_adr_dpx(sp, 0x74, x), a);  /* 0957 mov $74+x,a */
  S(0x0959, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), a); /* 0959 */
  S(0x095B, 2); s_inc_mem(sp, sps_dp(sp, SPS_DSPADDR)); /* 095B inc !DSPADDR */
  S(0x095D, 3); s_idx(sp);                              /* 095D mov a,$0244+x */
  a = s_load(sp, (uint16_t) (0x0244 + x));
  S(0x0960, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), a); /* 0960 */
  S(0x0962, 2); s_inc_mem(sp, sps_dp(sp, SPS_DSPADDR)); /* 0962 inc !DSPADDR */
  S(0x0964, 3); s_idx(sp);                              /* 0964 mov a,$0274+x */
  a = s_load(sp, (uint16_t) (0x0274 + x));
  S(0x0967, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), a); /* 0967 */
  S(0x0969, 2); s_inc_mem(sp, sps_dp(sp, SPS_DSPADDR)); /* 0969 inc !DSPADDR */
  S(0x096B, 3); s_idx(sp);                              /* 096B mov a,$0284+x */
  a = s_load(sp, (uint16_t) (0x0284 + x));
  S(0x096E, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), a); /* 096E */
  S(0x0970, 2); s_inc_mem(sp, sps_dp(sp, SPS_DSPADDR)); /* 0970 inc !DSPADDR */
  S(0x0972, 3); s_movs(sp, sps_dp(sp, SPS_DSPDATA), 0x7F); /* 0972 GAIN */
  S(0x0975, 3); s_movs(sp, sps_dp(sp, SPS_DSPADDR), 0x5C); /* 0975 KOFF */
  S(0x0978, 3); s_movs(sp, sps_dp(sp, SPS_DSPDATA), 0x00); /* 0978 */
  S(0x097B, 3); s_movs(sp, sps_dp(sp, SPS_DSPADDR), 0x4C); /* 097B KON */
  S(0x097E, 3); s_idx(sp);                              /* 097E mov a,$0FC8+x */
  a = s_load(sp, (uint16_t) (0x0FC8 + x));
  S(0x0981, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), a); /* 0981 */
  S_GOTO(SEQ_NOTE_LENGTH);                              /* falls into seq_note_length */
}

/* ---------------------------------------------------------------------------
 * seq_note_length — $0983
 *
 * The duration and gate that follow a note. A non-zero note_len ($0120+x) is a
 * length the sequence set once and reuses, and costs one event byte; otherwise
 * the length is read inline from the stream, with a second byte for the gated
 * form ($01D0+x). The bytes consumed are added to the channel's sequence
 * pointer through the $00 pair.
 * ------------------------------------------------------------------------- */
static void seq_note_length(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);
  uint16_t ya;

  S(0x0983, 3); s_idx(sp);                              /* 0983 mov a,$0120+x */
  a = s_load(sp, (uint16_t) (0x0120 + x));
  bool inline_len = sps_z(sp);
  S(0x0986, 2); s_branch(sp, inline_len);               /* 0986 beq loc_0997 */
  if(!inline_len) {
    S(0x0988, 3); s_movs(sp, sps_dp(sp, 0x00), 0x01);   /* 0988 mov $00,#$01 */
    S(0x098B, 3); s_idx(sp);                            /* 098B mov a,$0120+x */
    a = s_load(sp, (uint16_t) (0x0120 + x));
    S(0x098E, 2); s_movs(sp, s_adr_dpx(sp, 0x34, x), a);/* 098E mov $34+x,a */
    S(0x0990, 3); s_idx(sp);                            /* 0990 mov a,$0130+x */
    a = s_load(sp, (uint16_t) (0x0130 + x));
    S(0x0993, 2); s_movs(sp, s_adr_dpx(sp, 0x24, x), a);/* 0993 mov $24+x,a */
    S(0x0995, 2); s_branch(sp, true);                   /* 0995 bra loc_09AE */
  } else {
    /* loc_0997 */
    S(0x0997, 2); y = 0x01; sps_set_zn(sp, y);          /* 0997 mov y,#$01 */
    S(0x0999, 2);                                       /* 0999 mov a,($00)+y */
    a = s_load(sp, s_adr_idy(sp, 0x00, y));
    S(0x099B, 2); s_movs(sp, s_adr_dpx(sp, 0x34, x), a);/* 099B mov $34+x,a */
    S(0x099D, 3); s_idx(sp);                            /* 099D mov a,$01D0+x */
    a = s_load(sp, (uint16_t) (0x01D0 + x));
    bool ungated = sps_z(sp);
    S(0x09A0, 2); s_branch(sp, ungated);                /* 09A0 beq loc_09AB */
    if(!ungated) {
      S(0x09A2, 2); a = s_load(sp, s_adr_dpx(sp, 0x34, x)); /* 09A2 mov a,$34+x */
      S(0x09A4, 2); s_movs(sp, s_adr_dpx(sp, 0x24, x), a);  /* 09A4 mov $24+x,a */
      S(0x09A6, 1); s_imp(sp); y++; sps_set_zn(sp, y);      /* 09A6 inc y */
      S(0x09A7, 2);                                         /* 09A7 mov a,($00)+y */
      a = s_load(sp, s_adr_idy(sp, 0x00, y));
      S(0x09A9, 2); s_movs(sp, s_adr_dpx(sp, 0x34, x), a);  /* 09A9 mov $34+x,a */
    }
    /* loc_09AB */
    S(0x09AB, 1); s_imp(sp); y++; sps_set_zn(sp, y);    /* 09AB inc y */
    S(0x09AC, 2); s_movs(sp, sps_dp(sp, 0x00), y);      /* 09AC mov $00,y */
  }
  /* loc_09AE */
  S(0x09AE, 3); s_movs(sp, sps_dp(sp, 0x01), 0x00);     /* 09AE mov $01,#$00 */
  S(0x09B1, 2); a = s_load(sp, s_adr_dpx(sp, 0x44, x)); /* 09B1 mov a,$44+x */
  S(0x09B3, 2); y = s_load(sp, s_adr_dpx(sp, 0x54, x)); /* 09B3 mov y,$54+x */
  S(0x09B5, 2);                                         /* 09B5 addw ya,$00 */
  ya = s_addw(sp, 0x00, (uint16_t) (a | (y << 8)));
  a = (uint8_t) ya; y = (uint8_t) (ya >> 8);
  S(0x09B7, 2); s_movs(sp, s_adr_dpx(sp, 0x54, x), y);  /* 09B7 mov $54+x,y */
  S(0x09B9, 2); s_movs(sp, s_adr_dpx(sp, 0x44, x), a);  /* 09B9 mov $44+x,a */
  S(0x09BB, 1); S_PUB(); sps_ret(sp);                   /* 09BB ret */
}

/* ---------------------------------------------------------------------------
 * channel_update — $09BC
 *
 * The per-tick modulation engine, run once for every channel on every timer
 * tick. The channel's flag byte $0150+x selects three independent stages:
 *
 *   bit 0  pitch slide: a delay ($01A0+x) then a rate counter ($0100+x); each
 *          expiry adds the signed delta $01B0+x to the 16-bit pitch in
 *          $84/$74+x, negated while the hold counter $01C0+x lasts, and the
 *          step count $94+x parks the slide at $FF when it runs out.
 *   bit 1  vibrato: delay $C4+x, step $B4+x, then the signed depth $0234+x is
 *          added to the pitch and inverted every $A4+x steps. Voice $0D, the
 *          sfx voice, also folds in the pitch offset cmd4 parked in $EC/$ED,
 *          tracking the previous value in $EE/$EF so the offset is applied as a
 *          difference rather than accumulating.
 *   bits 2-3  tremolo / volume envelope: delay $02A4+x, count $02B4+x, and the
 *          voice's two volume registers rewritten from $0254/$0264+x, with the
 *          delta $02D4+x inverted every $02F4+x steps unless bit 3 says the
 *          envelope is one-shot.
 *
 * Each stage writes the DSP only when the channel is not overridden by a sound
 * effect ($01E0+x), because the sfx voice owns those registers instead.
 * ------------------------------------------------------------------------- */
static void channel_update(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);
  uint16_t ya;
  bool t;

  S(0x09BC, 3); s_idx(sp);                              /* 09BC mov a,$0150+x */
  a = s_load(sp, (uint16_t) (0x0150 + x));
  S(0x09BF, 2); a = s_and(sp, a, 0x01);                 /* 09BF and a,#$01 */
  t = !sps_z(sp);
  S(0x09C1, 2); s_branch(sp, t);                        /* 09C1 bne loc_09C6 */
  if(!t) { S(0x09C3, 3); goto loc_0A39; }               /* 09C3 jmp loc_0A39 */

  /* loc_09C6 */
  S(0x09C6, 3); s_idx(sp);                              /* 09C6 mov a,$01A0+x */
  a = s_load(sp, (uint16_t) (0x01A0 + x));
  t = sps_z(sp);
  S(0x09C9, 2); s_branch(sp, t);                        /* 09C9 beq loc_09DA */
  if(!t) {
    S(0x09CB, 2); s_cmp(sp, a, 0xFF);                   /* 09CB cmp a,#$FF */
    t = sps_z(sp);
    S(0x09CD, 2); s_branch(sp, t);                      /* 09CD beq loc_0A39 */
    if(t) goto loc_0A39;
    S(0x09CF, 1); s_imp(sp); a--; sps_set_zn(sp, a);    /* 09CF dec a */
    t = !sps_z(sp);
    S(0x09D0, 3); s_idx(sp);                            /* 09D0 mov $01A0+x,a */
    s_movs(sp, (uint16_t) (0x01A0 + x), a);
    S(0x09D3, 2); s_branch(sp, t);                      /* 09D3 bne loc_0A39 */
    if(t) goto loc_0A39;
    S(0x09D5, 2); a = 0x01; sps_set_zn(sp, a);          /* 09D5 mov a,#$01 */
    S(0x09D7, 3); s_idx(sp);                            /* 09D7 mov $0100+x,a */
    s_movs(sp, (uint16_t) (0x0100 + x), a);
  }
  /* loc_09DA */
  S(0x09DA, 3); s_idx(sp);                              /* 09DA mov a,$0100+x */
  a = s_load(sp, (uint16_t) (0x0100 + x));
  S(0x09DD, 1); s_imp(sp); a--; sps_set_zn(sp, a);      /* 09DD dec a */
  t = !sps_z(sp);
  S(0x09DE, 3); s_idx(sp);                              /* 09DE mov $0100+x,a */
  s_movs(sp, (uint16_t) (0x0100 + x), a);
  S(0x09E1, 2); s_branch(sp, t);                        /* 09E1 bne loc_0A39 */
  if(t) goto loc_0A39;
  S(0x09E3, 3); s_idx(sp);                              /* 09E3 mov a,$0170+x */
  a = s_load(sp, (uint16_t) (0x0170 + x));
  S(0x09E6, 3); s_idx(sp);                              /* 09E6 mov $0100+x,a */
  s_movs(sp, (uint16_t) (0x0100 + x), a);
  S(0x09E9, 3); s_idx(sp);                              /* 09E9 mov a,$01C0+x */
  a = s_load(sp, (uint16_t) (0x01C0 + x));
  t = sps_z(sp);
  S(0x09EC, 2); s_branch(sp, t);                        /* 09EC beq loc_0A10 */
  if(t) goto loc_0A10;
  S(0x09EE, 1); s_imp(sp); a--; sps_set_zn(sp, a);      /* 09EE dec a */
  S(0x09EF, 3); s_idx(sp);                              /* 09EF mov $01C0+x,a */
  s_movs(sp, (uint16_t) (0x01C0 + x), a);
  S(0x09F2, 3); s_idx(sp);                              /* 09F2 mov a,$01B0+x */
  a = s_load(sp, (uint16_t) (0x01B0 + x));
  S(0x09F5, 2); a = s_eor(sp, a, 0xFF);                 /* 09F5 eor a,#$FF */
  S(0x09F7, 1); s_imp(sp); a++; sps_set_zn(sp, a);      /* 09F7 inc a */
  t = !sps_n(sp);
  S(0x09F8, 2); s_movs(sp, sps_dp(sp, 0x00), a);        /* 09F8 mov $00,a */
  S(0x09FA, 2); s_branch(sp, t);                        /* 09FA bpl loc_0A00 */
  if(t) goto loc_0A00;
  S(0x09FC, 2); a = 0xFF; sps_set_zn(sp, a);            /* 09FC mov a,#$FF */
  S(0x09FE, 2); s_branch(sp, true);                     /* 09FE bra loc_0A02 */
  goto loc_0A02;

loc_0A00:
  S(0x0A00, 2); a = 0x00; sps_set_zn(sp, a);            /* 0A00 mov a,#$00 */
loc_0A02:
  S(0x0A02, 2); s_movs(sp, sps_dp(sp, 0x01), a);        /* 0A02 mov $01,a */
  S(0x0A04, 2); a = s_load(sp, s_adr_dpx(sp, 0x84, x)); /* 0A04 mov a,$84+x */
  S(0x0A06, 2); y = s_load(sp, s_adr_dpx(sp, 0x74, x)); /* 0A06 mov y,$74+x */
  S(0x0A08, 2);                                         /* 0A08 addw ya,$00 */
  ya = s_addw(sp, 0x00, (uint16_t) (a | (y << 8)));
  a = (uint8_t) ya; y = (uint8_t) (ya >> 8);
  S(0x0A0A, 2); s_movs(sp, s_adr_dpx(sp, 0x74, x), y);  /* 0A0A mov $74+x,y */
  S(0x0A0C, 2); s_movs(sp, s_adr_dpx(sp, 0x84, x), a);  /* 0A0C mov $84+x,a */
  S(0x0A0E, 2); s_branch(sp, true);                     /* 0A0E bra loc_0A1B */
  goto loc_0A1B;

loc_0A10:
  S(0x0A10, 3); s_idx(sp);                              /* 0A10 mov a,$01B0+x */
  a = s_load(sp, (uint16_t) (0x01B0 + x));
  t = !sps_n(sp);
  S(0x0A13, 2); s_movs(sp, sps_dp(sp, 0x00), a);        /* 0A13 mov $00,a */
  S(0x0A15, 2); s_branch(sp, t);                        /* 0A15 bpl loc_0A00 */
  if(t) goto loc_0A00;
  S(0x0A17, 2); a = 0xFF; sps_set_zn(sp, a);            /* 0A17 mov a,#$FF */
  S(0x0A19, 2); s_branch(sp, true);                     /* 0A19 bra loc_0A02 */
  goto loc_0A02;

loc_0A1B:
  S(0x0A1B, 3); s_idx(sp);                              /* 0A1B mov a,$01E0+x */
  a = s_load(sp, (uint16_t) (0x01E0 + x));
  t = !sps_z(sp);
  S(0x0A1E, 2); s_branch(sp, t);                        /* 0A1E bne loc_0A30 */
  if(t) goto loc_0A30;
  S(0x0A20, 1); s_imp(sp); a = x; sps_set_zn(sp, a);    /* 0A20 mov a,x */
  S(0x0A21, 2); a = s_and(sp, a, 0x07);                 /* 0A21 and a,#$07 */
  S(0x0A23, 1); a = s_xcn(sp, a);                       /* 0A23 xcn a */
  S(0x0A24, 2); a = s_or(sp, a, 0x02);                  /* 0A24 or a,#$02 */
  S(0x0A26, 2); s_movs(sp, sps_dp(sp, SPS_DSPADDR), a); /* 0A26 mov !DSPADDR,a */
  S(0x0A28, 2); a = s_load(sp, s_adr_dpx(sp, 0x84, x)); /* 0A28 mov a,$84+x */
  S(0x0A2A, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), a); /* 0A2A */
  S(0x0A2C, 2); s_inc_mem(sp, sps_dp(sp, SPS_DSPADDR)); /* 0A2C inc !DSPADDR */
  S(0x0A2E, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), y); /* 0A2E mov !DSPDATA,y */
loc_0A30:
  S(0x0A30, 2); s_dec_mem(sp, s_adr_dpx(sp, 0x94, x));  /* 0A30 dec $94+x */
  t = !sps_z(sp);
  S(0x0A32, 2); s_branch(sp, t);                        /* 0A32 bne loc_0A39 */
  if(t) goto loc_0A39;
  S(0x0A34, 2); a = 0xFF; sps_set_zn(sp, a);            /* 0A34 mov a,#$FF */
  S(0x0A36, 3); s_idx(sp);                              /* 0A36 mov $01A0+x,a */
  s_movs(sp, (uint16_t) (0x01A0 + x), a);

loc_0A39:
  S(0x0A39, 3); s_idx(sp);                              /* 0A39 mov a,$0150+x */
  a = s_load(sp, (uint16_t) (0x0150 + x));
  S(0x0A3C, 2); a = s_and(sp, a, 0x02);                 /* 0A3C and a,#$02 */
  t = sps_z(sp);
  S(0x0A3E, 2); s_branch(sp, t);                        /* 0A3E beq loc_0AB4 */
  if(t) goto loc_0AB4;
  S(0x0A40, 2); a = s_load(sp, s_adr_dpx(sp, 0xC4, x)); /* 0A40 mov a,$C4+x */
  t = sps_z(sp);
  S(0x0A42, 2); s_branch(sp, t);                        /* 0A42 beq loc_0A48 */
  if(!t) {
    S(0x0A44, 2); s_dec_mem(sp, s_adr_dpx(sp, 0xC4, x));/* 0A44 dec $C4+x */
    S(0x0A46, 2); s_branch(sp, true);                   /* 0A46 bra loc_0AB4 */
    goto loc_0AB4;
  }
  /* loc_0A48 */
  S(0x0A48, 2); s_dec_mem(sp, s_adr_dpx(sp, 0xB4, x));  /* 0A48 dec $B4+x */
  t = !sps_z(sp);
  S(0x0A4A, 2); s_branch(sp, t);                        /* 0A4A bne loc_0AB4 */
  if(t) goto loc_0AB4;
  S(0x0A4C, 3); s_idx(sp);                              /* 0A4C mov a,$0210+x */
  a = s_load(sp, (uint16_t) (0x0210 + x));
  S(0x0A4F, 2); s_movs(sp, s_adr_dpx(sp, 0xB4, x), a);  /* 0A4F mov $B4+x,a */
  S(0x0A51, 3); s_idx(sp);                              /* 0A51 mov a,$0234+x */
  a = s_load(sp, (uint16_t) (0x0234 + x));
  t = !sps_n(sp);
  S(0x0A54, 2); s_movs(sp, sps_dp(sp, 0x00), a);        /* 0A54 mov $00,a */
  S(0x0A56, 2); s_branch(sp, t);                        /* 0A56 bpl loc_0A5C */
  if(t) goto loc_0A5C;
  S(0x0A58, 2); a = 0xFF; sps_set_zn(sp, a);            /* 0A58 mov a,#$FF */
  S(0x0A5A, 2); s_branch(sp, true);                     /* 0A5A bra loc_0A5E */
  goto loc_0A5E;

loc_0A5C:
  S(0x0A5C, 2); a = 0x00; sps_set_zn(sp, a);            /* 0A5C mov a,#$00 */
loc_0A5E:
  S(0x0A5E, 2); s_movs(sp, sps_dp(sp, 0x01), a);        /* 0A5E mov $01,a */
  S(0x0A60, 2); a = s_load(sp, s_adr_dpx(sp, 0x84, x)); /* 0A60 mov a,$84+x */
  S(0x0A62, 2); y = s_load(sp, s_adr_dpx(sp, 0x74, x)); /* 0A62 mov y,$74+x */
  S(0x0A64, 2); s_cmp(sp, x, 0x0D);                     /* 0A64 cmp x,#$0D */
  t = !sps_z(sp);
  S(0x0A66, 2); s_branch(sp, t);                        /* 0A66 bne loc_0A87 */
  if(!t) {
    S(0x0A68, 1); s_push(sp, a);                        /* 0A68 push a */
    S(0x0A69, 2); a = s_load(sp, sps_dp(sp, 0xEC));     /* 0A69 mov a,$EC */
    S(0x0A6B, 2);                                       /* 0A6B cmp a,$EE */
    s_cmp(sp, a, s_read(sp, sps_dp(sp, 0xEE)));
    t = !sps_z(sp);
    S(0x0A6D, 2); s_branch(sp, t);                      /* 0A6D bne loc_0A78 */
    if(!t) {
      S(0x0A6F, 2); a = s_load(sp, sps_dp(sp, 0xED));   /* 0A6F mov a,$ED */
      S(0x0A71, 2);                                     /* 0A71 cmp a,$EF */
      s_cmp(sp, a, s_read(sp, sps_dp(sp, 0xEF)));
      t = !sps_z(sp);
      S(0x0A73, 2); s_branch(sp, t);                    /* 0A73 bne loc_0A78 */
      if(!t) {
        S(0x0A75, 1); a = s_pop(sp);                    /* 0A75 pop a */
        S(0x0A76, 2); s_branch(sp, true);               /* 0A76 bra loc_0A87 */
        goto loc_0A87;
      }
    }
    /* loc_0A78 */
    S(0x0A78, 1); a = s_pop(sp);                        /* 0A78 pop a */
    S(0x0A79, 2);                                       /* 0A79 subw ya,$EE */
    ya = s_subw(sp, 0xEE, (uint16_t) (a | (y << 8)));
    a = (uint8_t) ya; y = (uint8_t) (ya >> 8);
    S(0x0A7B, 2);                                       /* 0A7B addw ya,$EC */
    ya = s_addw(sp, 0xEC, ya); a = (uint8_t) ya; y = (uint8_t) (ya >> 8);
    S(0x0A7D, 1); s_push(sp, a);                        /* 0A7D push a */
    S(0x0A7E, 2); a = s_load(sp, sps_dp(sp, 0xEC));     /* 0A7E mov a,$EC */
    S(0x0A80, 2); s_movs(sp, sps_dp(sp, 0xEE), a);      /* 0A80 mov $EE,a */
    S(0x0A82, 2); a = s_load(sp, sps_dp(sp, 0xED));     /* 0A82 mov a,$ED */
    S(0x0A84, 2); s_movs(sp, sps_dp(sp, 0xEF), a);      /* 0A84 mov $EF,a */
    S(0x0A86, 1); a = s_pop(sp);                        /* 0A86 pop a */
  }
loc_0A87:
  S(0x0A87, 2);                                         /* 0A87 addw ya,$00 */
  ya = s_addw(sp, 0x00, (uint16_t) (a | (y << 8)));
  a = (uint8_t) ya; y = (uint8_t) (ya >> 8);
  S(0x0A89, 2); s_movs(sp, s_adr_dpx(sp, 0x74, x), y);  /* 0A89 mov $74+x,y */
  S(0x0A8B, 2); s_movs(sp, s_adr_dpx(sp, 0x84, x), a);  /* 0A8B mov $84+x,a */
  S(0x0A8D, 3); s_idx(sp);                              /* 0A8D mov a,$01E0+x */
  a = s_load(sp, (uint16_t) (0x01E0 + x));
  t = !sps_z(sp);
  S(0x0A90, 2); s_branch(sp, t);                        /* 0A90 bne loc_0AA2 */
  if(t) goto loc_0AA2;
  S(0x0A92, 1); s_imp(sp); a = x; sps_set_zn(sp, a);    /* 0A92 mov a,x */
  S(0x0A93, 2); a = s_and(sp, a, 0x07);                 /* 0A93 and a,#$07 */
  S(0x0A95, 1); a = s_xcn(sp, a);                       /* 0A95 xcn a */
  S(0x0A96, 2); a = s_or(sp, a, 0x02);                  /* 0A96 or a,#$02 */
  S(0x0A98, 2); s_movs(sp, sps_dp(sp, SPS_DSPADDR), a); /* 0A98 mov !DSPADDR,a */
  S(0x0A9A, 2); a = s_load(sp, s_adr_dpx(sp, 0x84, x)); /* 0A9A mov a,$84+x */
  S(0x0A9C, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), a); /* 0A9C */
  S(0x0A9E, 2); s_inc_mem(sp, sps_dp(sp, SPS_DSPADDR)); /* 0A9E inc !DSPADDR */
  S(0x0AA0, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), y); /* 0AA0 mov !DSPDATA,y */
loc_0AA2:
  S(0x0AA2, 2); s_dec_mem(sp, s_adr_dpx(sp, 0xA4, x));  /* 0AA2 dec $A4+x */
  t = !sps_z(sp);
  S(0x0AA4, 2); s_branch(sp, t);                        /* 0AA4 bne loc_0AB4 */
  if(t) goto loc_0AB4;
  S(0x0AA6, 3); s_idx(sp);                              /* 0AA6 mov a,$0200+x */
  a = s_load(sp, (uint16_t) (0x0200 + x));
  S(0x0AA9, 2); s_movs(sp, s_adr_dpx(sp, 0xA4, x), a);  /* 0AA9 mov $A4+x,a */
  S(0x0AAB, 3); s_idx(sp);                              /* 0AAB mov a,$0234+x */
  a = s_load(sp, (uint16_t) (0x0234 + x));
  S(0x0AAE, 2); a = s_eor(sp, a, 0xFF);                 /* 0AAE eor a,#$FF */
  S(0x0AB0, 1); s_imp(sp); a++; sps_set_zn(sp, a);      /* 0AB0 inc a */
  S(0x0AB1, 3); s_idx(sp);                              /* 0AB1 mov $0234+x,a */
  s_movs(sp, (uint16_t) (0x0234 + x), a);

loc_0AB4:
  S(0x0AB4, 3); s_idx(sp);                              /* 0AB4 mov a,$0150+x */
  a = s_load(sp, (uint16_t) (0x0150 + x));
  S(0x0AB7, 2); a = s_and(sp, a, 0x0C);                 /* 0AB7 and a,#$0C */
  t = !sps_z(sp);
  S(0x0AB9, 2); s_branch(sp, t);                        /* 0AB9 bne loc_0ABE */
  if(!t) { S(0x0ABB, 3); goto loc_0B17; }               /* 0ABB jmp loc_0B17 */
  /* loc_0ABE */
  S(0x0ABE, 3); s_idx(sp);                              /* 0ABE mov a,$02A4+x */
  a = s_load(sp, (uint16_t) (0x02A4 + x));
  t = sps_z(sp);
  S(0x0AC1, 2); s_branch(sp, t);                        /* 0AC1 beq loc_0ACD */
  if(!t) {
    S(0x0AC3, 3); s_idx(sp);                            /* 0AC3 mov a,$02A4+x */
    a = s_load(sp, (uint16_t) (0x02A4 + x));
    S(0x0AC6, 1); s_imp(sp); a--; sps_set_zn(sp, a);    /* 0AC6 dec a */
    S(0x0AC7, 3); s_idx(sp);                            /* 0AC7 mov $02A4+x,a */
    s_movs(sp, (uint16_t) (0x02A4 + x), a);
    S(0x0ACA, 3); goto loc_0B17;                        /* 0ACA jmp loc_0B17 */
  }
  /* loc_0ACD */
  S(0x0ACD, 3); s_idx(sp);                              /* 0ACD mov a,$02B4+x */
  a = s_load(sp, (uint16_t) (0x02B4 + x));
  S(0x0AD0, 1); s_imp(sp); a--; sps_set_zn(sp, a);      /* 0AD0 dec a */
  t = sps_z(sp);
  S(0x0AD1, 3); s_idx(sp);                              /* 0AD1 mov $02B4+x,a */
  s_movs(sp, (uint16_t) (0x02B4 + x), a);
  S(0x0AD4, 2); s_branch(sp, t);                        /* 0AD4 beq loc_0AD9 */
  if(!t) { S(0x0AD6, 3); goto loc_0B17; }               /* 0AD6 jmp loc_0B17 */
  /* loc_0AD9 */
  S(0x0AD9, 3); s_idx(sp);                              /* 0AD9 mov a,$02C4+x */
  a = s_load(sp, (uint16_t) (0x02C4 + x));
  S(0x0ADC, 3); s_idx(sp);                              /* 0ADC mov $02B4+x,a */
  s_movs(sp, (uint16_t) (0x02B4 + x), a);
  S(0x0ADF, 3); s_idx(sp);                              /* 0ADF mov a,$01E0+x */
  a = s_load(sp, (uint16_t) (0x01E0 + x));
  t = !sps_z(sp);
  S(0x0AE2, 2); s_branch(sp, t);                        /* 0AE2 bne loc_0AF8 */
  if(t) goto loc_0AF8;
  S(0x0AE4, 1); s_imp(sp); a = x; sps_set_zn(sp, a);    /* 0AE4 mov a,x */
  S(0x0AE5, 2); a = s_and(sp, a, 0x07);                 /* 0AE5 and a,#$07 */
  S(0x0AE7, 1); a = s_xcn(sp, a);                       /* 0AE7 xcn a */
  S(0x0AE8, 2); a = s_or(sp, a, 0x00);                  /* 0AE8 or a,#$00 */
  S(0x0AEA, 2); s_movs(sp, sps_dp(sp, SPS_DSPADDR), a); /* 0AEA mov !DSPADDR,a */
  S(0x0AEC, 3); s_idx(sp);                              /* 0AEC mov a,$0254+x */
  a = s_load(sp, (uint16_t) (0x0254 + x));
  S(0x0AEF, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), a); /* 0AEF */
  S(0x0AF1, 3); s_idx(sp);                              /* 0AF1 mov a,$0264+x */
  a = s_load(sp, (uint16_t) (0x0264 + x));
  S(0x0AF4, 2); s_inc_mem(sp, sps_dp(sp, SPS_DSPADDR)); /* 0AF4 inc !DSPADDR */
  S(0x0AF6, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), a); /* 0AF6 */
loc_0AF8:
  S(0x0AF8, 3); s_idx(sp);                              /* 0AF8 mov a,$02E4+x */
  a = s_load(sp, (uint16_t) (0x02E4 + x));
  S(0x0AFB, 1); s_imp(sp); a--; sps_set_zn(sp, a);      /* 0AFB dec a */
  t = !sps_z(sp);
  S(0x0AFC, 3); s_idx(sp);                              /* 0AFC mov $02E4+x,a */
  s_movs(sp, (uint16_t) (0x02E4 + x), a);
  S(0x0AFF, 2); s_branch(sp, t);                        /* 0AFF bne loc_0B17 */
  if(t) goto loc_0B17;
  S(0x0B01, 3); s_idx(sp);                              /* 0B01 mov a,$0150+x */
  a = s_load(sp, (uint16_t) (0x0150 + x));
  S(0x0B04, 2); a = s_and(sp, a, 0x08);                 /* 0B04 and a,#$08 */
  t = !sps_z(sp);
  S(0x0B06, 2); s_branch(sp, t);                        /* 0B06 bne loc_0B17 */
  if(t) goto loc_0B17;
  S(0x0B08, 3); s_idx(sp);                              /* 0B08 mov a,$02F4+x */
  a = s_load(sp, (uint16_t) (0x02F4 + x));
  S(0x0B0B, 3); s_idx(sp);                              /* 0B0B mov $02E4+x,a */
  s_movs(sp, (uint16_t) (0x02E4 + x), a);
  S(0x0B0E, 3); s_idx(sp);                              /* 0B0E mov a,$02D4+x */
  a = s_load(sp, (uint16_t) (0x02D4 + x));
  S(0x0B11, 2); a = s_eor(sp, a, 0xFF);                 /* 0B11 eor a,#$FF */
  S(0x0B13, 1); s_imp(sp); a++; sps_set_zn(sp, a);      /* 0B13 inc a */
  S(0x0B14, 3); s_idx(sp);                              /* 0B14 mov $02D4+x,a */
  s_movs(sp, (uint16_t) (0x02D4 + x), a);

loc_0B17:
  S(0x0B17, 1); S_PUB(); sps_ret(sp);                   /* 0B17 ret */
}

/* ---------------------------------------------------------------------------
 * seq_end — $0B18
 *
 * Sequence command $00. seq_fetch pushed X before the table jump, so the first
 * instruction takes it back. The channel is marked inactive and its voice keyed
 * off. A channel of 8 or more is a sound-effect voice: it is released back to
 * the music channel underneath (X - 8), whose noise bit is cleared and whose
 * echo bit is restored from $0294+x. Returns 0, which stops channel_loop's
 * step loop.
 * ------------------------------------------------------------------------- */
static void seq_end(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  S(0x0B18, 1); x = s_pop(sp);                          /* 0B18 pop x */
  S(0x0B19, 2); a = 0x00; sps_set_zn(sp, a);            /* 0B19 mov a,#$00 */
  S(0x0B1B, 3); s_idx(sp);                              /* 0B1B mov $0110+x,a */
  s_movs(sp, (uint16_t) (0x0110 + x), a);
  S(0x0B1E, 3); s_idx(sp);                              /* 0B1E mov a,$01E0+x */
  a = s_load(sp, (uint16_t) (0x01E0 + x));
  bool overridden = !sps_z(sp);
  S(0x0B21, 2); s_branch(sp, overridden);               /* 0B21 bne loc_0B2B */
  if(!overridden) {
    S(0x0B23, 3); s_movs(sp, sps_dp(sp, SPS_DSPADDR), 0x5C); /* 0B23 KOFF */
    S(0x0B26, 3); s_idx(sp);                            /* 0B26 mov a,$0FC8+x */
    a = s_load(sp, (uint16_t) (0x0FC8 + x));
    S(0x0B29, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), a);    /* 0B29 */
  }
  /* loc_0B2B */
  S(0x0B2B, 1); s_imp(sp); a = x; sps_set_zn(sp, a);    /* 0B2B mov a,x */
  S(0x0B2C, 2); s_cmp(sp, a, 0x08);                     /* 0B2C cmp a,#$08 */
  bool music = !sps_c(sp);
  S(0x0B2E, 2); s_branch(sp, music);                    /* 0B2E bcc loc_0B61 */
  if(!music) {
    S(0x0B30, 1); s_push(sp, x);                        /* 0B30 push x */
    S(0x0B31, 1); s_imp(sp); sps_set_c(sp, true);       /* 0B31 setc */
    S(0x0B32, 2); a = s_sbc(sp, a, 0x08);               /* 0B32 sbc a,#$08 */
    S(0x0B34, 1); s_imp(sp); x = a; sps_set_zn(sp, x);  /* 0B34 mov x,a */
    S(0x0B35, 2); a = 0x00; sps_set_zn(sp, a);          /* 0B35 mov a,#$00 */
    S(0x0B37, 3); s_idx(sp);                            /* 0B37 mov $01E0+x,a */
    s_movs(sp, (uint16_t) (0x01E0 + x), a);
    S(0x0B3A, 3); s_movs(sp, sps_dp(sp, SPS_DSPADDR), 0x3D); /* 0B3A NON */
    S(0x0B3D, 3); s_idx(sp);                            /* 0B3D mov a,$0FC8+x */
    a = s_load(sp, (uint16_t) (0x0FC8 + x));
    S(0x0B40, 2); a = s_eor(sp, a, 0xFF);               /* 0B40 eor a,#$FF */
    S(0x0B42, 2);                                       /* 0B42 and a,!DSPDATA */
    a = s_and(sp, a, s_read(sp, sps_dp(sp, SPS_DSPDATA)));
    S(0x0B44, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), a);    /* 0B44 */
    S(0x0B46, 3); s_movs(sp, sps_dp(sp, SPS_DSPADDR), 0x4D); /* 0B46 EON */
    S(0x0B49, 3); s_idx(sp);                            /* 0B49 mov a,$0294+x */
    a = s_load(sp, (uint16_t) (0x0294 + x));
    bool echo = !sps_z(sp);
    S(0x0B4C, 2); s_branch(sp, !echo);                  /* 0B4C beq loc_0B57 */
    if(echo) {
      S(0x0B4E, 3); s_idx(sp);                          /* 0B4E mov a,$0FC8+x */
      a = s_load(sp, (uint16_t) (0x0FC8 + x));
      S(0x0B51, 2);                                     /* 0B51 or a,!DSPDATA */
      a = s_or(sp, a, s_read(sp, sps_dp(sp, SPS_DSPDATA)));
      S(0x0B53, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), a);  /* 0B53 */
      S(0x0B55, 2); s_branch(sp, true);                 /* 0B55 bra loc_0B60 */
    } else {
      /* loc_0B57 */
      S(0x0B57, 3); s_idx(sp);                          /* 0B57 mov a,$0FC8+x */
      a = s_load(sp, (uint16_t) (0x0FC8 + x));
      S(0x0B5A, 2); a = s_eor(sp, a, 0xFF);             /* 0B5A eor a,#$FF */
      S(0x0B5C, 2);                                     /* 0B5C and a,!DSPDATA */
      a = s_and(sp, a, s_read(sp, sps_dp(sp, SPS_DSPDATA)));
      S(0x0B5E, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), a);  /* 0B5E */
    }
    /* loc_0B60 */
    S(0x0B60, 1); x = s_pop(sp);                        /* 0B60 pop x */
  }
  /* loc_0B61 */
  S(0x0B61, 2); a = 0x00; sps_set_zn(sp, a);            /* 0B61 mov a,#$00 */
  S(0x0B63, 1); S_PUB(); sps_ret(sp);                   /* 0B63 ret */
}

/* ---------------------------------------------------------------------------
 * seq_pop_x — $0B64
 *
 * The helper the sequence-command handlers call first. seq_fetch pushed X and
 * then jumped through the table, so the handler's own `call` frame sits on top
 * of that X: this lifts the return address out of the way, takes X back, and
 * puts the return address down again. Falls into seq_retrigger.
 * ------------------------------------------------------------------------- */
static void seq_pop_x(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  S(0x0B64, 1); y = s_pop(sp);                          /* 0B64 pop y */
  S(0x0B65, 1); a = s_pop(sp);                          /* 0B65 pop a */
  S(0x0B66, 1); x = s_pop(sp);                          /* 0B66 pop x */
  S(0x0B67, 1); s_push(sp, a);                          /* 0B67 push a */
  S(0x0B68, 1); s_push(sp, y);                          /* 0B68 push y */
  S_GOTO(0x0B69);                                       /* falls into seq_retrigger */
}

/* ---------------------------------------------------------------------------
 * seq_retrigger — $0B69
 *
 * Duration 1, gate 0: the channel fetches its next event on the very next tick.
 * Every sequence command that consumes operands and then wants the sequencer to
 * carry on ends here, through seq_pop_x.
 * ------------------------------------------------------------------------- */
static void seq_retrigger(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  S(0x0B69, 2); y = 0x01; sps_set_zn(sp, y);            /* 0B69 mov y,#$01 */
  S(0x0B6B, 2); s_movs(sp, s_adr_dpx(sp, 0x34, x), y);  /* 0B6B mov $34+x,y */
  S(0x0B6D, 2); a = 0x00; sps_set_zn(sp, a);            /* 0B6D mov a,#$00 */
  S(0x0B6F, 2); s_movs(sp, s_adr_dpx(sp, 0x24, x), a);  /* 0B6F mov $24+x,a */
  S(0x0B71, 1); S_PUB(); sps_ret(sp);                   /* 0B71 ret */
}

/* ---------------------------------------------------------------------------
 * sfx_start — $112A
 *
 * Command below $80: start sound effect A on channel X. The id is range-checked
 * against the two banks' counts ($2410 and $2E94) and forced to 0 if it is out
 * of range, then doubled into a pointer-table index. The effect runs on voice
 * X|8 -- the same DSP voice as the music channel underneath, which is why
 * $01E0+x is set on both -- with the channel state reset to defaults: full
 * volume, ADSR $8E/$E0, no transpose, no finetune, no envelope flags. The
 * sequence pointer comes from sfx_bank1_ptrs ($2412) below $C0 or
 * sfx_bank2_ptrs ($2E96) above it, and the voice's noise and echo bits are
 * cleared on the way in and out.
 * ------------------------------------------------------------------------- */
static void sfx_start(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  S(0x112A, 1); s_push(sp, a);                          /* 112A push a */
  S(0x112B, 2); s_cmp(sp, a, 0x60);                     /* 112B cmp a,#$60 */
  bool bank2 = !sps_n(sp);
  S(0x112D, 2); s_branch(sp, bank2);                    /* 112D bpl loc_1137 */
  if(!bank2) {
    S(0x112F, 1); s_imp(sp); sps_set_c(sp, true);       /* 112F setc */
    S(0x1130, 3);                                       /* 1130 sbc a,$2410 */
    a = s_sbc(sp, a, s_read(sp, 0x2410));
    bool out_of_range = !sps_n(sp);
    S(0x1133, 2); s_branch(sp, out_of_range);           /* 1133 bpl loc_1140 */
    if(out_of_range) goto loc_1140;
    S(0x1135, 2); s_branch(sp, true);                   /* 1135 bra loc_1144 */
    goto loc_1144;
  }
  /* loc_1137 */
  S(0x1137, 1); s_imp(sp); sps_set_c(sp, true);         /* 1137 setc */
  S(0x1138, 2); a = s_sbc(sp, a, 0x60);                 /* 1138 sbc a,#$60 */
  S(0x113A, 1); s_imp(sp); sps_set_c(sp, true);         /* 113A setc */
  S(0x113B, 3);                                         /* 113B sbc a,$2E94 */
  a = s_sbc(sp, a, s_read(sp, 0x2E94));
  { bool in_range = sps_n(sp);
    S(0x113E, 2); s_branch(sp, in_range);               /* 113E bmi loc_1144 */
    if(in_range) goto loc_1144; }

loc_1140:
  S(0x1140, 1); a = s_pop(sp);                          /* 1140 pop a */
  S(0x1141, 2); a = 0x00; sps_set_zn(sp, a);            /* 1141 mov a,#$00 */
  S(0x1143, 1); s_push(sp, a);                          /* 1143 push a */
loc_1144:
  S(0x1144, 1); a = s_pop(sp);                          /* 1144 pop a */
  S(0x1145, 1); a = s_asl_a(sp, a);                     /* 1145 asl a */
  S(0x1146, 1); s_push(sp, a);                          /* 1146 push a */
  S(0x1147, 2); a = 0x01; sps_set_zn(sp, a);            /* 1147 mov a,#$01 */
  S(0x1149, 3); s_idx(sp);                              /* 1149 mov $01E0+x,a */
  s_movs(sp, (uint16_t) (0x01E0 + x), a);
  S(0x114C, 3); s_movs(sp, sps_dp(sp, SPS_DSPADDR), 0x3D); /* 114C NON */
  S(0x114F, 3); s_idx(sp);                              /* 114F mov a,$0FC8+x */
  a = s_load(sp, (uint16_t) (0x0FC8 + x));
  S(0x1152, 2); a = s_eor(sp, a, 0xFF);                 /* 1152 eor a,#$FF */
  S(0x1154, 2);                                         /* 1154 and a,!DSPDATA */
  a = s_and(sp, a, s_read(sp, sps_dp(sp, SPS_DSPDATA)));
  S(0x1156, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), a); /* 1156 */
  S(0x1158, 1); s_imp(sp); a = x; sps_set_zn(sp, a);    /* 1158 mov a,x */
  S(0x1159, 1); s_imp(sp); sps_set_c(sp, false);        /* 1159 clrc */
  S(0x115A, 2); a = s_adc(sp, a, 0x08);                 /* 115A adc a,#$08 */
  S(0x115C, 1); s_imp(sp); x = a; sps_set_zn(sp, x);    /* 115C mov x,a */
  S(0x115D, 1); a = s_asl_a(sp, a);                     /* 115D asl a */
  S(0x115E, 1); a = s_asl_a(sp, a);                     /* 115E asl a */
  S(0x115F, 1); a = s_asl_a(sp, a);                     /* 115F asl a */
  S(0x1160, 2); s_movs(sp, s_adr_dpx(sp, 0xD4, x), a);  /* 1160 mov $D4+x,a */
  S(0x1162, 2); a = 0x01; sps_set_zn(sp, a);            /* 1162 mov a,#$01 */
  S(0x1164, 3); s_idx(sp);                              /* 1164 mov $0110+x,a */
  s_movs(sp, (uint16_t) (0x0110 + x), a);
  S(0x1167, 1); s_imp(sp); a--; sps_set_zn(sp, a);      /* 1167 dec a */
  S(0x1168, 3); s_idx(sp);                              /* 1168 mov $0120+x,a */
  s_movs(sp, (uint16_t) (0x0120 + x), a);
  S(0x116B, 3); s_idx(sp);                              /* 116B mov $0130+x,a */
  s_movs(sp, (uint16_t) (0x0130 + x), a);
  S(0x116E, 2); s_movs(sp, s_adr_dpx(sp, 0x24, x), a);  /* 116E mov $24+x,a */
  S(0x1170, 3); s_idx(sp);                              /* 1170 mov $01D0+x,a */
  s_movs(sp, (uint16_t) (0x01D0 + x), a);
  S(0x1173, 3); s_idx(sp);                              /* 1173 mov $01E0+x,a */
  s_movs(sp, (uint16_t) (0x01E0 + x), a);
  S(0x1176, 3); s_idx(sp);                              /* 1176 mov $0150+x,a */
  s_movs(sp, (uint16_t) (0x0150 + x), a);
  S(0x1179, 3); s_idx(sp);                              /* 1179 mov $0140+x,a */
  s_movs(sp, (uint16_t) (0x0140 + x), a);
  S(0x117C, 3); s_idx(sp);                              /* 117C mov $0294+x,a */
  s_movs(sp, (uint16_t) (0x0294 + x), a);
  S(0x117F, 2); s_movs(sp, s_adr_dpx(sp, 0x64, x), a);  /* 117F mov $64+x,a */
  S(0x1181, 2); a = 0x7F; sps_set_zn(sp, a);            /* 1181 mov a,#$7F */
  S(0x1183, 3); s_idx(sp);                              /* 1183 mov $0254+x,a */
  s_movs(sp, (uint16_t) (0x0254 + x), a);
  S(0x1186, 3); s_idx(sp);                              /* 1186 mov $0264+x,a */
  s_movs(sp, (uint16_t) (0x0264 + x), a);
  S(0x1189, 3); s_idx(sp);                              /* 1189 mov $0314+x,a */
  s_movs(sp, (uint16_t) (0x0314 + x), a);
  S(0x118C, 3); s_idx(sp);                              /* 118C mov $0324+x,a */
  s_movs(sp, (uint16_t) (0x0324 + x), a);
  S(0x118F, 2); a = 0x8E; sps_set_zn(sp, a);            /* 118F mov a,#$8E */
  S(0x1191, 3); s_idx(sp);                              /* 1191 mov $0274+x,a */
  s_movs(sp, (uint16_t) (0x0274 + x), a);
  S(0x1194, 2); a = 0xE0; sps_set_zn(sp, a);            /* 1194 mov a,#$E0 */
  S(0x1196, 3); s_idx(sp);                              /* 1196 mov $0284+x,a */
  s_movs(sp, (uint16_t) (0x0284 + x), a);
  S(0x1199, 1); a = s_pop(sp);                          /* 1199 pop a */
  S(0x119A, 2); s_cmp(sp, a, 0xC0);                     /* 119A cmp a,#$C0 */
  { bool second_bank = sps_c(sp);
    S(0x119C, 2); s_branch(sp, second_bank);            /* 119C bcs loc_11AC */
    if(!second_bank) {
      S(0x119E, 1); s_imp(sp); y = a; sps_set_zn(sp, y);/* 119E mov y,a */
      S(0x119F, 3); s_idx(sp);                          /* 119F mov a,$2412+y */
      a = s_load(sp, (uint16_t) (0x2412 + y));
      S(0x11A2, 2); s_movs(sp, s_adr_dpx(sp, 0x44, x), a);  /* 11A2 mov $44+x,a */
      S(0x11A4, 1); s_imp(sp); y++; sps_set_zn(sp, y);  /* 11A4 inc y */
      S(0x11A5, 3); s_idx(sp);                          /* 11A5 mov a,$2412+y */
      a = s_load(sp, (uint16_t) (0x2412 + y));
      S(0x11A8, 2); s_movs(sp, s_adr_dpx(sp, 0x54, x), a);  /* 11A8 mov $54+x,a */
      S(0x11AA, 2); s_branch(sp, true);                 /* 11AA bra loc_11BB */
    } else {
      /* loc_11AC */
      S(0x11AC, 1); s_imp(sp); sps_set_c(sp, true);     /* 11AC setc */
      S(0x11AD, 2); a = s_sbc(sp, a, 0xC0);             /* 11AD sbc a,#$C0 */
      S(0x11AF, 1); s_imp(sp); y = a; sps_set_zn(sp, y);/* 11AF mov y,a */
      S(0x11B0, 3); s_idx(sp);                          /* 11B0 mov a,$2E96+y */
      a = s_load(sp, (uint16_t) (0x2E96 + y));
      S(0x11B3, 2); s_movs(sp, s_adr_dpx(sp, 0x44, x), a);  /* 11B3 mov $44+x,a */
      S(0x11B5, 1); s_imp(sp); y++; sps_set_zn(sp, y);  /* 11B5 inc y */
      S(0x11B6, 3); s_idx(sp);                          /* 11B6 mov a,$2E96+y */
      a = s_load(sp, (uint16_t) (0x2E96 + y));
      S(0x11B9, 2); s_movs(sp, s_adr_dpx(sp, 0x54, x), a);  /* 11B9 mov $54+x,a */
    } }
  /* loc_11BB */
  S(0x11BB, 2); a = 0x02; sps_set_zn(sp, a);            /* 11BB mov a,#$02 */
  S(0x11BD, 2); s_movs(sp, s_adr_dpx(sp, 0x34, x), a);  /* 11BD mov $34+x,a */
  S(0x11BF, 3); s_movs(sp, sps_dp(sp, SPS_DSPADDR), 0x4D); /* 11BF EON */
  S(0x11C2, 3); s_idx(sp);                              /* 11C2 mov a,$0FC8+x */
  a = s_load(sp, (uint16_t) (0x0FC8 + x));
  S(0x11C5, 2); a = s_eor(sp, a, 0xFF);                 /* 11C5 eor a,#$FF */
  S(0x11C7, 2);                                         /* 11C7 and a,!DSPDATA */
  a = s_and(sp, a, s_read(sp, sps_dp(sp, SPS_DSPDATA)));
  S(0x11C9, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), a); /* 11C9 */
  S(0x11CB, 1); S_PUB(); sps_ret(sp);                   /* 11CB ret */
}

static const SpcRecompEntry kSequencer[] = {
  { 0x0781, "tick_wait",        tick_wait },
  { 0x078e, "loc_078E",           loc_078E },
  { 0x07a9, "channel_loop",     channel_loop },
  { 0x0813, "seq_step",         seq_step },
  { 0x0850, "seq_fetch",        seq_fetch },
  { 0x0867, "seq_note",         seq_note },
  { 0x0983, "seq_note_length",  seq_note_length },
  { 0x09bc, "channel_update",   channel_update },
  { 0x0b18, "seq_end",          seq_end },
  { 0x0b64, "seq_pop_x",        seq_pop_x },
  { 0x0b69, "seq_retrigger",    seq_retrigger },
  { 0x112a, "sfx_start",        sfx_start },
};
RECOMP_SPC_REGISTER(kSequencer)
