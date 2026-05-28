/*
 * Copyright (c) 2025 AkiraOS Contributors
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * @file levels.c
 * @brief Three level definitions as static tile maps (uint8_t[15*8]).
 *
 * Tile legend:
 *   0 = AIR   1 = SOLID   2 = COIN   3 = EXIT
 *   4 = SPIKE 5 = PLATFORM (moves horizontally)
 *
 * Grid is stored row-major, top→bottom, left→right.
 * Each row is 15 tiles wide; 8 rows tall.
 */

#include "../include/game.h"

/* ─────────────────────────────────────────────────────────────────────────
 * Level 1 — Introduction
 *   Flat ground, 5 coins in a row, EXIT on the right side.
 *   No gravity rotation needed — teaches basic movement.
 * ───────────────────────────────────────────────────────────────────────── */
static const uint8_t lvl1_map[LEVEL_CELLS] = {
    /* row 0 (top) */
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    /* row 1 */
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    /* row 2 */
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    /* row 3 — coins hover above ground */
    0,
    0,
    2,
    0,
    2,
    0,
    2,
    0,
    2,
    0,
    2,
    0,
    0,
    3,
    0,
    /* row 4 — coins row (same columns align over ground gap) */
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    /* row 5 — main ground platform */
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    0,
    1,
    /* row 6 — ground */
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    /* row 7 (bottom) — ground */
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
};

/* ─────────────────────────────────────────────────────────────────────────
 * Level 2 — Gravity Flip
 *   Platform above the EXIT; player must tilt ~90° right to "fall" onto it.
 *   3 coins accessible only via tilted gravity.
 *   One spike at the bottom as punishment for over-tilting.
 * ───────────────────────────────────────────────────────────────────────── */
static const uint8_t lvl2_map[LEVEL_CELLS] = {
    /* row 0 */
    1,
    1,
    1,
    1,
    1,
    0,
    0,
    0,
    0,
    0,
    0,
    1,
    1,
    1,
    1,
    /* row 1 — elevated coin pocket reachable via side gravity */
    1,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    2,
    0,
    0,
    0,
    0,
    1,
    /* row 2 */
    1,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    2,
    0,
    0,
    0,
    0,
    1,
    /* row 3 — EXIT is on upper section, requires sideways fall */
    1,
    0,
    0,
    0,
    3,
    0,
    0,
    0,
    0,
    2,
    0,
    0,
    0,
    0,
    1,
    /* row 4 — dividing ledge with gap */
    1,
    1,
    1,
    1,
    0,
    1,
    1,
    1,
    0,
    1,
    1,
    1,
    1,
    1,
    1,
    /* row 5 — lower area, spike in middle */
    1,
    0,
    0,
    0,
    0,
    0,
    4,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    1,
    /* row 6 — floor */
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    /* row 7 */
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
};

/* ─────────────────────────────────────────────────────────────────────────
 * Level 3 — Puzzle Maze
 *   Enclosed rooms requiring 3 distinct gravity rotations.
 *   Moving platforms (tile 5) block direct paths.
 *   8 coins, 4 spikes, EXIT behind a gravity-locked passage.
 *   Designed for 20–40 seconds of play.
 * ───────────────────────────────────────────────────────────────────────── */
static const uint8_t lvl3_map[LEVEL_CELLS] = {
    /* row 0 — top wall */
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    /* row 1 — upper corridor with coins */
    1,
    2,
    0,
    0,
    1,
    0,
    0,
    0,
    1,
    2,
    0,
    0,
    0,
    2,
    1,
    /* row 2 — first maze wall row */
    1,
    0,
    1,
    0,
    1,
    4,
    1,
    0,
    1,
    0,
    1,
    1,
    1,
    0,
    1,
    /* row 3 — middle section: moving platform + coins + spike */
    1,
    0,
    1,
    2,
    0,
    0,
    5,
    2,
    0,
    0,
    4,
    0,
    0,
    0,
    1,
    /* row 4 — second divider with spike pits */
    1,
    1,
    1,
    0,
    1,
    1,
    0,
    1,
    1,
    0,
    1,
    0,
    1,
    1,
    1,
    /* row 5 — lower corridor to EXIT, guarded by spikes + moving platform */
    1,
    0,
    0,
    0,
    4,
    0,
    5,
    0,
    0,
    2,
    0,
    0,
    0,
    3,
    1,
    /* row 6 — floor with spike pit */
    1,
    1,
    1,
    0,
    1,
    1,
    1,
    1,
    0,
    1,
    1,
    1,
    1,
    1,
    1,
    /* row 7 — bottom wall */
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
};

/* ─────────────────────────────────────────────────────────────────────────
 * Exported level table
 * ───────────────────────────────────────────────────────────────────────── */
LevelData g_levels[NUM_LEVELS] = {
    /* Level 1 — Introduction */
    {
        /* map: */ {0}, /* filled below via initialiser */
        /* spawn_x: */ 8.0f,
        /* spawn_y: */ 64.0f, /* row 4, floating above ground */
        /* coin_total: */ 5,
        /* platforms: */ {{0, 0, 0.0f}},
        /* platform_count: */ 0,
    },
    /* Level 2 — Gravity Flip */
    {
        {0},
        /* spawn_x: */ 16.0f,
        /* spawn_y: */ 80.0f, /* lower area row 5 */
        /* coin_total: */ 3,
        /* platforms: */ {{0, 0, 0.0f}},
        /* platform_count: */ 0,
    },
    /* Level 3 — Puzzle Maze */
    {
        {0},
        /* spawn_x: */ 8.0f,
        /* spawn_y: */ 16.0f, /* upper corridor */
        /* coin_total: */ 8,
        /* platforms: */ {
            {3 * TILES_W + 6, +1, 0.0f}, /* row 3, col 6 */
            {5 * TILES_W + 6, -1, 0.0f}, /* row 5, col 6 */
        },
        /* platform_count: */ 2,
    },
};

/* ── Copy static map arrays into LevelData structs at startup ───────────── */
void levels_init(void)
{
    int i;

    /* Level 1 */
    for (i = 0; i < LEVEL_CELLS; i++)
    {
        g_levels[0].map[i] = lvl1_map[i];
    }

    /* Level 2 */
    for (i = 0; i < LEVEL_CELLS; i++)
    {
        g_levels[1].map[i] = lvl2_map[i];
    }

    /* Level 3 */
    for (i = 0; i < LEVEL_CELLS; i++)
    {
        g_levels[2].map[i] = lvl3_map[i];
    }
}

/* ── Count coins in a map ───────────────────────────────────────────────── */
int levels_count_coins(int level_idx)
{
    int count;
    int i;

    if (level_idx < 0 || level_idx >= NUM_LEVELS)
    {
        return 0;
    }

    count = 0;
    for (i = 0; i < LEVEL_CELLS; i++)
    {
        if (g_levels[level_idx].map[i] == TILE_COIN)
        {
            count++;
        }
    }
    return count;
}
