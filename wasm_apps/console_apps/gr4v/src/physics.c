/*
 * physics.c — Gravity vector, AABB tilemap collision, velocity integration.
 */
#include "game.h"
#include "akira_api.h"

/* Zephyr sensor channel IDs (as known from AkiraOS) */
#define ACCEL_X 0
#define ACCEL_Y 1

/* Integer atan2 → angle index 0-63 (360° ÷ 64 = 5.625° per step) */
static int32_t angle_from_accel(int32_t ax, int32_t ay)
{
    /* atan2(ax, ay) using integer approximation from inclinometer */
    int32_t aax = ax < 0 ? -ax : ax;
    int32_t aay = ay < 0 ? -ay : ay;
    int32_t ang;
    if (aax >= aay)
        ang = (int32_t)((int64_t)10314 * aax * aay
                        / ((int64_t)18 * aax * aax + (int64_t)5 * aay * aay + 1));
    else
        ang = 900 - (int32_t)((int64_t)10314 * aay * aax
                               / ((int64_t)18 * aay * aay + (int64_t)5 * aax * aax + 1));
    /* Quadrant correction → tenths of degrees in [0, 3600) */
    if (ax < 0)  ang = 1800 - ang;
    if (ay < 0)  ang = 3600 - ang;
    if (ang < 0) ang += 3600;
    if (ang >= 3600) ang -= 3600;
    /* Map 0-3600 → 0-63 */
    return (ang * 64) / 3600;
}

/* Check whether pixel (x, y) lies in a collidable tile */
static int tile_solid(int px, int py)
{
    if (px < 0 || py < 0 || px >= SCR_W || py >= PLAY_H) return 1;
    int col = px / TILE_W;
    int row = py / TILE_H;
    if (col < 0 || col >= GRID_W || row < 0 || row >= GRID_H) return 1;
    uint8_t tid = g.tilemap[row][col];
    if (tid == T_VOID) return 0;  /* passthrough */
    if (tid == T_GLITCH || tid == T_AIR) return 0;
    /* Dissolve tile: check if gone */
    if (tid == T_DISSOLVE && g.dissolve[TIDX(col, row)].gone) return 0;
    return (tid != T_AIR);
}

/* Simple AABB sweep: move fp position by (dvx, dvy) one pixel at a time */
static void sweep_axis(int32_t *fppos, int32_t *vel, int32_t delta,
                        int32_t fp_other, int w, int h, int axis)
{
    while (delta != 0) {
        int step = (delta > 0) ? PHYS_FP : -PHYS_FP;
        int32_t nx = *fppos + step;
        int px_lo  = PX(nx);
        int px_hi  = PX(nx) + (axis == 0 ? w - 1 : 0);
        int py_lo  = PX(fp_other);
        int py_hi  = PX(fp_other) + (axis == 1 ? h - 1 : 0);
        if (axis == 0) py_hi = PX(fp_other) + h - 1;
        else           px_hi = PX(*fppos)    + w - 1;

        int hit = 0;
        if (axis == 0) {
            int ex = (delta > 0) ? (px_lo + w - 1) : px_lo;
            if (tile_solid(ex, py_lo) || tile_solid(ex, py_hi)) hit = 1;
        } else {
            int ey = (delta > 0) ? (py_lo + h - 1) : py_lo;
            if (tile_solid(px_lo, ey) || tile_solid(px_hi, ey)) hit = 1;
        }
        (void)px_hi; (void)py_hi;  /* suppress warnings in non-sweep paths */
        if (hit) { *vel = 0; break; }
        *fppos = nx;
        delta -= step;
    }
}

void physics_init(void)
{
    /* Spawn player */
    uint8_t sc = g.player.spawn_col;
    uint8_t sr = g.player.spawn_row;
    g.player.fpx = FP(sc * TILE_W + (TILE_W - PLAYER_W) / 2);
    g.player.fpy = FP(sr * TILE_H - PLAYER_H);
    g.player.vx  = 0;
    g.player.vy  = 0;
}

void physics_update(void)
{
    /* 1. Read IMU (values are ×1000 milli-m/s²) */
    int32_t ax = sensor_read(ACCEL_X);
    int32_t ay = sensor_read(ACCEL_Y);
    if (ax == -2147483647 - 1) ax = 0;  /* AKIRA_SENSOR_ERROR */
    if (ay == -2147483647 - 1) ay = 9800;

    /* 2. Derive angle index, apply deadzone */
    if (!g.anchor_active) {
        int32_t new_idx = angle_from_accel(ax, ay);
        int32_t diff = new_idx - g.angle_idx;
        if (diff >  32) diff -= 64;
        if (diff < -32) diff += 64;
        if (ABS(diff) >= PHYS_DEADZONE) {
            g.angle_idx = new_idx;
        }
    }

    /* 3. Apply glitch zone offset */
    int32_t eff_angle = g.angle_idx;
    if (g.in_glitch && g.glitch_surge > 0) {
        eff_angle += g.glitch_off;
        g.glitch_surge--;
        if (g.glitch_surge == 0) g.glitch_off = 0;
    }
    if (g.in_glitch && g.glitch_surge == 0) {
        g.glitch_timer--;
        if (g.glitch_timer <= 0) {
            /* LCG random */
            uint32_t r = (uint32_t)g.frame * 1664525u + 1013904223u;
            g.glitch_off = (r & 1u) ? GLITCH_SURGE_STEPS : -GLITCH_SURGE_STEPS;
            g.glitch_surge = GLITCH_SURGE_FRAMES;
            /* next surge: 1-3s random */
            r = r * 1664525u + 1013904223u;
            g.glitch_timer = (int16_t)(30 + (r & 63u) % 60);
        }
    }

    /* 4. Apply gravity acceleration */
    int32_t sv = SIN64(eff_angle);  /* Q14 sin */
    int32_t cv = COS64(eff_angle);  /* Q14 cos */
    /* dv = g_component × 8363 / 16384 */
    g.player.vx += (sv * PHYS_DV) >> PHYS_Q14;
    g.player.vy += (cv * PHYS_DV) >> PHYS_Q14;

    /* 5. Friction */
    g.player.vx = (g.player.vx * PHYS_FRIC) / 1000;
    g.player.vy = (g.player.vy * PHYS_FRIC) / 1000;

    /* 6. Clamp velocity */
    g.player.vx = CLAMP(g.player.vx, -PHYS_MAX_V, PHYS_MAX_V);
    g.player.vy = CLAMP(g.player.vy, -PHYS_MAX_V, PHYS_MAX_V);

    /* 7. Sweep X axis */
    sweep_axis(&g.player.fpx, &g.player.vx, g.player.vx,
               g.player.fpy, PLAYER_W, PLAYER_H, 0);

    /* 8. Sweep Y axis */
    sweep_axis(&g.player.fpy, &g.player.vy, g.player.vy,
               g.player.fpx, PLAYER_W, PLAYER_H, 1);

    /* 9. Screen bounds clamp */
    if (PX(g.player.fpx) < 0)
        g.player.fpx = 0;
    if (PX(g.player.fpx) + PLAYER_W > SCR_W)
        g.player.fpx = FP(SCR_W - PLAYER_W);

    /* 10. Walk animation */
    g.player.walk_timer++;
    if (g.player.walk_timer >= 8) {
        g.player.walk_timer = 0;
        g.player.walk_frame ^= 1u;
        if (ABS(g.player.vx) > 64) audio_play(SFX_STEP);
    }
    if (g.player.vx > 32)       g.player.facing_right = 1;
    else if (g.player.vx < -32) g.player.facing_right = 0;

    /* 11. Check tile interactions at player foot center */
    int pcx = PX(g.player.fpx) + PLAYER_W / 2;
    int pcy_top = PX(g.player.fpy);
    int pcy_bot = PX(g.player.fpy) + PLAYER_H - 1;
    int pcy_mid = PX(g.player.fpy) + PLAYER_H / 2;

    /* Check anchor lock decrement */
    if (g.anchor_active) {
        if (g.anchor_frames > 0) g.anchor_frames--;
        else                      g.anchor_active = 0;
    }

    /* Check feet/body tiles */
    int cx, cy;
    for (cy = pcy_top; cy <= pcy_bot; cy += 4) {
        for (cx = PX(g.player.fpx); cx < PX(g.player.fpx) + PLAYER_W; cx += 5) {
            if (cx < 0 || cy < 0 || cx >= SCR_W || cy >= PLAY_H) continue;
            int col = cx / TILE_W;
            int row = cy / TILE_H;
            if (col < 0 || col >= GRID_W || row < 0 || row >= GRID_H) continue;
            uint8_t tid = g.tilemap[row][col];
            int idx = TIDX(col, row);

            switch (tid) {
            case T_SPIKE:
                /* trigger death — handled in main */
                g.state = STATE_DEAD;
                g.state_timer = 36; /* 1.2s */
                effects_emit_death(pcx, pcy_mid);
                effects_flash(0xFFFF, 3);
                audio_play(SFX_DEATH);
                return;

            case T_VOID:
                /* fall through — no collision */
                break;

            case T_SHARD:
                if (!g.shard_gone[idx]) {
                    g.shard_gone[idx] = 1;
                    g.tilemap[row][col] = T_AIR;
                    g.dirty[idx] = 1;
                    g.shards_collected++;
                    /* Generate random hex address text */
                    effects_emit_collect(col * TILE_W + 8, row * TILE_H);
                    audio_play(SFX_SHARD);
                }
                break;

            case T_ANCHOR:
                if (!g.anchor_gone[idx]) {
                    g.anchor_gone[idx] = 1;
                    g.tilemap[row][col] = T_AIR;
                    g.dirty[idx] = 1;
                    g.anchor_active  = 1;
                    g.anchor_frames  = ANCHOR_FRAMES;
                    audio_play(SFX_ANCHOR);
                }
                break;

            case T_EXIT:
                if (g.shards_collected >= SHARD_GOAL) {
                    g.state = STATE_LEVEL_COMPLETE;
                    g.state_timer = 54; /* 1.8s */
                    effects_flash(C_SHARD, 3);
                    audio_play(SFX_WIN);
                }
                break;

            case T_GLITCH:
                g.in_glitch = 1;
                break;

            case T_DISSOLVE:
                if (!g.dissolve[idx].gone) {
                    g.dissolve[idx].stand++;
                    if (g.dissolve[idx].stand % 12 == 0 && g.dissolve[idx].stand > DSV_WARN1)
                        audio_play(SFX_DISSOLVE);
                    if (g.dissolve[idx].stand >= DSV_TOTAL) {
                        g.dissolve[idx].gone    = 1;
                        g.dissolve[idx].respawn = DSV_RESPAWN;
                        g.dirty[idx] = 1;
                    }
                }
                break;

            default: break;
            }
        }
    }

    /* Reset glitch if player left all glitch tiles */
    {
        int in_g = 0;
        for (cy = pcy_top; cy <= pcy_bot; cy += 4) {
            for (cx = PX(g.player.fpx); cx < PX(g.player.fpx) + PLAYER_W; cx += 5) {
                if (cx < 0 || cy < 0 || cx >= SCR_W || cy >= PLAY_H) continue;
                int col = cx / TILE_W, row = cy / TILE_H;
                if (col >= 0 && col < GRID_W && row >= 0 && row < GRID_H)
                    if (g.tilemap[row][col] == T_GLITCH) in_g = 1;
            }
        }
        g.in_glitch = in_g;
    }

    /* Dissolve tile respawn */
    for (int i = 0; i < TILE_COUNT; i++) {
        if (g.dissolve[i].gone && g.dissolve[i].respawn > 0) {
            g.dissolve[i].respawn--;
            if (g.dissolve[i].respawn == 0) {
                int row = i / GRID_W, col = i % GRID_W;
                g.tilemap[row][col] = T_DISSOLVE;
                g.dissolve[i].gone  = 0;
                g.dissolve[i].stand = 0;
                g.dirty[i] = 1;
            }
        }
    }

    /* Fall-off bottom: if player exits play area, respawn */
    if (PX(g.player.fpy) >= PLAY_H || PX(g.player.fpy) + PLAYER_H < 0) {
        g.state = STATE_DEAD;
        g.state_timer = 36;
        effects_emit_death(pcx, pcy_mid);
        effects_flash(0xFFFF, 3);
        audio_play(SFX_DEATH);
    }
}
