/*
 * assets_tiles_special.h — Cave, anomaly, utility tiles (T_CAVE_WALL..T_CRATE)
 */
#ifndef ASSETS_TILES_SPECIAL_H
#define ASSETS_TILES_SPECIAL_H

#include "assets_palette.h"

/* T_CAVE_WALL (43) — dark rocky wall */
static const uint16_t tile_cave_wall[64] = {
    A0, A0, A0, A0, A0, A0, A0, A0,
    A0, A1, A0, A0, A0, A1, A0, A0,
    A0, A0, A0, A0, A0, A0, A0, A0,
    A0, A0, A0, A1, A0, A0, A0, A0,
    A0, A0, A0, A0, A0, A0, A0, A0,
    A0, A0, A0, A0, A0, A0, A1, A0,
    A0, A1, A0, A0, A0, A0, A0, A0,
    A0, A0, A0, A0, A0, A0, A0, A0,
};

/* T_CRYSTAL (44) — glowing crystal on cave floor */
static const uint16_t tile_crystal[64] = {
    A0, A0, A0, C0, C1, A0, A0, A0,
    A0, A0, C0, C1, C1, C0, A0, A0,
    A0, C0, C1, C1, C1, C1, C0, A0,
    A0, C0, C1, WH, C1, C1, C0, A0,
    A0, C0, C1, C1, C1, C1, C0, A0,
    A0, A0, C0, C1, C1, C0, A0, A0,
    A0, A0, A0, C0, C0, A0, A0, A0,
    A0, A0, A0, A0, A0, A0, A0, A0,
};

/* T_TORCH (45) — wall torch with flame */
static const uint16_t tile_torch[64] = {
    A0, A0, A0, F1, F0, A0, A0, A0,
    A0, A0, F0, F1, F1, F0, A0, A0,
    A0, A0, A0, F0, O0, A0, A0, A0,
    A0, A0, A0, B0, B0, A0, A0, A0,
    A0, A0, A0, B1, B0, A0, A0, A0,
    A0, A0, A0, B1, B0, A0, A0, A0,
    A0, A0, A1, B1, B0, A1, A0, A0,
    A0, A0, A0, A0, A0, A0, A0, A0,
};

/* T_CHEST_CLOSED (46) — treasure chest (closed) */
static const uint16_t tile_chest_closed[64] = {
    A0, A0, A0, A0, A0, A0, A0, A0,
    A0, B0, B1, B1, B1, B1, B0, A0,
    A0, B1, B2, D0, D0, B2, B1, A0,
    A0, B1, B2, B2, B2, B2, B1, A0,
    A0, B0, B1, D0, D0, B1, B0, A0,
    A0, B1, B2, B2, B2, B2, B1, A0,
    A0, B0, B1, B1, B1, B1, B0, A0,
    A0, A0, A0, A0, A0, A0, A0, A0,
};

/* T_CHEST_OPEN (47) — open chest */
static const uint16_t tile_chest_open[64] = {
    A0, B0, B1, B1, B1, B1, B0, A0,
    A0, B1, B2, D0, D0, B2, B1, A0,
    A0, B0, B1, B1, B1, B1, B0, A0,
    A0, B1, A0, A0, A0, A0, B1, A0,
    A0, B1, A0, D0, D1, A0, B1, A0,
    A0, B1, A0, A0, A0, A0, B1, A0,
    A0, B0, B1, B1, B1, B1, B0, A0,
    A0, A0, A0, A0, A0, A0, A0, A0,
};

/* T_LADDER (48) — vertical ladder */
static const uint16_t tile_ladder[64] = {
    A0, A0, B0, A0, A0, B0, A0, A0,
    A0, A0, B1, B0, B0, B1, A0, A0,
    A0, A0, B0, A0, A0, B0, A0, A0,
    A0, A0, B1, B0, B0, B1, A0, A0,
    A0, A0, B0, A0, A0, B0, A0, A0,
    A0, A0, B1, B0, B0, B1, A0, A0,
    A0, A0, B0, A0, A0, B0, A0, A0,
    A0, A0, B1, B0, B0, B1, A0, A0,
};

/* T_ANOMALY_GROUND (49) — purple-tinted ground */
static const uint16_t tile_anomaly_ground[64] = {
    P0, P0, P0, P0, P0, P0, P0, P0,
    P0, P0, P0, P0, P0, P0, P0, P0,
    P0, P0, P0, P1, P0, P0, P0, P0,
    P0, P0, P0, P0, P0, P0, P0, P0,
    P0, P0, P0, P0, P0, P0, P0, P0,
    P0, P0, P0, P0, P0, P0, P0, P0,
    P0, P0, P0, P0, P0, P1, P0, P0,
    P0, P0, P0, P0, P0, P0, P0, P0,
};

/* T_GLOW_TILE (50) — glowing floor pattern */
static const uint16_t tile_glow_tile[64] = {
    P0, P0, P0, P1, P1, P0, P0, P0,
    P0, P0, P1, P2, P2, P1, P0, P0,
    P0, P1, P2, C1, C1, P2, P1, P0,
    P1, P2, C1, WH, WH, C1, P2, P1,
    P1, P2, C1, WH, WH, C1, P2, P1,
    P0, P1, P2, C1, C1, P2, P1, P0,
    P0, P0, P1, P2, P2, P1, P0, P0,
    P0, P0, P0, P1, P1, P0, P0, P0,
};

/* T_PORTAL (51) — swirling portal */
static const uint16_t tile_portal[64] = {
    P0, P0, P1, P2, P1, P0, P0, P0,
    P0, P1, C1, P2, C1, P1, P0, P0,
    P1, C0, P2, C1, P2, C1, P1, P0,
    P2, P1, C1, WH, C1, P2, C0, P1,
    P1, C0, P2, C1, P2, C1, P1, P0,
    P0, P1, C1, P2, C1, P1, P0, P0,
    P0, P0, P1, P2, P1, P0, P0, P0,
    P0, P0, P0, P1, P0, P0, P0, P0,
};

/* T_BRIDGE_H (52) — horizontal bridge planks over water */
static const uint16_t tile_bridge_h[64] = {
    L1, L1, L1, L1, L1, L1, L1, L1,
    B0, B0, B0, B0, B0, B0, B0, B0,
    B1, B2, B1, B2, B1, B2, B1, B2,
    B2, B2, B2, B2, B2, B2, B2, B2,
    B2, B2, B2, B2, B2, B2, B2, B2,
    B1, B2, B1, B2, B1, B2, B1, B2,
    B0, B0, B0, B0, B0, B0, B0, B0,
    L1, L1, L1, L1, L1, L1, L1, L1,
};

/* T_BRIDGE_V (53) — vertical bridge planks over water */
static const uint16_t tile_bridge_v[64] = {
    L1, B0, B1, B2, B2, B1, B0, L1,
    L1, B0, B2, B2, B2, B2, B0, L1,
    L1, B0, B1, B2, B2, B1, B0, L1,
    L1, B0, B2, B2, B2, B2, B0, L1,
    L1, B0, B1, B2, B2, B1, B0, L1,
    L1, B0, B2, B2, B2, B2, B0, L1,
    L1, B0, B1, B2, B2, B1, B0, L1,
    L1, B0, B2, B2, B2, B2, B0, L1,
};

/* T_FENCE_H (54) — horizontal fence on grass */
static const uint16_t tile_fence_h[64] = {
    G3, G3, G3, G3, G3, G3, G3, G3,
    G3, G3, G3, G3, G3, G3, G3, G3,
    B0, B1, B1, B1, B1, B1, B1, B0,
    B1, B2, B2, B2, B2, B2, B2, B1,
    B0, B1, B1, B1, B1, B1, B1, B0,
    G3, G3, G3, G3, G3, G3, G3, G3,
    G3, G3, G3, G3, G3, G3, G3, G3,
    G3, G3, G3, G3, G3, G3, G3, G3,
};

/* T_FENCE_V (55) — vertical fence on grass */
static const uint16_t tile_fence_v[64] = {
    G3, G3, G3, B0, B1, G3, G3, G3,
    G3, G3, G3, B1, B2, G3, G3, G3,
    G3, G3, G3, B0, B1, G3, G3, G3,
    G3, G3, G3, B1, B2, G3, G3, G3,
    G3, G3, G3, B0, B1, G3, G3, G3,
    G3, G3, G3, B1, B2, G3, G3, G3,
    G3, G3, G3, B0, B1, G3, G3, G3,
    G3, G3, G3, B1, B2, G3, G3, G3,
};

/* T_SIGN (56) — wooden sign post */
static const uint16_t tile_sign[64] = {
    G3, G3, G3, G3, G3, G3, G3, G3,
    G3, B0, B1, B1, B1, B1, B0, G3,
    G3, B1, B2, B3, B3, B2, B1, G3,
    G3, B0, B1, B1, B1, B1, B0, G3,
    G3, G3, G3, B0, B0, G3, G3, G3,
    G3, G3, G3, B0, B0, G3, G3, G3,
    G3, G3, G3, B0, B0, G3, G3, G3,
    G3, G3, G3, G3, G3, G3, G3, G3,
};

/* T_LAMP (57) — street lamp on stone */
static const uint16_t tile_lamp[64] = {
    A2, A2, D1, D1, D1, D1, A2, A2,
    A2, A2, A2, D0, D0, A2, A2, A2,
    A2, A2, A2, A1, A1, A2, A2, A2,
    A2, A2, A2, A1, A1, A2, A2, A2,
    A2, A2, A2, A1, A1, A2, A2, A2,
    A2, A2, A2, A1, A1, A2, A2, A2,
    A2, A2, A2, A1, A1, A2, A2, A2,
    A2, A2, A1, A1, A1, A1, A2, A2,
};

/* T_BARREL (58) — barrel top-down view */
static const uint16_t tile_barrel[64] = {
    G3, G3, B0, B0, B0, B0, G3, G3,
    G3, B0, B1, B2, B2, B1, B0, G3,
    B0, B1, B2, B3, B3, B2, B1, B0,
    B0, B2, B3, B3, B3, B3, B2, B0,
    B0, B2, B3, B3, B3, B3, B2, B0,
    B0, B1, B2, B3, B3, B2, B1, B0,
    G3, B0, B1, B2, B2, B1, B0, G3,
    G3, G3, B0, B0, B0, B0, G3, G3,
};

/* T_CRATE (59) — wooden crate */
static const uint16_t tile_crate[64] = {
    B0, B0, B0, B0, B0, B0, B0, B0,
    B0, B1, B2, B1, B1, B2, B1, B0,
    B0, B2, B3, B2, B2, B3, B2, B0,
    B0, B1, B2, B0, B0, B2, B1, B0,
    B0, B1, B2, B0, B0, B2, B1, B0,
    B0, B2, B3, B2, B2, B3, B2, B0,
    B0, B1, B2, B1, B1, B2, B1, B0,
    B0, B0, B0, B0, B0, B0, B0, B0,
};

#endif /* ASSETS_TILES_SPECIAL_H */
