/* scene: the game's own machine, booted on the side, for the pages that need it.
 *
 * Two things here need a machine rather than a decoder. A sprite frame is drawn
 * by the game's own OAM builder, and which palette an entity is drawn with is
 * observed in the OAM the PPU drew from. Neither can be assembled from the ROM
 * by hand and trusted, so this module boots a second machine (the same trick
 * recomp/app/music.c uses for the sound driver), drives it into a game_mode with
 * the same input the harness scripts use, and reads what the game left.
 *
 * The game's machine is never involved; it stays paused, byte for byte.
 */
#ifndef SCENE_H
#define SCENE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* game_mode 0-3: the same numbering gallery_table.h uses. The title screen is
 * not one of them; nothing here boots into it. */
#define SCENE_MODE_COUNT 4

typedef struct Scenes Scenes;

/* `rom` must stay alive and unmodified for the object's lifetime. Creating one
 * is cheap: no machine is booted until something is asked for. */
const char* scenes_mode_name(int mode);   /* "game_mode 0" ... "game_mode 3" */

Scenes* scenes_create(const uint8_t* rom, size_t romLen);
void scenes_destroy(Scenes* sc);

/* Ask for the OAM palette observation pass (see scenes_obs_* below). */
void scenes_observe_request(Scenes* sc);

/* Do up to `budgetMs` of work and return true while a job is still running. */
bool scenes_step(Scenes* sc, int budgetMs);

bool scenes_busy(const Scenes* sc);
int scenes_progress(const Scenes* sc);          /* 0..1000 */
const char* scenes_status(const Scenes* sc);    /* one line, for the page */

#define SCENE_SCREEN_W 256
#define SCENE_SCREEN_H 224

/* ---- one sprite frame, drawn by the game itself ---------------------------
 *
 * The Sprite frames page used to assemble a picture out of the frame file. This
 * assembles nothing. A machine is booted into a scene, every entity but slot 0
 * is set to the game's own "none" values, and
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
bool scenes_obs_ready(const Scenes* sc);
/* frames observed, entity-frame observations made, frames the pass covered */
void scenes_obs_stats(const Scenes* sc, int* framesSeen, int* observations);

#endif
