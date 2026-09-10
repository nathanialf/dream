/* coro — Windows backend: fibers (recomp/harness/coro.h).
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
 * 3. A fiber proc must never return -- returning from one calls ExitThread and
 *    takes the process with it. So the proc is an endless loop that runs fn,
 *    marks the coro done and switches back; a later coro_start reuses the same
 *    fiber and stack for the next body, which is what the POSIX backend's
 *    getcontext/makecontext pair does too.
 *
 * DeleteFiber on the *running* fiber would likewise call ExitThread; the
 * scheduler always frees from outside (harness/snes_state.c: ss_nocpu_free),
 * which is the teardown --test-coro exercises.
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

void coro_free(Coro* co) {
  if(co == NULL) return;
  if(co->fiber != NULL) DeleteFiber(co->fiber);
  free(co);
}

void coro_start(Coro* co, void (*fn)(void*), void* arg) {
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
  co->back = coro_self();
  SwitchToFiber(co->fiber);
}

void coro_yield(Coro* co) {
  SwitchToFiber(co->back);
}

bool coro_done(const Coro* co) { return co->done; }

const char* coro_backend(void) { return "fibers"; }
