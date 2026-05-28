/*
 * Copyright (c) 2025 AkiraOS Contributors
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * @file renderer.c
 * @brief Tile-based renderer with dirty-tile optimisation, HUD, and screen
 *        effects (EXIT pulse, flash).  All drawing uses akira_display_rect /
 *        akira_display_pixel; final frame is committed with akira_display_flush.
 *
 * Dirty-tile strategy: each 16×16 tile cell is flagged dirty when its content
 * changes (player enters/leaves, platform moves, coin collected, EXIT pulses).
 * Only dirty cells are redrawn per frame, keeping the frame budget well under
 * the 33 ms target on most frames.
 */

#include "../include/game.h"
#include "../include/akira_api.h"

#include <math.h> /* sinf, cosf */

/* ── Forward declarations ────────────────────────────────────────────────── */
static void draw_tile(int tx, int ty, const LevelData *lvl,
                      const uint8_t *coin_taken, uint8_t exit_pulse);
static void draw_player(const PhysBody *b);
static void draw_hud(int coins_collected, int coins_total,
                     int level, float angle);
static void draw_glyph(int x, int y, int glyph_idx, uint16_t color);
static void draw_digit(int x, int y, int digit, uint16_t color);
static void draw_arrow(int cx, int cy, float angle);
static void draw_circle_8(int cx, int cy, int r, uint16_t color);

/* ── Mark area occupied by a body's tile footprint dirty ─────────────────── */
static void mark_body_dirty(uint8_t *dirty, const PhysBody *b)
{
    int tx0;
    int ty0;
    int tx1;
    int ty1;
    int tx;
    int ty;

    tx0 = clampi((int)b->x / TILE_SIZE, 0, TILES_W - 1);
    ty0 = clampi((int)b->y / TILE_SIZE, 0, TILES_H - 1);
    tx1 = clampi((int)(b->x + PLAYER_W - 1) / TILE_SIZE, 0, TILES_W - 1);
    ty1 = clampi((int)(b->y + PLAYER_H - 1) / TILE_SIZE, 0, TILES_H - 1);

    for (ty = ty0; ty <= ty1; ty++)
    {
        for (tx = tx0; tx <= tx1; tx++)
        {
            dirty[ty * TILES_W + tx] = 1;
        }
    }
}

/* ── Public: mark all tiles dirty (full redraw) ──────────────────────────── */
void renderer_mark_all_dirty(uint8_t *dirty)
{
    int i;

    for (i = 0; i < DIRTY_TILES; i++)
    {
        dirty[i] = 1;
    }
}

/* ── Public: mark tiles affected by a moving platform dirty ──────────────── */
void renderer_mark_platform_dirty(uint8_t *dirty, int tile_idx)
{
    int tx;
    int ty;

    if (tile_idx < 0 || tile_idx >= LEVEL_CELLS)
    {
        return;
    }

    tx = tile_idx % TILES_W;
    ty = tile_idx / TILES_W;

    /* Also dirty the neighboring tile since platform moves into it */
    if (tx > 0)
    {
        dirty[ty * TILES_W + (tx - 1)] = 1;
    }
    dirty[tile_idx] = 1;
    if (tx < TILES_W - 1)
    {
        dirty[ty * TILES_W + (tx + 1)] = 1;
    }
}

/* ── Public: main render pass ────────────────────────────────────────────── */
void renderer_draw_frame(const Game *g_state, const LevelData *lvl,
                         const uint8_t *coin_taken)
{
    int tx;
    int ty;
    int idx;

    /* Redraw only dirty tiles */
    for (ty = 0; ty < TILES_H; ty++)
    {
        for (tx = 0; tx < TILES_W; tx++)
        {
            idx = ty * TILES_W + tx;
            if (g_state->dirty[idx])
            {
                draw_tile(tx, ty, lvl, coin_taken, g_state->exit_pulse);
            }
        }
    }

    /* Player is always redrawn (small cost, avoids complex dirty tracking) */
    draw_player(&g_state->player);

    /* HUD strip is always redrawn */
    draw_hud(g_state->coins_collected, lvl->coin_total,
             g_state->current_level, g_state->gravity_angle);

    akira_display_flush();
}

/* ── Public: update dirty map for next frame ─────────────────────────────── */
void renderer_update_dirty(uint8_t *dirty, const PhysBody *prev_player,
                           const PhysBody *cur_player)
{
    int i;

    /* Clear all dirty flags from last frame */
    for (i = 0; i < DIRTY_TILES; i++)
    {
        dirty[i] = 0;
    }

    /* Mark tiles the player was in */
    mark_body_dirty(dirty, prev_player);

    /* Mark tiles the player is now in */
    mark_body_dirty(dirty, cur_player);
}

/* ── Public: mark EXIT tile dirty every pulse tick ───────────────────────── */
void renderer_mark_exit_dirty(uint8_t *dirty, const LevelData *lvl)
{
    int i;

    for (i = 0; i < LEVEL_CELLS; i++)
    {
        if (lvl->map[i] == TILE_EXIT)
        {
            dirty[i] = 1;
        }
    }
}

/* ── Public: mark a coin as collected (dirty its tile) ───────────────────── */
void renderer_mark_coin_dirty(uint8_t *dirty, int tile_idx)
{
    if (tile_idx >= 0 && tile_idx < LEVEL_CELLS)
    {
        dirty[tile_idx] = 1;
    }
}

/* ── Public: flash screen white N times (blocking — called in transitions) ── */
void renderer_flash(int times)
{
    int i;

    for (i = 0; i < times; i++)
    {
        akira_display_fill(COLOR_WHITE);
        akira_display_flush();
        akira_display_fill(COLOR_SKY);
        akira_display_flush();
    }
}

/* ── Public: draw title screen ───────────────────────────────────────────── */
void renderer_draw_title(uint8_t blink_on)
{
    /* "GRAVITYDASH" centered — 11 chars × (4+1 px) = 55 px wide */
    static const uint8_t title_str[] = {
        16, 27, 10, 28, 18, 29, 24, 13, 10, 28, 17 /* G R A V I T Y D A S H */
    };
    static const uint8_t sub_str[] = {
        29, 18, 21, 29, 29, 14 /* T I L T T O */
    };
    static const uint8_t sub2_str[] = {
        28, 29, 10, 27, 29 /* S T A R T */
    };

    int i;
    int x;

    akira_display_fill(COLOR_SKY);

    /* Draw title glyphs */
    x = (DISP_W - (int)(sizeof(title_str)) * 5) / 2;
    for (i = 0; i < (int)sizeof(title_str); i++)
    {
        draw_glyph(x + i * 5, 40, title_str[i], COLOR_WHITE);
    }

    /* Blink "tilt to start" at 1 Hz */
    if (blink_on)
    {
        x = (DISP_W - (int)(sizeof(sub_str)) * 5) / 2;
        for (i = 0; i < (int)sizeof(sub_str); i++)
        {
            draw_glyph(x + i * 5, 56, sub_str[i], COLOR_COIN);
        }
        x = (DISP_W - (int)(sizeof(sub2_str)) * 5) / 2;
        for (i = 0; i < (int)sizeof(sub2_str); i++)
        {
            draw_glyph(x + i * 5, 64, sub2_str[i], COLOR_COIN);
        }
    }

    akira_display_flush();
}

/* ── Public: draw game-complete screen ───────────────────────────────────── */
void renderer_draw_game_complete(int coins, int total)
{
    /* "COMPLETE" */
    static const uint8_t msg[] = {
        12, 24, 22, 25, 21, 14, 29, 14 /* C O M P L E T E */
    };
    /* "PRESS A" */
    static const uint8_t press[] = {
        25, 27, 14, 28, 28 /* P R E S S */
    };
    static const uint8_t press2[] = {
        10 /* A */
    };

    int i;
    int x;

    akira_display_fill(COLOR_BLACK);

    x = (DISP_W - (int)(sizeof(msg)) * 5) / 2;
    for (i = 0; i < (int)sizeof(msg); i++)
    {
        draw_glyph(x + i * 5, 44, msg[i], COLOR_WHITE);
    }

    /* Coin total: draw two digits */
    draw_digit(DISP_W / 2 - 12, 56, coins / 10, COLOR_COIN);
    draw_digit(DISP_W / 2 - 6, 56, coins % 10, COLOR_COIN);
    draw_glyph(DISP_W / 2 - 2, 56, 35, COLOR_GRAY); /* '/' → Z placeholder */
    draw_digit(DISP_W / 2 + 2, 56, total / 10, COLOR_GRAY);
    draw_digit(DISP_W / 2 + 8, 56, total % 10, COLOR_GRAY);

    x = (DISP_W - (int)(sizeof(press)) * 5) / 2;
    for (i = 0; i < (int)sizeof(press); i++)
    {
        draw_glyph(x + i * 5, 70, press[i], COLOR_WHITE);
    }
    x = (DISP_W - (int)(sizeof(press2)) * 5) / 2;
    for (i = 0; i < (int)sizeof(press2); i++)
    {
        draw_glyph(x + i * 5, 78, press2[i], COLOR_WHITE);
    }

    akira_display_flush();
}

/* ── Internal: draw one tile cell ────────────────────────────────────────── */
static void draw_tile(int tx, int ty, const LevelData *lvl,
                      const uint8_t *coin_taken, uint8_t exit_pulse)
{
    int idx;
    int px;
    int py;
    uint8_t tile_id;
    uint16_t color;
    int cx;
    int cy;

    idx = ty * TILES_W + tx;
    px = tx * TILE_SIZE;
    py = ty * TILE_SIZE;
    tile_id = lvl->map[idx];

    switch (tile_id)
    {
    case TILE_AIR:
        /* Erase to sky background */
        akira_display_rect(px, py, TILE_SIZE, TILE_SIZE, COLOR_SKY);
        break;

    case TILE_SOLID:
        akira_display_rect(px, py, TILE_SIZE, TILE_SIZE, COLOR_SOLID);
        break;

    case TILE_COIN:
        /* Only draw if not collected */
        akira_display_rect(px, py, TILE_SIZE, TILE_SIZE, COLOR_SKY);
        if (!coin_taken[idx])
        {
            /* 8×8 circle centred in tile */
            cx = px + TILE_SIZE / 2;
            cy = py + TILE_SIZE / 2;
            draw_circle_8(cx, cy, 4, COLOR_COIN);
        }
        break;

    case TILE_EXIT:
    {
        /* Pulse brightness ±10% at 2 Hz using exit_pulse counter 0..15 */
        uint32_t bright = (exit_pulse < 8) ? (0x801F + exit_pulse * 2)
                                           : (0x801F + (16 - exit_pulse) * 2);
        /* Clamp to 16-bit */
        color = (uint16_t)(bright & 0xFFFFu);
        akira_display_rect(px, py, TILE_SIZE, TILE_SIZE, color);
        break;
    }

    case TILE_SPIKE:
        /* Red background + small triangle indicator */
        akira_display_rect(px, py, TILE_SIZE, TILE_SIZE, COLOR_SKY);
        /* Draw spike as a filled triangle approximated with three rects */
        akira_display_rect(px + 7, py + 2, 2, 2, COLOR_SPIKE);
        akira_display_rect(px + 5, py + 4, 6, 2, COLOR_SPIKE);
        akira_display_rect(px + 3, py + 6, 10, 4, COLOR_SPIKE);
        akira_display_rect(px + 1, py + 10, 14, 4, COLOR_SPIKE);
        break;

    case TILE_PLATFORM:
        akira_display_rect(px, py, TILE_SIZE, TILE_SIZE, COLOR_PLATFORM);
        break;

    default:
        /* Unknown tile — draw as air */
        akira_display_rect(px, py, TILE_SIZE, TILE_SIZE, COLOR_SKY);
        break;
    }
}

/* ── Internal: draw player rectangle ────────────────────────────────────── */
static void draw_player(const PhysBody *b)
{
    int px;
    int py;

    px = (int)b->x;
    py = (int)b->y;

    akira_display_rect(px, py, PLAYER_W, PLAYER_H, COLOR_PLAYER);
}

/* ── Internal: draw HUD strip (bottom 7 px) ──────────────────────────────── */
static void draw_hud(int coins_collected, int coins_total,
                     int level, float angle)
{
    int hud_y;
    int i;

    hud_y = PLAYFIELD_H; /* 128 */

    /* Background */
    akira_display_rect(0, hud_y, DISP_W, HUD_H, COLOR_HUD_BG);

    /* Gravity arrow — 6 px line from left-side anchor */
    draw_arrow(16, hud_y + 3, angle);

    /* Level dot indicators centred */
    for (i = 0; i < NUM_LEVELS; i++)
    {
        uint16_t dot_color = (i == level) ? COLOR_WHITE : COLOR_GRAY;
        int dot_x = DISP_W / 2 - (NUM_LEVELS * 5 / 2) + i * 5;
        akira_display_rect(dot_x, hud_y + 2, 3, 3, dot_color);
    }

    /* Coin counter on the right — two digits / two digits */
    draw_digit(DISP_W - 26, hud_y + 1, coins_collected / 10, COLOR_COIN);
    draw_digit(DISP_W - 20, hud_y + 1, coins_collected % 10, COLOR_COIN);
    /* Separator pixel */
    akira_display_pixel(DISP_W - 15, hud_y + 3, COLOR_GRAY);
    draw_digit(DISP_W - 12, hud_y + 1, coins_total / 10, COLOR_GRAY);
    draw_digit(DISP_W - 6, hud_y + 1, coins_total % 10, COLOR_GRAY);
}

/* ── Internal: draw gravity direction arrow ──────────────────────────────── */
static void draw_arrow(int cx, int cy, float angle)
{
    /* 6 px arrow from center outward in direction of gravity */
    int dx;
    int dy;
    int ax;
    int ay;
    int i;

    dx = (int)(sinf(angle) * 6.0f);
    dy = (int)(cosf(angle) * 6.0f);

    /* Draw line of pixels from center toward gravity direction */
    for (i = 1; i <= 6; i++)
    {
        ax = cx + (dx * i) / 6;
        ay = cy + (dy * i) / 6;
        if (ax >= 0 && ax < DISP_W && ay >= PLAYFIELD_H && ay < DISP_H)
        {
            akira_display_pixel(ax, ay, COLOR_ARROW);
        }
    }
}

/* ── Internal: draw a glyph from g_font at pixel position ───────────────── */
static void draw_glyph(int x, int y, int glyph_idx, uint16_t color)
{
    int row;
    int col;
    uint8_t bits;

    if (glyph_idx < 0 || glyph_idx >= 36)
    {
        return;
    }

    for (row = 0; row < 5; row++)
    {
        bits = g_font[glyph_idx][row];
        for (col = 0; col < 4; col++)
        {
            /* Bit 3 is leftmost column */
            if (bits & (0x8u >> col))
            {
                akira_display_pixel(x + col, y + row, color);
            }
        }
    }
}

/* ── Internal: draw a single decimal digit (convenience wrapper) ─────────── */
static void draw_digit(int x, int y, int digit, uint16_t color)
{
    if (digit < 0 || digit > 9)
    {
        return;
    }
    draw_glyph(x, y, digit, color);
}

/* ── Internal: draw a filled 8-pixel-diameter circle (coin) ─────────────── */
static void draw_circle_8(int cx, int cy, int r, uint16_t color)
{
    int px;
    int py;
    int dx;
    int dy;

    /* Scan bounding box; include pixel if within radius */
    for (dy = -r; dy <= r; dy++)
    {
        for (dx = -r; dx <= r; dx++)
        {
            if (dx * dx + dy * dy <= r * r)
            {
                px = cx + dx;
                py = cy + dy;
                if (px >= 0 && px < DISP_W && py >= 0 && py < PLAYFIELD_H)
                {
                    akira_display_pixel(px, py, color);
                }
            }
        }
    }
}
