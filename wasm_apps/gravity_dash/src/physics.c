/*
 * Copyright (c) 2025 AkiraOS Contributors
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * @file physics.c
 * @brief 2D physics engine: gravity integration, AABB tilemap collision.
 *        No dynamic allocation. All state lives in the caller-provided Game struct.
 */

#include "../include/game.h"
#include "../include/akira_api.h"

#include <math.h> /* sinf, cosf, fabsf — provided by wasi-libc or stub */

/* ── Forward declarations (internal) ───────────────────────────────────── */
static int tile_at(const LevelData *lvl, int tx, int ty);
static int is_solid_tile(int id);
static void resolve_x(PhysBody *b, const LevelData *lvl);
static void resolve_y(PhysBody *b, const LevelData *lvl);

/* ── Public: integrate physics one frame ────────────────────────────────── */
void physics_step(PhysBody *b, LevelData *lvl, float angle, float dt)
{
    float gx;
    float gy;
    float raw_angle;

    /* Apply IMU deadzone — suppress micro-jitter when near-upright */
    raw_angle = (fabsf(angle) < ANGLE_DEADZONE) ? 0.0f : angle;

    /* Decompose gravity vector from tilt angle */
    gx = sinf(raw_angle) * GRAVITY_BASE;
    gy = cosf(raw_angle) * GRAVITY_BASE;

    /* Integrate velocity */
    b->vx += gx * dt;
    b->vy += gy * dt;

    /* Air friction prevents runaway speed on prolonged tilt */
    b->vx *= AIR_FRICTION;
    b->vy *= AIR_FRICTION;

    /* Hard velocity cap */
    b->vx = clampf(b->vx, -VEL_MAX, VEL_MAX);
    b->vy = clampf(b->vy, -VEL_MAX, VEL_MAX);

    /* Integrate position */
    b->x += b->vx * dt;
    b->y += b->vy * dt;

    /* Clamp player to playfield boundaries before collision */
    if (b->x < 0.0f)
    {
        b->x = 0.0f;
        b->vx = 0.0f;
    }
    if (b->x + PLAYER_W > (float)DISP_W)
    {
        b->x = (float)(DISP_W - PLAYER_W);
        b->vx = 0.0f;
    }
    if (b->y < 0.0f)
    {
        b->y = 0.0f;
        b->vy = 0.0f;
    }
    if (b->y + PLAYER_H > (float)PLAYFIELD_H)
    {
        b->y = (float)(PLAYFIELD_H - PLAYER_H);
        b->vy = 0.0f;
    }

    /* AABB vs tilemap — X axis first, then Y axis */
    resolve_x(b, lvl);
    resolve_y(b, lvl);
}

/* ── Public: check if player overlaps a spike tile ─────────────────────── */
int physics_on_spike(const PhysBody *b, const LevelData *lvl)
{
    int tx0;
    int ty0;
    int tx1;
    int ty1;
    int tx;
    int ty;

    /* Tiles spanned by player AABB */
    tx0 = (int)b->x / TILE_SIZE;
    ty0 = (int)b->y / TILE_SIZE;
    tx1 = (int)(b->x + PLAYER_W - 1.0f) / TILE_SIZE;
    ty1 = (int)(b->y + PLAYER_H - 1.0f) / TILE_SIZE;

    for (ty = ty0; ty <= ty1; ty++)
    {
        for (tx = tx0; tx <= tx1; tx++)
        {
            if (tile_at(lvl, tx, ty) == TILE_SPIKE)
            {
                return 1;
            }
        }
    }
    return 0;
}

/* ── Public: check if player overlaps the EXIT tile ────────────────────── */
int physics_on_exit(const PhysBody *b, const LevelData *lvl)
{
    int tx0;
    int ty0;
    int tx1;
    int ty1;
    int tx;
    int ty;

    tx0 = (int)b->x / TILE_SIZE;
    ty0 = (int)b->y / TILE_SIZE;
    tx1 = (int)(b->x + PLAYER_W - 1.0f) / TILE_SIZE;
    ty1 = (int)(b->y + PLAYER_H - 1.0f) / TILE_SIZE;

    for (ty = ty0; ty <= ty1; ty++)
    {
        for (tx = tx0; tx <= tx1; tx++)
        {
            if (tile_at(lvl, tx, ty) == TILE_EXIT)
            {
                return 1;
            }
        }
    }
    return 0;
}

/* ── Public: collect coins the player overlaps; returns count taken ──────── */
int physics_collect_coins(const PhysBody *b, LevelData *lvl, uint8_t *coin_taken)
{
    int tx0;
    int ty0;
    int tx1;
    int ty1;
    int tx;
    int ty;
    int idx;
    int taken;

    tx0 = (int)b->x / TILE_SIZE;
    ty0 = (int)b->y / TILE_SIZE;
    tx1 = (int)(b->x + PLAYER_W - 1.0f) / TILE_SIZE;
    ty1 = (int)(b->y + PLAYER_H - 1.0f) / TILE_SIZE;
    taken = 0;

    for (ty = ty0; ty <= ty1; ty++)
    {
        for (tx = tx0; tx <= tx1; tx++)
        {
            idx = ty * TILES_W + tx;
            if (idx < 0 || idx >= LEVEL_CELLS)
            {
                continue;
            }
            if (lvl->map[idx] == TILE_COIN && !coin_taken[idx])
            {
                coin_taken[idx] = 1;
                taken++;
            }
        }
    }
    return taken;
}

/* ── Internal: safe tile lookup ─────────────────────────────────────────── */
static int tile_at(const LevelData *lvl, int tx, int ty)
{
    /* Out-of-bounds treated as solid to prevent tunnelling at edges */
    if (tx < 0 || tx >= TILES_W || ty < 0 || ty >= TILES_H)
    {
        return TILE_SOLID;
    }
    return lvl->map[ty * TILES_W + tx];
}

/* ── Internal: does this tile block movement? ───────────────────────────── */
static int is_solid_tile(int id)
{
    /* Platforms are also solid for collision purposes */
    return (id == TILE_SOLID || id == TILE_PLATFORM);
}

/* ── Internal: resolve horizontal overlap with solid tiles ──────────────── */
static void resolve_x(PhysBody *b, const LevelData *lvl)
{
    int tx0;
    int ty0;
    int tx1;
    int ty1;
    int tx;
    int ty;
    int id;

    tx0 = (int)b->x / TILE_SIZE;
    ty0 = (int)b->y / TILE_SIZE;
    tx1 = (int)(b->x + PLAYER_W - 1.0f) / TILE_SIZE;
    ty1 = (int)(b->y + PLAYER_H - 1.0f) / TILE_SIZE;

    for (ty = ty0; ty <= ty1; ty++)
    {
        for (tx = tx0; tx <= tx1; tx++)
        {
            id = tile_at(b->vx >= 0.0f
                             ? tx  /* moving right — check rightmost tile */
                             : tx, /* moving left  — check leftmost tile  */
                         ty);
            if (!is_solid_tile(id))
            {
                continue;
            }

            /* Push player out of tile along X */
            if (b->vx > 0.0f)
            {
                /* Moving right: push left edge of tile */
                b->x = (float)(tx * TILE_SIZE - PLAYER_W);
            }
            else if (b->vx < 0.0f)
            {
                /* Moving left: push right edge of tile */
                b->x = (float)((tx + 1) * TILE_SIZE);
            }
            b->vx = 0.0f;
            return;
        }
    }
}

/* ── Internal: resolve vertical overlap with solid tiles ────────────────── */
static void resolve_y(PhysBody *b, const LevelData *lvl)
{
    int tx0;
    int ty0;
    int tx1;
    int ty1;
    int tx;
    int ty;
    int id;

    tx0 = (int)b->x / TILE_SIZE;
    ty0 = (int)b->y / TILE_SIZE;
    tx1 = (int)(b->x + PLAYER_W - 1.0f) / TILE_SIZE;
    ty1 = (int)(b->y + PLAYER_H - 1.0f) / TILE_SIZE;

    for (tx = tx0; tx <= tx1; tx++)
    {
        for (ty = ty0; ty <= ty1; ty++)
        {
            id = tile_at(tx, ty);
            if (!is_solid_tile(id))
            {
                continue;
            }

            /* Push player out of tile along Y */
            if (b->vy > 0.0f)
            {
                /* Moving down: push above tile */
                b->y = (float)(ty * TILE_SIZE - PLAYER_H);
            }
            else if (b->vy < 0.0f)
            {
                /* Moving up: push below tile */
                b->y = (float)((ty + 1) * TILE_SIZE);
            }
            b->vy = 0.0f;
            return;
        }
    }
}
