#ifndef DREAM_XXH64_H
#define DREAM_XXH64_H
#include <stdint.h>
#include <stddef.h>
uint64_t xxh64(const void* data, size_t len, uint64_t seed);
#endif
