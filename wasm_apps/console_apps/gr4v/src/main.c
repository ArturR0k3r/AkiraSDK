/*
 * main.c — GR4V: Cat-Skull WASM game for AkiraConsole.
 * App lifecycle and game state machine. Static allocation, 30fps loop.
 *
 * Cat-Skull is trapped inside a corrupted WASM module. Tilt the console
 * to rotate gravity. Collect all 8 memory shards and reach EXIT.
 */
#include "game.h"
#include "akira_api.h"

/* Sprite bitmask data (10 wide × 12 tall; bit 9 = leftmost pixel) */
const uint16_t sprite_a[12] = {
    0x0FC, /* . . X X X X X X . . */
    0x1FE, /* . X X X X X X X X . */
    0x3FF, /* X X X X X X X X X X */
    0x3FF, /* X X X X X X X X X X */
    0x303, /* X X . . . . . . X X  eye sockets */
    0x3B7, /* X X X . X X . X X X  pupils top  */
    0x34B, /* X X . X . . X . X X  pupils mid  */
    0x3B7, /* X X X . X X . X X X  pupils bot  */
    0x3FF, /* X X X X X X X X X X  cheeks      */
    0x2FD, /* X . X X X X X X . X  jaw         */
    0x155, /* . X . X . X . X . X  teeth A     */
    0x000, /* . . . . . . . . . .               */
};

const uint16_t sprite_b[12] = {
    0x0FC,
    0x1FE,
    0x3FF,
    0x3FF,
    0x303,
    0x3B7,
    0x34B,
    0x3B7,
    0x3FF,
    0x2FD,
    0x12A, /* . X . . X . X . X .  teeth B (shifted) */
    0x000,
};

/* Q14 sin lookup (64 entries, 5.625° per step) */
const int16_t sin64[64] = {
        0,  1606,  3196,  4756,  6270,  7723,  9102, 10394,
    11585, 12665, 13623, 14449, 15137, 15679, 16069, 16305,
    16384, 16305, 16069, 15679, 15137, 14449, 13623, 12665,
    11585, 10394,  9102,  7723,  6270,  4756,  3196,  1606,
        0, -1606, -3196, -4756, -6270, -7723, -9102,-10394,
   -11585,-12665,-13623,-14449,-15137,-15679,-16069,-16305,
   -16384,-16305,-16069,-15679,-15137,-14449,-13623,-12665,
   -11585,-10394, -9102, -7723, -6270, -4756, -3196, -1606,
};

/* Global game context (static allocation) */
Game g;

/* ─── Level load ──────────────────────────────────────────────────── */
static void load_level(uint8_t lvl)
{
    g.level = lvl;
    g.shards_collected = 0;

    /* Copy tilemap */
    for (int r = 0; r < GRID_H; r++)
        for (int c = 0; c < GRID_W; c++)
            g.tilemap[r][c] = level_data[lvl][r][c];

    /* Reset dissolve, shard, anchor state */
    for (int i = 0; i < TILE_COUNT; i++) {
        g.dissolve[i].stand    = 0;
        g.dissolve[i].gone     = 0;
        g.dissolve[i].respawn  = 0;
        g.shard_gone[i]        = 0;
        g.anchor_gone[i]       = 0;
        g.dirty[i]             = 1;
    }

    /* Spawn player */
    g.player.spawn_col    = level_spawn_col[lvl];
    g.player.spawn_row    = level_spawn_row[lvl];
    g.player.facing_right = 1;
    g.player.walk_frame   = 0;
    g.player.walk_timer   = 0;
    physics_init();

    /* Reset gravity */
    g.angle_idx    = 0;
    g.anchor_active = 0;
    g.anchor_frames = 0;
    g.in_glitch    = 0;
    g.glitch_timer = 30;
    g.glitch_surge = 0;
    g.glitch_off   = 0;

    renderer_mark_all_dirty();
}

static void respawn_player(void)
{
    physics_init();
    g.angle_idx    = 0;
    g.anchor_active = 0;
    g.anchor_frames = 0;
    renderer_mark_all_dirty();
}

/* ─── Game update ─────────────────────────────────────────────────── */
void update_playing(void)
{
    physics_update();
    effects_update();
    audio_update();
}

void update_dead(void)
{
    effects_update();
    audio_update();
    if (g.state_timer > 0) {
        g.state_timer--;
        return;
    }
    /* Respawn */
    for (int i = 0; i < 16; i++) g.particles[i].active = 0;
    for (int i = 0; i < 8;  i++) g.floattexts[i].active = 0;
    g.flash_frames = 0;
    respawn_player();
    g.state = STATE_PLAYING;
}

void update_level_complete(void)
{
    effects_update();
    audio_update();
    if (g.state_timer > 0) { g.state_timer--; return; }
    uint8_t next = g.level + 1;
    if (next >= NUM_LEVELS) {
        g.state = STATE_GAME_COMPLETE;
        g.state_timer = 0;
    } else {
        load_level(next);
        g.state = STATE_PLAYING;
    }
}

void update_title(void)
{
    audio_update();
    g.title_frame++;
    int32_t ax = sensor_read(0);  /* ACCEL_X */
    int32_t ay = sensor_read(1);  /* ACCEL_Y */
    if (ax == -2147483647 - 1) ax = 0;
    if (ay == -2147483647 - 1) ay = 9800;
    /* Tilt > 0.3 rad → sin(0.3) ≈ 0.296 → ax ≈ 0.296 × 9800 ≈ 2900 */
    int32_t tilt = ax < 0 ? -ax : ax;
    int started = 0;
    if (gpio_read(15) == 1) started = 1;  /* A button (GPIO15, active-low) */
    if (tilt > 2500) started = 1;
    if (started) {
        load_level(0);
        g.shards_collected = 0;
        g.state = STATE_PLAYING;
    }
}

void update_game_complete(void)
{
    audio_update();
    if (gpio_read(15) == 1) {  /* A button (GPIO15, active-low) */
        g.shards_collected = 0;
        load_level(0);
        g.state = STATE_PLAYING;
    }
}

/* ─── Main (excluded from desktop stub which has its own main) ────── */
#ifndef GAME_STUB
int main(void)
{
    /* Static zero-init covers most of Game */
    gpio_configure(15, GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);  /* A button */
    audio_init();
    renderer_init();
    display_clear(C_BG);

    g.state       = STATE_TITLE;
    g.title_frame = 0;
    g.frame       = 0;

    while (1) {
        g.frame++;

        switch (g.state) {
        case STATE_TITLE:
            update_title();
            renderer_draw_title();
            break;

        case STATE_PLAYING:
            update_playing();
            renderer_draw_frame();
            display_flush();
            break;

        case STATE_DEAD:
            update_dead();
            renderer_draw_dead();
            break;

        case STATE_LEVEL_COMPLETE:
            update_level_complete();
            renderer_draw_level_complete();
            break;

        case STATE_GAME_COMPLETE:
            update_game_complete();
            renderer_draw_game_complete();
            break;
        }

        delay(FRAME_US);
    }

    return 0;
}
#endif /* GAME_STUB */
