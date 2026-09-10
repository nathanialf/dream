/* The driver's entry, main loop and port command set, SPC $0660-$0810
 * (spc/driver.asm).
 *
 * main_loop is a poll, not an interrupt: it compares port0 against the handshake
 * counter in $E9 and, when they match, reads the command byte from port1 and its
 * parameter from port2, echoes the counter and dispatches. A command below $80
 * is a sound effect; $80 and up selects one of the eight cmd_table handlers by
 * `cmd & 7`. Every handler ends by jumping into tick_wait ($0781), the timer-0
 * wait that drives the sequencer. The handlers hand the pc back there, so
 * tick_wait's own hook (sequencer.c) picks the driver up exactly as the ROM's
 * `jmp tick_wait` would have.
 *
 * spc_map.txt has the protocol; recomp/src/sound_iface.c is the 65816 side of
 * it (write_spc_command, spc_send_words), and the two busy-wait against each
 * other's clock, which is why every instruction here is modelled.
 */
#include <stdint.h>
#include <stdbool.h>

#include "spc_state.h"
#include "spc_time.h"

#define CMD_PARAM   0x055D   /* port2's byte, parked in the loader's tail bytes */
#define TICK_WAIT   0x0781   /* the handler tail; sequencer.c owns it */
#define DSP_INIT    0x103E
#define SCALE_VOL   0x0C59   /* seq_ops_a.c, reached through sps_run_callee */
#define SFX_START   0x112A   /* sequencer.c, likewise */

/* ---------------------------------------------------------------------------
 * start_song: $0660
 *
 * Command 3's tail: the song number in cmd_param indexes song_table ($1312) for
 * a 16-bit sequence pointer, which goes into $E5/$E6 for dsp_init to read the
 * channel headers from.
 * ------------------------------------------------------------------------- */
static void start_song(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  S(0x0660, 3); a = s_load(sp, CMD_PARAM);              /* 0660 mov a,$055D */
  S(0x0663, 1); a = s_asl_a(sp, a);                     /* 0663 asl a */
  S(0x0664, 1); s_imp(sp); y = a; sps_set_zn(sp, y);    /* 0664 mov y,a */
  S(0x0665, 3); s_idx(sp);                              /* 0665 mov a,$1312+y */
  a = s_load(sp, (uint16_t) (0x1312 + y));
  S(0x0668, 2); s_movs(sp, sps_dp(sp, 0xE5), a);        /* 0668 mov $E5,a */
  S(0x066A, 3); s_idx(sp);                              /* 066A mov a,$1313+y */
  a = s_load(sp, (uint16_t) (0x1313 + y));
  S(0x066D, 2); s_movs(sp, sps_dp(sp, 0xE6), a);        /* 066D mov $E6,a */
  S(0x066F, 3);                                         /* 066F jmp driver_init */
  S_GOTO(0x0678);
}

/* ---------------------------------------------------------------------------
 * driver_entry: $0672
 *
 * Where the loader's `jmp ($0539+x)` lands after the 65816 sends destination
 * $0672 with a word count of zero. Points the song pointer at $1300, the block
 * the 65816 uploads per song, and falls into driver_init.
 * ------------------------------------------------------------------------- */
static void driver_entry(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  S(0x0672, 3); s_movs(sp, sps_dp(sp, 0xE6), 0x13);     /* 0672 mov $E6,#$13 */
  S(0x0675, 3); s_movs(sp, sps_dp(sp, 0xE5), 0x00);     /* 0675 mov $E5,#$00 */
  S_GOTO(0x0678);                                       /* falls into driver_init */
}

/* ---------------------------------------------------------------------------
 * driver_init: $0678
 *
 * dsp_init, then clear the play flag, the mono flag and CONTROL (all timers
 * off), and fall into main_loop. The call is run on the emulator so that
 * dsp_init's own hook fires inside it, and so that the slice can end inside a
 * routine that executes some 360 instructions without this hook having to be
 * atomic across it.
 * ------------------------------------------------------------------------- */
static void driver_init(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  S(0x0678, 3);                                         /* 0678 call dsp_init */
  {
    sps_idle(sp);
    uint8_t sp0 = sps_sp(sp);
    sps_push16(sp, 0x067B);
    sps_idle(sp);
    sps_idle(sp);
    sps_set_pc(sp, DSP_INIT);
    if(sps_run_callee(sp, sp0)) return;                 /* left dsp_init running */
    a = sps_a(sp); x = sps_x(sp); y = sps_y(sp);
  }
  S(0x067B, 2); a = 0x00; sps_set_zn(sp, a);            /* 067B mov a,#$00 */
  S(0x067D, 2); s_movs(sp, sps_dp(sp, 0x1C), a);        /* 067D mov $1C,a (play_flag) */
  S(0x067F, 2); s_movs(sp, sps_dp(sp, 0x1D), a);        /* 067F mov $1D,a (mono_flag) */
  S(0x0681, 2); s_movs(sp, sps_dp(sp, SPS_CONTROL), a); /* 0681 mov !CONTROL,a */
  S_GOTO(0x0683);                                       /* falls into main_loop */
}

/* ---------------------------------------------------------------------------
 * main_loop: $0683
 *
 * The poll. Four instructions, entered again on every pass of the driver's outer
 * loop, so this is by far the most-called routine in the driver.
 * ------------------------------------------------------------------------- */
static void main_loop(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  S(0x0683, 2); a = s_load(sp, sps_dp(sp, 0xE9));       /* 0683 mov a,$E9 */
  S(0x0685, 2);                                         /* 0685 cmp a,!CPUIO0 */
  s_cmp(sp, a, s_read(sp, sps_dp(sp, SPS_CPUIO0)));
  bool cmd = sps_z(sp);
  S(0x0687, 2); s_branch(sp, cmd);                      /* 0687 beq cmd_receive */
  if(cmd) S_GOTO(0x068C);
  S(0x0689, 3);                                         /* 0689 jmp tick_wait */
  S_GOTO(TICK_WAIT);
}

/* ---------------------------------------------------------------------------
 * cmd_receive: $068C
 *
 * A command has arrived: parameter from port2 into cmd_param, command byte from
 * port1, the counter echoed back and bumped. Commands below $80 are sound
 * effects; the rest go through cmd_dispatch.
 * ------------------------------------------------------------------------- */
static void cmd_receive(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  S(0x068C, 2); x = s_load(sp, sps_dp(sp, SPS_CPUIO2)); /* 068C mov x,!CPUIO2 */
  S(0x068E, 3); s_movs(sp, CMD_PARAM, x);               /* 068E mov $055D,x */
  S(0x0691, 2); x = s_load(sp, sps_dp(sp, SPS_CPUIO1)); /* 0691 mov x,!CPUIO1 */
  S(0x0693, 2); s_movs(sp, sps_dp(sp, SPS_CPUIO0), a);  /* 0693 mov !CPUIO0,a */
  S(0x0695, 1); s_imp(sp); a++; sps_set_zn(sp, a);      /* 0695 inc a */
  S(0x0696, 2); s_movs(sp, sps_dp(sp, 0xE9), a);        /* 0696 mov $E9,a */
  S(0x0698, 1); s_imp(sp); a = x; sps_set_zn(sp, a);    /* 0698 mov a,x */
  S(0x0699, 2); s_cmp(sp, a, 0x80);                     /* 0699 cmp a,#$80 */
  bool table = !sps_n(sp);
  S(0x069B, 2); s_branch(sp, table);                    /* 069B bpl cmd_dispatch */
  if(table) S_GOTO(0x06A0);
  S(0x069D, 3);                                         /* 069D jmp play_sfx */
  S_GOTO(0x0773);
}

/* ---------------------------------------------------------------------------
 * cmd_dispatch: $06A0
 *
 * `cmd & 7` doubled indexes cmd_table ($06A7); `jmp (cmd_table+x)` reads the
 * handler address out of the table in ARAM.
 * ------------------------------------------------------------------------- */
static void cmd_dispatch(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  S(0x06A0, 2); a = s_and(sp, a, 0x07);                 /* 06A0 and a,#$07 */
  S(0x06A2, 1); a = s_asl_a(sp, a);                     /* 06A2 asl a */
  S(0x06A3, 1); s_imp(sp); x = a; sps_set_zn(sp, x);    /* 06A3 mov x,a */
  S(0x06A4, 3);                                         /* 06A4 jmp (cmd_table+x) */
  uint16_t target = s_jmp_iax(sp, 0x06A7, x);
  S_GOTO(target);
}

/* ---------------------------------------------------------------------------
 * cmd3_fade_and_song: $06B7
 *
 * $7F passes over every DSP volume register, each pass moving it two steps
 * toward zero, then start_song. Roughly a hundred thousand APU cycles, so the
 * catch-up slice ends inside it many times over.
 * ------------------------------------------------------------------------- */
static void cmd3_fade_and_song(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  /* the four global volume registers the pass ends with, and the call that
   * follows each: MVOLL, MVOLR, EVOLL, EVOLR */
  static const struct { uint16_t at; uint8_t reg; uint16_t call; } kGlobal[4] = {
    { 0x06CD, 0x0C, 0x06D0 }, { 0x06D3, 0x1C, 0x06D6 },
    { 0x06D9, 0x2C, 0x06DC }, { 0x06DF, 0x3C, 0x06E2 },
  };

  S(0x06B7, 2); x = 0x7F; sps_set_zn(sp, x);            /* 06B7 mov x,#$7F */
  for(;;) {                                             /* loc_06B9 */
    S(0x06B9, 2); a = 0x71; sps_set_zn(sp, a);          /* 06B9 mov a,#$71 */
    for(;;) {                                           /* loc_06BB */
      S(0x06BB, 1); s_imp(sp); y = a; sps_set_zn(sp, y);        /* 06BB mov y,a */
      S(0x06BC, 2); s_movs(sp, sps_dp(sp, SPS_DSPADDR), y);     /* 06BC mov !DSPADDR,y */
      S(0x06BE, 3);                                             /* 06BE call */
      { sps_idle(sp); uint8_t sp0 = sps_sp(sp); sps_push16(sp, 0x06C1);
        sps_idle(sp); sps_idle(sp); sps_set_pc(sp, 0x06EB);
        if(sps_run_callee(sp, sp0)) return;
        a = sps_a(sp); x = sps_x(sp); y = sps_y(sp); }
      S(0x06C1, 1); s_imp(sp); y--; sps_set_zn(sp, y);          /* 06C1 dec y */
      S(0x06C2, 2); s_movs(sp, sps_dp(sp, SPS_DSPADDR), y);     /* 06C2 mov !DSPADDR,y */
      S(0x06C4, 3);                                             /* 06C4 call */
      { sps_idle(sp); uint8_t sp0 = sps_sp(sp); sps_push16(sp, 0x06C7);
        sps_idle(sp); sps_idle(sp); sps_set_pc(sp, 0x06EB);
        if(sps_run_callee(sp, sp0)) return;
        a = sps_a(sp); x = sps_x(sp); y = sps_y(sp); }
      S(0x06C7, 1); s_imp(sp); a = y; sps_set_zn(sp, a);        /* 06C7 mov a,y */
      S(0x06C8, 1); s_imp(sp); sps_set_c(sp, true);             /* 06C8 setc */
      S(0x06C9, 2); a = s_sbc(sp, a, 0x0F);                     /* 06C9 sbc a,#$0F */
      bool again = !sps_n(sp);
      S(0x06CB, 2); s_branch(sp, again);                        /* 06CB bpl loc_06BB */
      if(!again) break;
    }
    for(int i = 0; i < 4; i++) {                        /* 06CD..06E4 */
      S(kGlobal[i].at, 3);
      s_movs(sp, sps_dp(sp, SPS_DSPADDR), kGlobal[i].reg);
      S(kGlobal[i].call, 3);
      { sps_idle(sp); uint8_t sp0 = sps_sp(sp);
        sps_push16(sp, (uint16_t) (kGlobal[i].call + 3));
        sps_idle(sp); sps_idle(sp); sps_set_pc(sp, 0x06EB);
        if(sps_run_callee(sp, sp0)) return;
        a = sps_a(sp); x = sps_x(sp); y = sps_y(sp); }
    }
    S(0x06E5, 1); s_imp(sp); x--; sps_set_zn(sp, x);    /* 06E5 dec x */
    bool more = !sps_z(sp);
    S(0x06E6, 2); s_branch(sp, more);                   /* 06E6 bne loc_06B9 */
    if(!more) break;
  }
  S(0x06E8, 3);                                         /* 06E8 jmp start_song */
  S_GOTO(0x0660);
}

/* ---------------------------------------------------------------------------
 * dsp_step_toward_zero: $06EB
 *
 * Read the DSP register $F2 selects, move it two steps toward zero, write it
 * back. cmd3's fade calls it six times a pass.
 * ------------------------------------------------------------------------- */
static void dsp_step_toward_zero(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  S(0x06EB, 2); a = s_load(sp, sps_dp(sp, SPS_DSPDATA));/* 06EB mov a,!DSPDATA */
  bool zero = sps_z(sp);
  S(0x06ED, 2); s_branch(sp, zero);                     /* 06ED beq loc_06F7 */
  if(!zero) {
    bool neg = sps_n(sp);
    S(0x06EF, 2); s_branch(sp, neg);                    /* 06EF bmi loc_06F5 */
    if(!neg) {
      S(0x06F1, 1); s_imp(sp); a--; sps_set_zn(sp, a);  /* 06F1 dec a */
      S(0x06F2, 1); s_imp(sp); a--; sps_set_zn(sp, a);  /* 06F2 dec a */
      S(0x06F3, 2); s_branch(sp, true);                 /* 06F3 bra loc_06F7 */
    } else {
      S(0x06F5, 1); s_imp(sp); a++; sps_set_zn(sp, a);  /* 06F5 inc a */
      S(0x06F6, 1); s_imp(sp); a++; sps_set_zn(sp, a);  /* 06F6 inc a */
    }
  }
  S(0x06F7, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), a); /* 06F7 mov !DSPDATA,a */
  S(0x06F9, 1); S_PUB(); sps_ret(sp);                   /* 06F9 ret */
}

/* One-line handlers: cmd_param into a byte of zero page, then back to
 * tick_wait. cmd2 sets the mono flag; cmd0 and cmd1 set $E8 / $E7, which no
 * other traced code reads. */
static void cmd_param_to_dp(SpcState* sp, uint16_t at, uint8_t dst) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  S(at, 3);     a = s_load(sp, CMD_PARAM);              /* mov a,$055D */
  S(at + 3, 2); s_movs(sp, sps_dp(sp, dst), a);         /* mov $xx,a */
  S(at + 5, 3);                                         /* jmp tick_wait */
  S_GOTO(TICK_WAIT);
}

static void cmd2_set_mono(SpcState* sp) { cmd_param_to_dp(sp, 0x06FA, 0x1D); }
static void cmd1_set_E7(SpcState* sp)   { cmd_param_to_dp(sp, 0x0702, 0xE7); }
static void cmd0_set_E8(SpcState* sp)   { cmd_param_to_dp(sp, 0x070A, 0xE8); }

/* ---------------------------------------------------------------------------
 * cmd5_voice5_volume: $0712
 *
 * Rescale voice 5's two DSP volume registers by the parameter, as a percentage,
 * with the old master percentage saved on the stack around it. scale_volume
 * ($0C59) is reached through sps_run_callee, so its own hook fires.
 * ------------------------------------------------------------------------- */
static void cmd5_voice5_volume(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  S(0x0712, 3); a = s_load(sp, 0x04B6);                 /* 0712 mov a,$04B6 */
  S(0x0715, 1); s_push(sp, a);                          /* 0715 push a */
  S(0x0716, 1); s_push(sp, x);                          /* 0716 push x */
  S(0x0717, 2); x = 0x05; sps_set_zn(sp, x);            /* 0717 mov x,#$05 */
  S(0x0719, 3); a = s_load(sp, CMD_PARAM);              /* 0719 mov a,$055D */
  S(0x071C, 3); s_movs(sp, 0x04B6, a);                  /* 071C mov $04B6,a */
  S(0x071F, 3); s_movs(sp, sps_dp(sp, SPS_DSPADDR), 0x50); /* 071F mov !DSPADDR,#$50 */
  S(0x0722, 2); a = s_load(sp, sps_dp(sp, SPS_DSPDATA));   /* 0722 mov a,!DSPDATA */
  S(0x0724, 3);                                            /* 0724 call scale_volume */
  { sps_idle(sp); uint8_t sp0 = sps_sp(sp); sps_push16(sp, 0x0727);
    sps_idle(sp); sps_idle(sp); sps_set_pc(sp, SCALE_VOL);
    if(sps_run_callee(sp, sp0)) return;
    a = sps_a(sp); x = sps_x(sp); y = sps_y(sp); }
  S(0x0727, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), a);    /* 0727 mov !DSPDATA,a */
  S(0x0729, 2); s_inc_mem(sp, sps_dp(sp, SPS_DSPADDR));    /* 0729 inc !DSPADDR */
  S(0x072B, 2); a = s_load(sp, sps_dp(sp, SPS_DSPDATA));   /* 072B mov a,!DSPDATA */
  S(0x072D, 3);                                            /* 072D call scale_volume */
  { sps_idle(sp); uint8_t sp0 = sps_sp(sp); sps_push16(sp, 0x0730);
    sps_idle(sp); sps_idle(sp); sps_set_pc(sp, SCALE_VOL);
    if(sps_run_callee(sp, sp0)) return;
    a = sps_a(sp); x = sps_x(sp); y = sps_y(sp); }
  S(0x0730, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), a);    /* 0730 mov !DSPDATA,a */
  S(0x0732, 1); x = s_pop(sp);                             /* 0732 pop x */
  S(0x0733, 1); a = s_pop(sp);                             /* 0733 pop a */
  S(0x0734, 3); s_movs(sp, 0x04B6, a);                     /* 0734 mov $04B6,a */
  S(0x0737, 2); s_branch(sp, true);                        /* 0737 bra tick_wait */
  S_GOTO(TICK_WAIT);
}

/* ---------------------------------------------------------------------------
 * cmd4_pitch_offset: $0739
 *
 * Sign-extend the parameter, multiply it by eight with eight 16-bit adds, and
 * park it in $EC/$ED for channel_update to apply to the sfx voice; then clear
 * EON bit 5.
 * ------------------------------------------------------------------------- */
static void cmd4_pitch_offset(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  S(0x0739, 3); a = s_load(sp, CMD_PARAM);              /* 0739 mov a,$055D */
  bool neg = sps_n(sp);
  S(0x073C, 2); s_branch(sp, neg);                      /* 073C bmi loc_074A */
  if(!neg) {
    S(0x073E, 1); s_imp(sp); sps_set_c(sp, false);      /* 073E clrc */
    S(0x073F, 3); a = s_load(sp, CMD_PARAM);            /* 073F mov a,$055D */
    S(0x0742, 2); s_movs(sp, sps_dp(sp, 0xEC), a);      /* 0742 mov $EC,a */
    S(0x0744, 2); a = 0x00; sps_set_zn(sp, a);          /* 0744 mov a,#$00 */
    S(0x0746, 2); s_movs(sp, sps_dp(sp, 0xED), a);      /* 0746 mov $ED,a */
    S(0x0748, 2); s_branch(sp, true);                   /* 0748 bra loc_0756 */
  } else {
    S(0x074A, 3); s_movs(sp, CMD_PARAM, a);             /* 074A mov $055D,a */
    S(0x074D, 3); a = s_load(sp, CMD_PARAM);            /* 074D mov a,$055D */
    S(0x0750, 2); s_movs(sp, sps_dp(sp, 0xEC), a);      /* 0750 mov $EC,a */
    S(0x0752, 2); a = 0xFF; sps_set_zn(sp, a);          /* 0752 mov a,#$FF */
    S(0x0754, 2); s_movs(sp, sps_dp(sp, 0xED), a);      /* 0754 mov $ED,a */
  }
  uint16_t ya;
  S(0x0756, 2); ya = s_movw_load(sp, 0xEC);             /* 0756 movw ya,$EC */
  a = (uint8_t) ya; y = (uint8_t) (ya >> 8);
  for(int i = 0; i < 7; i++) {                          /* 0758..0764 addw ya,$EC x7 */
    S(0x0758 + i * 2, 2);
    ya = s_addw(sp, 0xEC, ya);
    a = (uint8_t) ya; y = (uint8_t) (ya >> 8);
  }
  S(0x0766, 2); s_movw_store(sp, 0xEC, ya);             /* 0766 movw $EC,ya */
  S(0x0768, 3); s_movs(sp, sps_dp(sp, SPS_DSPADDR), 0x4D); /* 0768 mov !DSPADDR,#$4D */
  S(0x076B, 2); a = s_load(sp, sps_dp(sp, SPS_DSPDATA));   /* 076B mov a,!DSPDATA */
  S(0x076D, 2); a = s_and(sp, a, 0xDF);                    /* 076D and a,#$DF */
  S(0x076F, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), a);    /* 076F mov !DSPDATA,a */
  S(0x0771, 2); s_branch(sp, true);                        /* 0771 bra tick_wait */
  S_GOTO(TICK_WAIT);
}

/* ---------------------------------------------------------------------------
 * play_sfx: $0773
 *
 * A command below $80: the command byte is the sound-effect id (already in A),
 * the parameter is the channel. sfx_start ($112A) is reached through
 * sps_run_callee, so its own hook fires.
 * ------------------------------------------------------------------------- */
static void play_sfx(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  S(0x0773, 3); x = s_load(sp, CMD_PARAM);              /* 0773 mov x,$055D */
  S(0x0776, 3);                                         /* 0776 call sfx_start */
  { sps_idle(sp); uint8_t sp0 = sps_sp(sp); sps_push16(sp, 0x0779);
    sps_idle(sp); sps_idle(sp); sps_set_pc(sp, SFX_START);
    if(sps_run_callee(sp, sp0)) return;
    a = sps_a(sp); x = sps_x(sp); y = sps_y(sp); }
  S(0x0779, 2); s_branch(sp, true);                     /* 0779 bra loc_078E */
  S_GOTO(0x078E);
}

/* ---------------------------------------------------------------------------
 * cmd6_play: $077B
 *
 * Set the play flag and stop the timers; tick_wait restarts timer 0. This is the
 * command write_spc_command ends every song change with.
 * ------------------------------------------------------------------------- */
static void cmd6_play(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  S(0x077B, 3); s_movs(sp, sps_dp(sp, 0x1C), 0x01);        /* 077B mov $1C,#$01 */
  S(0x077E, 3); s_movs(sp, sps_dp(sp, SPS_CONTROL), 0x00); /* 077E mov !CONTROL,#$00 */
  S_GOTO(TICK_WAIT);                                       /* falls into tick_wait */
}

/* ---------------------------------------------------------------------------
 * cmd7_stop_to_loader: $07DB
 *
 * The command that gives the loader back control so the 65816 can upload a new
 * song. A non-zero parameter jumps straight there; a zero one keys every voice
 * off first, waits one full timer-1 period (target $C8, about 25 600 APU cycles
 * of spinning on $FE), silences the echo and re-runs dsp_init.
 * ------------------------------------------------------------------------- */
static void cmd7_stop_to_loader(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  S(0x07DB, 3); a = s_load(sp, CMD_PARAM);              /* 07DB mov a,$055D */
  bool quiet = sps_z(sp);
  S(0x07DE, 2); s_branch(sp, quiet);                    /* 07DE beq loc_07E3 */
  if(!quiet) {
    S(0x07E0, 3);                                       /* 07E0 jmp loader_reset_dsp */
    S_GOTO(0x04F3);
  }
  S(0x07E3, 3); s_movs(sp, sps_dp(sp, SPS_DSPADDR), 0x5C);  /* 07E3 !DSPADDR = KOFF */
  S(0x07E6, 3); s_movs(sp, sps_dp(sp, SPS_DSPDATA), 0xFF);  /* 07E6 !DSPDATA = $FF */
  S(0x07E9, 3); s_movs(sp, sps_dp(sp, SPS_CONTROL), 0x00);  /* 07E9 !CONTROL = 0 */
  S(0x07EC, 3); s_movs(sp, sps_dp(sp, SPS_T1TARGET), 0xC8); /* 07EC !T1TARGET = $C8 */
  S(0x07EF, 3); s_movs(sp, sps_dp(sp, SPS_CONTROL), 0x02);  /* 07EF !CONTROL = 2 */
  for(;;) {                                                 /* loc_07F2 */
    S(0x07F2, 2); a = s_load(sp, sps_dp(sp, SPS_T1OUT));    /* 07F2 mov a,!T1OUT */
    bool wait = sps_z(sp);
    S(0x07F4, 2); s_branch(sp, wait);                       /* 07F4 beq loc_07F2 */
    if(!wait) break;
  }
  S(0x07F6, 3); s_movs(sp, sps_dp(sp, SPS_DSPADDR), 0x6C);  /* 07F6 !DSPADDR = FLG */
  S(0x07F9, 3); s_movs(sp, sps_dp(sp, SPS_DSPDATA), 0xA0);  /* 07F9 !DSPDATA = $A0 */
  S(0x07FC, 2); x = 0x00; sps_set_zn(sp, x);                /* 07FC mov x,#$00 */
  S(0x07FE, 3); s_movs(sp, sps_dp(sp, SPS_DSPADDR), 0x4D);  /* 07FE !DSPADDR = EON */
  S(0x0801, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), x);     /* 0801 !DSPDATA = x */
  S(0x0803, 3); s_movs(sp, sps_dp(sp, SPS_DSPADDR), 0x2C);  /* 0803 !DSPADDR = EVOLL */
  S(0x0806, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), x);     /* 0806 !DSPDATA = x */
  S(0x0808, 3); s_movs(sp, sps_dp(sp, SPS_DSPADDR), 0x3C);  /* 0808 !DSPADDR = EVOLR */
  S(0x080B, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), x);     /* 080B !DSPDATA = x */
  S(0x080D, 3);                                             /* 080D call dsp_init */
  { sps_idle(sp); uint8_t sp0 = sps_sp(sp); sps_push16(sp, 0x0810);
    sps_idle(sp); sps_idle(sp); sps_set_pc(sp, DSP_INIT);
    if(sps_run_callee(sp, sp0)) return;
    a = sps_a(sp); x = sps_x(sp); y = sps_y(sp); }
  S(0x0810, 3);                                             /* 0810 jmp loader_reset_dsp */
  S_GOTO(0x04F3);
}

static const SpcRecompEntry kDriver[] = {
  { 0x0660, "start_song",           start_song },
  { 0x0672, "driver_entry",         driver_entry },
  { 0x0678, "driver_init",          driver_init },
  { 0x0683, "main_loop",            main_loop },
  { 0x068c, "cmd_receive",          cmd_receive },
  { 0x06a0, "cmd_dispatch",         cmd_dispatch },
  { 0x06b7, "cmd3_fade_and_song",   cmd3_fade_and_song },
  { 0x06eb, "dsp_step_toward_zero", dsp_step_toward_zero },
  { 0x06fa, "cmd2_set_mono",        cmd2_set_mono },
  { 0x0702, "cmd1_set_E7",          cmd1_set_E7 },
  { 0x070a, "cmd0_set_E8",          cmd0_set_E8 },
  { 0x0712, "cmd5_voice5_volume",   cmd5_voice5_volume },
  { 0x0739, "cmd4_pitch_offset",    cmd4_pitch_offset },
  { 0x0773, "play_sfx",             play_sfx },
  { 0x077b, "cmd6_play",            cmd6_play },
  { 0x07db, "cmd7_stop_to_loader",  cmd7_stop_to_loader },
};
RECOMP_SPC_REGISTER(kDriver)
