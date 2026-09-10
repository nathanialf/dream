
#ifndef CPU_H
#define CPU_H

#include <stdint.h>
#include <stdbool.h>

#include "statehandler.h"

typedef uint8_t (*CpuReadHandler)(void* mem, uint32_t adr);
typedef void (*CpuWriteHandler)(void* mem, uint32_t adr, uint8_t val);
typedef void (*CpuIdleHandler)(void* mem, bool waiting);

typedef struct Cpu Cpu;

// dream: recomp hook dispatch. Called before every instruction fetch with the
// 24-bit PC; if it returns true the instruction is not executed and the hook is
// responsible for leaving pc/k at the point execution should resume.
typedef bool (*CpuHookHandler)(void* ctx, Cpu* cpu, uint32_t pc24);

struct Cpu {
  // reference to memory handler, pointers to read/write/idle handlers
  void* mem;
  CpuReadHandler read;
  CpuWriteHandler write;
  CpuIdleHandler idle;
  // registers
  uint16_t a;
  uint16_t x;
  uint16_t y;
  uint16_t sp;
  uint16_t pc;
  uint16_t dp; // direct page (D)
  uint8_t k; // program bank (PB)
  uint8_t db; // data bank (B)
  // flags
  bool c;
  bool z;
  bool v;
  bool n;
  bool i;
  bool d;
  bool xf;
  bool mf;
  bool e;
  // power state (WAI/STP)
  bool waiting;
  bool stopped;
  // interrupts
  bool irqWanted;
  bool nmiWanted;
  bool intWanted;
  bool resetWanted;
  // dream: recomp hook (NULL = stock behaviour)
  CpuHookHandler hook;
  void* hookCtx;
};

Cpu* cpu_init(void* mem, CpuReadHandler read, CpuWriteHandler write, CpuIdleHandler idle);
void cpu_free(Cpu* cpu);
void cpu_reset(Cpu* cpu, bool hard);
void cpu_handleState(Cpu* cpu, StateHandler* sh);
void cpu_runOpcode(Cpu* cpu);
// dream: the non-instruction half of cpu_runOpcode (reset, stp/wai, interrupt
// entry). True when it ran one of them; false when an instruction would follow.
bool cpu_runNonInstruction(Cpu* cpu);
void cpu_nmi(Cpu* cpu);
void cpu_setIrq(Cpu* cpu, bool state);
// dream: latch pending interrupts the way the last cycle of an instruction does,
// so a hook that replaces an instruction sequence can end it identically.
void cpu_checkIntPublic(Cpu* cpu);

#endif
