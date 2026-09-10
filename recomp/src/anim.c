/* Velocity-to-animation-rate handlers, bank $C0.
 *
 * These are the four live entries (plus the three unreferenced siblings that
 * share the $99B9-$99EE run, converted here because two of them are steps of a
 * fall-through chain) of anim_rate_fn_table at $C0:B66A. entity_update_tick
 * reaches them with
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
 * have executed next. The tail ends with jsl anim_update (recomp/src/
 * anim_scripts.c): the hook builds the jsl's own stack frame by hand so the
 * instruction costs what it costs, then hands the CPU the callee -- whose own
 * hook fires from there -- and picks up again when it returns.
 */
#include <stdint.h>
#include <stdbool.h>

#include "snes_state.h"
#include "dream_ram.h"
#include "dream_alu.h"
#include "dream_time.h"

/* anim_rate_store — $C0:99D4, the shared tail. It is also an entry in its own
 * right: the beq at $99A9 in entity_update_tick reaches it directly when the
 * type has no handler for its state, so a hook installed here fires for that
 * path while the fall-through paths below arrive as a plain C call. */
static void anim_rate_store_body(SnesState* ss, uint16_t a) {
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
  /* anim_update runs from here, as a hook or as ROM code. It can reach
   * a block upload thousands of cycles long, so let it be interrupted the way it
   * would be: the frame that was pushed is the real one, and its rtl lands on
   * the rts below whether the hook is still here or not. */
  if(ss_run_callee(ss, sp0)) return;
  /* anim_update leaves its own A, X and Y behind; the step below publishes the
   * registers this body is holding in locals, so they have to be the callee's
   * and not the ones from before the jsl. Without this the rts put the
   * pre-call accumulator back -- invisible to the frame gate, because the
   * caller reloads before it reads A again, and caught by the unit gate, which
   * compares the registers the routine itself left. */
  a = ss_a(ss); x = ss_x(ss); y = ss_y(ss);

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
  anim_rate_store_body(ss, a);
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

/* The three-thirty-seconds shape at $99C4: four shifts kept aside, plus a
 * fifth. No table word in the ROM points at it (docs/handler_tables.md section
 * 1 lists it as unreferenced) and nothing falls into it either, so its entry
 * can only fire if a table is ever repointed; it is converted because it is one
 * of the two remaining steps of the $99B9-$99EE run. */
void anim_rate_3_32(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);  /* C099C4 */
  SI(0x99C4); a = alu_lsr16(ss, a);         /* C099C4 lsr A */
  SI(0x99C5); a = alu_lsr16(ss, a);         /* C099C5 lsr A */
  SI(0x99C6); a = alu_lsr16(ss, a);         /* C099C6 lsr A */
  SI(0x99C7); a = alu_lsr16(ss, a);         /* C099C7 lsr A */
  S(0x99C8, 2);                             /* C099C8 sta $18 */
  t_write16(ss, ss_dp(ss) + scratch_18, a);
  SI(0x99CA); a = alu_lsr16(ss, a);         /* C099CA lsr A */
  SI(0x99CB); ss_set_c(ss, false);          /* C099CB clc */
  S(0x99CC, 2);                             /* C099CC adc $18 */
  a = alu_adc16(ss, a, t_read16(ss, ss_dp(ss) + scratch_18));
  S(0x99CE, 1); t_branch(ss, true);         /* C099CE bra anim_rate_store */
  anim_rate_store_body(ss, a);
}

/* The plain-shift shapes all fall through into each other and then into the
 * store: $99D0 does four shifts, $99D1 three, $99D2 two, $99D3 one. */
void anim_rate_1_2(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);  /* C099D3 */
  SI(0x99D3);                               /* C099D3 lsr A */
  anim_rate_store_body(ss, alu_lsr16(ss, ss_a(ss)));
}

void anim_rate_1_4(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);  /* C099D2 */
  SI(0x99D2); a = alu_lsr16(ss, ss_a(ss));  /* C099D2 lsr A */
  SI(0x99D3); a = alu_lsr16(ss, a);         /* C099D3 lsr A */
  anim_rate_store_body(ss, a);
}

void anim_rate_1_16(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);  /* C099D0 */
  SI(0x99D0); a = alu_lsr16(ss, ss_a(ss));  /* C099D0 lsr A */
  SI(0x99D1); a = alu_lsr16(ss, a);         /* C099D1 lsr A */
  SI(0x99D2); a = alu_lsr16(ss, a);         /* C099D2 lsr A */
  SI(0x99D3); a = alu_lsr16(ss, a);         /* C099D3 lsr A */
  anim_rate_store_body(ss, a);
}

void anim_rate_1_8(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);  /* C099D1 */
  SI(0x99D1); a = alu_lsr16(ss, ss_a(ss));  /* C099D1 lsr A */
  SI(0x99D2); a = alu_lsr16(ss, a);         /* C099D2 lsr A */
  SI(0x99D3); a = alu_lsr16(ss, a);         /* C099D3 lsr A */
  anim_rate_store_body(ss, a);
}

void anim_rate_store(SnesState* ss) {
  anim_rate_store_body(ss, ss_a(ss));
}

static const RecompEntry kAnim[] = {
  { 0xc099b9, "anim_rate_3_16", anim_rate_3_16 },
  { 0xc099ba, "anim_rate_3_8", anim_rate_3_8 },
  { 0xc099c4, "anim_rate_3_32", anim_rate_3_32 },
  { 0xc099d0, "anim_rate_1_16", anim_rate_1_16 },
  { 0xc099d1, "anim_rate_1_8", anim_rate_1_8 },
  { 0xc099d2, "anim_rate_1_4", anim_rate_1_4 },
  { 0xc099d3, "anim_rate_1_2", anim_rate_1_2 },
  { 0xc099d4, "anim_rate_store", anim_rate_store },
};
RECOMP_REGISTER(kAnim)
