/* gallery: a viewer for what the ROM holds and the game never shows.
 *
 * Everything here is decoded from the user's own ROM image at run time, with the same
 * formats tools/assetcodec.py implements; the list of what exists comes from the
 * committed manifest config/assets.txt through the generated gallery_table.h. No ROM
 * bytes are compiled in (docs/LEGAL.md).
 *
 * The gallery draws into the app's own 256x224 framebuffer and never touches emulator
 * state: while a page is open main.c does not step the machine.
 */
#ifndef GALLERY_H
#define GALLERY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GALLERY_FB_W 256
#define GALLERY_FB_H 224

/* Sections, in menu order. */
enum {
  GALLERY_SEC_SPRITES = 0,   /* live sprite frames */
  GALLERY_SEC_SPRITES_ALT,   /* alternate-format frames, unread by the game */
  GALLERY_SEC_BACKGROUNDS,   /* tilesets, with a palette picker */
  GALLERY_SEC_FONTS,         /* the unreferenced font and the bank $C1 picture strips */
  GALLERY_SEC_PREV_BUILD,    /* the stale build in the first 32 KB */
  GALLERY_SEC_MUSIC,         /* songs and sound effects, played on a scratch machine */
  GALLERY_SEC_SAMPLES,       /* BRR samples, playable */
  GALLERY_SEC_STALE,         /* stale duplicate regions, as text */
  GALLERY_SECTION_COUNT
};

/* Button bits, in the same order snes_setButtonState() numbers them (main.c BTN_*). */
enum {
  GALLERY_BTN_B = 0, GALLERY_BTN_Y, GALLERY_BTN_SELECT, GALLERY_BTN_START,
  GALLERY_BTN_UP, GALLERY_BTN_DOWN, GALLERY_BTN_LEFT, GALLERY_BTN_RIGHT,
  GALLERY_BTN_A, GALLERY_BTN_X, GALLERY_BTN_L, GALLERY_BTN_R
};

typedef struct Gallery Gallery;

/* `rom` must stay alive and unmodified for the gallery's lifetime; it is only read. */
Gallery* gallery_create(const uint8_t* rom, size_t romLen);

/* The scene machine (scene.c), which owns an emulator of its own and so cannot
 * live in here: main.c creates it when a page that needs it is opened and steps
 * it while the page is up. NULL means "not available yet". */
struct Scenes;
void gallery_set_scenes(Gallery* g, struct Scenes* sc);
/* True while an open page still wants that machine stepped. */
bool gallery_wants_scenes(const Gallery* g);

/* How the live sprite frames come out: seen in OAM by the scene machine, derived
 * from the entity/animation tables only, or neither. `firstObserved` is the
 * 1-based position of the first observed frame in the page's own list, which is
 * what --gallery sprites:NAV counts in. Counts are 0 until the pass has run. */
/* What the Sprite frames page draws live frame `item` with: the frame id the
 * entity array holds, the scene, and the entity flag word (OBJ tile slot,
 * palette, priority). `source` says where the palette came from. */
#define GALLERY_PAL_OBSERVED 0
#define GALLERY_PAL_SCRIPT   1
#define GALLERY_PAL_DERIVED  2
#define GALLERY_PAL_GUESS    3

/* How the 1555 live frames come out. `viaScript` is the frames the machine never
 * saw itself but that share an animation script with one it did; `reordered` is
 * how many of the frames whose best evidence is the derivation had more than one
 * derived candidate and came out on a different one once they were ranked;
 * `disagree` is where an observation and the derivation name different palettes,
 * which the observation wins. */
typedef struct {
  int observed, viaScript, derived, guess;
  int multi, reordered, disagree;
} GalleryPalTally;
typedef struct {
  uint16_t frameId;
  uint16_t flags;
  uint8_t mode;
  uint8_t source;
} GalleryFramePlan;
int gallery_live_count(const Gallery* g);
bool gallery_frame_plan(const Gallery* g, int item, GalleryFramePlan* out);
/* Alternate-format frames, and how many of them a live frame shares tiles with. */
void gallery_alt_counts(const Gallery* g, int* total, int* withNearest, int* sharedTiles);
/* How many alternate frames the decoded header accounts for exactly. */
void gallery_alt_header_fit(Gallery* g, int* exact, int* total, int* beyond);

void gallery_frame_pal_counts(const Gallery* g, GalleryPalTally* out,
                              int* firstObserved);
void gallery_destroy(Gallery* g);

const char* gallery_section_name(int section);
int gallery_section_by_name(const char* name);   /* -1 if unknown */

bool gallery_is_open(const Gallery* g);
int gallery_section(const Gallery* g);
void gallery_open(Gallery* g, int section);
void gallery_close(Gallery* g);

/* One UI step. `held` is the current button state; the gallery does its own edge
 * detection and key repeat. Returns true while the page stays open. */
bool gallery_input(Gallery* g, uint16_t held);
/* One synthetic press, for the hidden test/screenshot flags. */
void gallery_press(Gallery* g, uint16_t bits);

/* Draw the current page into a 256x224 framebuffer of 0xRRGGBBXX pixels. */
void gallery_render(Gallery* g, uint32_t* fb);

/* What the Music page asked the app to play. The gallery cannot play a song itself:
 * a song or sound effect only exists as a command to the sound driver, so main.c runs
 * it on a scratch machine of its own (music.c) and never on the game's. */
enum { GALLERY_REQ_NONE = 0, GALLERY_REQ_SONG, GALLERY_REQ_SFX };
bool gallery_take_request(Gallery* g, int* kind, int* arg);

/* A sample the user asked to hear: mono 16-bit PCM at 32000 Hz, valid until the next
 * gallery_input()/gallery_press(). Returns false when nothing is pending. */
bool gallery_take_pcm(Gallery* g, const int16_t** pcm, int* count);

#endif
