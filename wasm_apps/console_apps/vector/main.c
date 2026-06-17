/**
 * @file main.c
 * @brief VECTOR — Asteroids-style wireframe shooter for AkiraOS
 *
 * Controls:
 *   Gyro Z  = rotate ship  (physical tilt)
 *   LEFT / RIGHT = rotate ship  (buttons, if no gyro)
 *   B       = thrust
 *   A       = fire
 *   SETTINGS = pause
 *
 * Asteroids split when shot. Three lives. Levels increase asteroid count.
 * Pure wireframe monochrome: all geometry via display_line.
 *
 * @copyright Copyright (c) 2026 PenEngineering S.R.L
 * @license Apache-2.0
 */

#include "akira_api.h"

/* ── Screen ──────────────────────────────────────────────────────────── */
#define SCR_W   320
#define SCR_H   240
#define CX      160
#define CY      120

/* ── Buttons ─────────────────────────────────────────────────────────── */
#define BTN_LEFT     6
#define BTN_RIGHT    7
#define BTN_A        15
#define BTN_B        16
#define BTN_SETTINGS 0

/* ── Fixed-point / angle ─────────────────────────────────────────────── */
#define Q14       16384
#define ANG_FULL  4096
#define ANG_MASK  (ANG_FULL - 1)
#define ANG_90    (ANG_FULL / 4)

/* ── Monochrome ──────────────────────────────────────────────────────── */
#define WHITE  0xFFFF
#define BLACK  0x0000

/* ── Gameplay constants ──────────────────────────────────────────────── */
#define TURN_RATE      80    /* angle units/frame */
#define THRUST_STR     24    /* velocity increment (×16 units) */
#define MAX_SPEED      96    /* max velocity magnitude (×16 units) */
#define FRICTION       252   /* 252/256 drag per frame */
#define BULLET_SPEED   160   /* pixels/frame × 16 */
#define BULLET_LIFE    48
#define FIRE_COOLDOWN  7
#define GYRO_SCALE     55
#define FRAME_MS       33

#define MAX_ASTEROIDS  16
#define MAX_BULLETS     8
#define MAX_EXPLOSIONS  6

/* ── Sin/cos LUT (64 entries, Q14) — same as cube3d ─────────────────── */
static const int16_t s64[64] = {
        0,  1606,  3196,  4756,  6270,  7723,  9102, 10394,
    11585, 12665, 13623, 14449, 15137, 15679, 16069, 16305,
    16384, 16305, 16069, 15679, 15137, 14449, 13623, 12665,
    11585, 10394,  9102,  7723,  6270,  4756,  3196,  1606,
        0, -1606, -3196, -4756, -6270, -7723, -9102,-10394,
   -11585,-12665,-13623,-14449,-15137,-15679,-16069,-16305,
   -16384,-16305,-16069,-15679,-15137,-14449,-13623,-12665,
   -11585,-10394, -9102, -7723, -6270, -4756, -3196, -1606
};

static int32_t sin_a(int ang)
{
    ang &= ANG_MASK;
    int idx = ang >> 6, frac = ang & 63;
    int32_t s0 = s64[idx], s1 = s64[(idx + 1) & 63];
    return s0 + ((s1 - s0) * frac >> 6);
}
static int32_t cos_a(int ang) { return sin_a(ang + ANG_90); }

/* ── PRNG ────────────────────────────────────────────────────────────── */
static uint32_t rng_state = 54321;
static int rng_next(int mod)
{
    rng_state = rng_state * 1103515245u + 12345u;
    return (int)((rng_state >> 16) & 0x7FFF) % mod;
}
static int iabs(int x) { return x < 0 ? -x : x; }

/* ── Types ───────────────────────────────────────────────────────────── */
typedef struct {
    int16_t x16, y16;       /* position × 16 */
    int16_t vx16, vy16;     /* velocity × 16 (pixels/frame) */
    int16_t ang, ang_vel;   /* rotation angle + rate */
    uint8_t size;            /* 2=large, 1=medium, 0=small */
    uint8_t alive;
    uint8_t n_verts;
    int8_t  verts[10][2];   /* pixel offsets from center */
} asteroid_t;

typedef struct {
    int16_t x16, y16;
    int16_t dx16, dy16;
    uint8_t life;
} bullet_t;

typedef struct {
    int16_t x, y;
    uint8_t age;
    uint8_t alive;
} explosion_t;

/* ── State ───────────────────────────────────────────────────────────── */
static asteroid_t  asteroids[MAX_ASTEROIDS];
static bullet_t    bullets[MAX_BULLETS];
static explosion_t explosions[MAX_EXPLOSIONS];

static int16_t  ship_x16, ship_y16;
static int16_t  ship_vx16, ship_vy16;
static int16_t  ship_heading;
static uint8_t  ship_lives, ship_invuln, ship_alive;
static uint8_t  thrusting, fire_cd;
static int32_t  score;
static uint8_t  level, game_over, paused;
static uint8_t  frame_tick;

/* input state */
static int pl, pr, pa, pb, ps;

static const int8_t RADII[3] = { 10, 22, 44 };

/* ── Asteroid ────────────────────────────────────────────────────────── */
static void gen_asteroid(asteroid_t *a, int16_t x16, int16_t y16, uint8_t size)
{
    a->x16 = x16; a->y16 = y16;
    a->size = size; a->alive = 1;
    a->ang = (int16_t)(rng_next(ANG_FULL));

    int spd = (3 - (int)size) * 14 + 6;
    a->vx16 = (int16_t)(rng_next(spd * 2 + 1) - spd);
    a->vy16 = (int16_t)(rng_next(spd * 2 + 1) - spd);
    if (a->vx16 == 0 && a->vy16 == 0) a->vx16 = 6;
    a->ang_vel = (int16_t)(rng_next(36) - 18);
    if (a->ang_vel == 0) a->ang_vel = 6;

    int r = RADII[size];
    a->n_verts = (uint8_t)(8 + rng_next(3));
    for (int i = 0; i < (int)a->n_verts; i++) {
        int angle = ANG_FULL * i / (int)a->n_verts
                  + rng_next(ANG_FULL / (int)a->n_verts / 2);
        int radius = r * (70 + rng_next(60)) / 100;
        a->verts[i][0] = (int8_t)(cos_a(angle) * radius >> 14);
        a->verts[i][1] = (int8_t)(sin_a(angle) * radius >> 14);
    }
}

static void spawn_level(void)
{
    int na = 2 + (int)level;
    if (na > 6) na = 6;
    for (int i = 0; i < MAX_ASTEROIDS; i++) asteroids[i].alive = 0;
    for (int i = 0; i < MAX_BULLETS;   i++) bullets[i].life = 0;
    for (int i = 0; i < MAX_EXPLOSIONS; i++) explosions[i].alive = 0;

    for (int i = 0; i < na; i++) {
        int16_t x16, y16;
        int tries = 0;
        do {
            x16 = (int16_t)(rng_next(SCR_W) * 16);
            y16 = (int16_t)(rng_next(SCR_H) * 16);
            tries++;
        } while (tries < 20 &&
                 iabs((int)(x16 >> 4) - CX) < 70 &&
                 iabs((int)(y16 >> 4) - CY) < 70);
        gen_asteroid(&asteroids[i], x16, y16, 2);
    }
}

static void respawn_ship(void)
{
    ship_x16 = (int16_t)(CX * 16);
    ship_y16 = (int16_t)(CY * 16);
    ship_vx16 = 0; ship_vy16 = 0;
    ship_heading = ANG_90; /* pointing up */
    ship_alive = 1;
    ship_invuln = 90;      /* 3 s invincibility */
}

static void init_game(void)
{
    score = 0; level = 1; game_over = 0; paused = 0;
    ship_lives = 3; fire_cd = 0; thrusting = 0; frame_tick = 0;
    respawn_ship();
    spawn_level();
}

/* ── Explosion ───────────────────────────────────────────────────────── */
static void spawn_explosion(int x, int y)
{
    for (int i = 0; i < MAX_EXPLOSIONS; i++) {
        if (!explosions[i].alive) {
            explosions[i].x = (int16_t)x;
            explosions[i].y = (int16_t)y;
            explosions[i].age = 0;
            explosions[i].alive = 1;
            return;
        }
    }
}

/* ── Bullet ──────────────────────────────────────────────────────────── */
static void fire_bullet(void)
{
    if (fire_cd) return;
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!bullets[i].life) {
            int sx = ship_x16 >> 4, sy = ship_y16 >> 4;
            int tx = sx + (int)(cos_a(ship_heading) * 11 >> 14);
            int ty = sy - (int)(sin_a(ship_heading) * 11 >> 14);
            bullets[i].x16  = (int16_t)(tx * 16);
            bullets[i].y16  = (int16_t)(ty * 16);
            bullets[i].dx16 = (int16_t)(cos_a(ship_heading) * BULLET_SPEED >> 14);
            bullets[i].dy16 = (int16_t)(-(sin_a(ship_heading) * BULLET_SPEED >> 14));
            bullets[i].life = BULLET_LIFE;
            fire_cd = FIRE_COOLDOWN;
            return;
        }
    }
}

/* ── Drawing ─────────────────────────────────────────────────────────── */
static void draw_ship(void)
{
    if (!ship_alive) return;
    if (ship_invuln > 0 && (frame_tick & 4)) return; /* blink */

    int sx = ship_x16 >> 4, sy = ship_y16 >> 4;
    int h = ship_heading;

    /* Tip */
    int tx = sx + (int)(cos_a(h) * 10 >> 14);
    int ty = sy - (int)(sin_a(h) * 10 >> 14);
    /* Left wing (heading + 150°) */
    int lx = sx + (int)(cos_a(h + 1707) * 7 >> 14);
    int ly = sy - (int)(sin_a(h + 1707) * 7 >> 14);
    /* Right wing (heading - 150°) */
    int rx = sx + (int)(cos_a(h - 1707) * 7 >> 14);
    int ry = sy - (int)(sin_a(h - 1707) * 7 >> 14);

    display_line(tx, ty, lx, ly, WHITE);
    display_line(tx, ty, rx, ry, WHITE);
    display_line(lx, ly, rx, ry, WHITE);

    /* Thruster flame */
    if (thrusting) {
        int fl = 5 + rng_next(5);
        int fx = sx - (int)(cos_a(h) * fl >> 14);
        int fy = sy + (int)(sin_a(h) * fl >> 14);
        display_line(lx, ly, fx, fy, WHITE);
        display_line(rx, ry, fx, fy, WHITE);
    }
}

static void draw_asteroid(asteroid_t *a)
{
    if (!a->alive) return;
    int cx = a->x16 >> 4, cy = a->y16 >> 4;
    int32_t ca = cos_a(a->ang), sa = sin_a(a->ang);

    for (int i = 0; i < (int)a->n_verts; i++) {
        int j = (i + 1) % (int)a->n_verts;
        int xi = a->verts[i][0], yi = a->verts[i][1];
        int xj = a->verts[j][0], yj = a->verts[j][1];
        int rx_i = (int)(((int32_t)xi * ca - (int32_t)yi * sa) >> 14);
        int ry_i = (int)(((int32_t)xi * sa + (int32_t)yi * ca) >> 14);
        int rx_j = (int)(((int32_t)xj * ca - (int32_t)yj * sa) >> 14);
        int ry_j = (int)(((int32_t)xj * sa + (int32_t)yj * ca) >> 14);
        display_line(cx + rx_i, cy + ry_i, cx + rx_j, cy + ry_j, WHITE);
    }
}

static void draw_explosion(explosion_t *e)
{
    if (!e->alive) return;
    int r_inner = e->age * 2;
    int r_outer = r_inner + 4;
    for (int i = 0; i < 8; i++) {
        int ang = ANG_FULL * i / 8;
        int x1 = e->x + (int)(cos_a(ang) * r_inner >> 14);
        int y1 = e->y - (int)(sin_a(ang) * r_inner >> 14);
        int x2 = e->x + (int)(cos_a(ang) * r_outer >> 14);
        int y2 = e->y - (int)(sin_a(ang) * r_outer >> 14);
        display_line(x1, y1, x2, y2, WHITE);
    }
}

static void draw_hud(void)
{
    /* Lives as small ship icons */
    for (int i = 0; i < (int)ship_lives; i++) {
        int lx = 6 + i * 14;
        display_line(lx, 8,  lx - 4, 18, WHITE);
        display_line(lx, 8,  lx + 4, 18, WHITE);
        display_line(lx - 4, 18, lx + 4, 18, WHITE);
    }
    /* Score centered */
    display_number(CX - 18, 4, score, WHITE);
    /* Level top-right */
    display_text(SCR_W - 40, 4, "Lv", WHITE);
    display_number(SCR_W - 18, 4, level, WHITE);
}

/* ── Update ──────────────────────────────────────────────────────────── */
static int count_asteroids(void)
{
    int n = 0;
    for (int i = 0; i < MAX_ASTEROIDS; i++) if (asteroids[i].alive) n++;
    return n;
}

static void update(void)
{
    int l = gpio_read(BTN_LEFT);
    int r = gpio_read(BTN_RIGHT);
    int a = gpio_read(BTN_A);
    int b = gpio_read(BTN_B);
    int s = gpio_read(BTN_SETTINGS);

    if (s && !ps) paused = !paused;
    ps = s;
    if (paused) return;

    /* Rotation: gyro + buttons */
    int gz = sensor_read(SENSOR_CHAN_GYRO_Z);
    if (gz != AKIRA_SENSOR_ERROR)
        ship_heading = (int16_t)((ship_heading - gz / GYRO_SCALE) & ANG_MASK);
    if (l) ship_heading = (int16_t)((ship_heading + TURN_RATE) & ANG_MASK);
    if (r) ship_heading = (int16_t)((ship_heading - TURN_RATE) & ANG_MASK);

    /* Thrust */
    thrusting = b;
    if (b && ship_alive) {
        ship_vx16 += (int16_t)(cos_a(ship_heading) * THRUST_STR >> 14);
        ship_vy16 -= (int16_t)(sin_a(ship_heading) * THRUST_STR >> 14);
        if (ship_vx16 >  MAX_SPEED) ship_vx16 =  MAX_SPEED;
        if (ship_vx16 < -MAX_SPEED) ship_vx16 = -MAX_SPEED;
        if (ship_vy16 >  MAX_SPEED) ship_vy16 =  MAX_SPEED;
        if (ship_vy16 < -MAX_SPEED) ship_vy16 = -MAX_SPEED;
    }

    /* Friction */
    ship_vx16 = (int16_t)((int32_t)ship_vx16 * FRICTION / 256);
    ship_vy16 = (int16_t)((int32_t)ship_vy16 * FRICTION / 256);

    /* Move ship + wrap */
    ship_x16 += ship_vx16;
    ship_y16 += ship_vy16;
    if (ship_x16 < -(int16_t)(20 * 16)) ship_x16 += (int16_t)(SCR_W * 16);
    if (ship_x16 >  (int16_t)(SCR_W * 16) + (int16_t)(20 * 16)) ship_x16 -= (int16_t)(SCR_W * 16);
    if (ship_y16 < -(int16_t)(20 * 16)) ship_y16 += (int16_t)(SCR_H * 16);
    if (ship_y16 >  (int16_t)(SCR_H * 16) + (int16_t)(20 * 16)) ship_y16 -= (int16_t)(SCR_H * 16);

    /* Fire */
    if (fire_cd) fire_cd--;
    if (a && !pa && ship_alive) fire_bullet();
    pa = a; pl = l; pr = r; pb = b;

    /* Bullets */
    for (int i = 0; i < MAX_BULLETS; i++) {
        bullet_t *bu = &bullets[i];
        if (!bu->life) continue;
        bu->x16 += bu->dx16;
        bu->y16 += bu->dy16;
        bu->life--;
        if (bu->x16 < 0)                bu->x16 += (int16_t)(SCR_W * 16);
        if (bu->x16 >= (int16_t)(SCR_W * 16)) bu->x16 -= (int16_t)(SCR_W * 16);
        if (bu->y16 < 0)                bu->y16 += (int16_t)(SCR_H * 16);
        if (bu->y16 >= (int16_t)(SCR_H * 16)) bu->y16 -= (int16_t)(SCR_H * 16);
    }

    /* Asteroids: move + wrap */
    for (int i = 0; i < MAX_ASTEROIDS; i++) {
        asteroid_t *ast = &asteroids[i];
        if (!ast->alive) continue;
        ast->x16 += ast->vx16;
        ast->y16 += ast->vy16;
        ast->ang  = (int16_t)((ast->ang + ast->ang_vel) & ANG_MASK);
        int16_t mx = (int16_t)((int)RADII[ast->size] * 16 + 16);
        if (ast->x16 < -mx)                     ast->x16 += (int16_t)(SCR_W * 16);
        if (ast->x16 > (int16_t)(SCR_W * 16) + mx) ast->x16 -= (int16_t)(SCR_W * 16);
        if (ast->y16 < -mx)                     ast->y16 += (int16_t)(SCR_H * 16);
        if (ast->y16 > (int16_t)(SCR_H * 16) + mx) ast->y16 -= (int16_t)(SCR_H * 16);
    }

    /* Explosions age */
    for (int i = 0; i < MAX_EXPLOSIONS; i++) {
        if (explosions[i].alive && ++explosions[i].age > 16)
            explosions[i].alive = 0;
    }

    /* Bullet vs asteroid */
    for (int bi = 0; bi < MAX_BULLETS; bi++) {
        if (!bullets[bi].life) continue;
        int bx = bullets[bi].x16 >> 4, by = bullets[bi].y16 >> 4;

        for (int ai = 0; ai < MAX_ASTEROIDS; ai++) {
            asteroid_t *ast = &asteroids[ai];
            if (!ast->alive) continue;
            int ax = ast->x16 >> 4, ay = ast->y16 >> 4;
            int dx = bx - ax, dy = by - ay;
            int ri = (int)RADII[ast->size] + 2;
            if (dx * dx + dy * dy < ri * ri) {
                bullets[bi].life = 0;
                spawn_explosion(ax, ay);

                static const int16_t pts[3] = { 100, 50, 20 };
                score += pts[ast->size];

                /* Split */
                if (ast->size > 0) {
                    int found = 0;
                    for (int ni = 0; ni < MAX_ASTEROIDS && found < 2; ni++) {
                        if (!asteroids[ni].alive) {
                            gen_asteroid(&asteroids[ni], ast->x16, ast->y16,
                                         ast->size - 1);
                            asteroids[ni].vx16 += (int16_t)(rng_next(24) - 12);
                            asteroids[ni].vy16 += (int16_t)(rng_next(24) - 12);
                            found++;
                        }
                    }
                }
                ast->alive = 0;
                break;
            }
        }
    }

    /* Ship vs asteroid */
    if (ship_alive && !ship_invuln) {
        int sx = ship_x16 >> 4, sy = ship_y16 >> 4;
        for (int ai = 0; ai < MAX_ASTEROIDS; ai++) {
            asteroid_t *ast = &asteroids[ai];
            if (!ast->alive) continue;
            int ax = ast->x16 >> 4, ay = ast->y16 >> 4;
            int dx = sx - ax, dy = sy - ay;
            int ri = (int)RADII[ast->size] + 5;
            if (dx * dx + dy * dy < ri * ri) {
                spawn_explosion(sx, sy);
                ship_alive = 0;
                if (ship_lives > 0) ship_lives--;
                if (ship_lives == 0) { game_over = 1; return; }
                respawn_ship();
                break;
            }
        }
    }
    if (ship_invuln) ship_invuln--;

    /* Level complete */
    if (count_asteroids() == 0) {
        level++;
        spawn_level();
        respawn_ship();
    }
}

/* ── Frame ───────────────────────────────────────────────────────────── */
static void draw_frame(void)
{
    display_clear(BLACK);

    for (int i = 0; i < MAX_ASTEROIDS; i++) draw_asteroid(&asteroids[i]);

    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!bullets[i].life) continue;
        int bx = bullets[i].x16 >> 4, by = bullets[i].y16 >> 4;
        display_rect(bx - 1, by - 1, 3, 3, WHITE);
    }

    draw_ship();

    for (int i = 0; i < MAX_EXPLOSIONS; i++) draw_explosion(&explosions[i]);

    draw_hud();

    if (paused) {
        display_rect(CX - 36, CY - 10, 72, 20, BLACK);
        display_text(CX - 24, CY - 6, "PAUSED", WHITE);
    }

    display_flush();
    frame_tick++;
}

/* ── Title ───────────────────────────────────────────────────────────── */
static void draw_title(void)
{
    display_clear(BLACK);

    /* Big title */
    display_text_large(80, 40, "VECTOR", WHITE);

    /* Decorative asteroid outline */
    static const int8_t demo_v[9][2] = {
        {0,-40}, {28,-28}, {38,5}, {22,36}, {-10,42},
        {-38,20}, {-42,-8}, {-28,-30}, {-8,-42}
    };
    int dx = CX, dy = 145;
    for (int i = 0; i < 9; i++) {
        int j = (i + 1) % 9;
        display_line(dx + demo_v[i][0], dy + demo_v[i][1],
                     dx + demo_v[j][0], dy + demo_v[j][1], WHITE);
    }

    /* Ship icon */
    display_line(dx,      dy - 14, dx - 6, dy + 4, WHITE);
    display_line(dx,      dy - 14, dx + 6, dy + 4, WHITE);
    display_line(dx - 6,  dy + 4,  dx + 6, dy + 4, WHITE);

    display_text(50, 195, "Tilt/L-R: rotate  B: thrust  A: fire", WHITE);
    display_text(90, 215, "Press A to launch!", WHITE);
    display_flush();
}

/* ── Game over ───────────────────────────────────────────────────────── */
static void draw_gameover(void)
{
    display_clear(BLACK);
    display_text_large(40, 60, "GAME OVER", WHITE);
    display_hline(20, 95, SCR_W - 40, WHITE);
    display_text(CX - 32, 115, "SCORE", WHITE);
    display_number(CX - 16, 135, score, WHITE);
    display_text(CX - 32, 165, "LEVEL", WHITE);
    display_number(CX - 8, 185, level, WHITE);
    display_hline(20, 205, SCR_W - 40, WHITE);
    display_text(68, 215, "Press A to play again", WHITE);
    display_flush();
}

/* ── Main ────────────────────────────────────────────────────────────── */
int main(void)
{
    printf("AkiraOS VECTOR v1.0");

    gpio_configure(BTN_LEFT,     GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_RIGHT,    GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_A,        GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_B,        GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_SETTINGS, GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);

    uint32_t seed = 1;
    draw_title();
    while (!gpio_read(BTN_A)) { seed++; delay(20000); }
    rng_state = seed ^ 0xDEADBEEFu;
    rng_next(5); rng_next(5);
    delay(80000);

    int tmr = timer_create();

    while (1) {
        init_game();

        while (!game_over) {
            timer_start(tmr);
            update();
            draw_frame();
            int left = FRAME_MS - timer_elapsed(tmr);
            if (left > 1) delay((uint32_t)left * 1000);
        }

        draw_gameover();
        delay(600000);
        while (!gpio_read(BTN_A)) { seed++; delay(20000); }
        rng_state = seed ^ 0xCAFEBABEu;
        delay(80000);
    }

    return 0;
}
