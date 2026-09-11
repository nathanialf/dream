/* romfont: see romfont.h. */
#include "romfont.h"

#include <string.h>

/* One row of a glyph as a bitmask, msb = leftmost pixel of the trimmed glyph. */
static uint8_t gRows[ROMFONT_GLYPHS][ROMFONT_H];
static uint8_t gWidth[ROMFONT_GLYPHS];
static bool gReady;

bool romfont_load(const uint8_t* rom, size_t romLen) {
  gReady = false;
  memset(gRows, 0, sizeof(gRows));
  memset(gWidth, 0, sizeof(gWidth));
  if(rom == NULL || romLen < ROMFONT_OFF + (size_t) ROMFONT_GLYPHS * 16u) return false;
  for(int gi = 0; gi < ROMFONT_GLYPHS; gi++) {
    const uint8_t* p = rom + ROMFONT_OFF + (size_t) gi * 16u;
    uint8_t raw[ROMFONT_H];
    int lo = 8, hi = -1;
    for(int r = 0; r < ROMFONT_H; r++) {
      /* 2bpp planes 0 and 1 interleaved; plane 1 is empty in every glyph, so the
       * two are ORed rather than combined into a colour index */
      raw[r] = (uint8_t) (p[2 * r] | p[2 * r + 1]);
      for(int x = 0; x < 8; x++) {
        if(((raw[r] >> (7 - x)) & 1) == 0) continue;
        if(x < lo) lo = x;
        if(x > hi) hi = x;
      }
    }
    if(hi < lo) { gWidth[gi] = 0; continue; }        /* space and friends */
    gWidth[gi] = (uint8_t) (hi - lo + 1);
    for(int r = 0; r < ROMFONT_H; r++) gRows[gi][r] = (uint8_t) (raw[r] << lo);
  }
  gReady = true;
  return true;
}

bool romfont_ready(void) { return gReady; }

static int glyph_index(unsigned ch) {
  if(!gReady || ch < 0x20u || ch >= 0x20u + ROMFONT_GLYPHS) return -1;
  return (int) (ch - 0x20u);
}

bool romfont_pixel(unsigned ch, int x, int y) {
  int gi = glyph_index(ch);
  if(gi < 0 || x < 0 || y < 0 || y >= ROMFONT_H || x >= 8) return false;
  return ((gRows[gi][y] >> (7 - x)) & 1) != 0;
}

int romfont_glyph_w(unsigned ch) {
  int gi = glyph_index(ch);
  return gi < 0 ? 0 : gWidth[gi];
}

int romfont_advance(unsigned ch) {
  int w = romfont_glyph_w(ch);
  return w == 0 ? ROMFONT_SPACE : w + 1;
}

int romfont_text_w(const char* s) {
  int w = 0;
  for(; s != NULL && *s != 0; s++) w += romfont_advance((unsigned char) *s);
  return w;
}
