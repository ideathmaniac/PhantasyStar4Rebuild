#ifndef PS4_HDSCALE_H
#define PS4_HDSCALE_H
#include <stdint.h>
/* 4x edge-directed pixel-art upscale: dst must hold (4w x 4h) pixels, rows dpitch apart */
void hd_scale4(const uint32_t *src, int w, int h, int pitch, uint32_t *dst, int dpitch);
/* the same in two steps, so bands of rows can run on several threads:
 * hd_prepare once per frame, then hd_rows for disjoint [y0, y1) ranges */
void hd_prepare(const uint32_t *src, int w, int h, int pitch);
void hd_rows(const uint32_t *src, int w, int h, int pitch, uint32_t *dst, int dpitch, int y0, int y1);
#endif
