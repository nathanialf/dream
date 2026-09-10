/* Implementation of the recomp hook API over the vendored LakeSnes core.
 *
 * The timed accessors go through snes_cpuRead / snes_cpuWrite / snes_cpuIdle,
 * the very functions the CPU core hands to cpu_init(), so a hook that replays a
 * routine's bus transactions costs the emulator exactly what the routine cost:
 * the same access times, the same DMA/HDMA interleaving, the same open-bus.
 *
 * The second half of the file is the --no-cpu scheduler: the same machine with
 * the instruction fetch taken out, so the C bodies *are* the program. See
 * "Running without the CPUs" in recomp/README.md.
 */
#include <stdint.h>
#include <stdbool.h>

#include <stdio.h>
#include <stdlib.h>

#include "ss_internal.h"
#include "dma.h"
#include "apu.h"

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
static void ss_nocpu_inner_step(SnesState* ss);

void ss_run_until_return(SnesState* ss, uint16_t spBefore) {
  Cpu* c = ss->snes->cpu;
  /* the sp comparison is unsigned and the stack lives near $01FF; a callee that
   * unbalances the stack downward would hang, so bound the run generously. */
  unsigned long guard = 0;
  while(c->sp < spBefore) {
    /* --no-cpu: the callee is a chain of bodies, entered through the registry
     * on this same stack. Everything else about the frame is unchanged. */
    if(ss->nocpu) ss_nocpu_inner_step(ss); else cpu_runOpcode(c);
    if(++guard > 100000000ul) {
      fprintf(stderr, "snes_state: ss_call_* did not return (sp %04X, want %04X)\n", c->sp, spBefore);
      exit(2);
    }
  }
}

bool ss_run_callee(SnesState* ss, uint16_t spBefore) {
  Cpu* c = ss->snes->cpu;
  while(c->sp < spBefore) {
    /* With no CPU a yield suspends this whole stack rather than reporting back,
     * so ss_yield_wanted() answers false and the caller is resumed inside it. */
    if(ss_yield_wanted(ss)) return true;
    if(ss->nocpu) ss_nocpu_inner_step(ss); else cpu_runOpcode(c);
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
  /* the body's own entry address: --no-cpu names it when a pc it hands over has
   * no body of its own */
  ss->entry[ss->depth].pc24 =
    (uint32_t) ((ss->snes->cpu->k << 16) | ss->snes->cpu->pc);
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

static void ss_nocpu_suspend(SnesState* ss);
static bool ss_nocpu_frame_ends_here(SnesState* ss);

void ss_unit_hold(SnesState* ss, bool on) { ss->unitHold = on; }

bool ss_yield_wanted(const SnesState* ss) {
  const Snes* snes = ss->snes;
  /* --unit holds one routine to its end: there is no boundary to hand it back
   * at, because nothing samples this machine until the routine is over and no
   * interrupt is enabled while it runs (harness/ss_internal.h). */
  if(ss->unitHold) return false;
  bool want;
  if(snes->cpu->intWanted) want = true;
  /* Outside a hook there is no routine to hand back, and no snapshot to compare
   * against; only the latched interrupt above is meaningful. */
  else if(ss->depth <= 0) want = false;
  else {
    const SsHookEntry* e = &ss->entry[ss->depth - 1];
    want = snes->frames != e->frames || (snes->inVblank && !e->vblank);
  }
  if(!ss->nocpu) return want;
  /* --no-cpu: there is no ROM to hand the rest of the routine to, so the answer
   * is always "carry on". What the yield point is used for instead is to stop
   * the body exactly where the reference machine stops -- by suspending the
   * stack it runs on, which the scheduler resumes at this same instruction.
   *
   * Two boundaries need the machine back, and they are the two the reference
   * stops at: an interrupt, which the 65816 services between two instructions,
   * and the end of a frame, which is where snes_runFrame() returns and the
   * harness and the app sample the machine. Every other yield the ROM would
   * have been offered is invisible from outside the routine -- the ROM would
   * simply have finished it, which is what the body now does itself -- so
   * suspending for one would cost a context switch and change nothing. */
  SnesState* m = (SnesState*) ss;
  if(m->running != NULL && (snes->cpu->intWanted || ss_nocpu_frame_ends_here(m))) {
    ss_nocpu_suspend(m);
  }
  (void) want;
  return false;
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

/* ---- instructions the hook API cannot otherwise perform ---------------- */
/* xce, LakeSnes cpu.c case 0xfb minus the opcode fetch. cpu_adrImp turns the
 * internal cycle into a read from pc when an interrupt is already latched, so
 * this reproduces both. cpu_setFlags(cpu_getFlags()) is ss_set_p(ss_p()): it is
 * what re-applies emulation mode's own masking after e has moved. */
void ss_xce(SnesState* ss) {
  Cpu* c = ss->snes->cpu;
  cpu_checkIntPublic(c);
  if(c->intWanted) snes_cpuRead(ss->snes, (uint32_t) ((c->k << 16) | c->pc));
  else ss_idle(ss);
  const bool carry = c->c;
  c->c = c->e;
  c->e = carry;
  ss_set_p(ss, ss_p(ss));
}

/* wai, LakeSnes cpu.c case 0xcb minus the opcode fetch: two internal cycles and
 * the park. The machine idles in cpu_runOpcode / ss_nocpu_step from here until
 * an interrupt lifts it. */
void ss_wai(SnesState* ss) {
  ss->snes->cpu->waiting = true;
  ss_idle(ss);
  ss_idle(ss);
}

/* ---- registry lookup --------------------------------------------------- */
/* Entry addresses are written in the disassembly's canonical $C0:0000+offset
 * form; this ROM runs most of its code through the $80/$81 mirror banks, so a
 * pc is folded before it is matched (the same fold harness/main.c performs). */
static uint32_t ss_canon(uint32_t pc24) {
  const uint8_t b = (uint8_t) ((pc24 >> 16) & 0x7f);
  const uint16_t adr = (uint16_t) pc24;
  if(b >= 0x40 || adr >= 0x8000) {
    const uint32_t off = (uint32_t) (((b & 0x3f) << 16) | adr);
    if(off < 0x200000u) return 0xc00000u + off;
  }
  return pc24;
}

const RecompEntry* recomp_find(uint32_t pc24) {
  const uint32_t key = ss_canon(pc24);
  for(unsigned i = 0; i < gRegistryCount; i++) {
    if(gRegistry[i].addr == key) return &gRegistry[i];
  }
  return NULL;
}

/* ---- the --no-cpu scheduler -------------------------------------------- */
#define SS_NOCPU_STACK (512u * 1024u)

void ss_nocpu_enable(SnesState* ss, bool on) {
  ss->nocpu = on;
  ss->nctx = 0;
  ss->running = NULL;
  ss->flPhase = 0;
}

bool ss_nocpu_enabled(const SnesState* ss) { return ss->nocpu; }

/* Give the body chains' stacks back. Only the harness's teardown calls this;
 * a suspended chain is simply dropped, which is safe because a body owns
 * nothing but its own stack frames. */
void ss_nocpu_free(SnesState* ss) {
  for(int i = 0; i < SS_NOCPU_CTX_MAX; i++) {
    coro_free(ss->ctx[i].co);
    ss->ctx[i].co = NULL;
  }
  ss->nctx = 0;
  ss->running = NULL;
  ss->nocpu = false;
}

uint64_t ss_nocpu_dispatches(const SnesState* ss)  { return ss->dispatches; }
uint64_t ss_nocpu_suspensions(const SnesState* ss) { return ss->suspensions; }
uint64_t ss_nocpu_abandoned(const SnesState* ss)   { return ss->abandoned; }
int      ss_nocpu_max_contexts(const SnesState* ss){ return ss->maxCtx; }

/* Would ss_nocpu_run_frame() return at this instruction boundary?
 *
 * It is snes_runFrame()'s own stopping rule written as a test instead of two
 * loops. The first loop runs the machine out of vblank and can never end a
 * frame; the second begins when it does, remembers the frame counter, and ends
 * the frame the moment vblank starts or that counter moves. The phase moves on
 * here rather than in the frame loop because most of these calls come from
 * inside a body, which does not return to the frame loop between two
 * instructions. */
static bool ss_nocpu_frame_ends_here(SnesState* ss) {
  const Snes* snes = ss->snes;
  if(ss->flPhase == 0) {
    if(snes->inVblank) return false;
    ss->flPhase = 1;
    ss->flFrameMark = snes->frames;
    return false;
  }
  return snes->inVblank || snes->frames != ss->flFrameMark;
}

/* A pc the registry does not name. In --no-cpu that is the whole point of the
 * mode: it is either dead code the port never had to convert, or a routine that
 * is still the ROM's. Name the body that handed the pc over, so the gap can be
 * read off the message. */
static void ss_nocpu_no_body(SnesState* ss, uint32_t pc24) {
  const char* from = "(the reset vector)";
  /* the body in flight if there is one, otherwise the last one that ran: a tail
   * jmp hands the pc over after its body has already returned */
  uint32_t fromPc = ss->depth > 0 ? ss->entry[ss->depth - 1].pc24 : ss->lastDispatch;
  if(fromPc != 0) {
    const RecompEntry* e = recomp_find(fromPc);
    from = e != NULL ? e->name : "(an unregistered pc)";
  }
  fprintf(stderr,
          "dream_harness: --no-cpu: no C body at %02X:%04X (canonical %06X)\n"
          "               handed over by %s",
          (unsigned) ((pc24 >> 16) & 0xff), (unsigned) (pc24 & 0xffff),
          ss_canon(pc24), from);
  if(fromPc != 0) fprintf(stderr, " at %06X", ss_canon(fromPc));
  fprintf(stderr, ", %d bod%s in flight\n", ss->depth, ss->depth == 1 ? "y" : "ies");
  exit(3);
}

/* Run the one body that owns this pc. The dispatcher is the harness's own hook
 * callback -- the same one the CPU core calls -- so a body is entered, counted
 * and charged exactly as it is with the CPU running. */
static void ss_nocpu_call_body(SnesState* ss, uint32_t pc24) {
  Cpu* c = ss->snes->cpu;
  ss->dispatches++;
  if(c->hook == NULL || !c->hook(c->hookCtx, c, pc24)) ss_nocpu_no_body(ss, pc24);
  ss->lastDispatch = pc24;   /* set on the way out: the message wants the caller */
}

/* A step taken from inside a body chain (ss_run_callee / ss_run_until_return):
 * the callee's own bodies run on the caller's stack, so a yield anywhere in the
 * chain suspends all of it at once. */
static void ss_nocpu_inner_step(SnesState* ss) {
  Cpu* c = ss->snes->cpu;
  if(cpu_runNonInstruction(c)) return;
  ss_nocpu_call_body(ss, (uint32_t) ((c->k << 16) | c->pc));
}

static void ss_nocpu_trampoline(void* arg) {
  SsNoCpuCtx* ctx = (SsNoCpuCtx*) arg;
  ss_nocpu_call_body(ctx->ss, ctx->startPc);
}

static void ss_nocpu_suspend(SnesState* ss) {
  SsNoCpuCtx* ctx = ss->running;
  Cpu* c = ss->snes->cpu;
  ctx->pc24 = (uint32_t) ((c->k << 16) | c->pc);
  ctx->sp = c->sp;
  ctx->suspended = true;
  ss->suspensions++;
  coro_yield(ctx->co);
  ctx->suspended = false;
}

/* A suspended context an interrupt displaced is reachable again only through an
 * rti that lands on its pc with its stack pointer. Once the stack pointer is
 * back at or above where it stood, the frame that rti would pop is gone and the
 * routine has been abandoned -- which is what this game's NMI handler does two
 * instructions in, with `ldx #$01FF ; txs`. Free the stack and put the hook
 * snapshot depth back where the chain found it. */
static void ss_nocpu_reap(SnesState* ss) {
  const Cpu* c = ss->snes->cpu;
  while(ss->nctx > 0) {
    SsNoCpuCtx* top = &ss->ctx[ss->nctx - 1];
    if(!top->suspended || !top->displaced) break;
    const uint32_t pc24 = (uint32_t) ((c->k << 16) | c->pc);
    if(c->sp < top->sp || pc24 == top->pc24) break;   /* an rti could still land */
    ss->depth = top->depth;
    ss->abandoned++;
    ss->nctx--;
  }
}

static SsNoCpuCtx* ss_nocpu_push(SnesState* ss, uint32_t pc24) {
  if(ss->nctx >= SS_NOCPU_CTX_MAX) {
    fprintf(stderr, "dream_harness: --no-cpu: more than %d suspended body chains\n",
            SS_NOCPU_CTX_MAX);
    exit(2);
  }
  SsNoCpuCtx* ctx = &ss->ctx[ss->nctx++];
  if(ss->nctx > ss->maxCtx) ss->maxCtx = ss->nctx;
  if(ctx->co == NULL) ctx->co = coro_new(SS_NOCPU_STACK);
  ctx->ss = ss;
  ctx->startPc = pc24;
  ctx->pc24 = pc24;
  ctx->sp = ss->snes->cpu->sp;
  ctx->depth = ss->depth;
  ctx->suspended = false;
  ctx->displaced = false;
  return ctx;
}

/* One step of the machine with nothing fetching an instruction.
 *
 * cpu_runNonInstruction() is the core's own reset / stp / wai / interrupt-entry
 * path, factored out of cpu_runOpcode so the frame an interrupt pushes and the
 * cycles it spends are the core's, not a copy of them. Everything else is a pc,
 * and a pc is a body. */
static void ss_nocpu_step(SnesState* ss) {
  Cpu* c = ss->snes->cpu;
  /* The IRQ path is not part of this program and --no-cpu says so rather than
   * assuming it: NMITIMEN's shadow at $34 is only ever $00, $01 or $81, so the
   * h/v timer enables are never set, no `cop` or `brk` is executed, and the
   * `rti` at unused_vec ($C0:A442) that every non-NMI vector points at is never
   * reached. If any of that stops being true the machine would take an
   * interrupt whose frame no body pops, so stop instead of running on. */
  if(c->irqWanted || ss->snes->hIrqEnabled || ss->snes->vIrqEnabled) {
    fprintf(stderr, "dream_harness: --no-cpu: an IRQ was enabled or raised"
                    " (irqWanted %d, hIrq %d, vIrq %d); this ROM never uses one\n",
            c->irqWanted, ss->snes->hIrqEnabled, ss->snes->vIrqEnabled);
    exit(3);
  }
  ss_nocpu_reap(ss);
  const bool takingInt = c->intWanted && !c->resetWanted && !c->stopped && !c->waiting;
  if(cpu_runNonInstruction(c)) {
    if(takingInt && ss->nctx > 0 && ss->ctx[ss->nctx - 1].suspended) {
      ss->ctx[ss->nctx - 1].displaced = true;
    }
    return;
  }
  const uint32_t pc24 = (uint32_t) ((c->k << 16) | c->pc);
  SsNoCpuCtx* ctx = NULL;
  if(ss->nctx > 0) {
    SsNoCpuCtx* top = &ss->ctx[ss->nctx - 1];
    if(top->suspended && top->pc24 == pc24 && top->sp == c->sp) {
      ctx = top;                          /* the routine that stopped here */
    } else if(top->suspended && !top->displaced) {
      /* Nothing but an interrupt can move the machine away from a suspended
       * routine: it stopped between two instructions and the scheduler was the
       * only thing running since. If this fires, a body left the pc somewhere
       * other than where it suspended. */
      fprintf(stderr, "dream_harness: --no-cpu: a body suspended at %06X sp %04X"
                      " but the machine is at %06X sp %04X\n",
              ss_canon(top->pc24), top->sp, ss_canon(pc24), c->sp);
      exit(2);
    }
  }
  SsNoCpuCtx* prev = ss->running;
  if(ctx != NULL) {
    ss->running = ctx;
    coro_resume(ctx->co);
  } else {
    ctx = ss_nocpu_push(ss, pc24);
    ss->running = ctx;
    coro_start(ctx->co, ss_nocpu_trampoline, ctx);
  }
  ss->running = prev;
  if(coro_done(ctx->co)) {
    /* the chain ran to its end: its stack is free for the next dispatch */
    ss->nctx--;
  }
}

/* snes_catchupApu(), which is static in the core: the APU is run for the cycles
 * the CPU has spent since it last was. */
static void ss_nocpu_catchup_apu(Snes* snes) {
  const int catchupCycles = (int) snes->apuCatchupCycles;
  const int ranCycles = apu_runCycles(snes->apu, catchupCycles);
  snes->apuCatchupCycles -= (double) ranCycles;
}

/* snes_runFrame()'s twin: run to the same instruction boundary, catch the APU
 * up at the same point. */
void ss_nocpu_run_frame(SnesState* ss) {
  ss->flPhase = 0;
  while(!ss_nocpu_frame_ends_here(ss)) ss_nocpu_step(ss);
  ss_nocpu_catchup_apu(ss->snes);
}
