/* writes resampled sine tones (float32 L only) for checking aliasing: rs_test f_hz out.raw [linear] */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "../src/resample.h"
int main(int c, char **v)
{
    double f = atof(v[1]); int lin = c > 3;
    resampler_t r; rs_init(&r, 53267.0, 48000.0);
    FILE *o = fopen(v[2], "wb");
    double lp = 0, lastx = 0, pos = 0, step = 53267.0 / 48000.0;
    for (int i = 0; i < 53267; i++) {
        double x = 0.5 * sin(2 * M_PI * f * i / 53267.0);
        if (lin) { while (pos < 1) { float y = (float)(lastx + (x - lastx) * pos); fwrite(&y, 4, 1, o); pos += step; } pos -= 1; lastx = x; }
        else { float out[4]; int m = rs_push(&r, (float)x, 0, out); for (int k = 0; k < m; k++) fwrite(&out[k * 2], 4, 1, o); }
    }
    fclose(o); (void)lp;
    return 0;
}
