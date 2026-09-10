/* dlog — the app's own crash log (recomp/app/README.md, "When it crashes").
 *
 * The Windows build is a console-subsystem program launched by double-click as
 * often as from a terminal, and a console that closes with the process carries
 * nothing away. So `dream` keeps a log of its own next to the executable —
 * dream.log, one timestamped line a stage — and installs a handler that writes
 * what it can about a fault into the same file before the process goes.
 *
 * Nothing here touches stdout: the frame line `--frames` prints has to stay
 * byte-identical to dream_harness's (recomp/app/README.md), so the log is a
 * second, separate stream and never a copy of the first.
 *
 * Every write is flushed (and on Windows FlushFileBuffers'd) before it returns,
 * because the interesting line is always the last one before the fault.
 */
#ifndef DREAM_DLOG_H
#define DREAM_DLOG_H

#include <stdbool.h>

/* mingw's printf is the C99 one (__USE_MINGW_ANSI_STDIO, recomp/CMakeLists.txt),
 * so the format check has to be gnu_printf there or every %zu is a warning. */
#if defined(__MINGW32__)
#define DLOG_PRINTF(a, b) __attribute__((format(gnu_printf, a, b)))
#elif defined(__GNUC__)
#define DLOG_PRINTF(a, b) __attribute__((format(printf, a, b)))
#else
#define DLOG_PRINTF(a, b)
#endif

/* Exit codes the crash paths use, distinct from anything main() returns
 * (0, 2, 3) so a script can tell a crash from a refusal. */
#define DLOG_EXIT_CRASH  86   /* an unhandled Windows exception */
#define DLOG_EXIT_SIGNAL 87   /* SIGSEGV/SIGABRT/SIGILL/SIGFPE */

/* Open <dir of the executable>/dream.log on Windows, ./dream.log elsewhere.
 * Truncates: one run, one log. Never fails loudly — if the file cannot be
 * opened every call below becomes a no-op and the game still runs. */
void dlog_open(void);

/* One timestamped line. Not a stage: use for detail under a stage. */
void dlog(const char* fmt, ...) DLOG_PRINTF(1, 2);

/* One timestamped line that also becomes "the last stage" the crash handler
 * reports. Every step of startup and the frame loop goes through this. */
void dlog_stage(const char* fmt, ...) DLOG_PRINTF(1, 2);

/* The last dlog_stage() text, for the crash handler. Never NULL. */
const char* dlog_last_stage(void);

/* SetErrorMode + SetUnhandledExceptionFilter on Windows, signal() everywhere.
 * Call it right after dlog_open(). */
void dlog_install_crash_handlers(void);

void dlog_close(void);

#endif
