
#ifndef SPC_H
#define SPC_H

#include <stdint.h>
#include <stdbool.h>

#include "statehandler.h"

typedef uint8_t (*SpcReadHandler)(void* mem, uint16_t adr);
typedef void (*SpcWriteHandler)(void* mem, uint16_t adr, uint8_t val);
typedef void (*SpcIdleHandler)(void* mem, bool waiting);

typedef struct Spc Spc;

// dream: recomp hook dispatch, the SPC700 twin of CpuHookHandler in cpu.h. Called
// before every opcode fetch with the 16-bit pc; if it returns true the instruction
// is not executed and the hook is responsible for leaving pc where execution should
// resume. A hook must always consume at least one SPC cycle, because apu_runCycles()
// loops until the slice is spent and would otherwise never make progress.
typedef bool (*SpcHookHandler)(void* ctx, Spc* spc, uint16_t pc);

struct Spc {
  // reference to memory handler, pointers to read/write/idle handlers
  void* mem;
  SpcReadHandler read;
  SpcWriteHandler write;
  SpcIdleHandler idle;
  // registers
  uint8_t a;
  uint8_t x;
  uint8_t y;
  uint8_t sp;
  uint16_t pc;
  // flags
  bool c;
  bool z;
  bool v;
  bool n;
  bool i;
  bool h;
  bool p;
  bool b;
  // stopping
  bool stopped;
  // reset
  bool resetWanted;
  // dream: recomp hook (NULL = stock behaviour)
  SpcHookHandler hook;
  void* hookCtx;
};

Spc* spc_init(void* mem, SpcReadHandler read, SpcWriteHandler write, SpcIdleHandler idle);
void spc_free(Spc* spc);
void spc_reset(Spc* spc, bool hard);
void spc_handleState(Spc* spc, StateHandler* sh);
void spc_runOpcode(Spc* spc);

#endif
