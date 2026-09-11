/* png: the smallest PNG writer that is still a PNG.
 *
 * The app has no zlib and does not want one: a screenshot is written once, by
 * hand, at a size the user chose to look at. A PNG's image data is a zlib
 * stream, and a zlib stream is allowed to be nothing but stored (uncompressed)
 * deflate blocks, so the encoder is a header, the rows with their filter byte,
 * and two checksums this file computes itself (CRC-32 for the chunks, Adler-32
 * for the zlib stream). The file is about a third larger than a compressed one
 * and every viewer reads it.
 */
#ifndef PNG_H
#define PNG_H

#include <stdbool.h>
#include <stdint.h>

/* `px` is w*h pixels as the app's own framebuffer holds them, 0xRRGGBBXX: the
 * low byte is ignored and the PNG is 8-bit RGB. Returns false if the file
 * cannot be written. */
bool png_write_rgbx(const char* path, const uint32_t* px, int w, int h);

#endif
