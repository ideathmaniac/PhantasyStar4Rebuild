/* Minimal PNG reading and writing (own inflate/deflate, no libraries). */
#ifndef PS4_PNG_H
#define PS4_PNG_H
#include <stdint.h>
#include <stddef.h>

/* Decodes a PNG (any colour type, bit depth 1-16, not interlaced) into
 * malloc'ed RGBA8 (bytes R, G, B, A). Returns 0 and a reason on failure. */
uint8_t *png_decode(const uint8_t *data, size_t n, int *w, int *h, const char **err);

/* Encodes RGBA8 as a PNG into malloc'ed memory. */
uint8_t *png_encode(const uint8_t *rgba, int w, int h, size_t *out_n);

#endif
