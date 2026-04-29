/*
 * assets_player.h — Player character sprites (16×16 pixels, 4 dirs × 2 frames)
 * Brown hair, blue shirt, tan pants. Magenta (TR) = transparent.
 */
#ifndef ASSETS_PLAYER_H
#define ASSETS_PLAYER_H

#include "assets_palette.h"

/* Index: [direction * 2 + frame] where direction = DIR_DOWN..DIR_RIGHT */

/* Player facing DOWN, frame 0 (standing) */
static const uint16_t spr_player_d0[256] = {
    TR, TR, TR, TR, B1, B1, B1, B1, B1, B1, B1, B1, TR, TR, TR, TR,
    TR, TR, TR, B1, B0, B0, B0, B0, B0, B0, B0, B0, B1, TR, TR, TR,
    TR, TR, B1, B0, B1, B2, B2, B2, B2, B2, B2, B1, B0, B1, TR, TR,
    TR, TR, B1, SK, SK, SK, SK, SK, SK, SK, SK, SK, SK, B1, TR, TR,
    TR, TR, B1, SK, BK, SK, SK, SK, SK, SK, BK, SK, SK, B1, TR, TR,
    TR, TR, B1, SK, SK, SK, SK, SK, SK, SK, SK, SK, SK, B1, TR, TR,
    TR, TR, TR, B1, SK, SK, SK, E1, SK, SK, SK, SK, B1, TR, TR, TR,
    TR, TR, TR, TR, B1, SK, SK, SK, SK, SK, B1, TR, TR, TR, TR, TR,
    TR, TR, TR, L1, L1, L2, L2, L2, L2, L2, L1, L1, TR, TR, TR, TR,
    TR, TR, TR, L1, L2, L2, L2, L2, L2, L2, L2, L1, TR, TR, TR, TR,
    TR, TR, TR, L1, L2, L2, L2, L2, L2, L2, L2, L1, TR, TR, TR, TR,
    TR, TR, TR, L1, L1, L2, L2, L2, L2, L2, L1, L1, TR, TR, TR, TR,
    TR, TR, TR, TR, B2, B3, B3, B3, B3, B3, B2, TR, TR, TR, TR, TR,
    TR, TR, TR, TR, B2, B3, B3, TR, B3, B3, B2, TR, TR, TR, TR, TR,
    TR, TR, TR, TR, B0, B1, B1, TR, B1, B1, B0, TR, TR, TR, TR, TR,
    TR, TR, TR, TR, B0, B0, B0, TR, B0, B0, B0, TR, TR, TR, TR, TR,
};

/* Player facing DOWN, frame 1 (walking) */
static const uint16_t spr_player_d1[256] = {
    TR, TR, TR, TR, B1, B1, B1, B1, B1, B1, B1, B1, TR, TR, TR, TR,
    TR, TR, TR, B1, B0, B0, B0, B0, B0, B0, B0, B0, B1, TR, TR, TR,
    TR, TR, B1, B0, B1, B2, B2, B2, B2, B2, B2, B1, B0, B1, TR, TR,
    TR, TR, B1, SK, SK, SK, SK, SK, SK, SK, SK, SK, SK, B1, TR, TR,
    TR, TR, B1, SK, BK, SK, SK, SK, SK, SK, BK, SK, SK, B1, TR, TR,
    TR, TR, B1, SK, SK, SK, SK, SK, SK, SK, SK, SK, SK, B1, TR, TR,
    TR, TR, TR, B1, SK, SK, SK, E1, SK, SK, SK, SK, B1, TR, TR, TR,
    TR, TR, TR, TR, B1, SK, SK, SK, SK, SK, B1, TR, TR, TR, TR, TR,
    TR, TR, TR, L1, L1, L2, L2, L2, L2, L2, L1, L1, TR, TR, TR, TR,
    TR, TR, TR, L1, L2, L2, L2, L2, L2, L2, L2, L1, TR, TR, TR, TR,
    TR, TR, TR, L1, L2, L2, L2, L2, L2, L2, L2, L1, TR, TR, TR, TR,
    TR, TR, TR, L1, L1, L2, L2, L2, L2, L2, L1, L1, TR, TR, TR, TR,
    TR, TR, TR, B2, B3, B3, B3, TR, B3, B3, B3, B2, TR, TR, TR, TR,
    TR, TR, TR, B2, B3, B3, TR, TR, TR, B3, B3, B2, TR, TR, TR, TR,
    TR, TR, B0, B0, B1, TR, TR, TR, TR, TR, B1, B0, B0, TR, TR, TR,
    TR, TR, B0, B0, TR, TR, TR, TR, TR, TR, TR, B0, B0, TR, TR, TR,
};

/* Player facing UP, frame 0 */
static const uint16_t spr_player_u0[256] = {
    TR, TR, TR, TR, B1, B1, B1, B1, B1, B1, B1, B1, TR, TR, TR, TR,
    TR, TR, TR, B1, B0, B0, B0, B0, B0, B0, B0, B0, B1, TR, TR, TR,
    TR, TR, B1, B0, B1, B2, B2, B2, B2, B2, B2, B1, B0, B1, TR, TR,
    TR, TR, B1, B0, B1, B1, B1, B1, B1, B1, B1, B1, B0, B1, TR, TR,
    TR, TR, B1, B0, B0, B0, B0, B0, B0, B0, B0, B0, B0, B1, TR, TR,
    TR, TR, TR, B1, B0, B0, B0, B0, B0, B0, B0, B0, B1, TR, TR, TR,
    TR, TR, TR, TR, B1, B0, B0, B0, B0, B0, B1, TR, TR, TR, TR, TR,
    TR, TR, TR, TR, B1, SK, SK, SK, SK, SK, B1, TR, TR, TR, TR, TR,
    TR, TR, TR, L1, L1, L2, L2, L2, L2, L2, L1, L1, TR, TR, TR, TR,
    TR, TR, TR, L1, L2, L2, L2, L2, L2, L2, L2, L1, TR, TR, TR, TR,
    TR, TR, TR, L1, L2, L2, L2, L2, L2, L2, L2, L1, TR, TR, TR, TR,
    TR, TR, TR, L1, L1, L2, L2, L2, L2, L2, L1, L1, TR, TR, TR, TR,
    TR, TR, TR, TR, B2, B3, B3, B3, B3, B3, B2, TR, TR, TR, TR, TR,
    TR, TR, TR, TR, B2, B3, B3, TR, B3, B3, B2, TR, TR, TR, TR, TR,
    TR, TR, TR, TR, B0, B1, B1, TR, B1, B1, B0, TR, TR, TR, TR, TR,
    TR, TR, TR, TR, B0, B0, B0, TR, B0, B0, B0, TR, TR, TR, TR, TR,
};

/* Player facing UP, frame 1 */
static const uint16_t spr_player_u1[256] = {
    TR, TR, TR, TR, B1, B1, B1, B1, B1, B1, B1, B1, TR, TR, TR, TR,
    TR, TR, TR, B1, B0, B0, B0, B0, B0, B0, B0, B0, B1, TR, TR, TR,
    TR, TR, B1, B0, B1, B2, B2, B2, B2, B2, B2, B1, B0, B1, TR, TR,
    TR, TR, B1, B0, B1, B1, B1, B1, B1, B1, B1, B1, B0, B1, TR, TR,
    TR, TR, B1, B0, B0, B0, B0, B0, B0, B0, B0, B0, B0, B1, TR, TR,
    TR, TR, TR, B1, B0, B0, B0, B0, B0, B0, B0, B0, B1, TR, TR, TR,
    TR, TR, TR, TR, B1, B0, B0, B0, B0, B0, B1, TR, TR, TR, TR, TR,
    TR, TR, TR, TR, B1, SK, SK, SK, SK, SK, B1, TR, TR, TR, TR, TR,
    TR, TR, TR, L1, L1, L2, L2, L2, L2, L2, L1, L1, TR, TR, TR, TR,
    TR, TR, TR, L1, L2, L2, L2, L2, L2, L2, L2, L1, TR, TR, TR, TR,
    TR, TR, TR, L1, L2, L2, L2, L2, L2, L2, L2, L1, TR, TR, TR, TR,
    TR, TR, TR, L1, L1, L2, L2, L2, L2, L2, L1, L1, TR, TR, TR, TR,
    TR, TR, TR, B2, B3, B3, B3, TR, B3, B3, B3, B2, TR, TR, TR, TR,
    TR, TR, TR, B2, B3, B3, TR, TR, TR, B3, B3, B2, TR, TR, TR, TR,
    TR, TR, B0, B0, B1, TR, TR, TR, TR, TR, B1, B0, B0, TR, TR, TR,
    TR, TR, B0, B0, TR, TR, TR, TR, TR, TR, TR, B0, B0, TR, TR, TR,
};

/* Player facing LEFT, frame 0 */
static const uint16_t spr_player_l0[256] = {
    TR, TR, TR, TR, B1, B1, B1, B1, B1, B1, B1, TR, TR, TR, TR, TR,
    TR, TR, TR, B1, B0, B0, B0, B0, B0, B0, B0, B1, TR, TR, TR, TR,
    TR, TR, B1, B0, B1, B2, B2, B2, B2, B2, B1, B0, B1, TR, TR, TR,
    TR, TR, B1, SK, SK, SK, SK, SK, SK, SK, SK, SK, B1, TR, TR, TR,
    TR, TR, B1, SK, BK, SK, SK, SK, SK, SK, SK, SK, B1, TR, TR, TR,
    TR, TR, B1, SK, SK, SK, SK, SK, SK, SK, SK, B1, TR, TR, TR, TR,
    TR, TR, TR, B1, SK, SK, E1, SK, SK, SK, B1, TR, TR, TR, TR, TR,
    TR, TR, TR, TR, B1, SK, SK, SK, SK, B1, TR, TR, TR, TR, TR, TR,
    TR, TR, TR, SK, L1, L2, L2, L2, L2, L1, TR, TR, TR, TR, TR, TR,
    TR, TR, TR, TR, L1, L2, L2, L2, L2, L1, TR, TR, TR, TR, TR, TR,
    TR, TR, TR, TR, L1, L2, L2, L2, L2, L1, TR, TR, TR, TR, TR, TR,
    TR, TR, TR, TR, L1, L1, L2, L2, L1, L1, TR, TR, TR, TR, TR, TR,
    TR, TR, TR, TR, B2, B3, B3, B3, B3, B2, TR, TR, TR, TR, TR, TR,
    TR, TR, TR, TR, B2, B3, B3, TR, B3, B2, TR, TR, TR, TR, TR, TR,
    TR, TR, TR, TR, B0, B1, B1, TR, B1, B0, TR, TR, TR, TR, TR, TR,
    TR, TR, TR, TR, B0, B0, TR, TR, B0, B0, TR, TR, TR, TR, TR, TR,
};

/* Player facing LEFT, frame 1 */
static const uint16_t spr_player_l1[256] = {
    TR, TR, TR, TR, B1, B1, B1, B1, B1, B1, B1, TR, TR, TR, TR, TR,
    TR, TR, TR, B1, B0, B0, B0, B0, B0, B0, B0, B1, TR, TR, TR, TR,
    TR, TR, B1, B0, B1, B2, B2, B2, B2, B2, B1, B0, B1, TR, TR, TR,
    TR, TR, B1, SK, SK, SK, SK, SK, SK, SK, SK, SK, B1, TR, TR, TR,
    TR, TR, B1, SK, BK, SK, SK, SK, SK, SK, SK, SK, B1, TR, TR, TR,
    TR, TR, B1, SK, SK, SK, SK, SK, SK, SK, SK, B1, TR, TR, TR, TR,
    TR, TR, TR, B1, SK, SK, E1, SK, SK, SK, B1, TR, TR, TR, TR, TR,
    TR, TR, TR, TR, B1, SK, SK, SK, SK, B1, TR, TR, TR, TR, TR, TR,
    TR, TR, TR, SK, L1, L2, L2, L2, L2, L1, TR, TR, TR, TR, TR, TR,
    TR, TR, TR, TR, L1, L2, L2, L2, L2, L1, TR, TR, TR, TR, TR, TR,
    TR, TR, TR, TR, L1, L2, L2, L2, L2, L1, TR, TR, TR, TR, TR, TR,
    TR, TR, TR, TR, L1, L1, L2, L2, L1, L1, TR, TR, TR, TR, TR, TR,
    TR, TR, TR, B2, B3, B3, B3, TR, B3, B3, B2, TR, TR, TR, TR, TR,
    TR, TR, B0, B2, B3, TR, TR, TR, TR, B3, B2, TR, TR, TR, TR, TR,
    TR, B0, B0, B1, TR, TR, TR, TR, TR, B1, B0, TR, TR, TR, TR, TR,
    TR, B0, B0, TR, TR, TR, TR, TR, TR, TR, B0, TR, TR, TR, TR, TR,
};

/* Player facing RIGHT, frame 0 */
static const uint16_t spr_player_r0[256] = {
    TR, TR, TR, TR, TR, B1, B1, B1, B1, B1, B1, B1, TR, TR, TR, TR,
    TR, TR, TR, TR, B1, B0, B0, B0, B0, B0, B0, B0, B1, TR, TR, TR,
    TR, TR, TR, B1, B0, B1, B2, B2, B2, B2, B2, B1, B0, B1, TR, TR,
    TR, TR, TR, B1, SK, SK, SK, SK, SK, SK, SK, SK, SK, B1, TR, TR,
    TR, TR, TR, B1, SK, SK, SK, SK, SK, SK, BK, SK, SK, B1, TR, TR,
    TR, TR, TR, TR, B1, SK, SK, SK, SK, SK, SK, SK, SK, B1, TR, TR,
    TR, TR, TR, TR, TR, B1, SK, SK, SK, E1, SK, SK, B1, TR, TR, TR,
    TR, TR, TR, TR, TR, TR, B1, SK, SK, SK, SK, B1, TR, TR, TR, TR,
    TR, TR, TR, TR, TR, TR, L1, L2, L2, L2, L2, L1, SK, TR, TR, TR,
    TR, TR, TR, TR, TR, TR, L1, L2, L2, L2, L2, L1, TR, TR, TR, TR,
    TR, TR, TR, TR, TR, TR, L1, L2, L2, L2, L2, L1, TR, TR, TR, TR,
    TR, TR, TR, TR, TR, TR, L1, L1, L2, L2, L1, L1, TR, TR, TR, TR,
    TR, TR, TR, TR, TR, TR, B2, B3, B3, B3, B3, B2, TR, TR, TR, TR,
    TR, TR, TR, TR, TR, TR, B2, B3, TR, B3, B3, B2, TR, TR, TR, TR,
    TR, TR, TR, TR, TR, TR, B0, B1, TR, B1, B1, B0, TR, TR, TR, TR,
    TR, TR, TR, TR, TR, TR, B0, B0, TR, TR, B0, B0, TR, TR, TR, TR,
};

/* Player facing RIGHT, frame 1 */
static const uint16_t spr_player_r1[256] = {
    TR, TR, TR, TR, TR, B1, B1, B1, B1, B1, B1, B1, TR, TR, TR, TR,
    TR, TR, TR, TR, B1, B0, B0, B0, B0, B0, B0, B0, B1, TR, TR, TR,
    TR, TR, TR, B1, B0, B1, B2, B2, B2, B2, B2, B1, B0, B1, TR, TR,
    TR, TR, TR, B1, SK, SK, SK, SK, SK, SK, SK, SK, SK, B1, TR, TR,
    TR, TR, TR, B1, SK, SK, SK, SK, SK, SK, BK, SK, SK, B1, TR, TR,
    TR, TR, TR, TR, B1, SK, SK, SK, SK, SK, SK, SK, SK, B1, TR, TR,
    TR, TR, TR, TR, TR, B1, SK, SK, SK, E1, SK, SK, B1, TR, TR, TR,
    TR, TR, TR, TR, TR, TR, B1, SK, SK, SK, SK, B1, TR, TR, TR, TR,
    TR, TR, TR, TR, TR, TR, L1, L2, L2, L2, L2, L1, SK, TR, TR, TR,
    TR, TR, TR, TR, TR, TR, L1, L2, L2, L2, L2, L1, TR, TR, TR, TR,
    TR, TR, TR, TR, TR, TR, L1, L2, L2, L2, L2, L1, TR, TR, TR, TR,
    TR, TR, TR, TR, TR, TR, L1, L1, L2, L2, L1, L1, TR, TR, TR, TR,
    TR, TR, TR, TR, TR, B2, B3, B3, TR, B3, B3, B3, B2, TR, TR, TR,
    TR, TR, TR, TR, TR, B2, B3, TR, TR, TR, TR, B3, B2, B0, TR, TR,
    TR, TR, TR, TR, TR, B0, B1, TR, TR, TR, TR, TR, B1, B0, B0, TR,
    TR, TR, TR, TR, TR, B0, TR, TR, TR, TR, TR, TR, TR, B0, B0, TR,
};

/* Lookup table: player_sprites[dir * 2 + frame] */
static const uint16_t * const player_sprites[8] = {
    spr_player_d0, spr_player_d1,
    spr_player_u0, spr_player_u1,
    spr_player_l0, spr_player_l1,
    spr_player_r0, spr_player_r1,
};

#endif /* ASSETS_PLAYER_H */
