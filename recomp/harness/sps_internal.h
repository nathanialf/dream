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

/* --no-cpu: the driver's C bodies as the program.
 *
 * The shape is the 65816 side's (recomp/harness/ss_internal.h) with the two
 * complications taken out. The catch-up slice can end between any two
 * instructions of a routine, so a body still has to stop there, and a body is
 * still straight-line C that cannot be re-entered in its middle -- so it runs
 * on a stack of its own and a yield suspends that stack. But the SPC700 takes
 * no interrupts here and nothing else moves its pc, so a suspension is always
 * resumed at the instruction it stopped on: one context, never displaced,
 * never abandoned.
 *
 * The one pc with no body is the SPC700's own IPL boot ROM at $FFC0, the
 * console's firmware rather than the ROM's program: it receives the loader
 * block at power-on and is never entered again. --no-cpu lets those
 * instructions execute and counts them separately. */
struct SsCoro;

struct SpcState {
  Apu* apu;
  Spc* spc;
  int depth;            /* SPC hooks in flight */
  int maxDepth;         /* high-water mark, for the run report */
  /* --no-cpu */
  bool nocpu;
  struct SsCoro* co;         /* the driver's stack */
  bool running;              /* a body chain is executing on it */
  bool suspended;            /* it stopped at a slice boundary */
  uint16_t suspendPc;        /* where */
  SpcHookHandler inner;      /* the harness's own body dispatcher */
  void* innerCtx;
  uint64_t dispatches;
  uint64_t suspensions;
  uint64_t iplInstructions;  /* IPL boot ROM opcodes the core still executes */
};

void sps_enter_hook(SpcState* sp);
void sps_nocpu_free(SpcState* sp);
uint64_t sps_nocpu_dispatches(const SpcState* sp);
uint64_t sps_nocpu_suspensions(const SpcState* sp);
uint64_t sps_nocpu_ipl_instructions(const SpcState* sp);
void sps_leave_hook(SpcState* sp);
int  sps_hook_depth(const SpcState* sp);
int  sps_hook_max_depth(const SpcState* sp);

#endif
