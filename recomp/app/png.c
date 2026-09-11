/* png: see png.h. */
#include "png.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- the two checksums ---------------------------------------------------
 *
 * CRC-32 as the PNG specification defines it (the reflected polynomial
 * $EDB88320, initialised to all ones and complemented at the end) and Adler-32
 * as RFC 1950 defines it. The CRC table is built on first use rather than
 * written out: 256 entries of eight shifts each, once per run.
 */
static uint32_t crc_table[256];
static bool crc_ready = false;

static void crc_init(void) {
  for(uint32_t n = 0; n < 256; n++) {
    uint32_t c = n;
    for(int k = 0; k < 8; k++) c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
    crc_table[n] = c;
  }
  crc_ready = true;
}

static uint32_t crc_update(uint32_t crc, const uint8_t* buf, size_t len) {
  if(!crc_ready) crc_init();
  for(size_t i = 0; i < len; i++) crc = crc_table[(crc ^ buf[i]) & 0xFFu] ^ (crc >> 8);
  return crc;
}

static uint32_t adler32(uint32_t adler, const uint8_t* buf, size_t len) {
  uint32_t a = adler & 0xFFFFu, b = (adler >> 16) & 0xFFFFu;
  for(size_t i = 0; i < len; i++) {
    a = (a + buf[i]) % 65521u;
    b = (b + a) % 65521u;
  }
  return (b << 16) | a;
}

/* ---- chunks -------------------------------------------------------------- */

static void put32(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t) (v >> 24);
  p[1] = (uint8_t) (v >> 16);
  p[2] = (uint8_t) (v >> 8);
  p[3] = (uint8_t) v;
}

/* length, type, data, CRC over type and data: the whole of a PNG chunk. */
static bool chunk(FILE* f, const char* type, const uint8_t* data, size_t len) {
  uint8_t head[8];
  put32(head, (uint32_t) len);
  memcpy(head + 4, type, 4);
  if(fwrite(head, 1, 8, f) != 8) return false;
  if(len > 0 && fwrite(data, 1, len, f) != len) return false;
  uint32_t crc = crc_update(0xFFFFFFFFu, head + 4, 4);
  crc = crc_update(crc, data, len) ^ 0xFFFFFFFFu;
  uint8_t tail[4];
  put32(tail, crc);
  return fwrite(tail, 1, 4, f) == 4;
}

/* A stored deflate block carries at most 65535 bytes, so the raw image is cut
 * into blocks of that size: a five-byte header each (the final-block flag and
 * compression type 00 in the first, then LEN and its ones-complement, both
 * little endian), and the bytes themselves. */
#define STORED_MAX 65535u

bool png_write_rgbx(const char* path, const uint32_t* px, int w, int h) {
  if(path == NULL || px == NULL || w <= 0 || h <= 0) return false;

  /* the raw image: one filter byte (0, "none") in front of each row of RGB */
  size_t stride = (size_t) w * 3u + 1u;
  size_t rawLen = stride * (size_t) h;
  uint8_t* raw = malloc(rawLen);
  if(raw == NULL) return false;
  for(int y = 0; y < h; y++) {
    uint8_t* row = raw + (size_t) y * stride;
    *row++ = 0;
    for(int x = 0; x < w; x++) {
      uint32_t p = px[(size_t) y * (size_t) w + (size_t) x];
      *row++ = (uint8_t) (p >> 24);
      *row++ = (uint8_t) (p >> 16);
      *row++ = (uint8_t) (p >> 8);
    }
  }

  /* the zlib stream: a two-byte header (deflate, 32 KB window, no dictionary,
   * and a check byte that makes the pair a multiple of 31), the stored blocks,
   * and the Adler-32 of the raw image */
  size_t blocks = (rawLen + STORED_MAX - 1u) / STORED_MAX;
  if(blocks == 0) blocks = 1;
  size_t zLen = 2u + blocks * 5u + rawLen + 4u;
  uint8_t* z = malloc(zLen);
  if(z == NULL) { free(raw); return false; }
  size_t zi = 0;
  z[zi++] = 0x78;
  z[zi++] = 0x01;
  size_t left = rawLen, off = 0;
  do {
    size_t n = left > STORED_MAX ? STORED_MAX : left;
    z[zi++] = (uint8_t) ((n == left) ? 1 : 0);       /* final block */
    z[zi++] = (uint8_t) (n & 0xFFu);
    z[zi++] = (uint8_t) (n >> 8);
    z[zi++] = (uint8_t) (~n & 0xFFu);
    z[zi++] = (uint8_t) ((~n >> 8) & 0xFFu);
    memcpy(z + zi, raw + off, n);
    zi += n;
    off += n;
    left -= n;
  } while(left > 0);
  put32(z + zi, adler32(1u, raw, rawLen));
  zi += 4;

  FILE* f = fopen(path, "wb");
  if(f == NULL) { free(raw); free(z); return false; }
  static const uint8_t kSig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
  uint8_t ihdr[13];
  put32(ihdr, (uint32_t) w);
  put32(ihdr + 4, (uint32_t) h);
  ihdr[8] = 8;     /* bits per sample */
  ihdr[9] = 2;     /* colour type 2: RGB */
  ihdr[10] = 0;    /* deflate */
  ihdr[11] = 0;    /* the only filter method */
  ihdr[12] = 0;    /* not interlaced */
  bool ok = fwrite(kSig, 1, 8, f) == 8;
  ok = ok && chunk(f, "IHDR", ihdr, sizeof(ihdr));
  ok = ok && chunk(f, "IDAT", z, zi);
  ok = ok && chunk(f, "IEND", NULL, 0);
  if(fclose(f) != 0) ok = false;
  free(raw);
  free(z);
  if(!ok) remove(path);
  return ok;
}
