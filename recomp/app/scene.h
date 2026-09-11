/* scene: the level of a game_mode, composed out of the emulator's own PPU.
 *
 * The Backgrounds page shows a tileset. A scene's picture is several tilesets
 * at their VRAM addresses, a metatile map assembled into a tilemap column by
 * column as the camera moves, a second static tilemap on another layer, and a
 * palette row picked per tilemap word. Nothing assembled from the ROM by hand
 * can be trusted to agree with that. So this
 * module does not assemble anything: it boots a second machine (the same trick
 * recomp/app/music.c uses for the sound driver), walks that machine's own camera
 * across the level with the game's own code doing every upload, and reads the
 * picture out of the PPU that rendered it.
 *
 * The game's machine is never involved; it stays paused, byte for byte.
 *
 * What the composed image is, exactly:
 *
 *  - one 256x224 render per 256 pixels of camera travel, per 224 pixels of
 *    camera height, blitted at the camera position it was taken at, so the
 *    layer the metatile blitter feeds is continuous across the whole level;
 *  - taken with `ppu_runLine` over the machine as the frame left it, with OBJ
 *    off (TM's sprite bit), forced blank off and brightness at 15, and with no
 *    HDMA applied: the per-scanline effects are screen-space, not level-space,
 *    and stitching them would draw the same gradient over and over. The
 *    registers HDMA left at the end of the frame are still there, which for
 *    game_mode 1 means CGRAM $00-$0F holds the last scanline's colours
 *    (docs/data_formats.md, "CGRAM after the init").
 *  - a parallax layer moves at its own rate, so it steps 256 pixels of *its*
 *    travel per screen and can show a seam at a screen boundary. That is what
 *    the layer toggles are for.
 */
#ifndef SCENE_H
#define SCENE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* game_mode 0-3, then the title screen: the same numbering gallery_table.h's
 * GAL_MODE_TITLE uses. */
#define SCENE_MODE_TITLE 4
#define SCENE_MODE_COUNT 5

/* Layer bits for scenes_request(); bit n = BG(n+1) on the main screen. */
#define SCENE_BG1 0x1u
#define SCENE_BG2 0x2u
#define SCENE_BG3 0x4u
#define SCENE_BG4 0x8u
#define SCENE_BG_ALL 0xFu

typedef struct Scenes Scenes;

/* `rom` must stay alive and unmodified for the object's lifetime. Creating one
 * is cheap: no machine is booted until something is asked for. */
const char* scenes_mode_name(int mode);   /* "game_mode 0" ... "title screen" */

Scenes* scenes_create(const uint8_t* rom, size_t romLen);
void scenes_destroy(Scenes* sc);

/* Ask for a mode. Every screen is rendered once with all the layers the mode's TM
 * enables and once with each of those layers on its own, so the page's layer
 * toggle costs nothing after the walk. A request for the mode already composed
 * does nothing. */
void scenes_request(Scenes* sc, int mode);

/* A view of the composed scene: 0 = every layer the mode draws, n = BG n alone. */
#define SCENE_VIEW_ALL 0
#define SCENE_VIEW_COUNT 5

/* Ask for the OAM palette observation pass (see scenes_obs_* below). */
void scenes_observe_request(Scenes* sc);

/* Do up to `budgetMs` of work and return true while a job is still running. */
bool scenes_step(Scenes* sc, int budgetMs);

bool scenes_busy(const Scenes* sc);
int scenes_progress(const Scenes* sc);          /* 0..1000 */
const char* scenes_status(const Scenes* sc);    /* one line, for the page */

/* The composed image, or NULL while there is none. 0xRRGGBBXX, as the app's own
 * framebuffer. */
const uint32_t* scenes_image(const Scenes* sc, int view, int* w, int* h);
bool scenes_view_available(const Scenes* sc, int view);
int scenes_image_mode(const Scenes* sc);
/* Facts about the composed scene, for the page's text: level size in metatiles,
 * the map and metatile-definition addresses the machine's own direct page held,
 * BGMODE, and which BG layers the mode's TM enables. */
typedef struct {
  int cols, rows;          /* level map, in 32x32 metatiles */
  uint32_t mapAddr;        /* $7A/$7C: the map the blitter reads */
  uint32_t metaAddr;       /* $7E/$80: the metatile definitions */
  int bgmode;              /* BGMODE's low three bits */
  bool bg3prio;            /* BGMODE bit 3 */
  int levelBg;             /* the BG whose tilemap is VRAM $7800: the level layer */
  uint16_t bgMap[4];       /* BGnSC: each layer's tilemap word address */
  uint16_t bgChr[4];       /* BG12NBA/BG34NBA: each layer's character base */
  unsigned tm;             /* TM as the mode's init left it */
  int screens;             /* renders the composition took */
  int frames;              /* frames the machine ran */
} SceneInfo;
bool scenes_info(const Scenes* sc, SceneInfo* out);

/* One screen straight off a machine booted the ordinary way (the harness
 * script, no walk, no camera hook), rendered exactly as the composition renders
 * its screens. This is what a composed screen is checked against: same PPU, same
 * masking, a machine that got there by playing the game. `sc` is used for the
 * boot and is left holding no image. */
bool scenes_reference(Scenes* sc, int mode, int view,
                      uint32_t* out256x224, int* camX, int* camY);
/* One composed screen at a camera position of your choosing: the same boot, the
 * same camera hook and the same prime the composition uses, for exactly one
 * screen. It is what separates "the walk draws the right picture" from "the
 * stitch puts it in the right place". */
bool scenes_screen_at(Scenes* sc, int mode, int camX, int camY, int view,
                      uint32_t* out256x224, int* gotX, int* gotY);

#define SCENE_SCREEN_W 256
#define SCENE_SCREEN_H 224

/* ---- one sprite frame, drawn by the game itself ---------------------------
 *
 * The Sprite frames page used to assemble a picture out of the frame file. This
 * assembles nothing. A machine is booted into a scene the way the composition
 * boots one, every entity but slot 0 is set to the game's own "none" values, and
 * slot 0's record is written from a hook at `entity_build_oam_frame`'s own entry
 * address: the frame id asked for, the flag word asked for (palette, priority
 * and the two flip bits), and a position taken from the camera the scene settled
 * at. `entity_frame_loaded` goes to $FFFF in the same write, which is what tells
 * the builder the frame changed and makes it queue the frame's tiles into
 * `entity_tile_job`; `entity_upload_pending_tiles` DMAs that queue at the top of
 * the next NMI, before the builder runs again. Two NMIs after the first forced
 * record, VRAM holds the frame's tiles and OAM holds its sprites.
 *
 * Which OAM entries are the entity's own is measured, not assumed: a hook at
 * $A5B9 (the emitter choice, Y = the slot) reads `oam_write_ptr` before the
 * emitter runs and one at $A6BF reads it after, so a particle the mode's own
 * dispatcher emitted is never inside the crop.
 */
#define SPRITE_FLIP_H 0x4000u
#define SPRITE_FLIP_V 0x8000u

typedef struct {
  uint16_t frameId;     /* the byte index into data_C40000, as entity_frame_id holds it */
  uint16_t flags;       /* the entity_flags the forced record carried */
  int mode;             /* the scene it was drawn in */
  int sprites;          /* OAM entries the entity's own emitter wrote */
  int w, h;             /* the crop; 0x0 when the game drew nothing */
  int x0, y0;           /* where the crop sat on the 256x224 screen */
  int ux0, uy0, uw, uh; /* the box the entity's OAM entries cover, before clamping */
  bool clipped;         /* the crop met a screen edge */
  uint32_t backdrop;    /* CGRAM 0 as the render produced it: what "transparent" looks like */
  char why[40];         /* why it is empty, when it is */
  uint32_t* px;         /* w*h, 0xRRGGBBXX; NULL when w*h is 0 */
} SpriteShot;

void sprite_shot_free(SpriteShot* shot);
bool sprite_shot_copy(SpriteShot* dst, const SpriteShot* src);

/* Ask for one. The first frame asked for in a scene pays that scene's boot (about
 * a second, sliced by scenes_step); every later one costs the handful of NMIs the
 * upload and the OAM build take. */
void scenes_sprite_request(Scenes* sc, int mode, uint16_t frameId, uint16_t flags);
/* The shot, or NULL while none is ready or the request has moved on. */
const SpriteShot* scenes_sprite_get(const Scenes* sc, int mode, uint16_t frameId,
                                    uint16_t flags);
/* The same, blocking: for the headless flags. */
bool scenes_sprite_shot(Scenes* sc, int mode, uint16_t frameId, uint16_t flags,
                        SpriteShot* out);

/* Every frame id the observation scripts show in OAM, as the running game drew
 * it: the walk calls `cb` once per frame id, the first time it appears, with the
 * crop of that entity's own OAM entries. The shot belongs to the walk and is
 * freed after `cb` returns. */
typedef void (*SceneSpriteRefFn)(void* ctx, const SpriteShot* shot);
void scenes_sprite_walk_observed(Scenes* sc, SceneSpriteRefFn cb, void* ctx);

/* ---- OAM palette observation ---------------------------------------------
 *
 * The derived answer to "what palette is this sprite frame drawn with" walks
 * entity init records and animation scripts (docs/data_formats.md 1d). The
 * observed one just runs the game and looks: over the harness's own input
 * scripts, every frame id an entity is showing while its attribute byte is in
 * OAM is recorded with the palette bits that byte carries. */
#define SCENE_OBS_MODES 4

/* palette bits seen for `frameId` in each of the four scenes, as a bitmask of
 * OBJ palettes 0-7. Returns false when the frame was never observed. */
bool scenes_obs_get(const Scenes* sc, uint16_t frameId, uint8_t palMask[SCENE_OBS_MODES]);
/* The entity_flags word the first observation of `frameId` in `mode` carried:
 * the tile slot, the priority bits and the flips the game itself gave it. */
uint16_t scenes_obs_flags(const Scenes* sc, uint16_t frameId, int mode);
/* The CGRAM the composition's first screen was rendered with: what the page's
 * own replay of the init's uploads is checked against. */
bool scenes_cgram(const Scenes* sc, uint16_t* out256);
bool scenes_obs_ready(const Scenes* sc);
/* frames observed, entity-frame observations made, frames the pass covered */
void scenes_obs_stats(const Scenes* sc, int* framesSeen, int* observations);

#endif
