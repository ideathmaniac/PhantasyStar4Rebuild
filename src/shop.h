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
    int id, type, atk, def, price, owned;   /* price -1: not in a shop (inventory) */
    unsigned money;
    char name[48], kind[40];
    char use[120];      /* what a consumable does ("" for equipment) */
    int nrow;
    shop_row_t row[5];
    int battle;         /* from the battle item list */
} shop_t;

/* 1 and *s filled while a shop's buy list is open (known ROM only) */
int shop_current(shop_t *s);
/* 1 and *s filled (price -1) while the field menu's inventory list is open */
int shop_inventory(shop_t *s);
/* 1 and *s filled (no comparison rows) while the battle item list is open */
int shop_battle_item(shop_t *s);
/* any item by id (comparison rows for the current party) */
int shop_item(int id, shop_t *s);

/* the item ids of a shop (the game's shop number), 0 if unknown */
int shop_list_items(int shop, int *ids, int max);
/* the shop whose buy list is open now, -1 if none */
int shop_open_id(void);
void shop_item_name(int id, char *out, int n);

#endif
