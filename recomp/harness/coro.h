/* coro — the one stack-switching primitive the --no-cpu scheduler needs.
 *
 * A dispatched body chain runs on a stack of its own, and a yield *suspends*
 * that stack instead of unwinding it: the scheduler regains control at exactly
 * the instruction boundary the reference CPU stops on, and resuming continues
 * the body from inside ss_yield_wanted(), which then answers false. See
 * "Running without the CPUs" in recomp/README.md for why that is the whole of
 * the "resume at an interior address" problem.
 *
 * That needs three operations and nothing else -- create with an explicit stack
 * size, switch, destroy -- so it is one header with one backend per platform,
 * chosen by CMake (recomp/CMakeLists.txt):
 *
 *   coro_ucontext.c   POSIX: getcontext/makecontext/swapcontext
 *   coro_fibers.c     Windows: ConvertThreadToFiber/CreateFiber/SwitchToFiber
 *
 * Contract, identical on both:
 *
 *   - coro_new() allocates a coroutine that owns stackSize bytes of stack and
 *     is *done* (nothing running on it). It never returns NULL; it exits on
 *     allocation failure, like the rest of the harness.
 *   - coro_start() runs fn(arg) on that stack until the first coro_yield() or
 *     until fn returns, then comes back to the caller. A coro that has finished
 *     may be started again with another fn: the stack is reused, not reallocated.
 *   - coro_resume() continues a coro suspended in coro_yield() and comes back
 *     when it yields again or returns. Only the coro itself calls coro_yield(),
 *     and only from inside its own fn.
 *   - "the caller" is whoever called coro_start()/coro_resume(), which may
 *     itself be running on another coro: the SPC700's driver stack is resumed
 *     from inside a 65816 body's stack whenever an accessor catches the APU up.
 *     Both backends record the resumer per switch rather than once, so that
 *     nesting returns to the right stack.
 *   - coro_free() destroys a coro, running or not. A coro suspended halfway
 *     through a body is dropped, which is safe because a body owns nothing but
 *     its own stack frames (harness/ss_internal.h).
 *   - coro_done() is false between coro_start() and fn's return, true otherwise.
 *
 * Not thread-safe and not meant to be: one scheduler, one thread.
 */
#ifndef DREAM_CORO_H
#define DREAM_CORO_H

#include <stdbool.h>
#include <stddef.h>

typedef struct Coro Coro;

Coro* coro_new(size_t stackSize);
void  coro_free(Coro* co);
void  coro_start(Coro* co, void (*fn)(void*), void* arg);
void  coro_resume(Coro* co);
void  coro_yield(Coro* co);
bool  coro_done(const Coro* co);

/* "ucontext" or "fibers", for --test-coro and the run report. */
const char* coro_backend(void);

#endif
