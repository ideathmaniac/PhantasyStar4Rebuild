/* Phantasy Star IV conveniences: encounter rate, experience and meseta multipliers. */
#ifndef PS4_QOL_H
#define PS4_QOL_H
/* encounters: 0 normal, 1 half as often, 2 none (story battles still happen) */
void qol_set(int encounters, int exp_mult, int meseta_mult);
void qol_after_frame(void);   /* after every emulated frame, known ROM only */
void qol_resync(void);        /* after loading a state */
#endif
