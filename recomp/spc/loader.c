/* The IPL-uploaded loader, SPC $04D8-$055C (spc/driver.asm).
 *
 * The 65816 pushes this 136-byte block through the IPL boot ROM and jumps to it;
 * it then stays resident for the rest of the run, because driver command 7 hands
 * control back to it whenever a new song's samples have to be uploaded.
 *
 * Its whole job is one handshake, run twice per block header and once per data
 * word: port0 ($F4) carries a counter, the SPC waits for the 65816 to write the
 * value it expects, reads port1/port2, echoes the counter back and increments.
 * The 65816's side of it (spc_send_words, $C1:8324, converted in
 * recomp/src/sound_iface.c) is a busy-wait against this one, so the two clocks
 * are load-bearing in both directions -- which is the reason every instruction
 * here is modelled rather than computed.
 *
 * The block loop patches the destination address into the operands of its own
 * two `mov abs+y,a` instructions and then jumps through `jmp ($0539+x)`, so the
 * ROM writes its jump target into its own code. The C body does exactly that:
 * the stores go into ARAM through the timed path, and the jump reads the vector
 * straight back out of ARAM, one byte at a time, as the SPC700 does.
 */
#include <stdint.h>
#include <stdbool.h>

#include "spc_state.h"
#include "spc_time.h"

/* the two self-modified `mov abs+y,a` operands the block loop patches */
#define LOADER_DEST   0x0539
#define LOADER_DEST2  0x0542

/* ---------------------------------------------------------------------------
 * spc_loader — $04D8
 *
 * Entered by the IPL's jump. Sets the stack up, clears port0, seeds the
 * handshake counter at 1 ($E9) and clears $D000-$FFFF -- the top of ARAM, where
 * the echo buffer will live -- with a 12 KB store loop, then falls into
 * loader_reset_dsp.
 * ------------------------------------------------------------------------- */
static void spc_loader(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  S(0x04D8, 1); s_imp(sp); sps_set_p(sp, false);        /* 04D8 clrp */
  S(0x04D9, 2); x = 0xFF; sps_set_zn(sp, x);            /* 04D9 mov x,#$FF */
  S(0x04DB, 1); s_imp(sp); sps_set_sp(sp, x);           /* 04DB mov sp,x */
  S(0x04DC, 1); s_imp(sp); x++; sps_set_zn(sp, x);      /* 04DC inc x */
  S(0x04DD, 2); s_movs(sp, sps_dp(sp, SPS_CPUIO0), x);  /* 04DD mov !CPUIO0,x */
  S(0x04DF, 1); s_imp(sp); x++; sps_set_zn(sp, x);      /* 04DF inc x */
  S(0x04E0, 2); s_movs(sp, sps_dp(sp, 0xE9), x);        /* 04E0 mov $E9,x */
  S(0x04E2, 2); a = 0x00; sps_set_zn(sp, a);            /* 04E2 mov a,#$00 */
  S(0x04E4, 2); s_movs(sp, sps_dp(sp, 0x00), a);        /* 04E4 mov $00,a */
  S(0x04E6, 3); s_movs(sp, sps_dp(sp, 0x01), 0xD0);     /* 04E6 mov $01,#$D0 */

  for(;;) {
    S(0x04E9, 1); s_imp(sp); y = a; sps_set_zn(sp, y);  /* 04E9 mov y,a */
    do {
      S(0x04EA, 2);                                     /* 04EA mov ($00)+y,a */
      s_movs(sp, s_adr_idy(sp, 0x00, y), a);
      S(0x04EC, 1); s_imp(sp); y++; sps_set_zn(sp, y);  /* 04EC inc y */
      S(0x04ED, 2); s_branch(sp, y != 0);               /* 04ED bne loc_04EA */
    } while(y != 0);
    S(0x04EF, 2); s_inc_mem(sp, sps_dp(sp, 0x01));      /* 04EF inc $01 */
    bool more = !sps_z(sp);
    S(0x04F1, 2); s_branch(sp, more);                   /* 04F1 bne loc_04E9 */
    if(!more) break;
  }
  S_GOTO(0x04F3);                                       /* falls into loader_reset_dsp */
}

/* ---------------------------------------------------------------------------
 * loader_reset_dsp — $04F3
 *
 * DSP FLG = $FF (mute, echo write off, noise reset), EDL = 0, ESA = $FF, and
 * $04B7 remembers the ESA. Driver command 7 jumps straight here, which is how a
 * song change gets back to the loader. Falls into loader_block_loop with X
 * holding the handshake counter.
 * ------------------------------------------------------------------------- */
static void loader_reset_dsp(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  S(0x04F3, 2); x = 0xFF; sps_set_zn(sp, x);              /* 04F3 mov x,#$FF */
  S(0x04F5, 3); s_movs(sp, sps_dp(sp, SPS_DSPADDR), 0x6C);/* 04F5 mov !DSPADDR,#$6C (FLG) */
  S(0x04F8, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), x);   /* 04F8 mov !DSPDATA,x */
  S(0x04FA, 3); s_movs(sp, sps_dp(sp, SPS_DSPADDR), 0x7D);/* 04FA mov !DSPADDR,#$7D (EDL) */
  S(0x04FD, 3); s_movs(sp, sps_dp(sp, SPS_DSPDATA), 0x00);/* 04FD mov !DSPDATA,#$00 */
  S(0x0500, 3); s_movs(sp, sps_dp(sp, SPS_DSPADDR), 0x6D);/* 0500 mov !DSPADDR,#$6D (ESA) */
  S(0x0503, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), x);   /* 0503 mov !DSPDATA,x */
  S(0x0505, 3); s_movs(sp, 0x04B7, x);                    /* 0505 mov $04B7,x */
  S(0x0508, 2); x = s_load(sp, sps_dp(sp, 0xE9));         /* 0508 mov x,$E9 */
  S_GOTO(0x050A);                                         /* falls into loader_block_loop */
}

/* ---------------------------------------------------------------------------
 * loader_block_loop — $050A
 *
 * One upload block: two handshakes read a 16-bit destination and a 16-bit word
 * count, then `count` handshakes deliver one word each (port1 = low byte,
 * port2 = high byte) into destination+Y. A count of zero means "jump to the
 * destination instead", which is loader_jump.
 *
 * The destination is written into the operand bytes of the two `mov $0000+y,a`
 * at $0538 and $0541, and the operand high bytes are bumped whenever Y wraps.
 * The two stores therefore read their own address out of ARAM every time they
 * execute; the body takes the operand from ARAM for exactly that reason, right
 * where the SPC700's operand fetch reads it.
 * ------------------------------------------------------------------------- */
static void loader_block_loop(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  for(;;) {                                   /* loader_block_loop / loc_054B */
    /* --- handshake 1: the destination address --- */
    for(;;) {
      S(0x050A, 2); s_cmp(sp, x, s_read(sp, sps_dp(sp, SPS_CPUIO0)));  /* cmp x,!CPUIO0 */
      bool wait = !sps_z(sp);
      S(0x050C, 2); s_branch(sp, wait);                                /* bne */
      if(!wait) break;
    }
    S(0x050E, 2);                                       /* 050E movw ya,!CPUIO1 */
    { uint16_t ya = s_movw_load(sp, SPS_CPUIO1); a = (uint8_t) ya; y = (uint8_t) (ya >> 8); }
    S(0x0510, 2); s_movs(sp, sps_dp(sp, SPS_CPUIO0), x);/* 0510 mov !CPUIO0,x */
    S(0x0512, 1); s_imp(sp); x++; sps_set_zn(sp, x);    /* 0512 inc x */
    S(0x0513, 3); s_movs(sp, LOADER_DEST, a);           /* 0513 mov $0539,a */
    S(0x0516, 3); s_movs(sp, LOADER_DEST2, a);          /* 0516 mov $0542,a */
    S(0x0519, 3); s_movs(sp, LOADER_DEST + 1, y);       /* 0519 mov $053A,y */
    S(0x051C, 3); s_movs(sp, LOADER_DEST2 + 1, y);      /* 051C mov $0543,y */

    /* --- handshake 2: the word count --- */
    for(;;) {                                           /* loc_051F */
      S(0x051F, 2); s_cmp(sp, x, s_read(sp, sps_dp(sp, SPS_CPUIO0)));
      bool wait = !sps_z(sp);
      S(0x0521, 2); s_branch(sp, wait);
      if(!wait) break;
    }
    S(0x0523, 2);                                       /* 0523 movw ya,!CPUIO1 */
    { uint16_t ya = s_movw_load(sp, SPS_CPUIO1); a = (uint8_t) ya; y = (uint8_t) (ya >> 8); }
    S(0x0525, 2); s_movs(sp, sps_dp(sp, SPS_CPUIO0), x);/* 0525 mov !CPUIO0,x */
    S(0x0527, 1); s_imp(sp); x++; sps_set_zn(sp, x);    /* 0527 inc x */
    S(0x0528, 2); s_movs(sp, sps_dp(sp, 0xEA), a);      /* 0528 mov $EA,a */
    S(0x052A, 2); s_movs(sp, sps_dp(sp, 0xEB), y);      /* 052A mov $EB,y */
    S(0x052C, 2); s_decw(sp, 0xEA);                     /* 052C decw $EA */
    bool done = sps_n(sp);
    S(0x052E, 2); s_branch(sp, done);                   /* 052E bmi loader_jump */
    if(done) S_GOTO(0x0556);

    S(0x0530, 2); y = 0x00; sps_set_zn(sp, y);          /* 0530 mov y,#$00 */

    /* --- the data words --- */
    for(;;) {
      for(;;) {                                         /* loc_0532 */
        S(0x0532, 2); s_cmp(sp, x, s_read(sp, sps_dp(sp, SPS_CPUIO0)));
        bool wait = !sps_z(sp);
        S(0x0534, 2); s_branch(sp, wait);
        if(!wait) break;
      }
      S(0x0536, 2); a = s_load(sp, sps_dp(sp, SPS_CPUIO1)); /* 0536 mov a,!CPUIO1 */
      /* 0538 mov $0000+y,a -- the operand the block loop patched, read back out
       * of ARAM at the instant the step above fetched those two bytes. */
      S(0x0538, 3);
      s_idx(sp);
      s_movs(sp, (uint16_t) (sps_aram_r16(sp, LOADER_DEST) + y), a);
      S(0x053B, 2); a = s_load(sp, sps_dp(sp, SPS_CPUIO2)); /* 053B mov a,!CPUIO2 */
      S(0x053D, 2); s_movs(sp, sps_dp(sp, SPS_CPUIO0), x);  /* 053D mov !CPUIO0,x */
      S(0x053F, 1); s_imp(sp); x++; sps_set_zn(sp, x);      /* 053F inc x */
      S(0x0540, 1); s_imp(sp); y++; sps_set_zn(sp, y);      /* 0540 inc y */
      S(0x0541, 3);                                         /* 0541 mov $0000+y,a */
      s_idx(sp);
      s_movs(sp, (uint16_t) (sps_aram_r16(sp, LOADER_DEST2) + y), a);
      S(0x0544, 1); s_imp(sp); y++; sps_set_zn(sp, y);      /* 0544 inc y */
      bool wrapped = y == 0;
      S(0x0545, 2); s_branch(sp, wrapped);                  /* 0545 beq loc_054E */
      if(wrapped) {                                         /* loc_054E */
        S(0x054E, 3); s_inc_mem(sp, LOADER_DEST + 1);       /* 054E inc $053A */
        S(0x0551, 3); s_inc_mem(sp, LOADER_DEST2 + 1);      /* 0551 inc $0543 */
        S(0x0554, 2); s_branch(sp, true);                   /* 0554 bra loc_0547 */
      }
      S(0x0547, 2); s_decw(sp, 0xEA);                       /* 0547 decw $EA */
      bool more = !sps_n(sp);
      S(0x0549, 2); s_branch(sp, more);                     /* 0549 bpl loc_0532 */
      if(!more) break;
    }
    S(0x054B, 3);                                           /* 054B jmp loader_block_loop */
  }
}

/* ---------------------------------------------------------------------------
 * loader_jump — $0556
 *
 * Word count zero: save the handshake counter and jump to the destination the
 * block header carried. `jmp ($0539+x)` with X = 0 reads the vector out of the
 * operand bytes the block loop wrote, so the ROM literally jumps through its own
 * instruction. The 65816 sends $0672 after every upload batch, which is how the
 * driver is (re)started.
 * ------------------------------------------------------------------------- */
static void loader_jump(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  S(0x0556, 2); s_movs(sp, sps_dp(sp, 0xE9), x);      /* 0556 mov $E9,x */
  S(0x0558, 2); x = 0x00; sps_set_zn(sp, x);          /* 0558 mov x,#$00 */
  S(0x055A, 3);                                       /* 055A jmp ($0539+x) */
  uint16_t target = s_jmp_iax(sp, LOADER_DEST, x);
  S_GOTO(target);
}

static const SpcRecompEntry kLoader[] = {
  { 0x04d8, "spc_loader",        spc_loader },
  { 0x04f3, "loader_reset_dsp",  loader_reset_dsp },
  { 0x050a, "loader_block_loop", loader_block_loop },
  { 0x0556, "loader_jump",       loader_jump },
};
RECOMP_SPC_REGISTER(kLoader)
