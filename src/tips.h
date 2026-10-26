/* Technique and skill descriptions for Phantasy Star IV (see tips.c). */
#ifndef PS4_TIPS_H
#define PS4_TIPS_H

typedef struct tip_s {
    int skill;          /* 0 technique, 1 skill */
    int id;             /* 0-based within its kind */
    int cost;           /* TP, -1 for skills */
    int power;          /* 1-5 for attacks and healing, else 0 */
    char name[32];      /* UTF-8 */
    char target[40];
    char desc[160];
    char note[200];
    char combo[320];    /* combination attacks with this entry, "; " between them, or "" */
    unsigned combo_ready;   /* bit n: combination n already has its other parts ordered this round */
} tip_t;

/* 1 and *t filled while a technique or skill list is open (known ROM only) */
int tips_current(tip_t *t);

#endif
