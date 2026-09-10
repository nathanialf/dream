/* dlog — the app's own crash log. See dlog.h for what it is for.
 *
 * The whole file is about one situation: dream.exe stops on someone else's
 * Windows machine, there is no debugger, no console left open and no core dump,
 * and the only thing that can explain it is what the program wrote down before
 * it went. So every line is flushed to disk as it is written, the last stage is
 * kept in a static the fault handler can read without allocating, and the fault
 * handler reports the fault address as an offset into the image so it can be
 * looked up in build/win/dream.map.
 */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#ifdef _WIN32
/* IsThreadAFiber (Vista); mingw defaults higher than this, so only raise. */
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0600
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "dlog.h"

/* Leave now, with a code of our own, having already flushed. TerminateProcess
 * rather than exit(): no atexit handler, no DLL detach, nothing that could sit
 * down and think about it while the log is on disk and the user is waiting. */
#ifdef _WIN32
#define DLOG_DIE(code) do { TerminateProcess(GetCurrentProcess(), (UINT) (code)); \
                            ExitProcess((UINT) (code)); } while(0)
#else
#define DLOG_DIE(code) _exit(code)
#endif

static FILE* gLog;
static char  gStage[256] = "(nothing logged yet)";
static int   gInCrash;

#ifdef _WIN32
static ULONGLONG gStartTick;
#else
static struct timespec gStart;
#endif

/* ---- the file ---------------------------------------------------------- */

/* Flushed through the C library *and* through the OS: a line that only reached
 * the CRT buffer is a line the fault took with it. */
static void dlog_flush(void) {
  if(gLog == NULL) return;
  fflush(gLog);
#ifdef _WIN32
  {
    int fd = _fileno(gLog);
    if(fd >= 0) {
      HANDLE h = (HANDLE) _get_osfhandle(fd);
      if(h != INVALID_HANDLE_VALUE) FlushFileBuffers(h);
    }
  }
#endif
}

/* "2026-09-10 19:35:02.123 +12.345" — wall clock for correlating with anything
 * else on the machine, elapsed seconds for reading the run on its own. */
static void dlog_stamp(char* out, size_t n) {
  double elapsed = 0.0;
#ifdef _WIN32
  SYSTEMTIME st;
  GetLocalTime(&st);
  elapsed = (double) (GetTickCount64() - gStartTick) / 1000.0;
  snprintf(out, n, "%04u-%02u-%02u %02u:%02u:%02u.%03u %+9.3f",
           st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
           st.wMilliseconds, elapsed);
#else
  struct timespec now;
  struct tm tmv;
  clock_gettime(CLOCK_REALTIME, &now);
  localtime_r(&now.tv_sec, &tmv);
  {
    struct timespec mono;
    clock_gettime(CLOCK_MONOTONIC, &mono);
    elapsed = (double) (mono.tv_sec - gStart.tv_sec) +
              (double) (mono.tv_nsec - gStart.tv_nsec) / 1e9;
  }
  snprintf(out, n, "%04d-%02d-%02d %02d:%02d:%02d.%03d %+9.3f",
           tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
           tmv.tm_hour, tmv.tm_min, tmv.tm_sec, (int) (now.tv_nsec / 1000000),
           elapsed);
#endif
}

static void dlog_emit(const char* text) {
  char ts[64];
  if(gLog == NULL) return;
  dlog_stamp(ts, sizeof(ts));
  fprintf(gLog, "%s  %s\n", ts, text);
  dlog_flush();
}

void dlog(const char* fmt, ...) {
  char buf[1024];
  va_list ap;
  if(gLog == NULL) return;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  dlog_emit(buf);
}

void dlog_stage(const char* fmt, ...) {
  char buf[1024];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  {                                    /* kept even when there is no log file */
    size_t n = strlen(buf);
    if(n >= sizeof(gStage)) n = sizeof(gStage) - 1;
    memcpy(gStage, buf, n);
    gStage[n] = 0;
  }
  dlog_emit(buf);
}

const char* dlog_last_stage(void) { return gStage; }

void dlog_close(void) {
  if(gLog == NULL) return;
  dlog_flush();
  fclose(gLog);
  gLog = NULL;
}

/* Next to the executable on Windows (the release zip is unpacked wherever the
 * player likes and the working directory is whatever Explorer felt like), the
 * working directory everywhere else. */
static void dlog_path(char* out, size_t n) {
#ifdef _WIN32
  char exe[MAX_PATH];
  DWORD got = GetModuleFileNameA(NULL, exe, (DWORD) sizeof(exe));
  if(got > 0 && got < sizeof(exe)) {
    char* slash = strrchr(exe, '\\');
    char* fwd = strrchr(exe, '/');
    if(fwd != NULL && (slash == NULL || fwd > slash)) slash = fwd;
    if(slash != NULL) {
      *slash = 0;
      snprintf(out, n, "%s\\dream.log", exe);
      return;
    }
  }
#endif
  snprintf(out, n, "dream.log");
}

void dlog_open(void) {
  char path[1024];
#ifdef _WIN32
  gStartTick = GetTickCount64();
#else
  clock_gettime(CLOCK_MONOTONIC, &gStart);
#endif
  dlog_path(path, sizeof(path));
  gLog = fopen(path, "w");
  if(gLog == NULL) {
    /* An unwritable directory is not a reason to refuse to play; the game just
     * has no log. Nothing is printed: stdout carries the frame line and only
     * the frame line. */
    return;
  }
  setvbuf(gLog, NULL, _IOLBF, 0);
  dlog_stage("dream: log opened at %s", path);
}

/* ---- crash capture ------------------------------------------------------ */

#ifdef _WIN32

/* GetCurrentFiber() reads the TEB through a segment override; GCC's
 * -Warray-bounds sees a dereference of a small constant address and warns.
 * Same wrapper, same reason, as recomp/harness/coro_fibers.c. */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
#endif
static void* dlog_current_fiber(void) {
  return IsThreadAFiber() ? GetCurrentFiber() : NULL;
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

/* Where dream.exe is loaded, how big it is, and the base it was *linked* at.
 * ASLR moves the first; the map file is written in terms of the third, so an
 * address in the log is only useful with all three: the log prints the offset
 * from the load base and the address to look up in dream.map. */
static uintptr_t     gModBase;
static unsigned long gModSize;
static unsigned long long gModLinkBase;

static void dlog_module_range(void) {
  HMODULE h = GetModuleHandleA(NULL);
  gModBase = (uintptr_t) h;
  gModSize = 0;
  gModLinkBase = 0;
  if(h == NULL) return;
  {
    const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*) h;
    if(dos->e_magic != IMAGE_DOS_SIGNATURE) return;
    {
      const IMAGE_NT_HEADERS* nt =
        (const IMAGE_NT_HEADERS*) ((const BYTE*) h + dos->e_lfanew);
      if(nt->Signature != IMAGE_NT_SIGNATURE) return;
      gModSize = (unsigned long) nt->OptionalHeader.SizeOfImage;
      gModLinkBase = (unsigned long long) nt->OptionalHeader.ImageBase;
    }
  }
}

/* "dream+0x1234 (map 0x140001234)" for an address inside the image, the bare
 * address for anything else (a Windows DLL, or a fiber stack gone astray). */
static void dlog_addr(char* out, size_t n, uintptr_t a) {
  if(gModSize != 0 && a >= gModBase && a < gModBase + gModSize)
    snprintf(out, n, "%p  dream+0x%llx  (map 0x%llx)", (void*) a,
             (unsigned long long) (a - gModBase),
             gModLinkBase + (unsigned long long) (a - gModBase));
  else
    snprintf(out, n, "%p", (void*) a);
}

static const char* dlog_exception_name(DWORD code) {
  switch(code) {
    case EXCEPTION_ACCESS_VIOLATION:         return "ACCESS_VIOLATION";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:    return "ARRAY_BOUNDS_EXCEEDED";
    case EXCEPTION_BREAKPOINT:               return "BREAKPOINT";
    case EXCEPTION_DATATYPE_MISALIGNMENT:    return "DATATYPE_MISALIGNMENT";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:       return "FLT_DIVIDE_BY_ZERO";
    case EXCEPTION_ILLEGAL_INSTRUCTION:      return "ILLEGAL_INSTRUCTION";
    case EXCEPTION_IN_PAGE_ERROR:            return "IN_PAGE_ERROR";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:       return "INT_DIVIDE_BY_ZERO";
    case EXCEPTION_INT_OVERFLOW:             return "INT_OVERFLOW";
    case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "NONCONTINUABLE_EXCEPTION";
    case EXCEPTION_PRIV_INSTRUCTION:         return "PRIV_INSTRUCTION";
    case EXCEPTION_STACK_OVERFLOW:           return "STACK_OVERFLOW";
    case 0xE06D7363u:                        return "C++ exception";
    default:                                 return "unknown";
  }
}

/* Addresses only. Symbolising them needs a symbol server and a DLL this
 * executable deliberately does not import (dbghelp), and the map file built
 * beside the exe does the same job offline: subtract the module base from an
 * address and look the offset up in dream.map. */
static void dlog_stack_walk(void) {
  typedef USHORT (WINAPI *PfnCaptureBackTrace)(ULONG, ULONG, PVOID*, PULONG);
  PfnCaptureBackTrace capture = NULL;
  HMODULE mod = GetModuleHandleA("kernel32.dll");
  FARPROC raw = mod != NULL ? GetProcAddress(mod, "RtlCaptureStackBackTrace") : NULL;
  if(raw == NULL) {
    mod = GetModuleHandleA("ntdll.dll");
    raw = mod != NULL ? GetProcAddress(mod, "RtlCaptureStackBackTrace") : NULL;
  }
  if(raw == NULL) {
    dlog("!!   stack   RtlCaptureStackBackTrace is not available");
    return;
  }
  memcpy(&capture, &raw, sizeof(capture));   /* no function/object cast warning */
  {
    PVOID frames[40];
    USHORT n = capture(0, (ULONG) (sizeof(frames) / sizeof(frames[0])), frames, NULL);
    USHORT i;
    dlog("!!   stack   %u frame(s)", (unsigned) n);
    for(i = 0; i < n; i++) {
      char where[128];
      dlog_addr(where, sizeof(where), (uintptr_t) frames[i]);
      dlog("!!     [%2u] %s", (unsigned) i, where);
    }
  }
}

static LONG WINAPI dlog_seh_filter(EXCEPTION_POINTERS* ep) {
  if(gInCrash) DLOG_DIE(DLOG_EXIT_CRASH);
  gInCrash = 1;
  dlog_module_range();
  dlog("!! CRASH: unhandled exception");
  if(ep != NULL && ep->ExceptionRecord != NULL) {
    const EXCEPTION_RECORD* er = ep->ExceptionRecord;
    char where[128];
    dlog_addr(where, sizeof(where), (uintptr_t) er->ExceptionAddress);
    dlog("!!   code    0x%08lX  %s", (unsigned long) er->ExceptionCode,
         dlog_exception_name(er->ExceptionCode));
    dlog("!!   address %s", where);
    if(er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2) {
      const char* how = er->ExceptionInformation[0] == 0 ? "read"
                      : er->ExceptionInformation[0] == 1 ? "write"
                      : er->ExceptionInformation[0] == 8 ? "execute" : "?";
      dlog("!!   fault   %s of 0x%llx", how,
           (unsigned long long) er->ExceptionInformation[1]);
    }
  } else {
    dlog("!!   (no exception record)");
  }
  if(ep != NULL && ep->ContextRecord != NULL) {
#if defined(_M_X64) || defined(__x86_64__)
    const CONTEXT* c = ep->ContextRecord;
    dlog("!!   rip     0x%llx  rsp 0x%llx  rbp 0x%llx",
         (unsigned long long) c->Rip, (unsigned long long) c->Rsp,
         (unsigned long long) c->Rbp);
#endif
  }
  dlog("!!   module  loaded at %p, size 0x%lx, linked at 0x%llx"
       "  (dream.map beside dream.exe)",
       (void*) gModBase, gModSize, gModLinkBase);
  dlog("!!   fiber   %p%s", dlog_current_fiber(),
       IsThreadAFiber() ? "  (running on a coro stack)"
                        : "  (not a fiber: the plain thread)");
  dlog("!!   stage   %s", gStage);
  dlog_stack_walk();
  dlog("!! exiting with %d", DLOG_EXIT_CRASH);
  dlog_flush();
  dlog_close();                                     /* on disk before we leave */
  DLOG_DIE(DLOG_EXIT_CRASH);
  return EXCEPTION_EXECUTE_HANDLER;
}
#endif /* _WIN32 */

static const char* dlog_signal_name(int sig) {
  switch(sig) {
    case SIGABRT: return "SIGABRT";
    case SIGSEGV: return "SIGSEGV";
    case SIGILL:  return "SIGILL";
    case SIGFPE:  return "SIGFPE";
#ifdef SIGBUS
    case SIGBUS:  return "SIGBUS";
#endif
    default:      return "signal";
  }
}

static void dlog_signal_handler(int sig) {
  signal(sig, SIG_DFL);            /* never come back here twice */
  if(gInCrash) DLOG_DIE(DLOG_EXIT_SIGNAL);
  gInCrash = 1;
  dlog("!! CRASH: %s (%d)", dlog_signal_name(sig), sig);
  dlog("!!   stage   %s", gStage);
#ifdef _WIN32
  dlog_module_range();
  dlog("!!   module  loaded at %p, size 0x%lx, linked at 0x%llx",
       (void*) gModBase, gModSize, gModLinkBase);
  dlog("!!   fiber   %p", dlog_current_fiber());
  dlog_stack_walk();
#endif
  dlog("!! exiting with %d", DLOG_EXIT_SIGNAL);
  dlog_flush();
  dlog_close();
  DLOG_DIE(DLOG_EXIT_SIGNAL);
}

void dlog_install_crash_handlers(void) {
#ifdef _WIN32
  /* No "dream.exe has stopped working" box and no critical-error dialog: a
   * process started from a script or a shortcut must fall over and be gone,
   * leaving the log behind, rather than sit on a modal window nobody sees. */
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
  SetUnhandledExceptionFilter(dlog_seh_filter);
  dlog_module_range();
  /* Written at startup as well as at a fault: a hang leaves no crash block, and
   * this is still what an address from a debugger has to be measured against. */
  dlog("module: dream.exe loaded at %p, size 0x%lx, linked at 0x%llx"
       " (add an RVA to the link base to find it in dream.map)",
       (void*) gModBase, gModSize, gModLinkBase);
  dlog_stage("crash handlers installed (SEH filter, SetErrorMode, signals)");
#else
  dlog_stage("crash handlers installed (signals)");
#endif
  signal(SIGABRT, dlog_signal_handler);
  signal(SIGSEGV, dlog_signal_handler);
  signal(SIGILL,  dlog_signal_handler);
  signal(SIGFPE,  dlog_signal_handler);
#ifdef SIGBUS
  signal(SIGBUS,  dlog_signal_handler);
#endif
}
