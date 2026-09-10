/* coro — POSIX backend: getcontext/makecontext/swapcontext (recomp/harness/coro.h).
 *
 * This is the code the --no-cpu scheduler has always run on, moved out of
 * snes_state.c unchanged: same stack allocation, same trampoline, same
 * swapcontext pair. The Linux gate's numbers are the numbers it produced.
 */
#define _XOPEN_SOURCE 700   /* makecontext/swapcontext */

#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>

#include "coro.h"

struct Coro {
  ucontext_t ctx;       /* the body chain's own stack */
  ucontext_t back;      /* whoever resumed it */
  char* stack;
  size_t stackSize;
  void (*fn)(void*);
  void* arg;
  bool done;
};

static Coro* gCoroStarting;   /* makecontext takes ints, not pointers */

static void coro_trampoline(void) {
  Coro* co = gCoroStarting;
  co->fn(co->arg);
  co->done = true;              /* uc_link swaps back to co->back */
}

Coro* coro_new(size_t stackSize) {
  Coro* co = calloc(1, sizeof(*co));
  if(co == NULL) { fprintf(stderr, "coro: out of memory\n"); exit(2); }
  co->stack = malloc(stackSize);
  if(co->stack == NULL) { fprintf(stderr, "coro: out of memory\n"); exit(2); }
  co->stackSize = stackSize;
  co->done = true;
  return co;
}

void coro_free(Coro* co) {
  if(co == NULL) return;
  free(co->stack);
  free(co);
}

void coro_start(Coro* co, void (*fn)(void*), void* arg) {
  co->fn = fn;
  co->arg = arg;
  co->done = false;
  if(getcontext(&co->ctx) != 0) { fprintf(stderr, "coro: getcontext failed\n"); exit(2); }
  co->ctx.uc_stack.ss_sp = co->stack;
  co->ctx.uc_stack.ss_size = co->stackSize;
  co->ctx.uc_link = &co->back;
  makecontext(&co->ctx, coro_trampoline, 0);
  gCoroStarting = co;
  swapcontext(&co->back, &co->ctx);
}

void coro_resume(Coro* co) {
  swapcontext(&co->back, &co->ctx);
}

void coro_yield(Coro* co) {
  swapcontext(&co->ctx, &co->back);
}

bool coro_done(const Coro* co) { return co->done; }

const char* coro_backend(void) { return "ucontext"; }
