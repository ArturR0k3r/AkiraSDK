/*
 * assets.h — Master asset include + tile properties lookup table
 * Include this ONLY from engine.c to avoid data duplication.
 */
#ifndef ASSETS_H
#define ASSETS_H

#include "engine.h"

/* ─── Tile graphics ───────────────────────────────────────────────────────── */
#include "assets_tiles_ground.h"
#include "assets_tiles_building.h"
#include "assets_tiles_nature.h"
#include "assets_tiles_special.h"

/* ─── Sprite graphics ─────────────────────────────────────────────────────── */
#include "assets_player.h"
#include "assets_npcs.h"
#include "assets_items.h"

/* ─── Tile graphics lookup: tile_gfx[tile_id] → pointer to 64 uint16_t ──── */
static const uint16_t * const tile_gfx[NUM_TILES] = {
    tile_grass,           /*  0: T_GRASS          */
    tile_grass2,          /*  1: T_GRASS2         */
    tile_dirt,            /*  2: T_DIRT           */
    tile_sand,            /*  3: T_SAND           */
    tile_stone,           /*  4: T_STONE          */
    tile_dark_stone,      /*  5: T_DARK_STONE     */
    tile_cave_floor,      /*  6: T_CAVE_FLOOR     */
    tile_water,           /*  7: T_WATER          */
    tile_water_deep,      /*  8: T_WATER_DEEP     */
    tile_water_edge_n,    /*  9: T_WATER_EDGE_N   */
    tile_water_edge_s,    /* 10: T_WATER_EDGE_S   */
    tile_water_edge_e,    /* 11: T_WATER_EDGE_E   */
    tile_water_edge_w,    /* 12: T_WATER_EDGE_W   */
    tile_path_h,          /* 13: T_PATH_H         */
    tile_path_v,          /* 14: T_PATH_V         */
    tile_path_cross,      /* 15: T_PATH_CROSS     */
    tile_road,            /* 16: T_ROAD           */
    tile_sidewalk,        /* 17: T_SIDEWALK       */
    tile_wall_top,        /* 18: T_WALL_TOP       */
    tile_wall_side,       /* 19: T_WALL_SIDE      */
    tile_wall_bot,        /* 20: T_WALL_BOT       */
    tile_roof,            /* 21: T_ROOF           */
    tile_window,          /* 22: T_WINDOW         */
    tile_door_closed,     /* 23: T_DOOR_CLOSED    */
    tile_door_open,       /* 24: T_DOOR_OPEN      */
    tile_chimney,         /* 25: T_CHIMNEY        */
    tile_wood_floor,      /* 26: T_WOOD_FLOOR     */
    tile_carpet,          /* 27: T_CARPET         */
    tile_bookshelf,       /* 28: T_BOOKSHELF      */
    tile_table,           /* 29: T_TABLE          */
    tile_bed,             /* 30: T_BED            */
    tile_chair,           /* 31: T_CHAIR          */
    tile_fireplace,       /* 32: T_FIREPLACE      */
    tile_stairs_up,       /* 33: T_STAIRS_UP      */
    tile_stairs_down,     /* 34: T_STAIRS_DOWN    */
    tile_tree_tl,         /* 35: T_TREE_TL        */
    tile_tree_tr,         /* 36: T_TREE_TR        */
    tile_tree_bl,         /* 37: T_TREE_BL        */
    tile_tree_br,         /* 38: T_TREE_BR        */
    tile_bush,            /* 39: T_BUSH           */
    tile_flower,          /* 40: T_FLOWER         */
    tile_rock,            /* 41: T_ROCK           */
    tile_tall_grass,      /* 42: T_TALL_GRASS     */
    tile_cave_wall,       /* 43: T_CAVE_WALL      */
    tile_crystal,         /* 44: T_CRYSTAL        */
    tile_torch,           /* 45: T_TORCH          */
    tile_chest_closed,    /* 46: T_CHEST_CLOSED   */
    tile_chest_open,      /* 47: T_CHEST_OPEN     */
    tile_ladder,          /* 48: T_LADDER         */
    tile_anomaly_ground,  /* 49: T_ANOMALY_GROUND */
    tile_glow_tile,       /* 50: T_GLOW_TILE      */
    tile_portal,          /* 51: T_PORTAL         */
    tile_bridge_h,        /* 52: T_BRIDGE_H       */
    tile_bridge_v,        /* 53: T_BRIDGE_V       */
    tile_fence_h,         /* 54: T_FENCE_H        */
    tile_fence_v,         /* 55: T_FENCE_V        */
    tile_sign,            /* 56: T_SIGN           */
    tile_lamp,            /* 57: T_LAMP           */
    tile_barrel,          /* 58: T_BARREL         */
    tile_crate,           /* 59: T_CRATE          */
};

/* ─── Tile property flags: tile_props[tile_id] ─────────────────────────── */
static const uint8_t tile_props[NUM_TILES] = {
    /* 0  T_GRASS         */ TILE_WALKABLE,
    /* 1  T_GRASS2        */ TILE_WALKABLE,
    /* 2  T_DIRT          */ TILE_WALKABLE,
    /* 3  T_SAND          */ TILE_WALKABLE,
    /* 4  T_STONE         */ TILE_WALKABLE,
    /* 5  T_DARK_STONE    */ TILE_WALKABLE,
    /* 6  T_CAVE_FLOOR    */ TILE_WALKABLE | TILE_DARK,
    /* 7  T_WATER         */ TILE_WATER,
    /* 8  T_WATER_DEEP    */ TILE_WATER,
    /* 9  T_WATER_EDGE_N  */ TILE_WATER,
    /* 10 T_WATER_EDGE_S  */ TILE_WATER,
    /* 11 T_WATER_EDGE_E  */ TILE_WATER,
    /* 12 T_WATER_EDGE_W  */ TILE_WATER,
    /* 13 T_PATH_H        */ TILE_WALKABLE,
    /* 14 T_PATH_V        */ TILE_WALKABLE,
    /* 15 T_PATH_CROSS    */ TILE_WALKABLE,
    /* 16 T_ROAD          */ TILE_WALKABLE,
    /* 17 T_SIDEWALK      */ TILE_WALKABLE,
    /* 18 T_WALL_TOP      */ TILE_SOLID,
    /* 19 T_WALL_SIDE     */ TILE_SOLID,
    /* 20 T_WALL_BOT      */ TILE_SOLID,
    /* 21 T_ROOF          */ TILE_SOLID,
    /* 22 T_WINDOW        */ TILE_SOLID,
    /* 23 T_DOOR_CLOSED   */ TILE_SOLID | TILE_DOOR,
    /* 24 T_DOOR_OPEN     */ TILE_DOOR,
    /* 25 T_CHIMNEY       */ TILE_SOLID,
    /* 26 T_WOOD_FLOOR    */ TILE_WALKABLE,
    /* 27 T_CARPET        */ TILE_WALKABLE,
    /* 28 T_BOOKSHELF     */ TILE_SOLID | TILE_INTERACTIVE,
    /* 29 T_TABLE         */ TILE_SOLID,
    /* 30 T_BED           */ TILE_SOLID,
    /* 31 T_CHAIR         */ TILE_WALKABLE,
    /* 32 T_FIREPLACE     */ TILE_SOLID,
    /* 33 T_STAIRS_UP     */ TILE_WALKABLE | TILE_DOOR,
    /* 34 T_STAIRS_DOWN   */ TILE_WALKABLE | TILE_DOOR,
    /* 35 T_TREE_TL       */ TILE_SOLID,
    /* 36 T_TREE_TR       */ TILE_SOLID,
    /* 37 T_TREE_BL       */ TILE_SOLID,
    /* 38 T_TREE_BR       */ TILE_SOLID,
    /* 39 T_BUSH          */ TILE_SOLID,
    /* 40 T_FLOWER        */ TILE_WALKABLE,
    /* 41 T_ROCK          */ TILE_SOLID,
    /* 42 T_TALL_GRASS    */ TILE_WALKABLE,
    /* 43 T_CAVE_WALL     */ TILE_SOLID | TILE_DARK,
    /* 44 T_CRYSTAL       */ TILE_SOLID | TILE_INTERACTIVE | TILE_ANOMALY,
    /* 45 T_TORCH         */ TILE_SOLID,
    /* 46 T_CHEST_CLOSED  */ TILE_SOLID | TILE_INTERACTIVE,
    /* 47 T_CHEST_OPEN    */ TILE_WALKABLE,
    /* 48 T_LADDER        */ TILE_WALKABLE | TILE_DOOR,
    /* 49 T_ANOMALY_GROUND*/ TILE_WALKABLE | TILE_ANOMALY,
    /* 50 T_GLOW_TILE     */ TILE_WALKABLE | TILE_ANOMALY,
    /* 51 T_PORTAL        */ TILE_WALKABLE | TILE_ANOMALY | TILE_DOOR,
    /* 52 T_BRIDGE_H      */ TILE_WALKABLE,
    /* 53 T_BRIDGE_V      */ TILE_WALKABLE,
    /* 54 T_FENCE_H       */ TILE_SOLID,
    /* 55 T_FENCE_V       */ TILE_SOLID,
    /* 56 T_SIGN          */ TILE_SOLID | TILE_INTERACTIVE,
    /* 57 T_LAMP          */ TILE_SOLID,
    /* 58 T_BARREL        */ TILE_SOLID,
    /* 59 T_CRATE         */ TILE_SOLID,
};

/* ─── Item name strings ───────────────────────────────────────────────────── */
static const char * const item_names[NUM_ITEM_TYPES] = {
    "",                /* 0: ITEM_NONE */
    "House Key",       /* 1 */
    "Diary",           /* 2 */
    "Lantern",         /* 3 */
    "Old Compass",     /* 4 */
    "Mansion Key",     /* 5 */
    "Lab Keycard",     /* 6 */
    "Crystal Shard",   /* 7 */
    "Crystal Shard",   /* 8 */
    "Crystal Shard",   /* 9 */
    "Tackle Box",      /* 10 */
    "Anomaly Sample",  /* 11 */
    "Map Fragment",    /* 12 */
    "Diary Page",      /* 13 */
};

#endif /* ASSETS_H */
