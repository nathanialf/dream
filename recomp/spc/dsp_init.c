/* dsp_init and dsp_flg_20, SPC $103E-$1129 (spc/driver.asm).
 *
 * The driver's reset: the DSP is muted and cleared, the sample directory is
 * pointed at $3100, all eight voices get a default volume/ADSR/GAIN set, and the
 * sixteen per-slot arrays are initialised from the song header at ($E5). Its two
 * loops run eight times each, some 360 instructions all told, so a catch-up slice
 * ends inside it several times over on every song change.
 */
#include <stdint.h>
#include <stdbool.h>

#include "spc_state.h"
#include "spc_time.h"

/* ---------------------------------------------------------------------------
 * dsp_init: $103E
 * ------------------------------------------------------------------------- */
static void dsp_init(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  S(0x103E, 2); a = 0x00; sps_set_zn(sp, a);                /* 103E mov a,#$00 */
  S(0x1040, 2); s_movs(sp, sps_dp(sp, 0xEC), a);            /* 1040 mov $EC,a */
  S(0x1042, 2); s_movs(sp, sps_dp(sp, 0xED), a);            /* 1042 mov $ED,a */
  S(0x1044, 2); s_movs(sp, sps_dp(sp, 0xEE), a);            /* 1044 mov $EE,a */
  S(0x1046, 2); s_movs(sp, sps_dp(sp, 0xEF), a);            /* 1046 mov $EF,a */
  S(0x1048, 3); s_movs(sp, sps_dp(sp, SPS_DSPADDR), 0x6C);  /* 1048 !DSPADDR = FLG */
  S(0x104B, 3); s_movs(sp, sps_dp(sp, SPS_DSPDATA), 0xE0);  /* 104B !DSPDATA = $E0 */
  S(0x104E, 3); s_movs(sp, sps_dp(sp, SPS_DSPADDR), 0x2C);  /* 104E !DSPADDR = EVOLL */
  S(0x1051, 3); s_movs(sp, 0x0232, a);                      /* 1051 mov $0232,a */
  S(0x1054, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), a);     /* 1054 !DSPDATA = a */
  S(0x1056, 3); s_movs(sp, sps_dp(sp, SPS_DSPADDR), 0x3C);  /* 1056 !DSPADDR = EVOLR */
  S(0x1059, 3); s_movs(sp, 0x0233, a);                      /* 1059 mov $0233,a */
  S(0x105C, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), a);     /* 105C !DSPDATA = a */
  S(0x105E, 3); s_movs(sp, sps_dp(sp, SPS_DSPADDR), 0x0D);  /* 105E !DSPADDR = EFB */
  S(0x1061, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), a);     /* 1061 !DSPDATA = a */
  S(0x1063, 3); s_movs(sp, sps_dp(sp, SPS_DSPADDR), 0x4C);  /* 1063 !DSPADDR = KON */
  S(0x1066, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), a);     /* 1066 !DSPDATA = a */
  S(0x1068, 3); s_movs(sp, sps_dp(sp, SPS_DSPADDR), 0x5C);  /* 1068 !DSPADDR = KOFF */
  S(0x106B, 3); s_movs(sp, sps_dp(sp, SPS_DSPDATA), 0xFF);  /* 106B !DSPDATA = $FF */
  S(0x106E, 3); s_movs(sp, sps_dp(sp, SPS_DSPADDR), 0x2D);  /* 106E !DSPADDR = PMON */
  S(0x1071, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), a);     /* 1071 !DSPDATA = a */
  S(0x1073, 3); s_movs(sp, sps_dp(sp, SPS_DSPADDR), 0x3D);  /* 1073 !DSPADDR = NON */
  S(0x1076, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), a);     /* 1076 !DSPDATA = a */
  S(0x1078, 3); s_movs(sp, sps_dp(sp, SPS_DSPADDR), 0x4D);  /* 1078 !DSPADDR = EON */
  S(0x107B, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), a);     /* 107B !DSPDATA = a */
  S(0x107D, 2); a = 0x3C; sps_set_zn(sp, a);                /* 107D mov a,#$3C */
  S(0x107F, 3); s_movs(sp, 0x0230, a);                      /* 107F mov $0230,a */
  S(0x1082, 3); s_movs(sp, 0x0231, a);                      /* 1082 mov $0231,a */
  S(0x1085, 3); s_movs(sp, sps_dp(sp, SPS_DSPADDR), 0x0C);  /* 1085 !DSPADDR = MVOLL */
  S(0x1088, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), a);     /* 1088 !DSPDATA = a */
  S(0x108A, 3); s_movs(sp, sps_dp(sp, SPS_DSPADDR), 0x1C);  /* 108A !DSPADDR = MVOLR */
  S(0x108D, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), a);     /* 108D !DSPDATA = a */
  S(0x108F, 2); a = 0x64; sps_set_zn(sp, a);                /* 108F mov a,#$64 */
  S(0x1091, 3); s_movs(sp, 0x04B6, a);                      /* 1091 mov $04B6,a */
  S(0x1094, 3); s_movs(sp, sps_dp(sp, SPS_DSPADDR), 0x5D);  /* 1094 !DSPADDR = DIR */
  S(0x1097, 3); s_movs(sp, sps_dp(sp, SPS_DSPDATA), 0x31);  /* 1097 !DSPDATA = $31 */
  S(0x109A, 2); y = 0x08; sps_set_zn(sp, y);                /* 109A mov y,#$08 */
  S(0x109C, 3); s_movs(sp, sps_dp(sp, SPS_DSPADDR), 0x00);  /* 109C !DSPADDR = V0_VOL_L */

  do {                                                      /* loc_109F, 8 voices */
    S(0x109F, 2); a = 0x7F; sps_set_zn(sp, a);              /* 109F mov a,#$7F */
    S(0x10A1, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), a);   /* 10A1 !DSPDATA = a */
    S(0x10A3, 2); s_inc_mem(sp, sps_dp(sp, SPS_DSPADDR));   /* 10A3 inc !DSPADDR */
    S(0x10A5, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), a);   /* 10A5 !DSPDATA = a */
    S(0x10A7, 1); s_imp(sp); sps_set_c(sp, false);          /* 10A7 clrc */
    S(0x10A8, 3); s_adcm(sp, sps_dp(sp, SPS_DSPADDR), 0x04);/* 10A8 adc !DSPADDR,#$04 */
    S(0x10AB, 2); a = 0x00; sps_set_zn(sp, a);              /* 10AB mov a,#$00 */
    S(0x10AD, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), a);   /* 10AD !DSPDATA = a */
    S(0x10AF, 2); s_inc_mem(sp, sps_dp(sp, SPS_DSPADDR));   /* 10AF inc !DSPADDR */
    S(0x10B1, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), a);   /* 10B1 !DSPDATA = a */
    S(0x10B3, 2); s_inc_mem(sp, sps_dp(sp, SPS_DSPADDR));   /* 10B3 inc !DSPADDR */
    S(0x10B5, 3); s_movs(sp, sps_dp(sp, SPS_DSPDATA), 0xFF);/* 10B5 !DSPDATA = $FF */
    S(0x10B8, 1); s_imp(sp); sps_set_c(sp, false);          /* 10B8 clrc */
    S(0x10B9, 3); s_adcm(sp, sps_dp(sp, SPS_DSPADDR), 0x09);/* 10B9 adc !DSPADDR,#$09 */
    S(0x10BC, 1); s_imp(sp); y--; sps_set_zn(sp, y);        /* 10BC dec y */
    S(0x10BD, 2); s_branch(sp, y != 0);                     /* 10BD bne loc_109F */
  } while(y != 0);

  S(0x10BF, 3); s_movs(sp, sps_dp(sp, 0xE7), 0xFF);         /* 10BF mov $E7,#$FF */
  S(0x10C2, 3); s_movs(sp, sps_dp(sp, 0xE8), 0xFF);         /* 10C2 mov $E8,#$FF */
  S(0x10C5, 2); a = 0x64; sps_set_zn(sp, a);                /* 10C5 mov a,#$64 */
  S(0x10C7, 2); s_movs(sp, sps_dp(sp, 0xE4), a);            /* 10C7 mov $E4,a (t0_target) */
  S(0x10C9, 2); a = 0x20; sps_set_zn(sp, a);                /* 10C9 mov a,#$20 */
  S(0x10CB, 3); s_movs(sp, 0x04B5, a);                      /* 10CB mov $04B5,a */
  S(0x10CE, 3); s_movs(sp, sps_dp(sp, 0x00), 0x08);         /* 10CE mov $00,#$08 */
  S(0x10D1, 2); x = 0x00; sps_set_zn(sp, x);                /* 10D1 mov x,#$00 */
  S(0x10D3, 2); y = 0x00; sps_set_zn(sp, y);                /* 10D3 mov y,#$00 */
  S(0x10D5, 2); s_movs(sp, sps_dp(sp, 0x0A), y);            /* 10D5 mov $0A,y */
  S(0x10D7, 3); s_movs(sp, 0x04B4, y);                      /* 10D7 mov $04B4,y */
  S(0x10DA, 2); s_movs(sp, sps_dp(sp, 0x01), y);            /* 10DA mov $01,y */

  /* The per-slot arrays, eight passes: each slot gets duration 1, active 1, its
   * sequence pointer out of the song header at ($E5)+Y, its call-stack base, and
   * zeroes everywhere else. */
  for(;;) {                                                 /* loc_10DC */
    S(0x10DC, 2); a = 0x01; sps_set_zn(sp, a);              /* 10DC mov a,#$01 */
    S(0x10DE, 2); s_idx(sp);                                /* 10DE mov $34+x,a */
    s_movs(sp, sps_dp(sp, (uint8_t) (0x34 + x)), a);
    S(0x10E0, 3); s_idx(sp);                                /* 10E0 mov $0110+x,a */
    s_movs(sp, (uint16_t) (0x0110 + x), a);
    S(0x10E3, 2); a = s_load(sp, s_adr_idy(sp, 0xE5, y));   /* 10E3 mov a,($E5)+y */
    S(0x10E5, 2); s_idx(sp);                                /* 10E5 mov $44+x,a */
    s_movs(sp, sps_dp(sp, (uint8_t) (0x44 + x)), a);
    S(0x10E7, 1); s_imp(sp); y++; sps_set_zn(sp, y);        /* 10E7 inc y */
    S(0x10E8, 2); a = s_load(sp, s_adr_idy(sp, 0xE5, y));   /* 10E8 mov a,($E5)+y */
    S(0x10EA, 2); s_idx(sp);                                /* 10EA mov $54+x,a */
    s_movs(sp, sps_dp(sp, (uint8_t) (0x54 + x)), a);
    S(0x10EC, 2); a = s_load(sp, sps_dp(sp, 0x01));         /* 10EC mov a,$01 */
    S(0x10EE, 2); s_idx(sp);                                /* 10EE mov $D4+x,a */
    s_movs(sp, sps_dp(sp, (uint8_t) (0xD4 + x)), a);
    S(0x10F0, 2); a = 0x00; sps_set_zn(sp, a);              /* 10F0 mov a,#$00 */
    S(0x10F2, 3); s_idx(sp);                                /* 10F2 mov $01D0+x,a */
    s_movs(sp, (uint16_t) (0x01D0 + x), a);
    S(0x10F5, 2); s_idx(sp);                                /* 10F5 mov $24+x,a */
    s_movs(sp, sps_dp(sp, (uint8_t) (0x24 + x)), a);
    S(0x10F7, 3); s_idx(sp);                                /* 10F7 mov $0120+x,a */
    s_movs(sp, (uint16_t) (0x0120 + x), a);
    S(0x10FA, 3); s_idx(sp);                                /* 10FA mov $0130+x,a */
    s_movs(sp, (uint16_t) (0x0130 + x), a);
    S(0x10FD, 3); s_idx(sp);                                /* 10FD mov $0150+x,a */
    s_movs(sp, (uint16_t) (0x0150 + x), a);
    S(0x1100, 3); s_idx(sp);                                /* 1100 mov $0140+x,a */
    s_movs(sp, (uint16_t) (0x0140 + x), a);
    S(0x1103, 2); s_idx(sp);                                /* 1103 mov $64+x,a */
    s_movs(sp, sps_dp(sp, (uint8_t) (0x64 + x)), a);
    S(0x1105, 3); s_idx(sp);                                /* 1105 mov $01E0+x,a */
    s_movs(sp, (uint16_t) (0x01E0 + x), a);
    S(0x1108, 3); s_idx(sp);                                /* 1108 mov $0294+x,a */
    s_movs(sp, (uint16_t) (0x0294 + x), a);
    S(0x110B, 1); s_imp(sp); x++; sps_set_zn(sp, x);        /* 110B inc x */
    S(0x110C, 1); s_imp(sp); y++; sps_set_zn(sp, y);        /* 110C inc y */
    S(0x110D, 1); s_imp(sp); sps_set_c(sp, false);          /* 110D clrc */
    S(0x110E, 3); s_adcm(sp, sps_dp(sp, 0x01), 0x08);       /* 110E adc $01,#$08 */
    S(0x1111, 2);                                           /* 1111 dbnz $00,loc_10DC */
    if(!s_dbnz_dp(sp, 0x00)) break;
  }

  S(0x1114, 2); a = s_load(sp, s_adr_idy(sp, 0xE5, y));     /* 1114 mov a,($E5)+y */
  S(0x1116, 2); s_movs(sp, sps_dp(sp, 0x1F), a);            /* 1116 mov $1F,a (tempo) */
  S(0x1118, 1); s_imp(sp); y++; sps_set_zn(sp, y);          /* 1118 inc y */
  S(0x1119, 2); a = s_load(sp, s_adr_idy(sp, 0xE5, y));     /* 1119 mov a,($E5)+y */
  S(0x111B, 2); s_movs(sp, sps_dp(sp, 0x22), a);            /* 111B mov $22,a (tempo2) */
  S(0x111D, 2); a = 0x00; sps_set_zn(sp, a);                /* 111D mov a,#$00 */
  S(0x111F, 2); s_movs(sp, sps_dp(sp, 0x1E), a);            /* 111F mov $1E,a */
  S(0x1121, 2); s_movs(sp, sps_dp(sp, 0x21), a);            /* 1121 mov $21,a */
  S_GOTO(0x1123);                                           /* falls into dsp_flg_20 */
}

/* ---------------------------------------------------------------------------
 * dsp_flg_20: $1123
 *
 * FLG = $20: unmute, echo *write* still off. dsp_init falls into it, and
 * seq_echo_delay calls it directly.
 * ------------------------------------------------------------------------- */
static void dsp_flg_20(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  S(0x1123, 3); s_movs(sp, sps_dp(sp, SPS_DSPADDR), 0x6C);  /* 1123 !DSPADDR = FLG */
  S(0x1126, 3); s_movs(sp, sps_dp(sp, SPS_DSPDATA), 0x20);  /* 1126 !DSPDATA = $20 */
  S(0x1129, 1); S_PUB(); sps_ret(sp);                       /* 1129 ret */
}

static const SpcRecompEntry kDspInit[] = {
  { 0x103e, "dsp_init",   dsp_init },
  { 0x1123, "dsp_flg_20", dsp_flg_20 },
};
RECOMP_SPC_REGISTER(kDspInit)
