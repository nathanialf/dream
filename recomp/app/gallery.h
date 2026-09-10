/* gallery — a viewer for what the ROM holds and the game never shows.
 *
 * Everything here is decoded from the user's own ROM image at run time, with the same
 * formats tools/assetcodec.py implements; the list of what exists comes from the
 * committed manifest config/assets.txt through the generated gallery_table.h. No ROM
 * bytes are compiled in (docs/LEGAL.md).
 *
 * The gallery draws into the app's own 256x224 framebuffer and never touches emulator
 * state: while a page is open main.c simply does not step the machine.
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
