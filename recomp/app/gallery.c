/* gallery: the viewer described in gallery.h.
 *
 * The decoders here are C ports of tools/assetcodec.py: the same tile bit-planes, the
 * same 15-bit BGR palette words, the same live/alternate sprite-frame assembly (canvas
 * plus spill strip), the same BRR filter/shift rules. They read the user's ROM image and
 * nothing else; docs/data_formats.md is the reference for every offset that is named
 * here rather than taken from the manifest.
 *
 * Colour is not a picker any more. Every page that can be shown in the colours the
 * game itself gives it is: a scene's CGRAM is rebuilt here by replaying the CGRAM
 * DMAs its init issues (kGalleryPalUploads, generated from the mode-init bodies), a
 * BG tileset is drawn through the CGRAM of the scene that uploads it, and a sprite
 * frame is drawn with the OBJ palette the entity that plays it carries in its
 * `entity_flags` init word. The picker is still there and still does what it did,
 * but it is now an explicit override and says so. docs/data_formats.md's "Palette
 * assignment" section is the derivation; everything below reads it out of the ROM.
 */
#include "gallery.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "romfont.h"
#include "gallery_table.h"
#include "scene.h"

/* ---- ROM landmarks (docs/data_formats.md; the manifest names the same ranges) ---- */

#define ROM_FONT_OFF     0x014FE0u   /* 2bpp font, 96 glyphs, ASCII $20-$7F */
#define ROM_FONT_GLYPHS  96
#define PREV_TILES_OFF   0x004B00u   /* stale build: 4bpp tiles */
#define PREV_PAL_OFF     0x007AC8u   /* stale build: 508 x 15-bit BGR */
#define PREV_ANIM_OFF    0x003000u   /* stale build: animation-script table, 8-byte records */
#define SONG_TABLE_OFF   0x0210B9u   /* 16 x {song ptr24, sample list ptr24} */
#define SFX_BANK1_OFF    0x022E5Cu   /* {dest $2410, words, data}: count, 21 pointers, ... */
#define SFX_BANK2_OFF    0x023070u   /* {dest $2E94, words, data}: the per-song bank 2 */
#define SFX_BANK2_ID     0x60        /* sfx_start: id >= $60 indexes bank 2 */
#define STRIP_TILE_BASE  0x44        /* the strips' tilemaps index VRAM from tile $44 */

/* The picture strips of bank $C1: {tilemap, tileset}, in manifest order. */
static const struct { uint32_t map; uint32_t tiles; } kStrips[3] = {
  { 0x010000u, 0x010100u }, { 0x012800u, 0x012900u }, { 0x013300u, 0x013400u },
};

/* ---- colours -------------------------------------------------------------------- */

#define RGB(r, g, b) (((uint32_t) (r) << 24) | ((uint32_t) (g) << 16) | ((uint32_t) (b) << 8) | 0xFFu)

#define C_BG      RGB(0x0E, 0x14, 0x20)
#define C_TITLE   RGB(0x1E, 0x3A, 0x5F)
#define C_WHITE   RGB(0xFF, 0xFF, 0xFF)
#define C_TEXT    RGB(0xC4, 0xD0, 0xE4)
#define C_DIM     RGB(0x76, 0x84, 0x9C)
#define C_SEL     RGB(0x2E, 0x5A, 0x9A)
#define C_MARK    RGB(0xE8, 0xA8, 0x30)
#define C_RULE    RGB(0x28, 0x3A, 0x54)
#define C_CHECK_A RGB(0x18, 0x1C, 0x24)
#define C_CHECK_B RGB(0x22, 0x28, 0x32)
#define C_WAVE    RGB(0x66, 0xD0, 0xA0)

/* ---- page layout ---------------------------------------------------------------- */

#define TITLE_H    9
#define FOOT_Y     215
#define LINE       8

/* ---- sprite-frame canvas -------------------------------------------------------- */

#define CANVAS_W 288
#define CANVAS_H 480

typedef struct {
  uint8_t hdr[8];
  int n1, n2, tileOff, unk1, unk2, ntiles1, vramOff, flags, ntiles2;
  int nrec, ntiles, spill;
  int altPal;         /* alternate format: the palette its OAM records carry */
  int cw, chh;        /* the assembled part */
  int w, h;           /* including the spill strip */
  bool truncated;
} FrameInfo;

/* ---- palette blocks ------------------------------------------------------------- */

typedef struct { uint32_t off; int rows; const char* name; } PalBlock;

/* Rows of 16 colours, for the override picker and for the pages that have nothing
 * else: the main block every scene draws its CGRAM from (docs/data_formats.md 1a),
 * the 256-colour title block, and the cycling ramp. Which of these rows a scene
 * actually puts where is kGalleryPalUploads' job, not this list's. */
static const PalBlock kMainPals[] = {
  { 0x046C48u, 64, "main" },
  { 0x06A36Bu, 16, "title" },
  { 0x046B88u,  6, "ramp" },
};
static const PalBlock kPrevPals[] = { { PREV_PAL_OFF, 31, "prev" } };

/* Every sound-effect command the program can issue, read off the disassembly: the nine
 * anim_cb_sfx_* callbacks in docs/handler_tables.md (one of which picks between $0704
 * and $0705, and another between $060E and $060D), play_footstep_sound,
 * play_zone_transition_sound and anim_cb_hit_enemies. High byte = channel, low byte =
 * the sfx number sfx_start looks up. Anything not here is a sound the ROM carries and
 * nothing ever asks for. */
static const uint16_t kSfxTriggered[] = {
  0x0506, 0x0507, 0x0508, 0x050A, 0x050B, 0x050C, 0x050F,
  0x0602, 0x0606, 0x060D, 0x060E, 0x0611,
  0x0703, 0x0704, 0x0705, 0x0709, 0x0710, 0x0712,
};

/* ---- the palettes the game actually gives a frame -------------------------------- */

/* At most four distinct {scene, OBJ palette} pairs turn out to apply to any one
 * frame in this ROM; six is the array so the walk can never overflow it. */
#define GAL_MAX_FRAME_PAL 6

typedef struct {
  uint8_t  n;
  uint8_t  mode[GAL_MAX_FRAME_PAL];   /* game_mode whose CGRAM holds the colours */
  uint8_t  pal[GAL_MAX_FRAME_PAL];    /* OBJ palette 0-7 -> CGRAM $80 + 16*pal */
  uint16_t types[GAL_MAX_FRAME_PAL];  /* bit (entity_type / 2) per type that plays it */
  uint16_t flags[GAL_MAX_FRAME_PAL];  /* the whole entity_flags word of that entity */
} FramePal;

/* VRAM is 32 KB of words; a 4bpp tile is 16 of them. */
#define GAL_VRAM_TILES 2048

typedef struct { int item, pal, page; } SecState;

/* Scenes page state: which scene, which layer view, where the window is over the
 * composed image and how far it is zoomed out. */
typedef struct {
  int mode, view, zoom, sx, sy;
} SceneState;

struct Gallery {
  const uint8_t* rom;
  uint32_t romLen;
  bool open;
  int section;
  SecState st[GALLERY_SECTION_COUNT];

  uint16_t prevHeld;
  int rep[12];

  int *live, liveCount;
  int *alt, altCount;
  int *bg, bgCount;
  int *brr, brrCount;
  int *stale, staleCount;
  int *song, songCount;

  bool brrUsed[256];          /* sample number -> referenced by a song's sample list */

  int reqKind, reqArg;        /* what the Music page asked the app to play */

  int16_t* pcm;
  int pcmCount;
  int pcmAsset;               /* asset index the PCM belongs to, -1 if none */
  bool pcmPending;

  uint8_t* canvas;
  uint8_t* claimed;

  /* The 256 colours each scene's init leaves in CGRAM, and which of them that
   * init actually wrote (the rest are whatever the previous scene left). */
  uint16_t cgram[GAL_MODE_COUNT][256];
  uint8_t cgramSet[GAL_MODE_COUNT][256];

  /* VRAM as each scene's init leaves it: every upload replayed in order, so a
   * tile index in one of the scene's maps means here what it means there. The
   * metatile blitter's own $7800 columns are not in it (they are a column a
   * frame as the camera moves): the Scenes page is where those are shown. */
  uint16_t vram[GAL_MODE_COUNT][0x8000];
  uint8_t vramSet[GAL_MODE_COUNT][0x8000];
  /* Per 4bpp VRAM tile: the palette row the scene's own maps reference it with
   * most often, +1 (0 = no map references it), and which upload put it there. */
  uint8_t vramRow[GAL_MODE_COUNT][GAL_VRAM_TILES];
  uint16_t vramVotes[GAL_MODE_COUNT][GAL_VRAM_TILES];
  uint8_t vramSrc[GAL_MODE_COUNT][GAL_VRAM_TILES];

  /* One row per manifest asset; only the sprite_frame rows are filled. */
  FramePal* framePal;
  /* frame id (the byte index into data_C40000) of each sprite_frame asset, so a
   * page can ask the observation pass about the frame it is showing. */
  uint16_t* frameId;

  uint16_t modeFlags[4];   /* each scene's first entity's flag word */

  /* Sprite frames: 0 = the game's own render, 1 = the file layout; and which of
   * the four emit-loop copies the flag word picks. */
  int spriteView, spriteFlip;
#define GAL_SHOT_CACHE 8
  SpriteShot shot[GAL_SHOT_CACHE];
  int shotNext;

  /* Alternate frames: the live frame that shares the most tiles with each, and
   * how many, so an unreadable format can be checked against a readable one. */
  int *altNearest, *altShared;

  /* Backgrounds: 0 = VRAM as the scene's init leaves it, 1 = one manifest asset
   * in file order, 2 = the scene's metatiles, each drawn once. */
  int bgView;
#define GAL_BG_VRAM 0
#define GAL_BG_RAW  1
#define GAL_BG_META 2
#define GAL_BG_VIEWS 3
  bool bgPaged;            /* the user has paged: stop picking a default page */

  Scenes* scenes;          /* main.c owns it; NULL until a page needs one */
  SceneState scene;
};

/* ---- ROM access ----------------------------------------------------------------- */

static uint8_t rd8(const Gallery* g, uint32_t off) {
  return off < g->romLen ? g->rom[off] : 0;
}

static uint16_t rd16(const Gallery* g, uint32_t off) {
  return (uint16_t) (rd8(g, off) | (rd8(g, off + 1) << 8));
}

static uint32_t rd24(const Gallery* g, uint32_t off) {
  return (uint32_t) (rd8(g, off) | (rd8(g, off + 1) << 8) | (rd8(g, off + 2) << 16));
}

/* HiROM: a $C0:0000-relative pointer back to a file offset. */
static uint32_t ptr_to_off(uint32_t p) {
  return (uint32_t) ((((p >> 16) & 0x3Fu) << 16) | (p & 0xFFFFu));
}

/* ---- framebuffer primitives ------------------------------------------------------ */

static void fb_clear(uint32_t* fb, uint32_t c) {
  for(int i = 0; i < GALLERY_FB_W * GALLERY_FB_H; i++) fb[i] = c;
}

static void fb_px(uint32_t* fb, int x, int y, uint32_t c) {
  if(x < 0 || y < 0 || x >= GALLERY_FB_W || y >= GALLERY_FB_H) return;
  fb[y * GALLERY_FB_W + x] = c;
}

static void fb_fill(uint32_t* fb, int x, int y, int w, int h, uint32_t c) {
  for(int r = 0; r < h; r++)
    for(int q = 0; q < w; q++) fb_px(fb, x + q, y + r, c);
}

/* Every character the app puts on screen is the ROM's own font (romfont.c): the
 * game owns exactly one alphabet and this is it. Proportional, so a line fits
 * about what it used to. */
static int fb_text(uint32_t* fb, int x, int y, const char* s, uint32_t c) {
  for(; *s != 0; s++) {
    unsigned ch = (unsigned char) *s;
    int w = romfont_glyph_w(ch);
    for(int gy = 0; gy < ROMFONT_H; gy++)
      for(int gx = 0; gx < w; gx++)
        if(romfont_pixel(ch, gx, gy)) fb_px(fb, x + gx, y + gy, c);
    x += romfont_advance(ch);
  }
  return x;
}

#if defined(__GNUC__)
__attribute__((format(printf, 5, 6)))
#endif
static void fb_textf(uint32_t* fb, int x, int y, uint32_t c, const char* fmt, ...) {
  char buf[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  fb_text(fb, x, y, buf, c);
}

/* ---- SNES tiles and palettes ----------------------------------------------------- */

/* One tile -> 64 palette indices, exactly as assetcodec.tile_to_pixels(). */
static void tile_pixels(const Gallery* g, uint32_t off, int bpp, uint8_t* px) {
  memset(px, 0, 64);
  for(int pair = 0; pair < bpp / 2; pair++) {
    uint32_t base = off + (uint32_t) (pair * 16);
    int lo = pair * 2, hi = pair * 2 + 1;
    for(int r = 0; r < 8; r++) {
      uint8_t p0 = rd8(g, base + (uint32_t) (2 * r));
      uint8_t p1 = rd8(g, base + (uint32_t) (2 * r + 1));
      for(int c = 0; c < 8; c++) {
        int b = 7 - c;
        px[r * 8 + c] |= (uint8_t) ((((p0 >> b) & 1) << lo) | (((p1 >> b) & 1) << hi));
      }
    }
  }
}

/* 15-bit BGR word -> RGB. */
static uint32_t bgr15(uint16_t w) {
  unsigned r = w & 31u, gr = (w >> 5) & 31u, b = (w >> 10) & 31u;
  return RGB((r << 3) | (r >> 2), (gr << 3) | (gr >> 2), (b << 3) | (b >> 2));
}

/* `count` colours of a palette block in the ROM, resolved to RGB. */
static void pal_from_rom(const Gallery* g, uint32_t palOff, int count, uint32_t* out) {
  for(int i = 0; i < count; i++) out[i] = bgr15(rd16(g, palOff + (uint32_t) (2 * i)));
}

/* Defined with the input handling below; the page renderers need it for the
 * palette cursor, which now wraps over "the game's palettes, then the picker". */
static int wrap(int v, int n);

static uint32_t checker(int x, int y) {
  return (((x >> 2) + (y >> 2)) & 1) ? C_CHECK_B : C_CHECK_A;
}

/* ---- palette picker -------------------------------------------------------------- */

static int pal_total(const PalBlock* set, int n) {
  int t = 0;
  for(int i = 0; i < n; i++) t += set[i].rows;
  return t;
}

static uint32_t pal_pick(const PalBlock* set, int n, int idx, const char** name, int* row) {
  int total = pal_total(set, n);
  if(total <= 0) { *name = "-"; *row = 0; return 0; }
  idx = ((idx % total) + total) % total;
  for(int i = 0; i < n; i++) {
    if(idx < set[i].rows) {
      *name = set[i].name;
      *row = idx;
      return set[i].off + (uint32_t) (32 * idx);
    }
    idx -= set[i].rows;
  }
  *name = set[0].name;
  *row = 0;
  return set[0].off;
}

#define MAIN_PAL_N ((int) (sizeof(kMainPals) / sizeof(kMainPals[0])))

/* The picker, now an override rather than the only way to colour a page: fills 16
 * RGB entries and returns the line that says which row the user asked for. */
static void pal_override(const Gallery* g, int idx, uint32_t* rgb, char* line, size_t cap) {
  const char* name;
  int row;
  uint32_t off = pal_pick(kMainPals, MAIN_PAL_N, idx, &name, &row);
  pal_from_rom(g, off, 16, rgb);
  snprintf(line, cap, "OVERRIDE picker: %s row %d @ %06X", name, row, off);
}

/* ---- the manifest ---------------------------------------------------------------- */

static int asset_at(uint32_t start) {
  for(unsigned i = 0; i < kGalleryAssetCount; i++)
    if(kGalleryAssets[i].start == start) return (int) i;
  return -1;
}

static const char* asset_name(int idx) {
  const char* p = kGalleryAssets[idx].path;
  const char* slash = strrchr(p, '/');
  return slash != NULL ? slash + 1 : p;
}

static uint32_t asset_size(int idx) {
  return kGalleryAssets[idx].end - kGalleryAssets[idx].start;
}

static int* collect(unsigned kindMask, int* countOut, bool skipBankC1) {
  int n = 0;
  for(unsigned i = 0; i < kGalleryAssetCount; i++) {
    if(!((kindMask >> kGalleryAssets[i].kind) & 1u)) continue;
    if(skipBankC1 && kGalleryAssets[i].start >= 0x010000u && kGalleryAssets[i].start < 0x0155E0u)
      continue;
    n++;
  }
  int* out = calloc((size_t) (n > 0 ? n : 1), sizeof(int));
  int k = 0;
  for(unsigned i = 0; i < kGalleryAssetCount; i++) {
    if(!((kindMask >> kGalleryAssets[i].kind) & 1u)) continue;
    if(skipBankC1 && kGalleryAssets[i].start >= 0x010000u && kGalleryAssets[i].start < 0x0155E0u)
      continue;
    out[k++] = (int) i;
  }
  *countOut = n;
  return out;
}

/* ---- palette assignment ----------------------------------------------------------
 *
 * docs/data_formats.md, "Palette assignment". The generated header carries the
 * addresses and counts; the bytes at those addresses come from the user's ROM,
 * here, at run time.
 * -------------------------------------------------------------------------------- */

/* kGalleryAssets is sorted by start, so an exact-start lookup is a binary search. */
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

/* Replay every CGRAM write a scene's init issues, in order, into a 256-entry
 * table: the same DMAs `dma_upload_to_cgram` performs, with the same overwrite
 * order, so a later narrower upload wins exactly as it does in the game. */
static void build_mode_cgram(Gallery* g) {
  memset(g->cgram, 0, sizeof(g->cgram));
  memset(g->cgramSet, 0, sizeof(g->cgramSet));
  for(unsigned u = 0; u < kGalleryPalUploadCount; u++) {
    const GalleryPalUpload* p = &kGalleryPalUploads[u];
    if(p->mode >= GAL_MODE_COUNT) continue;
    for(unsigned i = 0; i < p->colours; i++) {
      unsigned e = p->cgadd + i;
      if(e >= 256) break;
      g->cgram[p->mode][e] = rd16(g, p->src + (uint32_t) (2 * i));
      g->cgramSet[p->mode][e] = 1;
    }
  }
}

/* `count` entries of a scene's CGRAM, resolved to RGB. */
static void pal_from_cgram(const Gallery* g, int mode, int first, int count, uint32_t* out) {
  for(int i = 0; i < count; i++) {
    int e = first + i;
    out[i] = bgr15((mode >= 0 && mode < GAL_MODE_COUNT && e >= 0 && e < 256)
                   ? g->cgram[mode][e] : 0);
  }
}

/* ---- which frames the game plays, and with which OBJ palette ---------------------
 *
 * entity_build_oam_frame writes `entity_flags` into $1A before the emitter runs and
 * the emitter only ever touches the low byte, so the OAM attribute byte of every
 * sprite of the frame is the *high* byte of entity_flags: palette in bits 3-1.
 * entity_flags is set once, from byte +12 of the 18-byte init record, and no routine
 * in the ROM writes bits 9-11 again (docs/data_formats.md). So an entity's palette is
 * a constant of its init record, and a frame's palette is the palette of whichever
 * entities can reach the animation that names it.
 * -------------------------------------------------------------------------------- */

#define ANIM_IDS  ((int) GX_ANIM_COUNT)

/* The script index holds 96 distinct offsets; a script runs to whichever of them
 * comes next, or to its own terminator record, whichever is first. */
static uint32_t script_limit(const Gallery* g, uint16_t p) {
  uint32_t best = (uint32_t) p + 0x800u;
  for(int i = 0; i < ANIM_IDS; i++) {
    uint16_t q = rd16(g, GX_ANIM_INDEX + (uint32_t) (2 * i));
    if(q > p && (uint32_t) q < best) best = q;
  }
  return best > 0x10000u ? 0x10000u : best;
}

typedef struct {
  uint16_t type, state, flags, parent;
  uint8_t anims[174];      /* ANIM_IDS; anim id / 2 -> reachable */
} EntitySlot;

/* Every animation id the two-level table at data_C0B7AE gives this entity type,
 * over the twelve even states the state machine can set. */
static void row_anims(const Gallery* g, int type, int bac, uint8_t* set) {
  if(type < 0 || type > 0x10) return;
  uint16_t base = rd16(g, GX_STATE_ANIM + (uint32_t) type);
  for(int st = 0; st < 0x18; st += 2) {
    uint32_t idx = (uint32_t) base + (uint32_t) bac + (uint32_t) st;
    if(GX_STATE_ANIM + idx + 1u >= GX_STATE_ANIM_END) continue;
    uint16_t a = rd16(g, GX_STATE_ANIM + idx);
    if(a != 0 && (a & 1) == 0 && a < (uint16_t) (ANIM_IDS * 2)) set[a / 2] = 1;
  }
}

static void add_anim(uint8_t* set, int a) {
  if(a > 0 && (a & 1) == 0 && a < ANIM_IDS * 2) set[a / 2] = 1;
}

/* frame id -> the file offset of the frame it names, through data_C40000. */
static uint32_t frame_id_offset(const Gallery* g, uint16_t fid) {
  if(fid == 0 || fid >= (uint16_t) (GX_FRAME_COUNT * 4) || (fid & 3) != 0) return 0;
  uint32_t e = GX_FRAME_TABLE + fid;
  return (uint32_t) (((rd8(g, e + 2u) & 0x3Fu) << 16) | rd16(g, e));
}

/* Record one {scene, palette, type} on the frame at `off`, merging into whatever
 * an earlier entity already recorded for the same {scene, palette}. */
static void frame_pal_add(Gallery* g, uint32_t off, int mode, int pal, int type,
                          uint16_t flags) {
  int idx = asset_by_start(off);
  if(idx < 0 || kGalleryAssets[idx].kind != GK_SPRITE_FRAME) return;
  FramePal* fp = &g->framePal[idx];
  for(int i = 0; i < fp->n; i++)
    if(fp->mode[i] == mode && fp->pal[i] == pal) {
      fp->types[i] |= (uint16_t) (1u << (type / 2));
      return;
    }
  if(fp->n >= GAL_MAX_FRAME_PAL) return;
  fp->mode[fp->n] = (uint8_t) mode;
  fp->pal[fp->n] = (uint8_t) pal;
  fp->types[fp->n] = (uint16_t) (1u << (type / 2));
  /* The whole flag word, not just its palette bits: bits 0-8 are the OBJ tile
   * slot the frame's tiles are DMA'd to and bits 12-13 the priority, which is
   * what a forced render has to hand the entity back. */
  fp->flags[fp->n] = flags;
  fp->n++;
}

/* Walk one animation script, recording its frames and following the one link a
 * terminator can carry (duration $FFFF = switch to the animation id in `frame`). */
static void walk_script(Gallery* g, int aid, uint8_t* seen, int mode, int pal, int type,
                        uint16_t flags) {
  if(aid <= 0 || (aid & 1) != 0 || aid >= ANIM_IDS * 2) return;
  uint16_t p = rd16(g, GX_ANIM_INDEX + (uint32_t) aid);
  uint32_t limit = GX_BANK_C4 + script_limit(g, p);
  uint32_t o = GX_BANK_C4 + p;
  for(int i = 0; i < 1024; i++) {
    uint32_t q = o + (uint32_t) (8 * i);
    if(q + 6u > limit) break;
    uint16_t dur = rd16(g, q + 4u), fr = rd16(g, q + 6u);
    if(dur == 0xFFFE) break;
    if(dur == 0xFFFF) {
      if(fr > 0 && (fr & 1) == 0 && fr < (uint16_t) (ANIM_IDS * 2) && !seen[fr / 2]) {
        seen[fr / 2] = 1;
        walk_script(g, fr, seen, mode, pal, type, flags);
      }
      break;
    }
    uint32_t off = frame_id_offset(g, fr);
    if(off != 0) frame_pal_add(g, off, mode, pal, type, flags);
  }
}

/* The whole derivation, once, at gallery_create: for each of the four scenes, the
 * entity roster its init table spawns, the animations each entity can reach, and
 * the frames those animations name. */
/* frame id -> asset, the inverse of frame_id_offset: the observation pass keys
 * on frame ids (that is what the entity arrays hold) and the pages key on
 * manifest assets. */
static void build_frame_ids(Gallery* g) {
  for(unsigned fid = 4; fid < (unsigned) (GX_FRAME_COUNT * 4); fid += 4) {
    uint32_t off = frame_id_offset(g, (uint16_t) fid);
    if(off == 0) continue;
    int idx = asset_by_start(off);
    if(idx >= 0 && kGalleryAssets[idx].kind == GK_SPRITE_FRAME && g->frameId[idx] == 0)
      g->frameId[idx] = (uint16_t) fid;
  }
}

static void build_frame_palettes(Gallery* g) {
  EntitySlot slot[16];
  for(int mode = 0; mode < 4; mode++) {
    int bac = (int) kGalleryStateRowOffset[mode];
    uint32_t x = rd16(g, GX_ENTITY_TABLE + (uint32_t) (2 * mode));
    int n = 0;
    while(n < 16) {
      uint32_t rec = GX_ENTITY_TABLE + x;
      uint16_t t = rd16(g, rec);
      if((t & 0x8000u) != 0) break;
      memset(&slot[n], 0, sizeof(slot[n]));
      slot[n].type = t;
      slot[n].state = rd16(g, rec + 2u);
      slot[n].flags = rd16(g, rec + 12u);
      slot[n].parent = rd16(g, rec + 14u);
      n++;
      x += GX_ENTITY_REC;
    }

    /* The animation an entity starts on. entity_init_from_table takes it from the
     * record's own second word when the type is 0, and from the two-level table
     * otherwise. */
    for(int i = 0; i < n; i++) {
      int a0 = 0;
      if(slot[i].type == 0) {
        a0 = slot[i].state;
      } else if(slot[i].type <= 0x10) {
        uint32_t idx = (uint32_t) rd16(g, GX_STATE_ANIM + slot[i].type)
                     + (uint32_t) bac + slot[i].state;
        if(GX_STATE_ANIM + idx + 1u < GX_STATE_ANIM_END) a0 = rd16(g, GX_STATE_ANIM + idx);
      }
      /* Types 4, 6 and $0A are the spawn transforms: their animation is the
       * parent's, offset. Fill the others first, then them. */
      if(slot[i].type == 4 || slot[i].type == 6 || slot[i].type == 0x0A) continue;
      if(slot[i].type == 0) add_anim(slot[i].anims, a0);
      else { row_anims(g, slot[i].type, bac, slot[i].anims); add_anim(slot[i].anims, a0); }
    }
    for(int i = 0; i < n; i++) {
      int t = slot[i].type;
      if(t != 4 && t != 6 && t != 0x0A) continue;
      int p = slot[i].parent / 2;
      const uint8_t* pa = (p >= 0 && p < n) ? slot[p].anims : NULL;
      if(t == 6) {
        /* entity_spawn_transform_b $C0:9B00: game_mode 1 forces anim $0158,
         * game_mode 2 returns before touching the animation at all. */
        if(mode == 1) { add_anim(slot[i].anims, 0x0158); continue; }
        if(mode == 2) continue;
      }
      if(pa == NULL) continue;
      for(int a = 2; a < ANIM_IDS * 2; a += 2) {
        if(!pa[a / 2]) continue;
        if(t == 4) add_anim(slot[i].anims, a + 2);            /* $9AE4 adc #$0002 */
        else if(t == 6) add_anim(slot[i].anims, a + 4);        /* $9B3F adc #$0004 */
        else {
          /* $9BC0/$9BC3: parent + 4 + $0BB6, and $0BB6 cycles 2 -> 4 -> 0 at
           * $C081DB; at 0 the entity is not drawn at all. */
          add_anim(slot[i].anims, a + 6);
          add_anim(slot[i].anims, a + 8);
        }
      }
    }

    /* The flag word of the scene's own first entity: what a frame with no
     * evidence at all is handed, so its tiles land in the OBJ slot that scene
     * uses rather than at VRAM word zero. */
    if(n > 0) g->modeFlags[mode] = slot[0].flags;

    for(int i = 0; i < n; i++) {
      int pal = (slot[i].flags >> 9) & 7;
      uint8_t seen[174];
      memset(seen, 0, sizeof(seen));
      for(int a = 2; a < ANIM_IDS * 2; a += 2) {
        if(!slot[i].anims[a / 2] || seen[a / 2]) continue;
        seen[a / 2] = 1;
        walk_script(g, a, seen, mode, pal, slot[i].type, slot[i].flags);
      }
    }
  }
}

/* ---- the alternate format against the live one -----------------------------
 *
 * The live code has no path to an alternate-format frame, so its layout is a
 * documented guess and nothing in the ROM can confirm it. What can be confirmed
 * is the art: a 4bpp tile is 32 bytes, and an alt frame whose tiles are byte for
 * byte a live frame's tiles is the same picture in a different container. So
 * every live frame's tiles go into one hash table and every alt frame's tiles
 * are looked up in it; the live frame that shares the most is the alt frame's
 * "nearest live frame", and its placement and palette are what the page borrows.
 *
 * An all-zero tile is not evidence of anything and is left out of the table.
 * What the check finds in this ROM is below, in the README: no alternate frame
 * shares more than two tiles with any live one, so no placement is borrowed and
 * the reconstruction stands on its own. The comparison stays because it is
 * what proves that.
 * -------------------------------------------------------------------------- */

/* How many tiles an alternate frame has to share with a live one before the
 * live one is called its nearest: one or two recurring tiles are the sort of
 * coincidence a 4bpp border or a flat block produces on its own. */
#define ALT_NEAREST_MIN 4

#define TILE_HASH_BITS 18
#define TILE_HASH_SIZE (1u << TILE_HASH_BITS)

typedef struct { uint32_t off; int32_t asset; } TileHashCell;

static uint32_t tile_hash(const uint8_t* p) {
  uint32_t h = 2166136261u;                    /* FNV-1a over the 32 bytes */
  for(int i = 0; i < 32; i++) { h ^= p[i]; h *= 16777619u; }
  return h;
}

/* Where a live frame's tiles start, and how many there are. Same parse as
 * frame_build_live's header, without assembling anything. */
static bool frame_tiles_live(const Gallery* g, int idx, uint32_t* at, int* n) {
  uint32_t st = kGalleryAssets[idx].start, e = kGalleryAssets[idx].end;
  if(e < st + 8u) return false;
  int nrec = rd8(g, st) + rd8(g, st + 1u);
  uint32_t recEnd = st + 8u + (uint32_t) (2 * nrec);
  if(recEnd > e) return false;
  int count = rd8(g, st + 5u) + (rd8(g, st + 7u) & 0x7F);
  int fits = (int) ((e - recEnd) / 32u);
  *at = recEnd;
  *n = count < fits ? count : fits;
  return *n > 0;
}

static bool frame_tiles_alt(const Gallery* g, int idx, uint32_t* at, int* n) {
  uint32_t st = kGalleryAssets[idx].start, e = kGalleryAssets[idx].end;
  if(e < st + 8u) return false;
  int recs = 0;
  while(recs < 200) {
    uint32_t p = st + 8u + (uint32_t) (3 * recs);
    if(p + 2u >= e) break;
    uint8_t attr = rd8(g, p + 2u);
    if(attr < 0x1C || attr > 0x22) break;
    recs++;
  }
  uint32_t recEnd = st + 8u + (uint32_t) (3 * recs);
  if(recEnd >= e) return false;
  *at = recEnd;
  *n = (int) ((e - recEnd) / 32u);
  return *n > 0;
}

static void build_alt_nearest(Gallery* g) {
  g->altNearest = malloc((size_t) kGalleryAssetCount * sizeof(int));
  g->altShared = calloc((size_t) kGalleryAssetCount, sizeof(int));
  TileHashCell* tab = calloc(TILE_HASH_SIZE, sizeof(TileHashCell));
  int* votes = calloc((size_t) kGalleryAssetCount, sizeof(int));
  if(g->altNearest == NULL || g->altShared == NULL || tab == NULL || votes == NULL) {
    free(tab);
    free(votes);
    return;
  }
  for(unsigned i = 0; i < kGalleryAssetCount; i++) g->altNearest[i] = -1;
  for(unsigned i = 0; i < TILE_HASH_SIZE; i++) tab[i].asset = -1;

  for(int i = 0; i < g->liveCount; i++) {
    int idx = g->live[i];
    uint32_t at = 0;
    int n = 0;
    if(!frame_tiles_live(g, idx, &at, &n)) continue;
    for(int t = 0; t < n; t++) {
      uint32_t off = at + (uint32_t) (32 * t);
      if(off + 32u > g->romLen) break;
      bool blank = true;
      for(int b = 0; b < 32 && blank; b++) if(g->rom[off + (uint32_t) b] != 0) blank = false;
      if(blank) continue;
      uint32_t h = tile_hash(g->rom + off) & (TILE_HASH_SIZE - 1u);
      while(tab[h].asset >= 0) {
        if(memcmp(g->rom + tab[h].off, g->rom + off, 32) == 0) break;
        h = (h + 1u) & (TILE_HASH_SIZE - 1u);
      }
      if(tab[h].asset < 0) { tab[h].off = off; tab[h].asset = idx; }
    }
  }

  for(int i = 0; i < g->altCount; i++) {
    int idx = g->alt[i];
    uint32_t at = 0;
    int n = 0;
    if(!frame_tiles_alt(g, idx, &at, &n)) continue;
    int touched[64];
    int ntouched = 0;
    for(int t = 0; t < n; t++) {
      uint32_t off = at + (uint32_t) (32 * t);
      if(off + 32u > g->romLen) break;
      uint32_t h = tile_hash(g->rom + off) & (TILE_HASH_SIZE - 1u);
      while(tab[h].asset >= 0) {
        if(memcmp(g->rom + tab[h].off, g->rom + off, 32) == 0) break;
        h = (h + 1u) & (TILE_HASH_SIZE - 1u);
      }
      if(tab[h].asset < 0) continue;
      int a = tab[h].asset;
      if(votes[a] == 0 && ntouched < 64) touched[ntouched++] = a;
      votes[a]++;
    }
    int best = -1, bestN = 0;
    for(int k = 0; k < ntouched; k++)
      if(votes[touched[k]] > bestN) { bestN = votes[touched[k]]; best = touched[k]; }
    for(int k = 0; k < ntouched; k++) votes[touched[k]] = 0;
    g->altNearest[idx] = bestN >= ALT_NEAREST_MIN ? best : -1;
    g->altShared[idx] = bestN;
  }
  free(tab);
  free(votes);
}

/* How many alternate frames a live frame shares tiles with. */
static int alt_nearest_count(const Gallery* g) {
  int n = 0;
  if(g == NULL || g->altNearest == NULL) return 0;
  for(int i = 0; i < g->altCount; i++) if(g->altNearest[g->alt[i]] >= 0) n++;
  return n;
}

/* ---- background palettes ---------------------------------------------------------
 *
 * A bare tileset page has no tilemap word to take a palette row from, so the row it
 * is drawn with is the row most of its tiles are referenced with in the maps of the
 * scene that uploads it. Returns -1 when nothing references the set.
 * -------------------------------------------------------------------------------- */

static const GalleryBgTileset* bg_tileset_of(uint32_t start) {
  for(unsigned i = 0; i < kGalleryBgTilesetCount; i++)
    if(kGalleryBgTilesets[i].tileset == start) return &kGalleryBgTilesets[i];
  return NULL;
}

static const GalleryObjTileset* obj_tileset_of(uint32_t start) {
  for(unsigned i = 0; i < kGalleryObjTilesetCount; i++)
    if(kGalleryObjTilesets[i].tileset == start) return &kGalleryObjTilesets[i];
  return NULL;
}

/* The scene rewrites some CGRAM entries after its init (an HDMA colour table, a
 * palette-cycle ramp, a zone palette the streaming descriptors carry). If the row
 * being shown overlaps one, say so rather than let the page imply the colours are
 * fixed for the whole frame. */
static const char* pal_animated_note(int mode, int first, int count) {
  for(unsigned i = 0; i < kGalleryPalAnimatedCount; i++) {
    const GalleryPalAnimated* a = &kGalleryPalAnimated[i];
    if(a->mode != mode) continue;
    if(first < a->first + a->count && a->first < first + count) return a->what;
  }
  return NULL;
}

static int bg_default_row(const Gallery* g, const GalleryBgTileset* bt, int ntiles,
                          int* votesOut, int* totalOut) {
  int hist[8];
  memset(hist, 0, sizeof(hist));
  int first = (bt->vram - bt->charBase) / 16;      /* 4bpp: 16 words per tile */
  int total = 0;
  for(int m = 0; m < bt->nmaps; m++) {
    for(uint32_t o = bt->maps[m].start; o + 1u < bt->maps[m].end; o += 2) {
      uint16_t w = rd16(g, o);
      int idx = (int) (w & 0x3FFu) - first;
      if(idx < 0 || idx >= ntiles) continue;
      hist[(w >> 10) & 7]++;
      total++;
    }
  }
  if(total == 0) { if(votesOut) *votesOut = 0; if(totalOut) *totalOut = 0; return -1; }
  int best = 0;
  for(int i = 1; i < 8; i++) if(hist[i] > hist[best]) best = i;
  if(votesOut) *votesOut = hist[best];
  if(totalOut) *totalOut = total;
  return best;
}

/* ---- VRAM as a scene leaves it ---------------------------------------------
 *
 * A tileset on its own is not what the game draws. Each scene uploads several
 * sets to different VRAM addresses and its maps index tiles across them from a
 * character base, so a map word only means what it says once every set is where
 * the init put it. This replays kGalleryVramUploads (the same writes in the
 * same order, generated from the mode-init bodies) into a VRAM image per
 * scene, and then votes each tile's palette row out of the scene's own maps.
 * -------------------------------------------------------------------------- */

static void build_mode_vram(Gallery* g) {
  memset(g->vram, 0, sizeof(g->vram));
  memset(g->vramSet, 0, sizeof(g->vramSet));
  memset(g->vramSrc, 0, sizeof(g->vramSrc));
  for(unsigned u = 0; u < kGalleryVramUploadCount; u++) {
    const GalleryVramUpload* up = &kGalleryVramUploads[u];
    if(up->mode >= GAL_MODE_COUNT) continue;
    for(unsigned i = 0; i < up->words; i++) {
      unsigned a = (unsigned) ((up->vram + i) & 0x7FFFu);
      uint16_t v = (up->src == GAL_VRAM_FILL)
                   ? up->fill
                   : (uint16_t) (rd16(g, up->src + (uint32_t) (2 * i)) + up->bias);
      g->vram[up->mode][a] = v;
      g->vramSet[up->mode][a] = 1;
      if((a & 15u) == 0 && a / 16u < GAL_VRAM_TILES)
        g->vramSrc[up->mode][a / 16u] = (uint8_t) (u + 1);
    }
  }
}

/* The palette row each VRAM tile is referenced with, voted out of the maps the
 * scene's own layers read. A map word's tile field counts from that layer's
 * character base, so the VRAM tile it names is charBase/16 + (word & $3FF). */
static void build_vram_rows(Gallery* g) {
  static uint16_t hist[GAL_MODE_COUNT][GAL_VRAM_TILES][8];
  memset(hist, 0, sizeof(hist));
  memset(g->vramRow, 0, sizeof(g->vramRow));
  memset(g->vramVotes, 0, sizeof(g->vramVotes));
  for(unsigned i = 0; i < kGalleryBgTilesetCount; i++) {
    const GalleryBgTileset* bt = &kGalleryBgTilesets[i];
    if(bt->mode >= GAL_MODE_COUNT) continue;
    for(int m = 0; m < bt->nmaps; m++) {
      for(uint32_t o = bt->maps[m].start; o + 1u < bt->maps[m].end; o += 2) {
        uint16_t w = rd16(g, o);
        unsigned t = (unsigned) (bt->charBase / 16u) + (unsigned) (w & 0x3FFu);
        if(t >= GAL_VRAM_TILES) continue;
        uint16_t* h = hist[bt->mode][t];
        if(h[(w >> 10) & 7] < 0xFFFFu) h[(w >> 10) & 7]++;
      }
    }
  }
  for(int mode = 0; mode < GAL_MODE_COUNT; mode++)
    for(int t = 0; t < GAL_VRAM_TILES; t++) {
      int best = -1, bestN = 0;
      for(int r = 0; r < 8; r++)
        if(hist[mode][t][r] > bestN) { bestN = hist[mode][t][r]; best = r; }
      g->vramRow[mode][t] = (uint8_t) (best < 0 ? 0 : best + 1);
      g->vramVotes[mode][t] = (uint16_t) bestN;
    }
}

/* One tile out of a VRAM image: the same planes tile_pixels() reads out of the
 * ROM, only the bytes are VRAM words (low byte = the even plane). */
static void vram_tile_pixels(const uint16_t* vram, unsigned word, int bpp, uint8_t* px) {
  memset(px, 0, 64);
  for(int pair = 0; pair < bpp / 2; pair++) {
    int lo = pair * 2, hi = pair * 2 + 1;
    for(int r = 0; r < 8; r++) {
      uint16_t w = vram[(word + (unsigned) (pair * 8 + r)) & 0x7FFFu];
      uint8_t p0 = (uint8_t) w, p1 = (uint8_t) (w >> 8);
      for(int c = 0; c < 8; c++) {
        int b = 7 - c;
        px[r * 8 + c] |= (uint8_t) ((((p0 >> b) & 1) << lo) | (((p1 >> b) & 1) << hi));
      }
    }
  }
}

/* ---- which BRR samples the songs actually use ------------------------------------ */

/* Walked from the ROM rather than hard-coded: the song table's second pointer of each
 * pair is a $FFFF-terminated list of sample numbers (docs/data_formats.md 1). Over this
 * ROM that leaves samples 0, 27, 28 and 40 unreferenced, which is what the region table
 * documents. */
static void brr_scan_usage(Gallery* g) {
  memset(g->brrUsed, 0, sizeof(g->brrUsed));
  for(int song = 0; song < 8; song++) {
    uint32_t listPtr = rd24(g, SONG_TABLE_OFF + (uint32_t) (6 * song) + 3u);
    uint32_t off = ptr_to_off(listPtr);
    for(int i = 0; i < 256; i++) {
      uint16_t w = rd16(g, off + (uint32_t) (2 * i));
      if(w == 0xFFFF) break;
      if(w < 256) g->brrUsed[w] = true;
    }
  }
}

/* ---- BRR decoding (assetcodec._brr_decode_block) --------------------------------- */

static int clamp16(int v) {
  return v < -32768 ? -32768 : (v > 32767 ? 32767 : v);
}

static int brr_pred(int filt, int p1, int p2) {
  switch(filt) {
    case 0: return 0;
    case 1: return p1 + ((-p1) >> 4);
    case 2: return p1 * 2 + ((-p1 * 3) >> 5) - p2 + (p2 >> 4);
    default: return p1 * 2 + ((-p1 * 13) >> 6) - p2 + ((p2 * 3) >> 4);
  }
}

static int brr_step(int nib, int shift) {
  int s = nib >= 8 ? nib - 16 : nib;
  if(shift <= 12) return (s << shift) >> 1;
  return s < 0 ? -2048 : 0;
}

/* Decode one sample record into g->pcm. Returns the block count. */
static int brr_decode(Gallery* g, int assetIdx) {
  uint32_t start = kGalleryAssets[assetIdx].start;
  uint32_t end = kGalleryAssets[assetIdx].end;
  uint32_t length = rd16(g, start + 2u);
  uint32_t avail = end > start + 4u ? end - start - 4u : 0u;
  if(length > avail) length = avail;
  int nblocks = (int) (length / 9u);

  free(g->pcm);
  g->pcm = calloc((size_t) (nblocks > 0 ? nblocks : 1) * 16u, sizeof(int16_t));
  g->pcmCount = 0;
  g->pcmAsset = assetIdx;

  int p1 = 0, p2 = 0;
  for(int b = 0; b < nblocks; b++) {
    uint32_t bo = start + 4u + (uint32_t) (b * 9);
    uint8_t head = rd8(g, bo);
    int shift = head >> 4, filt = (head >> 2) & 3;
    for(int i = 0; i < 8; i++) {
      uint8_t byte = rd8(g, bo + 1u + (uint32_t) i);
      int nibs[2] = { byte >> 4, byte & 0xF };
      for(int k = 0; k < 2; k++) {
        int v = clamp16(brr_step(nibs[k], shift) + brr_pred(filt, p1, p2));
        g->pcm[g->pcmCount++] = (int16_t) v;
        p2 = p1;
        p1 = v;
      }
    }
  }
  return nblocks;
}

/* ---- sprite frames --------------------------------------------------------------- */

#define TILE_SEQ_STEP 0x10
#define GRID_MAX 2048

static int next_tile(int t) {
  t += 2;
  if(t & 0x10) t += 0x10;
  return t;
}

static void canvas_reset(Gallery* g) {
  memset(g->canvas, 0, (size_t) CANVAS_W * CANVAS_H);
  memset(g->claimed, 0, (size_t) CANVAS_W * CANVAS_H);
}

static bool canvas_free(const Gallery* g, int x, int y) {
  for(int r = 0; r < 8; r++)
    for(int c = 0; c < 8; c++) {
      int px = x + c, py = y + r;
      if(px < 0 || py < 0 || px >= CANVAS_W || py >= CANVAS_H) continue;
      if(g->claimed[py * CANVAS_W + px]) return false;
    }
  return true;
}

static void canvas_put(Gallery* g, int x, int y, const uint8_t* px) {
  for(int r = 0; r < 8; r++)
    for(int c = 0; c < 8; c++) {
      int dx = x + c, dy = y + r;
      if(dx < 0 || dy < 0 || dx >= CANVAS_W || dy >= CANVAS_H) continue;
      g->canvas[dy * CANVAS_W + dx] = px[r * 8 + c];
      g->claimed[dy * CANVAS_W + dx] = 1;
    }
}

/* Live format (docs/data_formats.md 1b, assetcodec.decode_sprite_frame): 16x16 sprites
 * at their OAM positions, tiles that could not be placed uniquely spilled below. */
static bool frame_build_live(Gallery* g, int assetIdx, FrameInfo* fi) {
  uint32_t s = kGalleryAssets[assetIdx].start, e = kGalleryAssets[assetIdx].end;
  if(e < s + 8u) return false;
  memset(fi, 0, sizeof(*fi));
  for(int i = 0; i < 8; i++) fi->hdr[i] = rd8(g, s + (uint32_t) i);
  fi->n1 = fi->hdr[0];
  fi->n2 = fi->hdr[1];
  fi->tileOff = fi->hdr[2];
  fi->unk1 = fi->hdr[3];
  fi->unk2 = fi->hdr[4];
  fi->ntiles1 = fi->hdr[5];
  fi->vramOff = fi->hdr[6];
  fi->flags = fi->hdr[7];
  fi->ntiles2 = fi->flags & 0x7F;
  fi->nrec = fi->n1 + fi->n2;

  uint32_t recEnd = s + 8u + (uint32_t) (2 * fi->nrec);
  if(recEnd > e) return false;
  fi->ntiles = fi->ntiles1 + fi->ntiles2;
  uint32_t tilesEnd = recEnd + (uint32_t) (32 * fi->ntiles);
  if(tilesEnd > e) {
    fi->ntiles = (int) ((e - recEnd) / 32u);
    fi->truncated = true;
  }

  static int16_t grid[GRID_MAX];
  for(int i = 0; i < GRID_MAX; i++) grid[i] = -1;
  for(int i = 0; i < fi->ntiles1 && i < GRID_MAX; i++) grid[i] = (int16_t) i;
  for(int i = 0; i < fi->ntiles2; i++) {
    int gi = fi->vramOff + i;
    if(gi >= 0 && gi < GRID_MAX) grid[gi] = (int16_t) (fi->ntiles1 + i);
  }

  int minx = 255, miny = 255, maxx = 0, maxy = 0;
  for(int i = 0; i < fi->nrec; i++) {
    int x = rd8(g, s + 8u + (uint32_t) (2 * i));
    int y = rd8(g, s + 9u + (uint32_t) (2 * i));
    if(x < minx) minx = x;
    if(y < miny) miny = y;
    if(x > maxx) maxx = x;
    if(y > maxy) maxy = y;
  }
  if(fi->nrec == 0) { minx = miny = maxx = maxy = 0; }
  fi->cw = fi->nrec ? maxx + 16 - minx : 0;
  fi->chh = fi->nrec ? maxy + 16 - miny : 0;
  if(fi->cw > CANVAS_W) fi->cw = CANVAS_W;
  if(fi->chh > CANVAS_H) fi->chh = CANVAS_H;

  canvas_reset(g);
  bool placed[512];
  memset(placed, 0, sizeof(placed));
  uint8_t px[64];
  int t = 0;
  for(int i = 0; i < fi->nrec; i++) {
    if(i == fi->n1) t = fi->tileOff;
    int x = rd8(g, s + 8u + (uint32_t) (2 * i)) - minx;
    int y = rd8(g, s + 9u + (uint32_t) (2 * i)) - miny;
    for(int dy = 0; dy < 2; dy++)
      for(int dx = 0; dx < 2; dx++) {
        int gi = t + dx + dy * TILE_SEQ_STEP;
        int bi = (gi >= 0 && gi < GRID_MAX) ? grid[gi] : -1;
        if(bi < 0 || bi >= fi->ntiles) continue;
        int cx = x + dx * 8, cy = y + dy * 8;
        if(!canvas_free(g, cx, cy)) continue;
        tile_pixels(g, recEnd + (uint32_t) (32 * bi), 4, px);
        canvas_put(g, cx, cy, px);
        if(bi < 512) placed[bi] = true;
      }
    t = next_tile(t);
  }

  fi->spill = 0;
  for(int i = 0; i < fi->ntiles && i < 512; i++) if(!placed[i]) fi->spill++;
  int gap = (fi->chh > 0 && fi->spill > 0) ? 1 : 0;
  int srows = (fi->spill + 15) / 16;
  fi->w = fi->cw;
  if(fi->spill > 0 && fi->w < 128) fi->w = 128;
  if(fi->w < 8) fi->w = 8;
  fi->h = fi->chh + gap + srows * 8;
  if(fi->h > CANVAS_H) fi->h = CANVAS_H;

  int k = 0;
  for(int i = 0; i < fi->ntiles && i < 512; i++) {
    if(placed[i]) continue;
    tile_pixels(g, recEnd + (uint32_t) (32 * i), 4, px);
    canvas_put(g, (k % 16) * 8, fi->chh + gap + (k / 16) * 8, px);
    k++;
  }
  return true;
}

/* Alternate format (docs/data_formats.md 1c): 8-byte header, then {x, y, attr} records
 * whose attr band identifies them, then 4bpp tiles: one tile per record, in order. */
static bool frame_build_alt(Gallery* g, int assetIdx, FrameInfo* fi) {
  uint32_t s = kGalleryAssets[assetIdx].start, e = kGalleryAssets[assetIdx].end;
  if(e < s + 8u) return false;
  memset(fi, 0, sizeof(*fi));
  for(int i = 0; i < 8; i++) fi->hdr[i] = rd8(g, s + (uint32_t) i);

  int n = 0;
  int palVotes[8];
  memset(palVotes, 0, sizeof(palVotes));
  while(n < 200) {
    uint32_t p = s + 8u + (uint32_t) (3 * n);
    if(p + 2u >= e) break;
    uint8_t attr = rd8(g, p + 2u);
    if(attr < 0x1C || attr > 0x22) break;
    palVotes[(attr >> 1) & 7]++;
    n++;
  }
  fi->nrec = n;
  /* The record's low OBJ attribute byte decodes as vhppp pN (docs/data_formats.md
   * 1c), so bits 3-1 are the palette the frame's own data asks for: the only
   * palette evidence there is for a format the live game never reads. */
  fi->altPal = 0;
  for(int i = 1; i < 8; i++) if(palVotes[i] > palVotes[fi->altPal]) fi->altPal = i;
  uint32_t recEnd = s + 8u + (uint32_t) (3 * n);
  fi->ntiles = recEnd < e ? (int) ((e - recEnd) / 32u) : 0;

  int minx = 255, miny = 255, maxx = 0, maxy = 0;
  for(int i = 0; i < n; i++) {
    int x = rd8(g, s + 8u + (uint32_t) (3 * i));
    int y = rd8(g, s + 9u + (uint32_t) (3 * i));
    if(x < minx) minx = x;
    if(y < miny) miny = y;
    if(x > maxx) maxx = x;
    if(y > maxy) maxy = y;
  }
  if(n == 0) { minx = miny = maxx = maxy = 0; }
  fi->cw = n ? maxx + 8 - minx : 0;
  fi->chh = n ? maxy + 8 - miny : 0;
  if(fi->cw > CANVAS_W) fi->cw = CANVAS_W;
  if(fi->chh > CANVAS_H) fi->chh = CANVAS_H;

  canvas_reset(g);
  bool placed[512];
  memset(placed, 0, sizeof(placed));
  uint8_t px[64];
  for(int i = 0; i < n && i < fi->ntiles; i++) {
    int x = rd8(g, s + 8u + (uint32_t) (3 * i)) - minx;
    int y = rd8(g, s + 9u + (uint32_t) (3 * i)) - miny;
    if(!canvas_free(g, x, y)) continue;
    tile_pixels(g, recEnd + (uint32_t) (32 * i), 4, px);
    canvas_put(g, x, y, px);
    if(i < 512) placed[i] = true;
  }

  fi->spill = 0;
  for(int i = 0; i < fi->ntiles && i < 512; i++) if(!placed[i]) fi->spill++;
  int gap = (fi->chh > 0 && fi->spill > 0) ? 1 : 0;
  int srows = (fi->spill + 15) / 16;
  fi->w = fi->cw;
  if(fi->spill > 0 && fi->w < 128) fi->w = 128;
  if(fi->w < 8) fi->w = 8;
  fi->h = fi->chh + gap + srows * 8;
  if(fi->h > CANVAS_H) fi->h = CANVAS_H;

  int k = 0;
  for(int i = 0; i < fi->ntiles && i < 512; i++) {
    if(placed[i]) continue;
    tile_pixels(g, recEnd + (uint32_t) (32 * i), 4, px);
    canvas_put(g, (k % 16) * 8, fi->chh + gap + (k / 16) * 8, px);
    k++;
  }
  return true;
}

/* Blit the assembled canvas, centred in the box, colour 0 showing the checkerboard. */
static void canvas_draw(const Gallery* g, uint32_t* fb, const FrameInfo* fi,
                        const uint32_t* rgb, int bx, int by, int bw, int bh) {
  int x0 = bx + (bw - fi->w) / 2;
  int y0 = by + (bh - fi->h) / 2;
  if(x0 < bx) x0 = bx;
  if(y0 < by) y0 = by;
  for(int y = 0; y < fi->h; y++) {
    int dy = y0 + y;
    if(dy < by || dy >= by + bh) continue;
    for(int x = 0; x < fi->w; x++) {
      int dx = x0 + x;
      if(dx < bx || dx >= bx + bw) continue;
      uint8_t v = g->canvas[y * CANVAS_W + x];
      fb_px(fb, dx, dy, v == 0 ? checker(dx, dy) : rgb[v]);
    }
  }
}

/* ---- tile grids and tilemaps ------------------------------------------------------ */

static int draw_tile_grid(const Gallery* g, uint32_t* fb, uint32_t start, uint32_t end,
                          int bpp, const uint32_t* rgb, int x0, int y0, int cols,
                          int maxRows, int firstTile) {
  int tsize = bpp * 8;
  int ntiles = (int) ((end - start) / (uint32_t) tsize);
  uint8_t px[64];
  int drawn = 0;
  for(int r = 0; r < maxRows; r++)
    for(int c = 0; c < cols; c++) {
      int idx = firstTile + r * cols + c;
      if(idx >= ntiles) return drawn;
      tile_pixels(g, start + (uint32_t) (idx * tsize), bpp, px);
      for(int y = 0; y < 8; y++)
        for(int x = 0; x < 8; x++) {
          int dx = x0 + c * 8 + x, dy = y0 + r * 8 + y;
          uint8_t v = px[y * 8 + x];
          fb_px(fb, dx, dy, v == 0 ? checker(dx, dy) : rgb[v]);
        }
      drawn++;
    }
  return drawn;
}

/* Draw a tilemap of 16-bit words {tile, palette, priority, flips}. The strips' tile
 * numbers are VRAM-relative and start at $44 (their tilesets never reached VRAM, so the
 * base is read off the maps themselves: see recomp/app/README.md). */
static void draw_tilemap(const Gallery* g, uint32_t* fb, uint32_t mapOff, int words,
                         uint32_t tilesOff, uint32_t tilesEnd, const uint32_t* rgb,
                         bool wordPal, int baseTile, int cols, int x0, int y0) {
  int ntiles = (int) ((tilesEnd - tilesOff) / 32u);
  uint8_t px[64];
  for(int i = 0; i < words; i++) {
    uint16_t w = rd16(g, mapOff + (uint32_t) (2 * i));
    int idx = (w & 0x3FF) - baseTile;
    int cx = x0 + (i % cols) * 8, cy = y0 + (i / cols) * 8;
    if(idx < 0 || idx >= ntiles) {
      for(int y = 0; y < 8; y++)
        for(int x = 0; x < 8; x++) fb_px(fb, cx + x, cy + y, checker(cx + x, cy + y));
      continue;
    }
    tile_pixels(g, tilesOff + (uint32_t) (idx * 32), 4, px);
    bool hflip = (w & 0x4000) != 0, vflip = (w & 0x8000) != 0;
    /* wordPal: `rgb` is the whole 128-colour BG half of CGRAM and the word's own
     * palette bits pick the row; otherwise `rgb` is already the one row to use. */
    const uint32_t* row = wordPal ? rgb + 16 * ((w >> 10) & 7) : rgb;
    for(int y = 0; y < 8; y++)
      for(int x = 0; x < 8; x++) {
        uint8_t v = px[(vflip ? 7 - y : y) * 8 + (hflip ? 7 - x : x)];
        fb_px(fb, cx + x, cy + y, v == 0 ? checker(cx + x, cy + y) : row[v]);
      }
  }
}

/* ---- chrome ---------------------------------------------------------------------- */

static void draw_frame_chrome(uint32_t* fb, const char* title, const char* right) {
  fb_clear(fb, C_BG);
  fb_fill(fb, 0, 0, GALLERY_FB_W, TITLE_H, C_TITLE);
  fb_text(fb, 3, 1, title, C_WHITE);
  if(right != NULL) {
    int w = romfont_text_w(right);
    fb_text(fb, GALLERY_FB_W - 3 - w, 1, right, C_WHITE);
  }
  fb_fill(fb, 0, TITLE_H, GALLERY_FB_W, 1, C_RULE);
  fb_fill(fb, 0, FOOT_Y - 2, GALLERY_FB_W, 1, C_RULE);
}

static void draw_foot(uint32_t* fb, const char* s) {
  fb_text(fb, 3, FOOT_Y, s, C_DIM);
}

static void draw_palette_strip(uint32_t* fb, const uint32_t* rgb, int count, int x, int y) {
  for(int i = 0; i < count; i++) fb_fill(fb, x + i * 5, y, 4, 5, rgb[i]);
}

/* ---- sections -------------------------------------------------------------------- */

/* The entity types a frame's {scene, palette} row belongs to, as "$02 $04". */
static void types_str(uint16_t mask, char* out, size_t cap) {
  size_t k = 0;
  out[0] = 0;
  for(int t = 0; t < 16 && k + 4 < cap; t++) {
    if(!((mask >> t) & 1u)) continue;
    k += (size_t) snprintf(out + k, cap - k, "%s$%02X", k ? " " : "", t * 2);
  }
  if(k == 0) snprintf(out, cap, "-");
}

/* ---- scenes ----------------------------------------------------------------
 *
 * The one page whose picture does not come out of the ROM by hand. scene.c boots
 * a machine of its own, walks its camera across the level with the game's own
 * code doing every metatile column, every DMA and every scroll register, and
 * reads the picture back out of the PPU that drew it; this only puts a window on
 * the result. See recomp/app/scene.h for what the composed image is and where it
 * can differ from a frame of the running game.
 * -------------------------------------------------------------------------- */

#define SCENE_VIEW_Y0 12
#define SCENE_VIEW_H  164

static const char* scene_view_name(int view) {
  static char buf[24];
  if(view == SCENE_VIEW_ALL) return "all layers";
  snprintf(buf, sizeof(buf), "BG%d only", view);
  return buf;
}

static void draw_scenes(Gallery* g, uint32_t* fb) {
  SceneState* st = &g->scene;
  char right[32];
  snprintf(right, sizeof(right), "%d/%d", st->mode + 1, SCENE_MODE_COUNT);
  draw_frame_chrome(fb, "SCENES", right);

  if(g->scenes == NULL) {
    fb_text(fb, 3, 20, "no scene machine", C_MARK);
    draw_foot(fb, "A back");
    return;
  }
  scenes_request(g->scenes, st->mode);

  int w = 0, h = 0;
  const uint32_t* img = scenes_image(g->scenes, st->view, &w, &h);
  if(img == NULL) img = scenes_image(g->scenes, SCENE_VIEW_ALL, &w, &h);

  if(img == NULL || scenes_image_mode(g->scenes) != st->mode) {
    /* Composing. The bar is the walk's own progress: the machine runs one frame
     * per 8 pixels of camera travel, because that is how often the game's
     * metatile blitter lays down a column. */
    fb_textf(fb, 3, 24, C_TEXT, "composing %s", scenes_mode_name(st->mode));
    char line[64];
    snprintf(line, sizeof(line), "%.34s", scenes_status(g->scenes));
    fb_text(fb, 3, 34, line, C_DIM);
    int pct = scenes_progress(g->scenes);
    fb_fill(fb, 3, 48, 250, 9, C_RULE);
    fb_fill(fb, 4, 49, 248 * pct / 1000, 7, C_SEL);
    fb_textf(fb, 3, 64, C_DIM, "%d.%d%%", pct / 10, pct % 10);
    fb_text(fb, 3, 80, "a second machine booted from reset,", C_DIM);
    fb_text(fb, 3, 88, "walked across the level by the game", C_DIM);
    draw_foot(fb, "A back");
    return;
  }

  /* Zoom 0 is "fit the level's height", which is what a scene page should open
   * on: the whole map top to bottom, scrolled along its length. */
  int zoom = st->zoom;
  if(zoom < 1) {
    zoom = 1;
    while(zoom < 16 && h / zoom > SCENE_VIEW_H) zoom *= 2;
  }
  int vw = w / zoom, vh = h / zoom;
  int maxSx = vw > GALLERY_FB_W ? vw - GALLERY_FB_W : 0;
  int maxSy = vh > SCENE_VIEW_H ? vh - SCENE_VIEW_H : 0;
  if(st->sx > maxSx) st->sx = maxSx;
  if(st->sy > maxSy) st->sy = maxSy;
  if(st->sx < 0) st->sx = 0;
  if(st->sy < 0) st->sy = 0;

  /* centre an image smaller than the window rather than pin it to a corner */
  int ox = vw < GALLERY_FB_W ? (GALLERY_FB_W - vw) / 2 : 0;
  int oy = vh < SCENE_VIEW_H ? (SCENE_VIEW_H - vh) / 2 : 0;
  for(int y = 0; y < SCENE_VIEW_H; y++) {
    int sy = (st->sy + y - oy) * zoom;
    for(int x = 0; x < GALLERY_FB_W; x++) {
      int sx = (st->sx + x - ox) * zoom;
      uint32_t c = (sx >= 0 && sy >= 0 && sx < w && sy < h)
                   ? img[(size_t) sy * w + sx] : C_BG;
      fb_px(fb, x, SCENE_VIEW_Y0 + y, c);
    }
  }

  SceneInfo info;
  bool haveInfo = scenes_info(g->scenes, &info);
  int y = SCENE_VIEW_Y0 + SCENE_VIEW_H + 4;
  fb_textf(fb, 3, y, C_TEXT, "%s %dx%d %s 1:%d",
           scenes_mode_name(st->mode), w, h, scene_view_name(st->view), zoom);
  y += LINE;
  if(haveInfo && info.cols > 0) {
    fb_textf(fb, 3, y, C_TEXT, "map %06X %dx%d meta %06X",
             info.mapAddr, info.cols, info.rows, info.metaAddr);
    y += LINE;
    fb_textf(fb, 3, y, C_DIM, "BGMODE %d%s TM %02X  level BG%d",
             info.bgmode, info.bg3prio ? "+p3" : "", info.tm, info.levelBg);
    y += LINE;
    fb_textf(fb, 3, y, C_DIM, "%d screens %d frames  HDMA off",
             info.screens, info.frames);
  } else {
    fb_text(fb, 3, y, "the title screen, no button pressed", C_DIM);
    y += LINE;
    fb_textf(fb, 3, y, C_DIM, "BGMODE %d TM %02X  8bpp BG1, no map",
             haveInfo ? info.bgmode : 0, haveInfo ? info.tm : 0);
  }
  draw_foot(fb, "dpad scroll  LR scene  B layer  Y zoom");
}

/* ---- the palettes a frame is actually drawn with ---------------------------
 *
 * Two sources, and the page says which is which. Observed: the scene machine
 * runs the harness's own input scripts and records, for every frame id an entity
 * is showing while its OAM attribute byte is in the OAM the PPU drew from, the
 * palette bits that byte carries. Derived: the walk over entity init records and
 * animation scripts in docs/data_formats.md 1d, which covers frames no script
 * reaches but can only say what the tables allow. Observed comes first.
 * -------------------------------------------------------------------------- */

#define GAL_SRC_OBSERVED 0
#define GAL_SRC_DERIVED  1
#define GAL_SRC_GUESS    2

typedef struct {
  uint8_t mode, pal, source;
  uint16_t types;      /* entity types the derivation attributes it to, if any */
  uint16_t flags;      /* the whole entity_flags word a forced render hands over */
} PalCand;

#define GAL_MAX_CAND (GAL_MAX_FRAME_PAL + 8)

/* The palette bits of an entity_flags word are bits 9-11; everything else in it
 * (the OBJ tile slot, the priority, the flips) is left as the evidence found it. */
static uint16_t flags_with_pal(uint16_t flags, int pal) {
  return (uint16_t) ((flags & ~0x0E00u) | ((unsigned) (pal & 7) << 9));
}

static int frame_pal_candidates(const Gallery* g, int idx, PalCand* out) {
  int n = 0;
  const FramePal* fp = g->framePal != NULL ? &g->framePal[idx] : NULL;
  uint8_t obs[SCENE_OBS_MODES];
  uint16_t fid = g->frameId != NULL ? g->frameId[idx] : 0;
  if(fid != 0 && g->scenes != NULL && scenes_obs_ready(g->scenes) &&
     scenes_obs_get(g->scenes, fid, obs)) {
    for(int m = 0; m < SCENE_OBS_MODES && n < GAL_MAX_CAND; m++)
      for(int p = 0; p < 8 && n < GAL_MAX_CAND; p++) {
        if(((obs[m] >> p) & 1u) == 0) continue;
        out[n].mode = (uint8_t) m;
        out[n].pal = (uint8_t) p;
        out[n].source = GAL_SRC_OBSERVED;
        out[n].types = 0;
        /* The flag word the entity carried when it was seen: that is the tile
         * slot and priority the game itself gave this frame. */
        out[n].flags = flags_with_pal(scenes_obs_flags(g->scenes, fid, m), p);
        if(fp != NULL)
          for(int i = 0; i < fp->n; i++)
            if(fp->mode[i] == m && fp->pal[i] == p) out[n].types = fp->types[i];
        n++;
      }
  }
  if(fp != NULL) {
    for(int i = 0; i < fp->n && n < GAL_MAX_CAND; i++) {
      bool dup = false;
      for(int k = 0; k < n; k++)
        if(out[k].mode == fp->mode[i] && out[k].pal == fp->pal[i]) dup = true;
      if(dup) continue;
      out[n].mode = fp->mode[i];
      out[n].pal = fp->pal[i];
      out[n].source = GAL_SRC_DERIVED;
      out[n].types = fp->types[i];
      out[n].flags = flags_with_pal(fp->flags[i], fp->pal[i]);
      n++;
    }
  }
  /* No entity in any of the four scenes plays this frame, so there is no
   * evidence to order. The game can still be made to draw it: game_mode 0, each
   * of the eight OBJ palettes, labelled as guesses. */
  if(n == 0) {
    for(int p = 0; p < 8; p++) {
      out[n].mode = 0;
      out[n].pal = (uint8_t) p;
      out[n].source = GAL_SRC_GUESS;
      out[n].types = 0;
      out[n].flags = flags_with_pal(g->modeFlags[0], p);
      n++;
    }
  }
  return n;
}

/* How the whole corpus comes out: observed, derived only, and neither. */
static void frame_pal_tally(const Gallery* g, int* observed, int* derived, int* unknown) {
  int o = 0, d = 0, u = 0;
  for(int i = 0; i < g->liveCount; i++) {
    int idx = g->live[i];
    uint8_t obs[SCENE_OBS_MODES];
    uint16_t fid = g->frameId != NULL ? g->frameId[idx] : 0;
    bool seen = fid != 0 && g->scenes != NULL && scenes_obs_ready(g->scenes) &&
                scenes_obs_get(g->scenes, fid, obs);
    if(seen) o++;
    else if(g->framePal != NULL && g->framePal[idx].n > 0) d++;
    else u++;
  }
  *observed = o;
  *derived = d;
  *unknown = u;
}

/* ---- the forced render, and the cache in front of it ----------------------
 *
 * A shot costs a boot the first time a scene is asked for and about fifteen
 * emulated frames after that, so the last few are kept: walking back along the
 * list does not re-run the machine for a frame it already drew. */
static const SpriteShot* shot_cached(Gallery* g, int mode, uint16_t fid, uint16_t flags) {
  if(fid == 0) return NULL;     /* what an empty cache slot holds */
  for(int i = 0; i < GAL_SHOT_CACHE; i++) {
    const SpriteShot* s = &g->shot[i];
    if(s->frameId == fid && s->mode == mode && s->flags == flags) return s;
  }
  return NULL;
}

static const SpriteShot* shot_keep(Gallery* g, const SpriteShot* src) {
  SpriteShot* dst = &g->shot[g->shotNext];
  sprite_shot_free(dst);
  if(!sprite_shot_copy(dst, src)) return NULL;
  g->shotNext = (g->shotNext + 1) % GAL_SHOT_CACHE;
  return dst;
}

/* The shot for this frame and palette, asking the scene machine for it when it
 * is not already in hand. NULL means "not drawn yet": the page says so. */
static const SpriteShot* shot_for(Gallery* g, int mode, uint16_t fid, uint16_t flags) {
  if(fid == 0 || g->scenes == NULL) return NULL;
  const SpriteShot* hit = shot_cached(g, mode, fid, flags);
  if(hit != NULL) return hit;
  scenes_sprite_request(g->scenes, mode, fid, flags);
  const SpriteShot* got = scenes_sprite_get(g->scenes, mode, fid, flags);
  return got != NULL ? shot_keep(g, got) : NULL;
}

/* Blit a shot centred in the box. A pixel the same colour as the backdrop is a
 * transparent one: the OBJ layer is the only layer on, so that is what the PPU
 * puts everywhere a sprite does not cover. */
static void shot_draw(uint32_t* fb, const SpriteShot* sh, int bx, int by, int bw, int bh) {
  int x0 = bx + (bw - sh->w) / 2, y0 = by + (bh - sh->h) / 2;
  if(x0 < bx) x0 = bx;
  if(y0 < by) y0 = by;
  for(int y = 0; y < sh->h; y++) {
    int dy = y0 + y;
    if(dy < by || dy >= by + bh) continue;
    for(int x = 0; x < sh->w; x++) {
      int dx = x0 + x;
      if(dx < bx || dx >= bx + bw) continue;
      uint32_t px = sh->px[y * sh->w + x];
      fb_px(fb, dx, dy, px == sh->backdrop ? checker(dx, dy) : px);
    }
  }
}

static const char* const kFlipName[4] = { "no flip", "h-flip", "v-flip", "hv-flip" };

/* The two flip bits of entity_flags, which is what picks between the four copies
 * of the emit loop the ROM carries. */
static uint16_t flip_bits(int flip) {
  return (uint16_t) (((flip & 1) ? SPRITE_FLIP_H : 0u) | ((flip & 2) ? SPRITE_FLIP_V : 0u));
}

static void draw_sprites(Gallery* g, uint32_t* fb, bool alt) {
  SecState* st = &g->st[alt ? GALLERY_SEC_SPRITES_ALT : GALLERY_SEC_SPRITES];
  const int* list = alt ? g->alt : g->live;
  int count = alt ? g->altCount : g->liveCount;
  char right[32];
  int idx0 = count ? list[st->item] : -1;
  snprintf(right, sizeof(right), "%s%d/%d",
           (idx0 >= 0 && (kGalleryAssets[idx0].flags & GA_UNUSED)) ? "unused  " : "",
           count ? st->item + 1 : 0, count);
  draw_frame_chrome(fb, alt ? "SPRITE FRAMES (ALT)" : "SPRITE FRAMES (LIVE)", right);
  if(count == 0) {
    fb_text(fb, 3, 20, "no frames in the manifest", C_TEXT);
    return;
  }
  int idx = list[st->item];

  /* The alternate format has no frame-table entry, so the game has no code path
   * that draws it: its page keeps the reconstruction. A live frame's default is
   * the game's own render, with the reconstruction on B as "file layout". */
  bool rendered = !alt && g->spriteView == 0;
  int near = alt && g->altNearest != NULL ? g->altNearest[idx] : -1;
  if(alt && g->spriteView == 0 && near >= 0) rendered = true;

  FrameInfo fi;
  memset(&fi, 0, sizeof(fi));
  bool ok = alt ? frame_build_alt(g, idx, &fi) : frame_build_live(g, idx, &fi);

  /* The palettes the game gives this frame: what the scene machine saw in OAM
   * first, then what the entity/animation walk derives, then the eight guesses
   * for a frame no entity plays. An alternate-format frame is played by nothing,
   * so the nearest evidence is the palette its own OAM records carry. */
  PalCand cand[GAL_MAX_CAND];
  int palIdx = alt && near >= 0 ? near : idx;
  int nauto = alt && near < 0 ? (ok && fi.nrec > 0 ? 1 : 0)
                              : frame_pal_candidates(g, palIdx, cand);
  /* In the rendered view the picker is not offered: the game cannot be made to
   * draw a frame through a palette block it never uploads. */
  int total = nauto + (rendered ? 0 : pal_total(kMainPals, MAIN_PAL_N));
  int sel = wrap(st->pal, total);

  uint32_t rgb[16];
  char palLine[64], whoLine[64];
  whoLine[0] = 0;
  int mode = 0, pal = 0;
  uint16_t flags = 0;
  int source = GAL_SRC_GUESS;
  if(sel < nauto && !(alt && near < 0)) {
    mode = cand[sel].mode;
    pal = cand[sel].pal;
    source = cand[sel].source;
    flags = (uint16_t) ((cand[sel].flags & ~(SPRITE_FLIP_H | SPRITE_FLIP_V))
                        | flip_bits(g->spriteFlip));
    pal_from_cgram(g, mode, 0x80 + 16 * pal, 16, rgb);
    static const char* const kSrcName[3] = { "OBSERVED", "DERIVED", "GUESS" };
    snprintf(palLine, sizeof(palLine), "%s pal %d = CGRAM $%02X, %s",
             kSrcName[source], pal, 0x80 + 16 * pal, kGalleryModeName[mode]);
    char t[32];
    types_str(cand[sel].types, t, sizeof(t));
    snprintf(whoLine, sizeof(whoLine), "%s types %s   %d/%d",
             source == GAL_SRC_OBSERVED ? "seen in OAM,"
                                        : (source == GAL_SRC_DERIVED ? "entity" : "no entity,"),
             t, sel + 1, nauto);
  } else if(sel < nauto) {
    pal = fi.altPal;
    pal_from_cgram(g, 0, 0x80 + 16 * pal, 16, rgb);
    snprintf(palLine, sizeof(palLine), "ATTR pal %d = CGRAM $%02X, %s",
             pal, 0x80 + 16 * pal, kGalleryModeName[0]);
    snprintf(whoLine, sizeof(whoLine), "no frame-table entry: its own attr");
    rendered = false;
  } else {
    pal_override(g, sel - nauto, rgb, palLine, sizeof(palLine));
    snprintf(whoLine, sizeof(whoLine), "%s",
             nauto ? "not the palette the game uses"
                   : "no entity in modes 0-3 plays this frame");
    rendered = false;
  }

  const SpriteShot* shot = NULL;
  if(rendered) {
    uint16_t fid = g->frameId != NULL ? g->frameId[palIdx] : 0;
    shot = shot_for(g, mode, fid, flags);
    if(shot != NULL && shot->w > 0) shot_draw(fb, shot, 4, 12, 248, 136);
    else if(ok) canvas_draw(g, fb, &fi, rgb, 4, 12, 248, 136);
  } else if(ok) {
    canvas_draw(g, fb, &fi, rgb, 4, 12, 248, 136);
  }

  draw_palette_strip(fb, rgb, 16, 3, 150);
  int y = 158;
  fb_textf(fb, 3, y, C_TEXT, "%.15s %06X %uB", asset_name(idx),
           kGalleryAssets[idx].start, asset_size(idx));
  y += LINE;
  if(!ok) {
    fb_text(fb, 3, y, "frame does not parse", C_MARK);
    return;
  }
  if(alt) {
    fb_textf(fb, 3, y, C_TEXT, "%d recs  %d tiles  %d spill  hdr %02X %02X %02X",
             fi.nrec, fi.ntiles, fi.spill, fi.hdr[0], fi.hdr[1], fi.hdr[2]);
    y += LINE;
    if(near >= 0)
      fb_textf(fb, 3, y, C_TEXT, "nearest live frame %u, %d/%d tiles",
               (unsigned) (g->frameId[near] / 4), g->altShared[idx], fi.ntiles);
    else if(g->altShared != NULL && g->altShared[idx] > 0)
      fb_textf(fb, 3, y, C_DIM, "%d tile(s) recur in a live frame: not a match",
               g->altShared[idx]);
    else
      fb_text(fb, 3, y, "no live frame shares a tile with it", C_DIM);
  } else if(rendered && shot != NULL && shot->w > 0) {
    fb_textf(fb, 3, y, C_TEXT, "%d OAM entries  %dx%d  flags %04X",
             shot->sprites, shot->w, shot->h, shot->flags);
    y += LINE;
    fb_textf(fb, 3, y, C_DIM, "the game's own render, %s%s", kFlipName[g->spriteFlip],
             shot->clipped ? ", clipped" : "");
  } else if(rendered) {
    fb_textf(fb, 3, y, C_TEXT, "n1=%d n2=%d to=%02X t1=%d vo=%02X fl=%02X",
             fi.n1, fi.n2, fi.tileOff, fi.ntiles1, fi.vramOff, fi.flags);
    y += LINE;
    const char* s = g->scenes == NULL ? "no scene machine"
                                      : (shot != NULL ? shot->why : scenes_status(g->scenes));
    fb_textf(fb, 3, y, C_MARK, "%.37s", s);
  } else {
    fb_textf(fb, 3, y, C_TEXT, "n1=%d n2=%d to=%02X t1=%d vo=%02X fl=%02X",
             fi.n1, fi.n2, fi.tileOff, fi.ntiles1, fi.vramOff, fi.flags);
    y += LINE;
    fb_textf(fb, 3, y, C_DIM, "file layout: %d spr  %d tiles  %d spill",
             fi.nrec, fi.ntiles, fi.spill);
  }
  y += LINE;
  fb_text(fb, 3, y, palLine, sel < nauto ? C_TEXT : C_MARK);
  y += LINE;
  fb_text(fb, 3, y, whoLine, C_DIM);
  if(!alt) {
    y += LINE;
    if(g->scenes == NULL) {
      fb_text(fb, 3, y, "no scene machine: derived palettes only", C_DIM);
    } else if(!scenes_obs_ready(g->scenes)) {
      char line[64];
      snprintf(line, sizeof(line), "%.34s", scenes_status(g->scenes));
      fb_text(fb, 3, y, line, C_MARK);
    } else {
      int o = 0, d = 0, u = 0;
      frame_pal_tally(g, &o, &d, &u);
      fb_textf(fb, 3, y, C_DIM, "obs %d  derived %d  unknown %d", o, d, u);
    }
  }
  draw_foot(fb, alt ? "dpad frame+palette  LR x25  A back"
                    : "dpad frame+palette  B layout  Y flip  LR x25  A back");
}

/* The Backgrounds page has two views. VRAM is the default: the whole 32 KB as
 * the scene's init leaves it, drawn as 4bpp tiles at their VRAM addresses with
 * the palette row the scene's own maps give each tile, so a tile referenced
 * across two uploaded sets lines up with itself. The raw-set view is the old
 * one: one manifest asset, in file order, through one palette row. */
static void draw_bg_vram(Gallery* g, uint32_t* fb) {
  SecState* st = &g->st[GALLERY_SEC_BACKGROUNDS];
  int mode = wrap(st->item, GAL_MODE_COUNT);
  bool title = mode == GAL_MODE_TITLE;
  int bpp = title ? 8 : 4;
  int wordsPerTile = bpp * 4;      /* a 4bpp tile is 32 bytes = 16 VRAM words */
  int ntiles = 0x8000 / wordsPerTile;

  char right[32];
  snprintf(right, sizeof(right), "VRAM  %d/%d", mode + 1, GAL_MODE_COUNT);
  draw_frame_chrome(fb, "BACKGROUNDS", right);

  uint32_t rgb[256];
  pal_from_cgram(g, mode, 0, 256, rgb);

  /* the picker, when the user asks for it, replaces the BG half row by row */
  int sel = wrap(st->pal, 1 + pal_total(kMainPals, MAIN_PAL_N));
  char palLine[64];
  palLine[0] = 0;
  uint32_t over[16];
  if(sel > 0) {
    pal_override(g, sel - 1, over, palLine, sizeof(palLine));
    for(int r = 0; r < 8; r++)
      for(int i = 0; i < 16; i++) rgb[r * 16 + i] = over[i];
  }

  const int cols = 32, rows = 16;
  int perPage = cols * rows;
  int pages = (ntiles + perPage - 1) / perPage;
  if(st->page >= pages) st->page = 0;
  if(st->page < 0) st->page = pages - 1;
  /* Open on the page the scene uploaded most of: page 0 of mode 0's VRAM is
   * empty (its first upload is at $1600), and the page worth seeing first is
   * the one the level's own tileset landed on. Only until the user pages. */
  if(!g->bgPaged) {
    int bestPg = 0, bestN = -1;
    for(int pg = 0; pg < pages; pg++) {
      int n = 0;
      for(int t = pg * perPage; t < (pg + 1) * perPage && t < ntiles; t++)
        if(g->vramSet[mode][(unsigned) (t * wordsPerTile)]) n++;
      if(n > bestN) { bestN = n; bestPg = pg; }
    }
    st->page = bestPg;
  }

  uint8_t px[64];
  int first = st->page * perPage;
  int rowsUsed = 0, refd = 0;
  for(int r = 0; r < rows; r++)
    for(int c = 0; c < cols; c++) {
      int t = first + r * cols + c;
      if(t >= ntiles) continue;
      unsigned word = (unsigned) (t * wordsPerTile);
      int cx = c * 8, cy = 12 + r * 8;
      bool set = g->vramSet[mode][word] || g->vramSet[mode][word + 1];
      if(!set) {
        for(int y = 0; y < 8; y++)
          for(int x = 0; x < 8; x++) fb_px(fb, cx + x, cy + y, checker(cx + x, cy + y));
        continue;
      }
      vram_tile_pixels(g->vram[mode], word, bpp, px);
      const uint32_t* pal = rgb;
      if(!title) {
        int vr = t < GAL_VRAM_TILES ? g->vramRow[mode][t] : 0;
        if(vr > 0) refd++;
        pal = rgb + 16 * (vr > 0 ? vr - 1 : 0);
      }
      for(int y = 0; y < 8; y++)
        for(int x = 0; x < 8; x++) {
          uint8_t v = px[y * 8 + x];
          fb_px(fb, cx + x, cy + y, v == 0 ? checker(cx + x, cy + y) : pal[v]);
        }
      rowsUsed++;
    }

  draw_palette_strip(fb, rgb, 32, 3, 144);
  int y = 152;
  fb_textf(fb, 3, y, C_TEXT, "%s VRAM $%04X-$%04X %dbpp", kGalleryModeName[mode],
           first * wordsPerTile, (first + perPage) * wordsPerTile - 1, bpp);
  y += LINE;
  fb_textf(fb, 3, y, C_TEXT, "page %d/%d  %d uploaded  %d in a map",
           st->page + 1, pages, rowsUsed, refd);
  y += LINE;
  /* what the init put on this page, and where it came from */
  int shown = 0;
  for(unsigned u = 0; u < kGalleryVramUploadCount && shown < 2; u++) {
    const GalleryVramUpload* up = &kGalleryVramUploads[u];
    if(up->mode != mode) continue;
    unsigned t0 = up->vram / (unsigned) wordsPerTile;
    unsigned t1 = (up->vram + up->words) / (unsigned) wordsPerTile;
    if((int) t1 <= first || (int) t0 >= first + perPage) continue;
    if(up->src == GAL_VRAM_FILL)
      fb_textf(fb, 3, y, C_DIM, "$%04X fill $%04X (%s)", up->vram, up->fill, up->site);
    else
      fb_textf(fb, 3, y, C_DIM, "$%04X <- %06X %uw (%s)", up->vram, up->src,
               up->words, up->site);
    y += LINE;
    shown++;
  }
  if(sel > 0) fb_text(fb, 3, y, palLine, C_MARK);
  else if(title)
    fb_text(fb, 3, y, "8bpp: the pixel byte is the CGRAM index", C_DIM);
  else
    fb_text(fb, 3, y, "row per tile, voted from its maps", C_DIM);
  draw_foot(fb, "dpad scene/pal  LR page  B view  A back");
}

static void draw_bg_raw(Gallery* g, uint32_t* fb) {
  SecState* st = &g->st[GALLERY_SEC_BACKGROUNDS];
  char right[32];
  int idx0 = g->bgCount ? g->bg[wrap(st->item, g->bgCount)] : -1;
  snprintf(right, sizeof(right), "%sset %d/%d",
           (idx0 >= 0 && (kGalleryAssets[idx0].flags & GA_UNUSED)) ? "unused " : "",
           g->bgCount ? wrap(st->item, g->bgCount) + 1 : 0, g->bgCount);
  draw_frame_chrome(fb, "BACKGROUNDS", right);
  if(g->bgCount == 0) {
    fb_text(fb, 3, 20, "no tilesets in the manifest", C_TEXT);
    return;
  }
  int idx = g->bg[wrap(st->item, g->bgCount)];
  uint32_t start = kGalleryAssets[idx].start;
  int bpp = kGalleryAssets[idx].kind == GK_TILESET_2BPP ? 2
          : kGalleryAssets[idx].kind == GK_TILESET_8BPP ? 8 : 4;
  int ntiles = (int) (asset_size(idx) / (uint32_t) (bpp * 8));

  const GalleryBgTileset* bt = bg_tileset_of(start);
  const GalleryObjTileset* ot = obj_tileset_of(start);

  uint32_t rgb[256];
  char palLine[64], srcLine[64];
  srcLine[0] = 0;
  int nauto = (bt != NULL || ot != NULL) ? 1 : 0;
  int total = nauto + pal_total(kMainPals, MAIN_PAL_N);
  int sel = wrap(st->pal, total);
  int row = -1, votes = 0, refs = 0;
  const char* animNote = NULL;

  if(sel < nauto && bt != NULL && bpp == 8) {
    pal_from_cgram(g, bt->mode, 0, 256, rgb);
    snprintf(palLine, sizeof(palLine), "%s CGRAM $00-$FF, 8bpp direct",
             kGalleryModeName[bt->mode]);
    snprintf(srcLine, sizeof(srcLine), "BG%d, VRAM $%04X; pixel value = CGRAM index",
             bt->bg, bt->vram);
    animNote = pal_animated_note(bt->mode, 0, 256);
  } else if(sel < nauto && bt != NULL) {
    row = bg_default_row(g, bt, ntiles, &votes, &refs);
    if(row < 0) row = 0;
    pal_from_cgram(g, bt->mode, 16 * row, 16, rgb);
    snprintf(palLine, sizeof(palLine), "%s BG%d, CGRAM row %d ($%02X)",
             kGalleryModeName[bt->mode], bt->bg, row, 16 * row);
    snprintf(srcLine, sizeof(srcLine), "%d of %d words in %s%s use row %d",
             votes, refs, bt->nmaps ? bt->maps[0].name : "-",
             bt->nmaps > 1 ? " +" : "", row);
    animNote = pal_animated_note(bt->mode, 16 * row, 16);
  } else if(sel < nauto && ot != NULL) {
    pal_from_cgram(g, ot->mode, 0x80 + 16 * ot->pal, 16, rgb);
    snprintf(palLine, sizeof(palLine), "%s OBJ pal %d = CGRAM $%02X",
             kGalleryModeName[ot->mode], ot->pal, 0x80 + 16 * ot->pal);
    snprintf(srcLine, sizeof(srcLine), "sprite tiles, VRAM $1600; not a BG set");
    animNote = pal_animated_note(ot->mode, 0x80 + 16 * ot->pal, 16);
  } else {
    pal_override(g, sel - nauto, rgb, palLine, sizeof(palLine));
    snprintf(srcLine, sizeof(srcLine), "%s",
             nauto ? "not the palette the game uses"
                   : "no scene uploads it: no CGRAM");
  }

  int cols = 32, rows = 17;
  int perPage = cols * rows;
  int pages = (ntiles + perPage - 1) / perPage;
  if(pages < 1) pages = 1;
  if(st->page >= pages) st->page = 0;
  draw_tile_grid(g, fb, start, kGalleryAssets[idx].end, bpp, rgb,
                 0, 12, cols, rows, st->page * perPage);
  draw_palette_strip(fb, rgb, bpp == 8 ? 32 : 16, 3, 152);

  int y = 160;
  fb_textf(fb, 3, y, C_TEXT, "%.22s %06X %u bytes", asset_name(idx), start, asset_size(idx));
  y += LINE;
  fb_textf(fb, 3, y, C_TEXT, "%s  %d tiles  page %d/%d",
           kGalleryKindName[kGalleryAssets[idx].kind], ntiles, st->page + 1, pages);
  y += LINE;
  fb_text(fb, 3, y, palLine, sel < nauto ? C_TEXT : C_MARK);
  y += LINE;
  fb_text(fb, 3, y, srcLine, C_DIM);
  if(animNote != NULL) {
    y += LINE;
    char note[48];
    snprintf(note, sizeof(note), "%.34s", animNote);
    fb_text(fb, 3, y, note, C_MARK);
  }
  draw_foot(fb, "dpad set/pal  LR page  B view  A back");
}

/* ---- a scene's metatiles, each drawn once ----------------------------------
 *
 * A tileset in file order is not what anyone drew: the artists composed 32x32
 * metatiles, and the level map is a grid of indices into those. This view is the
 * metatile table itself, one metatile per cell in index order, each of its
 * sixteen tilemap words resolved through the VRAM the scene's init leaves and
 * drawn with the palette row, priority and flips that word carries. It is the
 * same table the blitter reads: 32 bytes a metatile, four rows of four words,
 * which build_metatile_column_580 walks a column at a time ($00/$08/$10/$18).
 * -------------------------------------------------------------------------- */
#define META_PX 32
#define META_WORDS 16

static const GalleryBgTileset* meta_set_of(int mode) {
  for(unsigned i = 0; i < kGalleryBgTilesetCount; i++) {
    const GalleryBgTileset* bt = &kGalleryBgTilesets[i];
    if(bt->mode != mode) continue;
    for(int m = 0; m < bt->nmaps; m++)
      if(bt->maps[m].name != NULL && strncmp(bt->maps[m].name, "metatiles", 9) == 0)
        return bt;
  }
  return NULL;
}

static uint32_t meta_map_start(const GalleryBgTileset* bt, uint32_t* end) {
  for(int m = 0; m < bt->nmaps; m++)
    if(bt->maps[m].name != NULL && strncmp(bt->maps[m].name, "metatiles", 9) == 0) {
      if(end != NULL) *end = bt->maps[m].end;
      return bt->maps[m].start;
    }
  return 0;
}

/* One 32x32 metatile into the framebuffer, or into `out` when `out` is given
 * (which is what the pixel check against a composed scene uses). */
static void meta_draw(const Gallery* g, uint32_t* fb, uint32_t* out, uint8_t* opaque,
                      int mode, const GalleryBgTileset* bt, int index, int px0, int py0) {
  uint32_t rgb[256];
  pal_from_cgram(g, mode, 0, 256, rgb);
  uint32_t base = meta_map_start(bt, NULL) + (uint32_t) (index * 32);
  uint8_t px[64];
  for(int r = 0; r < 4; r++)
    for(int c = 0; c < 4; c++) {
      uint16_t w = rd16(g, base + (uint32_t) (r * 8 + c * 2));
      unsigned word = (unsigned) (bt->charBase + (unsigned) (w & 0x3FFu) * 16u);
      vram_tile_pixels(g->vram[mode], word, 4, px);
      const uint32_t* pal = rgb + 16 * ((w >> 10) & 7);
      bool hf = (w & 0x4000u) != 0, vf = (w & 0x8000u) != 0;
      for(int y = 0; y < 8; y++)
        for(int x = 0; x < 8; x++) {
          uint8_t v = px[(vf ? 7 - y : y) * 8 + (hf ? 7 - x : x)];
          int dx = px0 + c * 8 + x, dy = py0 + r * 8 + y;
          uint32_t col = v == 0 ? checker(dx, dy) : pal[v];
          if(out != NULL) {
            size_t o = (size_t) (r * 8 + y) * META_PX + (c * 8 + x);
            out[o] = v == 0 ? rgb[0] : pal[v];
            if(opaque != NULL) opaque[o] = (uint8_t) (v != 0);
          } else {
            fb_px(fb, dx, dy, col);
          }
        }
    }
}

static void draw_bg_meta(Gallery* g, uint32_t* fb) {
  SecState* st = &g->st[GALLERY_SEC_BACKGROUNDS];
  int mode = wrap(st->item, 4);
  const GalleryBgTileset* bt = meta_set_of(mode);
  char right[32];
  snprintf(right, sizeof(right), "METATILES  %d/4", mode + 1);
  draw_frame_chrome(fb, "BACKGROUNDS", right);
  if(bt == NULL) {
    fb_text(fb, 3, 20, "no metatile table for this scene", C_TEXT);
    draw_foot(fb, "dpad scene  LR page  B view  A back");
    return;
  }
  uint32_t end = 0, start = meta_map_start(bt, &end);
  int count = (int) ((end - start) / 32u);
  const int cols = 8, rows = 4;
  int perPage = cols * rows;
  int pages = (count + perPage - 1) / perPage;
  if(pages < 1) pages = 1;
  if(st->page >= pages) st->page = 0;
  if(st->page < 0) st->page = pages - 1;

  int first = st->page * perPage;
  for(int r = 0; r < rows; r++)
    for(int c = 0; c < cols; c++) {
      int i = first + r * cols + c;
      if(i >= count) continue;
      meta_draw(g, fb, NULL, NULL, mode, bt, i, c * META_PX, 12 + r * META_PX);
    }

  uint32_t rgb[256];
  pal_from_cgram(g, mode, 0, 256, rgb);
  draw_palette_strip(fb, rgb, 32, 3, 144);
  int y = 152;
  fb_textf(fb, 3, y, C_TEXT, "%s  %d metatiles of 32x32  %06X",
           kGalleryModeName[mode], count, start);
  y += LINE;
  fb_textf(fb, 3, y, C_TEXT, "page %d/%d  metatiles %d-%d  BG%d chars $%04X",
           st->page + 1, pages, first,
           first + perPage - 1 < count ? first + perPage - 1 : count - 1,
           bt->bg, bt->charBase);
  y += LINE;
  fb_text(fb, 3, y, "4x4 tilemap words, each with its own row", C_DIM);
  y += LINE;
  fb_text(fb, 3, y, "the level map is a grid of these", C_DIM);
  draw_foot(fb, "dpad scene  LR page  B view  A back");
}

static void draw_backgrounds(Gallery* g, uint32_t* fb) {
  if(g->bgView == GAL_BG_RAW) draw_bg_raw(g, fb);
  else if(g->bgView == GAL_BG_META) draw_bg_meta(g, fb);
  else draw_bg_vram(g, fb);
}

/* ---- the unreferenced font and the bank $C1 picture strips ------------------
 *
 * Nothing in the ROM uploads any of this, so there is no CGRAM to take a palette
 * from and no VRAM base to read a tile number against. Both have to come out of
 * the bytes themselves.
 *
 * The base. A strip's map pads with one tile index over and over (all three pad
 * with $044) and its content runs upward from just above it. If the set holds an
 * all-zero tile, that is the tile the pad means, so base = pad - (that tile's
 * index); if it holds none, the pad is below the set and base = the
 * lowest content index, which puts the first content tile at tile 0. That gives
 * $44, $50 and $43 for the three strips (not the one base for all three that
 * the earlier reading assumed), and with them no word in any of the three maps
 * lands outside its own set, so nothing spills into the next.
 *
 * Every all-zero tile in a set starts another block of the same shape: strip 1
 * has four (312 tiles for a 70-tile caption) and strip 3 has three. Re-basing
 * the same map onto the second block of strip 1 reads as the next caption, so
 * the sets hold a series; the later blocks want maps of their own, which are not
 * in the ROM, and come out mis-tiled. L/R walks them and the page says so.
 *
 * The palette. The maps' words all ask for palette 7, and nothing uploads a
 * palette 7 for them, so the page ranks the ROM's own 16-colour rows by how well
 * they suit the art (colour 0 dark and the other fifteen a monotone ramp,
 * which is what a metallic caption needs) and offers the best as a *guess*.
 * Row 7 of the main palette block at 046C48 wins it: it is the row the words
 * name, and it is a clean fifteen-step gold ramp. The others are on up/down.
 * -------------------------------------------------------------------------- */

/* 16-colour rows worth offering, best first. Everything here is an address in
 * the ROM; the colours come from the user's own image. */
static const struct { uint32_t off; const char* name; } kGuessPals[] = {
  { 0x046D28u, "main 046C48 row 7 (words ask 7)" },
  { 0x06A36Bu, "title 06A36B row 0 (black/white)" },
  { 0x007E88u, "prev build 007AC8 row 30" },
  { 0x046EE8u, "mode 1 BG 046EA8 row 2" },
  { 0x0470C3u, "mode 2 BG 046FE3 row 7" },
  { 0x0473C3u, "mode 3 BG 047343 row 4" },
  { 0x046C48u, "main 046C48 row 0" },
};
#define GUESS_PAL_N ((int) (sizeof(kGuessPals) / sizeof(kGuessPals[0])))

/* The guess list first, then the ordinary override picker. */
static void guess_pal(const Gallery* g, int idx, int best, uint32_t* rgb,
                      char* line, size_t cap) {
  int total = GUESS_PAL_N + pal_total(kMainPals, MAIN_PAL_N);
  idx = wrap(idx + best, total);
  if(idx < GUESS_PAL_N) {
    pal_from_rom(g, kGuessPals[idx].off, 16, rgb);
    snprintf(line, cap, "%s %.28s", idx == best ? "BEST:" : "guess:",
             kGuessPals[idx].name);
  } else {
    pal_override(g, idx - GUESS_PAL_N, rgb, line, cap);
  }
}

static bool tile_blank(const Gallery* g, uint32_t tilesOff, int i) {
  for(int k = 0; k < 32; k++) if(rd8(g, tilesOff + (uint32_t) (i * 32 + k)) != 0) return false;
  return true;
}

/* The index a map pads with: whichever it uses most. */
static int strip_pad(const Gallery* g, uint32_t mapOff, int words) {
  int bestIdx = 0, bestN = 0;
  for(int i = 0; i < words; i++) {
    int v = rd16(g, mapOff + (uint32_t) (2 * i)) & 0x3FF;
    int n = 0;
    for(int k = 0; k < words; k++)
      if((rd16(g, mapOff + (uint32_t) (2 * k)) & 0x3FF) == v) n++;
    if(n > bestN) { bestN = n; bestIdx = v; }
  }
  return bestIdx;
}

/* Every all-zero tile in the set, each of which starts a block. */
static int strip_blocks(const Gallery* g, uint32_t tilesOff, uint32_t tilesEnd,
                        int* out, int cap) {
  int n = (int) ((tilesEnd - tilesOff) / 32u), k = 0;
  for(int i = 0; i < n && k < cap; i++) if(tile_blank(g, tilesOff, i)) out[k++] = i;
  return k;
}

static void draw_fonts(Gallery* g, uint32_t* fb) {
  SecState* st = &g->st[GALLERY_SEC_FONTS];
  int item = st->item % 4;
  char right[32];
  snprintf(right, sizeof(right), "unused  %d/4", item + 1);
  draw_frame_chrome(fb, "FONTS AND PICTURE STRIPS", right);

  uint32_t rgb[16];
  char palLine[64];

  if(item == 0) {
    /* the ROM's own font: 96 2bpp glyphs, plane 1 empty, ASCII $20-$7F. 2bpp
     * takes four colours from a row, and only values 0 and 1 ever occur, so what
     * matters is entry 0 and entry 1: the title palette's row 0 is black then
     * white, which is what a font wants. */
    guess_pal(g, st->pal, 1, rgb, palLine, sizeof(palLine));
    int x0 = 64, y0 = 16;
    uint8_t px[64];
    for(int i = 0; i < ROM_FONT_GLYPHS; i++) {
      tile_pixels(g, ROM_FONT_OFF + (uint32_t) (i * 16), 2, px);
      int cx = x0 + (i % 16) * 8, cy = y0 + (i / 16) * 8;
      for(int y = 0; y < 8; y++)
        for(int x = 0; x < 8; x++) {
          uint8_t v = px[y * 8 + x];
          fb_px(fb, cx + x, cy + y, v == 0 ? checker(cx + x, cy + y) : rgb[v]);
        }
    }
    fb_text(fb, 3, y0 + 56, "rendered as text:", C_DIM);
    fb_text(fb, 3, y0 + 68, "THE QUICK BROWN FOX", C_WHITE);
    fb_text(fb, 3, y0 + 80, "jumps over 0123456789!", C_WHITE);
    int idx = asset_at(ROM_FONT_OFF);
    int y = 166;
    if(idx >= 0)
      fb_textf(fb, 3, y, C_TEXT, "%s %06X %u bytes", asset_name(idx),
               kGalleryAssets[idx].start, asset_size(idx));
    y += LINE;
    fb_text(fb, 3, y, "96 glyphs $20-$7F; the only font", C_TEXT);
    y += LINE;
    fb_text(fb, 3, y, palLine, C_MARK);
    y += LINE;
    fb_text(fb, 3, y, "2bpp: only values 0 and 1 occur", C_DIM);
    draw_palette_strip(fb, rgb, 4, 3, 158);
    draw_foot(fb, "dpad item+palette guess  A back");
    return;
  }

  const int sIdx = item - 1;
  int mapIdx = asset_at(kStrips[sIdx].map), tilesIdx = asset_at(kStrips[sIdx].tiles);
  if(mapIdx < 0 || tilesIdx < 0) {
    fb_text(fb, 3, 20, "strip missing from the manifest", C_MARK);
    return;
  }
  uint32_t mapOff = kGalleryAssets[mapIdx].start;
  uint32_t tOff = kGalleryAssets[tilesIdx].start, tEnd = kGalleryAssets[tilesIdx].end;
  int words = (int) (asset_size(mapIdx) / 2u);
  int ntiles = (int) ((tEnd - tOff) / 32u);

  int blocks[16];
  int nblocks = strip_blocks(g, tOff, tEnd, blocks, 16);
  int block = nblocks > 0 ? wrap(st->page, nblocks) : 0;
  int pad = strip_pad(g, mapOff, words);
  int base;
  if(nblocks > 0) {
    base = pad - blocks[block];
  } else {
    base = 0x400;
    for(int i = 0; i < words; i++) {
      int v = rd16(g, mapOff + (uint32_t) (2 * i)) & 0x3FF;
      if(v != pad && v != 0 && v < base) base = v;
    }
    if(base > 0x3FF) base = pad;
  }

  /* how many words this base leaves outside the set: with the right base, none */
  int outside = 0;
  for(int i = 0; i < words; i++) {
    int v = (rd16(g, mapOff + (uint32_t) (2 * i)) & 0x3FF) - base;
    if(v == pad - base || v == 0 - base) continue;      /* the pads are meant to miss */
    if(v < 0 || v >= ntiles) outside++;
  }

  guess_pal(g, st->pal, 0, rgb, palLine, sizeof(palLine));
  draw_tilemap(g, fb, mapOff, words, tOff, tEnd, rgb, false, base, 32, 0, 12);
  int mapH = ((words + 31) / 32) * 8;
  fb_text(fb, 3, 12 + mapH + 4, "tileset from the block base:", C_DIM);
  int rows = (152 - (12 + mapH + 14)) / 8;
  if(rows > 0)
    draw_tile_grid(g, fb, tOff + (uint32_t) (nblocks > 0 ? blocks[block] * 32 : 0), tEnd,
                   4, rgb, 0, 12 + mapH + 14, 32, rows, 0);
  draw_palette_strip(fb, rgb, 16, 3, 154);

  int y = 162;
  fb_textf(fb, 3, y, C_TEXT, "strip %d map %06X %dw tiles %06X", sIdx + 1,
           mapOff, words, tOff);
  y += LINE;
  fb_textf(fb, 3, y, C_TEXT, "base $%02X pad $%02X  %d tiles  %d out",
           base & 0xFF, pad, ntiles, outside);
  y += LINE;
  fb_text(fb, 3, y, palLine, C_MARK);
  y += LINE;
  if(nblocks > 1)
    fb_textf(fb, 3, y, C_DIM, "block %d/%d at tile %d (map fits blk 1)",
             block + 1, nblocks, blocks[block]);
  else
    fb_text(fb, 3, y, "one block: the set has no blank tile", C_DIM);
  draw_foot(fb, "dpad item+palette  LR block  A back");
}

static void draw_prev_build(Gallery* g, uint32_t* fb) {
  SecState* st = &g->st[GALLERY_SEC_PREV_BUILD];
  int view = st->item % 3;
  static const char* const kViews[3] = { "TILES", "PALETTE", "ANIMATION TABLE" };
  char right[40];
  snprintf(right, sizeof(right), "unused  %s %d/3", kViews[view], view + 1);
  draw_frame_chrome(fb, "PREVIOUS BUILD", right);

  /* The previous build's own palette block, and only that: this code is not in the
   * live program, so no scene's CGRAM has anything to do with it. */
  const char* palName;
  int palRow;
  uint32_t palOff = pal_pick(kPrevPals, 1, st->pal, &palName, &palRow);
  uint32_t rgb[16];
  pal_from_rom(g, palOff, 16, rgb);

  int idx;
  int y = 166;
  if(view == 0) {
    idx = asset_at(PREV_TILES_OFF);
    if(idx < 0) return;
    int ntiles = (int) (asset_size(idx) / 32u);
    int cols = 32, rows = 18, perPage = cols * rows;
    int pages = (ntiles + perPage - 1) / perPage;
    if(pages < 1) pages = 1;
    if(st->page >= pages) st->page = 0;
    draw_tile_grid(g, fb, kGalleryAssets[idx].start, kGalleryAssets[idx].end, 4, rgb,
                   0, 12, cols, rows, st->page * perPage);
    fb_textf(fb, 3, y, C_TEXT, "%s %06X %u bytes", asset_name(idx),
             kGalleryAssets[idx].start, asset_size(idx));
    y += LINE;
    fb_textf(fb, 3, y, C_TEXT, "%d tiles  page %d/%d", ntiles, st->page + 1, pages);
    y += LINE;
    fb_textf(fb, 3, y, C_DIM, "picker: %s row %d @ %06X", palName, palRow, palOff);
    draw_palette_strip(fb, rgb, 16, 3, 158);
  } else if(view == 1) {
    idx = asset_at(PREV_PAL_OFF);
    if(idx < 0) return;
    int colours = (int) (asset_size(idx) / 2u);
    int rows = (colours + 15) / 16;
    for(int r = 0; r < rows && r < 18; r++) {
      fb_textf(fb, 2, 14 + r * 8, C_DIM, "%02X", r * 16);
      for(int c = 0; c < 16 && r * 16 + c < colours; c++)
        fb_fill(fb, 18 + c * 12, 13 + r * 8, 11, 7,
                bgr15(rd16(g, kGalleryAssets[idx].start + (uint32_t) (2 * (r * 16 + c)))));
    }
    fb_textf(fb, 3, y, C_TEXT, "%s %06X", asset_name(idx), kGalleryAssets[idx].start);
    y += LINE;
    fb_textf(fb, 3, y, C_TEXT, "%d colours, %d rows of 16", colours, rows);
    y += LINE;
    fb_text(fb, 3, y, "15-bit BGR, bit 15 clear throughout", C_DIM);
  } else {
    idx = asset_at(PREV_ANIM_OFF);
    if(idx < 0) return;
    int records = (int) (asset_size(idx) / 8u);
    int lines = 18;
    int pages = (records + lines - 1) / lines;
    if(pages < 1) pages = 1;
    if(st->page >= pages) st->page = 0;
    for(int i = 0; i < lines; i++) {
      int rec = st->page * lines + i;
      if(rec >= records) break;
      uint32_t o = kGalleryAssets[idx].start + (uint32_t) (rec * 8);
      fb_textf(fb, 3, 13 + i * 8, i & 1 ? C_TEXT : C_DIM,
               "%04X %06X %02X%02X %02X%02X %02X%02X %02X%02X", rec, o,
               rd8(g, o), rd8(g, o + 1), rd8(g, o + 2), rd8(g, o + 3),
               rd8(g, o + 4), rd8(g, o + 5), rd8(g, o + 6), rd8(g, o + 7));
    }
    fb_textf(fb, 3, y, C_TEXT, "%s %06X  %d records", asset_name(idx),
             kGalleryAssets[idx].start, records);
    y += LINE;
    fb_textf(fb, 3, y, C_TEXT, "8 bytes each  page %d/%d", st->page + 1, pages);
    y += LINE;
    fb_text(fb, 3, y, "older copy of the anim table", C_DIM);
  }
  draw_foot(fb, "dpad view+palette  LR page  A back");
}

/* ---- music and sound effects ------------------------------------------------------ */

/* The page's item list: every song slot, then bank 1's sound effects, then bank 2's.
 * The two counts are the banks' own count bytes ($2410 / $2E94 once uploaded), read
 * out of the blocks in the ROM rather than assumed. */
static void music_counts(const Gallery* g, int* songs, int* n1, int* n2) {
  *songs = g->songCount;
  *n1 = rd8(g, SFX_BANK1_OFF + 4u);
  *n2 = rd8(g, SFX_BANK2_OFF + 4u);
}

static int music_item_count(const Gallery* g) {
  int songs, n1, n2;
  music_counts(g, &songs, &n1, &n2);
  return songs + n1 + n2;
}

/* The command word the game would send for this sfx id, or 0 if nothing ever sends it. */
static uint16_t sfx_trigger_word(int id) {
  for(unsigned i = 0; i < sizeof(kSfxTriggered) / sizeof(kSfxTriggered[0]); i++)
    if((kSfxTriggered[i] & 0xFF) == (unsigned) id) return kSfxTriggered[i];
  return 0;
}

static void draw_music(Gallery* g, uint32_t* fb) {
  SecState* st = &g->st[GALLERY_SEC_MUSIC];
  int songs, n1, n2;
  music_counts(g, &songs, &n1, &n2);
  int total = songs + n1 + n2;
  char right[32];
  snprintf(right, sizeof(right), "%d/%d", total ? st->item + 1 : 0, total);
  draw_frame_chrome(fb, "MUSIC AND SOUND EFFECTS", right);
  if(total == 0) {
    fb_text(fb, 3, 20, "no songs or sfx banks in the manifest", C_TEXT);
    return;
  }
  const int rows = 18;
  int top = st->item - rows / 2;
  if(top > total - rows) top = total - rows;
  if(top < 0) top = 0;
  for(int i = 0; i < rows && top + i < total; i++) {
    int n = top + i, y = 13 + i * 8;
    bool sel = n == st->item;
    if(sel) fb_fill(fb, 0, y - 1, GALLERY_FB_W, 8, C_SEL);
    if(n < songs) {
      int idx = g->song[n];
      uint32_t words = rd16(g, kGalleryAssets[idx].start + 2u);
      bool empty = words == 0;
      uint32_t col = sel ? C_WHITE : (empty ? C_DIM : C_TEXT);
      fb_textf(fb, 3, y, col, "song %d %06X %5uw %s", n,
               kGalleryAssets[idx].start, words,
               empty ? "empty slot" : "-> $1300");
    } else if(n < songs + n1) {
      int id = n - songs;
      uint16_t word = sfx_trigger_word(id);
      uint32_t col = sel ? C_WHITE : (word ? C_TEXT : C_DIM);
      fb_textf(fb, 3, y, col, "sfx $%02X  bank 1  %s", id,
               word ? "triggered" : "never triggered");
    } else {
      int id = SFX_BANK2_ID + (n - songs - n1);
      fb_textf(fb, 3, y, sel ? C_WHITE : C_DIM, "sfx $%02X  bank 2  never", id);
    }
  }

  int y = 166;
  if(st->item < songs) {
    int idx = g->song[st->item];
    fb_textf(fb, 3, y, C_TEXT, "%.15s %06X %uB", asset_name(idx),
             kGalleryAssets[idx].start, asset_size(idx));
    y += LINE;
    fb_text(fb, 3, y, rd16(g, kGalleryAssets[idx].start + 2u) != 0
                      ? "B: spc_command, A = song number"
                      : "empty slot: nothing to upload", C_DIM);
  } else if(st->item < songs + n1) {
    int id = st->item - songs;
    uint16_t word = sfx_trigger_word(id);
    fb_textf(fb, 3, y, C_TEXT, "bank 1 entry %d/%d  ptr SPC $%04X", id, n1,
             0x2412 + 2 * id);
    y += LINE;
    if(word) fb_textf(fb, 3, y, C_DIM, "the game sends $%04X (chan %d)", word, word >> 8);
    else fb_text(fb, 3, y, "nothing ever asks for it", C_MARK);
  } else {
    int id = SFX_BANK2_ID + (st->item - songs - n1);
    fb_textf(fb, 3, y, C_TEXT, "bank 2 entry %d/%d  ptr SPC $%04X",
             id - SFX_BANK2_ID, n2, 0x2E96 + 2 * (id - SFX_BANK2_ID));
    y += LINE;
    fb_text(fb, 3, y, "nothing ever asks for it", C_MARK);
  }
  y += LINE;
  fb_text(fb, 3, y, "cmd $FB (fade+song) is never sent", C_DIM);
  draw_foot(fb, "dpad entry  LR x10  B play  A back");
}

static void draw_samples(Gallery* g, uint32_t* fb) {
  SecState* st = &g->st[GALLERY_SEC_SAMPLES];
  char right[32];
  snprintf(right, sizeof(right), "%d/%d", g->brrCount ? st->item + 1 : 0, g->brrCount);
  draw_frame_chrome(fb, "SAMPLES (BRR)", right);
  if(g->brrCount == 0) {
    fb_text(fb, 3, 20, "no samples in the manifest", C_TEXT);
    return;
  }
  const int rows = 13;
  int top = st->item - rows / 2;
  if(top > g->brrCount - rows) top = g->brrCount - rows;
  if(top < 0) top = 0;
  for(int i = 0; i < rows && top + i < g->brrCount; i++) {
    int n = top + i;
    int idx = g->brr[n];
    int y = 13 + i * 8;
    bool sel = n == st->item;
    if(sel) fb_fill(fb, 0, y - 1, GALLERY_FB_W, 8, C_SEL);
    uint32_t loop = rd16(g, kGalleryAssets[idx].start);
    uint32_t len = rd16(g, kGalleryAssets[idx].start + 2u);
    fb_textf(fb, 3, y, sel ? C_WHITE : C_TEXT, "%02d %06X %6uB len %5u %s",
             n, kGalleryAssets[idx].start, asset_size(idx), len,
             (n < 256 && g->brrUsed[n]) ? "" : "unused");
    (void) loop;
  }

  /* waveform of the selected sample, decoded on the way in */
  int wy = 13 + rows * 8 + 4, wh = 40;
  fb_fill(fb, 0, wy, GALLERY_FB_W, wh, RGB(0x08, 0x0C, 0x14));
  fb_fill(fb, 0, wy + wh / 2, GALLERY_FB_W, 1, C_RULE);
  if(g->pcmCount > 0) {
    for(int x = 0; x < GALLERY_FB_W; x++) {
      int a = (int) ((int64_t) x * g->pcmCount / GALLERY_FB_W);
      int b = (int) ((int64_t) (x + 1) * g->pcmCount / GALLERY_FB_W);
      if(b <= a) b = a + 1;
      int lo = 32767, hi = -32768;
      for(int i = a; i < b && i < g->pcmCount; i++) {
        if(g->pcm[i] < lo) lo = g->pcm[i];
        if(g->pcm[i] > hi) hi = g->pcm[i];
      }
      int y0 = wy + wh / 2 - hi * (wh / 2) / 32768;
      int y1 = wy + wh / 2 - lo * (wh / 2) / 32768;
      for(int y = y0; y <= y1; y++) fb_px(fb, x, y, C_WAVE);
    }
  }

  int idx = g->brr[st->item];
  int y = 166;
  fb_textf(fb, 3, y, C_TEXT, "%s %06X-%06X loop %u", asset_name(idx),
           kGalleryAssets[idx].start, kGalleryAssets[idx].end,
           rd16(g, kGalleryAssets[idx].start));
  y += LINE;
  fb_textf(fb, 3, y, C_TEXT, "%d blk  %d smp  %.2fs at 32kHz",
           g->pcmCount / 16, g->pcmCount, (double) g->pcmCount / 32000.0);
  y += LINE;
  if(st->item < 256 && !g->brrUsed[st->item])
    fb_text(fb, 3, y, "no song's sample list names it", C_MARK);
  else
    fb_text(fb, 3, y, "a song's sample list names it", C_DIM);
  draw_foot(fb, "dpad sample  LR x10  B play  A back");
}

static void draw_stale(Gallery* g, uint32_t* fb) {
  SecState* st = &g->st[GALLERY_SEC_STALE];
  char right[32];
  snprintf(right, sizeof(right), "%d/%d", g->staleCount ? st->item + 1 : 0, g->staleCount);
  draw_frame_chrome(fb, "STALE DUPLICATES", right);
  if(g->staleCount == 0) {
    fb_text(fb, 3, 20, "no stale regions in the manifest", C_TEXT);
    return;
  }
  const int rows = 9;
  int top = st->item - rows / 2;
  if(top > g->staleCount - rows) top = g->staleCount - rows;
  if(top < 0) top = 0;
  for(int i = 0; i < rows && top + i < g->staleCount; i++) {
    int n = top + i;
    int idx = g->stale[n];
    int y = 13 + i * 16;
    bool sel = n == st->item;
    if(sel) fb_fill(fb, 0, y - 1, GALLERY_FB_W, 16, C_SEL);
    fb_textf(fb, 3, y, sel ? C_WHITE : C_TEXT, "%06X-%06X %.16s",
             kGalleryAssets[idx].start, kGalleryAssets[idx].end, asset_name(idx));
    /* the manifest note says what live region the copy shadows */
    const char* note = kGalleryNotes[kGalleryAssets[idx].note];
    const char* of = strstr(note, "duplicate of ");
    char buf[64];
    if(of != NULL) snprintf(buf, sizeof(buf), "shadows %.22s", of + 13);
    else snprintf(buf, sizeof(buf), "%.29s", note);
    fb_text(fb, 11, y + 8, buf, sel ? C_WHITE : C_DIM);
  }
  int y = 166;
  int idx = g->stale[st->item];
  fb_textf(fb, 3, y, C_TEXT, "%s", asset_name(idx));
  y += LINE;
  const char* note = kGalleryNotes[kGalleryAssets[idx].note];
  char line[64];
  snprintf(line, sizeof(line), "%.33s", note);
  fb_text(fb, 3, y, line, C_DIM);
  y += LINE;
  if(strlen(note) > 33) {
    snprintf(line, sizeof(line), "%.33s", note + 33);
    fb_text(fb, 3, y, line, C_DIM);
  }
  draw_foot(fb, "dpad region  LR x5  A back");
}

/* ---- input ------------------------------------------------------------------------ */

#define BIT(b) ((uint16_t) (1u << (b)))

/* The palette cursor is allowed to go negative: every page wraps it itself, over
 * "the palettes the game gives this item, then the override picker". */
static void clamp_state(Gallery* g) {
  for(int s = 0; s < GALLERY_SECTION_COUNT; s++) {
    if(g->st[s].page < 0) g->st[s].page = 0;
    if(g->st[s].item < 0) g->st[s].item = 0;
  }
}

static int wrap(int v, int n) {
  if(n <= 0) return 0;
  return ((v % n) + n) % n;
}

/* Move the current section's item cursor by `d`, and keep the derived state sane. */
static void move_item(Gallery* g, int d) {
  if(g->section < 0 || g->section >= GALLERY_SECTION_COUNT) return;
  SecState* st = &g->st[g->section];
  int n = 1;
  switch(g->section) {
    case GALLERY_SEC_SPRITES:     n = g->liveCount; break;
    case GALLERY_SEC_SPRITES_ALT: n = g->altCount; break;
    case GALLERY_SEC_BACKGROUNDS:
      n = g->bgView == GAL_BG_RAW ? g->bgCount
        : (g->bgView == GAL_BG_META ? 4 : GAL_MODE_COUNT);
      break;
    case GALLERY_SEC_FONTS:       n = 4; break;
    case GALLERY_SEC_PREV_BUILD:  n = 3; break;
    case GALLERY_SEC_MUSIC:       n = music_item_count(g); break;
    case GALLERY_SEC_SAMPLES:     n = g->brrCount; break;
    case GALLERY_SEC_STALE:       n = g->staleCount; break;
    default: break;
  }
  st->item = wrap(st->item + d, n);
  if(g->section == GALLERY_SEC_BACKGROUNDS || g->section == GALLERY_SEC_FONTS ||
     g->section == GALLERY_SEC_PREV_BUILD)
    st->page = 0;
  /* A new item gets the palette the game gives it, not the previous item's
   * override: the override is a deliberate act, not a mode. */
  if(g->section == GALLERY_SEC_SPRITES || g->section == GALLERY_SEC_SPRITES_ALT ||
     g->section == GALLERY_SEC_BACKGROUNDS)
    st->pal = 0;
  if(g->section == GALLERY_SEC_SAMPLES && g->brrCount > 0) brr_decode(g, g->brr[st->item]);
}

/* The scenes page has its own controls: the d-pad walks the window over an image
 * that is bigger than the screen instead of walking a list. */
static void apply_scenes(Gallery* g, uint16_t press) {
  SceneState* st = &g->scene;
  if(press & BIT(GALLERY_BTN_LEFT)) st->sx -= 16;
  if(press & BIT(GALLERY_BTN_RIGHT)) st->sx += 16;
  if(press & BIT(GALLERY_BTN_UP)) st->sy -= 16;
  if(press & BIT(GALLERY_BTN_DOWN)) st->sy += 16;
  if(press & (BIT(GALLERY_BTN_L) | BIT(GALLERY_BTN_R))) {
    st->mode = wrap(st->mode + ((press & BIT(GALLERY_BTN_R)) ? 1 : -1), SCENE_MODE_COUNT);
    st->sx = 0;
    st->sy = 0;
    st->zoom = 0;                 /* back to "fit the height" for the new scene */
    st->view = SCENE_VIEW_ALL;
  }
  if(press & BIT(GALLERY_BTN_B)) {
    /* the next view the composition actually holds: all layers, then each BG the
     * mode's TM enables, on its own */
    for(int i = 0; i < SCENE_VIEW_COUNT; i++) {
      int v = wrap(st->view + 1 + i, SCENE_VIEW_COUNT);
      if(v == SCENE_VIEW_ALL || scenes_view_available(g->scenes, v)) { st->view = v; break; }
    }
  }
  if(press & BIT(GALLERY_BTN_Y)) {
    st->zoom = st->zoom >= 16 ? 0 : (st->zoom < 1 ? 1 : st->zoom * 2);
    st->sx = 0;
    st->sy = 0;
  }
  if(st->sx < 0) st->sx = 0;
  if(st->sy < 0) st->sy = 0;
}

static void apply(Gallery* g, uint16_t press) {
  if(g->section < 0 || g->section >= GALLERY_SECTION_COUNT) return;
  SecState* st = &g->st[g->section];
  bool list = g->section == GALLERY_SEC_SAMPLES || g->section == GALLERY_SEC_STALE ||
              g->section == GALLERY_SEC_MUSIC;

  if(press & BIT(GALLERY_BTN_A)) { gallery_close(g); return; }

  if(g->section == GALLERY_SEC_SCENES) { apply_scenes(g, press); return; }

  if(list) {
    int step = g->section == GALLERY_SEC_STALE ? 5 : 10;
    if(press & BIT(GALLERY_BTN_UP)) move_item(g, -1);
    if(press & BIT(GALLERY_BTN_DOWN)) move_item(g, +1);
    if(press & BIT(GALLERY_BTN_LEFT)) move_item(g, -1);
    if(press & BIT(GALLERY_BTN_RIGHT)) move_item(g, +1);
    if(press & BIT(GALLERY_BTN_L)) move_item(g, -step);
    if(press & BIT(GALLERY_BTN_R)) move_item(g, +step);
    if((press & BIT(GALLERY_BTN_B)) && g->section == GALLERY_SEC_SAMPLES && g->brrCount > 0) {
      if(g->pcmAsset != g->brr[st->item]) brr_decode(g, g->brr[st->item]);
      g->pcmPending = g->pcmCount > 0;
    }
    if((press & BIT(GALLERY_BTN_B)) && g->section == GALLERY_SEC_MUSIC) {
      int songs, n1, n2;
      music_counts(g, &songs, &n1, &n2);
      if(st->item < songs) {
        /* an empty slot has nothing to upload and the ROM's own path never returns
         * from one (see recomp/app/music.c), so it is listed but not playable */
        if(rd16(g, kGalleryAssets[g->song[st->item]].start + 2u) != 0) {
          g->reqKind = GALLERY_REQ_SONG;
          g->reqArg = st->item;
        }
      } else {
        int id = st->item < songs + n1 ? st->item - songs
                                       : SFX_BANK2_ID + (st->item - songs - n1);
        uint16_t word = sfx_trigger_word(id);
        /* the channel the game uses for this sound, else channel 7: sfx_start takes
         * the channel from the command's high byte and nothing else depends on it */
        g->reqKind = GALLERY_REQ_SFX;
        g->reqArg = word ? word : (int) (0x0700 | (unsigned) id);
      }
    }
    return;
  }

  if(g->section == GALLERY_SEC_BACKGROUNDS) {
    if(press & BIT(GALLERY_BTN_B)) {
      g->bgView = (g->bgView + 1) % GAL_BG_VIEWS;
      g->bgPaged = false;
      st->item = 0;
      st->page = 0;
      st->pal = 0;
      return;
    }
    if(press & (BIT(GALLERY_BTN_L) | BIT(GALLERY_BTN_R))) g->bgPaged = true;
    if(press & (BIT(GALLERY_BTN_LEFT) | BIT(GALLERY_BTN_RIGHT))) g->bgPaged = false;
  }
  if(g->section == GALLERY_SEC_SPRITES) {
    /* B switches between the game's own render and the file layout; Y walks the
     * four flip combinations, which are four copies of the emit loop in the ROM
     * and so four pictures the game itself can draw. */
    if(press & BIT(GALLERY_BTN_B)) { g->spriteView = g->spriteView ? 0 : 1; st->pal = 0; }
    if(press & BIT(GALLERY_BTN_Y)) g->spriteFlip = (g->spriteFlip + 1) & 3;
  }
  int step = (g->section == GALLERY_SEC_SPRITES || g->section == GALLERY_SEC_SPRITES_ALT)
             ? 25 : 1;
  if(press & BIT(GALLERY_BTN_LEFT)) move_item(g, -1);
  if(press & BIT(GALLERY_BTN_RIGHT)) move_item(g, +1);
  if(press & BIT(GALLERY_BTN_UP)) st->pal--;
  if(press & BIT(GALLERY_BTN_DOWN)) st->pal++;
  if(g->section == GALLERY_SEC_SPRITES || g->section == GALLERY_SEC_SPRITES_ALT) {
    if(press & BIT(GALLERY_BTN_L)) move_item(g, -step);
    if(press & BIT(GALLERY_BTN_R)) move_item(g, +step);
  } else {
    if(press & BIT(GALLERY_BTN_L)) st->page = st->page > 0 ? st->page - 1 : 0;
    if(press & BIT(GALLERY_BTN_R)) st->page++;
  }
  clamp_state(g);
}

void gallery_press(Gallery* g, uint16_t bits) {
  if(g->open) apply(g, bits);
}

bool gallery_input(Gallery* g, uint16_t held) {
  if(!g->open) { g->prevHeld = held; return false; }
  uint16_t edge = (uint16_t) (held & ~g->prevHeld);
  uint16_t press = edge;
  const uint16_t repeatable = BIT(GALLERY_BTN_UP) | BIT(GALLERY_BTN_DOWN) |
                              BIT(GALLERY_BTN_LEFT) | BIT(GALLERY_BTN_RIGHT) |
                              BIT(GALLERY_BTN_L) | BIT(GALLERY_BTN_R);
  for(int b = 0; b < 12; b++) {
    uint16_t m = BIT(b);
    if(!(repeatable & m)) continue;
    if(held & m) {
      if(edge & m) g->rep[b] = 24;
      else if(--g->rep[b] <= 0) { press |= m; g->rep[b] = 4; }
    } else {
      g->rep[b] = 0;
    }
  }
  g->prevHeld = held;
  if(press != 0) apply(g, press);
  return g->open;
}

/* ---- public -------------------------------------------------------------------- */

static const char* const kSectionNames[GALLERY_SECTION_COUNT] = {
  "Scenes",
  "Sprite frames (live)",
  "Sprite frames (alternate)",
  "Backgrounds",
  "Fonts and picture strips",
  "Previous build",
  "Music and sound effects",
  "Samples (BRR)",
  "Stale duplicates",
};

/* Short names for the hidden --gallery flag. */
static const char* const kSectionKeys[GALLERY_SECTION_COUNT] = {
  "scenes", "sprites", "alt", "backgrounds", "fonts", "prev", "music", "samples", "stale",
};

const char* gallery_section_name(int section) {
  if(section < 0 || section >= GALLERY_SECTION_COUNT) return "?";
  return kSectionNames[section];
}

int gallery_section_by_name(const char* name) {
  for(int i = 0; i < GALLERY_SECTION_COUNT; i++)
    if(strcmp(name, kSectionKeys[i]) == 0) return i;
  char* end = NULL;
  long v = strtol(name, &end, 10);
  if(end != NULL && *end == 0 && v >= 0 && v < GALLERY_SECTION_COUNT) return (int) v;
  return -1;
}

void gallery_set_scenes(Gallery* g, struct Scenes* sc) {
  if(g != NULL) g->scenes = (Scenes*) sc;
}

int gallery_metatile_count(const Gallery* g, int mode) {
  const GalleryBgTileset* bt = g != NULL ? meta_set_of(mode) : NULL;
  if(bt == NULL) return 0;
  uint32_t end = 0, start = meta_map_start(bt, &end);
  return (int) ((end - start) / 32u);
}

bool gallery_metatile(const Gallery* g, int mode, int index, unsigned flip, uint32_t* out,
                      uint8_t* opaque) {
  const GalleryBgTileset* bt = g != NULL ? meta_set_of(mode) : NULL;
  if(bt == NULL || out == NULL || index < 0 || index >= gallery_metatile_count(g, mode))
    return false;
  meta_draw(g, NULL, out, opaque, mode, bt, index, 0, 0);
  /* The level map word flips the whole metatile, which the blitter does by
   * reading its four words backwards and complementing their own flip bits. */
  if(flip & 0x4000u)
    for(int y = 0; y < META_PX; y++)
      for(int x = 0; x < META_PX / 2; x++) {
        int a = y * META_PX + x, b = y * META_PX + (META_PX - 1 - x);
        uint32_t t = out[a];
        out[a] = out[b];
        out[b] = t;
        if(opaque != NULL) { uint8_t u = opaque[a]; opaque[a] = opaque[b]; opaque[b] = u; }
      }
  if(flip & 0x8000u)
    for(int y = 0; y < META_PX / 2; y++)
      for(int x = 0; x < META_PX; x++) {
        int a = y * META_PX + x, b = (META_PX - 1 - y) * META_PX + x;
        uint32_t t = out[a];
        out[a] = out[b];
        out[b] = t;
        if(opaque != NULL) { uint8_t u = opaque[a]; opaque[a] = opaque[b]; opaque[b] = u; }
      }
  return true;
}


/* Which of the eight palette rows a metatile's sixteen tilemap words name. */
unsigned gallery_metatile_rows(const Gallery* g, int mode, int index) {
  const GalleryBgTileset* bt = g != NULL ? meta_set_of(mode) : NULL;
  if(bt == NULL || index < 0 || index >= gallery_metatile_count(g, mode)) return 0;
  uint32_t base = meta_map_start(bt, NULL) + (uint32_t) (index * 32);
  unsigned rows = 0;
  for(int i = 0; i < META_WORDS; i++) rows |= 1u << ((rd16(g, base + (uint32_t) (2 * i)) >> 10) & 7);
  return rows;
}

uint16_t gallery_cgram_entry(const Gallery* g, int mode, int entry) {
  if(g == NULL || mode < 0 || mode >= GAL_MODE_COUNT || entry < 0 || entry >= 256) return 0;
  return g->cgram[mode][entry];
}

int gallery_live_count(const Gallery* g) { return g != NULL ? g->liveCount : 0; }

bool gallery_frame_plan(const Gallery* g, int item, GalleryFramePlan* out) {
  if(g == NULL || out == NULL || item < 0 || item >= g->liveCount) return false;
  int idx = g->live[item];
  PalCand cand[GAL_MAX_CAND];
  int n = frame_pal_candidates(g, idx, cand);
  out->frameId = g->frameId != NULL ? g->frameId[idx] : 0;
  out->mode = n > 0 ? cand[0].mode : 0;
  out->flags = n > 0 ? cand[0].flags : flags_with_pal(g->modeFlags[0], 0);
  out->source = n > 0 ? cand[0].source : GAL_SRC_GUESS;
  return true;
}

void gallery_alt_counts(const Gallery* g, int* total, int* withNearest, int* sharedTiles) {
  if(total != NULL) *total = g != NULL ? g->altCount : 0;
  if(withNearest != NULL) *withNearest = alt_nearest_count(g);
  if(sharedTiles != NULL) {
    int n = 0;                 /* the most any one alternate frame shares */
    if(g != NULL && g->altShared != NULL)
      for(int i = 0; i < g->altCount; i++)
        if(g->altShared[g->alt[i]] > n) n = g->altShared[g->alt[i]];
    *sharedTiles = n;
  }
}

void gallery_frame_pal_counts(const Gallery* g, int* observed, int* derived,
                              int* unknown, int* firstObserved) {
  int o = 0, d = 0, u = 0;
  if(g != NULL) frame_pal_tally(g, &o, &d, &u);
  if(observed != NULL) *observed = o;
  if(derived != NULL) *derived = d;
  if(unknown != NULL) *unknown = u;
  if(firstObserved != NULL) {
    *firstObserved = 0;
    if(g != NULL)
      for(int i = 0; i < g->liveCount; i++) {
        uint8_t obs[SCENE_OBS_MODES];
        uint16_t fid = g->frameId != NULL ? g->frameId[g->live[i]] : 0;
        if(fid != 0 && g->scenes != NULL && scenes_obs_ready(g->scenes) &&
           scenes_obs_get(g->scenes, fid, obs)) { *firstObserved = i + 1; break; }
      }
  }
}

bool gallery_wants_scenes(const Gallery* g) {
  if(g == NULL || !g->open) return false;
  return g->section == GALLERY_SEC_SCENES || g->section == GALLERY_SEC_SPRITES;
}

Gallery* gallery_create(const uint8_t* rom, size_t romLen) {
  Gallery* g = calloc(1, sizeof(Gallery));
  if(g == NULL) return NULL;
  g->scene.view = SCENE_VIEW_ALL;
  g->scene.zoom = 0;      /* 0 = fit the level height */
  g->rom = rom;
  g->romLen = (uint32_t) romLen;
  g->pcmAsset = -1;
  g->canvas = calloc((size_t) CANVAS_W * CANVAS_H, 1);
  g->claimed = calloc((size_t) CANVAS_W * CANVAS_H, 1);
  g->live = collect(1u << GK_SPRITE_FRAME, &g->liveCount, false);
  g->alt = collect(1u << GK_SPRITE_FRAME_ALT, &g->altCount, false);
  g->bg = collect((1u << GK_TILESET_4BPP) | (1u << GK_TILESET_2BPP) | (1u << GK_TILESET_8BPP),
                  &g->bgCount, true);
  g->brr = collect(1u << GK_BRR, &g->brrCount, false);
  g->stale = collect(1u << GK_STALE, &g->staleCount, false);
  g->song = collect(1u << GK_SONG, &g->songCount, false);
  g->framePal = calloc((size_t) kGalleryAssetCount, sizeof(FramePal));
  if(g->canvas == NULL || g->claimed == NULL || g->framePal == NULL) {
    gallery_destroy(g);
    return NULL;
  }
  g->frameId = calloc((size_t) kGalleryAssetCount, sizeof(uint16_t));
  if(g->frameId == NULL) { gallery_destroy(g); return NULL; }
  brr_scan_usage(g);
  build_mode_cgram(g);
  build_mode_vram(g);
  build_vram_rows(g);
  build_frame_palettes(g);
  build_frame_ids(g);
  build_alt_nearest(g);
  return g;
}

void gallery_destroy(Gallery* g) {
  if(g != NULL) {
    for(int i = 0; i < GAL_SHOT_CACHE; i++) sprite_shot_free(&g->shot[i]);
    free(g->altNearest);
    free(g->altShared);
    g->altNearest = NULL;
    g->altShared = NULL;
  }
  if(g == NULL) return;
  free(g->live);
  free(g->alt);
  free(g->bg);
  free(g->brr);
  free(g->stale);
  free(g->song);
  free(g->pcm);
  free(g->canvas);
  free(g->claimed);
  free(g->framePal);
  free(g->frameId);
  free(g);
}

bool gallery_is_open(const Gallery* g) { return g != NULL && g->open; }
int gallery_section(const Gallery* g) { return g != NULL ? g->section : -1; }

void gallery_open(Gallery* g, int section) {
  if(g == NULL || section < 0 || section >= GALLERY_SECTION_COUNT) return;
  g->open = true;
  g->section = section;
  g->prevHeld = 0xFFFF;      /* ignore whatever was held when the page opened */
  memset(g->rep, 0, sizeof(g->rep));
  if(section == GALLERY_SEC_SAMPLES && g->brrCount > 0 &&
     g->pcmAsset != g->brr[g->st[section].item])
    brr_decode(g, g->brr[g->st[section].item]);
}

void gallery_close(Gallery* g) {
  if(g == NULL) return;
  g->open = false;
  g->pcmPending = false;
  g->reqKind = GALLERY_REQ_NONE;
}

bool gallery_take_request(Gallery* g, int* kind, int* arg) {
  if(g == NULL || g->reqKind == GALLERY_REQ_NONE) return false;
  *kind = g->reqKind;
  *arg = g->reqArg;
  g->reqKind = GALLERY_REQ_NONE;
  return true;
}

bool gallery_take_pcm(Gallery* g, const int16_t** pcm, int* count) {
  if(g == NULL || !g->pcmPending || g->pcmCount <= 0) return false;
  g->pcmPending = false;
  *pcm = g->pcm;
  *count = g->pcmCount;
  return true;
}

void gallery_render(Gallery* g, uint32_t* fb) {
  if(g == NULL) return;
  switch(g->section) {
    case GALLERY_SEC_SCENES:      draw_scenes(g, fb); break;
    case GALLERY_SEC_SPRITES:     draw_sprites(g, fb, false); break;
    case GALLERY_SEC_SPRITES_ALT: draw_sprites(g, fb, true); break;
    case GALLERY_SEC_BACKGROUNDS: draw_backgrounds(g, fb); break;
    case GALLERY_SEC_FONTS:       draw_fonts(g, fb); break;
    case GALLERY_SEC_PREV_BUILD:  draw_prev_build(g, fb); break;
    case GALLERY_SEC_MUSIC:       draw_music(g, fb); break;
    case GALLERY_SEC_SAMPLES:     draw_samples(g, fb); break;
    case GALLERY_SEC_STALE:       draw_stale(g, fb); break;
    default:                      fb_clear(fb, C_BG); break;
  }
}
