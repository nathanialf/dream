/* Private glue between the public hook API (recomp/include/snes_state.h) and the
 * vendored LakeSnes core. Harness sources only. */
#ifndef DREAM_SS_INTERNAL_H
#define DREAM_SS_INTERNAL_H

#include "coro.h"
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
  uint32_t pc24;        /* the hook's own entry address, for --no-cpu diagnostics */
} SsHookEntry;

/* ---- --no-cpu: the C bodies as the program ----------------------------- *
 *
 * In --no-cpu mode nothing fetches an instruction. ss_nocpu_run_frame() stands
 * in for snes_runFrame(): it runs the machine's non-instruction steps out of
 * the core (cpu_runNonInstruction: reset, the wai park, interrupt entry) and
 * resolves every other pc through the registry, running the C body that owns
 * it.
 *
 * A body is straight-line C that `return`s when ss_yield_wanted() says the
 * machine has moved on underneath it, leaving the ROM to finish the routine.
 * With no ROM there is nothing to finish it, and the address it stopped at is
 * in the middle of a routine, which the registry does not name. So a dispatched
 * body chain runs on a stack of its own (Coro), and a yield suspends that stack
 * instead of unwinding it (harness/coro.h, whose backend is ucontext on POSIX
 * and Win32 fibers on Windows): the scheduler regains control at exactly the
 * instruction boundary the reference CPU stops on, and resuming continues the
 * body from inside ss_yield_wanted(), which then answers false. One suspended
 * context is the whole of the "resume at an interior address" problem, and it
 * needs no change to any body.
 *
 * A suspension that an interrupt displaces is kept until the machine can no
 * longer return to it (an rti would land on its pc with its stack pointer);
 * this game's NMI handler resets S and parks instead, so the context is
 * reclaimed two instructions into the handler. */

#define SS_NOCPU_CTX_MAX 8      /* body chains alive at once: running + displaced */

typedef struct SsNoCpuCtx {
  Coro* co;
  struct SnesState* ss;
  uint32_t startPc;     /* the body this context was started to run */
  uint32_t pc24;        /* where it suspended */
  uint16_t sp;          /* the 65816 stack pointer there */
  int depth;            /* ss->depth when it started, to unwind an abandoned one */
  bool suspended;
  bool displaced;       /* an interrupt was taken while it was suspended */
} SsNoCpuCtx;

struct SnesState {
  Snes* snes;
  SsHookEntry entry[SS_HOOK_DEPTH_MAX];
  int depth;            /* hooks in flight; entry[depth-1] is the innermost */
  int maxDepth;         /* high-water mark, for the run report */
  /* --no-cpu */
  bool nocpu;
  SsNoCpuCtx ctx[SS_NOCPU_CTX_MAX];
  int nctx;                  /* ctx[nctx-1] is the innermost */
  SsNoCpuCtx* running;       /* the context whose body chain is executing */
  int flPhase;               /* 0 = snes_runFrame's first loop, 1 = its second */
  uint32_t flFrameMark;      /* snes->frames when the second loop began */
  uint32_t lastDispatch;     /* the last body the scheduler entered, for errors */
  uint64_t dispatches;       /* bodies entered by the scheduler */
  uint64_t suspensions;      /* yields that suspended a body chain */
  uint64_t abandoned;        /* suspensions an interrupt threw away */
  int maxCtx;                /* high-water mark of nctx */
  /* --unit: hold the routine, see ss_unit_hold() below */
  bool unitHold;
};

/* Called by the dispatcher around a hook body: push a snapshot of the machine on
 * entry, pop it on return. Every path out of the dispatcher must pop, including
 * the one where a declining hook (harness/hooks.c) returns false. */
void ss_enter_hook(SnesState* ss);
void ss_leave_hook(SnesState* ss);
int  ss_hook_depth(const SnesState* ss);
int  ss_hook_max_depth(const SnesState* ss);

void ss_nocpu_free(SnesState* ss);

/* --unit: run one routine to its end rather than offering it back.
 *
 * The frame-level yield exists so that a hook is not atomic across a boundary
 * the reference CPU stops at. A --unit run has no such boundary: nothing
 * samples the machine until the routine is over, NMI and both timer IRQs are
 * off for the duration, and the reference runs the ROM's own code straight
 * through. With the hold on, ss_yield_wanted() answers false, so a body long
 * enough to outlast a frame (unused_wram_clear_full is about five) is compared
 * against the ROM over the whole routine instead of only its first frame. It is
 * the 65816's equivalent of the far-away apu->sliceEnd the SPC700 side of the
 * same gate sets, and it is set on the candidate machine only. */
void ss_unit_hold(SnesState* ss, bool on);

/* --no-cpu run report */
uint64_t ss_nocpu_dispatches(const SnesState* ss);
uint64_t ss_nocpu_suspensions(const SnesState* ss);
uint64_t ss_nocpu_abandoned(const SnesState* ss);
int      ss_nocpu_max_contexts(const SnesState* ss);

#endif
