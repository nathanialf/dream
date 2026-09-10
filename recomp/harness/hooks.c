/* recomp_hooks[] — the recomp's routine table.
 *
 * Each entry claims one 65816 entry address. When --hooks on is given and the CPU
 * is about to fetch the first opcode of that routine, the C function runs instead
 * and leaves the CPU at the routine's return address. --lockstep then runs the
 * same ROM twice, once with this table installed and once without, and compares
 * WRAM/VRAM/CGRAM/OAM after every frame.
 *
 * recomp_hooks_empty[] is the zero-entry table (--hook-table empty): the shape a
 * new port starts from.
 */
#include <stdint.h>
#include <stdbool.h>

#include "snes_state.h"

#define RECOMP_HOOKS_MAX 64
unsigned long recomp_hook_hits[RECOMP_HOOKS_MAX];

/* ---------------------------------------------------------------------------
 * clear_sprite_table — $C0:A500, called once per main-loop iteration from
 * $C08235 and once from $C09324 (see out/dream.asm):
 *
 *     C0A500  stz $0400      ; x16, $0400..$041E
 *     ...
 *     C0A530  lda #$0200
 *     C0A533  sta $94        ; oam_write_ptr
 *     C0A535  stz $96
 *     C0A537  rts
 *
 * Everything is 16-bit (m=0) and the absolute stores go through the data bank,
 * which the main loop leaves at $80 (pea $8080 / plb / plb at $C0A54D), i.e. the
 * WRAM mirror. The hook reproduces the effect in C and the cycle pattern through
 * the timed accessors: per stz abs, three fetches then two writes with the
 * mid-store interrupt latch; the immediate load is three fetches; the two direct
 * page stores are two fetches and two writes each (dp low byte is 0, so no extra
 * idle cycle); the rts is the stock 6-cycle sequence.
 *
 * It declines (returns false, letting the ROM run) if the CPU is not in the mode
 * the routine assumes, so the table stays safe if it is ever reached another way.
 * ------------------------------------------------------------------------- */
static bool hook_clear_sprite_table(SnesState* ss) {
  if(ss_flag_e(ss) || ss_flag_m(ss)) return false;   /* needs native, 16-bit A */
  if(ss_dp(ss) & 0xff) return false;                 /* dp low byte 0: no extra cycle */

  const uint32_t db = (uint32_t) ss_db(ss) << 16;
  const uint16_t dp = ss_dp(ss);

  for(int i = 0; i < 16; i++) {
    const uint32_t adr = (db + 0x0400 + (uint32_t) (i * 2)) & 0xffffff;
    ss_fetch(ss, 3);                                  /* stz abs */
    ss_bus_w8(ss, adr, 0x00);
    ss_check_int(ss);
    ss_bus_w8(ss, (adr + 1) & 0xffffff, 0x00);
  }

  ss_fetch(ss, 1);                                    /* lda # */
  ss_fetch(ss, 1);                                    /* imm low */
  ss_check_int(ss);
  ss_fetch(ss, 1);                                    /* imm high */
  ss_set_a(ss, 0x0200);
  ss_set_nz16(ss, 0x0200);

  ss_fetch(ss, 2);                                    /* sta dp */
  ss_bus_w8(ss, (uint16_t) (dp + 0x94), 0x00);
  ss_check_int(ss);
  ss_bus_w8(ss, (uint16_t) (dp + 0x95), 0x02);

  ss_fetch(ss, 2);                                    /* stz dp */
  ss_bus_w8(ss, (uint16_t) (dp + 0x96), 0x00);
  ss_check_int(ss);
  ss_bus_w8(ss, (uint16_t) (dp + 0x97), 0x00);

  ss_rts(ss);                                         /* rts */
  return true;
}

const RecompHook recomp_hooks[] = {
  { 0xc0a500, hook_clear_sprite_table, "clear_sprite_table" },
  { 0, NULL, NULL }
};

const RecompHook recomp_hooks_empty[] = {
  { 0, NULL, NULL }
};
