/* romfont: the ROM's own font, decoded from the user's image at run time.
 *
 * The image carries exactly one font: 96 2bpp glyphs at file offset 014FE0,
 * ASCII $20-$7F, 8x8, with plane 1 empty so it is a 1bpp font wearing a 2bpp
 * layout (docs/data_formats.md). No code in the ROM ever uploads it, which is
 * why the gallery has a page for it; it is also, being the only lettering the
 * game owns, what the app's own UI is drawn with. There is no second font: the
 * app draws no text at all before the ROM is loaded and verified, and refuses to
 * start without one.
 *
 * Spacing is proportional rather than the 8-pixel cell: the glyphs are drawn
 * inside the cell with their own left and right margins, so the advance is the
 * inked width plus one column. A cell-width bar would be a third wider than the
 * layout it replaced and would not fit the 256-pixel framebuffer the gallery
 * draws into.
 */
#ifndef DREAM_ROMFONT_H
#define DREAM_ROMFONT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ROMFONT_OFF    0x014FE0u  /* the 2bpp font, 96 glyphs of 16 bytes */
#define ROMFONT_GLYPHS 96
#define ROMFONT_H      8          /* rows in a glyph */
#define ROMFONT_LINE   9          /* one blank row between lines */
#define ROMFONT_SPACE  4          /* the advance of a glyph with no ink */

/* Decode the font out of `rom`. Safe to call more than once; false means the
 * image was too short, and every call below then draws nothing. */
bool romfont_load(const uint8_t* rom, size_t romLen);
bool romfont_ready(void);

/* True when the glyph for `ch` has a pixel at (x, y) after its left margin is
 * trimmed, 0 <= x < romfont_glyph_w(ch), 0 <= y < ROMFONT_H. */
bool romfont_pixel(unsigned ch, int x, int y);

/* Inked width of a glyph (0 for a blank one) and the advance to the next. */
int romfont_glyph_w(unsigned ch);
int romfont_advance(unsigned ch);
int romfont_text_w(const char* s);

#endif
