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
 *   --screenshot-ui FILE        dump the whole window, menu bar included
 *   --gallery SEC[:NAV]         open a gallery page (for the screenshot)
 *   --gallery-toggle N,ITERS    open the gallery mid-run and close it again, so a
 *                               run with the toggle can be diffed against one without
 *   --sprite-pal-report         how the 1555 live frames' palettes come out
 *   --sprite-probe F:M:FLAGS:FILE  one sprite frame, drawn by the game
 *   --sprite-verify             every forced sprite render against the game
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

#include <time.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>     /* _mkdir: the screenshots directory */
#else
#include <sys/types.h>
#endif

#ifdef _WIN32
/* GetModuleFileNameA, for the ROM sitting next to dream.exe (find_rom below)
 * and for the screenshots directory beside it (exe_dir below).
 * Nothing else in the app is Windows-specific: SDL covers the window, the
 * sound, the pads and the 60.0988 Hz pacing, and the coroutine backend the
 * --no-cpu scheduler needs is chosen by CMake (recomp/harness/coro.h). */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <SDL3/SDL.h>

/* The one path separator that differs. Windows takes either, but a path in
 * dream.log should look like the platform's own. */
#ifdef _WIN32
#define DIR_SEP '\\'
#else
#define DIR_SEP '/'
#endif

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
#include "romfont.h"
#include "menubar.h"
#include "music.h"
#include "scene.h"
#include "coro.h"   /* coro_backend(), for the log line naming the backend */
#include "dlog.h"   /* dream.log: the run, and the crash, written down */
#include "png.h"    /* the screenshot writer: stored deflate, no zlib */

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
/* Player 2 drives the first enemy (the game reads pad 2 in check_pending_player_attack),
 * so a second device that is really the first one seen twice (XInput next to
 * DirectInput, a Steam virtual pad next to the physical one) would move enemies with
 * the player. A candidate only becomes player 2 when it is a different device: a
 * different path, or a different serial when the path is unknown. */
static bool same_device(SDL_Gamepad* a, SDL_Gamepad* b) {
  const char* pa = SDL_GetGamepadPath(a);
  const char* pb = SDL_GetGamepadPath(b);
  if(pa != NULL && pb != NULL) return strcmp(pa, pb) == 0;
  const char* sa = SDL_GetGamepadSerial(a);
  const char* sb = SDL_GetGamepadSerial(b);
  if(sa != NULL && sb != NULL && sa[0] != 0 && sb[0] != 0) return strcmp(sa, sb) == 0;
  return SDL_GetGamepadVendor(a) == SDL_GetGamepadVendor(b) &&
         SDL_GetGamepadProduct(a) == SDL_GetGamepadProduct(b) &&
         SDL_GetGamepadType(a) != SDL_GetGamepadType(b);   /* same pad, two drivers */
}

static void open_gamepads(SDL_Gamepad** pad1, SDL_Gamepad** pad2) {
  int count = 0;
  SDL_JoystickID* ids = SDL_GetGamepads(&count);
  *pad1 = NULL;
  *pad2 = NULL;
  if(ids != NULL) {
    for(int i = 0; i < count && *pad2 == NULL; i++) {
      SDL_Gamepad* p = SDL_OpenGamepad(ids[i]);
      if(p == NULL) continue;
      dlog("input: device %d: \"%s\" path=%s serial=%s vendor=%04x product=%04x type=%d", i,
           SDL_GetGamepadName(p) ? SDL_GetGamepadName(p) : "?",
           SDL_GetGamepadPath(p) ? SDL_GetGamepadPath(p) : "?",
           SDL_GetGamepadSerial(p) ? SDL_GetGamepadSerial(p) : "?",
           SDL_GetGamepadVendor(p), SDL_GetGamepadProduct(p), (int) SDL_GetGamepadType(p));
      if(*pad1 == NULL) { *pad1 = p; continue; }
      if(same_device(*pad1, p)) {
        dlog("input: device %d is the player 1 pad seen again; not assigned to player 2", i);
        SDL_CloseGamepad(p);
        continue;
      }
      *pad2 = p;
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


/* The same, at a size of its own: a sprite crop is not 256x224. */
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

/* ---- the Screenshot item (File menu, or F12) -----------------------------
 *
 * What is saved is the viewport as it stands: the 256x224 framebuffer, at its
 * own size, with whatever page or frame of the game is in it and without the
 * menu bar, which is drawn with SDL primitives over the top and is not part of
 * the picture. The scale the window happens to be at is a property of the
 * window, not of the image, so it is not baked in.
 *
 * Where it goes: a `screenshots` directory beside the executable, made if it is
 * not there. That is the directory GetModuleFileNameA reports on Windows and
 * SDL_GetBasePath everywhere else (/proc/self/exe on Linux, which is what SDL
 * reads); if neither can say, the current directory is used, which is what a
 * checkout run from its own root wants anyway.
 */
static void exe_dir(char* out, size_t outLen) {
  out[0] = 0;
#ifdef _WIN32
  char exe[MAX_PATH];
  DWORD n = GetModuleFileNameA(NULL, exe, (DWORD) sizeof(exe));
  if(n > 0 && n < sizeof(exe)) {
    char* slash = strrchr(exe, '\\');
    char* fwd = strrchr(exe, '/');
    if(fwd != NULL && (slash == NULL || fwd > slash)) slash = fwd;
    if(slash != NULL) {
      *slash = 0;
      snprintf(out, outLen, "%s", exe);
      return;
    }
  }
  dlog("screenshot: GetModuleFileNameA failed (%lu)", (unsigned long) GetLastError());
#else
  const char* base = SDL_GetBasePath();   /* owned by SDL; has a trailing slash */
  if(base != NULL && base[0] != 0) {
    snprintf(out, outLen, "%s", base);
    size_t len = strlen(out);
    while(len > 1 && out[len - 1] == '/') out[--len] = 0;
    return;
  }
  dlog("screenshot: SDL_GetBasePath gave nothing: %s", SDL_GetError());
#endif
}

static bool dir_make(const char* path) {
#ifdef _WIN32
  return _mkdir(path) == 0 || errno == EEXIST;
#else
  return mkdir(path, 0777) == 0 || errno == EEXIST;
#endif
}

/* The picture, and the name it went under for the menu bar. `name` gets the file
 * name alone; dream.log gets the whole path either way. */
static bool save_screenshot(const uint32_t* fb, char* name, size_t nameLen) {
  char dir[512];
  exe_dir(dir, sizeof(dir));
  char shots[576];
  if(dir[0] != 0) snprintf(shots, sizeof(shots), "%s%cscreenshots", dir, DIR_SEP);
  else snprintf(shots, sizeof(shots), "screenshots");
  if(!dir_make(shots)) {
    dlog("screenshot: cannot make %s: %s", shots, strerror(errno));
    snprintf(name, nameLen, "no screenshots directory");
    return false;
  }

  time_t now = time(NULL);
  struct tm tmv;
#ifdef _WIN32
  struct tm* got = localtime(&now);
  if(got != NULL) tmv = *got;
  else memset(&tmv, 0, sizeof(tmv));
#else
  if(localtime_r(&now, &tmv) == NULL) memset(&tmv, 0, sizeof(tmv));
#endif
  char file[64];
  char path[640];
  /* Two shots inside one second would otherwise be the same name and the second
   * would eat the first, so the seconds get a counter after them when they have
   * to. Twenty is more than anyone can press in a second. */
  for(int n = 0; n < 20; n++) {
    if(n == 0)
      snprintf(file, sizeof(file), "dream-%04d%02d%02d-%02d%02d%02d.png",
               tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
               tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    else
      snprintf(file, sizeof(file), "dream-%04d%02d%02d-%02d%02d%02d-%d.png",
               tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
               tmv.tm_hour, tmv.tm_min, tmv.tm_sec, n + 1);
    snprintf(path, sizeof(path), "%s%c%s", shots, DIR_SEP, file);
    if(!file_exists(path)) break;
  }

  if(!png_write_rgbx(path, fb, FB_W, FB_H)) {
    dlog("screenshot: cannot write %s: %s", path, strerror(errno));
    snprintf(name, nameLen, "screenshot failed");
    return false;
  }
  dlog_stage("screenshot: wrote %s", path);
  snprintf(name, nameLen, "saved %s", file);
  return true;
}

/* --sprite-probe FID:MODE:FLAGS:FILE: one sprite frame, drawn by the game.
 *
 * How the forced render is looked at without a display. FID is the frame id the
 * gallery numbers frames with (entity_frame_id / 4), FLAGS the entity flag word
 * in hex. */
static int sprite_probe(const uint8_t* rom, size_t romLen, const char* spec) {
  unsigned fid = 0, mode = 0, flags = 0;
  char path[256] = "";
  if(sscanf(spec, "%u:%u:%x:%255s", &fid, &mode, &flags, path) < 4) return 2;
  Scenes* sc = scenes_create(rom, romLen);
  if(sc == NULL) return 2;
  SpriteShot shot;
  bool ok = scenes_sprite_shot(sc, (int) mode, (uint16_t) (fid * 4), (uint16_t) flags, &shot);
  if(!ok || shot.w == 0) {
    printf("sprite %u: nothing drawn (%s)\n", fid, ok ? shot.why : scenes_status(sc));
    scenes_destroy(sc);
    return 1;
  }
  printf("sprite %u in %s: %d sprites, %dx%d at (%d,%d)%s -> %s\n", fid,
         scenes_mode_name(shot.mode), shot.sprites, shot.w, shot.h, shot.x0, shot.y0,
         shot.clipped ? ", clipped" : "", path);
  write_ppm_size(path, shot.px, shot.w, shot.h);
  sprite_shot_free(&shot);
  scenes_destroy(sc);
  return 0;
}


/* --sprite-verify: the forced sprite render against the running game.
 *
 * Two passes, and the second is the wider one.
 *
 * The first is the gate. For every frame id the observation scripts show in OAM,
 * the running game's own crop of that entity is taken at the frame it was seen
 * at, and the same frame id is then forced onto a scratch machine with the same
 * entity flag word and the same scene. The two pictures have to be the same
 * picture: same size, same pixels. The sprite sits somewhere else on the screen
 * in each, which is what cropping to the entity's own OAM entries takes out.
 *
 * The second renders every live frame once, with the scene and flag word the
 * page would use, and counts what came out: a picture, or a reason there is not
 * one. */
typedef struct {
  SpriteShot shot;
  bool have;
} SpriteRef;

typedef struct {
  SpriteRef* ref;      /* by frame id / 4 */
  int n;
} SpriteRefSet;

static void sprite_ref_cb(void* ctx, const SpriteShot* shot) {
  SpriteRefSet* set = (SpriteRefSet*) ctx;
  unsigned i = shot->frameId / 4u;
  if(i >= (unsigned) set->n || set->ref[i].have) return;
  if(!sprite_shot_copy(&set->ref[i].shot, shot)) return;
  set->ref[i].have = true;
}

/* The two crops are the same sprite drawn in two places, so they are compared in
 * the entity's own box rather than in screen coordinates. A crop the screen edge
 * cut short covers only part of that box; the overlap is what can be compared,
 * and `whole` says whether the whole box was. */
static bool shot_same(const SpriteShot* a, const SpriteShot* b, bool* whole) {
  *whole = false;
  if(a->w == 0 || b->w == 0) return false;
  if(a->uw != b->uw || a->uh != b->uh) return false;
  int ax = a->x0 - a->ux0, ay = a->y0 - a->uy0;
  int bx = b->x0 - b->ux0, by = b->y0 - b->uy0;
  int x0 = ax > bx ? ax : bx, y0 = ay > by ? ay : by;
  int x1 = (ax + a->w < bx + b->w) ? ax + a->w : bx + b->w;
  int y1 = (ay + a->h < by + b->h) ? ay + a->h : by + b->h;
  if(x1 <= x0 || y1 <= y0) return false;
  for(int y = y0; y < y1; y++)
    for(int x = x0; x < x1; x++)
      if(a->px[(y - ay) * a->w + (x - ax)] != b->px[(y - by) * b->w + (x - bx)])
        return false;
  *whole = (x1 - x0) == a->uw && (y1 - y0) == a->uh;
  return true;
}

#define SPRITE_FRAME_IDS 1558

/* One line per distinct reason the game gave for drawing nothing. */
typedef struct { char why[40]; int n; } WhyCount;

static void why_add(WhyCount* w, int cap, int* used, const char* why) {
  for(int i = 0; i < *used; i++)
    if(strcmp(w[i].why, why) == 0) { w[i].n++; return; }
  if(*used >= cap) return;
  snprintf(w[*used].why, sizeof(w[*used].why), "%s", why);
  w[*used].n = 1;
  (*used)++;
}

static int sprite_verify(const uint8_t* rom, size_t romLen) {
  Gallery* gal = gallery_create(rom, romLen);
  Scenes* sc = scenes_create(rom, romLen);
  SpriteRefSet set = { NULL, SPRITE_FRAME_IDS };
  set.ref = calloc(SPRITE_FRAME_IDS, sizeof(SpriteRef));
  if(gal == NULL || sc == NULL || set.ref == NULL) return 2;
  gallery_set_scenes(gal, sc);

  printf("sprite-verify: the game's own render against the game itself\n");
  scenes_observe_request(sc);
  while(scenes_step(sc, 1000)) { }
  GalleryPalTally tal;
  int firstObs = 0;
  gallery_frame_pal_counts(gal, &tal, &firstObs);

  scenes_sprite_walk_observed(sc, sprite_ref_cb, &set);
  int refs = 0;
  for(int i = 0; i < SPRITE_FRAME_IDS; i++) if(set.ref[i].have) refs++;

  /* Forced renders, grouped by scene so the machine is booted four times and
   * not once per frame. */
  int match = 0, partial = 0, differ = 0, noshot = 0;
  for(int mode = 0; mode < 4; mode++) {
    for(int i = 0; i < SPRITE_FRAME_IDS; i++) {
      if(!set.ref[i].have || set.ref[i].shot.mode != mode) continue;
      const SpriteShot* ref = &set.ref[i].shot;
      SpriteShot got;
      if(!scenes_sprite_shot(sc, mode, ref->frameId, ref->flags, &got)) { noshot++; continue; }
      bool whole = false;
      if(got.w == 0) noshot++;
      else if(!shot_same(ref, &got, &whole)) differ++;
      else if(whole) match++;
      else partial++;
      sprite_shot_free(&got);
    }
  }
  printf("  observed frames        %d of %d live frames\n",
         tal.observed, tal.observed + tal.viaScript + tal.derived + tal.guess);
  printf("  crops taken            %d (the running game, OBJ layer only, HDMA restored)\n", refs);
  printf("  no crop                %d (the game showed the frame for one build, where\n"
         "                         its own tiles are still the previous frame's)\n",
         tal.observed - refs);
  printf("  exact, whole sprite    %d/%d\n", match, refs);
  printf("  exact where comparable %d (the game's own sprite met a screen edge)\n", partial);
  printf("  differ                 %d\n", differ);
  printf("  not drawn              %d\n", noshot);

  /* Every live frame once, with the scene and the flag word the page uses. */
  WhyCount why[16];
  int nwhy = 0, drawn = 0, empty = 0;
  int bySource[4] = { 0, 0, 0, 0 };
  int n = gallery_live_count(gal);
  for(int mode = 0; mode < 4; mode++) {
    for(int i = 0; i < n; i++) {
      GalleryFramePlan plan;
      if(!gallery_frame_plan(gal, i, &plan) || plan.mode != mode) continue;
      SpriteShot got;
      if(!scenes_sprite_shot(sc, plan.mode, plan.frameId, plan.flags, &got)) {
        empty++;
        why_add(why, 16, &nwhy, "no machine");
        continue;
      }
      if(got.w > 0) { drawn++; bySource[plan.source & 3]++; }   /* GALLERY_PAL_* */
      else {
        empty++;
        why_add(why, 16, &nwhy, got.why[0] ? got.why : "no reason given");
        printf("    refused: frame %u in %s, flags %04X: %s\n",
               (unsigned) (plan.frameId / 4), scenes_mode_name(plan.mode), plan.flags,
               got.why);
      }
      sprite_shot_free(&got);
    }
  }
  printf("  every live frame       %d drawn, %d refused, of %d\n", drawn, empty, n);
  printf("                         %d at an observed palette, %d observed via a script,\n"
         "                         %d derived, %d a guess\n",
         bySource[0], bySource[1], bySource[2], bySource[3]);
  printf("  derived, ranked        %d of %d with more than one candidate changed their\n"
         "                         top candidate once ranked\n", tal.reordered, tal.multi);
  printf("  observation wins       %d frames where an observation and the derivation\n"
         "                         name different palettes\n", tal.disagree);
  for(int i = 0; i < nwhy; i++) printf("    refused: %-34s %d\n", why[i].why, why[i].n);

  int altTotal = 0, altNear = 0, altShared = 0;
  gallery_alt_counts(gal, &altTotal, &altNear, &altShared);
  int hExact = 0, hTotal = 0, hBeyond = 0;
  gallery_alt_header_fit(gal, &hExact, &hTotal, &hBeyond);
  printf("  alternate headers      %d of %d account for their asset's length exactly;\n"
         "                         %d bytes beyond, where another frame starts\n",
         hExact, hTotal, hBeyond);
  printf("  alternate frames       %d of %d have a nearest live frame;"
         " the most tiles any one\n"
         "                         shares with a live frame is %d\n",
         altNear, altTotal, altShared);

  for(int i = 0; i < SPRITE_FRAME_IDS; i++) sprite_shot_free(&set.ref[i].shot);
  free(set.ref);
  scenes_destroy(sc);
  gallery_destroy(gal);
  printf("sprite-verify: %s\n", differ == 0 ? "every observed frame is exact" : "SOME DIFFER");
  return differ == 0 ? 0 : 1;
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
      case 'x': gallery_press(gal, 1u << GALLERY_BTN_X); break;
      case 'y': gallery_press(gal, 1u << GALLERY_BTN_Y); break;
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
  const char* spriteProbe = NULL;
  bool wantSpriteVerify = false;
  const char* uiShotPath = NULL;
  bool palReport = false;
  int frames = 0;                 /* 0 = run until the user quits */
  int toggleFrame = -1, toggleIters = 0;

  for(int i = 1; i < argc; i++) {
    const char* a = argv[i];
    bool hasNext = i + 1 < argc;
    if(strcmp(a, "--frames") == 0 && hasNext) frames = atoi(argv[++i]);
    else if(strcmp(a, "--input") == 0 && hasNext) inputPath = argv[++i];
    else if(strcmp(a, "--screenshot") == 0 && hasNext) shotPath = argv[++i];
    else if(strcmp(a, "--screenshot-ui") == 0 && hasNext) uiShotPath = argv[++i];
    else if(strcmp(a, "--sprite-pal-report") == 0) palReport = true;
    else if(strcmp(a, "--gallery") == 0 && hasNext) gallerySpec = argv[++i];
    else if(strcmp(a, "--sprite-probe") == 0 && hasNext) spriteProbe = argv[++i];
    else if(strcmp(a, "--sprite-verify") == 0) wantSpriteVerify = true;
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

  if(spriteProbe != NULL) {
    int rc = sprite_probe(rom, romLen, spriteProbe);
    free(rom);
    dlog_close();
    return rc;
  }

  if(wantSpriteVerify) {
    int rc = sprite_verify(rom, romLen);
    free(rom);
    dlog_close();
    return rc;
  }

  /* Every glyph the app draws comes out of the image just verified: the ROM has
   * one font and the UI uses it (recomp/app/romfont.h). Nothing is drawn before
   * this point: a missing or wrong ROM leaves through the two returns above. */
  if(!romfont_load(rom, romLen)) {
    fprintf(stderr, "dream: %s has no font at %06X\n", romPath, ROMFONT_OFF);
    free(rom);
    return 2;
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
  bool wantShot = false;          /* the File menu's Screenshot item, or F12 */
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
          case MENU_ACT_SCREENSHOT: wantShot = true; break;
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
          /* F12 is the Screenshot item's shortcut, and does exactly what it
           * does: the picture is taken below, once this iteration's frame has
           * been drawn, so what is saved is what the user is looking at. */
          if(ev.key.scancode == SDL_SCANCODE_F12) wantShot = true;
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
          else if(pad2 == NULL) {
            SDL_Gamepad* p = SDL_OpenGamepad(ev.gdevice.which);
            if(p != NULL && same_device(pad, p)) { dlog("input: hot-plugged device is the player 1 pad seen again; ignored"); SDL_CloseGamepad(p); }
            else pad2 = p;
          }
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

      /* The sprite page gets a machine of its own, and it is stepped a few
       * milliseconds at a time so the page stays responsive while a frame is
       * being drawn on it. It is kept once built: the boot into a scene and the
       * observation pass are thousands of frames between them and nobody wants
       * either repeated because a page was closed. */
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

    /* The framebuffer is complete and is the picture without the bar: this is
     * where a screenshot is taken from. Nothing above is touched by it. */
    if(wantShot) {
      char said[96];
      save_screenshot(fb, said, sizeof(said));
      menubar_notice(bar, said);
      wantShot = false;
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
      /* A page that needs the scene machine gets it run to completion here: the
       * screenshot flag exists to look at a finished page, not at a progress
       * bar. Interactively the same work is spread over the paused iterations. */
      if(gallery_wants_scenes(gal) && scenes == NULL) {
        scenes = scenes_create(rom, romLen);
        gallery_set_scenes(gal, scenes);
      }
      /* Run the machine out, then apply the moves, then run out whatever they
       * asked for: a move can pick another frame, whose render is another boot. */
      for(int pass = 0; pass < 2; pass++) {
        if(gallery_wants_scenes(gal) && scenes != NULL) {
          scenes_observe_request(scenes);
          gallery_render(gal, fb);    /* the page says which frame it wants */
          while(scenes_step(scenes, 1000)) { }
        }
        if(pass == 0 && colon != NULL) gallery_nav(gal, colon + 1);
      }
      gallery_render(gal, fb);
    }
  }
  /* --sprite-pal-report: how the 1555 live frames come out once the observation
   * pass has run. A number for the record, not a feature. */
  if(palReport) {
    if(scenes == NULL) { scenes = scenes_create(rom, romLen); gallery_set_scenes(gal, scenes); }
    scenes_observe_request(scenes);
    while(scenes_step(scenes, 1000)) { }
    GalleryPalTally t;
    int first = 0, seen = 0, nobs = 0;
    gallery_frame_pal_counts(gal, &t, &first);
    scenes_obs_stats(scenes, &seen, &nobs);
    printf("sprite palettes: %d observed, %d observed via a script, %d derived only,"
           " %d a guess, of %d live frames\n",
           t.observed, t.viaScript, t.derived, t.guess,
           t.observed + t.viaScript + t.derived + t.guess);
    printf("sprite palettes: %d of %d multi-candidate derived frames changed their top"
           " candidate once ranked; %d disagreements, observation wins\n",
           t.reordered, t.multi, t.disagree);
    printf("sprite palettes: %d {scene, frame} cells seen, %d entity-frame observations,"
           " first observed frame is item %d\n", seen, nobs, first);
  }

  if(shotPath != NULL) write_ppm(shotPath, fb);
  /* --screenshot-ui: the window as the user sees it, menu bar included. The bar is
   * drawn with SDL primitives rather than into the framebuffer, so it is the only
   * way to look at it without a display. */
  if(uiShotPath != NULL) {
    SDL_UpdateTexture(texture, NULL, fb, FB_W * (int) sizeof(uint32_t));
    int outW = 0, outH = 0;
    SDL_GetRenderOutputSize(renderer, &outW, &outH);
    int barH = menubar_height(outW);
    SDL_FRect dst = view_rect(&view, outW, outH, barH);
    MenuModel model = { view.scaleMode, view.aspect, view.fullscreen,
                        gallery_is_open(gal), gallery_section(gal) };
    SDL_SetRenderDrawColor(renderer, 0x0A, 0x0C, 0x10, 255);
    SDL_RenderClear(renderer);
    SDL_RenderTexture(renderer, texture, NULL, &dst);
    menubar_draw(bar, renderer, outW, &model);
    SDL_Surface* surf = SDL_RenderReadPixels(renderer, NULL);
    if(surf == NULL) {
      fprintf(stderr, "dream: cannot read the window back: %s\n", SDL_GetError());
    } else {
      SDL_Surface* rgb = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_RGB24);
      FILE* f = rgb != NULL ? fopen(uiShotPath, "wb") : NULL;
      if(f != NULL) {
        fprintf(f, "P6\n%d %d\n255\n", rgb->w, rgb->h);
        for(int yy = 0; yy < rgb->h; yy++)
          fwrite((const uint8_t*) rgb->pixels + (size_t) yy * (size_t) rgb->pitch,
                 1, (size_t) rgb->w * 3u, f);
        fclose(f);
      }
      if(rgb != NULL) SDL_DestroySurface(rgb);
      SDL_DestroySurface(surf);
    }
  }

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
