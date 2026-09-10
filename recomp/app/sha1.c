/* SHA-1 (FIPS 180-4). Public-domain style compact implementation, written for
 * this repository; it is used only to check the ROM the user supplied. */
#include <string.h>

#include "sha1.h"

#define ROL(v, n) (((v) << (n)) | ((v) >> (32 - (n))))

static void sha1_block(uint32_t h[5], const uint8_t p[64]) {
  uint32_t w[80];
  for(int i = 0; i < 16; i++) {
    w[i] = ((uint32_t) p[i * 4] << 24) | ((uint32_t) p[i * 4 + 1] << 16) |
           ((uint32_t) p[i * 4 + 2] << 8) | (uint32_t) p[i * 4 + 3];
  }
  for(int i = 16; i < 80; i++) w[i] = ROL(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

  uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
  for(int i = 0; i < 80; i++) {
    uint32_t f, k;
    if(i < 20)      { f = (b & c) | (~b & d);          k = 0x5a827999u; }
    else if(i < 40) { f = b ^ c ^ d;                   k = 0x6ed9eba1u; }
    else if(i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8f1bbcdcu; }
    else            { f = b ^ c ^ d;                   k = 0xca62c1d6u; }
    uint32_t t = ROL(a, 5) + f + e + k + w[i];
    e = d; d = c; c = ROL(b, 30); b = a; a = t;
  }
  h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
}

void sha1(const uint8_t* data, size_t len, uint8_t out[20]) {
  uint32_t h[5] = { 0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u, 0xc3d2e1f0u };
  size_t full = len / 64;
  for(size_t i = 0; i < full; i++) sha1_block(h, data + i * 64);

  uint8_t tail[128];
  size_t rest = len - full * 64;
  memcpy(tail, data + full * 64, rest);
  tail[rest++] = 0x80;
  size_t total = rest <= 56 ? 64 : 128;
  memset(tail + rest, 0, total - rest);
  uint64_t bits = (uint64_t) len * 8u;
  for(int i = 0; i < 8; i++) tail[total - 1 - (size_t) i] = (uint8_t) (bits >> (i * 8));
  sha1_block(h, tail);
  if(total == 128) sha1_block(h, tail + 64);

  for(int i = 0; i < 5; i++) {
    out[i * 4]     = (uint8_t) (h[i] >> 24);
    out[i * 4 + 1] = (uint8_t) (h[i] >> 16);
    out[i * 4 + 2] = (uint8_t) (h[i] >> 8);
    out[i * 4 + 3] = (uint8_t) h[i];
  }
}

void sha1_hex(const uint8_t* data, size_t len, char out[41]) {
  static const char kHex[] = "0123456789abcdef";
  uint8_t d[20];
  sha1(data, len, d);
  for(int i = 0; i < 20; i++) {
    out[i * 2]     = kHex[d[i] >> 4];
    out[i * 2 + 1] = kHex[d[i] & 0xf];
  }
  out[40] = 0;
}
