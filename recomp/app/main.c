/* dream: the native application.
 *
 * One window, one game. It loads the user's own DREAM.sfc, installs every
 * routine the recomp has registered (recomp/src), and runs the reference core
 * at the SNES's own 60.0988 Hz with picture, sound and a modern controller.
 *
 * There is no launcher and no settings file: docs/RECOMP.md fixes the controller
 * mapping and this file is where that mapping lives. The one piece of app UI is
 * the menu bar across the top of the window (menubar.c: File, View and Gallery)
 * and the gallery pages it opens (gallery.c), which pause the game and draw into
 * the same 256x224 framebuffer. Neither ever writes emulator state.
 *
 * The hidden flags exist so that this binary can be checked against dream_harness:
 * same ROM, same script, same hook table, and the frame line printed at the end is
 * byte-identical to the harness's. They exist as proof that the platform layer
 * changed nothing:
 *
 *   --frames N --input FILE     run a harness script and print the frame line
 *   --screenshot FILE           dump the 256x224 framebuffer as a binary PPM
 *   --gallery SEC[:NAV]         open a gallery page (for the screenshot)
 *   --gallery-toggle N,ITERS    open the gallery mid-run and close it again, so a
 *                               run with the toggle can be diffed against one without
 */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>

#ifdef _WIN32
/* GetModuleFileNameA, for the ROM sitting next to dream.exe (find_rom below).
 * Nothing else in the app is Windows-specific: SDL covers the window, the
 * sound, the pads and the 60.0988 Hz pacing, and the coroutine backend the
 * --no-cpu scheduler needs is chosen by CMake (recomp/harness/coro.h). */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <SDL3/SDL.h>

#include "snes.h"
#include "cart.h"
#include "cpu.h"
#include "ppu.h"

#include "ss_internal.h"
#include "sps_internal.h"
#include "xxh64.h"
#include "sha1.h"
#include "dream_cycles.h"   /* generated from config/recomp_cycles.txt by CMake */
#include "gallery.h"
#include "menubar.h"
#include "music.h"
#include "scene.h"
#include "coro.h"   /* coro_backend(), for the log line naming the backend */
#include "dlog.h"   /* dream.log: the run, and the crash, written down */

#define ROM_SIZE    0x200000u
#define WRAM_SIZE   0x20000u
#define VRAM_BYTES  0x10000u
#define CGRAM_BYTES 0x200u
#define OAM_BYTES   (0x200u + 0x20u)

#define ROM_SHA1 "2675d7afe886f20462337aa1ee3aa5c3135fff3a"

/* The picture. The core renders 512x480 (two subpixels per dot, two fields per
 * line); the SNES's own signal is 256x224, which is what the window shows. */
#define SRC_W 512
#define SRC_H 480
#define FB_W  256
#define FB_H  224

/* 60.0988 Hz: the NTSC SNES's frame rate, and the rate the harness measures
 * everything against. */
#define FRAME_NS 16639250u   /* 1e9 / 60.0988, rounded */

/* The DSP produces 534 stereo samples per NTSC frame. Declaring the stream at
 * 534 * 60.0988 Hz means a frame's worth of samples is a frame's worth of time,
 * so the queue neither starves nor grows while the game runs on schedule; SDL
 * resamples that to whatever the device wants. */
#define AUDIO_SAMPLES_PER_FRAME 534
#define AUDIO_HZ                32093   /* 534 * 60.0988 */
#define AUDIO_QUEUE_TARGET      (AUDIO_SAMPLES_PER_FRAME * 3)

/* SNES button bit order, as snes_setButtonState() numbers it. */
enum {
  BTN_B = 0, BTN_Y, BTN_SELECT, BTN_START,
  BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT,
  BTN_A, BTN_X, BTN_L, BTN_R
};

/* ---- installed routines ------------------------------------------------ */

/* One recomped routine, as dream_harness installs it. */
typedef struct {
  uint32_t addr;
  const char* name;
  RecompFn fn;
  int cycleCost;
} Installed;

/* One recomped SPC700 routine. No cycle charge: an SPC body models its own
 * instruction stream, so there is nothing to calibrate. */
typedef struct {
  uint16_t addr;
  const char* name;
  SpcRecompFn fn;
} SpcInstalled;

typedef struct {
  Snes* snes;
  SnesState ss;
  SpcState sps;
  Installed* hooks;
  unsigned hookCount;
  SpcInstalled* spcHooks;
  unsigned spcHookCount;
  /* scratch for the frame hash (--frames only) */
  uint8_t vram[VRAM_BYTES];
  uint8_t cgram[CGRAM_BYTES];
  uint8_t oam[OAM_BYTES];
} Machine;

/* Canonical ROM address for a 24-bit PC, in the disassembly's $C0:0000+offset
 * form. Identical to the harness's: the ROM runs most of its code through the
 * $80/$81 mirror banks, so the PC is folded before a hook address is matched. */
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
  uint32_t key = pc24;
  if(!canon_rom_addr(pc24, &key)) key = pc24;
  for(unsigned i = 0; i < m->hookCount; i++) {
    if(m->hooks[i].addr == key) return (int) i;
  }
  return -1;
}

/* The dispatcher, kept deliberately identical to dream_harness's hooks-on path:
 * same fold, same entry snapshot, same hit counter, same cycle charge, charged
 * only when the routine actually returned. Anything else here would make the two
 * binaries different machines and the hash comparison meaningless. */
static bool app_hook(void* ctx, Cpu* cpu, uint32_t pc24) {
  Machine* m = (Machine*) ctx;
  int idx = machine_find_hook(m, pc24);
  if(idx < 0) return false;
  const Installed* h = &m->hooks[idx];
  uint16_t spBefore = cpu->sp;
  ss_enter_hook(&m->ss);
  h->fn(&m->ss);
  ss_leave_hook(&m->ss);
  recomp_hook_hits[idx]++;
  if(h->cycleCost > 0 && cpu->sp > spBefore) ss_consume_cycles(&m->ss, h->cycleCost);
  return true;
}

/* The SPC700 dispatcher, again identical to dream_harness's: the sound driver's
 * converted routines run in the app exactly as they do under the gate. */
static bool app_spc_hook(void* ctx, Spc* spc, uint16_t pc) {
  Machine* m = (Machine*) ctx;
  (void) spc;
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

/* Force the cart to HiROM, 2 MiB, no SRAM. See the note in harness/main.c: this
 * ROM's internal header at $ffc0 is overwritten by tilemap data, so
 * snes_loadRom()'s header scoring has nothing to score. */
static bool machine_load_rom(Machine* m, const uint8_t* rom, size_t len) {
  if(len != ROM_SIZE) {
    fprintf(stderr, "dream: expected a %u byte ROM, got %zu\n", ROM_SIZE, len);
    return false;
  }
  cart_load(m->snes->cart, 2 /* HiROM */, (uint8_t*) rom, (int) len, 0 /* no SRAM */);
  snes_reset(m->snes, true);
  m->snes->palTiming = false;
  return true;
}

static Installed* build_table(unsigned* countOut) {
  unsigned n = 0;
  const RecompEntry* reg = recomp_registry(&n);
  Installed* out = calloc(n ? n : 1, sizeof(Installed));
  for(unsigned i = 0; i < n; i++) {
    out[i].addr = reg[i].addr;
    out[i].name = reg[i].name;
    out[i].fn = reg[i].fn;
    for(unsigned k = 0; k < kRecompCyclesCount; k++) {
      if(kRecompCycles[k].addr == reg[i].addr) out[i].cycleCost = kRecompCycles[k].cycles;
    }
  }
  *countOut = n;
  return out;
}

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

static void machine_set_input(Machine* m, uint16_t state, uint16_t state2) {
  for(int b = 0; b < 12; b++) {
    snes_setButtonState(m->snes, 1, b, (state >> b) & 1);
    snes_setButtonState(m->snes, 2, b, (state2 >> b) & 1);
  }
}

/* ---- the frame hash, for --frames ------------------------------------- */

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

/* Byte-for-byte the harness's per-frame line, so the two runs can be diffed. */
static void print_frame_line(int frame, Machine* m) {
  const Cpu* c = m->snes->cpu;
  const Ppu* p = m->snes->ppu;
  printf("frame %6d wram=%016llx vram=%016llx cgram=%016llx oam=%016llx"
         " pc=%02X:%04X a=%04X x=%04X y=%04X db=%02X dp=%04X p=%02X sp=%04X"
         " inidisp=%02X mode=%u\n",
         frame,
         (unsigned long long) xxh64(m->snes->ram, WRAM_SIZE, 0),
         (unsigned long long) xxh64(m->vram, VRAM_BYTES, 0),
         (unsigned long long) xxh64(m->cgram, CGRAM_BYTES, 0),
         (unsigned long long) xxh64(m->oam, OAM_BYTES, 0),
         c->k, c->pc, c->a, c->x, c->y, c->db, c->dp,
         (unsigned) ((c->n << 7) | (c->v << 6) | (c->mf << 5) | (c->xf << 4) |
                     (c->d << 3) | (c->i << 2) | (c->z << 1) | c->c),
         c->sp,
         (unsigned) ((p->forcedBlank ? 0x80 : 0) | (p->brightness & 0xf)),
         p->mode);
}

/* ---- input scripts (--input; the harness's format) --------------------- */

static const struct { const char* name; int bit; } kButtons[] = {
  {"B", BTN_B}, {"Y", BTN_Y}, {"Select", BTN_SELECT}, {"Start", BTN_START},
  {"Up", BTN_UP}, {"Down", BTN_DOWN}, {"Left", BTN_LEFT}, {"Right", BTN_RIGHT},
  {"A", BTN_A}, {"X", BTN_X}, {"L", BTN_L}, {"R", BTN_R},
};

typedef struct { int frame; uint16_t state; uint16_t state2; } InputEvent;
typedef struct { InputEvent* ev; int count; } InputScript;

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
      fprintf(stderr, "dream: %s:%d: unknown button '%s'\n", path, lineno, tok);
      return false;
    }
  }
  *outState = state;
  return true;
}

/* "frame Buttons" or "frame Buttons | Buttons2" (harness format,
 * recomp/README.md); the optional second column addresses player 2. */
static bool input_load(InputScript* s, const char* path) {
  FILE* f = fopen(path, "r");
  if(f == NULL) {
    fprintf(stderr, "dream: cannot open input script %s: %s\n", path, strerror(errno));
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
      fprintf(stderr, "dream: %s:%d: expected a frame number\n", path, lineno);
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

/* ---- the fixed mapping (docs/RECOMP.md) -------------------------------- */

/* Left stick counts as a d-pad past half deflection. */
#define STICK_DEADZONE 16384   /* 0.5 * 32767, rounded up */

static uint16_t read_gamepad(SDL_Gamepad* pad) {
  if(pad == NULL) return 0;
  uint16_t s = 0;
  if(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_SOUTH))          s |= 1u << BTN_B;
  if(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_EAST))           s |= 1u << BTN_A;
  if(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_WEST))           s |= 1u << BTN_Y;
  if(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_NORTH))          s |= 1u << BTN_X;
  if(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER))  s |= 1u << BTN_L;
  if(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) s |= 1u << BTN_R;
  if(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_START))          s |= 1u << BTN_START;
  if(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_BACK))           s |= 1u << BTN_SELECT;
  if(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_UP))        s |= 1u << BTN_UP;
  if(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN))      s |= 1u << BTN_DOWN;
  if(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT))      s |= 1u << BTN_LEFT;
  if(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT))     s |= 1u << BTN_RIGHT;
  int lx = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX);
  int ly = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
  if(lx <= -STICK_DEADZONE) s |= 1u << BTN_LEFT;
  if(lx >=  STICK_DEADZONE) s |= 1u << BTN_RIGHT;
  if(ly <= -STICK_DEADZONE) s |= 1u << BTN_UP;
  if(ly >=  STICK_DEADZONE) s |= 1u << BTN_DOWN;
  return s;
}

static uint16_t read_keyboard(void) {
  const bool* k = SDL_GetKeyboardState(NULL);
  uint16_t s = 0;
  if(k[SDL_SCANCODE_UP])     s |= 1u << BTN_UP;
  if(k[SDL_SCANCODE_DOWN])   s |= 1u << BTN_DOWN;
  if(k[SDL_SCANCODE_LEFT])   s |= 1u << BTN_LEFT;
  if(k[SDL_SCANCODE_RIGHT])  s |= 1u << BTN_RIGHT;
  if(k[SDL_SCANCODE_Z])      s |= 1u << BTN_B;
  if(k[SDL_SCANCODE_X])      s |= 1u << BTN_A;
  if(k[SDL_SCANCODE_A])      s |= 1u << BTN_Y;
  if(k[SDL_SCANCODE_S])      s |= 1u << BTN_X;
  if(k[SDL_SCANCODE_Q])      s |= 1u << BTN_L;
  if(k[SDL_SCANCODE_W])      s |= 1u << BTN_R;
  if(k[SDL_SCANCODE_RETURN]) s |= 1u << BTN_START;
  if(k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT]) s |= 1u << BTN_SELECT;
  return s;
}

/* The first connected pad is player 1, the second is player 2 (same fixed
 * mapping, docs/RECOMP.md); a third is ignored until one of the first two
 * goes. */
static void open_gamepads(SDL_Gamepad** pad1, SDL_Gamepad** pad2) {
  int count = 0;
  SDL_JoystickID* ids = SDL_GetGamepads(&count);
  *pad1 = NULL;
  *pad2 = NULL;
  if(ids != NULL) {
    for(int i = 0; i < count && *pad2 == NULL; i++) {
      SDL_Gamepad* p = SDL_OpenGamepad(ids[i]);
      if(p == NULL) continue;
      if(*pad1 == NULL) *pad1 = p; else *pad2 = p;
    }
    SDL_free(ids);
  }
}

/* ---- ROM lookup -------------------------------------------------------- */

static uint8_t* read_file(const char* path, size_t* lenOut) {
  FILE* f = fopen(path, "rb");
  if(f == NULL) return NULL;
  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  if(len <= 0) { fclose(f); return NULL; }
  uint8_t* buf = malloc((size_t) len);
  if(buf == NULL || fread(buf, 1, (size_t) len, f) != (size_t) len) {
    fclose(f);
    free(buf);
    return NULL;
  }
  fclose(f);
  *lenOut = (size_t) len;
  return buf;
}

static bool file_exists(const char* path) {
  FILE* f = fopen(path, "rb");
  if(f == NULL) return false;
  fclose(f);
  return true;
}

/* One candidate of the search below, written to dream.log either way. "the game
 * does not start" is nearly always this list, so the list is what the log
 * carries: every path tried, in order, and what was there. */
static bool rom_try(const char* what, const char* path) {
  bool ok = file_exists(path);
  dlog("rom: %-16s %s -> %s", what, path, ok ? "FOUND" : "not there");
  return ok;
}

/* Where the ROM is looked for, in order:
 *
 *   the path on the command line, then
 *   Windows: the directory dream.exe is in, then %APPDATA%\dream\DREAM.sfc,
 *   everywhere: ./baserom/DREAM.sfc (a checkout, on either platform), then
 *   POSIX: $HOME/.local/share/dream/DREAM.sfc.
 *
 * The Windows pair is what the release zip needs: it holds dream.exe and no
 * data, so the player drops their own DREAM.sfc next to it, and %APPDATA% is
 * where it belongs when the zip is unpacked somewhere read-only. The path is
 * taken with GetModuleFileNameA rather than SDL_GetBasePath because it is
 * handed straight to fopen: both speak the process's ANSI code page, while
 * SDL's path is UTF-8 and fopen would mis-read a non-ASCII directory name.
 *
 * buf holds whichever path is returned, so it must outlive the call. */
static const char* find_rom(const char* fromArgv, char* buf, size_t bufLen) {
  dlog_stage("rom: searching");
  if(fromArgv != NULL) {
    dlog("rom: %-16s %s (taken as given)", "command line", fromArgv);
    return fromArgv;
  }
#ifdef _WIN32
  {
    char exe[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, exe, (DWORD) sizeof(exe));
    if(n > 0 && n < sizeof(exe)) {
      char* slash = strrchr(exe, '\\');
      char* fwd = strrchr(exe, '/');
      if(fwd != NULL && (slash == NULL || fwd > slash)) slash = fwd;
      if(slash != NULL) {
        *slash = 0;
        snprintf(buf, bufLen, "%s\\DREAM.sfc", exe);
        if(rom_try("beside the exe", buf)) return buf;
      }
    } else {
      dlog("rom: GetModuleFileNameA failed (%lu)", (unsigned long) GetLastError());
    }
  }
  {
    const char* appdata = getenv("APPDATA");
    if(appdata != NULL) {
      snprintf(buf, bufLen, "%s\\dream\\DREAM.sfc", appdata);
      if(rom_try("%APPDATA%", buf)) return buf;
    } else {
      dlog("rom: %%APPDATA%% is not set");
    }
  }
#endif
  if(rom_try("checkout", "baserom/DREAM.sfc")) return "baserom/DREAM.sfc";
#ifndef _WIN32
  {
    const char* home = getenv("HOME");
    if(home != NULL) {
      snprintf(buf, bufLen, "%s/.local/share/dream/DREAM.sfc", home);
      if(rom_try("$HOME share", buf)) return buf;
    }
  }
#endif
  dlog("rom: nothing found; falling back to baserom/DREAM.sfc");
  return "baserom/DREAM.sfc";
}

/* ---- presentation: where the picture goes, under the menu bar ----------- */

/* View state. Nothing is persisted: the window's current size is the only memory the
 * app keeps (docs/RECOMP.md). None of it can touch emulation. */
typedef struct {
  int scaleMode;   /* 0 = integer fit to window, 1..4 = fixed multiple */
  int aspect;      /* 0 = 8:7 square pixels, 1 = 4:3 */
  bool fullscreen;
} View;

static SDL_FRect view_rect(const View* v, int outW, int outH, int barH) {
  float aw = (float) outW, ah = (float) (outH - barH);
  if(ah < 1.0f) ah = 1.0f;
  float w, h;
  if(v->scaleMode > 0) {
    h = (float) (FB_H * v->scaleMode);
    w = (v->aspect == 1) ? h * 4.0f / 3.0f : (float) (FB_W * v->scaleMode);
  } else if(v->aspect == 1) {
    h = ah;
    w = h * 4.0f / 3.0f;
    if(w > aw) { w = aw; h = w * 3.0f / 4.0f; }
  } else {
    int s = (int) (aw / (float) FB_W);
    int sy = (int) (ah / (float) FB_H);
    if(sy < s) s = sy;
    if(s < 1) s = 1;
    w = (float) (FB_W * s);
    h = (float) (FB_H * s);
  }
  SDL_FRect r = { (aw - w) / 2.0f, (float) barH + (ah - h) / 2.0f, w, h };
  return r;
}

/* 512x480 down to the signal's own 256x224: the core doubles every dot and every
 * line, and row y of the picture sits at 2y+16. */
static void downsample(const uint8_t* src, uint32_t* fb) {
  for(int y = 0; y < FB_H; y++) {
    const uint32_t* row = (const uint32_t*) (src + (size_t) (y * 2 + 16) * SRC_W * 4);
    for(int x = 0; x < FB_W; x++) fb[y * FB_W + x] = row[x * 2];
  }
}

/* --screenshot: the framebuffer as it stands, as a binary PPM. A test aid, not a
 * feature: on a machine with no display it is the only way to look at a page. */
static bool write_ppm(const char* path, const uint32_t* fb) {
  FILE* f = fopen(path, "wb");
  if(f == NULL) {
    fprintf(stderr, "dream: cannot write %s: %s\n", path, strerror(errno));
    return false;
  }
  fprintf(f, "P6\n%d %d\n255\n", FB_W, FB_H);
  for(int i = 0; i < FB_W * FB_H; i++) {
    uint32_t p = fb[i];
    fputc((int) ((p >> 24) & 0xFF), f);
    fputc((int) ((p >> 16) & 0xFF), f);
    fputc((int) ((p >> 8) & 0xFF), f);
  }
  fclose(f);
  return true;
}


/* --scene-dump MODE[,LAYERS][:FILE]: compose a scene and write it out.
 *
 * How the Scenes page is checked without a display. The image is the whole
 * level, so it is written as a PPM of its own size rather than through the
 * 256x224 framebuffer. */
static bool write_ppm_size(const char* path, const uint32_t* px, int w, int h) {
  FILE* f = fopen(path, "wb");
  if(f == NULL) {
    fprintf(stderr, "dream: cannot write %s: %s\n", path, strerror(errno));
    return false;
  }
  fprintf(f, "P6\n%d %d\n255\n", w, h);
  for(int i = 0; i < w * h; i++) {
    uint32_t p = px[i];
    fputc((int) ((p >> 24) & 0xFF), f);
    fputc((int) ((p >> 16) & 0xFF), f);
    fputc((int) ((p >> 8) & 0xFF), f);
  }
  fclose(f);
  return true;
}

static int scene_dump(const uint8_t* rom, size_t romLen, const char* spec) {
  char buf[256];
  snprintf(buf, sizeof(buf), "%s", spec);
  char* file = strchr(buf, ':');
  if(file != NULL) *file++ = 0;
  int view = SCENE_VIEW_ALL;
  char* comma = strchr(buf, ',');
  if(comma != NULL) { *comma = 0; view = atoi(comma + 1); }
  int mode = atoi(buf);
  Scenes* sc = scenes_create(rom, romLen);
  if(sc == NULL) { fprintf(stderr, "dream: no scene machine\n"); return 2; }
  scenes_request(sc, mode);
  while(scenes_step(sc, 250)) {
    printf("scene: %s (%d.%d%%)\n", scenes_status(sc),
           scenes_progress(sc) / 10, scenes_progress(sc) % 10);
    fflush(stdout);
  }
  int w = 0, h = 0;
  const uint32_t* img = scenes_image(sc, view, &w, &h);
  SceneInfo info;
  if(img == NULL || !scenes_info(sc, &info)) {
    fprintf(stderr, "dream: scene %d not composed: %s\n", mode, scenes_status(sc));
    scenes_destroy(sc);
    return 2;
  }
  printf("scene %d: %dx%d px, %d x %d metatiles, map %06X, meta %06X, "
         "bgmode %d%s, tm %02X, level layer BG%d, %d screens, %d frames\n",
         mode, w, h, info.cols, info.rows, info.mapAddr, info.metaAddr,
         info.bgmode, info.bg3prio ? "+bg3prio" : "", info.tm, info.levelBg,
         info.screens, info.frames);
  for(int i = 0; i < 4; i++)
    printf("  BG%d map $%04X chars $%04X%s\n", i + 1, info.bgMap[i], info.bgChr[i],
           ((info.tm >> i) & 1u) ? "  (TM)" : "");
  int rc = 0;
  if(file != NULL && *file != 0) rc = write_ppm_size(file, img, w, h) ? 0 : 2;
  scenes_destroy(sc);
  return rc;
}


/* --scene-verify [DIR]: the Scenes page against the running game.
 *
 * For each scene: compose it the way the page does, then boot a second machine
 * the ordinary way (the harness script, no walk, no camera hook), render its BG
 * layers through the same PPU with the same masking, and compare that screen
 * against the composed image cropped at the camera the second machine happens to
 * be at. The layer the metatile blitter feeds is continuous across the whole
 * composition, so it has to match pixel for pixel wherever the crop lands; a
 * parallax layer only matches where the crop lands on a screen the composition
 * took, and the second row of each pair measures that.
 */
static int scene_cmp(const uint32_t* a, const uint32_t* b, int n) {
  int same = 0;
  for(int i = 0; i < n; i++) if(a[i] == b[i]) same++;
  return same;
}

static int scene_probe(const uint8_t* rom, size_t romLen, const char* spec) {
  int mode = 0, cx = 0, cy = 0, view = SCENE_VIEW_ALL;
  char path[256] = "";
  if(sscanf(spec, "%d:%d:%d:%d:%255s", &mode, &cx, &cy, &view, path) < 5) return 2;
  uint32_t* out = malloc((size_t) SCENE_SCREEN_W * SCENE_SCREEN_H * sizeof(uint32_t));
  Scenes* sc = scenes_create(rom, romLen);
  int gx = 0, gy = 0;
  if(out == NULL || sc == NULL) return 2;
  if(!scenes_screen_at(sc, mode, cx, cy, view, out, &gx, &gy)) return 2;
  printf("probe mode %d asked (%d,%d) got (%d,%d) -> %s\n", mode, cx, cy, gx, gy, path);
  write_ppm_size(path, out, SCENE_SCREEN_W, SCENE_SCREEN_H);
  scenes_destroy(sc);
  free(out);
  return 0;
}

static int scene_verify(const uint8_t* rom, size_t romLen, const char* dir) {
  const int N = SCENE_SCREEN_W * SCENE_SCREEN_H;
  uint32_t* ref = malloc((size_t) N * sizeof(uint32_t));
  uint32_t* crop = malloc((size_t) N * sizeof(uint32_t));
  Scenes* a = scenes_create(rom, romLen);
  Scenes* b = scenes_create(rom, romLen);
  if(ref == NULL || crop == NULL || a == NULL || b == NULL) return 2;
  int bad = 0;
  printf("scene-verify: composed scene vs the running game, BG layers only,"
         " sprites masked\n");
  for(int mode = 0; mode < SCENE_MODE_COUNT; mode++) {
    SceneInfo info;
    /* pass 1: every layer the mode's TM enables; pass 2: the level layer alone */
    for(int pass = 0; pass < 2; pass++) {
      int view = SCENE_VIEW_ALL;
      if(pass == 1) {
        if(!scenes_info(a, &info) || info.levelBg < 1) {
          printf("  %-14s level layer: no BG has its tilemap at VRAM $7800\n",
                 scenes_mode_name(mode));
          continue;
        }
        view = info.levelBg;
      }
      scenes_request(a, mode);
      while(scenes_step(a, 1000)) { }
      int w = 0, h = 0;
      const uint32_t* img = scenes_image(a, view, &w, &h);
      if(img == NULL || !scenes_info(a, &info)) {
        printf("  %-14s FAILED to compose: %s\n", scenes_mode_name(mode), scenes_status(a));
        bad++;
        break;
      }
      int cx = 0, cy = 0;
      if(!scenes_reference(b, mode, view, ref, &cx, &cy)) { bad++; break; }
      for(int y = 0; y < SCENE_SCREEN_H; y++)
        for(int x = 0; x < SCENE_SCREEN_W; x++) {
          int sx = cx + x, sy = cy + y;
          crop[y * SCENE_SCREEN_W + x] =
            (sx >= 0 && sx < w && sy >= 0 && sy < h) ? img[(size_t) sy * w + sx] : 0;
        }
      int same = scene_cmp(ref, crop, N);
      printf("  %-14s %-11s%s camera (%4d,%4d)  stitch %6d/%6d %s",
             scenes_mode_name(mode),
             pass == 0 ? "all layers" : "level layer",
             pass == 0 ? "" : "",
             cx, cy, same, N, same == N ? "exact  " : "DIFFERS");
      if(mode != SCENE_MODE_TITLE) {
        int gx = 0, gy = 0;
        if(scenes_screen_at(b, mode, cx, cy, view, crop, &gx, &gy)) {
          int s2 = scene_cmp(ref, crop, N);
          printf("   one screen at (%4d,%4d) %6d/%6d %s",
                 gx, gy, s2, N, s2 == N ? "exact" : "DIFFERS");
          if(dir != NULL) {
            char path[512];
            snprintf(path, sizeof(path), "%s/verify_m%d_p%d_one.ppm", dir, mode, pass);
            write_ppm_size(path, crop, SCENE_SCREEN_W, SCENE_SCREEN_H);
          }
        }
      }
      printf("\n");
      if(pass == 1 && same != N) bad++;
      if(dir != NULL) {
        char path[512];
        snprintf(path, sizeof(path), "%s/verify_m%d_p%d_ref.ppm", dir, mode, pass);
        write_ppm_size(path, ref, SCENE_SCREEN_W, SCENE_SCREEN_H);
        snprintf(path, sizeof(path), "%s/verify_m%d_p%d_crop.ppm", dir, mode, pass);
        write_ppm_size(path, crop, SCENE_SCREEN_W, SCENE_SCREEN_H);
      }
      if(mode == SCENE_MODE_TITLE) break;   /* one screen, no level layer */
    }
  }
  scenes_destroy(a);
  scenes_destroy(b);
  free(ref);
  free(crop);
  printf("scene-verify: %s\n", bad == 0 ? "all level layers exact" : "SOME DIFFER");
  return bad == 0 ? 0 : 1;
}

/* --gallery SEC:NAV takes one gallery press per character. */
static void gallery_nav(Gallery* gal, const char* moves) {
  for(const char* p = moves; *p != 0; p++) {
    switch(*p) {
      case 'u': gallery_press(gal, 1u << GALLERY_BTN_UP); break;
      case 'd': gallery_press(gal, 1u << GALLERY_BTN_DOWN); break;
      case 'l': gallery_press(gal, 1u << GALLERY_BTN_LEFT); break;
      case 'r': gallery_press(gal, 1u << GALLERY_BTN_RIGHT); break;
      case 'p': gallery_press(gal, 1u << GALLERY_BTN_L); break;
      case 'n': gallery_press(gal, 1u << GALLERY_BTN_R); break;
      case 'b': gallery_press(gal, 1u << GALLERY_BTN_B); break;
      case 'a': gallery_press(gal, 1u << GALLERY_BTN_A); break;
      default: break;
    }
  }
}

/* A decoded BRR sample, into the same stream the DSP feeds: mono 32000 Hz doubled to
 * stereo. The stream is declared at 32093 Hz, so a sample plays 0.3% sharp, far below
 * hearing, and it keeps the app to one audio path. */
static void play_pcm(SDL_AudioStream* audio, const int16_t* pcm, int count) {
  if(audio == NULL || count <= 0) return;
  int16_t* buf = malloc((size_t) count * 2 * sizeof(int16_t));
  if(buf == NULL) return;
  for(int i = 0; i < count; i++) { buf[i * 2] = pcm[i]; buf[i * 2 + 1] = pcm[i]; }
  SDL_ClearAudioStream(audio);
  SDL_PutAudioStreamData(audio, buf, count * 2 * (int) sizeof(int16_t));
  free(buf);
}

/* ---- main -------------------------------------------------------------- */

/* A plain C main(), on every platform including Windows.
 *
 * SDL3, unlike SDL2, does not include SDL_main.h from SDL.h and does not rename
 * main(): the entry point below is the real one, and SDL_MAIN_USE_CALLBACKS is
 * an opt-in this app has no use for: it owns its frame loop, which is paced by
 * the SNES's 60.0988 Hz and not by the platform (recomp/app/README.md, "Rate").
 *
 * On Windows that leaves dream.exe a console-subsystem program (no -mwindows, no
 * WinMain), which is deliberate: `--frames N --input SCRIPT` has to print the
 * harness's frame line to the terminal it was started from, and that line being
 * byte-identical to dream_harness's is the whole proof that the platform layer
 * changed nothing. A console window alongside the game is the price. */
int main(int argc, char** argv) {
  /* Before anything else, including the argument parse: whatever goes wrong
   * below, the log is already open and the crash handler is already on. */
  dlog_open();
  dlog_install_crash_handlers();
  dlog_stage("argv: %d argument(s)", argc);
  for(int i = 0; i < argc; i++) dlog("argv[%d] = %s", i, argv[i] != NULL ? argv[i] : "(null)");

  const char* romArg = NULL;
  const char* inputPath = NULL;
  const char* shotPath = NULL;
  const char* gallerySpec = NULL;
  const char* sceneSpec = NULL;
  const char* sceneVerify = NULL;
  const char* sceneProbe = NULL;
  bool wantSceneVerify = false;
  int frames = 0;                 /* 0 = run until the user quits */
  int toggleFrame = -1, toggleIters = 0;

  for(int i = 1; i < argc; i++) {
    const char* a = argv[i];
    bool hasNext = i + 1 < argc;
    if(strcmp(a, "--frames") == 0 && hasNext) frames = atoi(argv[++i]);
    else if(strcmp(a, "--input") == 0 && hasNext) inputPath = argv[++i];
    else if(strcmp(a, "--screenshot") == 0 && hasNext) shotPath = argv[++i];
    else if(strcmp(a, "--gallery") == 0 && hasNext) gallerySpec = argv[++i];
    else if(strcmp(a, "--scene-dump") == 0 && hasNext) sceneSpec = argv[++i];
    else if(strcmp(a, "--scene-probe") == 0 && hasNext) sceneProbe = argv[++i];
    else if(strcmp(a, "--scene-verify") == 0) {
      wantSceneVerify = true;
      if(hasNext && argv[i + 1][0] != '-') sceneVerify = argv[++i];
    }
    else if(strcmp(a, "--gallery-toggle") == 0 && hasNext) {
      if(sscanf(argv[++i], "%d,%d", &toggleFrame, &toggleIters) != 2) {
        fprintf(stderr, "dream: --gallery-toggle wants FRAME,ITERATIONS\n");
        return 2;
      }
    }
    else if(a[0] == '-' && a[1] != 0) {
      fprintf(stderr, "dream: unknown option %s\n", a);
      return 2;
    } else if(romArg == NULL) romArg = a;
    else {
      fprintf(stderr, "dream: more than one ROM path given\n");
      return 2;
    }
  }
  if(frames < 0) {
    fprintf(stderr, "dream: --frames must be positive\n");
    return 2;
  }
  bool timed = frames > 0;   /* --frames: run N and print the frame line */

  char romBuf[1024];
  const char* romPath = find_rom(romArg, romBuf, sizeof(romBuf));
  size_t romLen = 0;
  dlog_stage("rom: reading %s", romPath);
  uint8_t* rom = read_file(romPath, &romLen);
  if(rom == NULL) {
    dlog("rom: cannot read %s (%s): giving up", romPath, strerror(errno));
    fprintf(stderr, "dream: cannot read %s: put your own DREAM.sfc there "
                    "(or pass its path)\n", romPath);
    return 2;
  }
  dlog("rom: %zu bytes (expected %u)", romLen, ROM_SIZE);
  char hex[41];
  sha1_hex(rom, romLen, hex);
  dlog_stage("rom: sha1 %s (expected %s): %s", hex, ROM_SHA1,
             strcmp(hex, ROM_SHA1) == 0 ? "match" : "MISMATCH");
  if(romLen != ROM_SIZE || strcmp(hex, ROM_SHA1) != 0) {
    fprintf(stderr, "dream: %s is not the supported ROM (sha1 %s, expected %s)\n",
            romPath, hex, ROM_SHA1);
    free(rom);
    return 2;
  }

  if(sceneProbe != NULL) {
    int rc = scene_probe(rom, romLen, sceneProbe);
    free(rom);
    dlog_close();
    return rc;
  }

  if(wantSceneVerify) {
    int rc = scene_verify(rom, romLen, sceneVerify);
    free(rom);
    dlog_close();
    return rc;
  }

  if(sceneSpec != NULL) {
    int rc = scene_dump(rom, romLen, sceneSpec);
    free(rom);
    dlog_close();
    return rc;
  }

  InputScript script = { NULL, 0 };
  if(inputPath != NULL) {
    dlog_stage("input: loading %s", inputPath);
    if(!input_load(&script, inputPath)) { free(rom); return 2; }
    dlog("input: %d event(s)", script.count);
  }

  /* SDL is initialised even for --frames: the harness comparison is only worth
   * something if it runs the same program, and the dummy drivers make that
   * possible without a display. */
  SDL_SetHint(SDL_HINT_APP_NAME, "Dream: Land of Giants");
  {
    int v = SDL_GetVersion();
    dlog("sdl: built against %d.%d.%d, running on %d.%d.%d (%s)",
         SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_MICRO_VERSION,
         SDL_VERSIONNUM_MAJOR(v), SDL_VERSIONNUM_MINOR(v), SDL_VERSIONNUM_MICRO(v),
         SDL_GetRevision());
  }
  dlog_stage("sdl: SDL_Init(VIDEO|AUDIO|GAMEPAD)");
  if(!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
    dlog("sdl: SDL_Init failed: %s", SDL_GetError());
    fprintf(stderr, "dream: SDL_Init failed: %s\n", SDL_GetError());
    free(rom);
    return 2;
  }
  {
    const char* vd = SDL_GetCurrentVideoDriver();
    const char* ad = SDL_GetCurrentAudioDriver();
    dlog("sdl: video driver %s, audio driver %s",
         vd != NULL ? vd : "(none)", ad != NULL ? ad : "(none)");
  }

  SDL_Window* window = NULL;
  SDL_Renderer* renderer = NULL;
  SDL_Texture* texture = NULL;
  SDL_AudioStream* audio = NULL;
  uint8_t* srcPixels = NULL;
  int16_t* audioBuf = NULL;

  dlog_stage("sdl: creating the window and renderer (%dx%d)",
             FB_W * 3, FB_H * 3 + menubar_height(FB_W * 3));
  if(!SDL_CreateWindowAndRenderer("Dream: Land of Giants", FB_W * 3,
                                  FB_H * 3 + menubar_height(FB_W * 3),
                                  SDL_WINDOW_RESIZABLE, &window, &renderer)) {
    dlog("sdl: SDL_CreateWindowAndRenderer failed: %s", SDL_GetError());
    fprintf(stderr, "dream: cannot create a window: %s\n", SDL_GetError());
    SDL_Quit();
    free(rom);
    return 2;
  }
  /* No logical presentation: the menu bar is drawn at the window's own resolution
   * and the picture goes into a rect computed under it (view_rect), which keeps the
   * default 8:7 view on whole-numbered pixels exactly as before. */
  {
    const char* rn = SDL_GetRendererName(renderer);
    dlog("sdl: window %p renderer %p (%s)", (void*) window, (void*) renderer,
         rn != NULL ? rn : "(unnamed)");
  }
  dlog_stage("sdl: creating the %dx%d framebuffer texture", FB_W, FB_H);
  texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBX8888,
                              SDL_TEXTUREACCESS_STREAMING, FB_W, FB_H);
  if(texture == NULL) {
    dlog("sdl: SDL_CreateTexture failed: %s", SDL_GetError());
    fprintf(stderr, "dream: cannot create the framebuffer: %s\n", SDL_GetError());
    SDL_Quit();
    free(rom);
    return 2;
  }
  SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
  srcPixels = malloc((size_t) SRC_W * SRC_H * 4);

  /* No audio device is not a reason to refuse to play the game. */
  dlog_stage("sdl: opening the audio device (%d Hz, stereo s16)", AUDIO_HZ);
  SDL_AudioSpec want = { SDL_AUDIO_S16, 2, AUDIO_HZ };
  audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &want, NULL, NULL);
  if(audio != NULL) {
    SDL_AudioDeviceID dev = SDL_GetAudioStreamDevice(audio);
    const char* dn = SDL_GetAudioDeviceName(dev);
    dlog("audio: device %u \"%s\", stream %p", (unsigned) dev,
         dn != NULL ? dn : "(unnamed)", (void*) audio);
    audioBuf = malloc((size_t) (AUDIO_SAMPLES_PER_FRAME + 64) * 4);
    SDL_ResumeAudioStreamDevice(audio);
  } else {
    dlog("audio: no device (%s): the game runs silent, which is not an error",
         SDL_GetError());
  }

  dlog_stage("machine: snes_init");
  Machine m;
  memset(&m, 0, sizeof(m));
  m.snes = snes_init();
  m.ss.snes = m.snes;
  m.hooks = build_table(&m.hookCount);
  m.spcHooks = build_spc_table(&m.spcHookCount);
  dlog("machine: hook tables: %u 65816 entries, %u SPC700 entries",
       m.hookCount, m.spcHookCount);
  dlog_stage("machine: loading the cart and resetting");
  if(!machine_load_rom(&m, rom, romLen)) { free(rom); return 2; }
  /* The ROM image stays mapped for the gallery, which decodes it at run time. */
  snes_setPixelFormat(m.snes, pixelFormatRGBX);
  m.snes->cpu->hook = app_hook;
  m.snes->cpu->hookCtx = &m;
  m.sps.apu = m.snes->apu;
  m.sps.spc = m.snes->apu->spc;
  m.snes->apu->spc->hook = app_spc_hook;
  m.snes->apu->spc->hookCtx = &m;
  /* The game is the C bodies: neither emulated CPU fetches an instruction. The
   * scheduler in the harness sources resolves every pc through the tables above
   * (recomp/README.md, "Running without the CPUs"). */
  dlog_stage("machine: --no-cpu on, coroutine backend \"%s\"", coro_backend());
  ss_nocpu_enable(&m.ss, true);
  sps_nocpu_enable(&m.sps, true);

  dlog_stage("input: opening gamepads");
  SDL_Gamepad* pad = NULL;
  SDL_Gamepad* pad2 = NULL;
  open_gamepads(&pad, &pad2);
  {
    int padCount = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&padCount);
    if(ids != NULL) SDL_free(ids);
    dlog("input: %d gamepad(s) present; player 1 %s, player 2 %s", padCount,
         pad  != NULL ? SDL_GetGamepadName(pad)  : "(none)",
         pad2 != NULL ? SDL_GetGamepadName(pad2) : "(none)");
  }

  /* The app's own UI: a menu bar and the gallery pages it opens. The gallery reads
   * the ROM image and the framebuffer and nothing else; no path from here reaches
   * the machine above. */
  dlog_stage("ui: creating the gallery and the menu bar");
  uint32_t* fb = calloc((size_t) FB_W * FB_H, sizeof(uint32_t));
  Gallery* gal = gallery_create(rom, romLen);
  Menubar* bar = menubar_create(renderer);
  MusicPlayer* music = NULL;
  Scenes* scenes = NULL;
  View view = { 0, 0, false };

  bool running = true;
  int frame = 0;
  int toggleLeft = 0;
  uint64_t nextFrame = SDL_GetTicksNS();
  dlog_stage("run: entering the frame loop (%s)",
             timed ? "--frames, unpaced" : "60.0988 Hz");

  while(running) {
    MenuModel model = { view.scaleMode, view.aspect, view.fullscreen,
                        gallery_is_open(gal), gallery_section(gal) };
    SDL_Event ev;
    while(SDL_PollEvent(&ev)) {
      MenuAction act;
      if(menubar_event(bar, renderer, &ev, &model, &act)) {
        switch(act.kind) {
          case MENU_ACT_QUIT: running = false; break;
          case MENU_ACT_SCALE:
          case MENU_ACT_FIT: {
            view.scaleMode = act.kind == MENU_ACT_FIT ? 0 : act.arg;
            if(view.scaleMode > 0 && !view.fullscreen) {
              int w = view.aspect == 1 ? (FB_H * view.scaleMode * 4) / 3
                                       : FB_W * view.scaleMode;
              SDL_SetWindowSize(window, w, FB_H * view.scaleMode + menubar_height(w));
            }
            break;
          }
          case MENU_ACT_ASPECT: view.aspect = act.arg; break;
          case MENU_ACT_FULLSCREEN:
            view.fullscreen = !view.fullscreen;
            SDL_SetWindowFullscreen(window, view.fullscreen);
            break;
          case MENU_ACT_GALLERY:
            dlog_stage("gallery: opening section %d (menu)", act.arg);
            gallery_open(gal, act.arg);
            if(audio != NULL) SDL_ClearAudioStream(audio);   /* the game falls silent */
            break;
          case MENU_ACT_GALLERY_CLOSE:
            dlog_stage("gallery: closing (menu)");
            gallery_close(gal);
            break;
          default: break;
        }
        continue;
      }
      switch(ev.type) {
        case SDL_EVENT_QUIT:
          dlog_stage("run: SDL_EVENT_QUIT");
          running = false;
          break;
        case SDL_EVENT_KEY_DOWN:
          /* Escape closes the page if one is open, and quits otherwise. */
          if(ev.key.scancode == SDL_SCANCODE_ESCAPE) {
            if(gallery_is_open(gal)) {
              dlog_stage("gallery: closing (escape)");
              gallery_close(gal);
            } else {
              dlog_stage("run: escape, quitting");
              running = false;
            }
          }
          break;
        case SDL_EVENT_GAMEPAD_ADDED:
          if(pad == NULL) pad = SDL_OpenGamepad(ev.gdevice.which);
          else if(pad2 == NULL) pad2 = SDL_OpenGamepad(ev.gdevice.which);
          break;
        case SDL_EVENT_GAMEPAD_REMOVED:
          if((pad != NULL && SDL_GetGamepadID(pad) == ev.gdevice.which) ||
             (pad2 != NULL && SDL_GetGamepadID(pad2) == ev.gdevice.which)) {
            if(pad != NULL && SDL_GetGamepadID(pad) == ev.gdevice.which) SDL_CloseGamepad(pad);
            if(pad2 != NULL && SDL_GetGamepadID(pad2) == ev.gdevice.which) SDL_CloseGamepad(pad2);
            open_gamepads(&pad, &pad2);   /* silently promote whatever is left */
          }
          break;
        default:
          break;
      }
    }
    if(!running) break;

    /* The hidden toggle test: open the gallery mid-run, walk every section, close
     * it again. Nothing below runs the machine while it is open: the frame line at
     * the end has to be the one an untoggled run prints. */
    if(toggleFrame >= 0 && frame == toggleFrame && toggleIters > 0) {
      dlog_stage("gallery: opening at frame %d (--gallery-toggle, %d iterations)",
                 frame, toggleIters);
      gallery_open(gal, 0);
      toggleLeft = toggleIters;
      toggleFrame = -1;                 /* once: a paused iteration is not a frame */
    }

    bool paused = gallery_is_open(gal);

    if(paused) {
      /* The gallery's own UI step. The machine is not stepped, not written to and
       * not even handed an input state: it stands exactly as it was. */
      if(toggleLeft > 0) {
        if(--toggleLeft == 0) {
          dlog_stage("gallery: closing (--gallery-toggle done), back at frame %d", frame);
          gallery_close(gal);
        }
        else if(toggleLeft % 4 == 0)
          gallery_open(gal, (gallery_section(gal) + 1) % GALLERY_SECTION_COUNT);
        else gallery_press(gal, 1u << GALLERY_BTN_RIGHT);
      } else {
        uint16_t held = 0;
        if(inputPath == NULL && !menubar_focused(bar))
          held = (uint16_t) (read_gamepad(pad) | read_keyboard());
        gallery_input(gal, held);
      }

      /* The Scenes page (and the observed half of the sprite palettes) get a
       * machine of their own too, and it is stepped a few milliseconds at a time
       * so the page stays responsive while it composes. It is kept once built:
       * the walk across a level is thousands of frames and nobody wants it
       * repeated because a page was closed. */
      if(gallery_wants_scenes(gal)) {
        if(scenes == NULL) {
          dlog_stage("gallery: creating the scene machine");
          scenes = scenes_create(rom, romLen);
          gallery_set_scenes(gal, scenes);
        }
        if(gallery_section(gal) == GALLERY_SEC_SPRITES) scenes_observe_request(scenes);
      }
      if(scenes != NULL) scenes_step(scenes, 12);

      /* The Music page gets a machine of its own, booted on first use and thrown
       * away when the page closes; its DSP output is what plays. */
      bool wantMusic = gallery_section(gal) == GALLERY_SEC_MUSIC;
      if(wantMusic && music == NULL) music = music_create(rom, romLen);
      if(!wantMusic && music != NULL) { music_destroy(music); music = NULL; }
      int reqKind = 0, reqArg = 0;
      if(gallery_take_request(gal, &reqKind, &reqArg) && music != NULL) {
        if(reqKind == GALLERY_REQ_SONG) music_play_song(music, reqArg);
        else if(reqKind == GALLERY_REQ_SFX) music_play_sfx(music, (uint16_t) reqArg);
      }
      if(music != NULL && audio != NULL) {
        music_frame(music, audioBuf, AUDIO_SAMPLES_PER_FRAME);
        SDL_PutAudioStreamData(audio, audioBuf, AUDIO_SAMPLES_PER_FRAME * 4);
      }
      const int16_t* pcm = NULL;
      int pcmCount = 0;
      if(gallery_take_pcm(gal, &pcm, &pcmCount)) play_pcm(audio, pcm, pcmCount);

      gallery_render(gal, fb);
    } else {
      if(music != NULL) { music_destroy(music); music = NULL; }
      uint16_t state, state2;
      if(inputPath != NULL) {
        input_state_at(&script, frame, &state, &state2);
      } else if(menubar_focused(bar)) {
        state = 0;
        state2 = read_gamepad(pad2);
      } else {
        state = (uint16_t) (read_gamepad(pad) | read_keyboard());
        state2 = read_gamepad(pad2);
      }
      machine_set_input(&m, state, state2);

      ss_nocpu_run_frame(&m.ss);

      /* Nudge the sample count by ~1% when the queue drifts a frame off its
       * target: dsp_getSamples() resamples to whatever is asked for, so this costs
       * a pitch shift far below hearing and keeps latency bounded when the display
       * clock and 60.0988 Hz disagree. */
      if(audio != NULL) {
        int queued = SDL_GetAudioStreamQueued(audio) / 4;
        int want = AUDIO_SAMPLES_PER_FRAME;
        if(queued > AUDIO_QUEUE_TARGET + AUDIO_SAMPLES_PER_FRAME) want -= 6;
        else if(queued < AUDIO_QUEUE_TARGET - AUDIO_SAMPLES_PER_FRAME) want += 6;
        snes_setSamples(m.snes, audioBuf, want);
        SDL_PutAudioStreamData(audio, audioBuf, want * 4);
      }

      snes_setPixels(m.snes, srcPixels);
      downsample(srcPixels, fb);
    }

    SDL_UpdateTexture(texture, NULL, fb, FB_W * (int) sizeof(uint32_t));
    int outW = 0, outH = 0;
    SDL_GetRenderOutputSize(renderer, &outW, &outH);
    int barH = menubar_height(outW);
    SDL_FRect dst = view_rect(&view, outW, outH, barH);
    SDL_SetRenderDrawColor(renderer, 0x0A, 0x0C, 0x10, 255);
    SDL_RenderClear(renderer);
    SDL_RenderTexture(renderer, texture, NULL, &dst);
    model.galleryOpen = gallery_is_open(gal);
    model.gallerySection = gallery_section(gal);
    menubar_draw(bar, renderer, outW, &model);
    SDL_RenderPresent(renderer);

    /* Paced off the clock rather than the display: the game's rate is 60.0988 Hz
     * whatever the monitor happens to run at. --frames is the one exception, so
     * the comparison against dream_harness runs at full speed. */
    if(!timed) {
      nextFrame += FRAME_NS;
      uint64_t now = SDL_GetTicksNS();
      if(now < nextFrame) SDL_DelayNS(nextFrame - now);
      else if(now - nextFrame > FRAME_NS * 8) nextFrame = now;   /* gave up catching up */
    }

    if(paused) continue;      /* a paused iteration is not a frame of the game */
    frame++;
    /* The first frame proves the machine ran at all; every 300th (five seconds)
     * is the heartbeat, and its WRAM hash is what says whether the run that
     * stopped was still the same run as a good one. The last of these lines in
     * dream.log is where the crash handler's "stage" points. */
    if(frame == 1 || frame % 300 == 0)
      dlog_stage("frame %d: wram=%016llx", frame,
                 (unsigned long long) xxh64(m.snes->ram, WRAM_SIZE, 0));
    if(timed && frame >= frames) break;
  }

  if(timed) {
    machine_snapshot(&m);
    print_frame_line(frame - 1, &m);
  }

  /* --gallery / --screenshot: open the page the test asked for, drive it with the
   * given moves and dump whatever the framebuffer then holds. */
  if(gallerySpec != NULL) {
    char spec[128];
    snprintf(spec, sizeof(spec), "%s", gallerySpec);
    char* colon = strchr(spec, ':');
    if(colon != NULL) *colon = 0;
    int sec = gallery_section_by_name(spec);
    if(sec < 0) {
      fprintf(stderr, "dream: unknown gallery section '%s'\n", spec);
    } else {
      dlog_stage("gallery: opening section '%s' (--gallery)", spec);
      gallery_open(gal, sec);
      if(colon != NULL) gallery_nav(gal, colon + 1);
      /* A page that needs the scene machine gets it run to completion here: the
       * screenshot flag exists to look at a finished page, not at a progress
       * bar. Interactively the same work is spread over the paused iterations. */
      if(gallery_wants_scenes(gal)) {
        if(scenes == NULL) {
          scenes = scenes_create(rom, romLen);
          gallery_set_scenes(gal, scenes);
        }
        if(gallery_section(gal) == GALLERY_SEC_SPRITES) scenes_observe_request(scenes);
        gallery_render(gal, fb);      /* the page says which scene it wants */
        while(scenes_step(scenes, 1000)) { }
      }
      gallery_render(gal, fb);
    }
  }
  if(shotPath != NULL) write_ppm(shotPath, fb);

  dlog_stage("run: leaving the frame loop after %d frame(s); tearing down", frame);
  if(music != NULL) music_destroy(music);
  if(scenes != NULL) scenes_destroy(scenes);
  menubar_destroy(bar);
  gallery_destroy(gal);
  free(fb);
  free(rom);
  if(pad != NULL) SDL_CloseGamepad(pad);
  if(pad2 != NULL) SDL_CloseGamepad(pad2);
  if(audio != NULL) SDL_DestroyAudioStream(audio);
  if(texture != NULL) SDL_DestroyTexture(texture);
  if(renderer != NULL) SDL_DestroyRenderer(renderer);
  if(window != NULL) SDL_DestroyWindow(window);
  SDL_Quit();

  snes_free(m.snes);
  free(m.hooks);
  free(m.spcHooks);
  free(srcPixels);
  free(audioBuf);
  free(script.ev);
  dlog_stage("dream: clean exit (0)");
  dlog_close();
  return 0;
}
