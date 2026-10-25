/* Band-limited resampler (windowed sinc, polyphase), stereo. */
#ifndef PS4_RESAMPLE_H
#define PS4_RESAMPLE_H
#include <stdint.h>

typedef struct {
    float xl[64], xr[64];        /* the last 64 input samples (ring) */
    unsigned n;                  /* input samples so far (ring position) */
    double t;                    /* next output time, in input samples relative to the newest one */
    double step;                 /* input samples per output sample */
} resampler_t;

void rs_init(resampler_t *r, double in_rate, double out_rate);
/* feeds one input sample; writes 0, 1 or 2 output samples to out (L, R pairs), returns how many */
int  rs_push(resampler_t *r, float l, float rr, float *out);

#endif
