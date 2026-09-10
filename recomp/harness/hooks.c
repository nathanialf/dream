/* The demo hook table.
 *
 * The port itself lives under recomp/src: every routine there registers its own
 * entry address at load time (RECOMP_REGISTER), and --hook-table all installs the
 * whole registry. This file only keeps the two hand-written tables the harness
 * predates that mechanism with:
 *
 *   recomp_hooks[]        --hook-table demo: one routine, clear_sprite_table,
 *                         which now lives in recomp/src/oam.c like every other
 *                         converted routine. The wrapper below is what makes it
 *                         a *declining* hook: it checks the CPU mode the routine
 *                         assumes and hands the instruction back to the ROM if it
 *                         does not hold. Registry routines never decline.
 *   recomp_hooks_empty[]  --hook-table empty: the zero-entry table.
 *
 * See recomp/README.md.
 */
#include <stdint.h>
#include <stdbool.h>

#include "snes_state.h"

unsigned long recomp_hook_hits[RECOMP_HOOKS_MAX];

void clear_sprite_table(SnesState* ss);   /* recomp/src/oam.c */

static bool hook_clear_sprite_table(SnesState* ss) {
  if(ss_flag_e(ss) || ss_flag_m(ss)) return false;   /* needs native, 16-bit A */
  if(ss_dp(ss) & 0xff) return false;                 /* dp low byte 0: no extra cycle */
  clear_sprite_table(ss);
  return true;
}

const RecompHook recomp_hooks[] = {
  { 0xc0a500, hook_clear_sprite_table, "clear_sprite_table" },
  { 0, NULL, NULL }
};

const RecompHook recomp_hooks_empty[] = {
  { 0, NULL, NULL }
};
