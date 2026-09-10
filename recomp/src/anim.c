/* Velocity-to-animation-rate handlers, bank $C0.
 *
 * These are the four live entries (plus three unreferenced siblings, of which
 * anim_rate_1_2 is converted here because it is one of the fall-through steps)
 * of anim_rate_fn_table at $C0:B66A. entity_update_tick reaches them with
 * jmp ($0004) at $C0:99B6, so they are not called: they *are* the tail of their
 * caller, and every one of them falls through into the shared store at
 * $C0:99D4. See docs/handler_tables.md section 1.
 *
 * Entry: A = |entity_vel_x| (the caller has already taken the absolute value),
 * X = entity index (slot * 2), Y = the table index, DB = $80, m0x0. The stack
 * carries the second half of the pea $8080 at $C0:98EE, which the tail's plb
 * pulls back.
 *
 * Because the fall-through chain is the routine, a hook installed on one of
 * these entry addresses has to run the tail as well; that is what the ROM would
 * have executed next. The tail ends with jsl anim_update, a routine the port has
 * not converted, so it runs on the reference CPU: the hook builds the jsl's own
 * stack frame by hand so the instruction costs what it costs, then hands the CPU
 * the callee and picks up again when it returns.
 */
#include <stdint.h>
#include <stdbool.h>

#include "snes_state.h"
#include "dream_ram.h"
#include "dream_alu.h"
#include "dream_time.h"

/* anim_rate_store — $C0:99D4, the shared tail. */
static void anim_rate_store(SnesState* ss, uint16_t a) {
  const uint8_t pb = ss_pb(ss);
  uint16_t x = ss_x(ss), y = ss_y(ss);

  S(0x99D4, 3); t_index(ss);                /* C099D4 sta entity_anim_rate,X */
  t_write16(ss, ss_abs(ss, (uint16_t) (entity_anim_rate + x)), a);

  S(0x99D7, 1);                             /* C099D7 plb */
  ss_idle(ss);
  ss_idle(ss);
  ss_check_int(ss);
  uint8_t b = ss_pull8(ss);
  ss_set_db(ss, b);
  ss_set_nz8(ss, b);

  ss_set_a(ss, a);

  /* C099D8 jsl anim_update — opcode and target word, push the program bank, an
   * internal cycle, the bank byte of the operand, then the return address. */
  const uint16_t sp0 = ss_sp(ss);
  S(0x99D8, 3);
  ss_push8(ss, pb);
  ss_idle(ss);
  ss_fetch(ss, 1);
  ss_push16(ss, (uint16_t) (ss_pc(ss) - 1));
  ss_set_pc(ss, 0x80, 0xAEC8);
  /* anim_update is not converted, so it runs on the reference CPU. It can reach
   * a block upload thousands of cycles long, so let it be interrupted the way it
   * would be: the frame that was pushed is the real one, and its rtl lands on
   * the rts below whether the hook is still here or not. */
  if(ss_run_callee(ss, sp0)) return;

  S(0x99DC, 1);                             /* C099DC rts */
  ss_rts(ss);
}

/* The three-eighths shape: A/4 kept aside, plus A/8. */
static void anim_rate_3_8_body(SnesState* ss, uint16_t a) {
  const uint8_t pb = ss_pb(ss);
  uint16_t x = ss_x(ss), y = ss_y(ss);
  SI(0x99BA); a = alu_lsr16(ss, a);         /* C099BA lsr A */
  SI(0x99BB); a = alu_lsr16(ss, a);         /* C099BB lsr A */
  S(0x99BC, 2);                             /* C099BC sta $18 */
  t_write16(ss, ss_dp(ss) + scratch_18, a);
  SI(0x99BE); a = alu_lsr16(ss, a);         /* C099BE lsr A */
  SI(0x99BF); ss_set_c(ss, false);          /* C099BF clc */
  S(0x99C0, 2);                             /* C099C0 adc $18 */
  a = alu_adc16(ss, a, t_read16(ss, ss_dp(ss) + scratch_18));
  S(0x99C2, 1); t_branch(ss, true);         /* C099C2 bra anim_rate_store */
  anim_rate_store(ss, a);
}

void anim_rate_3_16(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);  /* C099B9 */
  SI(0x99B9);                               /* C099B9 lsr A, then falls through */
  anim_rate_3_8_body(ss, alu_lsr16(ss, ss_a(ss)));
}

void anim_rate_3_8(SnesState* ss) {         /* C099BA */
  anim_rate_3_8_body(ss, ss_a(ss));
}

/* The plain-shift shapes all fall through into each other and then into the
 * store: $99D1 does three shifts, $99D2 two, $99D3 one. */
void anim_rate_1_2(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);  /* C099D3 */
  SI(0x99D3);                               /* C099D3 lsr A */
  anim_rate_store(ss, alu_lsr16(ss, ss_a(ss)));
}

void anim_rate_1_4(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);  /* C099D2 */
  SI(0x99D2); a = alu_lsr16(ss, ss_a(ss));  /* C099D2 lsr A */
  SI(0x99D3); a = alu_lsr16(ss, a);         /* C099D3 lsr A */
  anim_rate_store(ss, a);
}

void anim_rate_1_8(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);  /* C099D1 */
  SI(0x99D1); a = alu_lsr16(ss, ss_a(ss));  /* C099D1 lsr A */
  SI(0x99D2); a = alu_lsr16(ss, a);         /* C099D2 lsr A */
  SI(0x99D3); a = alu_lsr16(ss, a);         /* C099D3 lsr A */
  anim_rate_store(ss, a);
}

static const RecompEntry kAnim[] = {
  { 0xc099b9, "anim_rate_3_16", anim_rate_3_16 },
  { 0xc099ba, "anim_rate_3_8", anim_rate_3_8 },
  { 0xc099d1, "anim_rate_1_8", anim_rate_1_8 },
  { 0xc099d2, "anim_rate_1_4", anim_rate_1_4 },
  { 0xc099d3, "anim_rate_1_2", anim_rate_1_2 },
};
RECOMP_REGISTER(kAnim)
