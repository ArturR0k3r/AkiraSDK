/*
 * world.h — Glasses Mystery: World data structures
 */
#ifndef WORLD_H
#define WORLD_H

#include "engine.h"

/* Door — connects a tile position on one screen to a spawn point on another */
typedef struct {
    uint8_t tile_x, tile_y;      /* door position on source screen */
    uint8_t target_screen;       /* destination screen ID */
    uint8_t spawn_x, spawn_y;   /* spawn tile position on target */
    uint32_t require_flag;       /* quest flag required to enter (0=none) */
    uint8_t require_item;        /* item required (ITEM_NONE=none) */
} door_t;

/* NPC spawn point on a screen */
typedef struct {
    uint8_t npc_id;
    uint8_t tile_x, tile_y;
    uint8_t direction;           /* facing direction */
    uint32_t require_flag;       /* only appears if this flag IS set (0=always) */
    uint32_t hide_flag;          /* hidden if this flag IS set (0=never hidden) */
} npc_spawn_t;

/* Item spawn point on a screen */
typedef struct {
    uint8_t item_id;
    uint8_t tile_x, tile_y;
    uint32_t hide_flag;          /* item disappears once this flag is set */
} item_spawn_t;

/* Screen definition */
typedef struct {
    const uint8_t *rle_data;     /* RLE-encoded tile data */
    uint16_t rle_size;           /* byte length of rle_data */
    int8_t  connections[4];      /* adjacent screens: [UP,DOWN,LEFT,RIGHT], -1=wall */
    const door_t *doors;
    uint8_t num_doors;
    const npc_spawn_t *npc_spawns;
    uint8_t num_npcs;
    const item_spawn_t *item_spawns;
    uint8_t num_items;
} screen_def_t;

/* World data — defined in world.c */
extern const screen_def_t world_screens[NUM_SCREENS];

/* Current decoded screen tile buffer */
extern uint8_t current_tiles[TILES_X * TILES_Y];
extern uint8_t current_screen_id;

/* Load and decode a screen */
void world_load_screen(int screen_id);

/* Get tile at tile coordinates (bounds-checked) */
int world_get_tile(int tx, int ty);

/* Get doors/NPCs/items for current screen */
const door_t *world_find_door(int tile_x, int tile_y);
const npc_spawn_t *world_get_npc_spawns(int *count);
const item_spawn_t *world_get_item_spawns(int *count);

#endif /* WORLD_H */
