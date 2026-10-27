#ifndef PS4_MUSIC_H
#define PS4_MUSIC_H
#include <stdint.h>
#include <stddef.h>
int  music_load(const uint8_t *data, size_t n);   /* WAV (16-bit PCM) or Ogg Vorbis; 0 if not understood */
void music_free(void);
int  music_loaded(void);
void music_play(int fade_ms);
void music_stop(void);                            /* fades out */
int  music_playing(void);
void music_mix(int16_t *out, int frames, int out_rate, int volume);   /* volume 0..100 */
#endif
