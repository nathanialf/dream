/* Private glue between the public hook API (recomp/include/snes_state.h) and the
 * vendored LakeSnes core. Harness sources only. */
#ifndef DREAM_SS_INTERNAL_H
#define DREAM_SS_INTERNAL_H

#include "snes_state.h"
#include "snes.h"
#include "cpu.h"

struct SnesState {
  Snes* snes;
};

#endif
