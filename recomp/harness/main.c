/* dream_harness — headless lockstep verification harness for the Dream recomp.
 *
 * Runs the original ROM in the vendored LakeSnes core, hashes WRAM/VRAM/CGRAM/OAM
 * after every frame, optionally records a PC coverage set, and can run two
 * instances side by side (one with recomp hooks installed, one without) to prove a
 * recomped routine is behaviourally identical. See recomp/README.md.
 */
/* clock_gettime(CLOCK_MONOTONIC) for the fps figure */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <errno.h>
#include <inttypes.h>

#include "snes.h"
#include "cart.h"
#include "cpu.h"
#include "ppu.h"

#include "coro.h"
#include "ss_internal.h"
#include "sps_internal.h"
#include "xxh64.h"

#define ROM_SIZE   0x200000u   /* 2 MiB */
#define WRAM_SIZE  0x20000u    /* 128 KB */
#define VRAM_BYTES 0x10000u
#define CGRAM_BYTES 0x200u
#define OAM_BYTES  (0x200u + 0x20u)
#define ARAM_BYTES 0x10000u    /* the SPC700's 64 KB */
#define DSPREG_BYTES 0x80u     /* the DSP's 128 registers */
#define SPCREG_BYTES 8u        /* A X Y SP PSW PC(lo,hi) */
#define REGION_COUNT 7

#define COV_BYTES  (1u << 21)  /* 2^24 PCs / 8 */

/* ---- input scripts ---------------------------------------------------- */

/* SNES button bit order used by snes_setButtonState() */
enum {
  BTN_B = 0, BTN_Y, BTN_SELECT, BTN_START,
  BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT,
  BTN_A, BTN_X, BTN_L, BTN_R
};

static const struct { const char* name; int bit; } kButtons[] = {
  {"B", BTN_B}, {"Y", BTN_Y}, {"Select", BTN_SELECT}, {"Start", BTN_START},
  {"Up", BTN_UP}, {"Down", BTN_DOWN}, {"Left", BTN_LEFT}, {"Right", BTN_RIGHT},
  {"A", BTN_A}, {"X", BTN_X}, {"L", BTN_L}, {"R", BTN_R},
};

typedef struct {
  int frame;
  uint16_t state;
  uint16_t state2;
} InputEvent;

typedef struct {
  InputEvent* ev;
  int count;
} InputScript;

static int str_ieq(const char* a, const char* b) {
  while(*a && *b) {
    int ca = *a >= 'A' && *a <= 'Z' ? *a + 32 : *a;
    int cb = *b >= 'A' && *b <= 'Z' ? *b + 32 : *b;
    if(ca != cb) return 0;
    a++; b++;
  }
  return *a == 0 && *b == 0;
}

/* Parse a "Button+Button+..." button set (one player's column of an input
 * line). "-", "none" and an empty list mean no buttons. */
static bool parse_button_set(char* p, const char* path, int lineno, uint16_t* outState) {
  uint16_t state = 0;
  while(*p != 0) {
    while(*p == ' ' || *p == '\t' || *p == '+' || *p == '\n' || *p == '\r') p++;
    if(*p == 0) break;
    char tok[32];
    size_t n = 0;
    while(*p != 0 && *p != ' ' && *p != '\t' && *p != '+' && *p != '\n' && *p != '\r') {
      if(n + 1 < sizeof(tok)) tok[n++] = *p;
      p++;
    }
    tok[n] = 0;
    if(str_ieq(tok, "-") || str_ieq(tok, "none")) continue;
    bool found = false;
    for(size_t i = 0; i < sizeof(kButtons) / sizeof(kButtons[0]); i++) {
      if(str_ieq(tok, kButtons[i].name)) {
        state |= (uint16_t) (1u << kButtons[i].bit);
        found = true;
        break;
      }
    }
    if(!found) {
      fprintf(stderr, "dream_harness: %s:%d: unknown button '%s'\n", path, lineno, tok);
      return false;
    }
  }
  *outState = state;
  return true;
}

/* Parse "frame Buttons" or "frame Buttons | Buttons2" lines; a button set is
 * held until a later line changes it. "-", "none" and an empty button list
 * mean no buttons. The "| Buttons2" column is optional and addresses player
 * 2's controller; when a line omits it, player 2's held set carries forward
 * unchanged from whatever an earlier line last gave it (default none). */
static bool input_load(InputScript* s, const char* path) {
  FILE* f = fopen(path, "r");
  if(f == NULL) {
    fprintf(stderr, "dream_harness: cannot open input script %s: %s\n", path, strerror(errno));
    return false;
  }
  int cap = 16;
  s->ev = malloc((size_t) cap * sizeof(InputEvent));
  s->count = 0;
  uint16_t lastState2 = 0;
  char line[512];
  int lineno = 0;
  while(fgets(line, sizeof(line), f) != NULL) {
    lineno++;
    char* p = line;
    char* hash = strchr(p, '#');
    if(hash != NULL) *hash = 0;
    while(*p == ' ' || *p == '\t') p++;
    if(*p == 0 || *p == '\n' || *p == '\r') continue;
    char* endp = NULL;
    long frame = strtol(p, &endp, 10);
    if(endp == p || frame < 0) {
      fprintf(stderr, "dream_harness: %s:%d: expected a frame number\n", path, lineno);
      fclose(f);
      return false;
    }
    p = endp;
    char* bar = strchr(p, '|');
    if(bar != NULL) *bar = 0;
    uint16_t state = 0, state2 = lastState2;
    if(!parse_button_set(p, path, lineno, &state)) { fclose(f); return false; }
    if(bar != NULL) {
      if(!parse_button_set(bar + 1, path, lineno, &state2)) { fclose(f); return false; }
      lastState2 = state2;
    }
    if(s->count == cap) {
      cap *= 2;
      s->ev = realloc(s->ev, (size_t) cap * sizeof(InputEvent));
    }
    s->ev[s->count].frame = (int) frame;
    s->ev[s->count].state = state;
    s->ev[s->count].state2 = state2;
    s->count++;
  }
  fclose(f);
  return true;
}

static void input_state_at(const InputScript* s, int frame, uint16_t* state, uint16_t* state2) {
  *state = 0;
  *state2 = 0;
  for(int i = 0; i < s->count && s->ev[i].frame <= frame; i++) {
    *state = s->ev[i].state;
    *state2 = s->ev[i].state2;
  }
}

/* ---- machine ---------------------------------------------------------- */

/* One installed routine. Exactly one of hookFn (a declining hook from
 * recomp_hooks[]) and fn (a registry routine from recomp/src) is set. */
typedef struct {
  uint32_t addr;
  const char* name;
  RecompHookFn hookFn;
  RecompFn fn;
  int cycleCost;             /* master cycles to charge, 0 = charge nothing */
} Installed;

/* One installed SPC700 routine. There is no declining form and no cycle charge:
 * an SPC body models its own instruction stream, and one APU cycle is one read,
 * write or idle, so a modelled body costs exactly what the routine cost. */
typedef struct {
  uint16_t addr;
  const char* name;
  SpcRecompFn fn;
} SpcInstalled;

#define PROF_DEPTH 64
#define PROF_STACK 8         /* bytes of its caller's stack a frame remembers */

/* One in-flight routine while --profile is running. */
typedef struct {
  unsigned idx;              /* index into Machine.hooks */
  uint16_t sp;               /* stack pointer at the routine's first instruction */
  uint64_t cycles;           /* master cycle count at that instant */
  uint8_t above[PROF_STACK]; /* [sp+1 .. sp+PROF_STACK] as they stood then */
  bool aboveKnown;           /* false when the frame is not in the low WRAM mirror */
} ProfFrame;

typedef struct {
  Snes* snes;
  SnesState ss;
  SpcState sps;
  Installed* hooks;          /* NULL when nothing is installed */
  unsigned hookCount;
  bool hooksActive;          /* false for the profile pass and --hooks off */
  SpcInstalled* spcHooks;    /* NULL when nothing is installed */
  unsigned spcHookCount;
  bool spcHooksActive;       /* --spc-hooks */
  uint8_t* cov;              /* NULL when not tracing */
  uint64_t covNonRom;        /* PCs seen outside the cart map */
  uint64_t instructions;
  bool nocpu;                /* --no-cpu: no instruction is ever fetched */
  uint64_t emulated;         /* 65816 instructions the core actually executed */
  uint64_t emulatedSpc;      /* SPC700 instructions the core actually executed */
  /* profiling */
  bool profiling;            /* reference pass: measure the ROM's cost */
  bool measureSpend;         /* candidate pass: measure what the hook itself costs */
  ProfFrame profStack[PROF_DEPTH];
  int profDepth;
  uint64_t* profCycles;      /* per hook entry */
  uint64_t* profCalls;
  uint64_t* profMin;
  uint64_t* profMax;
  uint8_t* profNoReturn;     /* candidate pass: the hook handed control on instead */
  uint8_t* profRomTail;      /* reference pass: the ROM left through a tail jmp */
  uint16_t profFrameSp;      /* the lowest sp any open frame stands at */
  unsigned long profDropped; /* entries not measured: the frame stack was full */
  unsigned long profOpen;    /* frames still open when the run ended */
  /* serialisation scratch */
  uint8_t vram[VRAM_BYTES];
  uint8_t cgram[CGRAM_BYTES];
  uint8_t oam[OAM_BYTES];
  uint8_t aram[ARAM_BYTES];
  uint8_t dspreg[DSPREG_BYTES];
  uint8_t spcreg[SPCREG_BYTES];
} Machine;

/* Canonical ROM address for a 24-bit PC, in the disassembly's $C0:0000+offset
 * form. Returns false for PCs that are not in the cart map (WRAM, registers). */
static bool canon_rom_addr(uint32_t pc24, uint32_t* out) {
  uint8_t bank = (uint8_t) (pc24 >> 16);
  uint16_t adr = (uint16_t) pc24;
  uint8_t b = bank & 0x7f;
  if(b >= 0x40 || adr >= 0x8000) {
    uint32_t off = (uint32_t) (((b & 0x3f) << 16) | adr);
    if(off >= ROM_SIZE) return false;
    *out = 0xc00000u + off;
    return true;
  }
  return false;
}

static int machine_find_hook(const Machine* m, uint32_t pc24) {
  /* Hook addresses are written in the disassembly's canonical $C0:0000+offset
   * form; this ROM runs most of its code through the $80/$81 mirror banks, so
   * fold the PC before matching. */
  uint32_t key = pc24;
  if(!canon_rom_addr(pc24, &key)) key = pc24;
  for(unsigned i = 0; i < m->hookCount; i++) {
    if(m->hooks[i].addr == key) return (int) i;
  }
  return -1;
}

/* --profile, the reference pass: measure what the ROM's own code spends on a
 * routine, from its first instruction to the moment control leaves it. The
 * cycles a nested callee costs are billed to the outer routine too, which is
 * what a hook replacing the whole call tree needs to be charged.
 *
 * The stack pointer alone cannot say when control left. Three shapes in this ROM
 * unwind their own frame and keep running:
 *
 *   entity_animate_only and the three entity_spawn_transform routines end
 *   `jsl anim_update ; pla ; rts`, where the 16-bit pla drops the return address
 *   entity_update_tick's jsr pushed so the rts leaves the whole tick: sp is
 *   above the frame at the pla, one instruction before the routine ends.
 *
 *   anim_rate_store's plb pops the byte the caller's `pea $8080 ; plb` left on
 *   the stack, three instructions before the `jsl anim_update` that is nearly
 *   all of what the routine costs.
 *
 *   every OAM emitter's "table is full" exit is `pla ; jmp loc_C0A6CD`, a jump
 *   into entity_build_oam_frame's `pea $8080 ; plb ; plb ; rtl` tail: sp is above
 *   the frame at the jmp, and the emitter's own last instruction is that jmp.
 *
 * Closing on sp stopped the clock at the pla, at the plb and at the jmp. It cost
 * the first shape its last instruction, and it cost anim_rate_store the entire
 * jsl: 69 master cycles were measured for a routine that costs 1709. The C body
 * models every one of those instructions, so the routine looked cheaper than the
 * hook that replaces it -- the exact opposite of the reading --profile exists to
 * give.
 *
 * So a frame closes on the *pc* instead: on the instruction the return that
 * moved the stack pointer to where it now stands would land on, read out of the
 * bytes that sat above the frame when the routine started. rts pops a word and
 * adds one, rtl pops a word and a bank and adds one, rti pops flags, a word and
 * a bank and adds nothing. sp is still the guard: a frame that is still on the
 * stack cannot have returned.
 *
 * A routine that leaves through a tail jmp reaches no return target of its own,
 * so its frame is closed when the frame it was entered from returns, and flagged
 * (profRomTail) so its line says the two figures cover different work. */
static bool prof_frame_returned(const ProfFrame* f, uint16_t sp, uint32_t pc24) {
  if(sp <= f->sp) return false;                    /* the frame is still there */
  unsigned d = (unsigned) (uint16_t) (sp - f->sp); /* bytes popped past it */
  if(!f->aboveKnown || d > PROF_STACK) return true; /* long gone: the sp rule */
  const uint8_t* s = f->above;                     /* s[i] is the byte at f->sp+1+i */
  uint16_t pc = (uint16_t) pc24;
  uint8_t pb = (uint8_t) (pc24 >> 16);
  /* rts: the word it popped sat at [sp-1], [sp] */
  if(d >= 2 && pc == (uint16_t) ((s[d - 2] | (s[d - 1] << 8)) + 1)) return true;
  /* rtl: the word at [sp-2], [sp-1] and the bank at [sp] */
  if(d >= 3 && pb == s[d - 1] &&
     pc == (uint16_t) ((s[d - 3] | (s[d - 2] << 8)) + 1)) return true;
  /* rti: flags at [sp-3], the word at [sp-2], [sp-1], the bank at [sp], no +1 */
  if(d >= 4 && pb == s[d - 1] &&
     pc == (uint16_t) (s[d - 3] | (s[d - 2] << 8))) return true;
  return false;
}

/* The frame's own stack bytes, for the test above. The 65816 stack lives in
 * bank 0; everything this game does is in page 1, well inside the WRAM mirror,
 * and reading it out of snes->ram avoids the side effects a register read
 * through snes_read() would have. A frame that is not entirely inside the mirror
 * falls back to the plain sp rule. */
static bool prof_capture_stack(const Machine* m, uint16_t sp, uint8_t* out) {
  if((uint32_t) sp + PROF_STACK >= 0x2000u) return false;
  memcpy(out, m->snes->ram + sp + 1, PROF_STACK);
  return true;
}

static void prof_bill(Machine* m, const ProfFrame* f, bool viaTail) {
  uint64_t d = m->snes->cycles - f->cycles;
  if(getenv("DREAM_PROF_TRACE") != NULL)
    fprintf(stderr, "rom  %-28s start=%" PRIu64 " cost=%" PRIu64 "%s\n",
            m->hooks[f->idx].name, f->cycles, d, viaTail ? " (tail)" : "");
  m->profCycles[f->idx] += d;
  if(m->profCalls[f->idx] == 0 || d < m->profMin[f->idx]) m->profMin[f->idx] = d;
  if(d > m->profMax[f->idx]) m->profMax[f->idx] = d;
  m->profCalls[f->idx]++;
  if(viaTail) m->profRomTail[f->idx] = 1;
}

/* Close the outermost frame that has returned, and with it every frame entered
 * inside it: a routine cannot outlive the one that called it. */
static void prof_close(Machine* m, uint16_t sp, uint32_t pc24) {
  /* A frame can only have returned once the stack pointer is above it, so while
   * it is at or below the lowest open frame's nothing can have, which is the
   * case at almost every instruction. */
  if(m->profDepth == 0 || sp <= m->profFrameSp) return;
  int keep = m->profDepth;
  for(int i = 0; i < m->profDepth; i++) {
    if(prof_frame_returned(&m->profStack[i], sp, pc24)) { keep = i; break; }
  }
  for(int i = m->profDepth - 1; i >= keep; i--) {
    /* Several frames can end on the same instruction: `pla ; rts` returns two
     * levels at once, so the routine and the one that called it both close on
     * the same return. Only a frame that reached no return target of its own
     * left through a tail jmp. */
    prof_bill(m, &m->profStack[i], !prof_frame_returned(&m->profStack[i], sp, pc24));
  }
  m->profDepth = keep;
  m->profFrameSp = 0xffff;
  for(int i = 0; i < m->profDepth; i++) {
    if(m->profStack[i].sp < m->profFrameSp) m->profFrameSp = m->profStack[i].sp;
  }
}

/* The hook declined: the core is about to fetch and execute the instruction at
 * this pc. Counting the declines counts exactly the instructions the emulated
 * 65816 runs, which is the figure --no-cpu exists to drive to zero. */
static bool harness_hook_declined(Machine* m) {
  m->emulated++;
  return false;
}

static bool harness_hook(void* ctx, Cpu* cpu, uint32_t pc24) {
  Machine* m = (Machine*) ctx;
  m->instructions++;
  if(m->cov != NULL) m->cov[pc24 >> 3] |= (uint8_t) (1u << (pc24 & 7));
  if(m->hooks == NULL) return harness_hook_declined(m);

  if(m->profiling) {
    prof_close(m, cpu->sp, pc24);
    int idx = machine_find_hook(m, pc24);
    if(idx >= 0) {
      if(m->profDepth < PROF_DEPTH) {
        ProfFrame* f = &m->profStack[m->profDepth++];
        f->idx = (unsigned) idx;
        f->sp = cpu->sp;
        f->cycles = m->snes->cycles;
        f->aboveKnown = prof_capture_stack(m, cpu->sp, f->above);
        if(m->profDepth == 1 || f->sp < m->profFrameSp) m->profFrameSp = f->sp;
      } else {
        m->profDropped++;               /* reported, never silently dropped */
      }
    }
    return harness_hook_declined(m);
  }

  if(!m->hooksActive) return harness_hook_declined(m);
  int idx = machine_find_hook(m, pc24);
  if(idx < 0) return harness_hook_declined(m);
  const Installed* h = &m->hooks[idx];
  uint64_t before = m->snes->cycles;
  uint16_t spBefore = cpu->sp;
  /* One snapshot per invocation, not per machine: this hook may be running
   * inside another hook's ss_run_callee, and that one's snapshot has to be
   * intact when its callee returns (see harness/ss_internal.h). */
  ss_enter_hook(&m->ss);
  if(h->fn != NULL) {
    h->fn(&m->ss);
  } else if(!h->hookFn(&m->ss)) {
    ss_leave_hook(&m->ss);
    return harness_hook_declined(m);    /* the hook declined */
  }
  ss_leave_hook(&m->ss);
  recomp_hook_hits[idx]++;
  if(m->measureSpend) {
    uint64_t d = m->snes->cycles - before;
    if(getenv("DREAM_PROF_TRACE") != NULL)
      fprintf(stderr, "hook %-28s start=%" PRIu64 " cost=%" PRIu64 "\n",
              m->hooks[idx].name, before, d);
    m->profCycles[idx] += d;
    if(m->profCalls[idx] == 0 || d < m->profMin[idx]) m->profMin[idx] = d;
    if(d > m->profMax[idx]) m->profMax[idx] = d;
    m->profCalls[idx]++;
    if(cpu->sp <= spBefore) m->profNoReturn[idx] = 1;
  }
  /* A hook that computes in C costs the emulator far less than the routine it
   * replaces, which would move every later NMI. config/recomp_cycles.txt holds
   * the difference: the ROM's mean cost per call minus the mean the hook pays
   * on its own for timed register access, DMA transfers and callees that are
   * still emulated (see --profile). So it is added, not subtracted from.
   *
   * Only a hook that actually returned is charged. A routine whose last
   * instruction is a tail jmp leaves the stack where it found it and hands
   * control to a routine that has not run yet, so the measured figure covers
   * work the emulator is still about to do; charging it would count that work
   * twice. The jmp itself costs a few cycles, which is what is dropped instead. */
  if(h->cycleCost > 0 && cpu->sp > spBefore) {
    ss_consume_cycles(&m->ss, h->cycleCost);
  }
  return true;
}

/* The SPC700 dispatcher, the twin of harness_hook above.
 *
 * It needs no entry snapshot and no snapshot stack. The 65816's yield condition
 * is a level -- "vblank has started" stays true for the rest of the frame -- so
 * a hook has to remember what the machine looked like when it began. The SPC's
 * is a threshold on a monotonically increasing cycle count (apu->sliceEnd), and
 * every dispatch happens strictly below it, so the same question has the same
 * right answer for every hook in flight. The depth is tracked for the report. */
static bool harness_spc_hook(void* ctx, Spc* spc, uint16_t pc) {
  Machine* m = (Machine*) ctx;
  (void) spc;
  if(m->spcHooksActive && m->spcHooks != NULL) {
    for(unsigned i = 0; i < m->spcHookCount; i++) {
      if(m->spcHooks[i].addr != pc) continue;
      sps_enter_hook(&m->sps);
      m->spcHooks[i].fn(&m->sps);
      sps_leave_hook(&m->sps);
      recomp_spc_hook_hits[i]++;
      return true;
    }
  }
  m->emulatedSpc++;                     /* the core will fetch this one */
  return false;
}

/* Force the cart to HiROM, 2 MiB, no SRAM.
 *
 * snes_loadRom() scores the header candidates at $7fc0/$ffc0/$40ffc0. This ROM's
 * internal header at $ffc0 is overwritten by tilemap data (06 37 00 37 ...) and
 * carries no checksum, so the scoring is meaningless for it. We skip it entirely
 * and do by hand what snes_loadRom() does after the scoring: cart_load() with
 * type 2 (HiROM), the raw 2 MiB (already a power of two, so no mirroring pass is
 * needed), ramSize 0, then a hard reset and NTSC timing. No emulator source
 * change is involved. */
static bool machine_load_rom(Machine* m, const uint8_t* rom, size_t len) {
  if(len != ROM_SIZE) {
    fprintf(stderr, "dream_harness: expected a %u byte ROM, got %zu\n", ROM_SIZE, len);
    return false;
  }
  cart_load(m->snes->cart, 2 /* HiROM */, (uint8_t*) rom, (int) len, 0 /* no SRAM */);
  snes_reset(m->snes, true);
  m->snes->palTiming = false;
  return true;
}

static void machine_init(Machine* m, const uint8_t* rom, size_t len,
                         Installed* hooks, unsigned hookCount, bool active, bool trace) {
  memset(m, 0, sizeof(*m));
  m->snes = snes_init();
  m->ss.snes = m->snes;
  m->hooks = hooks;
  m->hookCount = hooks != NULL ? hookCount : 0;
  m->hooksActive = active;
  m->cov = trace ? calloc(COV_BYTES, 1) : NULL;
  if(!machine_load_rom(m, rom, len)) exit(2);
  m->snes->cpu->hook = harness_hook;
  m->snes->cpu->hookCtx = m;
  m->sps.apu = m->snes->apu;
  m->sps.spc = m->snes->apu->spc;
  m->snes->apu->spc->hook = harness_spc_hook;
  m->snes->apu->spc->hookCtx = m;
}

static void machine_install_spc(Machine* m, SpcInstalled* table, unsigned count, bool active) {
  m->spcHooks = table;
  m->spcHookCount = table != NULL ? count : 0;
  m->spcHooksActive = active;
}

static void machine_free(Machine* m) {
  if(m->nocpu) { ss_nocpu_free(&m->ss); sps_nocpu_free(&m->sps); }
  free(m->cov);
  free(m->profCycles);
  free(m->profCalls);
  free(m->profMin);
  free(m->profMax);
  free(m->profNoReturn);
  free(m->profRomTail);
  snes_free(m->snes);
}

/* One frame. In --no-cpu the scheduler stands in for snes_runFrame(): same
 * stopping point, same APU catch-up at the end (see recomp/README.md, "Running
 * without the CPUs"). */
static void machine_run_frame(Machine* m) {
  if(m->nocpu) ss_nocpu_run_frame(&m->ss);
  else snes_runFrame(m->snes);
}

static void machine_set_input(Machine* m, uint16_t state, uint16_t state2) {
  m->snes->input1->currentState = state;
  m->snes->input2->currentState = state2;
}

static void machine_snapshot(Machine* m) {
  const Ppu* ppu = m->snes->ppu;
  for(unsigned i = 0; i < 0x8000; i++) {
    m->vram[i * 2] = (uint8_t) ppu->vram[i];
    m->vram[i * 2 + 1] = (uint8_t) (ppu->vram[i] >> 8);
  }
  for(unsigned i = 0; i < 0x100; i++) {
    m->cgram[i * 2] = (uint8_t) ppu->cgram[i];
    m->cgram[i * 2 + 1] = (uint8_t) (ppu->cgram[i] >> 8);
  }
  for(unsigned i = 0; i < 0x100; i++) {
    m->oam[i * 2] = (uint8_t) ppu->oam[i];
    m->oam[i * 2 + 1] = (uint8_t) (ppu->oam[i] >> 8);
  }
  memcpy(m->oam + 0x200, ppu->highOam, 0x20);
  /* The APU's own state, for --lockstep: 64 KB of ARAM, the DSP's 128 registers
   * (dsp->ram is the register file dsp_read() answers from) and the six SPC700
   * registers. Everything the sound driver computes lands in one of the three. */
  const Apu* apu = m->snes->apu;
  memcpy(m->aram, apu->ram, ARAM_BYTES);
  memcpy(m->dspreg, apu->dsp->ram, DSPREG_BYTES);
  const Spc* spc = apu->spc;
  m->spcreg[0] = spc->a;
  m->spcreg[1] = spc->x;
  m->spcreg[2] = spc->y;
  m->spcreg[3] = spc->sp;
  m->spcreg[4] = (uint8_t) ((spc->n << 7) | (spc->v << 6) | (spc->p << 5) | (spc->b << 4) |
                            (spc->h << 3) | (spc->i << 2) | (spc->z << 1) | spc->c);
  m->spcreg[5] = (uint8_t) spc->pc;
  m->spcreg[6] = (uint8_t) (spc->pc >> 8);
  m->spcreg[7] = (uint8_t) (apu->dspAdr);
}

typedef struct {
  const char* name;
  const uint8_t* data;
  size_t len;
} Region;

static void machine_regions(Machine* m, Region out[REGION_COUNT]) {
  out[0] = (Region) { "wram",   m->snes->ram, WRAM_SIZE };
  out[1] = (Region) { "vram",   m->vram,      VRAM_BYTES };
  out[2] = (Region) { "cgram",  m->cgram,     CGRAM_BYTES };
  out[3] = (Region) { "oam",    m->oam,       OAM_BYTES };
  out[4] = (Region) { "aram",   m->aram,      ARAM_BYTES };
  out[5] = (Region) { "dspreg", m->dspreg,    DSPREG_BYTES };
  out[6] = (Region) { "spcreg", m->spcreg,    SPCREG_BYTES };
}

/* ---- trace output ----------------------------------------------------- */

static bool trace_write(Machine* m, const char* path) {
  FILE* f = fopen(path, "w");
  if(f == NULL) {
    fprintf(stderr, "dream_harness: cannot write %s: %s\n", path, strerror(errno));
    return false;
  }
  /* fold mirror banks onto the canonical $C0:0000+offset form before sorting */
  uint8_t* rom = calloc(ROM_SIZE / 8, 1);
  uint64_t nonRom = 0;
  for(uint32_t pc = 0; pc < (1u << 24); pc++) {
    if((m->cov[pc >> 3] & (1u << (pc & 7))) == 0) continue;
    uint32_t canon = 0;
    if(canon_rom_addr(pc, &canon)) {
      uint32_t off = canon - 0xc00000u;
      rom[off >> 3] |= (uint8_t) (1u << (off & 7));
    } else {
      nonRom++;
    }
  }
  uint64_t n = 0;
  for(uint32_t off = 0; off < ROM_SIZE; off++) {
    if(rom[off >> 3] & (1u << (off & 7))) n++;
  }
  fprintf(f, "# dream_harness PC coverage: %" PRIu64 " distinct ROM addresses\n", (uint64_t) n);
  fprintf(f, "# %" PRIu64 " executed PCs fell outside the cart map (WRAM/registers)\n", nonRom);
  for(uint32_t off = 0; off < ROM_SIZE; off++) {
    if(rom[off >> 3] & (1u << (off & 7))) fprintf(f, "%06X\n", 0xc00000u + off);
  }
  fclose(f);
  free(rom);
  m->covNonRom = nonRom;
  printf("trace: %" PRIu64 " distinct ROM PCs, %" PRIu64 " non-ROM PCs -> %s\n", n, nonRom, path);
  return true;
}

/* ---- main ------------------------------------------------------------- */

static void usage(void) {
  printf(
    "usage: dream_harness [options]\n"
    "  --rom FILE         ROM image (default baserom/DREAM.sfc)\n"
    "  --frames N         frames to run (default 600)\n"
    "  --input FILE       input script: lines 'frame Button+Button...'\n"
    "  --trace FILE       write the executed-PC coverage set (sorted C0XXXX)\n"
    "  --dump-wram DIR    write DIR/wram_NNNNNN.bin after every frame\n"
    "  --hooks on|off     install the recomp 65816 routines (default off)\n"
    "  --spc-hooks on|off install the recomp SPC700 routines (default off)\n"
    "  --hook-table all|demo|empty  which table to install (default all)\n"
    "                       all   every routine registered by recomp/src\n"
    "                       demo  the single worked example, clear_sprite_table\n"
    "                       empty nothing\n"
    "  --cycles FILE      per-routine cycle costs (default config/recomp_cycles.txt\n"
    "                     when it exists; 'none' disables the charge)\n"
    "  --profile FILE     measure mean cycles per call for every registered routine\n"
    "                     with hooks off and write FILE; implies --hooks off\n"
    "  --only A,B,C       install only these routines, by name (bisecting a failure)\n"
    "  --lockstep         run hooks-off vs hooks-on and compare every frame\n"
    "  --no-cpu           run the C bodies with neither CPU core executing an\n"
    "                     instruction: the registry resolves every pc hand-off and\n"
    "                     a pc with no body is a fatal error. Implies --hooks on\n"
    "                     --spc-hooks on; with --lockstep it is the candidate\n"
    "  --test-coro        self-test: the coroutine backend the --no-cpu scheduler\n"
    "                     runs bodies on (needs no ROM; exits 0 on pass, 1 on fail)\n"
    "  --test-nesting     self-test: a hook entered inside another hook's callee\n"
    "                     keeps its own yield snapshot (exits 0 on pass, 1 on fail)\n"
    "  --test-spc-timing  self-test: sps_op_cycles() against the SPC700 core's own\n"
    "                     opcode timing (exits 0 on pass, 1 on fail)\n"
    "  --unit FILE        routine-level lockstep over a seed spec, normally\n"
    "                     config/recomp_units.txt: boot to a script's frame, seed\n"
    "                     the registers and a few cells, then run the ROM's routine\n"
    "                     and the C body from the same state and compare\n"
    "  --unit-only A,B    restrict --unit to these routines, by name\n"
    "  --quiet            suppress the per-frame lines\n"
    "  --help\n"
    "buttons: A B X Y L R Start Select Up Down Left Right\n");
}

static uint8_t* read_file(const char* path, size_t* lenOut) {
  FILE* f = fopen(path, "rb");
  if(f == NULL) {
    fprintf(stderr, "dream_harness: cannot open %s: %s\n", path, strerror(errno));
    return NULL;
  }
  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  if(len <= 0) { fclose(f); return NULL; }
  uint8_t* buf = malloc((size_t) len);
  if(fread(buf, 1, (size_t) len, f) != (size_t) len) {
    fclose(f);
    free(buf);
    return NULL;
  }
  fclose(f);
  *lenOut = (size_t) len;
  return buf;
}

/* ---- installed table -------------------------------------------------- */

/* Build the table --hook-table names. "all" is the registry every recomp/src
 * file filled in from its own constructor, so adding a routine never means
 * editing this file. */
/* --only: keep just the named routines, for bisecting a lockstep failure. */
static bool name_in_list(const char* list, const char* name) {
  if(list == NULL) return true;
  size_t len = strlen(name);
  const char* p = list;
  while(*p != 0) {
    const char* q = strchr(p, ',');
    size_t n = q != NULL ? (size_t) (q - p) : strlen(p);
    if(n == len && strncmp(p, name, len) == 0) return true;
    if(q == NULL) break;
    p = q + 1;
  }
  return false;
}

static Installed* build_table(const char* which, unsigned* countOut) {
  unsigned n = 0;
  const RecompEntry* reg = recomp_registry(&n);
  Installed* out;
  if(strcmp(which, "all") == 0) {
    out = calloc(n ? n : 1, sizeof(Installed));
    for(unsigned i = 0; i < n; i++) {
      out[i].addr = reg[i].addr;
      out[i].name = reg[i].name;
      out[i].fn = reg[i].fn;
    }
    *countOut = n;
    return out;
  }
  const RecompHook* t = strcmp(which, "demo") == 0 ? recomp_hooks : recomp_hooks_empty;
  unsigned c = recomp_hooks_count(t);
  out = calloc(c ? c : 1, sizeof(Installed));
  for(unsigned i = 0; i < c; i++) {
    out[i].addr = t[i].addr;
    out[i].name = t[i].name;
    out[i].hookFn = t[i].fn;
  }
  *countOut = c;
  return out;
}

/* The SPC700 table --spc-hooks installs: everything recomp/spc registered from
 * its own constructor, so adding a routine edits no central table. */
static SpcInstalled* build_spc_table(unsigned* countOut) {
  unsigned n = 0;
  const SpcRecompEntry* reg = recomp_spc_registry(&n);
  SpcInstalled* out = calloc(n ? n : 1, sizeof(SpcInstalled));
  for(unsigned i = 0; i < n; i++) {
    out[i].addr = reg[i].addr;
    out[i].name = reg[i].name;
    out[i].fn = reg[i].fn;
  }
  *countOut = n;
  return out;
}

/* config/recomp_cycles.txt: "C0XXXX name mean_cycles calls", written by
 * --profile. Unknown addresses are ignored, missing routines cost nothing. */
static bool load_cycles(Installed* hooks, unsigned count, const char* path, bool required) {
  FILE* f = fopen(path, "r");
  if(f == NULL) {
    if(required) {
      fprintf(stderr, "dream_harness: cannot open %s: %s\n", path, strerror(errno));
      return false;
    }
    return true;
  }
  char line[512];
  while(fgets(line, sizeof(line), f) != NULL) {
    char* p = line;
    char* hash = strchr(p, '#');
    if(hash != NULL) *hash = 0;
    if(*p == ';') continue;
    uint32_t addr = 0;
    char name[128];
    long cycles = 0, calls = 0;
    if(sscanf(p, "%x %127s %ld %ld", &addr, name, &cycles, &calls) < 3) continue;
    for(unsigned i = 0; i < count; i++) {
      if(hooks[i].addr == addr) hooks[i].cycleCost = (int) cycles;
    }
  }
  fclose(f);
  return true;
}

/* Write the per-routine cycle charge.
 *
 * Two passes have run side by side over the same input: `rom` with hooks off,
 * measuring what the ROM's own code spends between a routine's first
 * instruction and the return that pops its frame (nested callees and any
 * interrupt serviced inside included), and `hook` with hooks on, measuring what
 * the C body spends on its own for timed register access, DMA transfers and
 * callees that are still emulated.
 *
 * The charge is the difference. Taking it this way is what makes a routine whose
 * cost is dominated by something the hook *does* pay for come out right: a DMA
 * upload's transfer time is in both means and cancels, leaving only the
 * instruction overhead the C body skipped, and that overhead is the part that is
 * the same on every call. Charging the ROM's total instead would bill the
 * transfer twice, and would bill the mean transfer to calls that moved a
 * hundredth of the data. */
static bool profile_write(Machine* rom, Machine* hook, const char* path) {
  FILE* f = fopen(path, "w");
  if(f == NULL) {
    fprintf(stderr, "dream_harness: cannot write %s: %s\n", path, strerror(errno));
    return false;
  }
  fprintf(f, "; Master cycles a hooked routine is charged, so that replacing it with C\n"
             "; does not move later NMIs. Written by dream_harness --profile: the ROM's\n"
             "; mean cost per call, minus the mean the C body already pays for timed\n"
             "; register access, DMA transfers and not-yet-converted callees.\n"
             ";\n"
             "; A hook that does not always return -- one ending in a tail jmp, or one\n"
             "; that handed the rest of the routine back to the ROM at a frame boundary --\n"
             "; still gets a line, but is never charged: the two figures then cover\n"
             "; different work. Same for an address the ROM only reaches by falling\n"
             "; through from the routine above it, which is never entered as a hook.\n"
             "; addr name charge calls rom_mean hook_mean\n");
  unsigned written = 0;
  for(unsigned i = 0; i < rom->hookCount; i++) {
    if(rom->profCalls[i] == 0) continue;
    uint64_t romMean = rom->profCycles[i] / rom->profCalls[i];
    uint64_t hookMean = hook->profCalls[i] ? hook->profCycles[i] / hook->profCalls[i] : 0;
    /* hookMean is what the C body spent on its own, measured *before* the charge
     * was added, so this is an absolute figure and not a correction: running
     * --profile again on its own output reproduces it. */
    int64_t charge = (int64_t) romMean - (int64_t) hookMean;
    if(charge < 0) charge = 0;
    const char* note = "";
    if(hook->profCalls[i] == 0) {
      /* the ROM reached this address by falling through from the routine above
       * it, which the hook installed there absorbs; nothing to calibrate */
      charge = 0;
      note = " (hook not reached: fall-through entry)";
    } else if(hook->profNoReturn[i]) {
      /* the hook did not always return: either it ends in a tail jmp, whose
       * callee has not run yet, or it handed the routine back to the ROM part
       * way through. Both make the two figures cover different work, and the
       * harness bills only a hook that returned. */
      charge = 0;
      note = " (did not always return: tail jmp or yielded)";
    } else if(rom->profRomTail[i]) {
      /* the ROM's copy did not always return either: it left through a tail jmp
       * into another routine, so its frame could only be closed when the routine
       * it was called from returned, and the figure covers that tail as well */
      charge = 0;
      note = " (rom side left through a tail jmp: measured to its caller's return)";
    }
    fprintf(f, "%06X %-32s %6" PRId64 " %8" PRIu64 "   ; rom %" PRIu64 " [%" PRIu64 "-%" PRIu64
            "] hook %" PRIu64 " [%" PRIu64 "-%" PRIu64 "]%s\n",
            rom->hooks[i].addr, rom->hooks[i].name, charge, rom->profCalls[i], romMean,
            rom->profMin[i], rom->profMax[i], hookMean,
            hook->profCalls[i] ? hook->profMin[i] : 0, hook->profMax[i], note);
    written++;
  }
  fclose(f);
  printf("profile: %u of %u routines were called -> %s\n", written, rom->hookCount, path);
  return true;
}

/* ---- --test-coro --------------------------------------------------------
 *
 * The --no-cpu scheduler rests on one primitive: a body chain runs on a stack
 * of its own and a yield suspends that stack instead of unwinding it
 * (harness/coro.h). There are two implementations of it -- ucontext on POSIX,
 * Win32 fibers on Windows -- and only one of them can be exercised by the
 * lockstep gate here, because the gate needs the ROM and the ROM never enters
 * CI. So the primitive gets a self-test of its own that needs no ROM, no
 * emulator and no data: it can run on any runner, which is the point.
 *
 * It checks the four things the scheduler actually asks of a backend:
 *
 *   1. a body suspends and resumes where it stopped, and its stack survives
 *      (a 16 KiB local buffer is written before a yield and verified after);
 *   2. coroutines nest -- a coro started and resumed from inside another
 *      coro's stack comes back to *that* stack, which is what happens every
 *      time the SPC700's driver stack is resumed from inside a 65816 body;
 *   3. a body yields across a simulated frame boundary exactly as
 *      ss_yield_wanted() makes it: the scheduler gets control back between two
 *      steps, and resuming continues the same loop;
 *   4. a suspended coroutine can be torn down (the scheduler drops one whenever
 *      an interrupt abandons it, and ss_nocpu_free frees them all at exit), and
 *      a finished coroutine can be started again on the same stack, which is
 *      how a context slot is reused;
 *   5. coro_start on a coroutine still parked in coro_yield -- not finished --
 *      discards the parked body instead of resuming it and runs the new fn from
 *      scratch (coro_fibers.c note 4; the ucontext backend gets this for free by
 *      re-makecontext-ing the same stack every coro_start), and the coroutine is
 *      fully usable afterwards: yield, resume, finish and free all work, whether
 *      the restart is initiated from main or from inside another coro's stack,
 *      which is the shape ss_nocpu_reap() abandons a driver's body chain in.
 */
#define CORO_TEST_LOG   64
#define CORO_TEST_BLOB  16384u
#define CORO_TEST_STACK (256u * 1024u)
#define CORO_TEST_STEPS 10
#define CORO_TEST_CYCLES_PER_FRAME 4
#define CORO_TEST_TEARDOWNS 64

typedef struct {
  char log[CORO_TEST_LOG];
  unsigned logN;
  /* nesting */
  Coro* outer;
  Coro* inner;
  /* stack survival */
  Coro* deep;
  unsigned blobBad;
  bool deepFinished;
  /* the simulated frame boundary */
  Coro* body;
  int frame;
  int cycles;
  int steps;
  int yields;
  int resumes;
  int yieldedAt[CORO_TEST_STEPS];
  bool bodyFinished;
  /* teardown of a suspended coroutine */
  Coro* parked;
  int parkedEntered;
  int parkedFinished;
  /* coro_start restarting a coroutine still parked in coro_yield */
  Coro* restart;
  Coro* host;
  bool restartOldPostYield;
} CoroTest;

static void ct_log(CoroTest* t, char c) {
  if(t->logN + 1 < sizeof(t->log)) t->log[t->logN++] = c;
}

/* 1. a stack that survives a suspension */
static void ct_deep(void* arg) {
  CoroTest* t = (CoroTest*) arg;
  volatile unsigned char blob[CORO_TEST_BLOB];
  for(unsigned i = 0; i < CORO_TEST_BLOB; i++) blob[i] = (unsigned char) (i * 31u + 7u);
  ct_log(t, 'd');
  coro_yield(t->deep);
  unsigned bad = 0;
  for(unsigned i = 0; i < CORO_TEST_BLOB; i++) {
    if(blob[i] != (unsigned char) (i * 31u + 7u)) bad++;
  }
  t->blobBad = bad;
  t->deepFinished = true;
  ct_log(t, 'e');
}

/* 2. nesting: the inner coro is started and resumed from the outer's stack */
static void ct_inner(void* arg) {
  CoroTest* t = (CoroTest*) arg;
  ct_log(t, 'i');
  coro_yield(t->inner);
  ct_log(t, 'j');
}

static void ct_outer(void* arg) {
  CoroTest* t = (CoroTest*) arg;
  ct_log(t, 'o');
  coro_start(t->inner, ct_inner, t);   /* runs 'i', yields back here, not to main */
  ct_log(t, 'p');
  coro_yield(t->outer);                /* now to main */
  ct_log(t, 'q');
  coro_resume(t->inner);               /* runs 'j' and returns, back here */
  ct_log(t, 'r');
}

/* 3. a body that yields whenever the machine has moved on underneath it, which
 *    is ss_yield_wanted()'s shape with the machine replaced by a counter: a
 *    body's own work advances the clock, and every fourth step ends a frame. */
static void ct_body(void* arg) {
  CoroTest* t = (CoroTest*) arg;
  int entryFrame = t->frame;
  for(int i = 0; i < CORO_TEST_STEPS; i++) {
    t->steps++;
    if(++t->cycles % CORO_TEST_CYCLES_PER_FRAME == 0) t->frame++;
    if(t->frame != entryFrame) {
      if(t->yields < CORO_TEST_STEPS) t->yieldedAt[t->yields] = t->cycles;
      t->yields++;
      coro_yield(t->body);             /* the scheduler runs here */
      entryFrame = t->frame;           /* resumed inside the same loop */
    }
  }
  t->bodyFinished = true;
}

/* 4. a coroutine that parks and is never resumed */
static void ct_park(void* arg) {
  CoroTest* t = (CoroTest*) arg;
  t->parkedEntered++;
  coro_yield(t->parked);
  t->parkedFinished++;                 /* must never happen: it is torn down */
}

/* 5. coro_start on a coroutine parked mid-body must discard that body, not
 *    resume it -- 'Y' below must never reach the log. */
static void ct_restart_old(void* arg) {
  CoroTest* t = (CoroTest*) arg;
  ct_log(t, 'X');
  coro_yield(t->restart);
  ct_log(t, 'Y');                      /* must never run: the body is abandoned */
  t->restartOldPostYield = true;
}

static void ct_restart_new(void* arg) {
  CoroTest* t = (CoroTest*) arg;
  ct_log(t, 'M');
  coro_yield(t->restart);
  ct_log(t, 'N');
}

/* the same restart, but issued from inside another coroutine's stack instead
 * of main's -- ss_nocpu_reap() drops a driver's body chain this way every time
 * an NMI displaces it, from inside whatever body caught the APU up. */
static void ct_restart_from_nested(void* arg) {
  CoroTest* t = (CoroTest*) arg;
  ct_log(t, 'H');
  coro_start(t->restart, ct_restart_new, t);
  ct_log(t, 'I');
  coro_yield(t->host);
}

static bool ct_check(const char* what, bool ok) {
  printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
  return ok;
}

static int run_coro_test(void) {
  CoroTest t;
  memset(&t, 0, sizeof(t));
  int bad = 0;

  printf("test-coro: backend %s\n", coro_backend());

  /* 1. suspend, resume, and a stack that is still there afterwards */
  t.deep = coro_new(CORO_TEST_STACK);
  bad += !ct_check("a fresh coroutine is done before it is started", coro_done(t.deep));
  coro_start(t.deep, ct_deep, &t);
  bad += !ct_check("a started coroutine that yielded is not done", !coro_done(t.deep));
  bad += !ct_check("it did not run past its yield", !t.deepFinished);
  coro_resume(t.deep);
  bad += !ct_check("it ran to its end after one resume", coro_done(t.deep));
  bad += !ct_check("16 KiB of its stack survived the suspension", t.blobBad == 0);

  /* the same object, started again: a context slot is reused, not reallocated */
  t.blobBad = 1;
  t.deepFinished = false;
  coro_start(t.deep, ct_deep, &t);
  coro_resume(t.deep);
  bad += !ct_check("a finished coroutine restarts on the same stack",
                   coro_done(t.deep) && t.deepFinished && t.blobBad == 0);
  bad += !ct_check("both runs logged their two halves", strcmp(t.log, "dede") == 0);
  coro_free(t.deep);
  t.deep = NULL;

  /* 2. nesting */
  t.logN = 0;
  memset(t.log, 0, sizeof(t.log));
  t.outer = coro_new(CORO_TEST_STACK);
  t.inner = coro_new(CORO_TEST_STACK);
  ct_log(&t, 'A');
  coro_start(t.outer, ct_outer, &t);
  bad += !ct_check("the outer coro yielded to main, not the inner one",
                   !coro_done(t.outer) && !coro_done(t.inner));
  ct_log(&t, 'B');
  coro_resume(t.outer);
  ct_log(&t, 'C');
  bad += !ct_check("a nested switch returned to the stack that made it",
                   strcmp(t.log, "AoipBqjrC") == 0);
  bad += !ct_check("both coroutines ran to their end",
                   coro_done(t.outer) && coro_done(t.inner));
  if(strcmp(t.log, "AoipBqjrC") != 0) printf("    log was \"%s\"\n", t.log);
  coro_free(t.inner);
  coro_free(t.outer);
  t.inner = t.outer = NULL;

  /* 3. yields across a simulated frame boundary */
  t.body = coro_new(CORO_TEST_STACK);
  coro_start(t.body, ct_body, &t);
  while(!coro_done(t.body)) {
    t.resumes++;
    if(t.resumes > CORO_TEST_STEPS) break;    /* a backend that never comes back */
    coro_resume(t.body);
  }
  bad += !ct_check("the body ran every one of its steps",
                   t.bodyFinished && t.steps == CORO_TEST_STEPS);
  bad += !ct_check("it stopped at both frame boundaries and nowhere else",
                   t.yields == CORO_TEST_STEPS / CORO_TEST_CYCLES_PER_FRAME &&
                   t.resumes == t.yields);
  bad += !ct_check("each stop was on the step that ended a frame",
                   t.yieldedAt[0] == CORO_TEST_CYCLES_PER_FRAME &&
                   t.yieldedAt[1] == 2 * CORO_TEST_CYCLES_PER_FRAME);
  coro_free(t.body);
  t.body = NULL;

  /* 4. teardown of a coroutine suspended halfway through a body */
  for(int i = 0; i < CORO_TEST_TEARDOWNS; i++) {
    t.parked = coro_new(CORO_TEST_STACK);
    coro_start(t.parked, ct_park, &t);
    if(coro_done(t.parked)) break;
    coro_free(t.parked);                     /* dropped where it stands */
    t.parked = NULL;
  }
  bad += !ct_check("every suspended coroutine was entered and torn down",
                   t.parkedEntered == CORO_TEST_TEARDOWNS && t.parked == NULL);
  bad += !ct_check("none of them ran on after the teardown", t.parkedFinished == 0);

  /* 5. coro_start on a coroutine parked in coro_yield, not finished */
  t.logN = 0;
  memset(t.log, 0, sizeof(t.log));
  t.restart = coro_new(CORO_TEST_STACK);
  coro_start(t.restart, ct_restart_old, &t);
  bad += !ct_check("the old body ran up to its yield and parked there",
                   strcmp(t.log, "X") == 0 && !coro_done(t.restart));
  coro_start(t.restart, ct_restart_new, &t);   /* restart while parked */
  bad += !ct_check("coro_start on a parked coroutine ran the new fn from its start",
                   !coro_done(t.restart) && strcmp(t.log, "XM") == 0);
  bad += !ct_check("the abandoned body's post-yield marker never appeared",
                   !t.restartOldPostYield && strchr(t.log, 'Y') == NULL);
  coro_resume(t.restart);
  bad += !ct_check("the restarted coroutine still yields, resumes and finishes",
                   coro_done(t.restart) && strcmp(t.log, "XMN") == 0);
  coro_free(t.restart);
  bad += !ct_check("coro_free of the restarted coroutine did not crash", true);
  t.restart = NULL;

  /* the same restart, but issued from inside another coroutine's stack */
  t.logN = 0;
  memset(t.log, 0, sizeof(t.log));
  t.restart = coro_new(CORO_TEST_STACK);
  t.host = coro_new(CORO_TEST_STACK);
  coro_start(t.restart, ct_restart_old, &t);   /* parks, logs 'X' */
  coro_start(t.host, ct_restart_from_nested, &t);
  bad += !ct_check("a restart from inside another coro's stack discards the old body",
                   !coro_done(t.restart) && !coro_done(t.host) &&
                   strcmp(t.log, "XHMI") == 0 && !t.restartOldPostYield);
  coro_resume(t.restart);
  coro_resume(t.host);
  bad += !ct_check("both coroutines finish normally afterwards",
                   coro_done(t.restart) && coro_done(t.host) && strcmp(t.log, "XHMIN") == 0);
  coro_free(t.restart);
  coro_free(t.host);
  t.restart = t.host = NULL;

  /* coro_start on an already-finished coroutine restarts it on the same stack
   * too: covered above in group 1 (t.deep is started a second time after it
   * ran to completion). */

  printf("test-coro: %s\n", bad == 0 ? "PASS" : "FAIL");
  return bad == 0 ? 0 : 1;
}

/* ---- --test-nesting ----------------------------------------------------
 *
 * The one thing a hook cannot get from the machine itself: whether the machine
 * has moved on since *this* hook was entered. The dispatcher snapshots
 * snes->frames and snes->inVblank, and ss_yield_wanted() compares against that
 * snapshot, which is how a body that models the instruction stream knows to hand
 * the rest of its routine back to the ROM at a frame boundary.
 *
 * Hooks nest: a converted routine reaches a converted callee through
 * ss_run_callee / ss_call_sub, which run the reference CPU, so the callee's own
 * entry address is dispatched again and its hook fires inside the caller's. With
 * one snapshot per machine the inner hook's entry overwrote the outer hook's and
 * never put it back, so an outer hook that had already crossed a frame boundary
 * was told, for the rest of its routine, that nothing had happened.
 *
 * This exercises exactly that, without depending on any ROM code: two test hooks
 * at two addresses no routine uses, entered by pointing the CPU at them.
 *
 *   phase 0  the outer hook is entered mid-frame, spends time until the machine
 *            enters vblank, then calls the inner hook.
 *   phase 1  the outer hook is entered in vblank and spends time until the frame
 *            counter moves on, which is the other half of ss_yield_wanted().
 *
 * In both, the outer hook must still want to yield after the nested call.
 */
#define NEST_OUTER 0xc0f000u   /* two cart addresses that no routine claims; the */
#define NEST_INNER 0xc0f010u   /* hooks answer for them, so no ROM code runs */

typedef struct {
  int phase;
  bool ranOuter, ranInner;
  bool outerYieldAtEntry;    /* false: a fresh hook has nothing to hand back */
  bool crossed;              /* the boundary the outer hook waited for */
  bool intPending;           /* must stay false: the yield has to be the boundary */
  bool innerYieldAtEntry;    /* false: the inner hook gets its own snapshot */
  bool outerYieldAfterCall;  /* true: the outer hook's snapshot survived */
  int depthInInner;
  int depthAfterCall;
  bool spRestored;
} NestTest;

static NestTest gNest;

static void nest_inner(SnesState* ss) {
  gNest.ranInner = true;
  gNest.depthInInner = ss_hook_depth(ss);
  /* Entered after the boundary the outer hook crossed, so this hook -- which has
   * only just started -- must see nothing to hand back. */
  gNest.innerYieldAtEntry = ss_yield_wanted(ss);
  ss_rts(ss);
}

static void nest_outer(SnesState* ss) {
  gNest.ranOuter = true;
  gNest.outerYieldAtEntry = ss_yield_wanted(ss);

  /* Spend time the way a long routine does until the machine moves on under us.
   * Phase 0 is entered outside vblank and stops when vblank starts; phase 1 is
   * entered inside it and stops when the frame counter moves. */
  unsigned long guard = 0;
  const uint32_t frame0 = ss->snes->frames;
  while(guard++ < 4000000ul) {
    if(gNest.phase == 0 ? ss_yield_wanted(ss) : ss->snes->frames != frame0) break;
    ss_idle(ss);
  }
  gNest.crossed = ss_yield_wanted(ss);
  gNest.intPending = ss->snes->cpu->intWanted;

  /* Now reach a second hook the way a converted routine reaches a converted
   * callee: push a return frame and let the reference CPU dispatch it. */
  const uint16_t spBefore = ss_sp(ss);
  ss_call_sub(ss, (uint8_t) (NEST_INNER >> 16), (uint16_t) NEST_INNER);
  gNest.spRestored = ss_sp(ss) == spBefore;
  gNest.depthAfterCall = ss_hook_depth(ss);

  /* The assertion. The boundary the outer hook crossed before the nested call is
   * still its business: the rest of its routine has to go back to the ROM. */
  gNest.outerYieldAfterCall = ss_yield_wanted(ss);
  ss_rts(ss);
}

static bool nest_check(const char* what, bool ok) {
  printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
  return ok;
}

static int run_nesting_test(const uint8_t* rom, size_t romLen) {
  Installed table[2] = {
    { NEST_OUTER, "test_nesting_outer", NULL, nest_outer, 0 },
    { NEST_INNER, "test_nesting_inner", NULL, nest_inner, 0 },
  };
  int failures = 0;
  for(int phase = 0; phase < 2; phase++) {
    Machine m;
    machine_init(&m, rom, romLen, table, 2, true, false);
    memset(&gNest, 0, sizeof gNest);
    gNest.phase = phase;
    /* The first opcode after a reset is the reset sequence itself: it sets the
     * stack pointer and the pc from the vector, and dispatches no hook. */
    cpu_runOpcode(m.snes->cpu);
    if(phase == 1) {
      /* start the outer hook inside vblank, so the boundary it waits for is the
       * frame counter rather than the start of vblank */
      while(!m.snes->inVblank) snes_cpuIdle(m.snes, false);
    }
    /* Enter the outer hook the way the dispatcher does: point the CPU at its
     * address and run one opcode. The hook claims the fetch, so no ROM
     * instruction at that address is ever executed. */
    uint16_t sp0 = m.snes->cpu->sp;
    m.snes->cpu->k = (uint8_t) (NEST_OUTER >> 16);
    m.snes->cpu->pc = (uint16_t) NEST_OUTER;
    ss_push16(&m.ss, (uint16_t) 0x1234);          /* a return frame for the outer */
    cpu_runOpcode(m.snes->cpu);

    printf("test-nesting phase %d (%s):\n", phase,
           phase == 0 ? "outer entered mid-frame, vblank starts inside it"
                      : "outer entered in vblank, the frame counter moves inside it");
    int bad = 0;
    bad += !nest_check("outer hook ran", gNest.ranOuter);
    bad += !nest_check("outer hook had nothing to yield at entry", !gNest.outerYieldAtEntry);
    bad += !nest_check("the machine crossed a boundary inside the outer hook",
                       gNest.crossed);
    bad += !nest_check("the crossing was a frame boundary, not a latched interrupt",
                       !gNest.intPending);
    bad += !nest_check("inner hook ran, nested inside the outer", gNest.ranInner);
    bad += !nest_check("inner hook was two hooks deep", gNest.depthInInner == 2);
    bad += !nest_check("inner hook had nothing to yield at its own entry",
                       !gNest.innerYieldAtEntry);
    bad += !nest_check("the nested call left the stack where it found it",
                       gNest.spRestored);
    bad += !nest_check("outer hook is one hook deep again after it",
                       gNest.depthAfterCall == 1);
    bad += !nest_check("OUTER HOOK STILL WANTS TO YIELD AFTER THE NESTED CALL",
                       gNest.outerYieldAfterCall);
    bad += !nest_check("the dispatcher unwound to no hook in flight",
                       ss_hook_depth(&m.ss) == 0);
    bad += !nest_check("the outer hook returned to its own frame",
                       m.snes->cpu->sp == sp0);
    bad += !nest_check("the snapshot stack reached depth 2",
                       ss_hook_max_depth(&m.ss) == 2);
    failures += bad;
    machine_free(&m);
  }
  printf("test-nesting: %s\n", failures == 0 ? "PASS" : "FAIL");
  return failures == 0 ? 0 : 1;
}

/* ---- --test-spc-timing --------------------------------------------------
 *
 * sps_op_cycles() states what the core's own implementation of an opcode costs
 * in APU cycles, and recomp/spc/spc_time.h is built on the same counts. This
 * checks the statement instead of trusting it: for every opcode the table
 * covers, the instruction is assembled into scratch ARAM, executed by the
 * reference SPC700 with hooks off, and the cycles it actually spent are compared
 * against the figure. A taken branch costs two more, which the table excludes,
 * so the expected figure adds them back where the flags make the branch taken.
 *
 * Operands are chosen to stay out of the $F0-$FF register block, so nothing here
 * touches a timer, a port or the DSP.
 */
#define SPCT_CODE 0x0200u    /* scratch: the instruction under test */
#define SPCT_DP   0x30u      /* scratch: its direct-page operand */
#define SPCT_ABS  0x0300u    /* scratch: its absolute operand */

typedef struct {
  uint8_t op;
  int len;
  uint8_t b1, b2;
  bool taken;                /* the branch this opcode takes with n = z = 0 */
  const char* text;
} SpcTimingCase;

static const SpcTimingCase kSpcTiming[] = {
  /* implied */
  { 0x1c, 1, 0, 0, false, "asl a" },      { 0x1d, 1, 0, 0, false, "dec x" },
  { 0x20, 1, 0, 0, false, "clrp" },       { 0x3d, 1, 0, 0, false, "inc x" },
  { 0x5d, 1, 0, 0, false, "mov x,a" },    { 0x60, 1, 0, 0, false, "clrc" },
  { 0x7d, 1, 0, 0, false, "mov a,x" },    { 0x80, 1, 0, 0, false, "setc" },
  { 0x9c, 1, 0, 0, false, "dec a" },      { 0xbc, 1, 0, 0, false, "inc a" },
  { 0xbd, 1, 0, 0, false, "mov sp,x" },   { 0xdc, 1, 0, 0, false, "dec y" },
  { 0xdd, 1, 0, 0, false, "mov a,y" },    { 0xfc, 1, 0, 0, false, "inc y" },
  { 0xfd, 1, 0, 0, false, "mov y,a" },
  { 0x2d, 1, 0, 0, false, "push a" },     { 0x4d, 1, 0, 0, false, "push x" },
  { 0xae, 1, 0, 0, false, "pop a" },      { 0xce, 1, 0, 0, false, "pop x" },
  { 0x6f, 1, 0, 0, false, "ret" },
  /* immediate */
  { 0x28, 2, 0x07, 0, false, "and a,#imm" }, { 0x68, 2, 0x80, 0, false, "cmp a,#imm" },
  { 0x8d, 2, 0x08, 0, false, "mov y,#imm" }, { 0xa8, 2, 0x0f, 0, false, "sbc a,#imm" },
  { 0xcd, 2, 0xff, 0, false, "mov x,#imm" }, { 0xe8, 2, 0x00, 0, false, "mov a,#imm" },
  /* branches, with n = z = 0 */
  { 0x10, 2, 0x00, 0, true,  "bpl (taken)" },
  { 0x30, 2, 0x00, 0, false, "bmi (not taken)" },
  { 0xd0, 2, 0x00, 0, true,  "bne (taken)" },
  { 0xf0, 2, 0x00, 0, false, "beq (not taken)" },
  { 0x2f, 2, 0x00, 0, true,  "bra" },
  /* direct page */
  { 0x3e, 2, SPCT_DP, 0, false, "cmp x,dp" },  { 0x64, 2, SPCT_DP, 0, false, "cmp a,dp" },
  { 0xe4, 2, SPCT_DP, 0, false, "mov a,dp" },  { 0xf8, 2, SPCT_DP, 0, false, "mov x,dp" },
  { 0xab, 2, SPCT_DP, 0, false, "inc dp" },    { 0xc4, 2, SPCT_DP, 0, false, "mov dp,a" },
  { 0xcb, 2, SPCT_DP, 0, false, "mov dp,y" },  { 0xd8, 2, SPCT_DP, 0, false, "mov dp,x" },
  { 0xba, 2, SPCT_DP, 0, false, "movw ya,dp" },{ 0x7a, 2, SPCT_DP, 0, false, "addw ya,dp" },
  { 0xda, 2, SPCT_DP, 0, false, "movw dp,ya" },{ 0x1a, 2, SPCT_DP, 0, false, "decw dp" },
  { 0xd4, 2, SPCT_DP, 0, false, "mov dp+x,a" },{ 0xf7, 2, SPCT_DP, 0, false, "mov a,(dp)+y" },
  { 0xd7, 2, SPCT_DP, 0, false, "mov (dp)+y,a" },
  /* absolute */
  { 0xe5, 3, (uint8_t) SPCT_ABS, (uint8_t) (SPCT_ABS >> 8), false, "mov a,abs" },
  { 0xe9, 3, (uint8_t) SPCT_ABS, (uint8_t) (SPCT_ABS >> 8), false, "mov x,abs" },
  { 0x5f, 3, (uint8_t) SPCT_ABS, (uint8_t) (SPCT_ABS >> 8), false, "jmp abs" },
  { 0xac, 3, (uint8_t) SPCT_ABS, (uint8_t) (SPCT_ABS >> 8), false, "inc abs" },
  { 0xc5, 3, (uint8_t) SPCT_ABS, (uint8_t) (SPCT_ABS >> 8), false, "mov abs,a" },
  { 0xc9, 3, (uint8_t) SPCT_ABS, (uint8_t) (SPCT_ABS >> 8), false, "mov abs,x" },
  { 0xcc, 3, (uint8_t) SPCT_ABS, (uint8_t) (SPCT_ABS >> 8), false, "mov abs,y" },
  { 0xf6, 3, (uint8_t) SPCT_ABS, (uint8_t) (SPCT_ABS >> 8), false, "mov a,abs+y" },
  { 0xd5, 3, (uint8_t) SPCT_ABS, (uint8_t) (SPCT_ABS >> 8), false, "mov abs+x,a" },
  { 0xd6, 3, (uint8_t) SPCT_ABS, (uint8_t) (SPCT_ABS >> 8), false, "mov abs+y,a" },
  { 0x1f, 3, (uint8_t) SPCT_ABS, (uint8_t) (SPCT_ABS >> 8), false, "jmp (abs+x)" },
  { 0x3f, 3, (uint8_t) SPCT_ABS, (uint8_t) (SPCT_ABS >> 8), false, "call abs" },
  /* dp,#imm: the immediate is the first operand byte, the destination the second */
  { 0x8f, 3, 0x20, SPCT_DP, false, "mov dp,#imm" },
  { 0x98, 3, 0x08, SPCT_DP, false, "adc dp,#imm" },
  /* dbnz dp,rel: $30 holds 1, so the decrement reaches zero and it is not taken */
  { 0x6e, 3, SPCT_DP, 0x00, false, "dbnz dp,rel (not taken)" },
};

static int run_spc_timing_test(const uint8_t* rom, size_t romLen) {
  Machine m;
  machine_init(&m, rom, romLen, NULL, 0, false, false);
  machine_install_spc(&m, NULL, 0, false);
  Apu* apu = m.snes->apu;
  Spc* spc = apu->spc;
  /* the first spc_runOpcode after a reset is the reset sequence itself */
  spc_runOpcode(spc);

  int bad = 0;
  for(unsigned i = 0; i < sizeof(kSpcTiming) / sizeof(kSpcTiming[0]); i++) {
    const SpcTimingCase* c = &kSpcTiming[i];
    apu->ram[SPCT_CODE] = c->op;
    apu->ram[SPCT_CODE + 1] = c->b1;
    apu->ram[SPCT_CODE + 2] = c->b2;
    apu->ram[SPCT_DP] = 1;              /* dbnz reaches zero; also the pointer low */
    apu->ram[SPCT_DP + 1] = 3;          /* (dp)+Y points into the scratch page */
    spc->pc = SPCT_CODE;
    spc->a = 0x12; spc->x = 0x00; spc->y = 0x00; spc->sp = 0xf0;
    spc->n = false; spc->z = false; spc->c = false; spc->v = false;
    spc->h = false; spc->p = false; spc->i = false; spc->b = false;
    apu->sliceEnd = apu->cycles;        /* unused with hooks off; keep it defined */
    uint32_t before = apu->cycles;
    spc_runOpcode(spc);
    int got = (int) (apu->cycles - before);
    int want = sps_op_cycles(c->op) + (c->taken ? 2 : 0);
    bool ok = want != 0 && got == want;
    if(!ok) bad++;
    printf("  %02X %-24s want %2d got %2d  %s\n", c->op, c->text, want, got,
           ok ? "ok" : "FAIL");
  }
  printf("test-spc-timing: %u opcodes, %s\n",
         (unsigned) (sizeof(kSpcTiming) / sizeof(kSpcTiming[0])),
         bad == 0 ? "PASS" : "FAIL");
  machine_free(&m);
  return bad == 0 ? 0 : 1;
}

/* ---- --unit: the routine-level lockstep gate ----------------------------
 *
 * The frame-level gate can only credit a routine some input script reaches.
 * Thirty-odd routines in this port are reachable by nothing: command handlers
 * the 65816 never sends, sequence opcodes no song uses, stale table slots,
 * the one-row OAM emitters, the animation-rate entries no table word points
 * at, `unused_vec`, and the four 65816 orphans with no caller at all. They are
 * converted, and until now they were listed as unverified because "never
 * entered" is the honest thing to say about them.
 *
 * --unit gives them a gate of their own, at the granularity of one routine and
 * one seeded machine state. For each seed in config/recomp_units.txt it:
 *
 *   1. boots the ROM under a named input script to a named frame, so WRAM,
 *      VRAM, CGRAM, OAM, ARAM, the DSP and the SPC registers hold content the
 *      game itself produced rather than zeroes;
 *   2. loads that state into two fresh machines -- the reference, which runs no
 *      hooks at all, and the candidate, which runs the whole table;
 *   3. overrides the registers and a few memory cells from the seed, pushes the
 *      return frame the routine expects, and points both machines at the
 *      routine's entry address;
 *   4. runs the ROM's own code on the reference until control leaves the
 *      routine, and the C body on the candidate from the identical state;
 *   5. compares all seven regions, every register, and the cycle counts.
 *
 * Leaving the routine is one rule for all four shapes, and it needs no
 * per-routine return kind at the stopping end: control has left when the pc is
 * outside the routine's own byte range *and* the stack pointer is at or above
 * where it stood at the first instruction. The stack-pointer half is what lets
 * a routine call a subroutine (the pc leaves the range, but a frame is below)
 * and what distinguishes a tail `jmp` from it. The return frame the harness
 * pushes carries a sentinel address that is deliberately outside every
 * routine, so an `rts`, an `rtl` and an `rti` all satisfy the same rule and no
 * instruction at the sentinel is ever fetched. `ret=` in the seed says which
 * frame to push, because the shape of the frame is the routine's business:
 * `rts` pops a word, `rtl` a word and a bank, `rti` flags, a word and a bank,
 * and `jmp` pops nothing.
 *
 * Interrupts are the one thing that has to be taken out of the picture. A hook
 * is atomic where the routine it replaces is not, so an NMI landing inside the
 * reference's run and inside a different instruction of the candidate's would
 * be a difference the routine is not responsible for. The boot therefore ends
 * with NMI and both timer IRQs disabled and the pending latch cleared, and
 * with the machine parked just after vblank ends, which leaves a whole active
 * frame -- some 300 000 master cycles -- before either the vblank flag or the
 * frame counter can move under the routine. Both machines get exactly the same
 * treatment, and the run report says how many cycles each seed actually spent.
 *
 * The seeds are data, not code: config/recomp_units.txt, one line per seed.
 */

#define UNIT_SENT_PC    0xFFFFu   /* the return address the harness pushes: no */
#define UNIT_SENT_BANK  0x00u     /* routine owns it, so returning leaves them all */
#define UNIT_MAX_CELLS  24
#define UNIT_MAX_STACK  8
#define UNIT_GUARD      40000000ul

typedef enum { UNIT_RET_RTS, UNIT_RET_RTL, UNIT_RET_RTI, UNIT_RET_JMP } UnitRet;

typedef struct {
  uint32_t addr;
  uint16_t val;
  int width;                 /* 1 or 2 bytes, little-endian */
} UnitCell;

typedef struct {
  char name[64];
  char script[192];
  int frame;
  int line;
  bool spc;                  /* which processor, resolved from the registries */
  uint32_t entry;            /* 65816: canonical $C0:0000+offset. SPC: 16-bit */
  uint32_t end;              /* exclusive, same form */
  bool hasEnd, hasRet;
  UnitRet ret;
  /* register overrides; only the ones the line names are applied */
  bool hasA, hasX, hasY, hasP, hasDB, hasDP, hasSP, hasDspAdr;
  uint16_t a, x, y, sp, dp;
  uint8_t p, db, dspAdr;
  UnitCell cell[UNIT_MAX_CELLS];   /* ram:/aram: writes */
  int cellCount;
  UnitCell port[4];                /* port:N=v  (SPC only) */
  int portCount;
  UnitCell dsp[8];                 /* dsp:RR=v  (SPC only) */
  int dspCount;
  uint8_t stack[UNIT_MAX_STACK];   /* stack=HH.. pushed after the return frame */
  int stackCount;
  char note[160];
} UnitSeed;

/* The bank the ROM itself runs this routine in. Every routine here is reached
 * through the $80/$81 mirror, and the mirror is the fast half of the map, so
 * running the reference at the canonical $C0 address would charge the wrong
 * access time for every fetch. */
static uint32_t unit_run_pc(uint32_t canon) {
  uint32_t off = canon - 0xc00000u;
  uint16_t adr = (uint16_t) off;
  if(off < ROM_SIZE && adr >= 0x8000) return (uint32_t) (((0x80u + (off >> 16)) << 16) | adr);
  return canon;
}

/* An untimed push, for the frame the harness builds before the routine starts:
 * it is setup, not part of what the routine costs. */
static void unit_push8(Snes* s, uint8_t v) {
  Cpu* c = s->cpu;
  snes_write(s, c->sp, v);
  c->sp--;
  if(c->e) c->sp = (uint16_t) ((c->sp & 0xff) | 0x100);
}

static void unit_push16(Snes* s, uint16_t v) {
  unit_push8(s, (uint8_t) (v >> 8));
  unit_push8(s, (uint8_t) v);
}

static void unit_spc_push8(Apu* apu, uint8_t v) {
  Spc* s = apu->spc;
  apu->ram[0x100 | s->sp] = v;
  s->sp--;
}

/* ---- the seed file ----------------------------------------------------- */

static bool unit_hex(const char* s, uint32_t* out, int* digits) {
  uint32_t v = 0;
  int n = 0;
  if(s[0] == '$') s++;
  if(s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
  for(; *s != 0; s++) {
    int d;
    if(*s >= '0' && *s <= '9') d = *s - '0';
    else if(*s >= 'a' && *s <= 'f') d = *s - 'a' + 10;
    else if(*s >= 'A' && *s <= 'F') d = *s - 'A' + 10;
    else return false;
    v = (v << 4) | (uint32_t) d;
    n++;
  }
  if(n == 0 || n > 8) return false;
  *out = v;
  *digits = n;
  return true;
}

static bool unit_seed_key(UnitSeed* sd, char* key, char* val, const char* path) {
  uint32_t v = 0;
  int digits = 0;
  if(strncmp(key, "ram:", 4) == 0 || strncmp(key, "aram:", 5) == 0) {
    uint32_t adr = 0;
    int adigits = 0;
    if(!unit_hex(strchr(key, ':') + 1, &adr, &adigits) || !unit_hex(val, &v, &digits)) return false;
    if(sd->cellCount == UNIT_MAX_CELLS) return false;
    sd->cell[sd->cellCount].addr = adr;
    sd->cell[sd->cellCount].val = (uint16_t) v;
    sd->cell[sd->cellCount].width = digits > 2 ? 2 : 1;
    sd->cellCount++;
    return true;
  }
  if(strncmp(key, "port:", 5) == 0) {
    uint32_t idx = 0;
    int adigits = 0;
    if(!unit_hex(key + 5, &idx, &adigits) || !unit_hex(val, &v, &digits) || idx > 3) return false;
    if(sd->portCount == 4) return false;
    sd->port[sd->portCount].addr = idx;
    sd->port[sd->portCount].val = (uint16_t) v;
    sd->port[sd->portCount].width = 1;
    sd->portCount++;
    return true;
  }
  if(strncmp(key, "dsp:", 4) == 0) {
    uint32_t reg = 0;
    int adigits = 0;
    if(!unit_hex(key + 4, &reg, &adigits) || !unit_hex(val, &v, &digits) || reg > 0x7f) return false;
    if(sd->dspCount == 8) return false;
    sd->dsp[sd->dspCount].addr = reg;
    sd->dsp[sd->dspCount].val = (uint16_t) v;
    sd->dsp[sd->dspCount].width = 1;
    sd->dspCount++;
    return true;
  }
  if(strcmp(key, "ret") == 0) {
    sd->hasRet = true;
    if(strcmp(val, "rts") == 0 || strcmp(val, "ret") == 0) sd->ret = UNIT_RET_RTS;
    else if(strcmp(val, "rtl") == 0) sd->ret = UNIT_RET_RTL;
    else if(strcmp(val, "rti") == 0) sd->ret = UNIT_RET_RTI;
    else if(strcmp(val, "jmp") == 0) sd->ret = UNIT_RET_JMP;
    else return false;
    return true;
  }
  if(strcmp(key, "stack") == 0) {
    size_t n = strlen(val);
    if(n == 0 || (n & 1) != 0 || n / 2 > UNIT_MAX_STACK) return false;
    for(size_t i = 0; i < n; i += 2) {
      char b[3] = { val[i], val[i + 1], 0 };
      if(!unit_hex(b, &v, &digits)) return false;
      sd->stack[sd->stackCount++] = (uint8_t) v;
    }
    return true;
  }
  if(!unit_hex(val, &v, &digits)) return false;
  if(strcmp(key, "end") == 0)      { sd->end = v; sd->hasEnd = true; }
  else if(strcmp(key, "a") == 0)   { sd->a = (uint16_t) v; sd->hasA = true; }
  else if(strcmp(key, "x") == 0)   { sd->x = (uint16_t) v; sd->hasX = true; }
  else if(strcmp(key, "y") == 0)   { sd->y = (uint16_t) v; sd->hasY = true; }
  else if(strcmp(key, "p") == 0)   { sd->p = (uint8_t) v; sd->hasP = true; }
  else if(strcmp(key, "psw") == 0) { sd->p = (uint8_t) v; sd->hasP = true; }
  else if(strcmp(key, "db") == 0)  { sd->db = (uint8_t) v; sd->hasDB = true; }
  else if(strcmp(key, "dp") == 0)  { sd->dp = (uint16_t) v; sd->hasDP = true; }
  else if(strcmp(key, "sp") == 0)  { sd->sp = (uint16_t) v; sd->hasSP = true; }
  /* the SPC700's DSPADDR latch is a register of the APU, not a byte of ARAM,
   * so it needs a key of its own: dsp:RR= sets what a register holds, this
   * sets which one $F3 is pointing at */
  else if(strcmp(key, "dspadr") == 0) { sd->dspAdr = (uint8_t) v; sd->hasDspAdr = true; }
  else {
    fprintf(stderr, "dream_harness: %s:%d: unknown seed key '%s'\n", path, sd->line, key);
    return false;
  }
  return true;
}

/* "name script frame key=val ... ; note" */
static bool unit_load(const char* path, UnitSeed** out, int* countOut) {
  FILE* f = fopen(path, "r");
  if(f == NULL) {
    fprintf(stderr, "dream_harness: cannot open %s: %s\n", path, strerror(errno));
    return false;
  }
  int cap = 32, count = 0;
  UnitSeed* seeds = calloc((size_t) cap, sizeof(UnitSeed));
  char line[1024];
  int lineno = 0;
  bool ok = true;
  while(fgets(line, sizeof(line), f) != NULL) {
    lineno++;
    char* note = strchr(line, ';');
    char* notep = NULL;
    if(note != NULL) { *note = 0; notep = note + 1; }
    char* p = line;
    while(*p == ' ' || *p == '\t') p++;
    if(*p == 0 || *p == '\n' || *p == '\r') continue;
    if(count == cap) {
      cap *= 2;
      seeds = realloc(seeds, (size_t) cap * sizeof(UnitSeed));
      memset(seeds + count, 0, (size_t) (cap - count) * sizeof(UnitSeed));
    }
    UnitSeed* sd = &seeds[count];
    memset(sd, 0, sizeof(*sd));
    sd->line = lineno;
    if(notep != NULL) {
      while(*notep == ' ' || *notep == '\t') notep++;
      size_t n = strlen(notep);
      while(n > 0 && (notep[n - 1] == '\n' || notep[n - 1] == '\r' || notep[n - 1] == ' ')) n--;
      if(n >= sizeof(sd->note)) n = sizeof(sd->note) - 1;
      memcpy(sd->note, notep, n);
      sd->note[n] = 0;
    }
    char* tok = strtok(p, " \t\r\n");
    if(tok == NULL) continue;
    snprintf(sd->name, sizeof(sd->name), "%s", tok);
    tok = strtok(NULL, " \t\r\n");
    if(tok == NULL) {
      fprintf(stderr, "dream_harness: %s:%d: expected a script name\n", path, lineno);
      ok = false; break;
    }
    snprintf(sd->script, sizeof(sd->script), "%s", tok);
    tok = strtok(NULL, " \t\r\n");
    if(tok == NULL) {
      fprintf(stderr, "dream_harness: %s:%d: expected a frame number\n", path, lineno);
      ok = false; break;
    }
    sd->frame = atoi(tok);
    while((tok = strtok(NULL, " \t\r\n")) != NULL) {
      char* eq = strchr(tok, '=');
      if(eq == NULL) {
        fprintf(stderr, "dream_harness: %s:%d: expected key=value, got '%s'\n", path, lineno, tok);
        ok = false; break;
      }
      *eq = 0;
      if(!unit_seed_key(sd, tok, eq + 1, path)) {
        fprintf(stderr, "dream_harness: %s:%d: bad seed field '%s=%s'\n", path, lineno, tok, eq + 1);
        ok = false; break;
      }
    }
    if(!ok) break;
    if(!sd->hasEnd || !sd->hasRet) {
      fprintf(stderr, "dream_harness: %s:%d: %s needs both end= and ret=\n",
              path, lineno, sd->name);
      ok = false; break;
    }
    count++;
  }
  fclose(f);
  if(!ok) { free(seeds); return false; }
  *out = seeds;
  *countOut = count;
  return true;
}

/* ---- one seed --------------------------------------------------------- */

typedef struct {
  uint8_t* data;
  int size;
  char script[192];
  int frame;
} UnitBoot;

/* Boot the ROM under one script to one frame, then park the machine where a
 * routine can run without an interrupt or a frame boundary landing inside it:
 * NMI and both timer IRQs off, the pending latch cleared, and the beam just
 * past the end of vblank. The state is saved once and loaded into both
 * machines of every seed that names this (script, frame). */
static bool unit_boot(const uint8_t* rom, size_t romLen, const char* script, int frame,
                      UnitBoot* out) {
  InputScript in = { NULL, 0 };
  if(script[0] != '-' && !input_load(&in, script)) return false;
  Machine m;
  machine_init(&m, rom, romLen, NULL, 0, false, false);
  machine_install_spc(&m, NULL, 0, false);
  for(int f = 0; f < frame; f++) {
    uint16_t s1, s2;
    input_state_at(&in, f, &s1, &s2);
    machine_set_input(&m, s1, s2);
    snes_runFrame(m.snes);
  }
  /* out of vblank, at the top of the next active frame */
  int guard = 0;
  while(m.snes->inVblank && guard++ < 200000) snes_runCycles(m.snes, 8);
  m.snes->nmiEnabled = false;
  m.snes->hIrqEnabled = false;
  m.snes->vIrqEnabled = false;
  m.snes->cpu->nmiWanted = false;
  m.snes->cpu->irqWanted = false;
  m.snes->cpu->intWanted = false;
  m.snes->cpu->waiting = false;
  m.snes->cpu->stopped = false;
  out->size = snes_saveState(m.snes, NULL);
  out->data = malloc((size_t) out->size);
  snes_saveState(m.snes, out->data);
  snprintf(out->script, sizeof(out->script), "%s", script);
  out->frame = frame;
  machine_free(&m);
  free(in.ev);
  return true;
}

static void unit_apply(Machine* m, const UnitSeed* sd) {
  Snes* s = m->snes;
  Cpu* c = s->cpu;
  s->nmiEnabled = false;
  s->hIrqEnabled = false;
  s->vIrqEnabled = false;
  c->nmiWanted = false;
  c->irqWanted = false;
  c->intWanted = false;
  c->waiting = false;
  c->stopped = false;
  s->apu->sliceEnd = s->apu->cycles + 0x40000000u;
  if(sd->spc) {
    Apu* apu = s->apu;
    Spc* sp = apu->spc;
    if(sd->hasP) sps_set_psw(&m->sps, sd->p);
    if(sd->hasA) sp->a = (uint8_t) sd->a;
    if(sd->hasX) sp->x = (uint8_t) sd->x;
    if(sd->hasY) sp->y = (uint8_t) sd->y;
    if(sd->hasSP) sp->sp = (uint8_t) sd->sp;
    for(int i = 0; i < sd->cellCount; i++) {
      uint16_t adr = (uint16_t) sd->cell[i].addr;
      apu->ram[adr] = (uint8_t) sd->cell[i].val;
      if(sd->cell[i].width == 2) apu->ram[(uint16_t) (adr + 1)] = (uint8_t) (sd->cell[i].val >> 8);
    }
    for(int i = 0; i < sd->portCount; i++) apu->inPorts[sd->port[i].addr] = (uint8_t) sd->port[i].val;
    if(sd->hasDspAdr) apu->dspAdr = sd->dspAdr;
    for(int i = 0; i < sd->dspCount; i++) dsp_write(apu->dsp, (uint8_t) sd->dsp[i].addr,
                                                    (uint8_t) sd->dsp[i].val);
    sp->resetWanted = false;
    sp->stopped = false;
    sp->pc = (uint16_t) sd->entry;
    if(sd->ret != UNIT_RET_JMP) {
      unit_spc_push8(apu, (uint8_t) (UNIT_SENT_PC >> 8));
      unit_spc_push8(apu, (uint8_t) UNIT_SENT_PC);
    }
    for(int i = 0; i < sd->stackCount; i++) unit_spc_push8(apu, sd->stack[i]);
    return;
  }
  if(sd->hasP) ss_set_p(&m->ss, sd->p);
  if(sd->hasA) c->a = sd->a;
  if(sd->hasX) c->x = sd->x;
  if(sd->hasY) c->y = sd->y;
  if(sd->hasDB) c->db = sd->db;
  if(sd->hasDP) c->dp = sd->dp;
  if(sd->hasSP) c->sp = sd->sp;
  for(int i = 0; i < sd->cellCount; i++) {
    uint32_t adr = sd->cell[i].addr;
    snes_write(s, adr, (uint8_t) sd->cell[i].val);
    if(sd->cell[i].width == 2) snes_write(s, (adr + 1) & 0xffffff, (uint8_t) (sd->cell[i].val >> 8));
  }
  uint32_t pc24 = unit_run_pc(sd->entry);
  c->k = (uint8_t) (pc24 >> 16);
  c->pc = (uint16_t) pc24;
  switch(sd->ret) {
    case UNIT_RET_RTS: unit_push16(s, (uint16_t) (UNIT_SENT_PC - 1)); break;
    case UNIT_RET_RTL: unit_push8(s, UNIT_SENT_BANK);
                       unit_push16(s, (uint16_t) (UNIT_SENT_PC - 1)); break;
    case UNIT_RET_RTI: unit_push8(s, UNIT_SENT_BANK);
                       unit_push16(s, UNIT_SENT_PC);
                       unit_push8(s, ss_p(&m->ss)); break;
    case UNIT_RET_JMP: break;
  }
  for(int i = 0; i < sd->stackCount; i++) unit_push8(s, sd->stack[i]);
}

/* Control has left the routine when the instruction about to run is outside its
 * byte range, the instruction *before* it was inside, and no frame the routine
 * pushed is still on the stack. All three conditions earn their place:
 *
 *   the stack pointer alone cannot say, because the pc leaves the range on
 *   every call to a routine that is not converted yet, and comes back;
 *
 *   the range alone cannot say either, for the same reason;
 *
 *   and "the instruction before it was inside" is what keeps a callee that
 *   deliberately unbalances the stack from looking like the end. The sound
 *   driver has exactly one: seq_pop_x pops its own return address, pops the
 *   channel index its caller pushed and pushes only the return address back,
 *   so for two instructions in the middle of it the stack pointer is *above*
 *   where the handler started while the pc is nowhere near the handler.
 *
 * The routine's first instruction is inside by construction, which is what
 * seeds the "before" half.  */
typedef struct { bool wasInside; } UnitWalk;

static bool unit_left_65816(const Machine* m, const UnitSeed* sd, uint16_t spEntry,
                            UnitWalk* w) {
  const Cpu* c = m->snes->cpu;
  uint32_t canon = 0;
  uint32_t pc24 = ((uint32_t) c->k << 16) | c->pc;
  const bool inside = canon_rom_addr(pc24, &canon) && canon >= sd->entry && canon < sd->end;
  if(!inside && w->wasInside && c->sp >= spEntry) return true;
  w->wasInside = inside;
  return false;
}

static bool unit_left_spc(const Machine* m, const UnitSeed* sd, uint8_t spEntry,
                          UnitWalk* w) {
  const Spc* s = m->snes->apu->spc;
  const bool inside = s->pc >= sd->entry && s->pc < sd->end;
  if(!inside && w->wasInside && s->sp >= spEntry) return true;
  w->wasInside = inside;
  return false;
}

static const char* unit_ret_name(UnitRet r) {
  switch(r) {
    case UNIT_RET_RTS: return "rts";
    case UNIT_RET_RTL: return "rtl";
    case UNIT_RET_RTI: return "rti";
    default: return "jmp";
  }
}

typedef struct { const char* name; uint32_t ref, cand; } UnitRegDiff;

/* Every register the two machines have to agree on afterwards, read out of one
 * machine at a time so the two lists line up by index. */
static void unit_regs(Machine* m, bool spc, uint32_t* v, const char** names, int* n) {
  int i = 0;
  if(spc) {
    const Spc* s = m->snes->apu->spc;
    names[i] = "a";   v[i++] = s->a;
    names[i] = "x";   v[i++] = s->x;
    names[i] = "y";   v[i++] = s->y;
    names[i] = "sp";  v[i++] = s->sp;
    names[i] = "pc";  v[i++] = s->pc;
    names[i] = "psw"; v[i++] = sps_psw(&m->sps);
  } else {
    const Cpu* c = m->snes->cpu;
    names[i] = "a";  v[i++] = c->a;
    names[i] = "x";  v[i++] = c->x;
    names[i] = "y";  v[i++] = c->y;
    names[i] = "sp"; v[i++] = c->sp;
    names[i] = "dp"; v[i++] = c->dp;
    names[i] = "db"; v[i++] = c->db;
    names[i] = "pb"; v[i++] = c->k;
    names[i] = "pc"; v[i++] = c->pc;
    names[i] = "p";  v[i++] = ss_p(&m->ss);
    names[i] = "e";  v[i++] = c->e ? 1u : 0u;
  }
  *n = i;
}

#define UNIT_REGS_MAX 10

static int unit_reg_diffs(Machine* ref, Machine* cand, bool spc, UnitRegDiff* out, int max) {
  uint32_t rv[UNIT_REGS_MAX], cv[UNIT_REGS_MAX];
  const char* rn[UNIT_REGS_MAX];
  const char* cn[UNIT_REGS_MAX];
  int nr = 0, nc = 0, n = 0;
  unit_regs(ref, spc, rv, rn, &nr);
  unit_regs(cand, spc, cv, cn, &nc);
  for(int i = 0; i < nr && i < nc; i++) {
    if(rv[i] == cv[i] || n >= max) continue;
    out[n].name = rn[i];
    out[n].ref = rv[i];
    out[n].cand = cv[i];
    n++;
  }
  return n;
}

/* Returns true when the seed passed. */
static bool unit_run_seed(const uint8_t* rom, size_t romLen, const UnitBoot* boot,
                          const UnitSeed* sd, Installed* table, unsigned tableCount,
                          SpcInstalled* spcTable, unsigned spcCount, int seedIndex) {
  Machine ref, cand;
  machine_init(&ref, rom, romLen, table, tableCount, false, false);
  machine_install_spc(&ref, spcTable, spcCount, false);
  machine_init(&cand, rom, romLen, table, tableCount, true, false);
  machine_install_spc(&cand, spcTable, spcCount, true);
  bool pass = true;
  const char* err = NULL;
  char detail[256];
  detail[0] = 0;

  if(!snes_loadState(ref.snes, boot->data, boot->size) ||
     !snes_loadState(cand.snes, boot->data, boot->size)) {
    err = "the boot state would not load";
    pass = false;
  }

  if(pass) {
    unit_apply(&ref, sd);
    unit_apply(&cand, sd);
    /* the two machines must be indistinguishable before the routine runs, or
     * nothing measured afterwards is the routine's doing */
    machine_snapshot(&ref);
    machine_snapshot(&cand);
    Region rr[REGION_COUNT], cr[REGION_COUNT];
    machine_regions(&ref, rr);
    machine_regions(&cand, cr);
    for(int k = 0; k < REGION_COUNT; k++) {
      if(memcmp(rr[k].data, cr[k].data, rr[k].len) != 0) {
        snprintf(detail, sizeof(detail), "the seeded machines differ in %s before the run",
                 rr[k].name);
        err = detail;
        pass = false;
        break;
      }
    }
  }

  uint64_t refCycles = 0, candCycles = 0;
  uint64_t refInstrs = 0, candInstrs = 0;
  uint32_t refApu = 0, candApu = 0;
  if(pass) {
    const uint64_t cycles0 = ref.snes->cycles;
    const uint32_t apu0 = ref.snes->apu->cycles;
    if(sd->spc) {
      uint8_t spEntry = ref.snes->apu->spc->sp;
      UnitWalk w = { true };
      unsigned long guard = 0;
      while(!unit_left_spc(&ref, sd, spEntry, &w)) {
        spc_runOpcode(ref.snes->apu->spc);
        if(++guard > UNIT_GUARD) { err = "the ROM's SPC700 routine never left its byte range"; pass = false; break; }
      }
    } else {
      uint16_t spEntry = ref.snes->cpu->sp;
      UnitWalk w = { true };
      unsigned long guard = 0;
      while(!unit_left_65816(&ref, sd, spEntry, &w)) {
        cpu_runOpcode(ref.snes->cpu);
        if(++guard > UNIT_GUARD) { err = "the ROM routine never left its byte range"; pass = false; break; }
      }
    }
    refCycles = ref.snes->cycles - cycles0;
    refApu = ref.snes->apu->cycles - apu0;


    if(pass) {
      /* The candidate runs the C body, held to the end of its routine
       * (ss_unit_hold: this machine has no frame boundary to hand the rest
       * back at, and the SPC700 side of the same gate gets the same effect for
       * free from the far-away slice end). The loop after it is the safety
       * net: if a body ever stops short, the ROM finishes the routine exactly
       * as it does in a hooked frame run, and the comparison still stands. */
      if(sd->spc) {
        const SpcInstalled* h = NULL;
        for(unsigned i = 0; i < spcCount; i++)
          if(strcmp(spcTable[i].name, sd->name) == 0) h = &spcTable[i];
        if(h == NULL) { err = "no SPC700 C body is registered under that name"; pass = false; }
        else {
          uint8_t spEntry = cand.snes->apu->spc->sp;
          sps_enter_hook(&cand.sps);
          h->fn(&cand.sps);
          sps_leave_hook(&cand.sps);
          UnitWalk w = { true };
          unsigned long guard = 0;
          while(!unit_left_spc(&cand, sd, spEntry, &w)) {
            spc_runOpcode(cand.snes->apu->spc);
            if(++guard > UNIT_GUARD) { err = "the C body's SPC700 routine never left its byte range"; pass = false; break; }
          }
        }
      } else {
        const Installed* h = NULL;
        for(unsigned i = 0; i < tableCount; i++)
          if(strcmp(table[i].name, sd->name) == 0) h = &table[i];
        if(h == NULL || h->fn == NULL) { err = "no C body is registered under that name"; pass = false; }
        else {
          uint16_t spEntry = cand.snes->cpu->sp;
          ss_unit_hold(&cand.ss, true);
          ss_enter_hook(&cand.ss);
          h->fn(&cand.ss);
          ss_leave_hook(&cand.ss);
          ss_unit_hold(&cand.ss, false);
          UnitWalk w = { true };
          unsigned long guard = 0;
          while(!unit_left_65816(&cand, sd, spEntry, &w)) {
            cpu_runOpcode(cand.snes->cpu);
            if(++guard > UNIT_GUARD) { err = "the C body's routine never left its byte range"; pass = false; break; }
          }
        }
      }
      candCycles = cand.snes->cycles - cycles0;
      candApu = cand.snes->apu->cycles - apu0;
      /* Instructions the emulated core still executed on the candidate: the
       * routine's own are the C body's, so what is left is a callee the port
       * has not converted, plus anything a body that stopped short handed
       * back. It is printed so the reading stays checkable rather than
       * asserted. */
      /* Both machines are fresh, so these counters start at zero and are the
       * run's own. */
      candInstrs = sd->spc ? cand.emulatedSpc : cand.emulated;
      refInstrs = sd->spc ? ref.emulatedSpc : ref.emulated;
    }
  }

  if(pass) {
    machine_snapshot(&ref);
    machine_snapshot(&cand);
    Region rr[REGION_COUNT], cr[REGION_COUNT];
    machine_regions(&ref, rr);
    machine_regions(&cand, cr);
    for(int k = 0; k < REGION_COUNT && pass; k++) {
      if(memcmp(rr[k].data, cr[k].data, rr[k].len) == 0) continue;
      for(size_t off = 0; off < rr[k].len; off++) {
        if(rr[k].data[off] == cr[k].data[off]) continue;
        snprintf(detail, sizeof(detail),
                 "%s differs at 0x%05zx: the ROM left %02X, the C body %02X",
                 rr[k].name, off, rr[k].data[off], cr[k].data[off]);
        break;
      }
      err = detail;
      pass = false;
    }
    if(pass) {
      UnitRegDiff d[4];
      int n = unit_reg_diffs(&ref, &cand, sd->spc, d, 4);
      if(n > 0) {
        snprintf(detail, sizeof(detail),
                 "%s differs: the ROM left %04X, the C body %04X%s",
                 d[0].name, d[0].ref, d[0].cand, n > 1 ? " (and more)" : "");
        err = detail;
        pass = false;
      }
    }
    if(pass && (refCycles != candCycles || refApu != candApu)) {
      snprintf(detail, sizeof(detail),
               "the ROM spent %" PRIu64 " master and %u APU cycles, the C body %" PRIu64 " and %u",
               refCycles, refApu, candCycles, candApu);
      err = detail;
      pass = false;
    }
  }

  printf("unit %s %-30s seed %d  %s@%d %s  %" PRIu64 " master %u apu, rom ran %"
         PRIu64 " instrs, C left %" PRIu64 "%s%s\n",
         pass ? "pass" : "FAIL", sd->name, seedIndex, boot->script, boot->frame,
         unit_ret_name(sd->ret), refCycles, refApu, refInstrs, candInstrs,
         sd->note[0] ? "  ; " : "", sd->note);
  if(!pass) printf("     %s\n", err != NULL ? err : "unknown failure");

  machine_free(&ref);
  machine_free(&cand);
  return pass;
}

static int run_unit_gate(const uint8_t* rom, size_t romLen, const char* specPath,
                         const char* onlyList, Installed* table, unsigned tableCount,
                         SpcInstalled* spcTable, unsigned spcCount) {
  UnitSeed* seeds = NULL;
  int count = 0;
  if(!unit_load(specPath, &seeds, &count)) return 2;

  /* resolve each name to a side and an entry address through the two registries */
  bool ok = true;
  for(int i = 0; i < count; i++) {
    UnitSeed* sd = &seeds[i];
    bool found = false;
    for(unsigned k = 0; k < tableCount && !found; k++) {
      if(strcmp(table[k].name, sd->name) != 0) continue;
      sd->spc = false;
      sd->entry = table[k].addr;
      found = true;
    }
    for(unsigned k = 0; k < spcCount && !found; k++) {
      if(strcmp(spcTable[k].name, sd->name) != 0) continue;
      sd->spc = true;
      sd->entry = spcTable[k].addr;
      found = true;
    }
    if(!found) {
      fprintf(stderr, "dream_harness: %s:%d: no routine named '%s' is registered\n",
              specPath, sd->line, sd->name);
      ok = false;
    } else if(sd->end <= sd->entry) {
      fprintf(stderr, "dream_harness: %s:%d: end=%X is not above %s's entry %X\n",
              specPath, sd->line, sd->end, sd->name, sd->entry);
      ok = false;
    }
  }
  if(!ok) { free(seeds); return 2; }

  int failures = 0, ran = 0;
  /* one boot per distinct (script, frame): booting is most of the work, and a
   * seed only needs the state it produced */
  bool* done = calloc((size_t) (count > 0 ? count : 1), 1);
  for(int i = 0; i < count; i++) {
    if(done[i]) continue;
    if(!name_in_list(onlyList, seeds[i].name)) { done[i] = true; continue; }
    UnitBoot boot;
    memset(&boot, 0, sizeof boot);
    if(!unit_boot(rom, romLen, seeds[i].script, seeds[i].frame, &boot)) {
      free(done); free(seeds);
      return 2;
    }
    for(int j = i; j < count; j++) {
      if(done[j]) continue;
      if(strcmp(seeds[j].script, boot.script) != 0 || seeds[j].frame != boot.frame) continue;
      done[j] = true;
      if(!name_in_list(onlyList, seeds[j].name)) continue;
      int seedIndex = 1;
      for(int k = 0; k < j; k++) if(strcmp(seeds[k].name, seeds[j].name) == 0) seedIndex++;
      if(!unit_run_seed(rom, romLen, &boot, &seeds[j], table, tableCount,
                        spcTable, spcCount, seedIndex)) failures++;
      ran++;
    }
    free(boot.data);
  }
  free(done);

  /* one line per routine, for tools/recomp_verify.py --units */
  int routines = 0;
  for(int i = 0; i < count; i++) {
    bool first = true;
    for(int k = 0; k < i; k++) if(strcmp(seeds[k].name, seeds[i].name) == 0) first = false;
    if(!first || !name_in_list(onlyList, seeds[i].name)) continue;
    int n = 0;
    for(int k = 0; k < count; k++) if(strcmp(seeds[k].name, seeds[i].name) == 0) n++;
    printf("unitok %s %d seeds\n", seeds[i].name, n);
    routines++;
  }
  printf("unit: %d seeds over %d routines, %d failed\n", ran, routines, failures);
  free(seeds);
  return failures == 0 ? 0 : 1;
}

static void print_frame_line(int frame, Machine* m, const Region* r) {
  const Cpu* c = m->snes->cpu;
  const Ppu* p = m->snes->ppu;
  printf("frame %6d wram=%016llx vram=%016llx cgram=%016llx oam=%016llx"
         " pc=%02X:%04X a=%04X x=%04X y=%04X db=%02X dp=%04X p=%02X sp=%04X"
         " inidisp=%02X mode=%u\n",
         frame,
         (unsigned long long) xxh64(r[0].data, r[0].len, 0),
         (unsigned long long) xxh64(r[1].data, r[1].len, 0),
         (unsigned long long) xxh64(r[2].data, r[2].len, 0),
         (unsigned long long) xxh64(r[3].data, r[3].len, 0),
         c->k, c->pc, c->a, c->x, c->y, c->db, c->dp,
         (unsigned) ((c->n << 7) | (c->v << 6) | (c->mf << 5) | (c->xf << 4) |
                     (c->d << 3) | (c->i << 2) | (c->z << 1) | c->c),
         c->sp,
         (unsigned) ((p->forcedBlank ? 0x80 : 0) | (p->brightness & 0xf)),
         p->mode);
}

int main(int argc, char** argv) {
  const char* romPath = "baserom/DREAM.sfc";
  const char* inputPath = NULL;
  const char* tracePath = NULL;
  const char* dumpDir = NULL;
  const char* profilePath = NULL;
  const char* cyclesPath = NULL;
  const char* onlyList = NULL;
  int frames = 600;
  bool hooksOn = false;
  bool lockstep = false;
  bool quiet = false;
  bool testCoro = false;
  bool testNesting = false;
  bool testSpcTiming = false;
  const char* unitPath = NULL;
  const char* unitOnly = NULL;
  bool spcHooksOn = false;
  bool noCpu = false;
  const char* tableName = "all";

  for(int i = 1; i < argc; i++) {
    const char* a = argv[i];
    bool hasNext = i + 1 < argc;
    if(strcmp(a, "--rom") == 0 && hasNext) romPath = argv[++i];
    else if(strcmp(a, "--frames") == 0 && hasNext) frames = atoi(argv[++i]);
    else if(strcmp(a, "--input") == 0 && hasNext) inputPath = argv[++i];
    else if(strcmp(a, "--trace") == 0 && hasNext) tracePath = argv[++i];
    else if(strcmp(a, "--dump-wram") == 0 && hasNext) dumpDir = argv[++i];
    else if(strcmp(a, "--hooks") == 0 && hasNext) {
      const char* v = argv[++i];
      if(strcmp(v, "on") == 0) hooksOn = true;
      else if(strcmp(v, "off") == 0) hooksOn = false;
      else { fprintf(stderr, "dream_harness: --hooks takes on|off\n"); return 2; }
    } else if(strcmp(a, "--spc-hooks") == 0 && hasNext) {
      const char* v = argv[++i];
      if(strcmp(v, "on") == 0) spcHooksOn = true;
      else if(strcmp(v, "off") == 0) spcHooksOn = false;
      else { fprintf(stderr, "dream_harness: --spc-hooks takes on|off\n"); return 2; }
    } else if(strcmp(a, "--hook-table") == 0 && hasNext) {
      const char* v = argv[++i];
      if(strcmp(v, "all") != 0 && strcmp(v, "demo") != 0 && strcmp(v, "empty") != 0) {
        fprintf(stderr, "dream_harness: --hook-table takes all|demo|empty\n");
        return 2;
      }
      tableName = v;
    }
    else if(strcmp(a, "--profile") == 0 && hasNext) profilePath = argv[++i];
    else if(strcmp(a, "--cycles") == 0 && hasNext) cyclesPath = argv[++i];
    else if(strcmp(a, "--only") == 0 && hasNext) onlyList = argv[++i];
    else if(strcmp(a, "--lockstep") == 0) lockstep = true;
    else if(strcmp(a, "--no-cpu") == 0) noCpu = true;
    else if(strcmp(a, "--test-coro") == 0) testCoro = true;
    else if(strcmp(a, "--test-nesting") == 0) testNesting = true;
    else if(strcmp(a, "--test-spc-timing") == 0) testSpcTiming = true;
    else if(strcmp(a, "--unit") == 0 && hasNext) unitPath = argv[++i];
    else if(strcmp(a, "--unit-only") == 0 && hasNext) unitOnly = argv[++i];
    else if(strcmp(a, "--quiet") == 0) quiet = true;
    else if(strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) { usage(); return 0; }
    else { fprintf(stderr, "dream_harness: unknown option %s\n", a); usage(); return 2; }
  }
  if(frames <= 0) { fprintf(stderr, "dream_harness: --frames must be positive\n"); return 2; }

  /* Before the ROM is opened: --test-coro exercises the coroutine backend and
   * nothing else, so it is the one mode that runs on a machine with no ROM --
   * which is every CI runner (.github/workflows/ci.yml). */
  if(testCoro) return run_coro_test();

  InputScript script = { NULL, 0 };
  if(inputPath != NULL && !input_load(&script, inputPath)) return 2;

  size_t romLen = 0;
  uint8_t* rom = read_file(romPath, &romLen);
  if(rom == NULL) return 2;

  if(testNesting) {
    int rc = run_nesting_test(rom, romLen);
    free(rom);
    free(script.ev);
    return rc;
  }

  if(testSpcTiming) {
    int rc = run_spc_timing_test(rom, romLen);
    free(rom);
    free(script.ev);
    return rc;
  }

  if(noCpu) {
    /* The mode is "the C bodies are the program", so every body has to be
     * installed on both processors; a narrowed table would report a routine the
     * port has as missing. --profile measures the ROM's own code, which is the
     * one thing this mode does not run. */
    if(profilePath != NULL) {
      fprintf(stderr, "dream_harness: --no-cpu and --profile are exclusive\n");
      return 2;
    }
    if(onlyList != NULL || strcmp(tableName, "all") != 0) {
      fprintf(stderr, "dream_harness: --no-cpu needs the whole table"
                      " (no --only, no --hook-table)\n");
      return 2;
    }
    hooksOn = true;
    spcHooksOn = true;
  }

  unsigned tableCount = 0;
  Installed* table = build_table(tableName, &tableCount);
  unsigned spcTableCount = 0;
  SpcInstalled* spcTable = build_spc_table(&spcTableCount);
  if(profilePath != NULL) { lockstep = false; hooksOn = false; }
  /* The charges are loaded for --profile too, and the measuring pass applies
   * them. That makes --profile a refinement step rather than a measurement from
   * scratch: an uncharged candidate drifts out of phase with the reference
   * within a few frames, and everything measured after that is measured on a
   * machine that is running the same code at a different point in the scanline,
   * which shows up as spurious DRAM-refresh differences. Run it, install the
   * result, run it again; the numbers settle. */
  {
    const char* cp = cyclesPath ? cyclesPath : "config/recomp_cycles.txt";
    if(cyclesPath == NULL || strcmp(cyclesPath, "none") != 0) {
      if(!load_cycles(table, tableCount, cp, cyclesPath != NULL)) return 2;
    }
  }

  if(unitPath != NULL) {
    /* The routine-level gate runs no frames of its own beyond the boot each
     * seed asks for, so none of the frame-loop options apply; it needs the
     * whole table on both processors, because a seeded routine reaches its
     * converted callees the same way it does in a frame run. */
    if(lockstep || noCpu || profilePath != NULL || onlyList != NULL) {
      fprintf(stderr, "dream_harness: --unit is its own mode: no --lockstep,"
                      " --no-cpu, --profile or --only (use --unit-only)\n");
      return 2;
    }
    int rc = run_unit_gate(rom, romLen, unitPath, unitOnly, table, tableCount,
                           spcTable, spcTableCount);
    free(rom);
    free(table);
    free(spcTable);
    free(script.ev);
    return rc;
  }

  if(onlyList != NULL) {
    unsigned kept = 0;
    for(unsigned i = 0; i < tableCount; i++) {
      if(name_in_list(onlyList, table[i].name)) table[kept++] = table[i];
    }
    tableCount = kept;
    /* --only names one routine on either side; the SPC table narrows the same
     * way, so a mismatch can be bisected down to a single sound-driver body. */
    kept = 0;
    for(unsigned i = 0; i < spcTableCount; i++) {
      if(name_in_list(onlyList, spcTable[i].name)) spcTable[kept++] = spcTable[i];
    }
    spcTableCount = kept;
  }

  Machine ref, cand;
  /* the reference never runs hooks; it still carries the table for --profile */
  machine_init(&ref, rom, romLen, table, tableCount, !lockstep && !profilePath && hooksOn,
               tracePath != NULL);
  /* The reference never runs SPC hooks either: --lockstep is precisely the
   * off-versus-on comparison, and --profile measures the ROM's own cost. */
  machine_install_spc(&ref, spcTable, spcTableCount,
                      !lockstep && !profilePath && spcHooksOn);
  bool twin = lockstep || profilePath != NULL;
  if(twin) {
    machine_init(&cand, rom, romLen, table, tableCount, true, false);
    machine_install_spc(&cand, spcTable, spcTableCount, lockstep ? spcHooksOn : false);
  }
  /* --no-cpu applies to the candidate under --lockstep -- the reference is
   * always the ROM running on both cores -- and to the single machine
   * otherwise. Enable it after the tables are installed: the SPC side steps in
   * front of the harness's own dispatcher. */
  if(noCpu) {
    Machine* nc = twin ? &cand : &ref;
    nc->nocpu = true;
    ss_nocpu_enable(&nc->ss, true);
    sps_nocpu_enable(&nc->sps, true);
  }
  if(profilePath != NULL) {
    unsigned n = tableCount ? tableCount : 1;
    ref.profiling = true;
    ref.profCycles = calloc(n, sizeof(uint64_t));
    ref.profCalls = calloc(n, sizeof(uint64_t));
    ref.profMin = calloc(n, sizeof(uint64_t));
    ref.profMax = calloc(n, sizeof(uint64_t));
    ref.profNoReturn = calloc(n, 1);
    ref.profRomTail = calloc(n, 1);
    cand.measureSpend = true;
    cand.profCycles = calloc(n, sizeof(uint64_t));
    cand.profCalls = calloc(n, sizeof(uint64_t));
    cand.profMin = calloc(n, sizeof(uint64_t));
    cand.profMax = calloc(n, sizeof(uint64_t));
    cand.profNoReturn = calloc(n, 1);
    cand.profRomTail = calloc(n, 1);
  }
  free(rom);

  int charged = 0;
  for(unsigned i = 0; i < tableCount; i++) if(table[i].cycleCost > 0) charged++;

  printf("dream_harness: %s, HiROM forced, 2 MiB, no SRAM, NTSC\n", romPath);
  printf("mode: %s, hooks %s, spc-hooks %s, table %s (%u entr%s, %d cycle-costed,"
         " %u spc), frames %d, input %s\n",
         profilePath ? "profile (hooks off, measuring)" :
           lockstep ? (noCpu ? "lockstep (reference=both CPUs, candidate=no-cpu)"
                             : "lockstep (reference=hooks off, candidate=hooks on)")
                    : (noCpu ? "single (no-cpu)" : "single"),
         lockstep || hooksOn ? "on" : "off",
         spcHooksOn ? "on" : "off",
         tableName, tableCount, tableCount == 1 ? "y" : "ies", charged, spcTableCount,
         frames, inputPath ? inputPath : "(none)");

  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);

  int status = 0;
  int ranFrames = 0;
  for(int frame = 0; frame < frames; frame++) {
    uint16_t state, state2;
    input_state_at(&script, frame, &state, &state2);
    machine_set_input(&ref, state, state2);
    machine_run_frame(&ref);
    machine_snapshot(&ref);
    Region rr[REGION_COUNT];
    machine_regions(&ref, rr);

    if(twin) {
      machine_set_input(&cand, state, state2);
      machine_run_frame(&cand);
      machine_snapshot(&cand);
      Region cr[REGION_COUNT];
      machine_regions(&cand, cr);
      for(int k = 0; k < REGION_COUNT && lockstep && status == 0; k++) {
        if(memcmp(rr[k].data, cr[k].data, rr[k].len) == 0) continue;
        for(size_t off = 0; off < rr[k].len; off++) {
          if(rr[k].data[off] == cr[k].data[off]) continue;
          printf("MISMATCH frame %d region %s offset 0x%05zx expected %02X got %02X"
                 " (candidate is %+" PRId64 " master cycles, %+" PRId64 " APU cycles)\n",
                 frame, rr[k].name, off, rr[k].data[off], cr[k].data[off],
                 (int64_t) cand.snes->cycles - (int64_t) ref.snes->cycles,
                 (int64_t) cand.snes->apu->cycles - (int64_t) ref.snes->apu->cycles);
          break;
        }
        status = 1;
      }
    }

    if(!quiet) print_frame_line(frame, &ref, rr);

    if(dumpDir != NULL) {
      char path[1024];
      snprintf(path, sizeof(path), "%s/wram_%06d.bin", dumpDir, frame);
      FILE* wf = fopen(path, "wb");
      if(wf == NULL) {
        fprintf(stderr, "dream_harness: cannot write %s: %s\n", path, strerror(errno));
        status = 2;
        ranFrames = frame + 1;
        break;
      }
      fwrite(ref.snes->ram, 1, WRAM_SIZE, wf);
      fclose(wf);
    }

    ranFrames = frame + 1;
    if(status != 0) break;
  }

  clock_gettime(CLOCK_MONOTONIC, &t1);
  double secs = (double) (t1.tv_sec - t0.tv_sec) + (double) (t1.tv_nsec - t0.tv_nsec) / 1e9;

  if(tracePath != NULL && !trace_write(&ref, tracePath)) status = 2;
  if(profilePath != NULL) {
    /* A frame still open when the run ended never returned, so there is nothing
     * to measure: billing it would invent a sample out of a routine the run cut
     * in half. Report the count instead. */
    ref.profOpen += (unsigned long) ref.profDepth;
    ref.profDepth = 0;
    if(ref.profOpen != 0 || ref.profDropped != 0)
      printf("profile: %lu routine%s still running when the run ended (not billed)"
             ", %lu entr%s not measured (frame stack full)\n",
             ref.profOpen, ref.profOpen == 1 ? "" : "s",
             ref.profDropped, ref.profDropped == 1 ? "y" : "ies");
    if(!profile_write(&ref, &cand, profilePath)) status = 2;
  }

  if(lockstep) {
    if(status == 0) {
      printf("lockstep: %d frames, no mismatches (candidate ended %+" PRId64
             " master cycles and %+" PRId64 " APU cycles from the reference)\n", ranFrames,
             (int64_t) cand.snes->cycles - (int64_t) ref.snes->cycles,
             (int64_t) cand.snes->apu->cycles - (int64_t) ref.snes->apu->cycles);
    }
    else printf("lockstep: FAILED after %d frames\n", ranFrames);
  }
  if(lockstep || hooksOn) {
    /* Hooks nest, and the depth is worth reporting: it is the number of entry
     * snapshots that have to be kept apart at once (harness/ss_internal.h). */
    printf("hook nesting: max %d hook%s in flight at once\n",
           ss_hook_max_depth(twin ? &cand.ss : &ref.ss),
           ss_hook_max_depth(twin ? &cand.ss : &ref.ss) == 1 ? "" : "s");
    for(unsigned i = 0; i < tableCount; i++) {
      printf("hook %06X %-32s %lu call%s\n", table[i].addr, table[i].name,
             recomp_hook_hits[i], recomp_hook_hits[i] == 1 ? "" : "s");
    }
  }
  if(lockstep || spcHooksOn) {
    printf("spc hook nesting: max %d hook%s in flight at once\n",
           sps_hook_max_depth(twin ? &cand.sps : &ref.sps),
           sps_hook_max_depth(twin ? &cand.sps : &ref.sps) == 1 ? "" : "s");
    for(unsigned i = 0; i < spcTableCount; i++) {
      printf("spchook %04X %-32s %lu call%s\n", spcTable[i].addr, spcTable[i].name,
             recomp_spc_hook_hits[i], recomp_spc_hook_hits[i] == 1 ? "" : "s");
    }
  }
  if(noCpu) {
    const Machine* nc = twin ? &cand : &ref;
    /* The whole claim of the mode, as a measurement rather than an assertion:
     * the number of instructions the emulated processors fetched and executed.
     * Every one of them would have been a hook that declined, and in this mode
     * a decline is a fatal error, so the only figure that can survive to here
     * is zero -- except for the SPC700's IPL boot ROM, which is the console's
     * firmware and has no body (see recomp/README.md). */
    printf("no-cpu: %" PRIu64 " 65816 instructions and %" PRIu64 " SPC700 instructions"
           " executed by the emulated cores (%" PRIu64 " of them the IPL boot ROM)\n",
           nc->emulated, nc->emulatedSpc, sps_nocpu_ipl_instructions(&nc->sps));
    printf("no-cpu: %" PRIu64 " bodies dispatched, %" PRIu64 " suspended at a frame"
           " boundary or an interrupt, %" PRIu64 " abandoned by an interrupt,"
           " %d stack%s at once\n",
           ss_nocpu_dispatches(&nc->ss), ss_nocpu_suspensions(&nc->ss),
           ss_nocpu_abandoned(&nc->ss), ss_nocpu_max_contexts(&nc->ss),
           ss_nocpu_max_contexts(&nc->ss) == 1 ? "" : "s");
    printf("no-cpu: spc %" PRIu64 " bodies dispatched, %" PRIu64 " suspended at a"
           " catch-up slice boundary\n",
           sps_nocpu_dispatches(&nc->sps), sps_nocpu_suspensions(&nc->sps));
    if(twin)
      printf("no-cpu: the reference executed %" PRIu64 " 65816 and %" PRIu64
             " SPC700 instructions over the same run\n", ref.emulated, ref.emulatedSpc);
  }
  /* ref.instructions counts the instruction boundaries the dispatcher was
   * offered. With a CPU running that is the instruction count; in a --no-cpu
   * run without a reference machine there are no instructions and the same
   * counter is the number of bodies the scheduler entered, so say which. */
  printf("%d frames in %.3f s = %.1f fps (%.2fx realtime), %" PRIu64 " %s\n",
         ranFrames, secs, secs > 0 ? ranFrames / secs : 0.0,
         secs > 0 ? (ranFrames / secs) / 60.0988 : 0.0, ref.instructions,
         noCpu && !twin ? "body dispatches" : "instructions");

  machine_free(&ref);
  if(twin) machine_free(&cand);
  free(table);
  free(spcTable);
  free(script.ev);
  return status;
}
