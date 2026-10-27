/* Bestiary for Phantasy Star IV (see bestiary.c). */
#ifndef PS4_BESTIARY_H
#define PS4_BESTIARY_H
#include <stdint.h>

#define BEST_MAX 200   /* monster ids looked at */

typedef struct {
    char name[48];     /* UTF-8, "Ксенофаг" */
    int hp, attack, defence, strength, mental, agility, dexterity, exp, meseta;
    uint32_t seen, killed;
    char weak[256], resist[384];   /* elements it is weak to / resists ("" if none) */
    int machine;                  /* a machine (ЭМИ and Помехи work on it) */
} best_info_t;

void bestiary_after_frame(void);                    /* after every emulated frame */
int  bestiary_info(int id, best_info_t *b);         /* 0 if id is not a monster */
void bestiary_get(int id, uint32_t *seen, uint32_t *killed);
/* this battle's monsters: id, HP now and at most (HP below 1 = defeated); 0 outside battles */
int  bestiary_battle(int *ids, int *hp, int *hpmax, int max);
void bestiary_set(int id, uint32_t seen, uint32_t killed);

#endif
