/* Private glue between the public hook API (recomp/include/snes_state.h) and the
 * vendored LakeSnes core. Harness sources only. */
#ifndef DREAM_SS_INTERNAL_H
#define DREAM_SS_INTERNAL_H

#include "snes_state.h"
#include "snes.h"
#include "cpu.h"

/* How deep hooks may nest.
 *
 * A converted routine reaches a converted callee through ss_run_callee, which
 * runs the reference CPU over it: the callee's own entry address is dispatched
 * again and its hook fires *inside* the caller's. The chain the ROM builds every
 * frame is nmi_handler -> entity_update_tick -> an animation-rate handler ->
 * anim_update -> an animation callback; the gate's own input scripts reach six
 * hooks in flight at once, which the run report prints. The cap is far above
 * that, and overflowing it is a fatal error rather than a silently wrong yield
 * decision. */
#define SS_HOOK_DEPTH_MAX 64

/* What the machine looked like when one hook was entered.
 *
 * It is per invocation, not per machine. A hook entered from inside another
 * hook's callee must answer ss_yield_wanted() about *its* own entry, and must
 * not disturb the answer the outer hook gets when it resumes: the outer hook is
 * still responsible for handing its routine back at the frame boundary it
 * crossed. Keeping one slot per machine made the inner hook's snapshot the outer
 * hook's too, and the outer hook then never yielded for a boundary it had
 * crossed before the nested call. */
typedef struct {
  bool vblank;          /* snes->inVblank at the hook's first instruction */
  uint32_t frames;      /* snes->frames at the same instant */
} SsHookEntry;

struct SnesState {
  Snes* snes;
  SsHookEntry entry[SS_HOOK_DEPTH_MAX];
  int depth;            /* hooks in flight; entry[depth-1] is the innermost */
  int maxDepth;         /* high-water mark, for the run report */
};

/* Called by the dispatcher around a hook body: push a snapshot of the machine on
 * entry, pop it on return. Every path out of the dispatcher must pop, including
 * the one where a declining hook (harness/hooks.c) returns false. */
void ss_enter_hook(SnesState* ss);
void ss_leave_hook(SnesState* ss);
int  ss_hook_depth(const SnesState* ss);
int  ss_hook_max_depth(const SnesState* ss);

#endif
