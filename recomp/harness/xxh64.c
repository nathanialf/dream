/* XXH64 (Yann Collet's xxHash, 64-bit variant), public-domain-equivalent
 * reimplementation from the published specification. Used for the per-frame
 * region digests: fast enough to hash 128 KB of WRAM + 64 KB of VRAM every frame
 * without dominating the run. */
#include "xxh64.h"

static const uint64_t P1 = 11400714785074694791ULL;
static const uint64_t P2 = 14029467366897019727ULL;
static const uint64_t P3 =  1609587929392839161ULL;
static const uint64_t P4 =  9650029242287828579ULL;
static const uint64_t P5 =  2870177450012600261ULL;

static uint64_t rol64(uint64_t v, int r) { return (v << r) | (v >> (64 - r)); }

static uint64_t rd64le(const uint8_t* p) {
  return (uint64_t) p[0] | ((uint64_t) p[1] << 8) | ((uint64_t) p[2] << 16) |
         ((uint64_t) p[3] << 24) | ((uint64_t) p[4] << 32) | ((uint64_t) p[5] << 40) |
         ((uint64_t) p[6] << 48) | ((uint64_t) p[7] << 56);
}

static uint32_t rd32le(const uint8_t* p) {
  return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}

static uint64_t round64(uint64_t acc, uint64_t input) {
  acc += input * P2;
  acc = rol64(acc, 31);
  acc *= P1;
  return acc;
}

static uint64_t merge(uint64_t acc, uint64_t val) {
  acc ^= round64(0, val);
  acc = acc * P1 + P4;
  return acc;
}

uint64_t xxh64(const void* data, size_t len, uint64_t seed) {
  const uint8_t* p = (const uint8_t*) data;
  const uint8_t* end = p + len;
  uint64_t h;

  if(len >= 32) {
    const uint8_t* limit = end - 32;
    uint64_t v1 = seed + P1 + P2, v2 = seed + P2, v3 = seed, v4 = seed - P1;
    do {
      v1 = round64(v1, rd64le(p)); p += 8;
      v2 = round64(v2, rd64le(p)); p += 8;
      v3 = round64(v3, rd64le(p)); p += 8;
      v4 = round64(v4, rd64le(p)); p += 8;
    } while(p <= limit);
    h = rol64(v1, 1) + rol64(v2, 7) + rol64(v3, 12) + rol64(v4, 18);
    h = merge(h, v1);
    h = merge(h, v2);
    h = merge(h, v3);
    h = merge(h, v4);
  } else {
    h = seed + P5;
  }
  h += (uint64_t) len;

  while(p + 8 <= end) {
    h ^= round64(0, rd64le(p));
    h = rol64(h, 27) * P1 + P4;
    p += 8;
  }
  if(p + 4 <= end) {
    h ^= (uint64_t) rd32le(p) * P1;
    h = rol64(h, 23) * P2 + P3;
    p += 4;
  }
  while(p < end) {
    h ^= (uint64_t) (*p) * P5;
    h = rol64(h, 11) * P1;
    p++;
  }
  h ^= h >> 33;
  h *= P2;
  h ^= h >> 29;
  h *= P3;
  h ^= h >> 32;
  return h;
}
