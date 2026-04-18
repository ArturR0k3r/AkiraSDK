/*
 * assets_items.h — Item icon sprites (8×8 pixels, RGB565)
 */
#ifndef ASSETS_ITEMS_H
#define ASSETS_ITEMS_H

#include "assets_palette.h"

/* ITEM_NONE (0) — empty/blank */
static const uint16_t item_none[64] = {
    TR, TR, TR, TR, TR, TR, TR, TR, TR, TR, TR, TR, TR, TR, TR, TR,
    TR, TR, TR, TR, TR, TR, TR, TR, TR, TR, TR, TR, TR, TR, TR, TR,
    TR, TR, TR, TR, TR, TR, TR, TR, TR, TR, TR, TR, TR, TR, TR, TR,
    TR, TR, TR, TR, TR, TR, TR, TR, TR, TR, TR, TR, TR, TR, TR, TR,
};

/* ITEM_HOUSE_KEY (1) — golden key */
static const uint16_t item_house_key[64] = {
    TR, TR, D0, D0, D0, TR, TR, TR,
    TR, D0, D1, D1, D1, D0, TR, TR,
    TR, D0, D1, TR, D1, D0, TR, TR,
    TR, TR, D0, D0, D0, TR, TR, TR,
    TR, TR, TR, D0, TR, TR, TR, TR,
    TR, TR, TR, D0, TR, TR, TR, TR,
    TR, TR, TR, D0, D0, TR, TR, TR,
    TR, TR, TR, D0, D0, TR, TR, TR,
};

/* ITEM_DIARY (2) — old book */
static const uint16_t item_diary[64] = {
    TR, B0, B1, B1, B1, B1, B0, TR,
    B0, B1, B2, B2, B2, B2, B1, B0,
    B0, B2, S2, S2, S2, S2, B2, B0,
    B0, B2, S2, BK, BK, S2, B2, B0,
    B0, B2, S2, BK, BK, S2, B2, B0,
    B0, B2, S2, S2, S2, S2, B2, B0,
    B0, B1, B2, B2, B2, B2, B1, B0,
    TR, B0, B1, B1, B1, B1, B0, TR,
};

/* ITEM_LANTERN (3) — glowing lantern */
static const uint16_t item_lantern[64] = {
    TR, TR, TR, D0, D0, TR, TR, TR,
    TR, TR, D0, D1, D1, D0, TR, TR,
    TR, A1, F1, F1, F1, F1, A1, TR,
    TR, A1, F0, F1, F1, F0, A1, TR,
    TR, A1, F1, F1, F1, F1, A1, TR,
    TR, A1, A2, A2, A2, A2, A1, TR,
    TR, TR, A1, A2, A2, A1, TR, TR,
    TR, TR, TR, A1, A1, TR, TR, TR,
};

/* ITEM_COMPASS (4) — magic compass */
static const uint16_t item_compass[64] = {
    TR, TR, A2, A2, A2, A2, TR, TR,
    TR, A2, A3, A3, A3, A3, A2, TR,
    A2, A3, A3, E2, A3, A3, A3, A2,
    A2, A3, A3, E2, A3, A3, A3, A2,
    A2, A3, A3, A3, A3, A3, A3, A2,
    A2, A3, A3, A3, L2, A3, A3, A2,
    TR, A2, A3, A3, A3, A3, A2, TR,
    TR, TR, A2, A2, A2, A2, TR, TR,
};

/* ITEM_MANSION_KEY (5) — ornate key */
static const uint16_t item_mansion_key[64] = {
    TR, TR, A2, A2, A3, TR, TR, TR,
    TR, A2, A3, A3, A3, A2, TR, TR,
    TR, A2, A3, TR, A3, A2, TR, TR,
    TR, TR, A2, A2, A2, TR, TR, TR,
    TR, TR, TR, A2, TR, TR, TR, TR,
    TR, TR, TR, A2, A3, TR, TR, TR,
    TR, TR, TR, A2, TR, TR, TR, TR,
    TR, TR, TR, A2, A3, TR, TR, TR,
};

/* ITEM_LAB_KEYCARD (6) — electronic keycard */
static const uint16_t item_lab_keycard[64] = {
    TR, A3, A3, A3, A3, A3, A3, TR,
    A3, A4, A4, A4, A4, A4, A4, A3,
    A3, A4, L2, L2, A4, A4, A4, A3,
    A3, A4, L2, L2, A4, A4, A4, A3,
    A3, A4, A4, A4, A4, A4, A4, A3,
    A3, A4, BK, BK, BK, BK, A4, A3,
    A3, A4, A4, A4, A4, A4, A4, A3,
    TR, A3, A3, A3, A3, A3, A3, TR,
};

/* ITEM_CRYSTAL_1/2/3 (7,8,9) — anomaly crystal shards */
static const uint16_t item_crystal[64] = {
    TR, TR, TR, TR, C1, TR, TR, TR,
    TR, TR, TR, C0, C1, C0, TR, TR,
    TR, TR, C0, C1, WH, C1, TR, TR,
    TR, TR, C0, C1, C1, C0, TR, TR,
    TR, TR, C0, C1, C1, C0, TR, TR,
    TR, TR, TR, C0, C1, TR, TR, TR,
    TR, TR, TR, C0, C0, TR, TR, TR,
    TR, TR, TR, TR, TR, TR, TR, TR,
};

/* ITEM_TACKLE_BOX (10) — fishing tackle box */
static const uint16_t item_tackle_box[64] = {
    TR, A2, A2, A2, A2, A2, A2, TR,
    A2, A3, A3, D0, D0, A3, A3, A2,
    A2, G2, G2, G2, G2, G2, G2, A2,
    A2, G2, G3, G3, G3, G3, G2, A2,
    A2, G2, G3, G3, G3, G3, G2, A2,
    A2, G2, G2, G2, G2, G2, G2, A2,
    A2, A3, A3, A3, A3, A3, A3, A2,
    TR, A2, A2, A2, A2, A2, A2, TR,
};

/* ITEM_ANOMALY_SAMPLE (11) — glowing vial */
static const uint16_t item_anomaly_sample[64] = {
    TR, TR, TR, A3, A3, TR, TR, TR,
    TR, TR, TR, A3, A3, TR, TR, TR,
    TR, TR, A3, A4, A4, A3, TR, TR,
    TR, TR, A3, P1, P2, A3, TR, TR,
    TR, TR, A3, P2, P1, A3, TR, TR,
    TR, TR, A3, P1, P2, A3, TR, TR,
    TR, TR, A3, A3, A3, A3, TR, TR,
    TR, TR, TR, A3, A3, TR, TR, TR,
};

/* ITEM_MAP_FRAGMENT (12) — torn map piece */
static const uint16_t item_map_fragment[64] = {
    TR, TR, S1, S2, S2, S1, TR, TR,
    TR, S1, S2, B1, S2, S2, S1, TR,
    S1, S2, S2, S2, B1, S2, S2, S1,
    S1, S2, B1, S2, S2, S2, S2, TR,
    S1, S2, S2, S2, S2, B1, TR, TR,
    TR, S1, S2, B1, S2, S1, TR, TR,
    TR, TR, S1, S2, S1, TR, TR, TR,
    TR, TR, TR, S1, TR, TR, TR, TR,
};

/* ITEM_DIARY_PAGE (13) — single diary page */
static const uint16_t item_diary_page[64] = {
    TR, S2, S2, S2, S2, S2, S2, TR,
    S2, WH, WH, WH, WH, WH, WH, S2,
    S2, WH, BK, BK, BK, BK, WH, S2,
    S2, WH, WH, WH, WH, WH, WH, S2,
    S2, WH, BK, BK, BK, WH, WH, S2,
    S2, WH, WH, WH, WH, WH, WH, S2,
    S2, WH, BK, BK, WH, WH, WH, S2,
    TR, S2, S2, S2, S2, S2, S2, TR,
};

/* Item sprite lookup: item_sprites[item_id] */
static const uint16_t * const item_sprites[14] = {
    item_none,           /* 0: ITEM_NONE */
    item_house_key,      /* 1: ITEM_HOUSE_KEY */
    item_diary,          /* 2: ITEM_DIARY */
    item_lantern,        /* 3: ITEM_LANTERN */
    item_compass,        /* 4: ITEM_COMPASS */
    item_mansion_key,    /* 5: ITEM_MANSION_KEY */
    item_lab_keycard,    /* 6: ITEM_LAB_KEYCARD */
    item_crystal,        /* 7: ITEM_CRYSTAL_1 */
    item_crystal,        /* 8: ITEM_CRYSTAL_2 (same sprite) */
    item_crystal,        /* 9: ITEM_CRYSTAL_3 (same sprite) */
    item_tackle_box,     /* 10: ITEM_TACKLE_BOX */
    item_anomaly_sample, /* 11: ITEM_ANOMALY_SAMPLE */
    item_map_fragment,   /* 12: ITEM_MAP_FRAGMENT */
    item_diary_page,     /* 13: ITEM_DIARY_PAGE */
};

#endif /* ASSETS_ITEMS_H */
