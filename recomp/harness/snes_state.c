/* Implementation of the recomp hook API over the vendored LakeSnes core.
 *
 * The timed accessors go through snes_cpuRead / snes_cpuWrite / snes_cpuIdle,
 * the very functions the CPU core hands to cpu_init(), so a hook that replays a
 * routine's bus transactions costs the emulator exactly what the routine cost:
 * the same access times, the same DMA/HDMA interleaving, the same open-bus.
 */
#include <stdint.h>
#include <stdbool.h>

#include <stdio.h>
#include <stdlib.h>

#include "ss_internal.h"
#include "dma.h"

/* ---- registers -------------------------------------------------------- */
uint16_t ss_a(const SnesState* ss)  { return ss->snes->cpu->a; }
uint16_t ss_x(const SnesState* ss)  { return ss->snes->cpu->x; }
uint16_t ss_y(const SnesState* ss)  { return ss->snes->cpu->y; }
uint16_t ss_sp(const SnesState* ss) { return ss->snes->cpu->sp; }
uint16_t ss_dp(const SnesState* ss) { return ss->snes->cpu->dp; }
uint16_t ss_pc(const SnesState* ss) { return ss->snes->cpu->pc; }
uint8_t  ss_db(const SnesState* ss) { return ss->snes->cpu->db; }
uint8_t  ss_pb(const SnesState* ss) { return ss->snes->cpu->k; }

uint8_t ss_p(const SnesState* ss) {
  const Cpu* c = ss->snes->cpu;
  return (uint8_t) ((c->n << 7) | (c->v << 6) | (c->mf << 5) | (c->xf << 4) |
                    (c->d << 3) | (c->i << 2) | (c->z << 1) | c->c);
}

void ss_set_a(SnesState* ss, uint16_t v)  { ss->snes->cpu->a = v; }
void ss_set_x(SnesState* ss, uint16_t v)  { ss->snes->cpu->x = v; }
void ss_set_y(SnesState* ss, uint16_t v)  { ss->snes->cpu->y = v; }
void ss_set_sp(SnesState* ss, uint16_t v) { ss->snes->cpu->sp = v; }
void ss_set_dp(SnesState* ss, uint16_t v) { ss->snes->cpu->dp = v; }
void ss_set_db(SnesState* ss, uint8_t v)  { ss->snes->cpu->db = v; }

void ss_set_pc(SnesState* ss, uint8_t bank, uint16_t pc) {
  ss->snes->cpu->k = bank;
  ss->snes->cpu->pc = pc;
}

void ss_set_p(SnesState* ss, uint8_t val) {
  Cpu* c = ss->snes->cpu;
  c->n = (val & 0x80) != 0;
  c->v = (val & 0x40) != 0;
  c->mf = (val & 0x20) != 0;
  c->xf = (val & 0x10) != 0;
  c->d = (val & 0x08) != 0;
  c->i = (val & 0x04) != 0;
  c->z = (val & 0x02) != 0;
  c->c = (val & 0x01) != 0;
  if(c->e) { c->mf = true; c->xf = true; c->sp = (c->sp & 0xff) | 0x100; }
  if(c->xf) { c->x &= 0xff; c->y &= 0xff; }
}

bool ss_flag_e(const SnesState* ss) { return ss->snes->cpu->e; }
bool ss_flag_m(const SnesState* ss) { return ss->snes->cpu->mf; }
bool ss_flag_x(const SnesState* ss) { return ss->snes->cpu->xf; }

void ss_set_nz16(SnesState* ss, uint16_t v) {
  Cpu* c = ss->snes->cpu;
  c->z = (v == 0);
  c->n = (v & 0x8000) != 0;
}

void ss_set_nz8(SnesState* ss, uint8_t v) {
  Cpu* c = ss->snes->cpu;
  c->z = (v == 0);
  c->n = (v & 0x80) != 0;
}

bool ss_c(const SnesState* ss) { return ss->snes->cpu->c; }
bool ss_z(const SnesState* ss) { return ss->snes->cpu->z; }
bool ss_v(const SnesState* ss) { return ss->snes->cpu->v; }
bool ss_n(const SnesState* ss) { return ss->snes->cpu->n; }
void ss_set_c(SnesState* ss, bool v) { ss->snes->cpu->c = v; }
void ss_set_z(SnesState* ss, bool v) { ss->snes->cpu->z = v; }
void ss_set_v(SnesState* ss, bool v) { ss->snes->cpu->v = v; }
void ss_set_n(SnesState* ss, bool v) { ss->snes->cpu->n = v; }

bool ss_int_pending(const SnesState* ss) {
  const Cpu* c = ss->snes->cpu;
  return c->intWanted || c->nmiWanted || c->irqWanted;
}

void ss_check_int(SnesState* ss) {
  cpu_checkIntPublic(ss->snes->cpu);
}

/* ---- memory, untimed -------------------------------------------------- */
uint8_t ss_wram_r8(const SnesState* ss, uint32_t off) {
  return ss->snes->ram[off & 0x1ffff];
}

uint16_t ss_wram_r16(const SnesState* ss, uint32_t off) {
  return (uint16_t) (ss->snes->ram[off & 0x1ffff] |
                     (ss->snes->ram[(off + 1) & 0x1ffff] << 8));
}

void ss_wram_w8(SnesState* ss, uint32_t off, uint8_t v) {
  ss->snes->ram[off & 0x1ffff] = v;
}

void ss_wram_w16(SnesState* ss, uint32_t off, uint16_t v) {
  ss->snes->ram[off & 0x1ffff] = (uint8_t) v;
  ss->snes->ram[(off + 1) & 0x1ffff] = (uint8_t) (v >> 8);
}

uint8_t ss_r8(SnesState* ss, uint32_t adr24) {
  return snes_read(ss->snes, adr24 & 0xffffff);
}

uint16_t ss_r16(SnesState* ss, uint32_t adr24) {
  uint8_t lo = snes_read(ss->snes, adr24 & 0xffffff);
  uint8_t hi = snes_read(ss->snes, (adr24 + 1) & 0xffffff);
  return (uint16_t) (lo | (hi << 8));
}

void ss_w8(SnesState* ss, uint32_t adr24, uint8_t v) {
  snes_write(ss->snes, adr24 & 0xffffff, v);
}

void ss_w16(SnesState* ss, uint32_t adr24, uint16_t v) {
  snes_write(ss->snes, adr24 & 0xffffff, (uint8_t) v);
  snes_write(ss->snes, (adr24 + 1) & 0xffffff, (uint8_t) (v >> 8));
}

/* ---- memory, timed ---------------------------------------------------- */
uint8_t ss_bus_r8(SnesState* ss, uint32_t adr24) {
  return snes_cpuRead(ss->snes, adr24 & 0xffffff);
}

void ss_bus_w8(SnesState* ss, uint32_t adr24, uint8_t v) {
  snes_cpuWrite(ss->snes, adr24 & 0xffffff, v);
}

void ss_bus_w16(SnesState* ss, uint32_t adr24, uint16_t v) {
  /* the 65816 writes the low byte first for every non-reversed 16-bit store */
  snes_cpuWrite(ss->snes, adr24 & 0xffffff, (uint8_t) v);
  snes_cpuWrite(ss->snes, (adr24 + 1) & 0xffffff, (uint8_t) (v >> 8));
}

void ss_fetch(SnesState* ss, int bytes) {
  Cpu* c = ss->snes->cpu;
  for(int i = 0; i < bytes; i++) {
    snes_cpuRead(ss->snes, (uint32_t) ((c->k << 16) | c->pc));
    c->pc = (uint16_t) (c->pc + 1);
  }
}

void ss_idle(SnesState* ss) {
  snes_cpuIdle(ss->snes, false);
}

/* ---- returns ---------------------------------------------------------- */
static uint8_t ss_pull8_internal(SnesState* ss);
static uint8_t ss_pull8_internal(SnesState* ss) {
  Cpu* c = ss->snes->cpu;
  c->sp++;
  if(c->e) c->sp = (uint16_t) ((c->sp & 0xff) | 0x100);
  return snes_cpuRead(ss->snes, c->sp);
}

void ss_rts(SnesState* ss) {
  /* mirrors LakeSnes cpu.c case 0x60 exactly */
  Cpu* c = ss->snes->cpu;
  ss_idle(ss);
  ss_idle(ss);
  uint8_t lo = ss_pull8_internal(ss);
  uint8_t hi = ss_pull8_internal(ss);
  c->pc = (uint16_t) ((lo | (hi << 8)) + 1);
  cpu_checkIntPublic(c);
  ss_idle(ss);
}

void ss_rtl(SnesState* ss) {
  /* mirrors LakeSnes cpu.c case 0x6b exactly */
  Cpu* c = ss->snes->cpu;
  ss_idle(ss);
  ss_idle(ss);
  uint8_t lo = ss_pull8_internal(ss);
  uint8_t hi = ss_pull8_internal(ss);
  c->pc = (uint16_t) ((lo | (hi << 8)) + 1);
  cpu_checkIntPublic(c);
  c->k = ss_pull8(ss);
}

/* ---- addressing helpers ----------------------------------------------- */
/* Direct page lives in bank 0 and wraps inside it. The dp-indexed forms this
 * game uses reach well past $00FF (entity_sort_draw_order runs X up to $09C4
 * over "ldy $04,X"), so the access goes through the bank-0 map rather than
 * straight into the WRAM array. */
static uint16_t ss_dp_adr(const SnesState* ss, uint16_t off) {
  return (uint16_t) (ss->snes->cpu->dp + off);
}

uint8_t ss_dp_r8(const SnesState* ss, uint16_t off) {
  SnesState* m = (SnesState*) ss;   /* snes_read is not const-qualified upstream */
  return snes_read(m->snes, ss_dp_adr(ss, off));
}

uint16_t ss_dp_r16(const SnesState* ss, uint16_t off) {
  SnesState* m = (SnesState*) ss;
  uint16_t a = ss_dp_adr(ss, off);
  uint8_t lo = snes_read(m->snes, a);
  uint8_t hi = snes_read(m->snes, (uint16_t) (a + 1));
  return (uint16_t) (lo | (hi << 8));
}

void ss_dp_w8(SnesState* ss, uint16_t off, uint8_t v) {
  snes_write(ss->snes, ss_dp_adr(ss, off), v);
}

void ss_dp_w16(SnesState* ss, uint16_t off, uint16_t v) {
  uint16_t a = ss_dp_adr(ss, off);
  snes_write(ss->snes, a, (uint8_t) v);
  snes_write(ss->snes, (uint16_t) (a + 1), (uint8_t) (v >> 8));
}

uint8_t ss_db_r8(SnesState* ss, uint16_t abs) {
  return snes_read(ss->snes, ((uint32_t) ss->snes->cpu->db << 16) | abs);
}

uint16_t ss_db_r16(SnesState* ss, uint16_t abs) {
  /* an absolute 16-bit access crosses into the next bank at $FFFF, like the CPU */
  uint32_t base = (uint32_t) ss->snes->cpu->db << 16;
  uint8_t lo = snes_read(ss->snes, (base + abs) & 0xffffff);
  uint8_t hi = snes_read(ss->snes, (base + abs + 1) & 0xffffff);
  return (uint16_t) (lo | (hi << 8));
}

void ss_db_w8(SnesState* ss, uint16_t abs, uint8_t v) {
  snes_write(ss->snes, ((uint32_t) ss->snes->cpu->db << 16) | abs, v);
}

void ss_db_w16(SnesState* ss, uint16_t abs, uint16_t v) {
  uint32_t base = (uint32_t) ss->snes->cpu->db << 16;
  snes_write(ss->snes, (base + abs) & 0xffffff, (uint8_t) v);
  snes_write(ss->snes, (base + abs + 1) & 0xffffff, (uint8_t) (v >> 8));
}

/* ---- hardware registers (timed, DMA-aware) ---------------------------- */
uint8_t ss_reg_r8(SnesState* ss, uint32_t adr24) {
  return snes_cpuRead(ss->snes, adr24 & 0xffffff);
}

uint16_t ss_reg_r16(SnesState* ss, uint32_t adr24) {
  uint8_t lo = snes_cpuRead(ss->snes, adr24 & 0xffffff);
  uint8_t hi = snes_cpuRead(ss->snes, (adr24 + 1) & 0xffffff);
  return (uint16_t) (lo | (hi << 8));
}

void ss_reg_w8(SnesState* ss, uint32_t adr24, uint8_t v) {
  snes_cpuWrite(ss->snes, adr24 & 0xffffff, v);
}

void ss_reg_w16(SnesState* ss, uint32_t adr24, uint16_t v) {
  snes_cpuWrite(ss->snes, adr24 & 0xffffff, (uint8_t) v);
  snes_cpuWrite(ss->snes, (adr24 + 1) & 0xffffff, (uint8_t) (v >> 8));
}

/* ---- stack ------------------------------------------------------------ */
uint8_t ss_pull8(SnesState* ss) { return ss_pull8_internal(ss); }

uint16_t ss_pull16(SnesState* ss) {
  uint8_t lo = ss_pull8_internal(ss);
  uint8_t hi = ss_pull8_internal(ss);
  return (uint16_t) (lo | (hi << 8));
}

void ss_push8(SnesState* ss, uint8_t v) {
  Cpu* c = ss->snes->cpu;
  snes_cpuWrite(ss->snes, c->sp, v);
  c->sp--;
  if(c->e) c->sp = (uint16_t) ((c->sp & 0xff) | 0x100);
}

void ss_push16(SnesState* ss, uint16_t v) {
  ss_push8(ss, (uint8_t) (v >> 8));
  ss_push8(ss, (uint8_t) v);
}

/* ---- calling a not-yet-converted callee ------------------------------- */
/* Push a return frame, jump the reference CPU into the callee and run it to
 * completion. "Completion" is the stack pointer coming back above the frame we
 * pushed: an rts/rtl pops exactly that far, while an NMI taken inside the callee
 * pushes below it and is therefore invisible to the test. The return address we
 * push is never executed, because the loop stops on the return itself. */
void ss_run_until_return(SnesState* ss, uint16_t spBefore) {
  Cpu* c = ss->snes->cpu;
  /* the sp comparison is unsigned and the stack lives near $01FF; a callee that
   * unbalances the stack downward would hang, so bound the run generously. */
  unsigned long guard = 0;
  while(c->sp < spBefore) {
    cpu_runOpcode(c);
    if(++guard > 100000000ul) {
      fprintf(stderr, "snes_state: ss_call_* did not return (sp %04X, want %04X)\n", c->sp, spBefore);
      exit(2);
    }
  }
}

bool ss_run_callee(SnesState* ss, uint16_t spBefore) {
  Cpu* c = ss->snes->cpu;
  while(c->sp < spBefore) {
    if(ss_yield_wanted(ss)) return true;
    cpu_runOpcode(c);
  }
  return false;
}

void ss_call_sub(SnesState* ss, uint8_t bank, uint16_t addr) {
  Cpu* c = ss->snes->cpu;
  uint16_t spBefore = c->sp;
  ss_push16(ss, (uint16_t) (c->pc - 1));   /* rts adds 1 */
  c->k = bank;
  c->pc = addr;
  ss_run_until_return(ss, spBefore);
}

void ss_call_long(SnesState* ss, uint8_t bank, uint16_t addr) {
  Cpu* c = ss->snes->cpu;
  uint16_t spBefore = c->sp;
  ss_push8(ss, c->k);
  ss_push16(ss, (uint16_t) (c->pc - 1));   /* rtl adds 1 */
  c->k = bank;
  c->pc = addr;
  ss_run_until_return(ss, spBefore);
}

/* ---- yielding back to the ROM ------------------------------------------- */
/* The entry snapshots are a stack, one frame per hook in flight, because hooks
 * nest: ss_run_callee runs the reference CPU over a callee, and a converted
 * callee's own hook fires inside the caller's. The inner hook has to answer
 * ss_yield_wanted() about its own entry -- it may hand its part of the routine
 * back on its own -- and the outer hook has to get its own answer back when the
 * callee returns, because it is still holding a routine that has to be given
 * back at the boundary it crossed. */
void ss_enter_hook(SnesState* ss) {
  if(ss->depth >= SS_HOOK_DEPTH_MAX) {
    fprintf(stderr, "snes_state: hooks nested deeper than %d; raise SS_HOOK_DEPTH_MAX\n",
            SS_HOOK_DEPTH_MAX);
    exit(2);
  }
  ss->entry[ss->depth].vblank = ss->snes->inVblank;
  ss->entry[ss->depth].frames = ss->snes->frames;
  ss->depth++;
  if(ss->depth > ss->maxDepth) ss->maxDepth = ss->depth;
}

void ss_leave_hook(SnesState* ss) {
  if(ss->depth <= 0) {
    fprintf(stderr, "snes_state: ss_leave_hook with no hook in flight\n");
    exit(2);
  }
  ss->depth--;
}

int ss_hook_depth(const SnesState* ss) { return ss->depth; }
int ss_hook_max_depth(const SnesState* ss) { return ss->maxDepth; }

bool ss_yield_wanted(const SnesState* ss) {
  const Snes* snes = ss->snes;
  if(snes->cpu->intWanted) return true;
  /* Outside a hook there is no routine to hand back, and no snapshot to compare
   * against; only the latched interrupt above is meaningful. */
  if(ss->depth <= 0) return false;
  const SsHookEntry* e = &ss->entry[ss->depth - 1];
  if(snes->frames != e->frames) return true;
  return snes->inVblank && !e->vblank;
}

/* ---- DMA ---------------------------------------------------------------- */
void ss_dma_run(SnesState* ss) {
  Dma* dma = ss->snes->dma;
  /* dma_handleDma() takes two calls to get from "armed" to "done": the first
   * moves the state on, the second performs the transfer and charges its cycles.
   * Those are the two bus cycles the instructions after the store would have
   * supplied, so spend six master cycles for each of them, as an internal cycle
   * would. */
  int guard = 0;
  while(dma->dmaState != 0 && guard++ < 16) {
    dma_handleDma(dma, 6);
    snes_runCycles(ss->snes, 6);
  }
}

/* ---- cycle accounting ------------------------------------------------- */
uint64_t ss_cycles(const SnesState* ss) { return ss->snes->cycles; }

void ss_consume_cycles(SnesState* ss, int cycles) {
  if(cycles <= 0) return;
  /* Spend the time the way the CPU would: in short steps, not one long one.
   * snes_runCycles() inserts the 40-cycle DRAM refresh when a step crosses
   * hPos 536, so a single 4000-cycle call would charge one refresh where the
   * real routine paid three or four, and would run DMA and HDMA at the wrong
   * points inside the frame. Six cycles is the CPU's own shortest access, and
   * the loop stops on the measured total rather than a step count, so the
   * refreshes it does trigger come out of the budget instead of adding to it. */
  const uint64_t target = ss->snes->cycles + (uint64_t) cycles;
  while(ss->snes->cycles < target) {
    uint64_t left = target - ss->snes->cycles;
    int step = left > 6 ? 6 : (int) left;
    step &= ~1;                       /* snes_runCycle advances two at a time */
    if(step == 0) break;
    dma_handleDma(ss->snes->dma, step);
    snes_runCycles(ss->snes, step);
  }
}

/* ---- routine registry ------------------------------------------------- */
static RecompEntry gRegistry[RECOMP_HOOKS_MAX];
static unsigned gRegistryCount;

void recomp_register(uint32_t entry_addr, const char* name, RecompFn fn) {
  for(unsigned i = 0; i < gRegistryCount; i++) {
    if(gRegistry[i].addr == entry_addr) {
      fprintf(stderr, "recomp_register: %06X registered twice (%s and %s)\n",
              entry_addr, gRegistry[i].name, name);
      exit(2);
    }
  }
  if(gRegistryCount == RECOMP_HOOKS_MAX) {
    fprintf(stderr, "recomp_register: more than %d routines; raise RECOMP_HOOKS_MAX\n",
            RECOMP_HOOKS_MAX);
    exit(2);
  }
  gRegistry[gRegistryCount].addr = entry_addr;
  gRegistry[gRegistryCount].name = name;
  gRegistry[gRegistryCount].fn = fn;
  gRegistryCount++;
}

const RecompEntry* recomp_registry(unsigned* count) {
  *count = gRegistryCount;
  return gRegistry;
}

/* ---- table helper ----------------------------------------------------- */
unsigned recomp_hooks_count(const RecompHook* table) {
  unsigned n = 0;
  while(table[n].fn != NULL) n++;
  return n;
}
