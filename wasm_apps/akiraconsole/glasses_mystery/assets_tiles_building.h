/*
 * assets_tiles_building.h — Wall, roof, door, indoor tiles (T_WALL_TOP..T_STAIRS_DOWN)
 */
#ifndef ASSETS_TILES_BUILDING_H
#define ASSETS_TILES_BUILDING_H

#include "assets_palette.h"

/* T_WALL_TOP (18) — top edge of building wall */
static const uint16_t tile_wall_top[64] = {
    A1, A1, A1, A1, A1, A1, A1, A1,
    A2, A3, A3, A3, A3, A3, A3, A2,
    A2, A3, A3, A3, A3, A3, A3, A2,
    A2, A3, A3, A3, A3, A3, A3, A2,
    A2, A3, A3, A3, A3, A3, A3, A2,
    A2, A3, A3, A3, A3, A3, A3, A2,
    A2, A3, A3, A3, A3, A3, A3, A2,
    A2, A3, A3, A3, A3, A3, A3, A2,
};

/* T_WALL_SIDE (19) — building wall surface */
static const uint16_t tile_wall_side[64] = {
    A2, A3, A3, A3, A3, A3, A3, A2,
    A2, A3, A3, A3, A3, A3, A3, A2,
    A2, A3, A3, A3, A3, A3, A3, A2,
    A2, A3, A3, A3, A3, A3, A3, A2,
    A2, A3, A3, A3, A3, A3, A3, A2,
    A2, A3, A3, A3, A3, A3, A3, A2,
    A2, A3, A3, A3, A3, A3, A3, A2,
    A2, A3, A3, A3, A3, A3, A3, A2,
};

/* T_WALL_BOT (20) — bottom edge of building wall */
static const uint16_t tile_wall_bot[64] = {
    A2, A3, A3, A3, A3, A3, A3, A2,
    A2, A3, A3, A3, A3, A3, A3, A2,
    A2, A3, A3, A3, A3, A3, A3, A2,
    A2, A3, A3, A3, A3, A3, A3, A2,
    A2, A3, A3, A3, A3, A3, A3, A2,
    A2, A3, A3, A3, A3, A3, A3, A2,
    A2, A3, A3, A3, A3, A3, A3, A2,
    A1, A1, A1, A1, A1, A1, A1, A1,
};

/* T_ROOF (21) — red/brown roof tiles */
static const uint16_t tile_roof[64] = {
    E1, E1, E1, E1, E1, E1, E1, E1,
    E1, E1, E1, E1, E1, E1, E1, E1,
    E1, E1, E1, E1, E1, E1, E1, E1,
    E0, E0, E0, E0, E0, E0, E0, E0,
    E1, E1, E1, E1, E1, E1, E1, E1,
    E1, E1, E1, E1, E1, E1, E1, E1,
    E1, E1, E1, E1, E1, E1, E1, E1,
    E0, E0, E0, E0, E0, E0, E0, E0,
};

/* T_WINDOW (22) — wall with window */
static const uint16_t tile_window[64] = {
    A2, A3, A3, A3, A3, A3, A3, A2,
    A2, A3, A1, A1, A1, A1, A3, A2,
    A2, A3, A1, L2, L3, A1, A3, A2,
    A2, A3, A1, L3, L2, A1, A3, A2,
    A2, A3, A1, L2, L3, A1, A3, A2,
    A2, A3, A1, A1, A1, A1, A3, A2,
    A2, A3, A3, A3, A3, A3, A3, A2,
    A2, A3, A3, A3, A3, A3, A3, A2,
};

/* T_DOOR_CLOSED (23) — brown door */
static const uint16_t tile_door_closed[64] = {
    A2, A3, A3, A3, A3, A3, A3, A2,
    A2, A3, B0, B1, B1, B0, A3, A2,
    A2, A3, B1, B2, B2, B1, A3, A2,
    A2, A3, B1, B2, D0, B1, A3, A2,
    A2, A3, B1, B2, B2, B1, A3, A2,
    A2, A3, B1, B2, B2, B1, A3, A2,
    A2, A3, B0, B1, B1, B0, A3, A2,
    A1, A1, B0, B0, B0, B0, A1, A1,
};

/* T_DOOR_OPEN (24) — open door showing dark interior */
static const uint16_t tile_door_open[64] = {
    A2, A3, A3, A3, A3, A3, A3, A2,
    A2, A3, B0, A0, A0, B0, A3, A2,
    A2, A3, B1, A0, A0, B1, A3, A2,
    A2, A3, B1, A0, A0, B1, A3, A2,
    A2, A3, B1, A0, A0, B1, A3, A2,
    A2, A3, B1, A0, A0, B1, A3, A2,
    A2, A3, B0, A0, A0, B0, A3, A2,
    A1, A1, B0, A0, A0, B0, A1, A1,
};

/* T_CHIMNEY (25) — brick chimney */
static const uint16_t tile_chimney[64] = {
    E0, E1, E0, A1, A1, E0, E1, E0,
    E1, E0, E1, A1, A1, E1, E0, E1,
    E0, E1, E1, E1, E1, E1, E1, E0,
    E1, E0, E0, E0, E0, E0, E0, E1,
    E0, E1, E1, E1, E1, E1, E1, E0,
    E1, E0, E0, E0, E0, E0, E0, E1,
    E0, E1, E1, E1, E1, E1, E1, E0,
    E1, E0, E0, E0, E0, E0, E0, E1,
};

/* T_WOOD_FLOOR (26) — indoor wood planks */
static const uint16_t tile_wood_floor[64] = {
    B2, B2, B2, B2, B2, B2, B2, B2,
    B3, B3, B3, B3, B3, B3, B3, B3,
    B2, B2, B2, B1, B2, B2, B2, B2,
    B3, B3, B3, B3, B3, B3, B3, B3,
    B2, B2, B2, B2, B2, B2, B2, B2,
    B3, B3, B3, B3, B3, B3, B3, B3,
    B2, B2, B2, B2, B2, B1, B2, B2,
    B3, B3, B3, B3, B3, B3, B3, B3,
};

/* T_CARPET (27) — red carpet */
static const uint16_t tile_carpet[64] = {
    E0, E0, E0, E0, E0, E0, E0, E0,
    E0, E1, E1, E1, E1, E1, E1, E0,
    E0, E1, E1, E1, E1, E1, E1, E0,
    E0, E1, E1, E1, E1, E1, E1, E0,
    E0, E1, E1, E1, E1, E1, E1, E0,
    E0, E1, E1, E1, E1, E1, E1, E0,
    E0, E1, E1, E1, E1, E1, E1, E0,
    E0, E0, E0, E0, E0, E0, E0, E0,
};

/* T_BOOKSHELF (28) — shelves with books */
static const uint16_t tile_bookshelf[64] = {
    B0, B0, B0, B0, B0, B0, B0, B0,
    B0, E1, L1, G2, D0, E1, L1, B0,
    B0, E1, L1, G2, D0, E1, L1, B0,
    B0, B1, B1, B1, B1, B1, B1, B0,
    B0, G2, D0, E1, L1, G2, D0, B0,
    B0, G2, D0, E1, L1, G2, D0, B0,
    B0, B1, B1, B1, B1, B1, B1, B0,
    B0, B0, B0, B0, B0, B0, B0, B0,
};

/* T_TABLE (29) — brown table surface */
static const uint16_t tile_table[64] = {
    B0, B2, B2, B2, B2, B2, B2, B0,
    B2, B3, B3, B3, B3, B3, B3, B2,
    B2, B3, B3, B3, B3, B3, B3, B2,
    B2, B3, B3, B3, B3, B3, B3, B2,
    B2, B3, B3, B3, B3, B3, B3, B2,
    B2, B3, B3, B3, B3, B3, B3, B2,
    B2, B3, B3, B3, B3, B3, B3, B2,
    B0, B2, B2, B2, B2, B2, B2, B0,
};

/* T_BED (30) — bed top-down view */
static const uint16_t tile_bed[64] = {
    B0, B1, B1, B1, B1, B1, B1, B0,
    B0, WH, WH, WH, WH, WH, WH, B0,
    B0, WH, A4, A4, A4, A4, WH, B0,
    B0, WH, A4, A4, A4, A4, WH, B0,
    B0, L1, L2, L2, L2, L2, L1, B0,
    B0, L1, L2, L2, L2, L2, L1, B0,
    B0, L1, L2, L2, L2, L2, L1, B0,
    B0, B0, B1, B1, B1, B1, B0, B0,
};

/* T_CHAIR (31) — small chair */
static const uint16_t tile_chair[64] = {
    B2, B2, B0, B0, B0, B0, B2, B2,
    B2, B2, B0, B1, B1, B0, B2, B2,
    B2, B2, B0, B1, B1, B0, B2, B2,
    B2, B2, B0, B1, B1, B0, B2, B2,
    B2, B2, B1, B2, B2, B1, B2, B2,
    B2, B2, B2, B2, B2, B2, B2, B2,
    B2, B2, B2, B2, B2, B2, B2, B2,
    B2, B2, B0, B2, B2, B0, B2, B2,
};

/* T_FIREPLACE (32) — stone fireplace with fire */
static const uint16_t tile_fireplace[64] = {
    A2, A2, A2, A2, A2, A2, A2, A2,
    A2, A1, A1, A1, A1, A1, A1, A2,
    A2, A1, BK, F0, F1, BK, A1, A2,
    A2, A1, F0, F1, F0, F1, A1, A2,
    A2, A1, F1, F0, F1, F0, A1, A2,
    A2, A1, BK, F0, F1, BK, A1, A2,
    A2, A1, A0, A0, A0, A0, A1, A2,
    A2, A2, A2, A2, A2, A2, A2, A2,
};

/* T_STAIRS_UP (33) — ascending stairs pattern */
static const uint16_t tile_stairs_up[64] = {
    A3, A3, A3, A3, A3, A3, A3, A3,
    A3, A3, A3, A3, A3, A3, A3, A2,
    A2, A2, A2, A2, A2, A2, A2, A2,
    A2, A2, A2, A2, A2, A2, A1, A1,
    A1, A1, A1, A1, A1, A1, A1, A1,
    A1, A1, A1, A1, A1, A0, A0, A0,
    A0, A0, A0, A0, A0, A0, A0, A0,
    A0, A0, A0, A0, BK, BK, BK, BK,
};

/* T_STAIRS_DOWN (34) — descending stairs */
static const uint16_t tile_stairs_down[64] = {
    A0, A0, A0, A0, BK, BK, BK, BK,
    A0, A0, A0, A0, A0, A0, A0, A0,
    A1, A1, A1, A1, A1, A0, A0, A0,
    A1, A1, A1, A1, A1, A1, A1, A1,
    A2, A2, A2, A2, A2, A2, A1, A1,
    A2, A2, A2, A2, A2, A2, A2, A2,
    A3, A3, A3, A3, A3, A3, A3, A2,
    A3, A3, A3, A3, A3, A3, A3, A3,
};

#endif /* ASSETS_TILES_BUILDING_H */
