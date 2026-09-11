/* scene: see scene.h.
 *
 * A second machine, booted from reset exactly as recomp/app/music.c boots one for
 * the sound driver, driven into a game_mode with the same input the harness
 * scripts use, and then walked across its own level. Nothing here decodes a
 * tileset, a metatile or a tilemap word: the ROM's own code does every upload and
 * the vendored PPU renders what it left, which is the only way to be sure the
 * picture is the one the game draws.
 */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "scene.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "snes.h"
#include "cart.h"
#include "cpu.h"
#include "ppu.h"

#include "ss_internal.h"
#include "gallery_table.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#endif

/* ---- the machine's own picture ------------------------------------------ */
#define SRC_W 512
#define SRC_H 480
#define SCR_W 256
#define SCR_H 224

/* ---- direct page and low WRAM the walk reads and writes ------------------
 * The names are recomp/src/dream_ram.h's; direct page is $0000 in this ROM
 * (every frame line prints dp=0000), so a DP offset is a WRAM offset. */
#define DP_CAMERA_X      0x0062
#define DP_CAMERA_Y      0x0068
#define DP_TILEMAP_A     0x007A   /* $7A/$7C: metatile map pointer and bank */
#define DP_TILEMAP_A_BK  0x007C
#define DP_TILEMAP_B     0x007E   /* $7E/$80: metatile definition base */
#define DP_META_BANK     0x0080
#define DP_LEVEL_W       0x0086
#define DP_LEVEL_H       0x0088
#define DP_GAME_MODE     0x00A4
#define RAM_ENTITY_TYPE  0x0708
#define RAM_ENTITY_FLAGS 0x0788
#define RAM_ENTITY_FRAME 0x07C8
#define RAM_ENTITY_X     0x0828
#define RAM_ENTITY_X_SUB 0x0848
#define RAM_ENTITY_VEL_X 0x0868
#define RAM_ENTITY_VX_TGT 0x0888
#define RAM_ENTITY_Y     0x08A8
#define RAM_ENTITY_Y_SUB 0x08C8
#define RAM_ENTITY_VEL_Y 0x0948
#define RAM_ENTITY_SUBSTATE 0x07A8
#define RAM_ENTITY_FRAME_LOADED 0x0808   /* $0808: the frame id whose tiles are in VRAM */
#define RAM_ENTITY_Z_DEAD 0x08E8
#define DP_OAM_WRITE_PTR 0x0094          /* $94: the live cursor into the low OAM table */
#define DP_BG2_SCROLL_BIAS 0x0076        /* $76: subtracted from the OAM builder's screen Y */
#define DP_SCREEN_Y_BIAS 0x0092          /* $92: added to it */
#define DP_ENTITY_COUNT 0x00A6           /* $A6: live entity count, slot * 2 units */
#define DP_ENTITY_SCREEN_Y 0x004E        /* $4E: what the OAM builder computed, pre-cull */
#define OAM_LOW_BASE 0x0200              /* $0200..$03FF: 128 entries, 4 bytes each */
#define OAM_LOW_END  0x0400
#define ENTITY_TYPE_NONE 0x8000          /* what the init table's terminator carries */
#define ENTITY_SUBSTATE_DRAWN 2          /* non-zero, and a value the sort order knows */

/* camera_follow_player ($C0:A1B0) is a clamp and nothing else:
 *   camera_x = clamp(entity_x[0] - $80, 0, level_width_mask)
 *   camera_y = clamp(entity_y[0] - $20, 0, level_height_mask)   (not in mode 1)
 * so writing the player's position is the same act as writing the camera's,
 * except that the player's own physics runs *before* the camera does, and puts
 * it back on the ground. The walk therefore writes the position from a hook at
 * that routine's own entry address and then lets the emulated CPU execute the
 * routine itself: no instruction is replaced, and the only change the walk
 * makes is where the player is standing when the camera looks. */
#define CAMERA_FOLLOW_PLAYER 0x00A1B0u
/* $C0:8073 is the instruction after `jsr (game_mode_table,X)` on the scene-start
 * path at $C0:805E: the mode's init has just run and nothing has been drawn yet,
 * so the PPU holds exactly the registers that init programmed.
 * $C0:8102 is the instruction after `jsr (jtbl_C08272,X)` in nmi_handler_gameplay:
 * the mode's own scroll handler has just written the BG scroll registers for this
 * frame, and HDMA has not yet run over them. */
#define AFTER_MODE_INIT   0x008073u
#define AFTER_NMI_SCROLL  0x008102u
/* entity_build_oam_frame's own entry, the emitter choice inside it (Y = the
 * entity slot times two, oam_write_ptr not yet advanced for this entity) and the
 * two addresses its per-entity loop can end at. */
#define BUILD_OAM_FRAME   0x00A538u
#define EMIT_PICK         0x00A5B9u
#define ENTITY_TAIL       0x00A6BFu
#define BUILD_TAIL        0x00A6CDu
#define CAMERA_X_BIAS 0x80
#define CAMERA_Y_BIAS 0x20

/* ---- input schedule -----------------------------------------------------
 * recomp/harness/inputs/mode_cycle.txt, transcribed: Start enters the game, and
 * a Select press while no fade is running advances game_mode by one. The frames
 * are that script's, and so are the settle frames below: the same ones
 * docs/data_formats.md samples CGRAM and OAM at. */
#define BTN_SELECT 0x0004u
#define BTN_START  0x0008u
#define BTN_LEFT   0x0040u
#define BTN_RIGHT  0x0080u
#define BTN_B      0x0001u

typedef struct { int frame; uint16_t state, state2; } ScriptEvent;

static const ScriptEvent kModeCycle[] = {
  { 0, 0, 0 }, { 120, BTN_START, 0 }, { 126, 0, 0 },
  { 240, BTN_SELECT, 0 }, { 246, 0, 0 }, { 360, BTN_SELECT, 0 }, { 366, 0, 0 },
  { 480, BTN_SELECT, 0 }, { 486, 0, 0 }, { 600, BTN_SELECT, 0 }, { 606, 0, 0 },
};
/* recomp/harness/inputs/level_walk_jump.txt and level_attack_enemy.txt, the two
 * scripts that spend their frames inside game_mode 0 with entities moving. */
static const ScriptEvent kWalkJump[] = {
  { 0, 0, 0 }, { 120, BTN_START, 0 }, { 126, 0, 0 }, { 240, BTN_RIGHT | BTN_B, 0 },
  { 1140, BTN_B, 0 }, { 1620, BTN_LEFT, 0 },
};
static const ScriptEvent kAttack[] = {
  { 0, 0, 0 }, { 120, BTN_START, 0 }, { 126, 0, 0 }, { 240, BTN_RIGHT, 0 },
  { 1140, BTN_RIGHT | BTN_B, 0 }, { 1146, BTN_RIGHT, 0 },
};

typedef struct { const ScriptEvent* ev; int n; int frames; const char* name; } Script;

/* Three of the harness's own scripts, run to the frame counts the gate runs them
 * to. p2_enemy_attack.txt was tried as a fourth (it reaches game_mode 2 with a
 * second player attacking) and added 1304 observations without adding a single
 * frame the other three had not already shown, so it is not worth the 2.7
 * seconds. Player 2's column is still driven, for whatever script is added next. */
static const Script kObsScripts[] = {
  { kModeCycle, (int) (sizeof(kModeCycle) / sizeof(kModeCycle[0])), 700, "mode_cycle" },
  { kWalkJump,  (int) (sizeof(kWalkJump)  / sizeof(kWalkJump[0])),  620, "level_walk_jump" },
  { kAttack,    (int) (sizeof(kAttack)    / sizeof(kAttack[0])),    620, "level_attack_enemy" },
};
#define OBS_SCRIPT_COUNT ((int) (sizeof(kObsScripts) / sizeof(kObsScripts[0])))

/* How much of mode_cycle.txt each scene needs, and the frame it is settled at.
 *
 * The script advances the mode every 120 frames, which is fine for a 700-frame
 * gate run and useless for a walk that has to stay in one scene for thousands:
 * so a scene is reached by replaying the script only as far as the Select that
 * selects it, and then holding no button at all. The settle frame is 110 frames
 * after that press: the mode number changes 17 frames after it and the entity
 * roster is repopulated about 47 frames after that (docs/data_formats.md,
 * "Checked against the running game").
 *
 * The title is not one of them: it is a sequence, black then "RARE PRESENTS"
 * then the logo, and 1200 is where the logo stands with no button pressed. */
static const int kScriptEvents[SCENE_MODE_COUNT] = { 3, 5, 7, 9, 1 };
static const int kSettleFrame[SCENE_MODE_COUNT] = { 230, 350, 470, 590, 1200 };

/* game_mode changes about 46 frames before entity_init_from_table repopulates
 * the entity arrays (docs/data_formats.md, "Checked against the running game"),
 * so an observation taken inside that gap compares the old scene's entities
 * against the new scene's number. 60 frames is comfortably past it. */
#define OBS_SETTLE_FRAMES 60

/* ---- job state ---------------------------------------------------------- */

/* Everything render_screen puts back before it draws: what the mode's init
 * programmed, not what HDMA left. */
typedef struct {
  uint8_t mode;
  bool bg3prio;
  struct { uint16_t map, chr, h, v; bool wider, higher, big, mosaic; } bg[4];
  bool main[5], sub[5];
  uint8_t mosaicSize, clipMode, preventMathMode;
  bool addSubscreen, subtractColor, halfColor, mathEnabled[6];
  uint8_t fixR, fixG, fixB;
  WindowLayer win[6];
  uint8_t w1l, w1r, w2l, w2r;
  bool forcedBlank;
  uint8_t brightness;
} PpuRegs;

typedef enum {
  JOB_NONE = 0,
  JOB_BOOT,       /* run the script up to the mode's settle frame */
  JOB_PRIME,      /* walk 32 columns out and back, so the visible window is fresh */
  JOB_SWEEP,      /* walk the level, rendering a screen every 256 pixels */
  JOB_OBSERVE,    /* the OAM palette pass */
  JOB_DONE,
  JOB_FAILED
} JobPhase;

#define FRAME_COUNT ((int) GX_FRAME_COUNT)

struct Scenes {
  const uint8_t* rom;
  size_t romLen;

  Snes* snes;
  SnesState ss;
  int frame;              /* frames run on the current machine */
  const ScriptEvent* script;
  int scriptN;

  JobPhase phase;
  char status[96];
  int progress;           /* 0..1000 */

  /* what was asked for, and what the images hold */
  int wantMode;
  int haveMode;

  /* view 0 = every layer the mode's TM enables, view n = BG n on its own. Only
   * the views the mode actually draws are allocated. */
  uint32_t* img[SCENE_VIEW_COUNT];
  unsigned viewMask[SCENE_VIEW_COUNT];
  int nviews;
  int imgW, imgH;
  SceneInfo info;

  /* the sweep */
  int camXMax, camYMax;
  int band, bandCount;
  int camY;               /* what the walk asks camera_follow_player for */
  int camYTarget;         /* where the band wants the camera to end up */
  int camX;               /* where the walk is now */
  int dir;                /* +8 or -8 */
  int primeLeft, primeBack;
  int capture[32];        /* camera X positions a screen is taken at */
  int captureN, captureAt;
  int sweepDone, sweepTotal, hold;
  int bootTarget;
  bool walking;           /* the camera hook is live */

  /* The PPU registers the mode's init programmed and the scroll its NMI handler
   * wrote, both taken before HDMA ran over them. Every one of these is a register
   * some mode's HDMA channel drives per scanline (game_mode 1 switches BG1's
   * tilemap halfway down the screen, game_mode 3 rewrites TM, the title drives
   * INIDISP), so the value left at the end of a frame is the last scanline's and
   * not the scene's. */
  bool regsValid, scrollValid;
  PpuRegs regs;
  uint16_t hScroll[4], vScroll[4];

  uint8_t* px;            /* the core's 512x480 output */
  uint32_t* screen;       /* one 256x224 render */

  /* ---- which OAM entries each entity's own emitter wrote ----
   * Filled per build by the $A5B9/$A6BF hooks, in low-OAM byte addresses, so a
   * crop can be taken of one entity and nothing else. -1 = not drawn. */
  bool oamTrack;
  int oamStart[16], oamEnd[16];
  uint16_t oamFid[16], oamFlags[16];   /* what the entity was showing at build time */
  int oamSlot;            /* the slot $A5B9 last announced */

  /* ---- the forced sprite record ---- */
  bool spForcing;         /* the slot-0 hook is live */
  int spMode;             /* the scene the machine is sitting in, -1 for none */
  uint16_t spFrame, spFlags;
  int spAnchorX, spAnchorY;   /* what the camera hook pins the player to */
  int spOffX, spOffY;         /* the entity's offset from it, so the sprite centres */
  bool spWanted, spReady, spBooting;
  int spWantMode;
  uint16_t spWantFrame, spWantFlags;
  int spBootTarget;
  SpriteShot spShot;

  /* ---- observation ---- */
  bool obsReady;
  bool obsWanted;
  int obsScript, obsFrame;
  int obsFramesSeen, obsObservations;
  int obsModeSettle, obsLastMode;
  uint16_t sceneCgram[256];   /* CGRAM at the composition's first screen */
  bool sceneCgramValid;

  uint8_t* obsPal;        /* [SCENE_OBS_MODES][FRAME_COUNT] bitmask of palettes */
  uint16_t* obsFlags;     /* the entity_flags word the first observation carried */
};

/* ---- small helpers ------------------------------------------------------- */

static uint64_t now_ms(void) {
#ifdef _WIN32
  return (uint64_t) GetTickCount64();
#else
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t) ts.tv_sec * 1000u + (uint64_t) (ts.tv_nsec / 1000000);
#endif
}

static uint16_t ram16(const Scenes* sc, uint32_t off) {
  const uint8_t* r = sc->snes->ram;
  return (uint16_t) (r[off] | (r[off + 1] << 8));
}

static void ram16w(Scenes* sc, uint32_t off, uint16_t v) {
  sc->snes->ram[off] = (uint8_t) v;
  sc->snes->ram[off + 1] = (uint8_t) (v >> 8);
}

/* kGalleryAssets is sorted by start. */
static int asset_by_start(uint32_t start) {
  int lo = 0, hi = (int) kGalleryAssetCount - 1;
  while(lo <= hi) {
    int mid = (lo + hi) / 2;
    if(kGalleryAssets[mid].start == start) return mid;
    if(kGalleryAssets[mid].start < start) lo = mid + 1;
    else hi = mid - 1;
  }
  return -1;
}

/* The walk renders nothing but the screens it keeps, so the layers are switched
 * off in between: ppu_runLine still runs a line at a time out of the core, and
 * with every layer disabled it costs a backdrop lookup instead of a tilemap
 * fetch per pixel. render_screen puts back the mode's own TM/TS. */
static void layers_off(Scenes* sc) {
  Ppu* p = sc->snes->ppu;
  for(int i = 0; i < 5; i++) {
    p->layer[i].mainScreenEnabled = false;
    p->layer[i].subScreenEnabled = false;
  }
}

/* The one hook the scene machine installs. It replaces nothing: it writes the
 * player's position at the instant camera_follow_player is entered and returns
 * false, so the ROM's own routine runs and clamps it into camera_x/camera_y. */
static void regs_read(const Ppu* p, PpuRegs* r);

static void regs_capture(Scenes* sc) {
  regs_read(sc->snes->ppu, &sc->regs);
  sc->regsValid = true;
}

static void scroll_capture(Scenes* sc) {
  for(int i = 0; i < 4; i++) {
    sc->hScroll[i] = sc->snes->ppu->bgLayer[i].hScroll;
    sc->vScroll[i] = sc->snes->ppu->bgLayer[i].vScroll;
  }
  sc->scrollValid = true;
}

static void sprite_force_record(Scenes* sc);
static void sprite_force_entities(Scenes* sc);

static bool scene_cpu_hook(void* ctx, Cpu* cpu, uint32_t pc24) {
  Scenes* sc = (Scenes*) ctx;
  uint8_t b = (uint8_t) ((pc24 >> 16) & 0x7Fu);
  uint16_t adr = (uint16_t) pc24;
  if(b < 0x40 && adr < 0x8000) return false;
  uint32_t off = (uint32_t) ((b & 0x3Fu) << 16) | adr;
  if(off == AFTER_MODE_INIT) { regs_capture(sc); return false; }
  if(off == AFTER_NMI_SCROLL) { scroll_capture(sc); return false; }
  if(sc->oamTrack) {
    /* One build walks all sixteen entities, so the table is cleared at the
     * routine's entry and filled a slot at a time inside it. */
    if(off == BUILD_OAM_FRAME) {
      for(int i = 0; i < 16; i++) { sc->oamStart[i] = -1; sc->oamEnd[i] = -1; }
      sc->oamSlot = -1;
      if(sc->spForcing) sprite_force_record(sc);
      return false;
    }
    if(off == EMIT_PICK) {
      int slot = (int) ((cpu->y >> 1) & 15u);
      sc->oamSlot = slot;
      sc->oamStart[slot] = (int) ram16(sc, DP_OAM_WRITE_PTR);
      sc->oamFid[slot] = ram16(sc, RAM_ENTITY_FRAME + (uint32_t) (slot * 2));
      sc->oamFlags[slot] = ram16(sc, RAM_ENTITY_FLAGS + (uint32_t) (slot * 2));
      return false;
    }
    if(off == ENTITY_TAIL || off == BUILD_TAIL) {
      /* $A6BF is also where a culled entity lands, and an emitter that filled
       * OAM jumps straight to $A6CD: record the end once, for the slot $A5B9
       * last announced. */
      if(sc->oamSlot >= 0 && sc->oamStart[sc->oamSlot] >= 0 && sc->oamEnd[sc->oamSlot] < 0)
        sc->oamEnd[sc->oamSlot] = (int) ram16(sc, DP_OAM_WRITE_PTR);
      return false;
    }
  }
  if(off != CAMERA_FOLLOW_PLAYER) return false;
  if(sc->spForcing) {
    /* The camera is a clamp on the player's position, and the player is the
     * entity being drawn: pin it, so the camera stays where the scene settled
     * and the sprite's screen position is the entity's offset from it. */
    sprite_force_entities(sc);
    ram16w(sc, RAM_ENTITY_X, (uint16_t) sc->spAnchorX);
    ram16w(sc, RAM_ENTITY_X_SUB, 0);
    ram16w(sc, RAM_ENTITY_VEL_X, 0);
    ram16w(sc, RAM_ENTITY_VX_TGT, 0);
    if(sc->spMode != 1) {
      ram16w(sc, RAM_ENTITY_Y, (uint16_t) sc->spAnchorY);
      ram16w(sc, RAM_ENTITY_Y_SUB, 0);
      ram16w(sc, RAM_ENTITY_VEL_Y, 0);
    }
    return false;
  }
  if(!sc->walking) return false;
  ram16w(sc, RAM_ENTITY_X, (uint16_t) (sc->camX + CAMERA_X_BIAS));
  ram16w(sc, RAM_ENTITY_X_SUB, 0);
  ram16w(sc, RAM_ENTITY_VEL_X, 0);
  ram16w(sc, RAM_ENTITY_VX_TGT, 0);
  if(sc->wantMode != 1) {
    ram16w(sc, RAM_ENTITY_Y, (uint16_t) (sc->camY + CAMERA_Y_BIAS));
    ram16w(sc, RAM_ENTITY_Y_SUB, 0);
    ram16w(sc, RAM_ENTITY_VEL_Y, 0);
  }
  return false;
}

static void machine_free(Scenes* sc) {
  if(sc->snes != NULL) { snes_free(sc->snes); sc->snes = NULL; }
  sc->ss.snes = NULL;
  sc->spMode = -1;
  sc->spForcing = false;
}

static bool machine_boot(Scenes* sc, const ScriptEvent* script, int n) {
  machine_free(sc);
  sc->snes = snes_init();
  if(sc->snes == NULL) return false;
  /* the same forced HiROM/2 MiB/no-SRAM cart the game's machine gets: this
   * ROM's internal header is overwritten by tilemap data (music.c carries the
   * same note for the same reason) */
  cart_load(sc->snes->cart, 2, (uint8_t*) sc->rom, (int) sc->romLen, 0);
  snes_reset(sc->snes, true);
  sc->snes->palTiming = false;
  sc->ss.snes = sc->snes;
  sc->snes->cpu->hook = scene_cpu_hook;
  sc->snes->cpu->hookCtx = sc;
  sc->frame = 0;
  /* the camera hook is off until a walk turns it on: an observation pass must
   * not have the player teleported under it by the last composition's camera */
  sc->walking = false;
  sc->spForcing = false;
  sc->oamTrack = false;
  sc->spMode = -1;
  sc->oamSlot = -1;
  for(int i = 0; i < 16; i++) { sc->oamStart[i] = -1; sc->oamEnd[i] = -1; }
  sc->regsValid = false;
  sc->scrollValid = false;
  sc->script = script;
  sc->scriptN = n;
  return true;
}

static void machine_input(Scenes* sc) {
  uint16_t state = 0, state2 = 0;
  for(int i = 0; i < sc->scriptN; i++)
    if(sc->script[i].frame <= sc->frame) {
      state = sc->script[i].state;
      state2 = sc->script[i].state2;
    }
  for(int b = 0; b < 12; b++) {
    snes_setButtonState(sc->snes, 1, b, (state >> b) & 1);
    snes_setButtonState(sc->snes, 2, b, (state2 >> b) & 1);
  }
}

static void machine_frame(Scenes* sc) {
  machine_input(sc);
  snes_runFrame(sc->snes);
  sc->frame++;
}

/* ---- rendering ----------------------------------------------------------- */

/* One screen out of the PPU as the machine left it.
 *
 * ppu_runLine() renders line by line out of VRAM/CGRAM/OAM and the register
 * state; snes.c calls it once a line, and calling it again over the same frame
 * re-renders that frame. Which is what this is: the machine's own picture, with
 * OBJ off so no sprite lands in a level shot, forced blank off and brightness at
 * 15 so a frame captured mid-fade is not black, and with no HDMA applied,
 * because per-scanline effects are screen-space and stitching them would repeat
 * the same gradient over every screen. */
/* Copy one PpuRegs into or out of the PPU. */
static void regs_read(const Ppu* p, PpuRegs* r) {
  r->mode = p->mode;
  r->bg3prio = p->bg3priority;
  for(int i = 0; i < 4; i++) {
    r->bg[i].map = p->bgLayer[i].tilemapAdr;
    r->bg[i].chr = p->bgLayer[i].tileAdr;
    r->bg[i].wider = p->bgLayer[i].tilemapWider;
    r->bg[i].higher = p->bgLayer[i].tilemapHigher;
    r->bg[i].big = p->bgLayer[i].bigTiles;
    r->bg[i].mosaic = p->bgLayer[i].mosaicEnabled;
    r->bg[i].h = p->bgLayer[i].hScroll;
    r->bg[i].v = p->bgLayer[i].vScroll;
  }
  for(int i = 0; i < 5; i++) {
    r->main[i] = p->layer[i].mainScreenEnabled;
    r->sub[i] = p->layer[i].subScreenEnabled;
  }
  r->mosaicSize = p->mosaicSize;
  r->clipMode = p->clipMode;
  r->preventMathMode = p->preventMathMode;
  r->addSubscreen = p->addSubscreen;
  r->subtractColor = p->subtractColor;
  r->halfColor = p->halfColor;
  for(int i = 0; i < 6; i++) { r->mathEnabled[i] = p->mathEnabled[i]; r->win[i] = p->windowLayer[i]; }
  r->fixR = p->fixedColorR;
  r->fixG = p->fixedColorG;
  r->fixB = p->fixedColorB;
  r->w1l = p->window1left;
  r->w1r = p->window1right;
  r->w2l = p->window2left;
  r->w2r = p->window2right;
  r->forcedBlank = p->forcedBlank;
  r->brightness = p->brightness;
}

static void regs_write(Ppu* p, const PpuRegs* r, bool withScroll) {
  p->mode = r->mode;
  p->bg3priority = r->bg3prio;
  for(int i = 0; i < 4; i++) {
    p->bgLayer[i].tilemapAdr = r->bg[i].map;
    p->bgLayer[i].tileAdr = r->bg[i].chr;
    p->bgLayer[i].tilemapWider = r->bg[i].wider;
    p->bgLayer[i].tilemapHigher = r->bg[i].higher;
    p->bgLayer[i].bigTiles = r->bg[i].big;
    p->bgLayer[i].mosaicEnabled = r->bg[i].mosaic;
    if(withScroll) {
      p->bgLayer[i].hScroll = r->bg[i].h;
      p->bgLayer[i].vScroll = r->bg[i].v;
    }
  }
  for(int i = 0; i < 5; i++) {
    p->layer[i].mainScreenEnabled = r->main[i];
    p->layer[i].subScreenEnabled = r->sub[i];
  }
  p->mosaicSize = r->mosaicSize;
  p->clipMode = r->clipMode;
  p->preventMathMode = r->preventMathMode;
  p->addSubscreen = r->addSubscreen;
  p->subtractColor = r->subtractColor;
  p->halfColor = r->halfColor;
  for(int i = 0; i < 6; i++) { p->mathEnabled[i] = r->mathEnabled[i]; p->windowLayer[i] = r->win[i]; }
  p->fixedColorR = r->fixR;
  p->fixedColorG = r->fixG;
  p->fixedColorB = r->fixB;
  p->window1left = r->w1l;
  p->window1right = r->w1r;
  p->window2left = r->w2l;
  p->window2right = r->w2r;
  p->forcedBlank = r->forcedBlank;
  p->brightness = r->brightness;
}

/* One render out of the machine as the frame left it. `bgMask` picks the BG
 * layers, `obj` the sprite layer; the scene's own registers go back over
 * whatever HDMA left, forced blank comes off and brightness goes to 15. */
static void render_with(Scenes* sc, unsigned layerMask, bool obj, uint32_t* out) {
  Ppu* p = sc->snes->ppu;
  PpuRegs live;
  regs_read(p, &live);

  /* The scene's own registers, over whatever HDMA left at the end of the frame.
   * This is the "HDMA effects off" the page advertises: no channel is disabled;
   * every register a channel drives is put back to the value the mode's init
   * and the mode's own scroll handler wrote (game_mode 1 switches BG1's
   * tilemap halfway down the screen, game_mode 3 rewrites TM, the title drives
   * INIDISP, and the value left at the end of a frame is the last scanline's). */
  if(sc->regsValid) regs_write(p, &sc->regs, false);
  if(sc->scrollValid) {
    for(int i = 0; i < 4; i++) {
      p->bgLayer[i].hScroll = sc->hScroll[i];
      p->bgLayer[i].vScroll = sc->vScroll[i];
    }
  }

  for(int i = 0; i < 4; i++) {
    if(((layerMask >> i) & 1u) == 0) {
      p->layer[i].mainScreenEnabled = false;
      p->layer[i].subScreenEnabled = false;
    }
  }
  p->layer[4].mainScreenEnabled = obj;
  p->layer[4].subScreenEnabled = false;
  p->forcedBlank = false;
  p->brightness = 15;

  for(int line = 1; line <= SCR_H; line++) ppu_runLine(p, line);
  ppu_putPixels(p, sc->px);

  /* the signal's own 256x224 out of the core's 512x480, exactly as main.c's
   * downsample(): row y of the picture sits at 2y+16 */
  for(int y = 0; y < SCR_H; y++) {
    const uint32_t* row = (const uint32_t*) (sc->px + (size_t) (y * 2 + 16) * SRC_W * 4);
    for(int x = 0; x < SCR_W; x++) out[y * SCR_W + x] = row[x * 2];
  }

  regs_write(p, &live, true);   /* the machine goes back exactly as it was */
}

/* A level shot: every BG layer the caller asked for and no sprites. */
static void render_screen(Scenes* sc, unsigned layerMask, uint32_t* out) {
  render_with(sc, layerMask, false, out);
}


/* ---- one sprite frame, drawn by the game's own OAM builder ---------------
 *
 * scene.h has the whole argument. In short: nothing here reads the frame file.
 * Slot 0 of the entity array is written from a hook at entity_build_oam_frame's
 * entry, every other slot is set to the values the init table's terminator
 * carries, and the picture is whatever the PPU makes of the tiles
 * entity_upload_pending_tiles DMA'd and the OAM entries the emitter wrote.
 */

/* OBSEL's two sprite sizes per setting, as the PPU applies them. */
static const int kSpriteSizes[8][2] = {
  { 8, 16 }, { 8, 32 }, { 8, 64 }, { 16, 32 },
  { 16, 64 }, { 32, 64 }, { 16, 32 }, { 16, 32 }
};

/* The roster: one entity and nothing else. entity_count is what the main loop's
 * own update loop runs to and what entity_init_from_table sets, so a roster of
 * one is spelled the way the game spells it, and the other fifteen slots get the
 * init table's own terminator type and the zero substate the OAM builder skips
 * on. Written from the camera hook, which runs before the update loop does. */
static void sprite_force_entities(Scenes* sc) {
  ram16w(sc, DP_ENTITY_COUNT, 2);
  for(int i = 1; i < 16; i++) {
    uint32_t o = (uint32_t) (i * 2);
    ram16w(sc, RAM_ENTITY_TYPE + o, ENTITY_TYPE_NONE);
    ram16w(sc, RAM_ENTITY_SUBSTATE + o, 0);
    ram16w(sc, RAM_ENTITY_FRAME + o, 0);
    ram16w(sc, RAM_ENTITY_FRAME_LOADED + o, 0);
  }
}

/* Slot 0's record. entity_frame_loaded goes to $FFFF rather than to the frame
 * id, so the builder's own "did the frame change" test fires every frame and
 * the tiles are re-queued every frame: the picture is then a function of the
 * frame asked for and not of whatever frame the machine drew before it. */
static void sprite_force_record(Scenes* sc) {
  sprite_force_entities(sc);
  ram16w(sc, RAM_ENTITY_TYPE, 0);
  ram16w(sc, RAM_ENTITY_SUBSTATE, ENTITY_SUBSTATE_DRAWN);
  ram16w(sc, RAM_ENTITY_FRAME, sc->spFrame);
  ram16w(sc, RAM_ENTITY_FRAME_LOADED, 0xFFFF);
  ram16w(sc, RAM_ENTITY_FLAGS, sc->spFlags);
  ram16w(sc, RAM_ENTITY_X, (uint16_t) (sc->spAnchorX + sc->spOffX));
  ram16w(sc, RAM_ENTITY_Y, (uint16_t) (sc->spAnchorY + sc->spOffY));
  ram16w(sc, RAM_ENTITY_Z_DEAD, 0);
}

/* One OAM entry as the PPU reads it: 9-bit X, 8-bit Y, and the size OBSEL and
 * the entry's own high-table bit choose between. */
static void sprite_geom(const Ppu* p, int n, int* x, int* y, int* size) {
  unsigned hi = p->highOam[n >> 2] >> ((unsigned) (n & 3) * 2u);
  int sx = (int) (p->oam[n * 2] & 0xFFu) | (int) ((hi & 1u) << 8);
  if(sx > 255) sx -= 512;
  *x = sx;
  *y = (int) (p->oam[n * 2] >> 8);
  *size = kSpriteSizes[p->objSize & 7][(hi >> 1) & 1u];
}

/* The box one entity's own OAM entries cover. `slot` indexes the entity array;
 * the range comes from the two hooks around the emitter, in low-OAM byte
 * addresses. Returns the number of entries, 0 when the entity drew none. */
static int sprite_bbox(const Scenes* sc, int slot, int* bx0, int* by0,
                       int* bx1, int* by1) {
  int start = sc->oamStart[slot], end = sc->oamEnd[slot];
  if(start < OAM_LOW_BASE || end > OAM_LOW_END || end <= start) return 0;
  const Ppu* p = sc->snes->ppu;
  int x0 = 1 << 20, y0 = 1 << 20, x1 = -(1 << 20), y1 = -(1 << 20), n = 0;
  int anchor = 0;
  for(int a = start; a + 3 < end; a += 4) {
    int i = (a - OAM_LOW_BASE) / 4, x = 0, y = 0, size = 0;
    sprite_geom(p, i, &x, &y, &size);
    if(y > SCENE_SCREEN_H + 16) y -= 256;    /* an entry pushed off the top */
    /* OBJ X is nine bits and wraps, so an entity half off the left edge has
     * entries at both ends of the range. Take each one as the representative
     * nearest the first, which is what puts the whole sprite in one box. */
    if(n == 0) anchor = x;
    else if(x - anchor > 256) x -= 512;
    else if(anchor - x > 256) x += 512;
    if(x < x0) x0 = x;
    if(y < y0) y0 = y;
    if(x + size > x1) x1 = x + size;
    if(y + size > y1) y1 = y + size;
    n++;
  }
  if(n == 0) return 0;
  *bx0 = x0;
  *by0 = y0;
  *bx1 = x1;
  *by1 = y1;
  return n;
}

/* Render the machine's OBJ layer with every entry but one entity's hidden, and
 * crop to that entity's box. Hiding is by X: an entry at X = 256 reads as -256,
 * which is past the widest sprite the PPU has, so the evaluation drops it. */
static bool sprite_capture(Scenes* sc, int slot, SpriteShot* out) {
  Ppu* p = sc->snes->ppu;
  memset(out, 0, sizeof(*out));
  out->mode = sc->spMode;
  out->frameId = sc->spFrame;
  out->flags = sc->spFlags;

  int bx0 = 0, by0 = 0, bx1 = 0, by1 = 0;
  int n = sprite_bbox(sc, slot, &bx0, &by0, &bx1, &by1);
  out->sprites = n;
  if(n == 0) {
    snprintf(out->why, sizeof(out->why), "no sprite emitted (game_mode %d)",
             (int) ram16(sc, DP_GAME_MODE));
    return false;
  }

  uint16_t savedOam[0x100];
  uint8_t savedHigh[0x20];
  memcpy(savedOam, p->oam, sizeof(savedOam));
  memcpy(savedHigh, p->highOam, sizeof(savedHigh));
  int first = (sc->oamStart[slot] - OAM_LOW_BASE) / 4;
  int last = (sc->oamEnd[slot] - OAM_LOW_BASE) / 4;
  for(int i = 0; i < 128; i++) {
    if(i >= first && i < last) continue;
    p->oam[i * 2] = (uint16_t) (p->oam[i * 2] & 0xFF00u);
    p->highOam[i >> 2] = (uint8_t) (p->highOam[i >> 2] | (1u << ((unsigned) (i & 3) * 2u)));
  }
  render_with(sc, 0, true, sc->screen);
  memcpy(p->oam, savedOam, sizeof(savedOam));
  memcpy(p->highOam, savedHigh, sizeof(savedHigh));

  out->ux0 = bx0;
  out->uy0 = by0;
  out->uw = bx1 - bx0;
  out->uh = by1 - by0;
  out->clipped = bx0 < 0 || by0 < 0 || bx1 > SCENE_SCREEN_W || by1 > SCENE_SCREEN_H;
  if(bx0 < 0) bx0 = 0;
  if(by0 < 0) by0 = 0;
  if(bx1 > SCENE_SCREEN_W) bx1 = SCENE_SCREEN_W;
  if(by1 > SCENE_SCREEN_H) by1 = SCENE_SCREEN_H;
  if(bx1 <= bx0 || by1 <= by0) {
    snprintf(out->why, sizeof(out->why), "every sprite is off screen");
    return false;
  }
  out->x0 = bx0;
  out->y0 = by0;
  /* the crop is centred, so the screen's own corner is outside every sprite:
   * that pixel is the backdrop, which is what a transparent sprite pixel shows */
  out->backdrop = sc->screen[0];
  out->w = bx1 - bx0;
  out->h = by1 - by0;
  out->px = malloc((size_t) out->w * out->h * sizeof(uint32_t));
  if(out->px == NULL) {
    out->w = out->h = 0;
    snprintf(out->why, sizeof(out->why), "out of memory");
    return false;
  }
  for(int y = 0; y < out->h; y++)
    memcpy(out->px + (size_t) y * out->w,
           sc->screen + (size_t) (by0 + y) * SCENE_SCREEN_W + bx0,
           (size_t) out->w * sizeof(uint32_t));
  return true;
}

bool sprite_shot_copy(SpriteShot* dst, const SpriteShot* src) {
  if(dst == NULL || src == NULL) return false;
  *dst = *src;
  dst->px = NULL;
  if(src->px == NULL || src->w <= 0 || src->h <= 0) {
    dst->w = 0;
    dst->h = 0;
    return true;
  }
  size_t n = (size_t) src->w * src->h * sizeof(uint32_t);
  dst->px = malloc(n);
  if(dst->px == NULL) { dst->w = 0; dst->h = 0; return false; }
  memcpy(dst->px, src->px, n);
  return true;
}

void sprite_shot_free(SpriteShot* shot) {
  if(shot == NULL) return;
  free(shot->px);
  shot->px = NULL;
  shot->w = 0;
  shot->h = 0;
}

/* Draw the frame the request names on a machine already inside its scene.
 *
 * Three NMIs settle it: the first forced record queues the tiles, the second
 * uploads them at the top of the handler and rebuilds and DMAs the OAM, the
 * third is slack for the camera. Then the entity is nudged until its own box is
 * centred on the screen, which costs three more NMIs a nudge and keeps a large
 * frame off the screen edges. */
#define SPRITE_SETTLE_FRAMES 3
#define SPRITE_CENTRE_ROUNDS 4

/* The emitter drops a sprite whose own screen row reaches $F0, and the frame's
 * records carry that row, so a frame whose records sit high or low can come out
 * with nothing emitted from any one position. These are the screen rows to try,
 * spread over the window entity_build_oam_frame culls the entity against
 * ($0130 - $0090 = -144 to 159); the first that emits anything is what the
 * centring rounds below refine. */
static const int kSpriteTryY[] = { 112, 80, 48, 16, -16, -48, -80, -112, -144, 144 };
#define SPRITE_TRY_COUNT ((int) (sizeof(kSpriteTryY) / sizeof(kSpriteTryY[0])))

static void sprite_draw(Scenes* sc) {
  sc->spFrame = sc->spWantFrame;
  sc->spFlags = sc->spWantFlags;
  sc->spAnchorX = (int) ram16(sc, DP_CAMERA_X) + CAMERA_X_BIAS;
  sc->spAnchorY = (int) ram16(sc, DP_CAMERA_Y) + CAMERA_Y_BIAS;
  /* The entity is the player, so the camera follows it and its own position
   * cancels out of the screen position: what places the sprite is the offset
   * between the anchor the camera hook pins and the position the record carries.
   * entity_build_oam_frame's own arithmetic makes screen_y = offY plus a
   * constant that depends on $76 and $92, so the constant is measured rather
   * than predicted: the routine writes entity_screen_y before it culls, and
   * nothing else in the program writes that cell. */
  sc->spOffX = SCENE_SCREEN_W / 2 - CAMERA_X_BIAS;
  sc->spOffY = SCENE_SCREEN_H / 2 - CAMERA_Y_BIAS + 0x100
             + (int) (int16_t) ram16(sc, DP_BG2_SCROLL_BIAS)
             - (int) (int16_t) ram16(sc, DP_SCREEN_Y_BIAS);
  sc->oamTrack = true;
  sc->spForcing = true;
  for(int f = 0; f < SPRITE_SETTLE_FRAMES; f++) machine_frame(sc);
  const int bias = (int) (int16_t) ram16(sc, DP_ENTITY_SCREEN_Y) - sc->spOffY;
  for(int t = 0; t < SPRITE_TRY_COUNT; t++) {
    int bx0 = 0, by0 = 0, bx1 = 0, by1 = 0;
    if(t > 0 || (int) (int16_t) ram16(sc, DP_ENTITY_SCREEN_Y) != kSpriteTryY[0]) {
      sc->spOffY = kSpriteTryY[t] - bias;
      for(int f = 0; f < SPRITE_SETTLE_FRAMES; f++) machine_frame(sc);
    }
    if(sprite_bbox(sc, 0, &bx0, &by0, &bx1, &by1) > 0) break;
  }
  for(int round = 0; round < SPRITE_CENTRE_ROUNDS; round++) {
    int bx0 = 0, by0 = 0, bx1 = 0, by1 = 0;
    if(sprite_bbox(sc, 0, &bx0, &by0, &bx1, &by1) == 0) break;
    int dx = SCENE_SCREEN_W / 2 - (bx0 + bx1) / 2;
    int dy = SCENE_SCREEN_H / 2 - (by0 + by1) / 2;
    if(dx == 0 && dy == 0) break;
    sc->spOffX += dx;
    sc->spOffY += dy;
    for(int f = 0; f < SPRITE_SETTLE_FRAMES; f++) machine_frame(sc);
  }
  sprite_shot_free(&sc->spShot);
  sprite_capture(sc, 0, &sc->spShot);
  sc->spForcing = false;
  sc->spReady = true;
  sc->spWanted = false;
}

void scenes_sprite_request(Scenes* sc, int mode, uint16_t frameId, uint16_t flags) {
  if(sc == NULL || mode < 0 || mode >= 4) return;
  if(sc->spReady && sc->spShot.frameId == frameId && sc->spShot.flags == flags &&
     sc->spShot.mode == mode)
    return;
  if(sc->spWanted && sc->spWantMode == mode && sc->spWantFrame == frameId &&
     sc->spWantFlags == flags)
    return;
  sc->spWanted = true;
  sc->spReady = false;
  sc->spWantMode = mode;
  sc->spWantFrame = frameId;
  sc->spWantFlags = flags;
  sc->spBootTarget = kSettleFrame[mode];
  snprintf(sc->status, sizeof(sc->status), "drawing frame %u in %s",
           (unsigned) (frameId / 4), kGalleryModeName[mode]);
}

const SpriteShot* scenes_sprite_get(const Scenes* sc, int mode, uint16_t frameId,
                                    uint16_t flags) {
  if(sc == NULL || !sc->spReady) return NULL;
  if(sc->spShot.mode != mode || sc->spShot.frameId != frameId ||
     sc->spShot.flags != flags)
    return NULL;
  return &sc->spShot;
}

/* The machine stays in the scene it was booted into, so only the first frame
 * asked for in a scene pays the boot; the rest cost the handful of NMIs the
 * upload and the OAM build take. */
static void step_sprite(Scenes* sc) {
  if(sc->spMode != sc->spWantMode) {
    if(!sc->spBooting) {
      if(!machine_boot(sc, kModeCycle, kScriptEvents[sc->spWantMode])) {
        sc->spWanted = false;
        snprintf(sc->status, sizeof(sc->status), "no machine");
        return;
      }
      sc->spBooting = true;
    }
    if(sc->frame < sc->spBootTarget) {
      machine_frame(sc);
      sc->progress = sc->frame * 1000 / (sc->spBootTarget > 0 ? sc->spBootTarget : 1);
      snprintf(sc->status, sizeof(sc->status), "booting %s",
               kGalleryModeName[sc->spWantMode]);
      return;
    }
    sc->scriptN = 0;                          /* from here the machine takes no input */
    sc->spBooting = false;
    sc->spMode = sc->spWantMode;
  }
  sc->progress = 1000;
  sprite_draw(sc);
}

bool scenes_sprite_shot(Scenes* sc, int mode, uint16_t frameId, uint16_t flags,
                        SpriteShot* out) {
  if(sc == NULL || out == NULL) return false;
  scenes_sprite_request(sc, mode, frameId, flags);
  while(scenes_step(sc, 1000)) { }
  const SpriteShot* got = scenes_sprite_get(sc, mode, frameId, flags);
  if(got == NULL) return false;
  return sprite_shot_copy(out, got);
}

/* ---- the running game's own sprite crops ----------------------------------
 *
 * The same three input scripts the palette observation runs, walked once, with
 * the crop taken out of the frame the entity was actually drawn on.
 *
 * Which frame that is takes care. The OAM the builder assembles in frame N is
 * DMA'd by the *next* frame's scroll handler, and the tiles it queues are
 * uploaded by the next frame's entity_upload_pending_tiles, so what the PPU
 * holds at the end of frame N is the build of frame N-1, tiles and OAM
 * together. The walk therefore crops with the OAM range the previous frame's
 * build recorded, for the frame id that build used, and only once the same
 * frame id has stood for two builds: on the frame an animation changes, the
 * game itself is showing the previous frame's tiles.
 *
 * A crop that meets a screen edge is reported, but a later unclipped appearance
 * of the same frame id replaces it. */
void scenes_sprite_walk_observed(Scenes* sc, SceneSpriteRefFn cb, void* ctx) {
  if(sc == NULL || cb == NULL) return;
  uint8_t* state = calloc((size_t) FRAME_COUNT, 1);   /* 0 none, 1 clipped, 2 clean */
  if(state == NULL) return;
  for(int si = 0; si < OBS_SCRIPT_COUNT; si++) {
    const Script* s = &kObsScripts[si];
    if(!machine_boot(sc, s->ev, s->n)) break;
    sc->oamTrack = true;
    int lastMode = -1, settle = OBS_SETTLE_FRAMES;
    int prevStart[16], prevEnd[16];
    uint16_t prevFid[16], prevFlags[16];
    for(int i = 0; i < 16; i++) { prevStart[i] = -1; prevEnd[i] = -1; prevFid[i] = 0; prevFlags[i] = 0; }
    for(int f = 0; f < s->frames; f++) {
      machine_frame(sc);
      int mode = (int) ram16(sc, DP_GAME_MODE);
      if(mode != lastMode) { lastMode = mode; settle = OBS_SETTLE_FRAMES; }
      if(settle > 0) settle--;
      for(int slot = 0; slot < 16 && settle == 0 && mode >= 0 && mode < SCENE_OBS_MODES; slot++) {
        uint16_t fid = prevFid[slot];
        if(prevStart[slot] < 0 || prevEnd[slot] <= prevStart[slot]) continue;
        if(fid == 0 || (fid & 3) != 0 || fid >= (uint16_t) (FRAME_COUNT * 4)) continue;
        if(sc->oamFid[slot] != fid) continue;    /* the frame has to have stood still */
        if(state[fid / 4] >= 2) continue;
        sc->spMode = mode;
        sc->spFrame = fid;
        sc->spFlags = prevFlags[slot];
        int keepStart = sc->oamStart[slot], keepEnd = sc->oamEnd[slot];
        sc->oamStart[slot] = prevStart[slot];
        sc->oamEnd[slot] = prevEnd[slot];
        SpriteShot shot;
        bool drew = sprite_capture(sc, slot, &shot);
        sc->oamStart[slot] = keepStart;
        sc->oamEnd[slot] = keepEnd;
        if(drew && (!shot.clipped || state[fid / 4] == 0)) {
          cb(ctx, &shot);
          state[fid / 4] = shot.clipped ? 1 : 2;
        }
        sprite_shot_free(&shot);
      }
      for(int slot = 0; slot < 16; slot++) {
        prevStart[slot] = sc->oamStart[slot];
        prevEnd[slot] = sc->oamEnd[slot];
        prevFid[slot] = sc->oamFid[slot];
        prevFlags[slot] = sc->oamFlags[slot];
      }
    }
    machine_free(sc);
  }
  free(state);
}

static void blit_screen(Scenes* sc, int view, int x0, int y0) {
  uint32_t* img = sc->img[view];
  for(int y = 0; y < SCR_H; y++) {
    int dy = y0 + y;
    if(dy < 0 || dy >= sc->imgH) continue;
    for(int x = 0; x < SCR_W; x++) {
      int dx = x0 + x;
      if(dx < 0 || dx >= sc->imgW) continue;
      img[(size_t) dy * sc->imgW + dx] = sc->screen[y * SCR_W + x];
    }
  }
}

/* One image for every layer the mode draws, plus one with all of them: rendering
 * is cheap next to the walk, so the page's layer toggle is a pointer swap rather
 * than another walk across the level. */
static void images_free(Scenes* sc) {
  for(int v = 0; v < SCENE_VIEW_COUNT; v++) { free(sc->img[v]); sc->img[v] = NULL; }
  sc->nviews = 0;
}

static bool images_alloc(Scenes* sc) {
  images_free(sc);
  size_t n = (size_t) sc->imgW * sc->imgH;
  unsigned all = 0;
  for(int i = 0; i < 4; i++) if(sc->regsValid && sc->regs.main[i]) all |= 1u << i;
  if(all == 0) all = SCENE_BG_ALL;
  sc->viewMask[SCENE_VIEW_ALL] = all;
  sc->img[SCENE_VIEW_ALL] = calloc(n, sizeof(uint32_t));
  if(sc->img[SCENE_VIEW_ALL] == NULL) return false;
  sc->nviews = 1;
  for(int i = 0; i < 4; i++) {
    if(((all >> i) & 1u) == 0) continue;
    sc->viewMask[i + 1] = 1u << i;
    sc->img[i + 1] = calloc(n, sizeof(uint32_t));
    if(sc->img[i + 1] == NULL) return false;
    sc->nviews++;
  }
  return true;
}

/* ---- the sweep ----------------------------------------------------------- */

/* Put the camera where the walk wants it and let the game move it there: the
 * player's position is what camera_follow_player clamps, and every column the
 * blitter writes this frame is written by the ROM. */
/* One frame of the walk. The camera hook above does the placing; everything
 * else (the column the metatile blitter builds, the DMA that uploads it, the
 * scroll registers the mode's NMI handler writes) is the game's own. */
static void walk_step(Scenes* sc) {
  sc->walking = true;
  layers_off(sc);
  machine_frame(sc);
}

static void sweep_begin_band(Scenes* sc) {
  /* Sweep left to right on even bands, right to left on odd ones, so a band
   * change never has to walk the whole level twice. */
  sc->dir = (sc->band & 1) ? -8 : +8;
  sc->camX = (sc->dir > 0) ? 0 : sc->camXMax;
  if(sc->bandCount > 1) {
    sc->camYTarget = sc->band * SCR_H > sc->camYMax ? sc->camYMax : sc->band * SCR_H;
    sc->camY = sc->camYTarget;
  } else {
    sc->camYTarget = sc->camY;
  }
  sc->captureAt = (sc->dir > 0) ? 0 : sc->captureN - 1;
  /* The window at the sweep's first camera position was last written for a
   * different camera Y (or never). One column is written per frame, so walk 32
   * columns out and 32 back before capturing anything. */
  sc->primeLeft = 32;
  sc->primeBack = 32;
  sc->hold = 0;
  sc->phase = JOB_PRIME;
}

static void sweep_plan(Scenes* sc) {
  sc->captureN = 0;
  for(int x = 0; x <= sc->camXMax && sc->captureN < 31; x += SCR_W)
    sc->capture[sc->captureN++] = x;
  if(sc->captureN == 0 || sc->capture[sc->captureN - 1] != sc->camXMax)
    sc->capture[sc->captureN++] = sc->camXMax;
  sc->sweepDone = 0;
  sc->sweepTotal = sc->bandCount * (sc->camXMax / 8 + 1 + 64);
}

/* ---- observation --------------------------------------------------------- */

static bool oam_has_attr(const Scenes* sc, uint8_t attr) {
  const uint16_t* oam = sc->snes->ppu->oam;
  for(int i = 0; i < 128; i++)
    if((uint8_t) (oam[i * 2 + 1] >> 8) == attr) return true;
  return false;
}

/* One frame's worth of "which frame id is on screen, with which palette bits".
 *
 * entity_build_oam_frame leaves entity_flags' high byte as the OAM attribute
 * byte of every sprite of the frame (docs/data_formats.md 1d), so the palette an
 * entity is drawn with is (entity_flags >> 9) & 7. This does not trust that: it
 * records the pair only when that attribute byte is in the OAM the PPU just
 * drew from, so the pair is observed rather than derived a second time. */
static void observe_frame(Scenes* sc) {
  int mode = (int) ram16(sc, DP_GAME_MODE);
  if(mode != sc->obsLastMode) { sc->obsLastMode = mode; sc->obsModeSettle = OBS_SETTLE_FRAMES; }
  if(sc->obsModeSettle > 0) { sc->obsModeSettle--; return; }
  if(mode < 0 || mode >= SCENE_OBS_MODES) return;

  for(int slot = 0; slot < 16; slot++) {
    uint16_t type = ram16(sc, RAM_ENTITY_TYPE + (uint32_t) (slot * 2));
    if((type & 0x8000u) != 0) continue;
    uint16_t fid = ram16(sc, RAM_ENTITY_FRAME + (uint32_t) (slot * 2));
    if(fid == 0 || (fid & 3) != 0 || fid >= (uint16_t) (FRAME_COUNT * 4)) continue;
    uint16_t flags = ram16(sc, RAM_ENTITY_FLAGS + (uint32_t) (slot * 2));
    uint8_t attr = (uint8_t) (flags >> 8);
    if(!oam_has_attr(sc, attr)) continue;
    uint8_t pal = (uint8_t) ((attr >> 1) & 7);
    uint8_t* cell = &sc->obsPal[(size_t) mode * FRAME_COUNT + (fid / 4)];
    if(*cell == 0) {
      sc->obsFramesSeen++;                   /* counted per {scene, frame} cell */
      sc->obsFlags[(size_t) mode * FRAME_COUNT + (fid / 4)] = flags;
    }
    *cell |= (uint8_t) (1u << pal);
    sc->obsObservations++;
  }
}

/* ---- public -------------------------------------------------------------- */

const char* scenes_mode_name(int mode) {
  return (mode >= 0 && mode < SCENE_MODE_COUNT) ? kGalleryModeName[mode] : "?";
}

Scenes* scenes_create(const uint8_t* rom, size_t romLen) {
  Scenes* sc = calloc(1, sizeof(Scenes));
  if(sc == NULL) return NULL;
  sc->rom = rom;
  sc->romLen = romLen;
  sc->haveMode = -1;
  sc->wantMode = -1;
  sc->spMode = -1;
  sc->oamSlot = -1;
  sc->px = calloc((size_t) SRC_W * SRC_H * 4, 1);
  sc->screen = calloc((size_t) SCR_W * SCR_H, sizeof(uint32_t));
  sc->obsPal = calloc((size_t) SCENE_OBS_MODES * FRAME_COUNT, 1);
  sc->obsFlags = calloc((size_t) SCENE_OBS_MODES * FRAME_COUNT, sizeof(uint16_t));
  if(sc->px == NULL || sc->screen == NULL || sc->obsPal == NULL || sc->obsFlags == NULL) {
    scenes_destroy(sc);
    return NULL;
  }
  snprintf(sc->status, sizeof(sc->status), "idle");
  return sc;
}

void scenes_destroy(Scenes* sc) {
  if(sc == NULL) return;
  sprite_shot_free(&sc->spShot);
  machine_free(sc);
  images_free(sc);
  free(sc->px);
  free(sc->screen);
  free(sc->obsPal);
  free(sc->obsFlags);
  free(sc);
}

void scenes_request(Scenes* sc, int mode) {
  if(sc == NULL || mode < 0 || mode >= SCENE_MODE_COUNT) return;
  if(sc->haveMode == mode && sc->phase != JOB_FAILED) return;
  if(sc->wantMode == mode &&
     (sc->phase == JOB_BOOT || sc->phase == JOB_PRIME || sc->phase == JOB_SWEEP)) return;
  sc->wantMode = mode;
  sc->haveMode = -1;
  sc->sceneCgramValid = false;
  sc->phase = JOB_BOOT;
  sc->bootTarget = kSettleFrame[mode];
  sc->progress = 0;
  snprintf(sc->status, sizeof(sc->status), "booting %s", kGalleryModeName[mode]);
  if(!machine_boot(sc, kModeCycle, kScriptEvents[mode])) {
    sc->phase = JOB_FAILED;
    snprintf(sc->status, sizeof(sc->status), "no machine");
  }
}

void scenes_observe_request(Scenes* sc) {
  if(sc == NULL || sc->obsReady) return;
  sc->obsWanted = true;
}

/* The sweep, one frame at a time, so the caller keeps its own frame rate. */
static void step_compose(Scenes* sc) {
  switch(sc->phase) {
    case JOB_BOOT: {
      machine_frame(sc);
      sc->progress = sc->frame * 1000 / (sc->bootTarget > 0 ? sc->bootTarget : 1);
      if(sc->progress > 999) sc->progress = 999;
      if(sc->frame < sc->bootTarget) return;

      /* The title is one screen and has no level: no map, no camera. */
      if(sc->wantMode == SCENE_MODE_TITLE) {
        sc->imgW = SCR_W;
        sc->imgH = SCR_H;
        if(!images_alloc(sc)) { sc->phase = JOB_FAILED; return; }
        for(int v = 0; v < SCENE_VIEW_COUNT; v++) {
          if(sc->img[v] == NULL) continue;
          render_screen(sc, sc->viewMask[v], sc->screen);
          memcpy(sc->img[v], sc->screen, (size_t) SCR_W * SCR_H * sizeof(uint32_t));
        }
        memset(&sc->info, 0, sizeof(sc->info));
        sc->info.bgmode = sc->snes->ppu->mode;
        sc->info.bg3prio = sc->snes->ppu->bg3priority;
        sc->info.screens = 1;
        sc->info.frames = sc->frame;
        for(int i = 0; i < 5; i++)
          if(sc->regsValid ? sc->regs.main[i] : sc->snes->ppu->layer[i].mainScreenEnabled)
            sc->info.tm |= 1u << i;
        sc->haveMode = sc->wantMode;
        sc->phase = JOB_DONE;
        sc->progress = 1000;
        snprintf(sc->status, sizeof(sc->status), "title screen, one screen");
        machine_free(sc);
        return;
      }

      /* From here the machine runs on no input at all. mode_cycle.txt keeps
       * pressing Select every 120 frames, and a walk long enough to cross the
       * level would cycle straight out of the mode it just entered. */
      sc->scriptN = 0;

      /* The level, out of the machine's own direct page. */
      int wmask = (int) ram16(sc, DP_LEVEL_W);
      int hmask = (int) ram16(sc, DP_LEVEL_H);
      uint32_t mapAddr = (uint32_t) (((sc->snes->ram[DP_TILEMAP_A_BK] & 0x3Fu) << 16)
                                     | ram16(sc, DP_TILEMAP_A));
      uint32_t metaAddr = (uint32_t) (((sc->snes->ram[DP_META_BANK] & 0x3Fu) << 16)
                                      | ram16(sc, DP_TILEMAP_B));
      int cols = (wmask + 1 + SCR_W) / 32;
      int mapIdx = asset_by_start(mapAddr);
      int rows = 0;
      if(mapIdx >= 0 && cols > 0)
        rows = (int) ((kGalleryAssets[mapIdx].end - kGalleryAssets[mapIdx].start) /
                      (uint32_t) (2 * cols));
      if(cols <= 0 || rows <= 0) {
        sc->phase = JOB_FAILED;
        snprintf(sc->status, sizeof(sc->status), "no level map at %06X", mapAddr);
        return;
      }

      sc->imgW = cols * 32;
      sc->imgH = rows * 32;
      sc->camXMax = wmask;
      sc->camYMax = sc->imgH - SCR_H;
      if(sc->camYMax > hmask) sc->camYMax = hmask;
      if(sc->camYMax < 0) sc->camYMax = 0;
      /* mode 1 keeps its own vertical scroll (camera_follow_player returns
       * before the vertical half), so its band is wherever the game put it. */
      if(sc->wantMode == 1) {
        sc->camY = (int) ram16(sc, DP_CAMERA_Y);
        sc->bandCount = 1;
        sc->imgH = SCR_H + sc->camY;
      } else {
        sc->camY = 0;
        /* bands at 0, 224, 448, ... and one pinned at the camera's own limit, so
         * the bottom of the level is covered rather than left at the last whole
         * screen */
        sc->bandCount = 1;
        while((sc->bandCount - 1) * SCR_H < sc->camYMax) sc->bandCount++;
      }

      if(!images_alloc(sc)) { sc->phase = JOB_FAILED; return; }

      memset(&sc->info, 0, sizeof(sc->info));
      sc->info.cols = cols;
      sc->info.rows = rows;
      sc->info.mapAddr = mapAddr;
      sc->info.metaAddr = metaAddr;
      sc->info.bgmode = sc->regs.mode;
      sc->info.bg3prio = sc->regs.bg3prio;
      sc->info.levelBg = 0;
      for(int i = 0; i < 4; i++) {
        sc->info.bgMap[i] = sc->regs.bg[i].map;
        sc->info.bgChr[i] = sc->regs.bg[i].chr;
        if(sc->info.bgMap[i] == 0x7800u) sc->info.levelBg = i + 1;
      }
      for(int i = 0; i < 5; i++) if(sc->regs.main[i]) sc->info.tm |= 1u << i;

      sweep_plan(sc);
      sc->band = 0;
      sweep_begin_band(sc);
      return;
    }

    case JOB_PRIME: {
      /* 32 columns out, then 32 back: one column is written per frame, so the
       * 32 columns the first screen shows end up current. */
      if(sc->primeLeft > 0) {
        sc->primeLeft--;
        sc->camX += sc->dir;
        if(sc->camX < 0) sc->camX = 0;
        if(sc->camX > sc->camXMax) sc->camX = sc->camXMax;
        walk_step(sc);
      } else if(sc->primeBack > 0) {
        sc->primeBack--;
        sc->camX -= sc->dir;
        if(sc->camX < 0) sc->camX = 0;
        if(sc->camX > sc->camXMax) sc->camX = sc->camXMax;
        walk_step(sc);
      } else {
        /* camera_follow_player adds camera_y_bias ($74) to the clamped player
         * position, so the camera settles a few pixels off what was asked for.
         * Take that out now the prime has settled it, rather than lose those
         * rows off the top of the band. */
        int gotY = (int) ram16(sc, DP_CAMERA_Y);
        sc->camY += sc->camYTarget - gotY;
        if(sc->camY < 0) sc->camY = 0;
        sc->phase = JOB_SWEEP;
      }
      sc->sweepDone++;
      return;
    }

    case JOB_SWEEP: {
      walk_step(sc);
      sc->sweepDone++;
      sc->progress = sc->sweepTotal > 0 ? sc->sweepDone * 1000 / sc->sweepTotal : 0;
      if(sc->progress > 999) sc->progress = 999;
      snprintf(sc->status, sizeof(sc->status), "%s: band %d/%d, x %d/%d",
               kGalleryModeName[sc->wantMode], sc->band + 1, sc->bandCount,
               sc->camX, sc->camXMax);

      /* The camera is the game's, not ours: read it back and blit where it is.
       *
       * The picture is two frames behind the walk while the walk is moving: each
       * mode's NMI handler writes the scroll registers from the camera the
       * *previous* frame computed. So a capture stops the camera for two frames
       * first, and then what is on the screen is what camera_x/camera_y say. */
      int gotX = (int) ram16(sc, DP_CAMERA_X);
      int gotY = (int) ram16(sc, DP_CAMERA_Y);
      if(sc->hold > 0) {
        if(--sc->hold == 0) {
          if(sc->info.screens == 0) {
            memcpy(sc->sceneCgram, sc->snes->ppu->cgram, sizeof(sc->sceneCgram));
            sc->sceneCgramValid = true;
          }
          for(int v = 0; v < SCENE_VIEW_COUNT; v++) {
            if(sc->img[v] == NULL) continue;
            render_screen(sc, sc->viewMask[v], sc->screen);
            blit_screen(sc, v, gotX, gotY);
          }
          sc->info.screens++;
          sc->captureAt += (sc->dir > 0) ? 1 : -1;
        }
        return;
      }
      if(sc->captureAt >= 0 && sc->captureAt < sc->captureN &&
         ((sc->dir > 0) ? (gotX >= sc->capture[sc->captureAt])
                        : (gotX <= sc->capture[sc->captureAt]))) {
        sc->hold = 2;
        return;
      }

      bool bandDone = (sc->dir > 0) ? (sc->camX >= sc->camXMax) : (sc->camX <= 0);
      if(!bandDone) {
        sc->camX += sc->dir;
        if(sc->camX > sc->camXMax) sc->camX = sc->camXMax;
        if(sc->camX < 0) sc->camX = 0;
        return;
      }
      sc->band++;
      if(sc->band < sc->bandCount) { sweep_begin_band(sc); return; }

      sc->info.frames = sc->frame;
      sc->haveMode = sc->wantMode;
      sc->phase = JOB_DONE;
      sc->progress = 1000;
      snprintf(sc->status, sizeof(sc->status), "%d screens, %d frames",
               sc->info.screens, sc->info.frames);
      machine_free(sc);
      return;
    }

    default: return;
  }
}

static void step_observe(Scenes* sc) {
  if(sc->obsScript >= OBS_SCRIPT_COUNT) {
    sc->obsReady = true;
    sc->obsWanted = false;
    sc->phase = JOB_NONE;
    machine_free(sc);
    return;
  }
  const Script* s = &kObsScripts[sc->obsScript];
  if(sc->snes == NULL || sc->obsFrame == 0) {
    if(!machine_boot(sc, s->ev, s->n)) { sc->phase = JOB_FAILED; return; }
    sc->obsLastMode = -1;
    sc->obsModeSettle = OBS_SETTLE_FRAMES;
  }
  machine_frame(sc);
  observe_frame(sc);
  sc->obsFrame++;
  snprintf(sc->status, sizeof(sc->status), "observing %s %d/%d",
           s->name, sc->obsFrame, s->frames);
  {
    int total = 0, done = 0;
    for(int i = 0; i < OBS_SCRIPT_COUNT; i++) {
      total += kObsScripts[i].frames;
      if(i < sc->obsScript) done += kObsScripts[i].frames;
    }
    done += sc->obsFrame;
    sc->progress = total > 0 ? done * 1000 / total : 1000;
  }
  if(sc->obsFrame >= s->frames) {
    sc->obsScript++;
    sc->obsFrame = 0;
    machine_free(sc);
  }
}

bool scenes_step(Scenes* sc, int budgetMs) {
  if(sc == NULL) return false;
  uint64_t end = now_ms() + (uint64_t) (budgetMs > 0 ? budgetMs : 1);
  do {
    if(sc->phase == JOB_BOOT || sc->phase == JOB_PRIME || sc->phase == JOB_SWEEP) {
      step_compose(sc);
    } else if(sc->spWanted) {
      step_sprite(sc);
    } else if(sc->obsWanted && !sc->obsReady) {
      sc->phase = JOB_OBSERVE;
      step_observe(sc);
    } else {
      return false;
    }
  } while(now_ms() < end);
  return scenes_busy(sc);
}

bool scenes_busy(const Scenes* sc) {
  if(sc == NULL) return false;
  if(sc->phase == JOB_BOOT || sc->phase == JOB_PRIME || sc->phase == JOB_SWEEP) return true;
  if(sc->spWanted) return true;
  return sc->obsWanted && !sc->obsReady;
}

int scenes_progress(const Scenes* sc) { return sc != NULL ? sc->progress : 0; }
const char* scenes_status(const Scenes* sc) { return sc != NULL ? sc->status : ""; }

const uint32_t* scenes_image(const Scenes* sc, int view, int* w, int* h) {
  if(sc == NULL || sc->haveMode < 0) return NULL;
  if(view < 0 || view >= SCENE_VIEW_COUNT || sc->img[view] == NULL) return NULL;
  if(w != NULL) *w = sc->imgW;
  if(h != NULL) *h = sc->imgH;
  return sc->img[view];
}

bool scenes_view_available(const Scenes* sc, int view) {
  return sc != NULL && sc->haveMode >= 0 && view >= 0 && view < SCENE_VIEW_COUNT &&
         sc->img[view] != NULL;
}

int scenes_image_mode(const Scenes* sc) { return sc != NULL ? sc->haveMode : -1; }

bool scenes_info(const Scenes* sc, SceneInfo* out) {
  if(sc == NULL || sc->haveMode < 0 || out == NULL) return false;
  *out = sc->info;
  return true;
}

static unsigned view_mask(const Scenes* sc, int view) {
  if(view <= 0) {
    unsigned all = 0;
    for(int i = 0; i < 4; i++) if(sc->regsValid && sc->regs.main[i]) all |= 1u << i;
    return all != 0 ? all : SCENE_BG_ALL;
  }
  return (view <= 4) ? (1u << (view - 1)) : SCENE_BG_ALL;
}

bool scenes_reference(Scenes* sc, int mode, int view,
                      uint32_t* out, int* camX, int* camY) {
  if(sc == NULL || mode < 0 || mode >= SCENE_MODE_COUNT || out == NULL) return false;
  if(!machine_boot(sc, kModeCycle, kScriptEvents[mode])) return false;
  sc->walking = false;
  while(sc->frame < kSettleFrame[mode]) machine_frame(sc);
  sc->scriptN = 0;
  /* Hold the reference until the camera has stopped moving. The mode's NMI
   * handler writes the scroll registers from the camera it computed on the
   * previous frame, so a frame taken while the camera is still drifting is
   * drawn one line away from the camera_x/camera_y that frame ends with, and
   * the comparison would be against the wrong row of the level rather than
   * against a difference in the picture. */
  if(mode != SCENE_MODE_TITLE) {
    int lastX = -1, lastY = -1, still = 0;
    for(int i = 0; i < 240 && still < 3; i++) {
      int x = (int) ram16(sc, DP_CAMERA_X), y = (int) ram16(sc, DP_CAMERA_Y);
      still = (x == lastX && y == lastY) ? still + 1 : 0;
      lastX = x;
      lastY = y;
      machine_frame(sc);
    }
  }
  if(camX != NULL) *camX = (int) ram16(sc, DP_CAMERA_X);
  if(camY != NULL) *camY = (int) ram16(sc, DP_CAMERA_Y);
  render_screen(sc, view_mask(sc, view), out);
  machine_free(sc);
  return true;
}

bool scenes_screen_at(Scenes* sc, int mode, int camX, int camY, int view,
                      uint32_t* out, int* gotX, int* gotY) {
  if(sc == NULL || mode < 0 || mode >= 4 || out == NULL) return false;
  if(!machine_boot(sc, kModeCycle, kScriptEvents[mode])) return false;
  sc->walking = false;
  while(sc->frame < kSettleFrame[mode]) machine_frame(sc);
  sc->scriptN = 0;
  sc->wantMode = mode;
  int wmask = (int) ram16(sc, DP_LEVEL_W);
  sc->camY = camY;
  sc->camX = camX;
  /* the same 32 columns out, 32 back the composition primes a band with */
  int start = (int) ram16(sc, DP_CAMERA_X);
  int step = (camX >= start) ? 8 : -8;
  for(int i = 0; i < 32; i++) {
    sc->camX = camX + (31 - i) * (-step);
    if(sc->camX < 0) sc->camX = 0;
    if(sc->camX > wmask) sc->camX = wmask;
    walk_step(sc);
  }
  for(int i = 0; i < 32; i++) {
    sc->camX = camX - (31 - i) * (-step);
    if(sc->camX < 0) sc->camX = 0;
    if(sc->camX > wmask) sc->camX = wmask;
    walk_step(sc);
  }
  sc->camX = camX;
  int settled = (int) ram16(sc, DP_CAMERA_Y);
  sc->camY += camY - settled;
  if(sc->camY < 0) sc->camY = 0;
  for(int i = 0; i < 4; i++) walk_step(sc);
  if(gotX != NULL) *gotX = (int) ram16(sc, DP_CAMERA_X);
  if(gotY != NULL) *gotY = (int) ram16(sc, DP_CAMERA_Y);
  render_screen(sc, view_mask(sc, view), out);
  machine_free(sc);
  return true;
}

bool scenes_obs_get(const Scenes* sc, uint16_t frameId, uint8_t palMask[SCENE_OBS_MODES]) {
  if(sc == NULL || sc->obsPal == NULL) return false;
  if((frameId & 3) != 0 || frameId / 4 >= (unsigned) FRAME_COUNT) return false;
  bool any = false;
  for(int m = 0; m < SCENE_OBS_MODES; m++) {
    palMask[m] = sc->obsPal[(size_t) m * FRAME_COUNT + (frameId / 4)];
    if(palMask[m] != 0) any = true;
  }
  return any;
}

uint16_t scenes_obs_flags(const Scenes* sc, uint16_t frameId, int mode) {
  if(sc == NULL || sc->obsFlags == NULL || mode < 0 || mode >= SCENE_OBS_MODES) return 0;
  if((frameId & 3) != 0 || frameId / 4 >= (unsigned) FRAME_COUNT) return 0;
  return sc->obsFlags[(size_t) mode * FRAME_COUNT + (frameId / 4)];
}

bool scenes_cgram(const Scenes* sc, uint16_t* out256) {
  if(sc == NULL || out256 == NULL || !sc->sceneCgramValid) return false;
  memcpy(out256, sc->sceneCgram, sizeof(sc->sceneCgram));
  return true;
}


bool scenes_obs_ready(const Scenes* sc) { return sc != NULL && sc->obsReady; }

void scenes_obs_stats(const Scenes* sc, int* framesSeen, int* observations) {
  if(sc == NULL) return;
  if(framesSeen != NULL) {
    int n = 0;
    for(int i = 0; i < FRAME_COUNT; i++) {
      for(int m = 0; m < SCENE_OBS_MODES; m++)
        if(sc->obsPal[(size_t) m * FRAME_COUNT + i] != 0) { n++; break; }
    }
    *framesSeen = n;
  }
  if(observations != NULL) *observations = sc->obsObservations;
}
