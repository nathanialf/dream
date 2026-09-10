/* font5x7: the app's own 5x7 pixel font.
 *
 * Written for this program (no ROM data, no imported font file): the gallery draws it
 * into the 256x224 framebuffer and the menu bar draws it through SDL. Glyphs cover
 * printable ASCII 0x20-0x7E; anything else renders as a blank.
 */
#ifndef DREAM_FONT5X7_H
#define DREAM_FONT5X7_H

#include <stdbool.h>

#define FONT5X7_W       5    /* glyph cell, pixels */
#define FONT5X7_H       7
#define FONT5X7_ADVANCE 6    /* one blank column between glyphs */
#define FONT5X7_LINE    8    /* one blank row between lines */

/* True when the glyph for `ch` has a pixel at (x, y), 0 <= x < 5, 0 <= y < 7. */
bool font5x7_pixel(char ch, int x, int y);

#endif
