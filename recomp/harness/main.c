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

static bool harness_hook(void* ctx, Cpu* cpu, uint32_t pc24) {
  Machine* m = (Machine*) ctx;
  m->instructions++;
  if(m->cov != NULL) m->cov[pc24 >> 3] |= (uint8_t) (1u << (pc24 & 7));
  if(m->hooks == NULL) return false;

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
    return false;
  }

  if(!m->hooksActive) return false;
  int idx = machine_find_hook(m, pc24);
  if(idx < 0) return false;
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
    return false;                       /* the hook declined */
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
  if(!m->spcHooksActive || m->spcHooks == NULL) return false;
  for(unsigned i = 0; i < m->spcHookCount; i++) {
    if(m->spcHooks[i].addr != pc) continue;
    sps_enter_hook(&m->sps);
    m->spcHooks[i].fn(&m->sps);
    sps_leave_hook(&m->sps);
    recomp_spc_hook_hits[i]++;
    return true;
  }
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
  free(m->cov);
  free(m->profCycles);
  free(m->profCalls);
  free(m->profMin);
  free(m->profMax);
  free(m->profNoReturn);
  free(m->profRomTail);
  snes_free(m->snes);
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
    "  --test-nesting     self-test: a hook entered inside another hook's callee\n"
    "                     keeps its own yield snapshot (exits 0 on pass, 1 on fail)\n"
    "  --test-spc-timing  self-test: sps_op_cycles() against the SPC700 core's own\n"
    "                     opcode timing (exits 0 on pass, 1 on fail)\n"
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
  bool testNesting = false;
  bool testSpcTiming = false;
  bool spcHooksOn = false;
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
    else if(strcmp(a, "--test-nesting") == 0) testNesting = true;
    else if(strcmp(a, "--test-spc-timing") == 0) testSpcTiming = true;
    else if(strcmp(a, "--quiet") == 0) quiet = true;
    else if(strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) { usage(); return 0; }
    else { fprintf(stderr, "dream_harness: unknown option %s\n", a); usage(); return 2; }
  }
  if(frames <= 0) { fprintf(stderr, "dream_harness: --frames must be positive\n"); return 2; }

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
           lockstep ? "lockstep (reference=hooks off, candidate=hooks on)" : "single",
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
    snes_runFrame(ref.snes);
    machine_snapshot(&ref);
    Region rr[REGION_COUNT];
    machine_regions(&ref, rr);

    if(twin) {
      machine_set_input(&cand, state, state2);
      snes_runFrame(cand.snes);
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
  printf("%d frames in %.3f s = %.1f fps (%.2fx realtime), %" PRIu64 " instructions\n",
         ranFrames, secs, secs > 0 ? ranFrames / secs : 0.0,
         secs > 0 ? (ranFrames / secs) / 60.0988 : 0.0, ref.instructions);

  machine_free(&ref);
  if(twin) machine_free(&cand);
  free(table);
  free(spcTable);
  free(script.ev);
  return status;
}
