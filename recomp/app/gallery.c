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
  int altAttr;        /* the OAM attribute byte its records carry, -1 when 2-byte */
  int recSize;        /* 2 or 3, from the header's own flag bit */
  int declared;       /* the length the header accounts for */
  int extra;          /* what the manifest asset has beyond it */
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
  /* How much of the tables points here: animation records that reach this frame
   * through this {scene, palette}, and whether any of the types that do is an
   * entity the scene's init table spawns itself rather than one of the three
   * spawn transforms (types 4, 6 and $0A, whose animation is the parent's
   * offset). Both are what orders several derived candidates. */
  uint16_t hits[GAL_MAX_FRAME_PAL];
  uint8_t  spawned[GAL_MAX_FRAME_PAL];
} FramePal;

/* An animation script is played by one entity, so every frame of it is drawn
 * with that entity's palette: an observation of any frame of a script is
 * evidence for every frame of it. These are the two directions of that map. */
#define GAL_MAX_FRAME_ANIM 4

/* VRAM is 32 KB of words; a 4bpp tile is 16 of them. */
#define GAL_VRAM_TILES 2048

typedef struct { int item, pal, page; } SecState;

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
   * metatile blitter's own $7800 columns are not in it: they are a column a
   * frame as the camera moves, not an init upload. */
  uint16_t vram[GAL_MODE_COUNT][0x8000];
  uint8_t vramSet[GAL_MODE_COUNT][0x8000];
  /* Per 4bpp VRAM tile: the palette row the scene's own maps reference it with
   * most often, +1 (0 = no map references it), and which upload put it there. */
  uint8_t vramRow[GAL_MODE_COUNT][GAL_VRAM_TILES];
  uint16_t vramVotes[GAL_MODE_COUNT][GAL_VRAM_TILES];
  uint8_t vramSrc[GAL_MODE_COUNT][GAL_VRAM_TILES];

  /* One row per manifest asset; only the sprite_frame rows are filled. */
  FramePal* framePal;
  /* frame asset -> the animation ids that name it, and animation id -> the
   * frames it names, as a head/next chain over the asset table. */
  uint8_t* frameAnimN;
  uint8_t (*frameAnim)[GAL_MAX_FRAME_ANIM];
  int animHead[GX_ANIM_COUNT];
  int* animNext;
  /* entity type -> how many animation records give it each OBJ palette, over
   * every scene: what orders the eight guesses for a frame no scene reaches. */
  uint32_t typePal[16][8];
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

  /* The two source lines the current page last set (see draw_source_foot). */
  char srcA[96], srcB[96];

  /* The picture strips' measured VRAM layout, found once (strip_find_base). */
  int stripBase[3], stripRot[3], stripCoarse[3], stripFine[3];
  bool stripDone[3];

  /* What the scene machine was last asked to draw, so a shot that finishes while
   * the page is showing something else is still taken into the cache. */
  bool prePending;
  int preMode;
  uint16_t preFid, preFlags;
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

/* Index 0 transparent, 1-15 an even grey ramp. What a page with no palette at
 * all is drawn through: nothing in the ROM says what colours these bytes are
 * meant to have, so the page shows the pixel values and says so. */
static void neutral_ramp(uint32_t* rgb) {
  for(int i = 0; i < 16; i++) {
    unsigned v = (unsigned) (i * 17);
    rgb[i] = RGB(v, v, v);
  }
}

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
  if(out == NULL) { *countOut = 0; return NULL; }   /* gallery_create refuses on NULL */
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
                          uint16_t flags, bool spawned, int aid) {
  int idx = asset_by_start(off);
  if(idx < 0 || kGalleryAssets[idx].kind != GK_SPRITE_FRAME) return;
  /* The script this frame belongs to, both ways round. */
  if(aid > 0 && (aid & 1) == 0 && aid < ANIM_IDS * 2) {
    uint8_t a = (uint8_t) (aid / 2);
    bool have = false;
    for(int i = 0; i < g->frameAnimN[idx]; i++) if(g->frameAnim[idx][i] == a) have = true;
    if(!have && g->frameAnimN[idx] < GAL_MAX_FRAME_ANIM) {
      g->frameAnim[idx][g->frameAnimN[idx]++] = a;
      g->animNext[idx] = g->animHead[a];
      g->animHead[a] = idx;
    }
  }
  if(type >= 0 && type < 32 && pal >= 0 && pal < 8) g->typePal[type / 2][pal]++;
  FramePal* fp = &g->framePal[idx];
  for(int i = 0; i < fp->n; i++)
    if(fp->mode[i] == mode && fp->pal[i] == pal) {
      fp->types[i] |= (uint16_t) (1u << (type / 2));
      if(fp->hits[i] < 0xFFFFu) fp->hits[i]++;
      if(spawned) fp->spawned[i] = 1;
      return;
    }
  if(fp->n >= GAL_MAX_FRAME_PAL) return;
  fp->mode[fp->n] = (uint8_t) mode;
  fp->pal[fp->n] = (uint8_t) pal;
  fp->types[fp->n] = (uint16_t) (1u << (type / 2));
  fp->hits[fp->n] = 1;
  fp->spawned[fp->n] = (uint8_t) (spawned ? 1 : 0);
  /* The whole flag word, not just its palette bits: bits 0-8 are the OBJ tile
   * slot the frame's tiles are DMA'd to and bits 12-13 the priority, which is
   * what a forced render has to hand the entity back. */
  fp->flags[fp->n] = flags;
  fp->n++;
}

/* Walk one animation script, recording its frames and following the one link a
 * terminator can carry (duration $FFFF = switch to the animation id in `frame`). */
static void walk_script(Gallery* g, int aid, uint8_t* seen, int mode, int pal, int type,
                        uint16_t flags, bool spawned) {
  if(aid <= 0 || (aid & 1) != 0 || aid >= ANIM_IDS * 2) return;
  uint16_t p = rd16(g, GX_ANIM_INDEX + (uint32_t) aid);
  uint32_t limit = GX_BANK_C4 + script_limit(g, p);
  uint32_t o = GX_BANK_C4 + p;
  for(int i = 0; i < 1024; i++) {
    uint32_t q = o + (uint32_t) (8 * i);
    if(q + 8u > limit) break;      /* the whole 8-byte record, not its first six */
    uint16_t dur = rd16(g, q + 4u), fr = rd16(g, q + 6u);
    if(dur == 0xFFFE) break;
    if(dur == 0xFFFF) {
      if(fr > 0 && (fr & 1) == 0 && fr < (uint16_t) (ANIM_IDS * 2) && !seen[fr / 2]) {
        seen[fr / 2] = 1;
        walk_script(g, fr, seen, mode, pal, type, flags, spawned);
      }
      break;
    }
    uint32_t off = frame_id_offset(g, fr);
    if(off != 0) frame_pal_add(g, off, mode, pal, type, flags, spawned, aid);
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
      int t0 = slot[i].type;
      uint8_t seen[174];
      memset(seen, 0, sizeof(seen));
      for(int a = 2; a < ANIM_IDS * 2; a += 2) {
        if(!slot[i].anims[a / 2] || seen[a / 2]) continue;
        seen[a / 2] = 1;
        walk_script(g, a, seen, mode, pal, slot[i].type, slot[i].flags,
                    t0 != 4 && t0 != 6 && t0 != 0x0A);
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

/* The shift goes through unsigned: s is a signed 4-bit nibble, and C11 6.5.7p4
 * leaves a left shift of a negative value undefined where Python, which
 * tools/assetcodec.py defines this decode in, does not. Every value here fits in
 * 16 bits, so the round trip through unsigned is the same number. */
static int brr_step(int nib, int shift) {
  int s = nib >= 8 ? nib - 16 : nib;
  if(shift <= 12) return (int) ((unsigned) s << shift) >> 1;
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
  /* No memory is not a reason to stop the app: the page draws an empty waveform
   * and plays nothing, which is what a zero block count already means here. */
  if(g->pcm == NULL) return 0;

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

/* Alternate format, decoded from its own header.
 *
 * The header is not opaque and the records are not one tile each. Over all 114
 * assets the manifest lists, these eight bytes account for the frame's length
 * exactly, with nothing left over:
 *
 *   hdr[0]  bit 7  the records carry a third byte, the OAM attribute
 *           bits 0-6  n1, the number of 16x16 sprites
 *   hdr[1]  n2, 8x8 sprites; hdr[2] off2, the VRAM tile they start at
 *   hdr[3]  n3, 8x8 sprites; hdr[4] off3, the VRAM tile they start at
 *   hdr[5]  nt1, the tiles of the first DMA chunk, which lands at VRAM tile 0
 *   hdr[6]  vo2, where the second chunk lands; hdr[7] nt2, its tile count
 *
 *   length = 8 + (3 or 2) * (n1 + n2 + n3) + 32 * (nt1 + nt2)
 *
 * and nt1 + nt2 = 4 * n1 + n2 + n3 in every frame, which is what a roster of n1
 * 16x16 sprites and n2 + n3 8x8 ones needs. A 16x16 sprite is four tiles in the
 * PPU's own name-table arrangement, t, t+1, t+16, t+17 across a sixteen-tile
 * VRAM row, and the i'th of them sits at 2 * (i % 8) + 32 * (i / 8): the tile
 * hdr[2] names is exactly the next free slot after n1 of those, in all 114.
 *
 * Records are {x, y} or {x, y, attr}, unsigned, top left, no bias, in file
 * order. There is no spill: every tile the header declares is used by exactly
 * one sprite and none is left over.
 *
 * Colour: none. Nothing in the ROM reads these frames, no CGRAM ever holds their
 * colours, and the attribute byte's palette bits name a row nothing uploads. The
 * page draws them through a neutral ramp and says so.
 */
static bool frame_build_alt(Gallery* g, int assetIdx, FrameInfo* fi) {
  uint32_t s = kGalleryAssets[assetIdx].start, e = kGalleryAssets[assetIdx].end;
  if(e < s + 8u) return false;
  memset(fi, 0, sizeof(*fi));
  for(int i = 0; i < 8; i++) fi->hdr[i] = rd8(g, s + (uint32_t) i);

  int n1 = fi->hdr[0] & 0x7F;
  bool wide = (fi->hdr[0] & 0x80) != 0;
  int n2 = fi->hdr[1], off2 = fi->hdr[2];
  int n3 = fi->hdr[3], off3 = fi->hdr[4];
  int nt1 = fi->hdr[5], vo2 = fi->hdr[6], nt2 = fi->hdr[7];
  int recs = n1 + n2 + n3, tiles = nt1 + nt2;
  int rsz = wide ? 3 : 2;

  fi->n1 = n1;
  fi->n2 = n2;
  fi->tileOff = off2;
  fi->unk1 = n3;
  fi->unk2 = off3;
  fi->ntiles1 = nt1;
  fi->vramOff = vo2;
  fi->flags = nt2;
  fi->ntiles2 = nt2;
  fi->nrec = recs;
  fi->ntiles = tiles;
  fi->recSize = rsz;
  if(recs <= 0 || tiles <= 0) return false;
  if(tiles != 4 * n1 + n2 + n3) fi->truncated = true;

  uint32_t recEnd = s + 8u + (uint32_t) (rsz * recs);
  uint32_t want = (uint32_t) (8 + rsz * recs + 32 * tiles);
  fi->declared = (int) want;
  fi->extra = (int) ((e - s) - want);          /* 0 when the header accounts for it all */
  if(recEnd + (uint32_t) (32 * tiles) > e) return false;

  /* The attribute byte the records carry, for the footer. It is not a palette
   * the page can use: nothing uploads a palette for these frames. */
  fi->altPal = 0;
  fi->altAttr = wide ? (int) rd8(g, s + 10u) : -1;

  int minx = 4096, miny = 4096, maxx = -4096, maxy = -4096;
  for(int i = 0; i < recs; i++) {
    int x = rd8(g, s + 8u + (uint32_t) (rsz * i));
    int y = rd8(g, s + 9u + (uint32_t) (rsz * i));
    int sz = i < n1 ? 16 : 8;
    if(x < minx) minx = x;
    if(y < miny) miny = y;
    if(x + sz > maxx) maxx = x + sz;
    if(y + sz > maxy) maxy = y + sz;
  }
  fi->cw = maxx - minx;
  fi->chh = maxy - miny;
  if(fi->cw < 8) fi->cw = 8;
  if(fi->chh < 8) fi->chh = 8;
  if(fi->cw > CANVAS_W) fi->cw = CANVAS_W;
  if(fi->chh > CANVAS_H) fi->chh = CANVAS_H;

  canvas_reset(g);
  uint8_t px[64];
  for(int i = 0; i < recs; i++) {
    int x = rd8(g, s + 8u + (uint32_t) (rsz * i)) - minx;
    int y = rd8(g, s + 9u + (uint32_t) (rsz * i)) - miny;
    /* The VRAM tile this sprite starts at, and from it the tile in the file:
     * the first chunk lands at tile 0, the second at vo2. */
    int v;
    if(i < n1) v = 2 * (i % 8) + 32 * (i / 8);
    else if(i < n1 + n2) v = off2 + (i - n1);
    else v = off3 + (i - n1 - n2);
    int quad = i < n1 ? 4 : 1;
    for(int q = 0; q < quad; q++) {
      int tv = v + (q & 1) + ((q >> 1) * 16);
      int f = tv < nt1 ? tv : nt1 + (tv - vo2);
      if(f < 0 || f >= tiles) continue;
      tile_pixels(g, recEnd + (uint32_t) (32 * f), 4, px);
      canvas_put(g, x + (q & 1) * 8, y + (q >> 1) * 8, px);
    }
  }
  fi->spill = 0;
  fi->w = fi->cw;
  fi->h = fi->chh;
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
/* `rot` rotates the map's columns, because a tilemap wraps: a caption that runs
 * off the right of a 32-column map comes back on the left, and the picture only
 * reads with the wrap taken out (strip_rotation below finds it). */
static void draw_tilemap_rot(const Gallery* g, uint32_t* fb, uint32_t mapOff, int words,
                             uint32_t tilesOff, uint32_t tilesEnd, const uint32_t* rgb,
                             bool wordPal, int baseTile, int cols, int x0, int y0,
                             int rot) {
  int ntiles = (int) ((tilesEnd - tilesOff) / 32u);
  uint8_t px[64];
  for(int i = 0; i < words; i++) {
    int c = i % cols, r = i / cols;
    int src = r * cols + ((c + rot) % cols + cols) % cols;
    uint16_t w = rd16(g, mapOff + (uint32_t) (2 * src));
    int idx = (w & 0x3FF) - baseTile;
    int cx = x0 + c * 8, cy = y0 + r * 8;
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

/* ---- where a page's content comes from -------------------------------------
 *
 * Every page says which bytes of the user's ROM it is showing and what the
 * program does with them: the file offsets, the manifest path under data/ the
 * same bytes extract to, and the routine or table that reads them. Two lines,
 * above the controls, in the ROM's own font. Where a value is not something the
 * ROM states, the line says "best guess", the way the palettes already do.
 * -------------------------------------------------------------------------- */
#define SRC_Y0 (FOOT_Y - 2 * LINE)

#if defined(__GNUC__)
__attribute__((format(printf, 2, 3)))
#endif
static void set_source(Gallery* g, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(g->srcA, sizeof(g->srcA), fmt, ap);
  va_end(ap);
  g->srcB[0] = 0;
}

#if defined(__GNUC__)
__attribute__((format(printf, 2, 3)))
#endif
static void set_source2(Gallery* g, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(g->srcB, sizeof(g->srcB), fmt, ap);
  va_end(ap);
}

/* The manifest path of an asset, without its directory, or "" when there is none. */
static const char* asset_file(int idx) {
  if(idx < 0 || idx >= (int) kGalleryAssetCount) return "";
  const char* p = kGalleryAssets[idx].path;
  if(p == NULL) return "";
  const char* slash = strrchr(p, '/');
  return slash != NULL ? slash + 1 : p;
}

static void draw_source_foot(const Gallery* g, uint32_t* fb) {
  if(g->srcA[0] != 0) fb_text(fb, 3, SRC_Y0, g->srcA, C_DIM);
  if(g->srcB[0] != 0) fb_text(fb, 3, SRC_Y0 + LINE, g->srcB, C_DIM);
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
#define GAL_SRC_SCRIPT   1
#define GAL_SRC_DERIVED  2
#define GAL_SRC_GUESS    3

typedef struct {
  uint8_t mode, pal, source;
  uint8_t via;         /* the animation id the observation travelled along, /2 */
  uint16_t types;      /* entity types the derivation attributes it to, if any */
  uint16_t flags;      /* the whole entity_flags word a forced render hands over */
  uint32_t rank;       /* what ordered it inside its own source */
} PalCand;

#define GAL_MAX_CAND (GAL_MAX_FRAME_PAL + 8)

/* The palette bits of an entity_flags word are bits 9-11; everything else in it
 * (the OBJ tile slot, the priority, the flips) is left as the evidence found it. */
static uint16_t flags_with_pal(uint16_t flags, int pal) {
  return (uint16_t) ((flags & ~0x0E00u) | ((unsigned) (pal & 7) << 9));
}

/* Did the observation pass see this frame itself, in this scene, at this palette? */
static bool obs_has(const Gallery* g, int idx, int mode, int pal) {
  uint8_t obs[SCENE_OBS_MODES];
  uint16_t fid = g->frameId != NULL ? g->frameId[idx] : 0;
  if(fid == 0 || g->scenes == NULL || !scenes_obs_ready(g->scenes)) return false;
  if(!scenes_obs_get(g->scenes, fid, obs)) return false;
  return ((obs[mode] >> pal) & 1u) != 0;
}

/* Order for the eight guesses: a frame no scene reaches has no entity of its
 * own, so the nearest frame that does have one lends its entity types, and the
 * palettes those types are given elsewhere come first. */
static uint16_t nearest_types(const Gallery* g, int idx) {
  for(int d = 1; d < 64; d++) {
    for(int s = 0; s < 2; s++) {
      int n = s ? idx - d : idx + d;
      if(n < 0 || n >= (int) kGalleryAssetCount) continue;
      if(kGalleryAssets[n].kind != GK_SPRITE_FRAME) continue;
      const FramePal* fp = &g->framePal[n];
      if(fp->n == 0) continue;
      uint16_t t = 0;
      for(int i = 0; i < fp->n; i++) t |= fp->types[i];
      if(t != 0) return t;
    }
  }
  return 0;
}

static int cand_cmp(const void* a, const void* b) {
  const PalCand* x = (const PalCand*) a;
  const PalCand* y = (const PalCand*) b;
  if(x->source != y->source) return x->source < y->source ? -1 : 1;
  if(x->rank != y->rank) return x->rank > y->rank ? -1 : 1;
  if(x->mode != y->mode) return x->mode < y->mode ? -1 : 1;
  return x->pal < y->pal ? -1 : (x->pal > y->pal ? 1 : 0);
}

/* Every palette this frame could be drawn with, best evidence first.
 *
 *  OBSERVED   the scene machine saw this frame in OAM with these palette bits.
 *  OBSERVED via script N
 *             the machine saw another frame of animation script N with them.
 *             One script is played by one entity and an entity's palette is a
 *             constant of its init record, so every frame of a script is drawn
 *             with the same palette: an observation anywhere in a script is an
 *             observation for all of it.
 *  DERIVED    the entity/animation walk allows it. Several are ordered by how
 *             much of the tables points at each: the number of animation records
 *             that reach this frame through that entity type, and whether the
 *             type is one the scene's init table spawns itself rather than one
 *             of the three spawn transforms.
 *  GUESS      no entity in any scene reaches this frame. The eight OBJ palettes
 *             of game_mode 0, ordered by what the nearest frame with an entity
 *             is given elsewhere.
 *
 * Where the derivation and an observation disagree the observation wins, because
 * it is a measurement: the derived candidate is still listed, below.
 */
static int frame_pal_candidates(const Gallery* g, int idx, PalCand* out) {
  int n = 0;
  const FramePal* fp = g->framePal != NULL ? &g->framePal[idx] : NULL;
  uint8_t obs[SCENE_OBS_MODES];
  uint16_t fid = g->frameId != NULL ? g->frameId[idx] : 0;
  bool ready = g->scenes != NULL && scenes_obs_ready(g->scenes);
  if(fid != 0 && ready && scenes_obs_get(g->scenes, fid, obs)) {
    for(int m = 0; m < SCENE_OBS_MODES && n < GAL_MAX_CAND; m++)
      for(int p = 0; p < 8 && n < GAL_MAX_CAND; p++) {
        if(((obs[m] >> p) & 1u) == 0) continue;
        memset(&out[n], 0, sizeof(out[n]));
        out[n].mode = (uint8_t) m;
        out[n].pal = (uint8_t) p;
        out[n].source = GAL_SRC_OBSERVED;
        /* The flag word the entity carried when it was seen: that is the tile
         * slot and priority the game itself gave this frame. */
        out[n].flags = flags_with_pal(scenes_obs_flags(g->scenes, fid, m), p);
        if(fp != NULL)
          for(int i = 0; i < fp->n; i++)
            if(fp->mode[i] == m && fp->pal[i] == p) out[n].types = fp->types[i];
        n++;
      }
  }
  /* The same observation, carried along the animation scripts this frame is in. */
  if(ready && g->frameAnimN != NULL)
    for(int k = 0; k < g->frameAnimN[idx] && n < GAL_MAX_CAND; k++) {
      int a = g->frameAnim[idx][k];
      for(int f = g->animHead[a]; f >= 0 && n < GAL_MAX_CAND; f = g->animNext[f]) {
        uint16_t ofid = g->frameId[f];
        uint8_t o2[SCENE_OBS_MODES];
        if(f == idx || ofid == 0 || !scenes_obs_get(g->scenes, ofid, o2)) continue;
        for(int m = 0; m < SCENE_OBS_MODES && n < GAL_MAX_CAND; m++)
          for(int p = 0; p < 8 && n < GAL_MAX_CAND; p++) {
            if(((o2[m] >> p) & 1u) == 0) continue;
            bool dup = false;
            for(int i = 0; i < n; i++)
              if(out[i].mode == m && out[i].pal == p) dup = true;
            if(dup) continue;
            memset(&out[n], 0, sizeof(out[n]));
            out[n].mode = (uint8_t) m;
            out[n].pal = (uint8_t) p;
            out[n].source = GAL_SRC_SCRIPT;
            out[n].via = (uint8_t) a;
            out[n].flags = flags_with_pal(scenes_obs_flags(g->scenes, ofid, m), p);
            if(fp != NULL)
              for(int i = 0; i < fp->n; i++)
                if(fp->mode[i] == m && fp->pal[i] == p) out[n].types = fp->types[i];
            n++;
          }
      }
    }
  int firstDerived = n;
  if(fp != NULL) {
    for(int i = 0; i < fp->n && n < GAL_MAX_CAND; i++) {
      bool dup = false;
      for(int k = 0; k < n; k++)
        if(out[k].mode == fp->mode[i] && out[k].pal == fp->pal[i]) dup = true;
      if(dup) continue;
      memset(&out[n], 0, sizeof(out[n]));
      out[n].mode = fp->mode[i];
      out[n].pal = fp->pal[i];
      out[n].source = GAL_SRC_DERIVED;
      out[n].types = fp->types[i];
      out[n].flags = flags_with_pal(fp->flags[i], fp->pal[i]);
      out[n].rank = (uint32_t) fp->hits[i] + (fp->spawned[i] ? 0x10000u : 0u);
      n++;
    }
  }
  /* No entity in any of the four scenes plays this frame, so there is no
   * evidence to order. The game can still be made to draw it: game_mode 0, each
   * of the eight OBJ palettes, labelled as guesses, commonest first. */
  if(n == 0) {
    uint16_t types = nearest_types(g, idx);
    for(int p = 0; p < 8; p++) {
      memset(&out[n], 0, sizeof(out[n]));
      out[n].mode = 0;
      out[n].pal = (uint8_t) p;
      out[n].source = GAL_SRC_GUESS;
      out[n].flags = flags_with_pal(g->modeFlags[0], p);
      uint32_t r = 0;
      for(int t = 0; t < 16; t++) if((types >> t) & 1u) r += g->typePal[t][p];
      out[n].rank = r;
      n++;
    }
    qsort(out, (size_t) n, sizeof(out[0]), cand_cmp);
    return n;
  }
  if(n > firstDerived)
    qsort(out + firstDerived, (size_t) (n - firstDerived), sizeof(out[0]), cand_cmp);
  return n;
}

/* Where the tables and the measurement disagree: an observed frame whose top
 * derived candidate names a different {scene, palette}. The observation wins;
 * this counts the cases so the report can say how many there are. */
static bool frame_pal_disagrees(const Gallery* g, int idx, PalCand* top) {
  PalCand cand[GAL_MAX_CAND];
  int n = frame_pal_candidates(g, idx, cand);
  if(n == 0 || cand[0].source > GAL_SRC_SCRIPT) return false;
  const FramePal* fp = &g->framePal[idx];
  if(fp->n == 0) return false;
  for(int i = 0; i < n; i++)
    if(cand[i].source >= GAL_SRC_DERIVED) {
      if(cand[i].mode == cand[0].mode && cand[i].pal == cand[0].pal) return false;
      if(top != NULL) *top = cand[i];
      return !obs_has(g, idx, cand[i].mode, cand[i].pal);
    }
  return false;
}

/* How the whole corpus comes out: observed, derived only, and neither. */
static void frame_pal_tally(const Gallery* g, GalleryPalTally* t) {
  memset(t, 0, sizeof(*t));
  for(int i = 0; i < g->liveCount; i++) {
    int idx = g->live[i];
    PalCand cand[GAL_MAX_CAND];
    int n = frame_pal_candidates(g, idx, cand);
    int src = n > 0 ? cand[0].source : GAL_SRC_GUESS;
    if(src == GAL_SRC_OBSERVED) t->observed++;
    else if(src == GAL_SRC_SCRIPT) t->viaScript++;
    else if(src == GAL_SRC_DERIVED) t->derived++;
    else t->guess++;
    if(src == GAL_SRC_DERIVED) {
      /* Did the ranking move this frame's top derived candidate off the one the
       * table walk happened to record first? */
      const FramePal* fp = &g->framePal[idx];
      if(fp->n > 1 && (fp->mode[0] != cand[0].mode || fp->pal[0] != cand[0].pal))
        t->reordered++;
      if(fp->n > 1) t->multi++;
    }
    if(frame_pal_disagrees(g, idx, NULL)) t->disagree++;
  }
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

static uint16_t flip_bits(int flip);

/* Take whatever the machine has finished into the cache. The page asks for the
 * frame it is showing and then for its neighbours, so a shot often lands while
 * the page is drawing something else. */
static void shot_take(Gallery* g) {
  if(!g->prePending || g->scenes == NULL) return;
  const SpriteShot* got = scenes_sprite_get(g->scenes, g->preMode, g->preFid, g->preFlags);
  if(got == NULL) return;
  shot_keep(g, got);
  g->prePending = false;
}

static void shot_ask(Gallery* g, int mode, uint16_t fid, uint16_t flags) {
  if(g->scenes == NULL || fid == 0) return;
  scenes_sprite_request(g->scenes, mode, fid, flags);
  g->prePending = true;
  g->preMode = mode;
  g->preFid = fid;
  g->preFlags = flags;
}

/* The shot for this frame and palette, asking the scene machine for it when it
 * is not already in hand. NULL means "not drawn yet": the page says so. */
static const SpriteShot* shot_for(Gallery* g, int mode, uint16_t fid, uint16_t flags) {
  if(fid == 0 || g->scenes == NULL) return NULL;
  shot_take(g);
  const SpriteShot* hit = shot_cached(g, mode, fid, flags);
  if(hit != NULL) return hit;
  shot_ask(g, mode, fid, flags);
  return NULL;
}

/* The frames either side of the one on screen, drawn in the idle slices the page
 * already gives the scene machine, so walking the list does not wait. The plan
 * for a neighbour is the plan the page would use for it: its own scene and its
 * own flag word, with the flip the page is showing. */
#define GAL_PREFETCH 3
static void shot_prefetch(Gallery* g, int item) {
  if(g->scenes == NULL || g->prePending || scenes_busy(g->scenes)) return;
  static const int kOrder[2 * GAL_PREFETCH] = { 1, -1, 2, -2, 3, -3 };
  for(int k = 0; k < 2 * GAL_PREFETCH; k++) {
    int n = item + kOrder[k];
    if(n < 0 || n >= g->liveCount) continue;
    GalleryFramePlan plan;
    if(!gallery_frame_plan(g, n, &plan)) continue;
    uint16_t flags = (uint16_t) ((plan.flags & ~(SPRITE_FLIP_H | SPRITE_FLIP_V))
                                 | flip_bits(g->spriteFlip));
    if(shot_cached(g, plan.mode, plan.frameId, flags) != NULL) continue;
    shot_ask(g, plan.mode, plan.frameId, flags);
    return;
  }
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

  FrameInfo fi;
  memset(&fi, 0, sizeof(fi));
  bool ok = alt ? frame_build_alt(g, idx, &fi) : frame_build_live(g, idx, &fi);

  /* The palettes the game gives this frame: what the scene machine saw in OAM
   * first, then what the entity/animation walk derives, then the eight guesses
   * for a frame no entity plays. An alternate-format frame is played by nothing,
   * so the nearest evidence is the palette its own OAM records carry. */
  PalCand cand[GAL_MAX_CAND];
  int palIdx = alt && near >= 0 ? near : idx;
  /* An alternate frame has no palette at all: nothing in the ROM reads these
   * bytes, so no CGRAM ever holds their colours. The page draws them through a
   * neutral ramp and offers the named rows only as an explicit override. */
  int nauto = alt ? 1 : frame_pal_candidates(g, palIdx, cand);
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
  int via = 0;
  if(sel < nauto && !alt) {
    mode = cand[sel].mode;
    pal = cand[sel].pal;
    source = cand[sel].source;
    flags = (uint16_t) ((cand[sel].flags & ~(SPRITE_FLIP_H | SPRITE_FLIP_V))
                        | flip_bits(g->spriteFlip));
    pal_from_cgram(g, mode, 0x80 + 16 * pal, 16, rgb);
    static const char* const kSrcName[4] = { "OBSERVED", "OBSERVED", "DERIVED", "GUESS" };
    via = cand[sel].via;
    if(source == GAL_SRC_SCRIPT)
      snprintf(palLine, sizeof(palLine), "OBSERVED via script %d, pal %d = $%02X, %s",
               via * 2, pal, 0x80 + 16 * pal, kGalleryModeName[mode]);
    else
      snprintf(palLine, sizeof(palLine), "%s pal %d = CGRAM $%02X, %s",
               kSrcName[source], pal, 0x80 + 16 * pal, kGalleryModeName[mode]);
    char t[32];
    types_str(cand[sel].types, t, sizeof(t));
    static const char* const kWho[4] = { "seen in OAM,", "another frame of it,",
                                         "entity", "no entity," };
    snprintf(whoLine, sizeof(whoLine), "%s types %s   %d/%d",
             kWho[source], t, sel + 1, nauto);
  } else if(sel < nauto) {
    neutral_ramp(rgb);
    snprintf(palLine, sizeof(palLine), "no palette information in the ROM");
    snprintf(whoLine, sizeof(whoLine), "unreferenced; Winning Run baseball sprites (visual id)");
    rendered = false;
  } else {
    pal_override(g, sel - nauto, rgb, palLine, sizeof(palLine));
    snprintf(whoLine, sizeof(whoLine), "%s",
             nauto ? "not the palette the game uses"
                   : "no entity in modes 0-3 plays this frame");
    rendered = false;
  }

  /* Nothing of the frame is drawn until the game has drawn it. The file layout
   * is a different picture at a different size, so putting it up first and
   * replacing it moves the image under the eye; the box stays empty and says
   * what it is waiting for instead. The file layout is still there, on B. */
  const SpriteShot* shot = NULL;
  if(rendered) {
    uint16_t fid = g->frameId != NULL ? g->frameId[palIdx] : 0;
    shot = shot_for(g, mode, fid, flags);
    if(shot != NULL && shot->w > 0) {
      shot_draw(fb, shot, 4, 12, 248, 136);
      if(!alt) shot_prefetch(g, st->item);
    } else {
      fb_fill(fb, 4, 12, 248, 1, C_RULE);
      fb_fill(fb, 4, 147, 248, 1, C_RULE);
      fb_fill(fb, 4, 12, 1, 136, C_RULE);
      fb_fill(fb, 251, 12, 1, 136, C_RULE);
      const char* why = shot != NULL && shot->why[0] ? shot->why : "rendering";
      fb_text(fb, 8, 76, why, C_MARK);
    }
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
    fb_textf(fb, 3, y, C_TEXT, "%dx16 + %d 8x8  %d tiles  %d-byte recs",
             fi.n1, fi.n2 + fi.unk1, fi.ntiles, fi.recSize);
    y += LINE;
    if(fi.extra != 0)
      fb_textf(fb, 3, y, C_MARK, "header accounts for %d of %u bytes: %d more",
               fi.declared, asset_size(idx), fi.extra);
    else if(fi.altAttr >= 0)
      fb_textf(fb, 3, y, C_DIM, "attr $%02X  hdr %02X %02X %02X %02X %02X %02X %02X %02X",
               fi.altAttr, fi.hdr[0], fi.hdr[1], fi.hdr[2], fi.hdr[3], fi.hdr[4],
               fi.hdr[5], fi.hdr[6], fi.hdr[7]);
    else
      fb_textf(fb, 3, y, C_DIM, "no attr byte  hdr %02X %02X %02X %02X %02X %02X %02X %02X",
               fi.hdr[0], fi.hdr[1], fi.hdr[2], fi.hdr[3], fi.hdr[4],
               fi.hdr[5], fi.hdr[6], fi.hdr[7]);
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
      GalleryPalTally t;
      frame_pal_tally(g, &t);
      fb_textf(fb, 3, y, C_DIM, "obs %d  via script %d  derived %d  guess %d",
               t.observed, t.viaScript, t.derived, t.guess);
    }
  }
  if(alt) {
    set_source(g, "frame %06X (%.22s), unread by the game",
               kGalleryAssets[idx].start, asset_file(idx));
    set_source2(g, "second sprite format, no table points at it");
  } else {
    char who[24];
    types_str(sel < nauto ? cand[sel].types : 0, who, sizeof(who));
    static const char* const kHow[4] = { "observed", "observed via a script",
                                         "derived", "best guess" };
    set_source(g, "frame %u: file %06X (%.18s)",
               (unsigned) (g->frameId != NULL ? g->frameId[idx] / 4 : 0),
               kGalleryAssets[idx].start, asset_file(idx));
    set_source2(g, "drawn by the game in %s, type %.10s pal %d (%s)",
                kGalleryModeName[mode], who, pal,
                sel < nauto ? kHow[source] : "override");
  }
  draw_source_foot(g, fb);
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
  {
    const GalleryVramUpload* firstV = NULL;
    const GalleryPalUpload* firstP = NULL;
    for(unsigned i = 0; i < kGalleryVramUploadCount && firstV == NULL; i++)
      if(kGalleryVramUploads[i].mode == mode && kGalleryVramUploads[i].src != GAL_VRAM_FILL)
        firstV = &kGalleryVramUploads[i];
    for(unsigned i = 0; i < kGalleryPalUploadCount && firstP == NULL; i++)
      if(kGalleryPalUploads[i].mode == mode) firstP = &kGalleryPalUploads[i];
    unsigned nup = 0;
    for(unsigned i = 0; i < kGalleryVramUploadCount; i++)
      if(kGalleryVramUploads[i].mode == mode) nup++;
    set_source(g, "VRAM as %s leaves it: %u uploads replayed",
               kGalleryModeName[mode], nup);
    set_source2(g, "first %06X -> $%04X (%s), CGRAM from %06X",
                firstV != NULL ? firstV->src : 0, firstV != NULL ? firstV->vram : 0,
                firstV != NULL ? firstV->site : "", firstP != NULL ? firstP->src : 0);
  }
  draw_source_foot(g, fb);
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
    /* The picker fills one 16-colour row. An 8bpp page indexes the whole block
     * and its strip reads 32 entries, so the row is repeated across all sixteen
     * rather than left as whatever the last call to this function put on the
     * stack. Sixteen colours are all the picker has; repeating them says so,
     * where uninitialised memory would say something different every frame. */
    if(bpp == 8)
      for(int r = 1; r < 16; r++)
        for(int i = 0; i < 16; i++) rgb[r * 16 + i] = rgb[i];
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
  set_source(g, "one manifest asset in file order: %06X (%.20s)",
             start, asset_file(idx));
  set_source2(g, "%s, %d tiles, through one palette row",
              kGalleryKindName[kGalleryAssets[idx].kind], ntiles);
  draw_source_foot(g, fb);
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

/* One 32x32 metatile into the framebuffer at (px0, py0). A tile pixel of zero is
 * the transparent index, and what a level draws there is whatever is behind it,
 * so the page shows the checker rather than pretending it is a colour. */
static void meta_draw(const Gallery* g, uint32_t* fb,
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
          fb_px(fb, dx, dy, v == 0 ? checker(dx, dy) : pal[v]);
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
      meta_draw(g, fb, mode, bt, i, c * META_PX, 12 + r * META_PX);
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
  set_source(g, "metatiles %06X (%.20s), 32 bytes each", start,
             asset_file(asset_at(start)));
  set_source2(g, "through %s VRAM at $%04X and its CGRAM",
              kGalleryModeName[mode], bt->charBase);
  draw_source_foot(g, fb);
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

/* ---- the picture strips' own VRAM layout, found by brute force -------------
 *
 * Nothing uploads these three maps or their tilesets, so the tile index a map
 * word carries counts from a VRAM base no code states. The earlier reading took
 * the base from a convention (the index the map pads with names an all-zero tile
 * of the set, or failing that the lowest index the map uses). That is right for
 * strips 1 and 3 and wrong for strip 2, whose set holds no all-zero tile at all:
 * the convention put it ten tiles out and the caption came apart.
 *
 * So the base is measured instead. A caption is a picture, and a picture's tiles
 * agree along the edges they share: for every base the map can be read at, the
 * whole map is laid out and the pixels either side of every tile seam are
 * compared. Two scores, because one does not separate every case:
 *
 *   coarse   every seam pixel pair, background included, counted as agreeing
 *            when the two indices are within three of each other. A caption is a
 *            ramp, so a correct base makes almost every pair agree.
 *   fine     only the pairs where at least one side has ink, counted as agreeing
 *            when both do and are within two. This is what separates a base that
 *            merely keeps the background quiet from one that joins the letters.
 *
 * The coarse score picks the base; where two bases are within half a percent of
 * each other on it, the fine score breaks the tie. That gives $44, $45 and $43,
 * and all three captions read. L and R nudge the base by hand and the page shows
 * what it is using, so the measurement can be argued with.
 *
 * The maps are 32 columns of a tilemap, and a tilemap wraps: strip 2's caption
 * sits in columns 21-31 and 0-10, and strip 3's runs off the right and comes
 * back on the left. The rotation is measured the same way: whichever rotation
 * leaves the least ink in the two edge columns, with the inked span centred to
 * break a tie. That is 0, 16 and 16.
 * -------------------------------------------------------------------------- */

#define STRIP_WORDS 128
#define STRIP_COLS  32

/* The pixels of the tile a map word names, or NULL when the base puts it outside
 * the set. */
static bool strip_tile(const Gallery* g, uint32_t tOff, int ntiles, uint16_t w,
                       int base, uint8_t* px) {
  int idx = (w & 0x3FF) - base;
  if(idx < 0 || idx >= ntiles) return false;
  uint8_t raw[64];
  tile_pixels(g, tOff + (uint32_t) (idx * 32), 4, raw);
  bool hf = (w & 0x4000u) != 0, vf = (w & 0x8000u) != 0;
  for(int y = 0; y < 8; y++)
    for(int x = 0; x < 8; x++) px[y * 8 + x] = raw[(vf ? 7 - y : y) * 8 + (hf ? 7 - x : x)];
  return true;
}

/* The two seam scores, in thousandths, and how many content words the base puts
 * outside the set (the pad is allowed to miss: that is what a pad is for). */
static void strip_score(const Gallery* g, uint32_t mapOff, int words, uint32_t tOff,
                        int ntiles, int base, int pad, int* coarse, int* fine,
                        int* missed, int* placed) {
  static uint8_t px[STRIP_WORDS][64];
  static uint8_t have[STRIP_WORDS];
  int miss = 0, n = 0;
  for(int i = 0; i < words && i < STRIP_WORDS; i++) {
    uint16_t w = rd16(g, mapOff + (uint32_t) (2 * i));
    if((w & 0x3FF) == 0) { have[i] = 0; continue; }
    have[i] = (uint8_t) (strip_tile(g, tOff, ntiles, w, base, px[i]) ? 1 : 0);
    if(have[i]) n++;
    else if((w & 0x3FF) != pad) miss++;
  }
  int cg = 0, ct = 0, fg = 0, ft = 0;
  for(int i = 0; i < words && i < STRIP_WORDS; i++) {
    if(!have[i]) continue;
    int c = i % STRIP_COLS;
    for(int d = 0; d < 2; d++) {
      int j = d == 0 ? i + 1 : i + STRIP_COLS;
      if(d == 0 && c == STRIP_COLS - 1) continue;
      if(j >= words || j >= STRIP_WORDS || !have[j]) continue;
      for(int k = 0; k < 8; k++) {
        int a = d == 0 ? px[i][k * 8 + 7] : px[i][7 * 8 + k];
        int b = d == 0 ? px[j][k * 8 + 0] : px[j][0 * 8 + k];
        ct++;
        if(a - b <= 3 && b - a <= 3) cg++;
        if(a == 0 && b == 0) continue;
        ft++;
        if(a != 0 && b != 0 && a - b <= 2 && b - a <= 2) fg++;
      }
    }
  }
  *coarse = ct > 0 ? cg * 1000 / ct : 0;
  *fine = ft > 0 ? fg * 1000 / ft : 0;
  *missed = miss;
  *placed = n;
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

static int strip_find_base(const Gallery* g, uint32_t mapOff, int words, uint32_t tOff,
                          int ntiles, int pad, int* coarseOut, int* fineOut) {
  int best = pad, bestC = -1, bestF = -1;
  for(int base = -32; base < ntiles; base++) {
    int c = 0, f = 0, miss = 0, placed = 0;
    strip_score(g, mapOff, words, tOff, ntiles, base, pad, &c, &f, &miss, &placed);
    if(miss > 1 || placed < words / 4) continue;
    /* within half a percent of the best coarse score is a tie, and the fine
     * score breaks it */
    if(c > bestC + 5 || (c + 5 >= bestC && c - 5 <= bestC && f > bestF)) {
      if(c > bestC) bestC = c;
      bestF = f;
      best = base;
    }
  }
  if(coarseOut != NULL) *coarseOut = bestC;
  if(fineOut != NULL) *fineOut = bestF;
  return best;
}

/* How much ink each column of the laid-out map holds. */
static void strip_col_ink(const Gallery* g, uint32_t mapOff, int words, uint32_t tOff,
                          int ntiles, int base, int* ink) {
  uint8_t px[64];
  for(int c = 0; c < STRIP_COLS; c++) ink[c] = 0;
  for(int i = 0; i < words && i < STRIP_WORDS; i++) {
    uint16_t w = rd16(g, mapOff + (uint32_t) (2 * i));
    if((w & 0x3FF) == 0 || !strip_tile(g, tOff, ntiles, w, base, px)) continue;
    for(int k = 0; k < 64; k++) if(px[k] != 0) ink[i % STRIP_COLS]++;
  }
}

static int strip_rotation(const Gallery* g, uint32_t mapOff, int words, uint32_t tOff,
                          int ntiles, int base) {
  int ink[STRIP_COLS];
  strip_col_ink(g, mapOff, words, tOff, ntiles, base, ink);
  int best = 0, bestEdge = -1, bestGap = -1;
  for(int rot = 0; rot < STRIP_COLS; rot++) {
    int edge = ink[rot % STRIP_COLS] + ink[(rot + STRIP_COLS - 1) % STRIP_COLS];
    /* the tie-break: how much blank the rotation leaves at the two ends */
    int lead = 0, trail = 0;
    while(lead < STRIP_COLS && ink[(rot + lead) % STRIP_COLS] == 0) lead++;
    while(trail < STRIP_COLS && ink[(rot + STRIP_COLS - 1 - trail) % STRIP_COLS] == 0) trail++;
    int gap = lead < trail ? lead : trail;
    if(bestEdge < 0 || edge < bestEdge || (edge == bestEdge && gap > bestGap)) {
      bestEdge = edge;
      bestGap = gap;
      best = rot;
    }
  }
  return best;
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
    set_source(g, "font %06X (%.20s), 2bpp, 96 glyphs", ROM_FONT_OFF,
               asset_file(idx));
    set_source2(g, "no code uploads it; the app is lettered with it");
    draw_source_foot(g, fb);
  draw_source_foot(g, fb);
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
  int pad = strip_pad(g, mapOff, words);

  /* Measured once per strip and kept: the brute force is 350 layouts of a
   * 128-word map and is not worth repeating sixty times a second. */
  if(!g->stripDone[sIdx]) {
    int coarse = 0, fine = 0;
    g->stripBase[sIdx] = strip_find_base(g, mapOff, words, tOff, ntiles, pad,
                                         &coarse, &fine);
    g->stripRot[sIdx] = strip_rotation(g, mapOff, words, tOff, ntiles,
                                       g->stripBase[sIdx]);
    g->stripCoarse[sIdx] = coarse;
    g->stripFine[sIdx] = fine;
    g->stripDone[sIdx] = true;
  }
  int base = g->stripBase[sIdx] + st->page;      /* L/R nudges it by hand */
  int rot = g->stripRot[sIdx];

  /* how many words this base leaves outside the set: with the right base, none */
  int outside = 0;
  for(int i = 0; i < words; i++) {
    int v = (rd16(g, mapOff + (uint32_t) (2 * i)) & 0x3FF);
    if(v == pad || v == 0) continue;            /* the pads are meant to miss */
    if(v - base < 0 || v - base >= ntiles) outside++;
  }

  /* No palette. Nothing in the ROM references these strips, so no CGRAM ever
   * holds their colours and there is nothing to derive one from: they are drawn
   * through a neutral ramp, index 0 transparent and 1-15 an even grey. The named
   * rows are still reachable, as an explicit override that says so. */
  int over = st->pal % (pal_total(kMainPals, MAIN_PAL_N) + 1);
  if(over == 0) {
    neutral_ramp(rgb);
    snprintf(palLine, sizeof(palLine), "no palette in the ROM: neutral ramp");
  } else {
    pal_override(g, over - 1, rgb, palLine, sizeof(palLine));
  }

  draw_tilemap_rot(g, fb, mapOff, words, tOff, tEnd, rgb, false, base, STRIP_COLS,
                   0, 12, rot);
  int mapH = ((words + 31) / 32) * 8;
  fb_text(fb, 3, 12 + mapH + 4, "tileset from the measured base:", C_DIM);
  int rows = (144 - (12 + mapH + 14)) / 8;
  if(rows > 0)
    draw_tile_grid(g, fb, tOff, tEnd, 4, rgb, 0, 12 + mapH + 14, 32, rows, 0);
  draw_palette_strip(fb, rgb, 16, 3, 146);

  int y = 154;
  fb_textf(fb, 3, y, C_TEXT, "strip %d map %06X %dw tiles %06X", sIdx + 1,
           mapOff, words, tOff);
  y += LINE;
  fb_textf(fb, 3, y, C_TEXT, "base $%02X%s rot %d  %d tiles  %d out",
           base & 0xFF, st->page != 0 ? "*" : " ", rot, ntiles, outside);
  y += LINE;
  fb_textf(fb, 3, y, over == 0 ? C_DIM : C_MARK, "%s", palLine);
  y += LINE;
  fb_textf(fb, 3, y, C_DIM, "seam fit %d.%d%% / %d.%d%%, %d blank tile(s)",
           g->stripCoarse[sIdx] / 10, g->stripCoarse[sIdx] % 10,
           g->stripFine[sIdx] / 10, g->stripFine[sIdx] % 10, nblocks);
  set_source(g, "bank $C1 leftovers, referenced by nothing");
  set_source2(g, "map %06X (%.14s), tiles %06X (%.14s)", mapOff, asset_file(mapIdx),
              tOff, asset_file(tilesIdx));
  draw_source_foot(g, fb);
  draw_foot(fb, "dpad item+palette  LR nudge base  A back");
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
  set_source(g, "stale image 000000-008000: an older assembly");
  set_source2(g, "tiles %06X, palette %06X, anim table %06X",
              PREV_TILES_OFF, PREV_PAL_OFF, PREV_ANIM_OFF);
  draw_source_foot(g, fb);
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
  if(st->item < songs) {
    int idx = g->song[st->item];
    set_source(g, "song block %06X (%.20s)", kGalleryAssets[idx].start,
               asset_file(idx));
    set_source2(g, "uploaded to SPC $1300 by spc_command(%d)", st->item);
  } else if(st->item < songs + n1) {
    set_source(g, "sfx bank 1 at %06X, pointer SPC $%04X", SFX_BANK1_OFF,
               0x2412 + 2 * (st->item - songs));
    set_source2(g, "sent through sfx_command_dispatch, dest SPC $2410");
  } else {
    set_source(g, "sfx bank 2 at %06X, pointer SPC $%04X", SFX_BANK2_OFF,
               0x2E96 + 2 * (st->item - songs - n1));
    set_source2(g, "dest SPC $2E94; sfx_start takes id >= $60 here");
  }
  draw_source_foot(g, fb);
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
  set_source(g, "BRR record %d at %06X (%.20s)", st->item,
             kGalleryAssets[idx].start, asset_file(idx));
  set_source2(g, "decoded here; %s", (st->item < 256 && !g->brrUsed[st->item])
              ? "no song's sample list names it"
              : "a song's sample list names it");
  draw_source_foot(g, fb);
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
  set_source(g, "stale region %06X-%06X (%.16s)", kGalleryAssets[idx].start,
             kGalleryAssets[idx].end, asset_file(idx));
  set_source2(g, "the manifest note names the live region it shadows");
  draw_source_foot(g, fb);
  draw_foot(fb, "dpad region  LR x5  A back");
}

/* ---- input ------------------------------------------------------------------------ */

#define BIT(b) ((uint16_t) (1u << (b)))

/* The palette cursor is allowed to go negative: every page wraps it itself, over
 * "the palettes the game gives this item, then the override picker". */
static void clamp_state(Gallery* g) {
  for(int s = 0; s < GALLERY_SECTION_COUNT; s++) {
    /* the Fonts page's `page` is the picture strips' own base nudge, which is
     * allowed to go either way */
    if(g->st[s].page < 0 && s != GALLERY_SEC_FONTS) g->st[s].page = 0;
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

static void apply(Gallery* g, uint16_t press) {
  if(g->section < 0 || g->section >= GALLERY_SECTION_COUNT) return;
  SecState* st = &g->st[g->section];
  bool list = g->section == GALLERY_SEC_SAMPLES || g->section == GALLERY_SEC_STALE ||
              g->section == GALLERY_SEC_MUSIC;

  if(press & BIT(GALLERY_BTN_A)) { gallery_close(g); return; }

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
  } else if(g->section == GALLERY_SEC_FONTS) {
    /* L and R nudge the measured tile base by hand rather than paging: the base
     * is a measurement and this is how it is argued with. */
    if(press & BIT(GALLERY_BTN_L)) st->page--;
    if(press & BIT(GALLERY_BTN_R)) st->page++;
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
  "sprites", "alt", "backgrounds", "fonts", "prev", "music", "samples", "stale",
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

/* How many of the alternate frames the manifest lists have a header that
 * accounts for their length exactly, and how many bytes the rest have beyond
 * what their header declares (another frame starts there). */
void gallery_alt_header_fit(Gallery* g, int* exact, int* total, int* beyond) {
  int ex = 0, n = 0, over = 0;
  if(g != NULL)
    for(int i = 0; i < g->altCount; i++) {
      FrameInfo fi;
      n++;
      if(!frame_build_alt(g, g->alt[i], &fi)) continue;
      if(fi.extra == 0) ex++;
      else over += fi.extra;
    }
  if(exact != NULL) *exact = ex;
  if(total != NULL) *total = n;
  if(beyond != NULL) *beyond = over;
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

void gallery_frame_pal_counts(const Gallery* g, GalleryPalTally* out,
                              int* firstObserved) {
  if(out != NULL) {
    memset(out, 0, sizeof(*out));
    if(g != NULL) frame_pal_tally(g, out);
  }
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

/* The sprite page is the one that needs a machine, for the forced render and the
 * OAM palette observation. */
bool gallery_wants_scenes(const Gallery* g) {
  if(g == NULL || !g->open) return false;
  return g->section == GALLERY_SEC_SPRITES;
}

Gallery* gallery_create(const uint8_t* rom, size_t romLen) {
  Gallery* g = calloc(1, sizeof(Gallery));
  if(g == NULL) return NULL;
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
  if(g->canvas == NULL || g->claimed == NULL || g->framePal == NULL ||
     g->live == NULL || g->alt == NULL || g->bg == NULL ||
     g->brr == NULL || g->stale == NULL || g->song == NULL) {
    gallery_destroy(g);
    return NULL;
  }
  g->frameId = calloc((size_t) kGalleryAssetCount, sizeof(uint16_t));
  g->frameAnimN = calloc((size_t) kGalleryAssetCount, 1);
  g->frameAnim = calloc((size_t) kGalleryAssetCount, GAL_MAX_FRAME_ANIM);
  g->animNext = calloc((size_t) kGalleryAssetCount, sizeof(int));
  if(g->frameId == NULL || g->frameAnimN == NULL || g->frameAnim == NULL ||
     g->animNext == NULL) {
    gallery_destroy(g);
    return NULL;
  }
  for(unsigned i = 0; i < GX_ANIM_COUNT; i++) g->animHead[i] = -1;
  for(unsigned i = 0; i < kGalleryAssetCount; i++) g->animNext[i] = -1;
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
  free(g->frameAnimN);
  free(g->frameAnim);
  free(g->animNext);
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
