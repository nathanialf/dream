/* Minimal SHA-1, for the one thing `dream` needs it for: proving the ROM the
 * user pointed us at is the prototype this port was verified against. */
#ifndef DREAM_SHA1_H
#define DREAM_SHA1_H

#include <stddef.h>
#include <stdint.h>

/* Writes 20 bytes to out. */
void sha1(const uint8_t* data, size_t len, uint8_t out[20]);

/* Writes 41 bytes to out (40 lowercase hex digits and a NUL). */
void sha1_hex(const uint8_t* data, size_t len, char out[41]);

#endif
