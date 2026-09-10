/* dream — the native application.
 *
 * One window, one game. It loads the user's own DREAM.sfc, installs every
 * routine the recomp has registered (recomp/src), and runs the reference core
 * at the SNES's own 60.0988 Hz with picture, sound and a modern controller.
 *
 * There is no launcher, no menu and no settings screen: docs/RECOMP.md fixes the
 * mapping, and this file is where that mapping lives. Escape quits; nothing else
 * in the program is UI.
 *
 * The two hidden flags (--frames, --input) exist so that this binary can be
 * checked against dream_harness: same ROM, same script, same hook table, and the
 * frame line printed at the end is byte-identical to the harness's. They are not
 * features, they are the proof that the platform layer changed nothing.
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>

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

/* Force the cart to HiROM, 2 MiB, no SRAM — see the note in harness/main.c: this
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

/* argv[1], else ./baserom/DREAM.sfc, else ~/.local/share/dream/DREAM.sfc. */
static const char* find_rom(const char* fromArgv, char* buf, size_t bufLen) {
  if(fromArgv != NULL) return fromArgv;
  FILE* f = fopen("baserom/DREAM.sfc", "rb");
  if(f != NULL) { fclose(f); return "baserom/DREAM.sfc"; }
  const char* home = getenv("HOME");
  if(home != NULL) {
    snprintf(buf, bufLen, "%s/.local/share/dream/DREAM.sfc", home);
    f = fopen(buf, "rb");
    if(f != NULL) { fclose(f); return buf; }
  }
  return "baserom/DREAM.sfc";
}

/* ---- main -------------------------------------------------------------- */

int main(int argc, char** argv) {
  const char* romArg = NULL;
  const char* inputPath = NULL;
  int frames = 0;                 /* 0 = run until the user quits */

  for(int i = 1; i < argc; i++) {
    const char* a = argv[i];
    bool hasNext = i + 1 < argc;
    if(strcmp(a, "--frames") == 0 && hasNext) frames = atoi(argv[++i]);
    else if(strcmp(a, "--input") == 0 && hasNext) inputPath = argv[++i];
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
  uint8_t* rom = read_file(romPath, &romLen);
  if(rom == NULL) {
    fprintf(stderr, "dream: cannot read %s: put your own DREAM.sfc there "
                    "(or pass its path)\n", romPath);
    return 2;
  }
  char hex[41];
  sha1_hex(rom, romLen, hex);
  if(romLen != ROM_SIZE || strcmp(hex, ROM_SHA1) != 0) {
    fprintf(stderr, "dream: %s is not the supported ROM (sha1 %s, expected %s)\n",
            romPath, hex, ROM_SHA1);
    free(rom);
    return 2;
  }

  InputScript script = { NULL, 0 };
  if(inputPath != NULL && !input_load(&script, inputPath)) { free(rom); return 2; }

  /* SDL is initialised even for --frames: the harness comparison is only worth
   * something if it runs the same program, and the dummy drivers make that
   * possible without a display. */
  SDL_SetHint(SDL_HINT_APP_NAME, "Dream: Land of Giants");
  if(!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
    fprintf(stderr, "dream: SDL_Init failed: %s\n", SDL_GetError());
    free(rom);
    return 2;
  }

  SDL_Window* window = NULL;
  SDL_Renderer* renderer = NULL;
  SDL_Texture* texture = NULL;
  SDL_AudioStream* audio = NULL;
  uint8_t* srcPixels = NULL;
  int16_t* audioBuf = NULL;

  if(!SDL_CreateWindowAndRenderer("Dream: Land of Giants", FB_W * 3, FB_H * 3,
                                  SDL_WINDOW_RESIZABLE, &window, &renderer)) {
    fprintf(stderr, "dream: cannot create a window: %s\n", SDL_GetError());
    SDL_Quit();
    free(rom);
    return 2;
  }
  /* Largest whole multiple of 256x224 that fits, centred; nearest-neighbour, so a
   * pixel stays a pixel. */
  SDL_SetRenderLogicalPresentation(renderer, FB_W, FB_H,
                                   SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
  texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBX8888,
                              SDL_TEXTUREACCESS_STREAMING, FB_W, FB_H);
  if(texture == NULL) {
    fprintf(stderr, "dream: cannot create the framebuffer: %s\n", SDL_GetError());
    SDL_Quit();
    free(rom);
    return 2;
  }
  SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
  srcPixels = malloc((size_t) SRC_W * SRC_H * 4);

  /* No audio device is not a reason to refuse to play the game. */
  SDL_AudioSpec want = { SDL_AUDIO_S16, 2, AUDIO_HZ };
  audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &want, NULL, NULL);
  if(audio != NULL) {
    audioBuf = malloc((size_t) (AUDIO_SAMPLES_PER_FRAME + 64) * 4);
    SDL_ResumeAudioStreamDevice(audio);
  }

  Machine m;
  memset(&m, 0, sizeof(m));
  m.snes = snes_init();
  m.ss.snes = m.snes;
  m.hooks = build_table(&m.hookCount);
  m.spcHooks = build_spc_table(&m.spcHookCount);
  if(!machine_load_rom(&m, rom, romLen)) { free(rom); return 2; }
  free(rom);
  snes_setPixelFormat(m.snes, pixelFormatRGBX);
  m.snes->cpu->hook = app_hook;
  m.snes->cpu->hookCtx = &m;
  m.sps.apu = m.snes->apu;
  m.sps.spc = m.snes->apu->spc;
  m.snes->apu->spc->hook = app_spc_hook;
  m.snes->apu->spc->hookCtx = &m;

  SDL_Gamepad* pad = NULL;
  SDL_Gamepad* pad2 = NULL;
  open_gamepads(&pad, &pad2);

  bool running = true;
  int frame = 0;
  uint64_t nextFrame = SDL_GetTicksNS();

  while(running) {
    SDL_Event ev;
    while(SDL_PollEvent(&ev)) {
      switch(ev.type) {
        case SDL_EVENT_QUIT:
          running = false;
          break;
        case SDL_EVENT_KEY_DOWN:
          if(ev.key.scancode == SDL_SCANCODE_ESCAPE) running = false;
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

    uint16_t state, state2;
    if(inputPath != NULL) {
      input_state_at(&script, frame, &state, &state2);
    } else {
      state = (uint16_t) (read_gamepad(pad) | read_keyboard());
      state2 = read_gamepad(pad2);
    }
    machine_set_input(&m, state, state2);

    snes_runFrame(m.snes);

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
    void* dst = NULL;
    int pitch = 0;
    if(SDL_LockTexture(texture, NULL, &dst, &pitch)) {
      /* 512x480 down to the signal's own 256x224: the core doubles every dot and
       * every line, and row y of the picture sits at 2y+16. */
      for(int y = 0; y < FB_H; y++) {
        const uint32_t* row = (const uint32_t*) (srcPixels + (size_t) (y * 2 + 16) * SRC_W * 4);
        uint32_t* d = (uint32_t*) ((uint8_t*) dst + (size_t) y * (size_t) pitch);
        for(int x = 0; x < FB_W; x++) d[x] = row[x * 2];
      }
      SDL_UnlockTexture(texture);
    }
    SDL_RenderClear(renderer);
    SDL_RenderTexture(renderer, texture, NULL, NULL);
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

    frame++;
    if(timed && frame >= frames) break;
  }

  if(timed) {
    machine_snapshot(&m);
    print_frame_line(frame - 1, &m);
  }

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
  return 0;
}
