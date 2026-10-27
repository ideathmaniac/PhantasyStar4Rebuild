#ifndef PS4_NEMESIS_H
#define PS4_NEMESIS_H
#include <stdint.h>
#include <stddef.h>
/* bytes written to out (0 on bad data or if out is too small); *used: input bytes read */
size_t nemesis_decode(const uint8_t *src, size_t srclen, uint8_t *out, size_t outmax, size_t *used);
#endif
