/* coro Windows backend: fibers (recomp/harness/coro.h).
 *
 * A Win32 fiber is the same object ucontext gives on POSIX: a stack plus the
 * register state to enter it, switched cooperatively and never scheduled by the
 * OS. The mapping is one to one.
 *
 *   coro_new     CreateFiberEx with the stack size the caller asked for
 *   coro_start   SwitchToFiber into a fiber whose proc has not begun fn yet
 *   coro_resume  SwitchToFiber into a fiber parked in coro_yield
 *   coro_yield   SwitchToFiber back to the fiber that switched in
 *   coro_free    DeleteFiber, which frees the stack of a suspended fiber
 *
 * Three details are worth stating, because they are where a fiber port usually
 * goes wrong:
 *
 * 1. Only a fiber can switch to a fiber, so the thread that runs the scheduler
 *    has to become one first: ConvertThreadToFiber, done lazily on the first
 *    switch. The thread is left converted for the life of the process, which
 *    costs one fiber object and keeps every later switch valid.
 * 2. The fiber to come back to is recorded per switch (GetCurrentFiber at
 *    coro_start/coro_resume), not once at creation. It has to be: the SPC700's
 *    driver stack is resumed from inside a 65816 body's stack every time an
 *    accessor catches the APU up, so "back" is sometimes the scheduler's thread
 *    fiber and sometimes another coro's.
 * 3. A fiber proc must never return: returning from one calls ExitThread and
 *    takes the process with it. So the proc is an endless loop that runs fn,
 *    marks the coro done and switches back; a later coro_start reuses the same
 *    fiber and stack for the next body, and the POSIX getcontext/makecontext
 *    pair does the same.
 *
 * DeleteFiber on the *running* fiber would likewise call ExitThread; the
 * scheduler always frees from outside (harness/snes_state.c: ss_nocpu_free),
 * which is the teardown --test-coro exercises. coro_free below refuses that
 * case rather than taking the process down with it.
 *
 * 4. coro_start has to give fn a *fresh* stack even when the coro it is handed
 *    is parked halfway through an earlier body. The POSIX backend cannot get
 *    this wrong (coro_start makecontext()s the stack again every time, which
 *    throws the suspended frames away), but SwitchToFiber has no such step: it
 *    resumes wherever the fiber stopped. The scheduler does hand back parked
 *    coros: ss_nocpu_reap() drops a body chain the NMI handler displaced (this
 *    ROM's does, two instructions in, with `ldx #$01FF ; txs`) and the slot,
 *    with its Coro, is reused for the next dispatch. So a suspended fiber is
 *    deleted and remade at coro_start; only a *finished* one is reused.
 */
#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600   /* IsThreadAFiber */
#endif

#include <windows.h>

#include <stdio.h>
#include <stdlib.h>

#include "coro.h"

struct Coro {
  LPVOID fiber;         /* the body chain's own stack */
  LPVOID back;          /* whoever switched into it, for this switch */
  size_t stackSize;
  void (*fn)(void*);
  void* arg;
  bool done;
};

static void CALLBACK coro_fiber_proc(LPVOID param) {
  Coro* co = (Coro*) param;
  for(;;) {
    co->fn(co->arg);
    co->done = true;
    SwitchToFiber(co->back);   /* a later coro_start comes back here */
  }
}

/* GetCurrentFiber() is a macro that reads the fiber pointer out of the TEB
 * (gs:[0x20] on x86-64). GCC's -Warray-bounds cannot see that mingw's
 * __readgsqword is a segment read rather than a dereference of address zero and
 * warns about every use, so the one use is wrapped here and the warning turned
 * off around it rather than left in the build's output. */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
#endif
static LPVOID coro_current_fiber(void) {
  return GetCurrentFiber();
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

/* The scheduler's own thread as a fiber, so it can switch to one. */
static LPVOID coro_self(void) {
  if(!IsThreadAFiber()) {
    LPVOID self = ConvertThreadToFiber(NULL);
    if(self == NULL) {
      fprintf(stderr, "coro: ConvertThreadToFiber failed (%lu)\n",
              (unsigned long) GetLastError());
      exit(2);
    }
    return self;
  }
  return coro_current_fiber();
}

Coro* coro_new(size_t stackSize) {
  Coro* co = calloc(1, sizeof(*co));
  if(co == NULL) { fprintf(stderr, "coro: out of memory\n"); exit(2); }
  co->stackSize = stackSize;
  co->done = true;
  return co;
}

/* Throw a fiber away. DeleteFiber frees the stack of a suspended fiber, which is
 * what dropping a coro means; called on the fiber that is running it calls
 * ExitThread instead and the process is gone, so that case is refused. */
static void coro_drop_fiber(Coro* co, const char* what) {
  if(co->fiber == NULL) return;
  if(IsThreadAFiber() && co->fiber == coro_current_fiber()) {
    fprintf(stderr, "coro: %s on the running fiber; refusing (it would ExitThread)\n",
            what);
    return;
  }
  DeleteFiber(co->fiber);
  co->fiber = NULL;
}

void coro_free(Coro* co) {
  if(co == NULL) return;
  coro_drop_fiber(co, "coro_free");
  free(co);
}

void coro_start(Coro* co, void (*fn)(void*), void* arg) {
  /* A coro that is not done still has a fiber parked inside coro_yield, halfway
   * through the body it was running when the scheduler abandoned it. Switching
   * into that fiber would resume the old body and never call fn: the stack has
   * to go. A finished coro is parked at the top of coro_fiber_proc's loop
   * instead, and that loop exists so it can be reused. (See note 4 at the
   * top of this file; the POSIX backend gets this for free.) */
  if(co->fiber != NULL && !co->done) coro_drop_fiber(co, "coro_start (restart)");
  co->fn = fn;
  co->arg = arg;
  co->done = false;
  co->back = coro_self();
  if(co->fiber == NULL) {
    /* Reserve and commit exactly what was asked for: the POSIX backend's stack
     * is a malloc of that size, and the two have to be the same machine.
     * FIBER_FLAG_FLOAT_SWITCH saves the x87/MMX state across the switch. */
    co->fiber = CreateFiberEx((SIZE_T) co->stackSize, (SIZE_T) co->stackSize,
                              FIBER_FLAG_FLOAT_SWITCH, coro_fiber_proc, co);
    if(co->fiber == NULL) {
      fprintf(stderr, "coro: CreateFiberEx(%zu) failed (%lu)\n",
              co->stackSize, (unsigned long) GetLastError());
      exit(2);
    }
  }
  SwitchToFiber(co->fiber);
}

void coro_resume(Coro* co) {
  if(co->fiber == NULL) {
    /* Only a coro that has been started can be resumed (coro.h). Say so here
     * rather than handing SwitchToFiber a null pointer, which faults inside
     * ntdll with nothing to read off the address. */
    fprintf(stderr, "coro: coro_resume on a coroutine that was never started\n");
    exit(2);
  }
  co->back = coro_self();
  SwitchToFiber(co->fiber);
}

void coro_yield(Coro* co) {
  SwitchToFiber(co->back);
}

bool coro_done(const Coro* co) { return co->done; }

const char* coro_backend(void) { return "fibers"; }
