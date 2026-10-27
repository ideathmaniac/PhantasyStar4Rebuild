/* Music files (WAV or Ogg Vorbis) played by the front end, for now the
 * launcher's music: decoded whole into memory, then mixed in a loop at the
 * output rate, with fade-in and fade-out. Ogg Vorbis by stb_vorbis
 * (third_party/stb, public domain). */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "music.h"

#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "../third_party/stb/stb_vorbis.c"

static int16_t *pcm;        /* interleaved stereo */
static long nframes, rate;
static double pos;          /* in source frames */
static double gain, target;
static int playing;

static int rd16(const uint8_t *p) { return p[0] | p[1] << 8; }
static long rd32(const uint8_t *p) { return (long)(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24); }

/* PCM WAV, 16-bit, mono or stereo */
static int load_wav(const uint8_t *d, size_t n)
{
    if (n < 44 || memcmp(d, "RIFF", 4) || memcmp(d + 8, "WAVE", 4)) return 0;
    int ch = 0, bits = 0; long sr = 0;
    size_t p = 12;
    while (p + 8 <= n) {
        long len = rd32(d + p + 4);
        if (len < 0 || p + 8 + (size_t)len > n) len = (long)(n - p - 8);
        if (!memcmp(d + p, "fmt ", 4) && len >= 16) {
            if (rd16(d + p + 8) != 1) return 0;           /* PCM only */
            ch = rd16(d + p + 10); sr = rd32(d + p + 12); bits = rd16(d + p + 22);
        } else if (!memcmp(d + p, "data", 4)) {
            if (bits != 16 || (ch != 1 && ch != 2) || sr < 8000) return 0;
            long fr = len / (2 * ch);
            pcm = malloc((size_t)fr * 4);
            if (!pcm) return 0;
            const uint8_t *s = d + p + 8;
            for (long i = 0; i < fr; i++) {
                int16_t l = (int16_t)rd16(s + i * 2 * ch), r = ch == 2 ? (int16_t)rd16(s + i * 4 + 2) : l;
                pcm[i * 2] = l; pcm[i * 2 + 1] = r;
            }
            nframes = fr; rate = sr;
            return 1;
        }
        p += 8 + (size_t)len + (len & 1);
    }
    return 0;
}

static int load_ogg(const uint8_t *d, size_t n)
{
    int ch = 0, sr = 0; short *out = 0;
    int fr = stb_vorbis_decode_memory(d, (int)n, &ch, &sr, &out);
    if (fr <= 0 || !out || ch < 1) { free(out); return 0; }
    pcm = malloc((size_t)fr * 4);
    if (!pcm) { free(out); return 0; }
    for (int i = 0; i < fr; i++) { pcm[i * 2] = out[i * ch]; pcm[i * 2 + 1] = out[i * ch + (ch > 1 ? 1 : 0)]; }
    free(out);
    nframes = fr; rate = sr;
    return 1;
}

int music_load(const uint8_t *data, size_t n)
{
    music_free();
    if (!data || n < 16) return 0;
    if (!memcmp(data, "RIFF", 4)) return load_wav(data, n);
    if (!memcmp(data, "OggS", 4)) return load_ogg(data, n);
    return 0;
}

void music_free(void) { free(pcm); pcm = 0; nframes = 0; playing = 0; }
int music_loaded(void) { return pcm != 0; }
void music_play(int fade_ms) { if (!pcm) return; if (!playing) { pos = 0; gain = 0; } playing = 1; target = 1; (void)fade_ms; }
void music_stop(void) { target = 0; }
int music_playing(void) { return playing; }

/* frames of stereo sound at out_rate; the volume follows target over ~0.6 s */
void music_mix(int16_t *out, int frames, int out_rate, int volume)
{
    if (!pcm || !playing) { memset(out, 0, (size_t)frames * 4); return; }
    double step = (double)rate / out_rate, fade = 1.0 / (0.6 * out_rate);
    for (int i = 0; i < frames; i++) {
        if (gain < target) { gain += fade; if (gain > target) gain = target; }
        else if (gain > target) { gain -= fade; if (gain < target) gain = target; }
        long a = (long)pos, b = a + 1 < nframes ? a + 1 : 0;
        double f = pos - a;
        double g = gain * volume / 100.0;
        double l = (pcm[a * 2] * (1 - f) + pcm[b * 2] * f) * g, r = (pcm[a * 2 + 1] * (1 - f) + pcm[b * 2 + 1] * f) * g;
        out[i * 2] = (int16_t)(l > 32767 ? 32767 : l < -32768 ? -32768 : l);
        out[i * 2 + 1] = (int16_t)(r > 32767 ? 32767 : r < -32768 ? -32768 : r);
        pos += step;
        if (pos >= nframes) pos -= nframes;   /* a loop */
    }
    if (target == 0 && gain <= 0) playing = 0;
}
