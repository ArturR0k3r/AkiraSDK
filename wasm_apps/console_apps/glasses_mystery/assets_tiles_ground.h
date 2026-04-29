/*
 * assets_tiles_ground.h — Ground, water, and path tiles (T_GRASS..T_SIDEWALK)
 * Each tile: 8×8 pixels, RGB565, row-major (64 uint16_t values)
 */
#ifndef ASSETS_TILES_GROUND_H
#define ASSETS_TILES_GROUND_H

#include "assets_palette.h"

/* T_GRASS (0) — standard grass, mostly solid */
static const uint16_t tile_grass[64] = {
    G3, G3, G3, G3, G3, G3, G3, G3,
    G3, G3, G3, G3, G3, G3, G3, G3,
    G3, G3, G3, G3, G3, G3, G3, G3,
    G3, G3, G3, G2, G3, G3, G3, G3,
    G3, G3, G3, G3, G3, G3, G3, G3,
    G3, G3, G3, G3, G3, G3, G3, G3,
    G3, G3, G3, G3, G3, G3, G3, G3,
    G3, G3, G3, G3, G3, G2, G3, G3,
};

/* T_GRASS2 (1) — grass with small flowers */
static const uint16_t tile_grass2[64] = {
    G3, G3, G3, G3, G3, G3, G3, G3,
    G3, G3, G3, G3, D1, G3, G3, G3,
    G3, G3, G3, G3, G3, G3, G3, G3,
    G3, G3, G3, G3, G3, G3, G3, G3,
    G3, G3, G3, G3, G3, G3, G3, G3,
    G3, G3, G3, G3, G3, E2, G3, G3,
    G3, G3, G3, G3, G3, G3, G3, G3,
    G3, G3, G3, G3, G3, G3, G3, G3,
};

/* T_DIRT (2) — brown dirt */
static const uint16_t tile_dirt[64] = {
    B1, B1, B1, B1, B1, B1, B1, B1,
    B1, B1, B1, B1, B1, B1, B1, B1,
    B1, B1, B1, B1, B1, B1, B1, B1,
    B1, B1, B1, B2, B1, B1, B1, B1,
    B1, B1, B1, B1, B1, B1, B1, B1,
    B1, B1, B1, B1, B1, B1, B1, B1,
    B1, B1, B1, B1, B1, B1, B1, B1,
    B1, B1, B1, B1, B1, B2, B1, B1,
};

/* T_SAND (3) — sandy ground */
static const uint16_t tile_sand[64] = {
    S1, S1, S1, S1, S1, S1, S1, S1,
    S1, S1, S1, S1, S1, S1, S1, S1,
    S1, S1, S1, S1, S1, S1, S1, S1,
    S1, S1, S1, S1, S1, S1, S1, S1,
    S1, S1, S1, S1, S1, S1, S1, S1,
    S1, S1, S1, S1, S1, S1, S1, S1,
    S1, S1, S1, S1, S1, S1, S1, S1,
    S1, S1, S1, S1, S1, S1, S1, S1,
};

/* T_STONE (4) — flagstone floor */
static const uint16_t tile_stone[64] = {
    A2, A2, A2, A1, A2, A2, A2, A1,
    A2, A3, A2, A1, A2, A3, A2, A1,
    A2, A2, A2, A1, A2, A2, A2, A1,
    A1, A1, A1, A1, A1, A1, A1, A1,
    A2, A2, A2, A1, A2, A2, A2, A1,
    A2, A2, A3, A1, A3, A2, A2, A1,
    A2, A2, A2, A1, A2, A2, A2, A1,
    A1, A1, A1, A1, A1, A1, A1, A1,
};

/* T_DARK_STONE (5) — dark stone floor */
static const uint16_t tile_dark_stone[64] = {
    A1, A1, A1, A0, A1, A1, A1, A0,
    A1, A2, A1, A0, A1, A2, A1, A0,
    A1, A1, A1, A0, A1, A1, A1, A0,
    A0, A0, A0, A0, A0, A0, A0, A0,
    A1, A1, A1, A0, A1, A1, A1, A0,
    A1, A1, A2, A0, A2, A1, A1, A0,
    A1, A1, A1, A0, A1, A1, A1, A0,
    A0, A0, A0, A0, A0, A0, A0, A0,
};

/* T_CAVE_FLOOR (6) — very dark cave ground */
static const uint16_t tile_cave_floor[64] = {
    A0, A0, A1, A0, A0, A0, A1, A0,
    A0, A0, A0, A0, A1, A0, A0, A0,
    A1, A0, A0, A0, A0, A0, A0, A1,
    A0, A0, A0, A0, A0, A1, A0, A0,
    A0, A1, A0, A0, A0, A0, A0, A0,
    A0, A0, A0, A1, A0, A0, A1, A0,
    A0, A0, A0, A0, A0, A0, A0, A0,
    A1, A0, A0, A0, A1, A0, A0, A1,
};

/* T_WATER (7) — water with wave highlights */
static const uint16_t tile_water[64] = {
    L1, L1, L1, L1, L1, L1, L1, L1,
    L1, L1, L1, L1, L1, L1, L1, L1,
    L1, L1, L2, L1, L1, L1, L1, L1,
    L1, L1, L1, L1, L1, L1, L1, L1,
    L1, L1, L1, L1, L1, L1, L1, L1,
    L1, L1, L1, L1, L1, L1, L1, L1,
    L1, L1, L1, L1, L1, L2, L1, L1,
    L1, L1, L1, L1, L1, L1, L1, L1,
};

/* T_WATER_DEEP (8) — deep water */
static const uint16_t tile_water_deep[64] = {
    L0, L0, L0, L0, L0, L0, L0, L0,
    L0, L0, L0, L0, L0, L0, L0, L0,
    L0, L0, L0, L0, L0, L0, L0, L0,
    L0, L0, L0, L1, L0, L0, L0, L0,
    L0, L0, L0, L0, L0, L0, L0, L0,
    L0, L0, L0, L0, L0, L0, L0, L0,
    L0, L0, L0, L0, L0, L0, L0, L0,
    L0, L0, L0, L0, L0, L1, L0, L0,
};

/* T_WATER_EDGE_N (9) — top=grass, bottom=water */
static const uint16_t tile_water_edge_n[64] = {
    G3, G3, G3, G3, G3, G3, G3, G3,
    G3, G3, G3, G3, G3, G3, G3, G3,
    G3, G3, G3, G3, G3, G3, G3, G3,
    G2, G2, G1, G1, G2, G1, G1, G2,
    L1, L1, L1, L1, L1, L1, L1, L1,
    L1, L1, L1, L1, L1, L1, L1, L1,
    L1, L1, L1, L1, L1, L1, L1, L1,
    L1, L1, L1, L1, L1, L1, L1, L1,
};

/* T_WATER_EDGE_S (10) — top=water, bottom=grass */
static const uint16_t tile_water_edge_s[64] = {
    L1, L1, L1, L1, L1, L1, L1, L1,
    L1, L1, L1, L1, L1, L1, L1, L1,
    L1, L1, L1, L1, L1, L1, L1, L1,
    L1, L1, L1, L1, L1, L1, L1, L1,
    G1, G1, G2, G1, G1, G2, G1, G1,
    G3, G3, G3, G3, G3, G3, G3, G3,
    G3, G3, G3, G3, G3, G3, G3, G3,
    G3, G3, G3, G3, G3, G3, G3, G3,
};

/* T_WATER_EDGE_E (11) — left=water, right=grass */
static const uint16_t tile_water_edge_e[64] = {
    L1, L1, L1, L1, G1, G3, G3, G3,
    L1, L1, L1, L1, G1, G3, G3, G3,
    L1, L1, L1, L1, G1, G3, G3, G3,
    L1, L1, L1, L1, G1, G3, G3, G3,
    L1, L1, L1, L1, G1, G3, G3, G3,
    L1, L1, L1, L1, G1, G3, G3, G3,
    L1, L1, L1, L1, G1, G3, G3, G3,
    L1, L1, L1, L1, G1, G3, G3, G3,
};

/* T_WATER_EDGE_W (12) — left=grass, right=water */
static const uint16_t tile_water_edge_w[64] = {
    G3, G3, G3, G1, L1, L1, L1, L1,
    G3, G3, G3, G1, L1, L1, L1, L1,
    G3, G3, G3, G1, L1, L1, L1, L1,
    G3, G3, G3, G1, L1, L1, L1, L1,
    G3, G3, G3, G1, L1, L1, L1, L1,
    G3, G3, G3, G1, L1, L1, L1, L1,
    G3, G3, G3, G1, L1, L1, L1, L1,
    G3, G3, G3, G1, L1, L1, L1, L1,
};

/* T_PATH_H (13) — horizontal path (grass edges top/bottom, dirt center) */
static const uint16_t tile_path_h[64] = {
    G3, G3, G3, G3, G3, G3, G3, G3,
    G3, G3, G3, G3, G3, G3, G3, G3,
    B1, B1, B1, B1, B1, B1, B1, B1,
    B1, B1, B1, B1, B1, B1, B1, B1,
    B1, B1, B1, B1, B1, B1, B1, B1,
    B1, B1, B1, B1, B1, B1, B1, B1,
    G3, G3, G3, G3, G3, G3, G3, G3,
    G3, G3, G3, G3, G3, G3, G3, G3,
};

/* T_PATH_V (14) — vertical path */
static const uint16_t tile_path_v[64] = {
    G3, G3, B1, B1, B1, B1, G3, G3,
    G3, G3, B1, B1, B1, B1, G3, G3,
    G3, G3, B1, B1, B1, B1, G3, G3,
    G3, G3, B1, B1, B1, B1, G3, G3,
    G3, G3, B1, B1, B1, B1, G3, G3,
    G3, G3, B1, B1, B1, B1, G3, G3,
    G3, G3, B1, B1, B1, B1, G3, G3,
    G3, G3, B1, B1, B1, B1, G3, G3,
};

/* T_PATH_CROSS (15) — path crossroads */
static const uint16_t tile_path_cross[64] = {
    G3, G3, B1, B1, B1, B1, G3, G3,
    G3, G3, B1, B1, B1, B1, G3, G3,
    B1, B1, B1, B1, B1, B1, B1, B1,
    B1, B1, B1, B1, B1, B1, B1, B1,
    B1, B1, B1, B1, B1, B1, B1, B1,
    B1, B1, B1, B1, B1, B1, B1, B1,
    G3, G3, B1, B1, B1, B1, G3, G3,
    G3, G3, B1, B1, B1, B1, G3, G3,
};

/* T_ROAD (16) — cobblestone road */
static const uint16_t tile_road[64] = {
    A2, A2, A2, A2, A2, A2, A2, A2,
    A2, A2, A2, A2, A2, A2, A2, A2,
    A2, A2, A2, A2, A2, A2, A2, A2,
    A2, A2, A2, A3, A2, A2, A2, A2,
    A2, A2, A2, A2, A2, A2, A2, A2,
    A2, A2, A2, A2, A2, A2, A2, A2,
    A2, A2, A2, A2, A2, A2, A2, A2,
    A2, A2, A2, A2, A2, A3, A2, A2,
};

/* T_SIDEWALK (17) — light stone sidewalk */
static const uint16_t tile_sidewalk[64] = {
    A3, A3, A3, A3, A3, A3, A3, A3,
    A3, A3, A3, A3, A3, A3, A3, A3,
    A3, A3, A3, A3, A3, A3, A3, A3,
    A3, A3, A3, A3, A3, A3, A3, A3,
    A3, A3, A3, A3, A3, A3, A3, A3,
    A3, A3, A3, A3, A3, A3, A3, A3,
    A3, A3, A3, A3, A3, A3, A3, A3,
    A3, A3, A3, A3, A3, A3, A3, A3,
};

#endif /* ASSETS_TILES_GROUND_H */
