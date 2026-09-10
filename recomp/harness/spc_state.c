/* Implementation of the SPC recomp hook API over the vendored LakeSnes APU.
 *
 * The timed accessors go through apu_spcRead / apu_spcWrite / apu_spcIdle, the
 * very handlers apu_init() hands to spc_init(), so a hook that replays a
 * routine's access sequence costs the emulator exactly what the routine cost:
 * one APU cycle each, the timers and the DSP ticked at the same instants, the
 * same register block and the same IPL ROM overlay.
 */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "sps_internal.h"

/* ---- registers -------------------------------------------------------- */
uint8_t  sps_a(const SpcState* sp)  { return sp->spc->a; }
uint8_t  sps_x(const SpcState* sp)  { return sp->spc->x; }
uint8_t  sps_y(const SpcState* sp)  { return sp->spc->y; }
uint8_t  sps_sp(const SpcState* sp) { return sp->spc->sp; }
uint16_t sps_pc(const SpcState* sp) { return sp->spc->pc; }

uint16_t sps_ya(const SpcState* sp) {
  return (uint16_t) (sp->spc->a | (sp->spc->y << 8));
}

/* nvpbhizc, the order spc_getFlags() pushes */
uint8_t sps_psw(const SpcState* sp) {
  const Spc* s = sp->spc;
  return (uint8_t) ((s->n << 7) | (s->v << 6) | (s->p << 5) | (s->b << 4) |
                    (s->h << 3) | (s->i << 2) | (s->z << 1) | s->c);
}

void sps_set_a(SpcState* sp, uint8_t v)  { sp->spc->a = v; }
void sps_set_x(SpcState* sp, uint8_t v)  { sp->spc->x = v; }
void sps_set_y(SpcState* sp, uint8_t v)  { sp->spc->y = v; }
void sps_set_sp(SpcState* sp, uint8_t v) { sp->spc->sp = v; }
void sps_set_pc(SpcState* sp, uint16_t v) { sp->spc->pc = v; }

void sps_set_ya(SpcState* sp, uint16_t v) {
  sp->spc->a = (uint8_t) v;
  sp->spc->y = (uint8_t) (v >> 8);
}

void sps_set_psw(SpcState* sp, uint8_t v) {
  Spc* s = sp->spc;
  s->n = (v & 0x80) != 0;
  s->v = (v & 0x40) != 0;
  s->p = (v & 0x20) != 0;
  s->b = (v & 0x10) != 0;
  s->h = (v & 0x08) != 0;
  s->i = (v & 0x04) != 0;
  s->z = (v & 0x02) != 0;
  s->c = (v & 0x01) != 0;
}

/* ---- flags ------------------------------------------------------------ */
bool sps_c(const SpcState* sp) { return sp->spc->c; }
bool sps_z(const SpcState* sp) { return sp->spc->z; }
bool sps_v(const SpcState* sp) { return sp->spc->v; }
bool sps_n(const SpcState* sp) { return sp->spc->n; }
bool sps_i(const SpcState* sp) { return sp->spc->i; }
bool sps_h(const SpcState* sp) { return sp->spc->h; }
bool sps_p(const SpcState* sp) { return sp->spc->p; }
bool sps_b(const SpcState* sp) { return sp->spc->b; }
void sps_set_c(SpcState* sp, bool v) { sp->spc->c = v; }
void sps_set_z(SpcState* sp, bool v) { sp->spc->z = v; }
void sps_set_v(SpcState* sp, bool v) { sp->spc->v = v; }
void sps_set_n(SpcState* sp, bool v) { sp->spc->n = v; }
void sps_set_h(SpcState* sp, bool v) { sp->spc->h = v; }
void sps_set_p(SpcState* sp, bool v) { sp->spc->p = v; }

void sps_set_zn(SpcState* sp, uint8_t v) {
  sp->spc->z = (v == 0);
  sp->spc->n = (v & 0x80) != 0;
}

/* movw / addw set Z on the whole word and N on bit 15, as spc.c case 0xba does */
void sps_set_zn16(SpcState* sp, uint16_t v) {
  sp->spc->z = (v == 0);
  sp->spc->n = (v & 0x8000) != 0;
}

uint16_t sps_dp(const SpcState* sp, uint8_t off) {
  return (uint16_t) (off | (sp->spc->p << 8));
}

/* ---- ARAM, untimed ---------------------------------------------------- */
uint8_t sps_aram_r8(const SpcState* sp, uint16_t adr) { return sp->apu->ram[adr]; }

uint16_t sps_aram_r16(const SpcState* sp, uint16_t adr) {
  return (uint16_t) (sp->apu->ram[adr] | (sp->apu->ram[(uint16_t) (adr + 1)] << 8));
}

void sps_aram_w8(SpcState* sp, uint16_t adr, uint8_t v) { sp->apu->ram[adr] = v; }

void sps_aram_w16(SpcState* sp, uint16_t adr, uint16_t v) {
  sp->apu->ram[adr] = (uint8_t) v;
  sp->apu->ram[(uint16_t) (adr + 1)] = (uint8_t) (v >> 8);
}

/* ---- memory, timed ---------------------------------------------------- */
uint8_t sps_read8(SpcState* sp, uint16_t adr)  { return apu_spcRead(sp->apu, adr); }
void sps_write8(SpcState* sp, uint16_t adr, uint8_t v) { apu_spcWrite(sp->apu, adr, v); }
void sps_idle(SpcState* sp) { apu_spcIdle(sp->apu, false); }

void sps_fetch(SpcState* sp, int bytes) {
  for(int i = 0; i < bytes; i++) apu_spcRead(sp->apu, sp->spc->pc++);
}

/* ---- stack ------------------------------------------------------------ */
void sps_push8(SpcState* sp, uint8_t v) {
  apu_spcWrite(sp->apu, (uint16_t) (0x100 | sp->spc->sp), v);
  sp->spc->sp--;
}

uint8_t sps_pull8(SpcState* sp) {
  sp->spc->sp++;
  return apu_spcRead(sp->apu, (uint16_t) (0x100 | sp->spc->sp));
}

/* spc_pushWord() pushes the high byte first, so the word reads back low-first */
void sps_push16(SpcState* sp, uint16_t v) {
  sps_push8(sp, (uint8_t) (v >> 8));
  sps_push8(sp, (uint8_t) v);
}

uint16_t sps_pull16(SpcState* sp) {
  uint8_t lo = sps_pull8(sp);
  return (uint16_t) (lo | (sps_pull8(sp) << 8));
}

/* ---- returns ---------------------------------------------------------- */
/* mirrors spc.c case 0x6f, minus the opcode fetch the body supplies */
void sps_ret(SpcState* sp) {
  apu_spcRead(sp->apu, sp->spc->pc);
  sps_idle(sp);
  sp->spc->pc = sps_pull16(sp);
}

/* ---- DSP -------------------------------------------------------------- */
uint8_t sps_dsp_addr(const SpcState* sp) { return sp->apu->dspAdr; }

uint8_t sps_dsp_read(const SpcState* sp, uint8_t reg) {
  return dsp_read(sp->apu->dsp, (uint8_t) (reg & 0x7f));
}

void sps_dsp_write(SpcState* sp, uint8_t reg, uint8_t v) {
  if(reg < 0x80) dsp_write(sp->apu->dsp, reg, v);
}

/* ---- ports ------------------------------------------------------------ */
uint8_t sps_port_in(const SpcState* sp, int i)  { return sp->apu->inPorts[i & 3]; }
uint8_t sps_port_out(const SpcState* sp, int i) { return sp->apu->outPorts[i & 3]; }

void sps_set_port_out(SpcState* sp, int i, uint8_t v) {
  sp->apu->outPorts[i & 3] = v;
  sp->apu->ram[0xf4 + (i & 3)] = v;   /* apu_write() mirrors every store into ram */
}

/* ---- timers ----------------------------------------------------------- */
uint8_t sps_timer_target(const SpcState* sp, int i)  { return sp->apu->timer[i % 3].target; }
uint8_t sps_timer_counter(const SpcState* sp, int i) { return sp->apu->timer[i % 3].counter; }
uint8_t sps_timer_divider(const SpcState* sp, int i) { return sp->apu->timer[i % 3].divider; }
bool    sps_timer_enabled(const SpcState* sp, int i) { return sp->apu->timer[i % 3].enabled; }

/* ---- cycle accounting ------------------------------------------------- */
uint32_t sps_cycles(const SpcState* sp) { return sp->apu->cycles; }

void sps_consume_cycles(SpcState* sp, int cycles) {
  for(int i = 0; i < cycles; i++) sps_idle(sp);
}

/* What the core's own implementation of an opcode costs, in APU cycles: one per
 * spc_read / spc_write / spc_idle it performs, counted off spc.c's own case
 * bodies and the addressing-mode helpers they call. Only the encodings
 * recomp/spc/ uses are listed; everything else returns 0, as documented.
 * `dream_harness --test-spc-timing` executes each of them on the reference core
 * and checks the figure, so this table is a checked fact rather than a claim.
 *
 * A taken branch costs two more (spc_doBranch's two idles) and is not included;
 * neither is the extra work a call's callee does. */
int sps_op_cycles(uint8_t opcode) {
  switch(opcode) {
    /* implied, 2: opcode fetch + the dummy read at pc */
    case 0x1c: case 0x1d: case 0x20: case 0x3d: case 0x5d: case 0x60:
    case 0x7d: case 0x80: case 0x9c: case 0xbc: case 0xbd: case 0xdc:
    case 0xdd: case 0xfc: case 0xfd:
      return 2;
    /* immediate, 2: opcode fetch + the immediate read (spc_adrImm) */
    case 0x28: case 0x68: case 0x8d: case 0xa8: case 0xcd: case 0xe8:
      return 2;
    /* branches, 2 not taken (opcode + displacement); +2 when taken */
    case 0x10: case 0x2f: case 0x30: case 0xd0: case 0xf0:
      return 2;
    case 0x3e: case 0x64: case 0xe4: case 0xf8: return 3;  /* op + dp + read */
    case 0xab: return 4;                       /* inc dp: op + dp + read + write */
    case 0xc4: case 0xcb: case 0xd8: return 4; /* mov dp,r: op + dp + read + write */
    case 0xe5: case 0xe9: return 4;            /* op + 2 abs + read */
    case 0x5f: return 3;                       /* jmp abs: op + 2 abs */
    case 0xac: return 5;                       /* inc abs: op + 2 abs + read + write */
    case 0xc5: case 0xc9: case 0xcc: return 5; /* mov abs,r: op + 2 abs + read + write */
    case 0x8f: return 5;                       /* mov dp,#imm: op + imm + dp + read + write */
    case 0x98: return 5;                       /* adc dp,#imm: same shape */
    case 0xba: case 0x7a: return 5;            /* movw ya,dp / addw: op+dp+read+idle+read */
    case 0xda: return 5;                       /* movw dp,ya: op+dp+read+2 writes */
    case 0xf6: return 5;                       /* mov a,abs+y: op+2 abs+idle+read */
    case 0xd4: return 5;                       /* mov dp+x,a: op+dp+idle+read+write */
    case 0x1a: return 6;                       /* decw dp: op+dp+2 reads+2 writes */
    case 0xd5: case 0xd6: return 6;            /* mov abs+r,a: op+2 abs+idle+read+write */
    case 0xf7: return 6;                       /* mov a,(dp)+y: op+dp+2 reads+idle+read */
    case 0x1f: return 6;                       /* jmp (abs+x): op+2 abs+idle+2 reads */
    case 0xd7: return 7;                       /* mov (dp)+y,a: op+dp+2 reads+idle+read+write */
    case 0x2d: case 0x4d: return 4;            /* push: op + dummy read + write + idle */
    case 0xae: case 0xce: return 4;            /* pop: op + dummy read + idle + read */
    case 0x6f: return 5;                       /* ret: op + dummy read + idle + 2 pulls */
    case 0x3f: return 8;                       /* call: op+2 abs+idle+2 pushes+2 idles */
    case 0x6e: return 5;                       /* dbnz dp: op+dp+read+write+rel (+2 taken) */
    default: return 0;
  }
}

/* ---- calling a routine that is not converted yet ---------------------- */
/* Completion is the stack pointer coming back above the frame we pushed: a `ret`
 * pops exactly that far. The SPC stack is one page, so the comparison is 8-bit;
 * a callee that unbalanced it downward would hang, hence the guard. */
void sps_run_until_return(SpcState* sp, uint8_t spBefore) {
  unsigned long guard = 0;
  while(sp->spc->sp < spBefore) {
    spc_runOpcode(sp->spc);
    if(++guard > 100000000ul) {
      fprintf(stderr, "spc_state: sps_call did not return (sp %02X, want %02X)\n",
              sp->spc->sp, spBefore);
      exit(2);
    }
  }
}

bool sps_run_callee(SpcState* sp, uint8_t spBefore) {
  while(sp->spc->sp < spBefore) {
    if(sps_yield_wanted(sp)) return true;
    spc_runOpcode(sp->spc);
  }
  return false;
}

/* the five cycles of `call abs` after its three fetches, then the callee */
void sps_call(SpcState* sp, uint16_t retAddr, uint16_t callee) {
  uint8_t spBefore = sp->spc->sp;
  sps_idle(sp);
  sps_push16(sp, retAddr);
  sps_idle(sp);
  sps_idle(sp);
  sp->spc->pc = callee;
  sps_run_until_return(sp, spBefore);
}

/* ---- yielding back to the driver -------------------------------------- */
void sps_enter_hook(SpcState* sp) {
  sp->depth++;
  if(sp->depth > sp->maxDepth) sp->maxDepth = sp->depth;
}

void sps_leave_hook(SpcState* sp) {
  if(sp->depth <= 0) {
    fprintf(stderr, "spc_state: sps_leave_hook with no hook in flight\n");
    exit(2);
  }
  sp->depth--;
}

int sps_hook_depth(const SpcState* sp) { return sp->depth; }
int sps_hook_max_depth(const SpcState* sp) { return sp->maxDepth; }

bool sps_yield_wanted(const SpcState* sp) {
  /* Signed difference: apu->cycles is a 32-bit counter and wraps. */
  return (int32_t) (sp->apu->cycles - sp->apu->sliceEnd) >= 0;
}

/* ---- routine registry ------------------------------------------------- */
static SpcRecompEntry gRegistry[RECOMP_SPC_HOOKS_MAX];
static unsigned gRegistryCount;

unsigned long recomp_spc_hook_hits[RECOMP_SPC_HOOKS_MAX];

void recomp_spc_register(uint16_t entry_addr, const char* name, SpcRecompFn fn) {
  for(unsigned i = 0; i < gRegistryCount; i++) {
    if(gRegistry[i].addr == entry_addr) {
      fprintf(stderr, "recomp_spc_register: %04X registered twice (%s and %s)\n",
              entry_addr, gRegistry[i].name, name);
      exit(2);
    }
  }
  if(gRegistryCount == RECOMP_SPC_HOOKS_MAX) {
    fprintf(stderr, "recomp_spc_register: more than %d routines; raise RECOMP_SPC_HOOKS_MAX\n",
            RECOMP_SPC_HOOKS_MAX);
    exit(2);
  }
  gRegistry[gRegistryCount].addr = entry_addr;
  gRegistry[gRegistryCount].name = name;
  gRegistry[gRegistryCount].fn = fn;
  gRegistryCount++;
}

const SpcRecompEntry* recomp_spc_registry(unsigned* count) {
  *count = gRegistryCount;
  return gRegistry;
}
