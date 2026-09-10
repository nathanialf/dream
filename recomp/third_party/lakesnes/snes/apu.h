
#ifndef APU_H
#define APU_H

#include <stdint.h>
#include <stdbool.h>

typedef struct Apu Apu;

#include "snes.h"
#include "spc.h"
#include "dsp.h"
#include "statehandler.h"

typedef struct Timer {
  uint8_t cycles;
  uint8_t divider;
  uint8_t target;
  uint8_t counter;
  bool enabled;
} Timer;

struct Apu {
  Snes* snes;
  Spc* spc;
  Dsp* dsp;
  uint8_t ram[0x10000];
  bool romReadable;
  uint8_t dspAdr;
  uint32_t cycles;
  // dream: the cycle count at which the catch-up step running now would end.
  // apu_runCycles() runs whole opcodes until the slice is spent, so the reference
  // SPC stops between two instructions of a routine at exactly the point a recomp
  // hook would otherwise have to run the routine to its end. A hook compares
  // `cycles` against this and hands the rest of the routine back (spc_state.h,
  // sps_yield_wanted); it is the SPC700 twin of the 65816 frame boundary.
  uint32_t sliceEnd;
  uint8_t inPorts[6]; // includes 2 bytes of ram
  uint8_t outPorts[4];
  Timer timer[3];
};

Apu* apu_init(Snes* snes);
void apu_free(Apu* apu);
void apu_reset(Apu* apu);
void apu_handleState(Apu* apu, StateHandler* sh);
int apu_runCycles(Apu* apu, int wantedCycles);
uint8_t apu_read(Apu* apu, uint16_t adr);
void apu_write(Apu* apu, uint16_t adr, uint8_t val);
uint8_t apu_spcRead(void* mem, uint16_t adr);
void apu_spcWrite(void* mem, uint16_t adr, uint8_t val);
void apu_spcIdle(void* mem, bool waiting);

#endif
