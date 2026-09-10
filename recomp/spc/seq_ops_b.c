/* Sequence command handlers, the second half: SPC $0E45-$0FC1 (spc/driver.asm).
 *
 * A sequence event byte below $80 is a command, and seq_fetch ($0850) dispatches
 * it with `push x ; asl a ; mov x,a ; jmp (seq_cmd_table+x)` -- no return
 * address, just the channel slot pushed under the handler. So every handler here
 * starts by recovering that slot, either with its own `pop x` or through
 * seq_pop_x ($0B64), and ends by jumping into one of the shared tails that add
 * the event's length to the channel's sequence pointer:
 *
 *     loc_0B78  tmp0 = 2   loc_0BBC  tmp0 = 3   loc_0D6A  tmp0 = 4
 *     loc_0F09  tmp0 = 1   seq_advance5 ($0FA9) tmp0 = 5
 *     loc_0B7B             the add itself: seq_ptr[x] += tmp0, a = 1
 *
 * The tails belong to other routines, so a body reaching one publishes its
 * registers and points the pc at it (S_GOTO): the routine that owns the address
 * runs next, its own hook if it has one and the driver's own code otherwise.
 *
 * This half is the DSP-facing set -- ADSR, master volume, echo, the FIR filter,
 * the noise clock -- plus the transpose/finetune/stored-note commands and the
 * four orphan handlers behind the stale table entries $26, $27, $2B and $2C,
 * which no sequence in the ROM emits ($26 and $27 chain through loc_0F86 and
 * fall into seq_advance5, which is live: seq_vibrato_delay jumps to it).
 * spc_map.txt has the opcode table and the per-command operand counts; the
 * counts are what the tail's tmp0 encodes.
 */
#include <stdint.h>
#include <stdbool.h>

#include "spc_state.h"
#include "spc_time.h"

#define SEQ_POP_X       0x0B64  /* drop the return address, restore x, retrigger */
#define SEQ_RETRIGGER   0x0B69  /* duration[x] = 1, gate[x] = 0 */
#define SEQ_READ_ADSR   0x0E4E

/* the shared tails, by the labels spc/driver.asm gives them */
#define LOC_0B78        0x0B78  /* tmp0 = 2, then the pointer add */
#define LOC_0B7B        0x0B7B  /* the pointer add */
#define LOC_0BBC        0x0BBC  /* tmp0 = 3, then the pointer add */
#define LOC_0D6A        0x0D6A  /* tmp0 = 4, then the pointer add */
#define LOC_0E9E        0x0E9E  /* seq_set_note_E1's tail: retrigger, then loc_0B78 */
#define LOC_0F09        0x0F09  /* seq_echo_on's tail: tmp0 = 1, then the pointer add */
#define LOC_0F61        0x0F61  /* seq_noise_on's tail: retrigger, then loc_0F09 */
#define LOC_0F86        0x0F86  /* orphan_slide_down2's body */
#define SEQ_ADVANCE5    0x0FA9

/* One modelled instruction inside a helper that reports the yield to its caller
 * rather than returning from the body itself. */
#define SC(addr, n) do { if(s_step(sp, (uint16_t) (addr), (n), a, x, y)) return true; } while(0)

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

/* `call seq_pop_x` ($0B64), the prologue ten of these handlers share: it
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
 * seq_ops_a.c carries the same helper for the fourteen handlers in its half.
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
 * seq_adsr — $0E45   seq cmd $10: ADSR1, ADSR2
 * ------------------------------------------------------------------------- */
static void seq_adsr(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  if(call_seq_pop_x(sp, 0x0E45, &a, &x, &y)) return;       /* 0E45 call seq_pop_x */
  S(0x0E48, 3);                                         /* 0E48 call seq_read_adsr */
  if(call_sub(sp, 0x0E4B, SEQ_READ_ADSR)) return;
  a = sps_a(sp); x = sps_x(sp); y = sps_y(sp);
  S(0x0E4B, 3);                                         /* 0E4B jmp loc_0BBC */
  S_GOTO(LOC_0BBC);
}

/* ---------------------------------------------------------------------------
 * seq_read_adsr — $0E4E
 *
 * Two operand bytes into the channel's ADSR1/ADSR2 slots. seq_instr_full ($0BAD)
 * calls it too, so this hook fires from both.
 * ------------------------------------------------------------------------- */
static void seq_read_adsr(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  S(0x0E4E, 2); a = s_load(sp, s_adr_idy(sp, 0x00, y));  /* 0E4E mov a,($00)+y */
  S(0x0E50, 3); s_idx(sp);                               /* 0E50 mov $0274+x,a */
  s_movs(sp, (uint16_t) (0x0274 + x), a);
  S(0x0E53, 1); s_imp(sp); y++; sps_set_zn(sp, y);       /* 0E53 inc y */
  S(0x0E54, 2); a = s_load(sp, s_adr_idy(sp, 0x00, y));  /* 0E54 mov a,($00)+y */
  S(0x0E56, 3); s_idx(sp);                               /* 0E56 mov $0284+x,a */
  s_movs(sp, (uint16_t) (0x0284 + x), a);
  S(0x0E59, 1); S_PUB(); sps_ret(sp);                    /* 0E59 ret */
}

/* ---------------------------------------------------------------------------
 * seq_master_volume — $0E5A   seq cmd $11: MVOLL, MVOLR
 *
 * The one sequence command Dream added over the driver it inherited: the DKC2
 * driver's table stops at $24 with no master-volume opcode of its own. The mono
 * flag ($1D) decides what the second DSP register gets -- the two operand bytes
 * averaged with `clrc ; adc ; ror`, or the right byte as written -- and $0230 /
 * $0231 keep the pair for the fade in cmd3 and for seq_master_percent.
 * ------------------------------------------------------------------------- */
static void seq_master_volume(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  if(call_seq_pop_x(sp, 0x0E5A, &a, &x, &y)) return;       /* 0E5A call seq_pop_x */
  S(0x0E5D, 3); s_movs(sp, sps_dp(sp, SPS_DSPADDR), 0x0C); /* 0E5D !DSPADDR = MVOLL */
  S(0x0E60, 2); a = s_load(sp, sps_dp(sp, 0x1D));          /* 0E60 mov a,$1D */
  bool stereo = sps_z(sp);
  S(0x0E62, 2); s_branch(sp, stereo);                      /* 0E62 beq loc_0E75 */
  if(!stereo) {
    S(0x0E64, 2); a = s_load(sp, s_adr_idy(sp, 0x00, y));  /* 0E64 mov a,($00)+y */
    S(0x0E66, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), a);  /* 0E66 !DSPDATA = a */
    S(0x0E68, 1); s_imp(sp); y++; sps_set_zn(sp, y);       /* 0E68 inc y */
    S(0x0E69, 1); s_imp(sp); sps_set_c(sp, false);         /* 0E69 clrc */
    S(0x0E6A, 2);                                          /* 0E6A adc a,($00)+y */
    a = s_adc(sp, a, s_read(sp, s_adr_idy(sp, 0x00, y)));
    S(0x0E6C, 1); a = s_ror_a(sp, a);                      /* 0E6C ror a */
    S(0x0E6D, 3); s_movs(sp, 0x0230, a);                   /* 0E6D mov $0230,a */
    S(0x0E70, 3); s_movs(sp, sps_dp(sp, SPS_DSPADDR), 0x1C); /* 0E70 !DSPADDR = MVOLR */
    S(0x0E73, 2); s_branch(sp, true);                      /* 0E73 bra loc_0E82 */
  } else {
    S(0x0E75, 2); a = s_load(sp, s_adr_idy(sp, 0x00, y));  /* 0E75 mov a,($00)+y */
    S(0x0E77, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), a);  /* 0E77 !DSPDATA = a */
    S(0x0E79, 3); s_movs(sp, 0x0230, a);                   /* 0E79 mov $0230,a */
    S(0x0E7C, 1); s_imp(sp); y++; sps_set_zn(sp, y);       /* 0E7C inc y */
    S(0x0E7D, 3); s_movs(sp, sps_dp(sp, SPS_DSPADDR), 0x1C); /* 0E7D !DSPADDR = MVOLR */
    S(0x0E80, 2); a = s_load(sp, s_adr_idy(sp, 0x00, y));  /* 0E80 mov a,($00)+y */
  }
  S(0x0E82, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), a);    /* 0E82 !DSPDATA = a */
  S(0x0E84, 3); s_movs(sp, 0x0231, a);                     /* 0E84 mov $0231,a */
  S(0x0E87, 3); s_movs(sp, sps_dp(sp, 0x00), 0x03);        /* 0E87 mov $00,#$03 */
  S(0x0E8A, 3);                                            /* 0E8A jmp loc_0B7B */
  S_GOTO(LOC_0B7B);
}

/* ---------------------------------------------------------------------------
 * seq_set_note_E0 — $0E8D   seq cmd $1C: the note event $E0 plays
 *
 * Leaves through seq_set_note_E1's tail at loc_0E9E, which retriggers the
 * channel and takes the two-byte tail.
 * ------------------------------------------------------------------------- */
static void seq_set_note_E0(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  S(0x0E8D, 1); x = s_pop(sp);                           /* 0E8D pop x */
  S(0x0E8E, 2); y = 0x01; sps_set_zn(sp, y);             /* 0E8E mov y,#$01 */
  S(0x0E90, 2); a = s_load(sp, s_adr_idy(sp, 0x00, y));  /* 0E90 mov a,($00)+y */
  S(0x0E92, 2); s_movs(sp, s_adr_dpx(sp, 0x0C, x), a);   /* 0E92 mov $0C+x,a */
  S(0x0E94, 3);                                          /* 0E94 jmp loc_0E9E */
  S_GOTO(LOC_0E9E);
}

/* ---------------------------------------------------------------------------
 * seq_set_note_E1 — $0E97   seq cmd $1D: the note event $E1 plays
 * ------------------------------------------------------------------------- */
/* Entered in its middle as well: loc_0E9E. --no-cpu resolves every pc
 * through the registry, so an address the driver jumps into needs a body that
 * can start there. */
static void seq_set_note_E1_at(SpcState* sp, uint16_t entry) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);
  if(entry == 0x0E9E) goto loc_0E9E;

  S(0x0E97, 1); x = s_pop(sp);                           /* 0E97 pop x */
  S(0x0E98, 2); y = 0x01; sps_set_zn(sp, y);             /* 0E98 mov y,#$01 */
  S(0x0E9A, 2); a = s_load(sp, s_adr_idy(sp, 0x00, y));  /* 0E9A mov a,($00)+y */
  S(0x0E9C, 2); s_movs(sp, s_adr_dpx(sp, 0x14, x), a);   /* 0E9C mov $14+x,a */
loc_0E9E:
  S(0x0E9E, 3);                                          /* 0E9E call seq_retrigger */
  if(call_sub(sp, 0x0EA1, SEQ_RETRIGGER)) return;
  a = sps_a(sp); x = sps_x(sp); y = sps_y(sp);
  S(0x0EA1, 3);                                          /* 0EA1 jmp loc_0B78 */
  S_GOTO(LOC_0B78);
}

static void seq_set_note_E1(SpcState* sp) { seq_set_note_E1_at(sp, 0x0E97); }
static void loc_0E9E(SpcState* sp) { seq_set_note_E1_at(sp, 0x0E9E); }

/* ---------------------------------------------------------------------------
 * seq_finetune — $0EA4   seq cmd $12: finetune[x], signed 1/256-semitone steps
 * ------------------------------------------------------------------------- */
static void seq_finetune(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  if(call_seq_pop_x(sp, 0x0EA4, &a, &x, &y)) return;       /* 0EA4 call seq_pop_x */
  S(0x0EA7, 2); a = s_load(sp, s_adr_idy(sp, 0x00, y));  /* 0EA7 mov a,($00)+y */
  S(0x0EA9, 2); s_movs(sp, s_adr_dpx(sp, 0x64, x), a);   /* 0EA9 mov $64+x,a */
  S(0x0EAB, 3);                                          /* 0EAB jmp loc_0B78 */
  S_GOTO(LOC_0B78);
}

/* ---------------------------------------------------------------------------
 * seq_transpose — $0EAE   seq cmd $13
 *
 * The `mov $24+x,a` writes the zero seq_retrigger left in A, so the gate is
 * cleared a second time; the operand byte is the new transpose.
 * ------------------------------------------------------------------------- */
static void seq_transpose(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  if(call_seq_pop_x(sp, 0x0EAE, &a, &x, &y)) return;       /* 0EAE call seq_pop_x */
  S(0x0EB1, 2); s_movs(sp, s_adr_dpx(sp, 0x24, x), a);   /* 0EB1 mov $24+x,a */
  S(0x0EB3, 2); a = s_load(sp, s_adr_idy(sp, 0x00, y));  /* 0EB3 mov a,($00)+y */
  S(0x0EB5, 3); s_idx(sp);                               /* 0EB5 mov $0140+x,a */
  s_movs(sp, (uint16_t) (0x0140 + x), a);
  S(0x0EB8, 3);                                          /* 0EB8 jmp loc_0B78 */
  S_GOTO(LOC_0B78);
}

/* ---------------------------------------------------------------------------
 * seq_transpose_add — $0EBB   seq cmd $14
 * ------------------------------------------------------------------------- */
static void seq_transpose_add(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  if(call_seq_pop_x(sp, 0x0EBB, &a, &x, &y)) return;       /* 0EBB call seq_pop_x */
  S(0x0EBE, 2); a = s_load(sp, s_adr_idy(sp, 0x00, y));  /* 0EBE mov a,($00)+y */
  S(0x0EC0, 1); s_imp(sp); sps_set_c(sp, false);         /* 0EC0 clrc */
  S(0x0EC1, 3); s_idx(sp);                               /* 0EC1 adc a,$0140+x */
  a = s_adc(sp, a, s_read(sp, (uint16_t) (0x0140 + x)));
  S(0x0EC4, 3); s_idx(sp);                               /* 0EC4 mov $0140+x,a */
  s_movs(sp, (uint16_t) (0x0140 + x), a);
  S(0x0EC7, 3);                                          /* 0EC7 jmp loc_0B78 */
  S_GOTO(LOC_0B78);
}

/* ---------------------------------------------------------------------------
 * seq_echo_setup — $0ECA   seq cmd $15: EFB, EVOLL, EVOLR, and FLG = 0
 *
 * Three operand bytes into the echo registers, the two volumes mirrored into
 * $0232/$0233 for the fade, and then the echo-off flag ($04B5) cleared and
 * written through FLG -- which is what actually switches the echo unit on.
 * ------------------------------------------------------------------------- */
static void seq_echo_setup(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  S(0x0ECA, 1); x = s_pop(sp);                             /* 0ECA pop x */
  S(0x0ECB, 3); s_movs(sp, sps_dp(sp, SPS_DSPADDR), 0x0D); /* 0ECB !DSPADDR = EFB */
  S(0x0ECE, 2); y = 0x01; sps_set_zn(sp, y);               /* 0ECE mov y,#$01 */
  S(0x0ED0, 2); a = s_load(sp, s_adr_idy(sp, 0x00, y));    /* 0ED0 mov a,($00)+y */
  S(0x0ED2, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), a);    /* 0ED2 !DSPDATA = a */
  S(0x0ED4, 1); s_imp(sp); y++; sps_set_zn(sp, y);         /* 0ED4 inc y */
  S(0x0ED5, 3); s_movs(sp, sps_dp(sp, SPS_DSPADDR), 0x2C); /* 0ED5 !DSPADDR = EVOLL */
  S(0x0ED8, 2); a = s_load(sp, s_adr_idy(sp, 0x00, y));    /* 0ED8 mov a,($00)+y */
  S(0x0EDA, 3); s_movs(sp, 0x0232, a);                     /* 0EDA mov $0232,a */
  S(0x0EDD, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), a);    /* 0EDD !DSPDATA = a */
  S(0x0EDF, 3); s_movs(sp, sps_dp(sp, SPS_DSPADDR), 0x3C); /* 0EDF !DSPADDR = EVOLR */
  S(0x0EE2, 1); s_imp(sp); y++; sps_set_zn(sp, y);         /* 0EE2 inc y */
  S(0x0EE3, 2); a = s_load(sp, s_adr_idy(sp, 0x00, y));    /* 0EE3 mov a,($00)+y */
  S(0x0EE5, 3); s_movs(sp, 0x0233, a);                     /* 0EE5 mov $0233,a */
  S(0x0EE8, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), a);    /* 0EE8 !DSPDATA = a */
  S(0x0EEA, 2); a = 0x00; sps_set_zn(sp, a);               /* 0EEA mov a,#$00 */
  S(0x0EEC, 3); s_movs(sp, 0x04B5, a);                     /* 0EEC mov $04B5,a */
  S(0x0EEF, 3); s_movs(sp, sps_dp(sp, SPS_DSPADDR), 0x6C); /* 0EEF !DSPADDR = FLG */
  S(0x0EF2, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), a);    /* 0EF2 !DSPDATA = a */
  S(0x0EF4, 3);                                            /* 0EF4 jmp loc_0D6A */
  S_GOTO(LOC_0D6A);
}

/* ---------------------------------------------------------------------------
 * seq_echo_on — $0EF7   seq cmd $16: EON |= the channel's voice bit
 *
 * loc_0F09, the one-byte tail four other handlers jump to, is the last two
 * instructions of this routine.
 * ------------------------------------------------------------------------- */
/* Entered in its middle as well: loc_0F09. --no-cpu resolves every pc
 * through the registry, so an address the driver jumps into needs a body that
 * can start there. */
static void seq_echo_on_at(SpcState* sp, uint16_t entry) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);
  if(entry == 0x0F09) goto loc_0F09;

  if(call_seq_pop_x(sp, 0x0EF7, &a, &x, &y)) return;       /* 0EF7 call seq_pop_x */
  S(0x0EFA, 3); s_movs(sp, sps_dp(sp, SPS_DSPADDR), 0x4D); /* 0EFA !DSPADDR = EON */
  S(0x0EFD, 3); s_idx(sp);                                 /* 0EFD mov a,$0FC8+x */
  a = s_load(sp, (uint16_t) (0x0FC8 + x));
  S(0x0F00, 2);                                            /* 0F00 or a,!DSPDATA */
  a = s_or(sp, a, s_read(sp, sps_dp(sp, SPS_DSPDATA)));
  S(0x0F02, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), a);    /* 0F02 !DSPDATA = a */
  S(0x0F04, 2); a = 0x01; sps_set_zn(sp, a);               /* 0F04 mov a,#$01 */
  S(0x0F06, 3); s_idx(sp);                                 /* 0F06 mov $0294+x,a */
  s_movs(sp, (uint16_t) (0x0294 + x), a);
loc_0F09:
  S(0x0F09, 3); s_movs(sp, sps_dp(sp, 0x00), 0x01);        /* 0F09 mov $00,#$01 */
  S(0x0F0C, 3);                                            /* 0F0C jmp loc_0B7B */
  S_GOTO(LOC_0B7B);
}

static void seq_echo_on(SpcState* sp) { seq_echo_on_at(sp, 0x0EF7); }
static void loc_0F09(SpcState* sp) { seq_echo_on_at(sp, 0x0F09); }

/* ---------------------------------------------------------------------------
 * seq_echo_off — $0F0F   seq cmd $17 (and the stale entries $30 and $32)
 *
 * Clears the voice's EON bit and its echo flag, and -- unlike seq_echo_on --
 * retriggers the channel by hand rather than through seq_retrigger.
 * ------------------------------------------------------------------------- */
static void seq_echo_off(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  S(0x0F0F, 1); x = s_pop(sp);                             /* 0F0F pop x */
  S(0x0F10, 3); s_movs(sp, sps_dp(sp, SPS_DSPADDR), 0x4D); /* 0F10 !DSPADDR = EON */
  S(0x0F13, 3); s_idx(sp);                                 /* 0F13 mov a,$0FC8+x */
  a = s_load(sp, (uint16_t) (0x0FC8 + x));
  S(0x0F16, 2); a = s_eor(sp, a, 0xFF);                    /* 0F16 eor a,#$FF */
  S(0x0F18, 2);                                            /* 0F18 and a,!DSPDATA */
  a = s_and(sp, a, s_read(sp, sps_dp(sp, SPS_DSPDATA)));
  S(0x0F1A, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), a);    /* 0F1A !DSPDATA = a */
  S(0x0F1C, 2); a = 0x00; sps_set_zn(sp, a);               /* 0F1C mov a,#$00 */
  S(0x0F1E, 3); s_idx(sp);                                 /* 0F1E mov $0294+x,a */
  s_movs(sp, (uint16_t) (0x0294 + x), a);
  S(0x0F21, 2); s_movs(sp, s_adr_dpx(sp, 0x24, x), a);     /* 0F21 mov $24+x,a */
  S(0x0F23, 1); s_imp(sp); a++; sps_set_zn(sp, a);         /* 0F23 inc a */
  S(0x0F24, 2); s_movs(sp, s_adr_dpx(sp, 0x34, x), a);     /* 0F24 mov $34+x,a */
  S(0x0F26, 3);                                            /* 0F26 jmp loc_0F09 */
  S_GOTO(LOC_0F09);
}

/* ---------------------------------------------------------------------------
 * seq_fir — $0F29   seq cmd $18: the eight FIR coefficients
 *
 * The loop walks $F2 itself: FIR0 is DSP register $0F and the eight taps are
 * $10 apart, so `adc !DSPADDR,#$10` is the index and `cmp !DSPADDR,#$8F` -- one
 * step past FIR7 at $7F -- is the exit test. Nine bytes of event, eight of them
 * operands.
 * ------------------------------------------------------------------------- */
static void seq_fir(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  if(call_seq_pop_x(sp, 0x0F29, &a, &x, &y)) return;       /* 0F29 call seq_pop_x */
  S(0x0F2C, 3); s_movs(sp, sps_dp(sp, SPS_DSPADDR), 0x0F); /* 0F2C !DSPADDR = FIR0 */
  for(;;) {                                                /* loc_0F2F */
    S(0x0F2F, 2); a = s_load(sp, s_adr_idy(sp, 0x00, y));  /* 0F2F mov a,($00)+y */
    S(0x0F31, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), a);  /* 0F31 !DSPDATA = a */
    S(0x0F33, 1); s_imp(sp); y++; sps_set_zn(sp, y);       /* 0F33 inc y */
    S(0x0F34, 1); s_imp(sp); sps_set_c(sp, false);         /* 0F34 clrc */
    S(0x0F35, 3);                                          /* 0F35 adc !DSPADDR,#$10 */
    s_adcm(sp, sps_dp(sp, SPS_DSPADDR), 0x10);
    S(0x0F38, 3);                                          /* 0F38 cmp !DSPADDR,#$8F */
    s_cmpm(sp, sps_dp(sp, SPS_DSPADDR), 0x8F);
    bool more = !sps_z(sp);
    S(0x0F3B, 2); s_branch(sp, more);                      /* 0F3B bne loc_0F2F */
    if(!more) break;
  }
  S(0x0F3D, 3); s_movs(sp, sps_dp(sp, 0x00), 0x09);        /* 0F3D mov $00,#$09 */
  S(0x0F40, 3);                                            /* 0F40 jmp loc_0B7B */
  S_GOTO(LOC_0B7B);
}

/* ---------------------------------------------------------------------------
 * seq_noise_clock — $0F43   seq cmd $19: the FLG noise-rate field
 *
 * FLG carries the noise clock in its low five bits and the echo-write disable
 * in bit 5, so the operand is kept in $04B4 and or-ed with the echo-off flag
 * ($04B5) before it goes to the DSP; seq_echo_setup writes the same register
 * from the other side.
 * ------------------------------------------------------------------------- */
static void seq_noise_clock(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  if(call_seq_pop_x(sp, 0x0F43, &a, &x, &y)) return;       /* 0F43 call seq_pop_x */
  S(0x0F46, 2); a = s_load(sp, s_adr_idy(sp, 0x00, y));    /* 0F46 mov a,($00)+y */
  S(0x0F48, 3); s_movs(sp, 0x04B4, a);                     /* 0F48 mov $04B4,a */
  S(0x0F4B, 3); a = s_or(sp, a, s_read(sp, 0x04B5));       /* 0F4B or a,$04B5 */
  S(0x0F4E, 3); s_movs(sp, sps_dp(sp, SPS_DSPADDR), 0x6C); /* 0F4E !DSPADDR = FLG */
  S(0x0F51, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), a);    /* 0F51 !DSPDATA = a */
  S(0x0F53, 3);                                            /* 0F53 jmp loc_0B78 */
  S_GOTO(LOC_0B78);
}

/* ---------------------------------------------------------------------------
 * seq_noise_on — $0F56   seq cmd $1A: NON |= the channel's voice bit
 *
 * loc_0F61, its two-instruction tail, is where seq_noise_off comes back in.
 * ------------------------------------------------------------------------- */
/* Entered in its middle as well: loc_0F61. --no-cpu resolves every pc
 * through the registry, so an address the driver jumps into needs a body that
 * can start there. */
static void seq_noise_on_at(SpcState* sp, uint16_t entry) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);
  if(entry == 0x0F61) goto loc_0F61;

  S(0x0F56, 1); x = s_pop(sp);                             /* 0F56 pop x */
  S(0x0F57, 3); s_movs(sp, sps_dp(sp, SPS_DSPADDR), 0x3D); /* 0F57 !DSPADDR = NON */
  S(0x0F5A, 3); s_idx(sp);                                 /* 0F5A mov a,$0FC8+x */
  a = s_load(sp, (uint16_t) (0x0FC8 + x));
  S(0x0F5D, 2);                                            /* 0F5D or a,!DSPDATA */
  a = s_or(sp, a, s_read(sp, sps_dp(sp, SPS_DSPDATA)));
  S(0x0F5F, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), a);    /* 0F5F !DSPDATA = a */
loc_0F61:
  S(0x0F61, 3);                                            /* 0F61 call seq_retrigger */
  if(call_sub(sp, 0x0F64, SEQ_RETRIGGER)) return;
  a = sps_a(sp); x = sps_x(sp); y = sps_y(sp);
  S(0x0F64, 3);                                            /* 0F64 jmp loc_0F09 */
  S_GOTO(LOC_0F09);
}

static void seq_noise_on(SpcState* sp) { seq_noise_on_at(sp, 0x0F56); }
static void loc_0F61(SpcState* sp) { seq_noise_on_at(sp, 0x0F61); }

/* ---------------------------------------------------------------------------
 * seq_noise_off — $0F67   seq cmd $1B
 * ------------------------------------------------------------------------- */
static void seq_noise_off(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  S(0x0F67, 1); x = s_pop(sp);                             /* 0F67 pop x */
  S(0x0F68, 3); s_movs(sp, sps_dp(sp, SPS_DSPADDR), 0x3D); /* 0F68 !DSPADDR = NON */
  S(0x0F6B, 3); s_idx(sp);                                 /* 0F6B mov a,$0FC8+x */
  a = s_load(sp, (uint16_t) (0x0FC8 + x));
  S(0x0F6E, 2); a = s_eor(sp, a, 0xFF);                    /* 0F6E eor a,#$FF */
  S(0x0F70, 2);                                            /* 0F70 and a,!DSPDATA */
  a = s_and(sp, a, s_read(sp, sps_dp(sp, SPS_DSPDATA)));
  S(0x0F72, 2); s_movs(sp, sps_dp(sp, SPS_DSPDATA), a);    /* 0F72 !DSPDATA = a */
  S(0x0F74, 3);                                            /* 0F74 jmp loc_0F61 */
  S_GOTO(LOC_0F61);
}

/* ---------------------------------------------------------------------------
 * orphan_slide_up2 — $0F77   stale seq_cmd_table entry $26
 *
 * A second pair of slide handlers, five operand bytes like seq_slide_up/_down
 * but reading them from Y = 4 downward instead of upward. Nothing in the ROM
 * emits command $26 or $27, so neither of these is ever entered; they are here
 * because they are table entries and the dispatch is not honest without them.
 * ------------------------------------------------------------------------- */
static void orphan_slide_up2(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  S(0x0F77, 1); x = s_pop(sp);                             /* 0F77 pop x */
  S(0x0F78, 2); y = 0x04; sps_set_zn(sp, y);               /* 0F78 mov y,#$04 */
  S(0x0F7A, 2); a = s_load(sp, s_adr_idy(sp, 0x00, y));    /* 0F7A mov a,($00)+y */
  S(0x0F7C, 2); a = s_eor(sp, a, 0xFF);                    /* 0F7C eor a,#$FF */
  S(0x0F7E, 1); s_imp(sp); a++; sps_set_zn(sp, a);         /* 0F7E inc a */
  S(0x0F7F, 2); s_branch(sp, true);                        /* 0F7F bra loc_0F86 */
  S_GOTO(LOC_0F86);
}

/* ---------------------------------------------------------------------------
 * orphan_slide_down2 — $0F81   stale seq_cmd_table entry $27
 *
 * Falls through into seq_advance5.
 * ------------------------------------------------------------------------- */
/* Entered in its middle as well: loc_0F86. --no-cpu resolves every pc
 * through the registry, so an address the driver jumps into needs a body that
 * can start there. */
static void orphan_slide_down2_at(SpcState* sp, uint16_t entry) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);
  if(entry == 0x0F86) goto loc_0F86;

  S(0x0F81, 1); x = s_pop(sp);                             /* 0F81 pop x */
  S(0x0F82, 2); y = 0x04; sps_set_zn(sp, y);               /* 0F82 mov y,#$04 */
  S(0x0F84, 2); a = s_load(sp, s_adr_idy(sp, 0x00, y));    /* 0F84 mov a,($00)+y */
loc_0F86:
  S(0x0F86, 3); s_idx(sp);                                 /* 0F86 mov $01B0+x,a */
  s_movs(sp, (uint16_t) (0x01B0 + x), a);
  S(0x0F89, 3); s_idx(sp);                                 /* 0F89 mov a,$0150+x */
  a = s_load(sp, (uint16_t) (0x0150 + x));
  S(0x0F8C, 2); a = s_or(sp, a, 0x01);                     /* 0F8C or a,#$01 */
  S(0x0F8E, 3); s_idx(sp);                                 /* 0F8E mov $0150+x,a */
  s_movs(sp, (uint16_t) (0x0150 + x), a);
  S(0x0F91, 3);                                            /* 0F91 call seq_retrigger */
  if(call_sub(sp, 0x0F94, SEQ_RETRIGGER)) return;
  a = sps_a(sp); x = sps_x(sp); y = sps_y(sp);
  S(0x0F94, 2); a = s_load(sp, s_adr_idy(sp, 0x00, y));    /* 0F94 mov a,($00)+y */
  S(0x0F96, 3); s_idx(sp);                                 /* 0F96 mov $0160+x,a */
  s_movs(sp, (uint16_t) (0x0160 + x), a);
  S(0x0F99, 1); s_imp(sp); y++; sps_set_zn(sp, y);         /* 0F99 inc y */
  S(0x0F9A, 2); a = s_load(sp, s_adr_idy(sp, 0x00, y));    /* 0F9A mov a,($00)+y */
  S(0x0F9C, 3); s_idx(sp);                                 /* 0F9C mov $0170+x,a */
  s_movs(sp, (uint16_t) (0x0170 + x), a);
  S(0x0F9F, 1); s_imp(sp); y++; sps_set_zn(sp, y);         /* 0F9F inc y */
  S(0x0FA0, 2); a = s_load(sp, s_adr_idy(sp, 0x00, y));    /* 0FA0 mov a,($00)+y */
  S(0x0FA2, 3); s_idx(sp);                                 /* 0FA2 mov $0190+x,a */
  s_movs(sp, (uint16_t) (0x0190 + x), a);
  S(0x0FA5, 1); a = s_asl_a(sp, a);                        /* 0FA5 asl a */
  S(0x0FA6, 3); s_idx(sp);                                 /* 0FA6 mov $0180+x,a */
  s_movs(sp, (uint16_t) (0x0180 + x), a);
  S_GOTO(SEQ_ADVANCE5);                                    /* falls into seq_advance5 */
}

static void orphan_slide_down2(SpcState* sp) { orphan_slide_down2_at(sp, 0x0F81); }
static void loc_0F86(SpcState* sp) { orphan_slide_down2_at(sp, 0x0F86); }

/* ---------------------------------------------------------------------------
 * seq_advance5 — $0FA9   the five-byte tail
 *
 * seq_vibrato_delay ($0E22) jumps here, and orphan_slide_down2 falls in.
 * ------------------------------------------------------------------------- */
static void seq_advance5(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  S(0x0FA9, 3); s_movs(sp, sps_dp(sp, 0x00), 0x05);        /* 0FA9 mov $00,#$05 */
  S(0x0FAC, 3);                                            /* 0FAC jmp loc_0B7B */
  S_GOTO(LOC_0B7B);
}

/* ---------------------------------------------------------------------------
 * orphan_gate_on — $0FAF   stale seq_cmd_table entry $2B
 *
 * gate_mode[x] = 1: seq_set_length reads it to decide whether a note length
 * carries a gate byte after it. Command $2B is never emitted, so the mode is
 * only ever the zero dsp_init leaves.
 * ------------------------------------------------------------------------- */
static void orphan_gate_on(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  if(call_seq_pop_x(sp, 0x0FAF, &a, &x, &y)) return;       /* 0FAF call seq_pop_x */
  S(0x0FB2, 1); s_imp(sp); a++; sps_set_zn(sp, a);         /* 0FB2 inc a */
  S(0x0FB3, 3); s_idx(sp);                                 /* 0FB3 mov $01D0+x,a */
  s_movs(sp, (uint16_t) (0x01D0 + x), a);
  S(0x0FB6, 3);                                            /* 0FB6 jmp loc_0F09 */
  S_GOTO(LOC_0F09);
}

/* ---------------------------------------------------------------------------
 * orphan_gate_off — $0FB9   stale seq_cmd_table entry $2C
 *
 * The same with the zero seq_retrigger left in A.
 * ------------------------------------------------------------------------- */
static void orphan_gate_off(SpcState* sp) {
  uint8_t a = sps_a(sp), x = sps_x(sp), y = sps_y(sp);

  if(call_seq_pop_x(sp, 0x0FB9, &a, &x, &y)) return;       /* 0FB9 call seq_pop_x */
  S(0x0FBC, 3); s_idx(sp);                                 /* 0FBC mov $01D0+x,a */
  s_movs(sp, (uint16_t) (0x01D0 + x), a);
  S(0x0FBF, 3);                                            /* 0FBF jmp loc_0F09 */
  S_GOTO(LOC_0F09);
}

static const SpcRecompEntry kSeqOpsB[] = {
  { 0x0e45, "seq_adsr",            seq_adsr },
  { 0x0e4e, "seq_read_adsr",       seq_read_adsr },
  { 0x0e5a, "seq_master_volume",   seq_master_volume },
  { 0x0e8d, "seq_set_note_E0",     seq_set_note_E0 },
  { 0x0e97, "seq_set_note_E1",     seq_set_note_E1 },
  { 0x0e9e, "loc_0E9E",           loc_0E9E },
  { 0x0ea4, "seq_finetune",        seq_finetune },
  { 0x0eae, "seq_transpose",       seq_transpose },
  { 0x0ebb, "seq_transpose_add",   seq_transpose_add },
  { 0x0eca, "seq_echo_setup",      seq_echo_setup },
  { 0x0ef7, "seq_echo_on",         seq_echo_on },
  { 0x0f09, "loc_0F09",           loc_0F09 },
  { 0x0f0f, "seq_echo_off",        seq_echo_off },
  { 0x0f29, "seq_fir",             seq_fir },
  { 0x0f43, "seq_noise_clock",     seq_noise_clock },
  { 0x0f56, "seq_noise_on",        seq_noise_on },
  { 0x0f61, "loc_0F61",           loc_0F61 },
  { 0x0f67, "seq_noise_off",       seq_noise_off },
  { 0x0f77, "orphan_slide_up2",    orphan_slide_up2 },
  { 0x0f81, "orphan_slide_down2",  orphan_slide_down2 },
  { 0x0f86, "loc_0F86",           loc_0F86 },
  { 0x0fa9, "seq_advance5",        seq_advance5 },
  { 0x0faf, "orphan_gate_on",      orphan_gate_on },
  { 0x0fb9, "orphan_gate_off",     orphan_gate_off },
};
RECOMP_SPC_REGISTER(kSeqOpsB)
