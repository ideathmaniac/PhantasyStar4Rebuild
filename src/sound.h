/* Sound: Z80 + YM2612 + SN76489 PSG, mixed to 48 kHz stereo. */
#ifndef PS4_SOUND_H
#define PS4_SOUND_H
#include <stdint.h>
#include <stddef.h>

#define SND_RATE 48000

void ym_reset(void);
void ym_write(int a, uint8_t v);
uint8_t ym_status(void);
void ym_clock_sample(int16_t *l, int16_t *r);
size_t ym_state_size(void);
void ym_state_save(uint8_t *out);
void ym_state_load(const uint8_t *in);

void psg_reset(void);
void psg_write(uint8_t v);
int16_t psg_sample(void);           /* one sample at the YM rate */
size_t psg_state_size(void);
void psg_state_save(uint8_t *out);
void psg_state_load(const uint8_t *in);

void snd_reset(void);
void snd_run_line(void);            /* Z80 + chips for one video line */
void snd_frame_start(void);
/* Host side: take mixed 48 kHz stereo samples (interleaved L R). */
int  snd_take(int16_t *out, int max_frames);
size_t snd_state_size(void);
void snd_state_save(uint8_t *out);
void snd_state_load(const uint8_t *in);
extern int g_snd_enabled;
extern int g_snd_hold;   /* skip the Z80 and the sound chips: music keeps its own tempo during speed-ups */
extern int g_snd_mask;              /* debug: bit0 mute FM, bit1 mute DAC, bit2 mute PSG */           /* 0: skip sound generation (lockstep tests) */

void snd_set_tone(int level);   /* 0 clean, 1 softer, 2 muffled (old TV) */
#endif
