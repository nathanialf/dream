/* Private glue between the public SPC hook API (recomp/include/spc_state.h) and
 * the vendored LakeSnes APU. Harness sources only.
 *
 * Unlike the 65816 side there is no per-invocation entry snapshot to keep. The
 * 65816's yield condition is a *level* -- "vblank has started" is true for the
 * rest of the frame -- so a hook has to remember what it looked like when it
 * began, and hooks nest. The SPC's condition is a threshold on a monotonically
 * increasing cycle count (apu->sliceEnd, set by apu_runCycles), and every hook
 * dispatch happens strictly below it, so the same question has the same right
 * answer for every hook in flight. The depth is tracked only for the run report.
 */
#ifndef DREAM_SPS_INTERNAL_H
#define DREAM_SPS_INTERNAL_H

#include "spc_state.h"
#include "apu.h"
#include "spc.h"
#include "dsp.h"

struct SpcState {
  Apu* apu;
  Spc* spc;
  int depth;            /* SPC hooks in flight */
  int maxDepth;         /* high-water mark, for the run report */
};

void sps_enter_hook(SpcState* sp);
void sps_leave_hook(SpcState* sp);
int  sps_hook_depth(const SpcState* sp);
int  sps_hook_max_depth(const SpcState* sp);

#endif
