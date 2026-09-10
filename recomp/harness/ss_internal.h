/* Private glue between the public hook API (recomp/include/snes_state.h) and the
 * vendored LakeSnes core. Harness sources only. */
#ifndef DREAM_SS_INTERNAL_H
#define DREAM_SS_INTERNAL_H

#include "snes_state.h"
#include "snes.h"
#include "cpu.h"

struct SnesState {
  Snes* snes;
  /* Snapshot taken by the harness just before a hook runs, so the hook can tell
   * whether the machine has crossed a frame boundary or latched an interrupt
   * while it was running (ss_yield_wanted). */
  bool entryVblank;
  uint32_t entryFrames;
};

#endif
