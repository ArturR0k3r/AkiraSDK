/*
 * levels.c — GR4V level tilemaps (3 levels, 15×8 each).
 * Tile key: 0=air 1=solid 2=shard 3=exit 4=spike 5=dissolve
 *           6=anchor 7=void 8=glitch
 * Each level has 8 shards (T_SHARD tiles).
 */
#include "game.h"

/* Level names shown in HUD */
const char *level_name[NUM_LEVELS] = {
    "0x0001::BOOT",
    "0x0002::LOOP",
    "0x0003::HEAP",
};

/* Spawn positions (col, row) — player standing ON this row */
const uint8_t level_spawn_col[NUM_LEVELS] = { 0, 1, 1 };
const uint8_t level_spawn_row[NUM_LEVELS] = { 6, 5, 6 };

/*
 * Level 0 — 0x0001::BOOT
 * Horizontal traversal. Teaches basic tilt.
 * Shards: 3 plain-sight, 5 require tilt to reach upper platforms.
 */
const uint8_t level_data[NUM_LEVELS][GRID_H][GRID_W] = {
{
    /* row 0 */ { 0,0,0,2,0,0,2,0,0,2,0,0,0,0,0 },
    /* row 1 */ { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 },
    /* row 2 */ { 0,0,0,0,0,0,0,0,0,0,0,0,0,2,0 },
    /* row 3 */ { 0,0,0,0,0,0,0,0,0,0,1,1,1,1,1 },
    /* row 4 */ { 0,0,2,0,0,0,0,0,2,0,0,0,0,0,0 },
    /* row 5 */ { 0,0,0,1,1,1,1,1,1,0,0,0,0,0,0 },
    /* row 6 */ { 0,2,0,0,0,0,0,0,0,0,0,0,2,0,0 },
    /* row 7 */ { 1,1,1,1,1,0,0,0,1,1,1,1,1,1,3 },
},
/*
 * Level 1 — 0x0002::LOOP
 * Gravity flip required. Dissolving platforms and glitch zone.
 */
{
    /* row 0 */ { 0,0,0,0,0,0,8,8,8,8,8,8,0,0,0 },
    /* row 1 */ { 0,0,0,0,0,0,8,2,0,0,2,8,0,0,0 },
    /* row 2 */ { 0,0,5,5,5,0,8,0,0,0,0,8,1,1,1 },
    /* row 3 */ { 0,0,0,0,0,0,8,8,8,8,8,8,0,0,2 },
    /* row 4 */ { 0,0,0,0,0,4,4,0,0,0,0,0,1,1,1 },
    /* row 5 */ { 0,2,0,0,1,1,1,0,0,6,0,0,0,0,0 },
    /* row 6 */ { 1,1,0,0,0,0,0,0,0,0,0,0,0,0,0 },
    /* row 7 */ { 0,0,0,0,0,0,0,0,0,2,0,0,0,0,3 },
},
/*
 * Level 2 — 0x0003::HEAP
 * Full 360° gravity. Void floors. Multiple anchors required.
 */
{
    /* row 0 */ { 1,1,1,0,0,0,0,0,0,0,0,0,2,1,1 },
    /* row 1 */ { 1,2,0,0,7,7,7,0,0,0,5,5,5,0,1 },
    /* row 2 */ { 1,0,4,0,7,0,7,0,0,0,0,0,0,0,1 },
    /* row 3 */ { 1,0,0,0,7,0,7,0,6,0,0,0,0,2,1 },
    /* row 4 */ { 1,0,0,0,7,7,7,0,0,1,1,1,0,0,1 },
    /* row 5 */ { 1,0,1,1,1,0,0,0,0,0,0,1,0,0,1 },
    /* row 6 */ { 1,2,0,0,0,4,4,0,0,0,0,0,0,2,1 },
    /* row 7 */ { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,3 },
},
};
