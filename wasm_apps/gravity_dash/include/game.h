/*
 * Copyright (c) 2025 AkiraOS Contributors
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * @file game.h
 * @brief Shared types, constants, and state definitions for gravity_dash.
 */

#ifndef GAME_H
#define GAME_H

#include <stdint.h>

/* ── Display geometry ───────────────────────────────────────────────────── */
#define DISP_W 240
#define DISP_H 135

/* ── Tile grid ──────────────────────────────────────────────────────────── */
#define TILE_SIZE 16
#define TILES_W 15                        /* 15 * 16 = 240 px */
#define TILES_H 8                         /* 8  * 16 = 128 px (playfield) */
#define HUD_H 7                           /* bottom strip */
#define PLAYFIELD_H (TILES_H * TILE_SIZE) /* 128 px */

/* ── Tile IDs ───────────────────────────────────────────────────────────── */
#define TILE_AIR 0
#define TILE_SOLID 1
#define TILE_COIN 2
#define TILE_EXIT 3
#define TILE_SPIKE 4
#define TILE_PLATFORM 5

/* ── Tile RGB565 colors ─────────────────────────────────────────────────── */
#define COLOR_SOLID 0x6B4Du    /* brown/tan ground    */
#define COLOR_COIN 0xFFE0u     /* yellow              */
#define COLOR_EXIT 0x801Fu     /* purple              */
#define COLOR_SPIKE 0xF800u    /* red                 */
#define COLOR_PLATFORM 0x4C92u /* cyan                */
#define COLOR_SKY 0x0410u      /* dark blue background */
#define COLOR_PLAYER 0x2D47u   /* Yoshi green         */
#define COLOR_HUD_BG 0x0000u   /* HUD strip black     */
#define COLOR_WHITE 0xFFFFu
#define COLOR_BLACK 0x0000u
#define COLOR_GRAY 0x7BEFu
#define COLOR_ARROW 0xFFE0u /* gravity arrow yellow */

/* ── Player geometry ────────────────────────────────────────────────────── */
#define PLAYER_W 8
#define PLAYER_H 10

/* ── Physics constants ──────────────────────────────────────────────────── */
#define GRAVITY_BASE 980.0f  /* px/s² */
#define ANGLE_DEADZONE 0.04f /* radians, suppress IMU jitter */
#define AIR_FRICTION 0.985f
#define VEL_MAX 300.0f
#define FALLBACK_ANGLE 0.2618f /* 15 degrees in radians */

/* ── Timing ─────────────────────────────────────────────────────────────── */
#define TARGET_FPS 30
#define FRAME_MS (1000u / TARGET_FPS) /* 33 ms */

/* ── Level counts ───────────────────────────────────────────────────────── */
#define NUM_LEVELS 3
#define LEVEL_CELLS (TILES_W * TILES_H) /* 120 cells */

/* ── Input bit masks ────────────────────────────────────────────────────── */
#define BTN_A (1u << 0)
#define BTN_B (1u << 1)
#define BTN_UP (1u << 2)
#define BTN_DOWN (1u << 3)

/* ── Dirty-tile map ─────────────────────────────────────────────────────── */
#define DIRTY_TILES (TILES_W * TILES_H) /* 120 entries */

/* ── Game state machine ─────────────────────────────────────────────────── */
typedef enum
{
    STATE_TITLE,
    STATE_PLAYING,
    STATE_LEVEL_COMPLETE,
    STATE_GAME_COMPLETE
} AppState;

/* ── Physics body ───────────────────────────────────────────────────────── */
typedef struct
{
    float x;  /* position X (px, sub-pixel) */
    float y;  /* position Y (px, sub-pixel) */
    float vx; /* velocity X (px/s)          */
    float vy; /* velocity Y (px/s)          */
} PhysBody;

/* ── Moving platform state ──────────────────────────────────────────────── */
typedef struct
{
    int tile_idx; /* index into level map (0..119) */
    int dir;      /* +1 or -1 (horizontal direction) */
    float frac;   /* sub-tile fractional offset (0..1) */
} MovPlatform;

#define MAX_MOV_PLATFORMS 4

/* ── Level data ─────────────────────────────────────────────────────────── */
typedef struct
{
    uint8_t map[LEVEL_CELLS]; /* tile IDs             */
    float spawn_x;            /* player spawn X       */
    float spawn_y;            /* player spawn Y       */
    int coin_total;           /* coins in this level  */
    MovPlatform platforms[MAX_MOV_PLATFORMS];
    int platform_count;
} LevelData;

/* ── Global game state ──────────────────────────────────────────────────── */
typedef struct
{
    AppState state;
    int current_level;
    int coins_collected;
    int coins_total_all; /* across all levels */
    PhysBody player;
    float gravity_angle; /* current IMU angle  */
    uint32_t last_tick_ms;
    uint32_t state_enter_ms;             /* when current state was entered */
    uint8_t prev_buttons;                /* last frame button state        */
    uint8_t title_blink;                 /* toggles for 1 Hz blink         */
    uint8_t exit_pulse;                  /* 0..15 counter for EXIT pulse   */
    uint8_t fallback_angle_btn;          /* accumulated fallback angle index */
    int plat_offsets[MAX_MOV_PLATFORMS]; /* pixel offsets for platforms */
    uint8_t dirty[DIRTY_TILES];          /* 1 = tile needs redraw this frame */
    uint8_t coin_taken[LEVEL_CELLS];     /* 1 = coin already collected */
} Game;

/* ── Exported by levels.c ───────────────────────────────────────────────── */
extern LevelData g_levels[NUM_LEVELS];

/* ── Exported by font.c ─────────────────────────────────────────────────── */
/* 36 glyphs: 0-9 then A-Z; each glyph is 5 rows of 4-bit column mask */
extern const uint8_t g_font[36][5];

/* ── Shared game instance (defined in main.c) ───────────────────────────── */
extern Game g;

/* ── Math helpers ───────────────────────────────────────────────────────── */
static inline float clampf(float v, float lo, float hi)
{
    /* Clamp float to [lo, hi] range */
    return (v < lo) ? lo : (v > hi) ? hi
                                    : v;
}

static inline int clampi(int v, int lo, int hi)
{
    /* Clamp int to [lo, hi] range */
    return (v < lo) ? lo : (v > hi) ? hi
                                    : v;
}

#endif /* GAME_H */
