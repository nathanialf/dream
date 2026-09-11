/* coro POSIX backend: getcontext/makecontext/swapcontext (recomp/harness/coro.h).
 *
 * This is the code the --no-cpu scheduler has always run on, moved out of
 * snes_state.c unchanged: same trampoline, same swapcontext pair. The Linux
 * gate's numbers are the numbers it produced.
 *
 * Two things here are not the original. The stack is an mmap with a PROT_NONE
 * guard page below it rather than a malloc: CreateFiberEx gives the Windows
 * fibers a guard page, malloc does not, so a body-chain stack overflow was a
 * trapping fault on Windows and a silent write into the heap on POSIX. And the
 * two misuses the fiber backend refuses are refused here as well, because
 * coro.h says the contract is the same on both: resuming a coroutine that was
 * never started (which used to swap into the all-zero ucontext_t left by
 * calloc), and freeing the coroutine that is currently running (which used to
 * free the stack it was executing on and carry on running).
 */
#define _XOPEN_SOURCE 700
#define _DARWIN_C_SOURCE 1   /* macOS hides MAP_ANON under strict _XOPEN_SOURCE without this */   /* makecontext/swapcontext */

#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>
#include <sys/mman.h>
#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON   /* macOS and the BSDs spell it this way */
#endif
#include <unistd.h>

#include "coro.h"

struct Coro {
  ucontext_t ctx;       /* the body chain's own stack */
  ucontext_t back;      /* whoever resumed it */
  char* stack;          /* the usable stack, one page above base */
  size_t stackSize;
  void* base;           /* the mapping, guard page included */
  size_t mapSize;
  void (*fn)(void*);
  void* arg;
  bool done;
  bool started;         /* coro_start has run at least once */
  bool running;         /* this coro's stack is the one executing */
};

static Coro* gCoroStarting;   /* makecontext takes ints, not pointers */

static void coro_trampoline(void) {
  Coro* co = gCoroStarting;
  co->fn(co->arg);
  co->done = true;              /* uc_link swaps back to co->back */
}

static size_t coro_page(void) {
  const long n = sysconf(_SC_PAGESIZE);
  return n > 0 ? (size_t) n : 4096u;
}

Coro* coro_new(size_t stackSize) {
  Coro* co = calloc(1, sizeof(*co));
  if(co == NULL) { fprintf(stderr, "coro: out of memory\n"); exit(2); }
  const size_t pg = coro_page();
  const size_t usable = (stackSize + pg - 1) & ~(pg - 1);
  const size_t total = usable + pg;
  void* base = mmap(NULL, total, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if(base == MAP_FAILED) { fprintf(stderr, "coro: out of memory\n"); exit(2); }
  /* The stack grows down, so the guard page goes at the low end: running off
   * the bottom of it faults instead of writing into whatever is below. */
  if(mprotect(base, pg, PROT_NONE) != 0) {
    fprintf(stderr, "coro: cannot protect the stack guard page\n");
    exit(2);
  }
  co->base = base;
  co->mapSize = total;
  co->stack = (char*) base + pg;
  co->stackSize = usable;
  co->done = true;
  return co;
}

void coro_free(Coro* co) {
  if(co == NULL) return;
  if(co->running) {
    /* The fiber backend refuses the same call (coro_drop_fiber). Freeing the
     * stack underneath the code that is running on it is not a thing to do
     * quietly. */
    fprintf(stderr, "coro: coro_free on the running coroutine; refusing\n");
    return;
  }
  if(co->base != NULL) munmap(co->base, co->mapSize);
  free(co);
}

void coro_start(Coro* co, void (*fn)(void*), void* arg) {
  co->fn = fn;
  co->arg = arg;
  co->done = false;
  co->started = true;
  if(getcontext(&co->ctx) != 0) { fprintf(stderr, "coro: getcontext failed\n"); exit(2); }
  co->ctx.uc_stack.ss_sp = co->stack;
  co->ctx.uc_stack.ss_size = co->stackSize;
  co->ctx.uc_link = &co->back;
  makecontext(&co->ctx, coro_trampoline, 0);
  gCoroStarting = co;
  co->running = true;
  swapcontext(&co->back, &co->ctx);
  co->running = false;
}

void coro_resume(Coro* co) {
  if(!co->started) {
    /* Only a coro that has been started can be resumed (coro.h). Without this
     * the swapcontext below jumps into the all-zero ucontext_t calloc left. */
    fprintf(stderr, "coro: coro_resume on a coroutine that was never started\n");
    exit(2);
  }
  co->running = true;
  swapcontext(&co->back, &co->ctx);
  co->running = false;
}

void coro_yield(Coro* co) {
  swapcontext(&co->ctx, &co->back);
}

bool coro_done(const Coro* co) { return co->done; }

const char* coro_backend(void) { return "ucontext"; }
