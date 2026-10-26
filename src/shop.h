/* Shop helper for Phantasy Star IV (see shop.c). Strings are UTF-8. */
#ifndef PS4_SHOP_H
#define PS4_SHOP_H

typedef struct {
    char who[24];       /* character name */
    int can;            /* can wear the item */
    int same;           /* already wears this very item */
    char now[100];      /* what it would replace ("ничего" if the slot is empty) */
    int datk, ddef;     /* change of attack / defence */
} shop_row_t;

typedef struct shop_s {
    int id, type, atk, def, price, owned;
    unsigned money;
    char name[48], kind[40];
    int nrow;
    shop_row_t row[5];
} shop_t;

/* 1 and *s filled while a shop's buy list is open (known ROM only) */
int shop_current(shop_t *s);

#endif
