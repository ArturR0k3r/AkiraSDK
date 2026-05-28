/*
 * Copyright (c) 2025 AkiraOS Contributors
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * @file main.c
 * @brief AkiraOS WASM app entry points: app_init / app_tick / app_destroy.
 *        Owns the global Game struct and drives the state machine.
 *        No dynamic allocation — all state is in static storage.
 */

#include "../include/game.h"
#include "../include/akira_api.h"

#include <math.h> /* fabsf */

/* ── Forward declarations for internal functions ─────────────────────────── */
static void state_enter(AppState next);
static void tick_title(uint32_t now_ms);
static void tick_playing(uint32_t now_ms);
static void tick_level_complete(uint32_t now_ms);
static void tick_game_complete(void);
static void load_level(int idx);
static void respawn_player(void);
static void update_platforms(LevelData *lvl);
static uint8_t buttons_pressed(uint8_t mask);

/* ── External function declarations (defined in other TUs) ───────────────── */
/* physics.c */
extern void physics_step(PhysBody *b, LevelData *lvl, float angle, float dt);
extern int physics_on_spike(const PhysBody *b, const LevelData *lvl);
extern int physics_on_exit(const PhysBody *b, const LevelData *lvl);
extern int physics_collect_coins(const PhysBody *b, LevelData *lvl,
                                 uint8_t *coin_taken);
/* renderer.c */
extern void renderer_draw_frame(const Game *g_state, const LevelData *lvl,
                                const uint8_t *coin_taken);
extern void renderer_mark_all_dirty(uint8_t *dirty);
extern void renderer_update_dirty(uint8_t *dirty, const PhysBody *prev,
                                  const PhysBody *cur);
extern void renderer_mark_exit_dirty(uint8_t *dirty, const LevelData *lvl);
extern void renderer_mark_platform_dirty(uint8_t *dirty, int tile_idx);
extern void renderer_mark_coin_dirty(uint8_t *dirty, int tile_idx);
extern void renderer_flash(int times);
extern void renderer_draw_title(uint8_t blink_on);
extern void renderer_draw_game_complete(int coins, int total);

/* levels.c */
extern void levels_init(void);
extern int levels_count_coins(int level_idx);

/* ── Global game instance (single allocation in static memory) ───────────── */
Game g;

/* ── Per-level coin taken flags — cleared on level load ──────────────────── */
static uint8_t s_coin_taken[LEVEL_CELLS];

/* ── Previous player position for dirty tracking ─────────────────────────── */
static PhysBody s_prev_player;

/* ── Accumulated fallback angle (when no IMU, use BTN_UP/DOWN) ───────────── */
static float s_fallback_angle;

/* ─────────────────────────────────────────────────────────────────────────
 * app_init — called once by AkiraOS on module load
 * ───────────────────────────────────────────────────────────────────────── */
WASM_EXPORT void app_init(void)
{
    int i;

    /* Zero entire game struct */
    for (i = 0; i < (int)sizeof(g); i++)
    {
        ((uint8_t *)&g)[i] = 0;
    }
    for (i = 0; i < LEVEL_CELLS; i++)
    {
        s_coin_taken[i] = 0;
    }

    s_fallback_angle = 0.0f;

    /* Initialise static level data into g_levels[] */
    levels_init();

    /* Compute total coins across all levels */
    g.coins_total_all = 0;
    for (i = 0; i < NUM_LEVELS; i++)
    {
        g.coins_total_all += levels_count_coins(i);
        /* Sync coin_total field for level struct */
        g_levels[i].coin_total = levels_count_coins(i);
    }

    g.last_tick_ms = akira_time_ms();

    /* Start on title screen */
    state_enter(STATE_TITLE);
}

/* ─────────────────────────────────────────────────────────────────────────
 * app_tick — called every frame by AkiraOS scheduler (~30 FPS)
 * ───────────────────────────────────────────────────────────────────────── */
WASM_EXPORT void app_tick(void)
{
    uint32_t now_ms;

    now_ms = akira_time_ms();

    switch (g.state)
    {
    case STATE_TITLE:
        tick_title(now_ms);
        break;

    case STATE_PLAYING:
        tick_playing(now_ms);
        break;

    case STATE_LEVEL_COMPLETE:
        tick_level_complete(now_ms);
        break;

    case STATE_GAME_COMPLETE:
        tick_game_complete();
        break;

    default:
        /* Unreachable — but be safe */
        state_enter(STATE_TITLE);
        break;
    }

    g.last_tick_ms = now_ms;
}

/* ─────────────────────────────────────────────────────────────────────────
 * app_destroy — called by AkiraOS on module unload
 * ───────────────────────────────────────────────────────────────────────── */
WASM_EXPORT void app_destroy(void)
{
    /* Nothing to free — all state is static.
     * Silence the display to indicate clean shutdown. */
    akira_display_fill(COLOR_BLACK);
    akira_display_flush();
}

/* ─────────────────────────────────────────────────────────────────────────
 * Internal: state machine transition
 * ───────────────────────────────────────────────────────────────────────── */
static void state_enter(AppState next)
{
    g.state = next;
    g.state_enter_ms = akira_time_ms();

    switch (next)
    {
    case STATE_TITLE:
        renderer_mark_all_dirty(g.dirty);
        renderer_draw_title(1);
        break;

    case STATE_PLAYING:
        load_level(g.current_level);
        renderer_mark_all_dirty(g.dirty);
        break;

    case STATE_LEVEL_COMPLETE:
        /* Flash and beep — handled in tick_level_complete */
        akira_audio_beep(880, 200);
        renderer_flash(3);
        break;

    case STATE_GAME_COMPLETE:
        renderer_draw_game_complete(g.coins_collected, g.coins_total_all);
        break;

    default:
        break;
    }
}

/* ─────────────────────────────────────────────────────────────────────────
 * Internal: title screen tick
 * ───────────────────────────────────────────────────────────────────────── */
static void tick_title(uint32_t now_ms)
{
    uint32_t elapsed;
    uint8_t btn;

    elapsed = now_ms - g.state_enter_ms;

    /* Toggle blink at ~1 Hz (every 500 ms) */
    g.title_blink = (uint8_t)((elapsed / 500u) & 1u);

    renderer_draw_title(g.title_blink);

    /* BTN_A starts the game */
    btn = akira_input_get();
    if (buttons_pressed(BTN_A))
    {
        g.current_level = 0;
        g.coins_collected = 0;
        state_enter(STATE_PLAYING);
    }

    g.prev_buttons = btn;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Internal: playing tick (physics + render)
 * ───────────────────────────────────────────────────────────────────────── */
static void tick_playing(uint32_t now_ms)
{
    float dt;
    uint8_t btn;
    float angle;
    int coins_taken;
    int i;
    LevelData *lvl;

    dt = (float)(now_ms - g.last_tick_ms) * 0.001f;
    /* Clamp dt to max 50 ms (avoid spiral on first frame or lag spike) */
    if (dt > 0.05f)
    {
        dt = 0.05f;
    }
    if (dt <= 0.0f)
    {
        return;
    }

    btn = akira_input_get();
    lvl = &g_levels[g.current_level];

    /* ── Gravity angle: IMU primary, BTN fallback ── */
    angle = akira_imu_gravity_angle();

    /* Fallback: if IMU returns 0 and buttons pressed, accumulate angle */
    if (angle == 0.0f)
    {
        if (btn & BTN_UP)
        {
            s_fallback_angle -= FALLBACK_ANGLE * dt * 3.0f;
        }
        if (btn & BTN_DOWN)
        {
            s_fallback_angle += FALLBACK_ANGLE * dt * 3.0f;
        }
        s_fallback_angle = clampf(s_fallback_angle, -1.5707f, 1.5707f);
        angle = s_fallback_angle;
    }

    g.gravity_angle = angle;

    /* ── Store previous position for dirty tracking ── */
    s_prev_player = g.player;

    /* ── Update moving platforms ── */
    update_platforms(lvl);

    /* ── Physics step ── */
    physics_step(&g.player, lvl, angle, dt);

    /* ── Coin collection ── */
    coins_taken = physics_collect_coins(&g.player, lvl, s_coin_taken);
    if (coins_taken > 0)
    {
        g.coins_collected += coins_taken;
        akira_audio_beep(1047, 80); /* C6 ding */

        /* Mark collected coin tiles dirty */
        for (i = 0; i < LEVEL_CELLS; i++)
        {
            if (s_coin_taken[i] && lvl->map[i] == TILE_COIN)
            {
                renderer_mark_coin_dirty(g.dirty, i);
            }
        }
    }

    /* ── Spike check ── */
    if (physics_on_spike(&g.player, lvl))
    {
        akira_audio_beep(200, 150);
        respawn_player();
        renderer_mark_all_dirty(g.dirty);
        g.prev_buttons = btn;
        return;
    }

    /* ── Exit check ── */
    if (physics_on_exit(&g.player, lvl))
    {
        if (g.current_level + 1 < NUM_LEVELS)
        {
            g.current_level++;
            state_enter(STATE_LEVEL_COMPLETE);
        }
        else
        {
            state_enter(STATE_GAME_COMPLETE);
        }
        g.prev_buttons = btn;
        return;
    }

    /* ── EXIT pulse (2 Hz, 30 fps → toggle every 8 frames) ── */
    g.exit_pulse = (uint8_t)((g.exit_pulse + 1) & 0xFu);
    if ((g.exit_pulse & 0x7u) == 0)
    {
        renderer_mark_exit_dirty(g.dirty, lvl);
    }

    /* ── Update dirty map ── */
    renderer_update_dirty(g.dirty, &s_prev_player, &g.player);

    /* ── Render ── */
    renderer_draw_frame(&g, lvl, s_coin_taken);

    g.prev_buttons = btn;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Internal: level complete transition tick
 * ───────────────────────────────────────────────────────────────────────── */
static void tick_level_complete(uint32_t now_ms)
{
    uint32_t elapsed;

    elapsed = now_ms - g.state_enter_ms;

    /* Wait 1.5 seconds, then load next level */
    if (elapsed >= 1500u)
    {
        state_enter(STATE_PLAYING);
    }
}

/* ─────────────────────────────────────────────────────────────────────────
 * Internal: game complete tick
 * ───────────────────────────────────────────────────────────────────────── */
static void tick_game_complete(void)
{
    /* BTN_A restarts from level 1 */
    if (buttons_pressed(BTN_A))
    {
        g.current_level = 0;
        g.coins_collected = 0;
        state_enter(STATE_PLAYING);
    }

    g.prev_buttons = akira_input_get();
}

/* ─────────────────────────────────────────────────────────────────────────
 * Internal: load a level — resets player, dirty map, and coin flags
 * ───────────────────────────────────────────────────────────────────────── */
static void load_level(int idx)
{
    int i;

    if (idx < 0 || idx >= NUM_LEVELS)
    {
        return;
    }

    /* Clear coin taken flags */
    for (i = 0; i < LEVEL_CELLS; i++)
    {
        s_coin_taken[i] = 0;
    }

    /* Place player at spawn */
    g.player.x = g_levels[idx].spawn_x;
    g.player.y = g_levels[idx].spawn_y;
    g.player.vx = 0.0f;
    g.player.vy = 0.0f;

    s_prev_player = g.player;
    s_fallback_angle = 0.0f;

    renderer_mark_all_dirty(g.dirty);
}

/* ─────────────────────────────────────────────────────────────────────────
 * Internal: respawn player at current level spawn
 * ───────────────────────────────────────────────────────────────────────── */
static void respawn_player(void)
{
    g.player.x = g_levels[g.current_level].spawn_x;
    g.player.y = g_levels[g.current_level].spawn_y;
    g.player.vx = 0.0f;
    g.player.vy = 0.0f;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Internal: update moving platforms (tile type TILE_PLATFORM)
 * Each platform oscillates horizontally within its row.
 * ───────────────────────────────────────────────────────────────────────── */
static void update_platforms(LevelData *lvl)
{
    int i;
    int idx;
    int tx;
    int ty;
    int next_tx;
    int next_idx;
    MovPlatform *p;

    for (i = 0; i < lvl->platform_count; i++)
    {
        p = &lvl->platforms[i];
        idx = p->tile_idx;

        if (idx < 0 || idx >= LEVEL_CELLS)
        {
            continue;
        }

        tx = idx % TILES_W;
        ty = idx / TILES_W;

        /* Platform moves 2 px/frame → one tile per 8 frames via frac */
        p->frac += 0.25f * (float)p->dir; /* 4 steps per tile */

        if (p->frac >= 1.0f || p->frac <= 0.0f)
        {
            /* Move the platform tile to adjacent column */
            next_tx = tx + p->dir;
            next_idx = ty * TILES_W + next_tx;

            /* Bounce at walls or solid tiles */
            if (next_tx < 0 || next_tx >= TILES_W ||
                lvl->map[next_idx] == TILE_SOLID)
            {
                p->dir = -p->dir;
                p->frac = clampf(p->frac, 0.0f, 1.0f);
                continue;
            }

            /* Clear old tile, set new tile */
            renderer_mark_platform_dirty(g.dirty, idx);
            lvl->map[idx] = TILE_AIR;
            lvl->map[next_idx] = TILE_PLATFORM;
            renderer_mark_platform_dirty(g.dirty, next_idx);

            p->tile_idx = next_idx;
            p->frac = (p->frac >= 1.0f) ? 0.0f : 1.0f;
        }
    }
}

/* ─────────────────────────────────────────────────────────────────────────
 * Internal: detect rising edge on button mask (pressed this frame only)
 * ───────────────────────────────────────────────────────────────────────── */
static uint8_t buttons_pressed(uint8_t mask)
{
    uint8_t cur;

    cur = akira_input_get();
    return (cur & mask) & ~(g.prev_buttons & mask);
}
