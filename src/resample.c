/*
 * Band-limited resampler: 32-tap windowed sinc (Kaiser, beta 8), 512 phases
 * with linear interpolation between them. Replaces linear interpolation for
 * the YM2612 rate (53,267 Hz) -> 48,000 Hz, which let everything between 24
 * and 29 kHz fold back into the audible range as harsh, out-of-tune tones.
 */
#include <math.h>
#include <string.h>
#include "resample.h"

#define TAPS 32
#define PH 512
static float K[PH + 1][TAPS];
static double K_ratio;

static double bessel_i0(double x)
{
    double s = 1, t = 1;
    for (int k = 1; k < 30; k++) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; }
    return s;
}

static void make_kernel(double ratio)          /* ratio = out_rate / in_rate */
{
    double fc = 0.5 * (ratio < 1 ? ratio : 1) * 0.94;   /* cutoff in cycles per input sample */
    const double beta = 8.0, i0b = bessel_i0(beta);
    for (int p = 0; p <= PH; p++) {
        double fr = (double)p / PH, sum = 0;
        for (int j = 0; j < TAPS; j++) {
            double d = (j - TAPS / 2 + 1) - fr;           /* tap distance from the output time */
            double x = 2 * fc * d;
            double sinc = fabs(x) < 1e-9 ? 1 : sin(M_PI * x) / (M_PI * x);
            double w = d / (TAPS / 2.0);
            double win = fabs(w) >= 1 ? 0 : bessel_i0(beta * sqrt(1 - w * w)) / i0b;
            K[p][j] = (float)(2 * fc * sinc * win);
            sum += K[p][j];
        }
        for (int j = 0; j < TAPS; j++) K[p][j] = (float)(K[p][j] / sum);   /* unity gain at DC */
    }
    K_ratio = ratio;
}

void rs_init(resampler_t *r, double in_rate, double out_rate)
{
    memset(r, 0, sizeof *r);
    r->step = in_rate / out_rate;
    r->t = -TAPS;                                         /* start after the history has filled */
    if (K_ratio != out_rate / in_rate) make_kernel(out_rate / in_rate);
}

int rs_push(resampler_t *r, float l, float rr, float *out)
{
    r->xl[r->n & 63] = l; r->xr[r->n & 63] = rr;
    r->n++;
    r->t -= 1;                                            /* the newest sample is at time 0 */
    int m = 0;
    /* an output at time t needs inputs up to t + TAPS/2 */
    while (r->t + TAPS / 2 <= 0 && m < 2) {
        double base = floor(r->t), fr = r->t - base;
        double pf = fr * PH; int p = (int)pf; float a = (float)(pf - p);
        const float *k0 = K[p], *k1 = K[p + 1];
        /* input index of tap 0: base - TAPS/2 + 1, relative to the newest sample (index n - 1) */
        unsigned i0 = (unsigned)((long long)r->n - 1 + (long long)base - TAPS / 2 + 1);
        float sl = 0, sr = 0;
        for (int j = 0; j < TAPS; j++) {
            float k = k0[j] + (k1[j] - k0[j]) * a;
            unsigned i = (i0 + (unsigned)j) & 63;
            sl += r->xl[i] * k; sr += r->xr[i] * k;
        }
        out[m * 2] = sl; out[m * 2 + 1] = sr;
        m++;
        r->t += r->step;
    }
    return m;
}
