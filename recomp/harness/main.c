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
#include "xxh64.h"

#define ROM_SIZE   0x200000u   /* 2 MiB */
#define WRAM_SIZE  0x20000u    /* 128 KB */
#define VRAM_BYTES 0x10000u
#define CGRAM_BYTES 0x200u
#define OAM_BYTES  (0x200u + 0x20u)

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

/* Parse "frame Button+Button+..." lines; a button set is held until the next
 * line changes it. "-", "none" and an empty button list mean no buttons. */
static bool input_load(InputScript* s, const char* path) {
  FILE* f = fopen(path, "r");
  if(f == NULL) {
    fprintf(stderr, "dream_harness: cannot open input script %s: %s\n", path, strerror(errno));
    return false;
  }
  int cap = 16;
  s->ev = malloc((size_t) cap * sizeof(InputEvent));
  s->count = 0;
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
    uint16_t state = 0;
    /* tokenize the rest on '+', space and tab */
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
        fclose(f);
        return false;
      }
    }
    if(s->count == cap) {
      cap *= 2;
      s->ev = realloc(s->ev, (size_t) cap * sizeof(InputEvent));
    }
    s->ev[s->count].frame = (int) frame;
    s->ev[s->count].state = state;
    s->count++;
  }
  fclose(f);
  return true;
}

static uint16_t input_state_at(const InputScript* s, int frame) {
  uint16_t state = 0;
  for(int i = 0; i < s->count && s->ev[i].frame <= frame; i++) state = s->ev[i].state;
  return state;
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

/* One in-flight routine while --profile is running. */
typedef struct {
  unsigned idx;              /* index into Machine.hooks */
  uint16_t sp;               /* stack pointer at the routine's first instruction */
  uint64_t cycles;           /* master cycle count at that instant */
} ProfFrame;

#define PROF_DEPTH 64

typedef struct {
  Snes* snes;
  SnesState ss;
  Installed* hooks;          /* NULL when nothing is installed */
  unsigned hookCount;
  bool hooksActive;          /* false for the profile pass and --hooks off */
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
  /* serialisation scratch */
  uint8_t vram[VRAM_BYTES];
  uint8_t cgram[CGRAM_BYTES];
  uint8_t oam[OAM_BYTES];
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

/* --profile: close every frame whose routine has returned. A routine has
 * returned when the stack pointer is back above where it stood at the routine's
 * first instruction: rts/rtl pop exactly that far, while an interrupt taken
 * inside the routine pushes below it and so cannot end a frame early. The
 * cycles a nested callee costs are billed to the outer routine too, which is
 * what a hook replacing the whole call tree needs to be charged. */
static void prof_close(Machine* m, uint16_t sp) {
  while(m->profDepth > 0 && sp > m->profStack[m->profDepth - 1].sp) {
    const ProfFrame* f = &m->profStack[--m->profDepth];
    uint64_t d = m->snes->cycles - f->cycles;
    if(getenv("DREAM_PROF_TRACE") != NULL)
      fprintf(stderr, "rom  %-28s start=%" PRIu64 " cost=%" PRIu64 "\n",
              m->hooks[f->idx].name, f->cycles, d);
    m->profCycles[f->idx] += d;
    if(m->profCalls[f->idx] == 0 || d < m->profMin[f->idx]) m->profMin[f->idx] = d;
    if(d > m->profMax[f->idx]) m->profMax[f->idx] = d;
    m->profCalls[f->idx]++;
  }
}

static bool harness_hook(void* ctx, Cpu* cpu, uint32_t pc24) {
  Machine* m = (Machine*) ctx;
  m->instructions++;
  if(m->cov != NULL) m->cov[pc24 >> 3] |= (uint8_t) (1u << (pc24 & 7));
  if(m->hooks == NULL) return false;

  if(m->profiling) {
    prof_close(m, cpu->sp);
    int idx = machine_find_hook(m, pc24);
    if(idx >= 0 && m->profDepth < PROF_DEPTH) {
      m->profStack[m->profDepth].idx = (unsigned) idx;
      m->profStack[m->profDepth].sp = cpu->sp;
      m->profStack[m->profDepth].cycles = m->snes->cycles;
      m->profDepth++;
    }
    return false;
  }

  if(!m->hooksActive) return false;
  int idx = machine_find_hook(m, pc24);
  if(idx < 0) return false;
  const Installed* h = &m->hooks[idx];
  uint64_t before = m->snes->cycles;
  uint16_t spBefore = cpu->sp;
  m->ss.entryVblank = m->snes->inVblank;
  m->ss.entryFrames = m->snes->frames;
  if(h->fn != NULL) {
    h->fn(&m->ss);
  } else if(!h->hookFn(&m->ss)) {
    return false;                       /* the hook declined */
  }
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
}

static void machine_free(Machine* m) {
  free(m->cov);
  free(m->profCycles);
  free(m->profCalls);
  free(m->profMin);
  free(m->profMax);
  free(m->profNoReturn);
  snes_free(m->snes);
}

static void machine_set_input(Machine* m, uint16_t state) {
  m->snes->input1->currentState = state;
  m->snes->input2->currentState = 0;
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
}

typedef struct {
  const char* name;
  const uint8_t* data;
  size_t len;
} Region;

static void machine_regions(Machine* m, Region out[4]) {
  out[0] = (Region) { "wram",  m->snes->ram, WRAM_SIZE };
  out[1] = (Region) { "vram",  m->vram,      VRAM_BYTES };
  out[2] = (Region) { "cgram", m->cgram,     CGRAM_BYTES };
  out[3] = (Region) { "oam",   m->oam,       OAM_BYTES };
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
    "  --hooks on|off     install the recomp routines (default off)\n"
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

  unsigned tableCount = 0;
  Installed* table = build_table(tableName, &tableCount);
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
  }

  Machine ref, cand;
  /* the reference never runs hooks; it still carries the table for --profile */
  machine_init(&ref, rom, romLen, table, tableCount, !lockstep && !profilePath && hooksOn,
               tracePath != NULL);
  bool twin = lockstep || profilePath != NULL;
  if(twin) machine_init(&cand, rom, romLen, table, tableCount, true, false);
  if(profilePath != NULL) {
    unsigned n = tableCount ? tableCount : 1;
    ref.profiling = true;
    ref.profCycles = calloc(n, sizeof(uint64_t));
    ref.profCalls = calloc(n, sizeof(uint64_t));
    ref.profMin = calloc(n, sizeof(uint64_t));
    ref.profMax = calloc(n, sizeof(uint64_t));
    ref.profNoReturn = calloc(n, 1);
    cand.measureSpend = true;
    cand.profCycles = calloc(n, sizeof(uint64_t));
    cand.profCalls = calloc(n, sizeof(uint64_t));
    cand.profMin = calloc(n, sizeof(uint64_t));
    cand.profMax = calloc(n, sizeof(uint64_t));
    cand.profNoReturn = calloc(n, 1);
  }
  free(rom);

  int charged = 0;
  for(unsigned i = 0; i < tableCount; i++) if(table[i].cycleCost > 0) charged++;

  printf("dream_harness: %s, HiROM forced, 2 MiB, no SRAM, NTSC\n", romPath);
  printf("mode: %s, hooks %s, table %s (%u entr%s, %d cycle-costed), frames %d, input %s\n",
         profilePath ? "profile (hooks off, measuring)" :
           lockstep ? "lockstep (reference=hooks off, candidate=hooks on)" : "single",
         lockstep || hooksOn ? "on" : "off",
         tableName, tableCount, tableCount == 1 ? "y" : "ies", charged,
         frames, inputPath ? inputPath : "(none)");

  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);

  int status = 0;
  int ranFrames = 0;
  for(int frame = 0; frame < frames; frame++) {
    uint16_t state = input_state_at(&script, frame);
    machine_set_input(&ref, state);
    snes_runFrame(ref.snes);
    machine_snapshot(&ref);
    Region rr[4];
    machine_regions(&ref, rr);

    if(twin) {
      machine_set_input(&cand, state);
      snes_runFrame(cand.snes);
      machine_snapshot(&cand);
      Region cr[4];
      machine_regions(&cand, cr);
      for(int k = 0; k < 4 && lockstep && status == 0; k++) {
        if(memcmp(rr[k].data, cr[k].data, rr[k].len) == 0) continue;
        for(size_t off = 0; off < rr[k].len; off++) {
          if(rr[k].data[off] == cr[k].data[off]) continue;
          printf("MISMATCH frame %d region %s offset 0x%05zx expected %02X got %02X"
                 " (candidate is %+" PRId64 " master cycles)\n",
                 frame, rr[k].name, off, rr[k].data[off], cr[k].data[off],
                 (int64_t) cand.snes->cycles - (int64_t) ref.snes->cycles);
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
    prof_close(&ref, 0xffff);           /* bill whatever is still on the stack */
    if(!profile_write(&ref, &cand, profilePath)) status = 2;
  }

  if(lockstep) {
    if(status == 0) {
      printf("lockstep: %d frames, no mismatches (candidate ended %+" PRId64
             " master cycles from the reference)\n", ranFrames,
             (int64_t) cand.snes->cycles - (int64_t) ref.snes->cycles);
    }
    else printf("lockstep: FAILED after %d frames\n", ranFrames);
  }
  if(lockstep || hooksOn) {
    for(unsigned i = 0; i < tableCount; i++) {
      printf("hook %06X %-32s %lu call%s\n", table[i].addr, table[i].name,
             recomp_hook_hits[i], recomp_hook_hits[i] == 1 ? "" : "s");
    }
  }
  printf("%d frames in %.3f s = %.1f fps (%.2fx realtime), %" PRIu64 " instructions\n",
         ranFrames, secs, secs > 0 ? ranFrames / secs : 0.0,
         secs > 0 ? (ranFrames / secs) / 60.0988 : 0.0, ref.instructions);

  machine_free(&ref);
  if(twin) machine_free(&cand);
  free(table);
  free(script.ev);
  return status;
}
