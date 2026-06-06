/**
 * @file main.c
 * @brief Pixel Dungeon — roguelike dungeon crawler for AkiraOS
 *
 * Controls:
 *   DPAD = Move / attack adjacent enemies
 *   A = Pick up item / use stairs
 *   B = Use health potion
 *   SETTINGS = Pause
 *
 * Gameplay:
 *   Procedurally generated dungeon floors. Fight monsters to earn XP,
 *   level up for more HP/ATK, collect potions and gold, descend stairs
 *   to go deeper. Harder enemies each floor. Die = game over.
 *
 * @copyright Copyright (c) 2026 PenEngineering S.R.L
* @license Apache-2.0
 */

#include "akira_api.h"

/* ── Display ─────────────────────────────────────────────────────────── */
static int32_t SCR_W = 320, SCR_H = 240;

/* ── Buttons ─────────────────────────────────────────────────────────── */
#define BTN_UP       4
#define BTN_DOWN     5
#define BTN_LEFT     7
#define BTN_RIGHT    6
#define BTN_A        15
#define BTN_B        16
#define BTN_SETTINGS 0    /* BTN.OK = GPIO0, active-low pull-up */

/* ── Colours ─────────────────────────────────────────────────────────── */
#define COL_BG        0x0000
#define COL_WALL      0x4208   /* dark grey  */
#define COL_FLOOR     0x2104   /* very dark  */
#define COL_PLAYER    0x07FF   /* cyan       */
#define COL_ENEMY     0xF800   /* red        */
#define COL_ENEMY2    0xFBE0   /* orange     */
#define COL_ENEMY3    0xA81F   /* purple     */
#define COL_GOLD      0xFE60   /* gold       */
#define COL_POTION    0x07E0   /* green      */
#define COL_STAIRS    0xFFE0   /* yellow     */
#define COL_HUD_BG    0x0000
#define COL_HUD_TXT   0xFFFF
#define COL_HP_BAR    0xF800
#define COL_HP_BG     0x4208
#define COL_XP_BAR    0x07FF
#define COL_LABEL     0x8410
#define COL_TITLE     0x07FF
#define COL_DMG       0xF800
#define COL_HEAL      0x07E0

/* ── Map ─────────────────────────────────────────────────────────────── */
#define MAP_W    20
#define MAP_H    14
#define TILE_W   16
#define TILE_H   15
#define MAP_OX   0    /* offset X */
#define MAP_OY   22   /* offset Y (below HUD) */

#define FRAME_US 33333

/* Tile types */
enum { T_WALL = 0, T_FLOOR, T_STAIRS };

static uint8_t map[MAP_H][MAP_W];

/* ── Entities ────────────────────────────────────────────────────────── */
#define MAX_ENEMIES   12
#define MAX_ITEMS      8

typedef struct {
    int x, y, hp, max_hp, atk, alive;
    uint16_t color;
} enemy_t;

enum { ITEM_NONE = 0, ITEM_GOLD, ITEM_POTION };

typedef struct {
    int x, y, type, active;
} item_t;

static enemy_t enemies[MAX_ENEMIES];
static item_t items[MAX_ITEMS];

/* ── Player ──────────────────────────────────────────────────────────── */
static int px, py;
static int p_hp, p_max_hp, p_atk, p_def;
static int p_xp, p_xp_next, p_level;
static int p_gold, p_potions;
static int p_floor;
static int game_over;
static int exit_to_supervisor, restart_game;

/* ── Message log ─────────────────────────────────────────────────────── */
static char msg_buf[40];
static int msg_timer;

static void set_msg(const char *s) {
    int i;
    for (i = 0; s[i] && i < 39; i++) msg_buf[i] = s[i];
    msg_buf[i] = '\0';
    msg_timer = 60;
}

/* ── PRNG ────────────────────────────────────────────────────────────── */
static uint32_t rng = 99999;
static int rng_next(int mod) {
    rng = rng * 1103515245 + 12345;
    return (int)((rng >> 16) & 0x7FFF) % mod;
}

/* ── Dungeon generation ──────────────────────────────────────────────── */
static void gen_room(int rx, int ry, int rw, int rh) {
    for (int y = ry; y < ry + rh && y < MAP_H; y++)
        for (int x = rx; x < rx + rw && x < MAP_W; x++)
            map[y][x] = T_FLOOR;
}

static void gen_corridor_h(int x1, int x2, int y) {
    int a = x1 < x2 ? x1 : x2;
    int b = x1 < x2 ? x2 : x1;
    for (int x = a; x <= b && x < MAP_W; x++)
        if (y >= 0 && y < MAP_H) map[y][x] = T_FLOOR;
}

static void gen_corridor_v(int y1, int y2, int x) {
    int a = y1 < y2 ? y1 : y2;
    int b = y1 < y2 ? y2 : y1;
    for (int y = a; y <= b && y < MAP_H; y++)
        if (x >= 0 && x < MAP_W) map[y][x] = T_FLOOR;
}

static void generate_map(void) {
    /* Fill with walls */
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++)
            map[y][x] = T_WALL;

    /* Generate 4-6 rooms + corridors */
    int num_rooms = 4 + rng_next(3);
    int room_cx[7], room_cy[7];

    for (int i = 0; i < num_rooms; i++) {
        int rw = 3 + rng_next(4);
        int rh = 3 + rng_next(3);
        int rx = 1 + rng_next(MAP_W - rw - 2);
        int ry = 1 + rng_next(MAP_H - rh - 2);
        gen_room(rx, ry, rw, rh);
        room_cx[i] = rx + rw / 2;
        room_cy[i] = ry + rh / 2;

        /* Connect to previous room */
        if (i > 0) {
            if (rng_next(2)) {
                gen_corridor_h(room_cx[i-1], room_cx[i], room_cy[i-1]);
                gen_corridor_v(room_cy[i-1], room_cy[i], room_cx[i]);
            } else {
                gen_corridor_v(room_cy[i-1], room_cy[i], room_cx[i-1]);
                gen_corridor_h(room_cx[i-1], room_cx[i], room_cy[i]);
            }
        }
    }

    /* Place player in first room */
    px = room_cx[0];
    py = room_cy[0];

    /* Place stairs in last room */
    map[room_cy[num_rooms-1]][room_cx[num_rooms-1]] = T_STAIRS;

    /* Place enemies */
    int num_enemies = 3 + p_floor;
    if (num_enemies > MAX_ENEMIES) num_enemies = MAX_ENEMIES;
    for (int i = 0; i < MAX_ENEMIES; i++) enemies[i].alive = 0;

    for (int i = 0; i < num_enemies; i++) {
        int attempts = 50;
        while (attempts-- > 0) {
            int ex = 1 + rng_next(MAP_W - 2);
            int ey = 1 + rng_next(MAP_H - 2);
            if (map[ey][ex] == T_FLOOR && !(ex == px && ey == py)) {
                enemies[i].x = ex;
                enemies[i].y = ey;
                enemies[i].max_hp = 2 + p_floor + rng_next(p_floor + 1);
                enemies[i].hp = enemies[i].max_hp;
                enemies[i].atk = 1 + p_floor / 2 + rng_next(2);
                enemies[i].alive = 1;
                /* Color by difficulty */
                if (p_floor <= 2) enemies[i].color = COL_ENEMY;
                else if (p_floor <= 5) enemies[i].color = COL_ENEMY2;
                else enemies[i].color = COL_ENEMY3;
                break;
            }
        }
    }

    /* Place items */
    int num_items = 2 + rng_next(3);
    if (num_items > MAX_ITEMS) num_items = MAX_ITEMS;
    for (int i = 0; i < MAX_ITEMS; i++) items[i].active = 0;

    for (int i = 0; i < num_items; i++) {
        int attempts = 50;
        while (attempts-- > 0) {
            int ix = 1 + rng_next(MAP_W - 2);
            int iy = 1 + rng_next(MAP_H - 2);
            if (map[iy][ix] == T_FLOOR && !(ix == px && iy == py)) {
                items[i].x = ix;
                items[i].y = iy;
                items[i].type = (rng_next(3) == 0) ? ITEM_POTION : ITEM_GOLD;
                items[i].active = 1;
                break;
            }
        }
    }
}

/* ── Init ────────────────────────────────────────────────────────────── */
static void init_game(void) {
    p_hp = 20; p_max_hp = 20;
    p_atk = 3; p_def = 1;
    p_xp = 0; p_xp_next = 10; p_level = 1;
    p_gold = 0; p_potions = 1;
    p_floor = 1;
    game_over = 0;
    msg_buf[0] = '\0';
    msg_timer = 0;
    generate_map();
}

/* ── Level up ────────────────────────────────────────────────────────── */
static void check_level_up(void) {
    while (p_xp >= p_xp_next) {
        p_xp -= p_xp_next;
        p_level++;
        p_max_hp += 5;
        p_hp = p_max_hp;
        p_atk += 1;
        p_def += 1;
        p_xp_next += 5 + p_level * 2;
        set_msg("LEVEL UP!");
    }
}

/* ── Combat ──────────────────────────────────────────────────────────── */
static void attack_enemy(int idx) {
    enemy_t *e = &enemies[idx];
    int dmg = p_atk - rng_next(2);
    if (dmg < 1) dmg = 1;
    e->hp -= dmg;

    if (e->hp <= 0) {
        e->alive = 0;
        int xp_gain = e->max_hp + e->atk;
        p_xp += xp_gain;
        set_msg("Enemy slain!");
        check_level_up();
    } else {
        set_msg("Hit!");
    }
}

static void enemy_attacks(int idx) {
    enemy_t *e = &enemies[idx];
    int dmg = e->atk - p_def + rng_next(2);
    if (dmg < 1) dmg = 1;
    p_hp -= dmg;
    if (p_hp <= 0) {
        p_hp = 0;
        game_over = 1;
        set_msg("You died!");
    }
}

/* ── Move enemies ────────────────────────────────────────────────────── */
static void move_enemies(void) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].alive) continue;
        int ex = enemies[i].x, ey = enemies[i].y;

        /* Simple AI: move toward player if close */
        int dx = px - ex;
        int dy = py - ey;
        int dist = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);

        if (dist <= 1) {
            /* Adjacent — attack */
            enemy_attacks(i);
            continue;
        }
        if (dist > 6) continue; /* Too far, idle */

        int nx = ex, ny = ey;
        /* Move in the axis with larger difference */
        if ((dx < 0 ? -dx : dx) >= (dy < 0 ? -dy : dy)) {
            nx += (dx > 0) ? 1 : -1;
        } else {
            ny += (dy > 0) ? 1 : -1;
        }

        /* Check valid move */
        if (nx < 0 || nx >= MAP_W || ny < 0 || ny >= MAP_H) continue;
        if (map[ny][nx] == T_WALL) continue;
        if (nx == px && ny == py) { enemy_attacks(i); continue; }

        /* Don't overlap other enemies */
        int blocked = 0;
        for (int j = 0; j < MAX_ENEMIES; j++) {
            if (j != i && enemies[j].alive &&
                enemies[j].x == nx && enemies[j].y == ny) {
                blocked = 1; break;
            }
        }
        if (!blocked) { enemies[i].x = nx; enemies[i].y = ny; }
    }
}

/* ── Pick up items ───────────────────────────────────────────────────── */
static void try_pickup(void) {
    for (int i = 0; i < MAX_ITEMS; i++) {
        if (!items[i].active) continue;
        if (items[i].x == px && items[i].y == py) {
            if (items[i].type == ITEM_GOLD) {
                int g = 3 + rng_next(5) + p_floor;
                p_gold += g;
                set_msg("Gold!");
            } else if (items[i].type == ITEM_POTION) {
                p_potions++;
                set_msg("Potion!");
            }
            items[i].active = 0;
        }
    }
}

/* ── Drawing ─────────────────────────────────────────────────────────── */
static void draw_tile(int x, int y) {
    int sx = MAP_OX + x * TILE_W;
    int sy = MAP_OY + y * TILE_H;

    switch (map[y][x]) {
    case T_WALL:
        display_rect(sx, sy, TILE_W, TILE_H, COL_WALL);
        /* Brick pattern */
        display_hline(sx, sy + TILE_H/2, TILE_W, 0x2104);
        break;
    case T_FLOOR:
        display_rect(sx, sy, TILE_W, TILE_H, COL_FLOOR);
        break;
    case T_STAIRS:
        display_rect(sx, sy, TILE_W, TILE_H, COL_FLOOR);
        /* Stairs icon */
        display_rect(sx + 2, sy + 2, TILE_W - 4, 3, COL_STAIRS);
        display_rect(sx + 4, sy + 6, TILE_W - 8, 3, COL_STAIRS);
        display_rect(sx + 6, sy + 10, TILE_W - 12, 3, COL_STAIRS);
        break;
    }
}

static void draw_map(void) {
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++)
            draw_tile(x, y);
}

static void draw_entities(void) {
    /* Items */
    for (int i = 0; i < MAX_ITEMS; i++) {
        if (!items[i].active) continue;
        int sx = MAP_OX + items[i].x * TILE_W + TILE_W/2 - 3;
        int sy = MAP_OY + items[i].y * TILE_H + TILE_H/2 - 3;
        uint16_t c = (items[i].type == ITEM_GOLD) ? COL_GOLD : COL_POTION;
        display_rect(sx, sy, 6, 6, c);
    }

    /* Enemies */
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].alive) continue;
        int sx = MAP_OX + enemies[i].x * TILE_W + 2;
        int sy = MAP_OY + enemies[i].y * TILE_H + 2;
        display_rect(sx, sy, TILE_W - 4, TILE_H - 4, enemies[i].color);
        /* Eyes */
        display_rect(sx + 2, sy + 2, 2, 2, COL_BG);
        display_rect(sx + TILE_W - 8, sy + 2, 2, 2, COL_BG);
        /* HP indicator */
        int hw = (TILE_W - 4) * enemies[i].hp / enemies[i].max_hp;
        display_rect(sx, sy + TILE_H - 5, TILE_W - 4, 2, COL_HP_BG);
        display_rect(sx, sy + TILE_H - 5, hw, 2, COL_HP_BAR);
    }

    /* Player */
    int sx = MAP_OX + px * TILE_W + 2;
    int sy = MAP_OY + py * TILE_H + 1;
    display_rect(sx, sy, TILE_W - 4, TILE_H - 2, COL_PLAYER);
    /* Face */
    display_rect(sx + 2, sy + 2, 2, 2, COL_BG);
    display_rect(sx + TILE_W - 8, sy + 2, 2, 2, COL_BG);
    display_rect(sx + 3, sy + 6, TILE_W - 10, 2, COL_BG);
}

static void draw_hud(void) {
    display_rect(0, 0, SCR_W, 20, COL_HUD_BG);

    /* HP bar */
    display_text(2, 4, "HP", COL_HP_BAR);
    display_rect(20, 4, 60, 10, COL_HP_BG);
    int hp_w = 60 * p_hp / p_max_hp;
    if (hp_w < 0) hp_w = 0;
    display_rect(20, 4, hp_w, 10, COL_HP_BAR);

    /* XP bar */
    display_text(86, 4, "XP", COL_XP_BAR);
    display_rect(104, 4, 40, 10, COL_HP_BG);
    int xp_w = 40 * p_xp / p_xp_next;
    display_rect(104, 4, xp_w, 10, COL_XP_BAR);

    /* Stats */
    display_text(150, 4, "Lv", COL_LABEL);
    display_number(166, 4, p_level, COL_HUD_TXT);

    display_text(190, 4, "F", COL_LABEL);
    display_number(200, 4, p_floor, COL_HUD_TXT);

    display_text(224, 4, "G", COL_GOLD);
    display_number(234, 4, p_gold, COL_GOLD);

    display_text(274, 4, "P", COL_POTION);
    display_number(284, 4, p_potions, COL_POTION);

    /* Message */
    if (msg_timer > 0) {
        display_rect(0, SCR_H - 16, SCR_W, 16, COL_HUD_BG);
        display_text(4, SCR_H - 14, msg_buf, COL_HUD_TXT);
    }
}

/* ── Pause menu ──────────────────────────────────────────────────────── */
enum { MENU_RESUME = 0, MENU_RESTART, MENU_EXIT, MENU_ITEMS };
static const char *menu_labels[] = { "Resume", "Restart", "Exit" };

static int show_pause_menu(void) {
    int cur = 0;
    int pu = 1, pd = 1, pa = 1, ps = 1;

    while (1) {
        display_rect(80, 60, 160, 120, 0x0000);
        display_rect_outline(80, 60, 160, 120, COL_TITLE);
        display_text(118, 70, "PAUSED", COL_TITLE);
        for (int i = 0; i < MENU_ITEMS; i++) {
            uint16_t cl = (i == cur) ? COL_TITLE : COL_LABEL;
            display_text(120, 95 + i * 18, menu_labels[i], cl);
            if (i == cur) display_text(108, 95 + i * 18, ">", cl);
        }
        display_flush();

        int u = gpio_read(BTN_UP);
        int d = gpio_read(BTN_DOWN);
        int a = gpio_read(BTN_A);
        int b = gpio_read(BTN_B);
        int s = gpio_read(BTN_SETTINGS);

        if (s && !ps) return MENU_RESUME;
        if (u && !pu) cur = (cur > 0) ? cur - 1 : MENU_ITEMS - 1;
        if (d && !pd) cur = (cur < MENU_ITEMS - 1) ? cur + 1 : 0;
        if ((a && !pa) || (b && !pa)) return cur;

        pu = u; pd = d; pa = a; ps = s;
        delay(20000);
    }
}

/* ── Input ───────────────────────────────────────────────────────────── */
static int prev_u, prev_d, prev_l, prev_r, prev_a, prev_b, prev_s;
static int turn_taken;

static int handle_input(void) {
    int u = gpio_read(BTN_UP);
    int d = gpio_read(BTN_DOWN);
    int l = gpio_read(BTN_LEFT);
    int r = gpio_read(BTN_RIGHT);
    int a = gpio_read(BTN_A);
    int b = gpio_read(BTN_B);
    int s = gpio_read(BTN_SETTINGS);

    turn_taken = 0;
    int nx = px, ny = py;

    if (u && !prev_u) { ny--; turn_taken = 1; }
    else if (d && !prev_d) { ny++; turn_taken = 1; }
    else if (l && !prev_l) { nx--; turn_taken = 1; }
    else if (r && !prev_r) { nx++; turn_taken = 1; }

    if (turn_taken) {
        if (nx >= 0 && nx < MAP_W && ny >= 0 && ny < MAP_H &&
            map[ny][nx] != T_WALL) {
            /* Check for enemy at target */
            int enemy_hit = -1;
            for (int i = 0; i < MAX_ENEMIES; i++) {
                if (enemies[i].alive &&
                    enemies[i].x == nx && enemies[i].y == ny) {
                    enemy_hit = i;
                    break;
                }
            }
            if (enemy_hit >= 0) {
                attack_enemy(enemy_hit);
            } else {
                px = nx;
                py = ny;
                try_pickup();
            }
        }
    }

    /* A = use stairs */
    if (a && !prev_a) {
        if (map[py][px] == T_STAIRS) {
            p_floor++;
            generate_map();
            set_msg("Descending...");
            turn_taken = 1;
        }
    }

    /* B = use potion */
    if (b && !prev_b) {
        if (p_potions > 0 && p_hp < p_max_hp) {
            p_potions--;
            int heal = 8 + p_level * 2;
            p_hp += heal;
            if (p_hp > p_max_hp) p_hp = p_max_hp;
            set_msg("Healed!");
        }
    }

    if (s && !prev_s) { prev_s = s; return 1; }

    prev_u = u; prev_d = d; prev_l = l; prev_r = r;
    prev_a = a; prev_b = b; prev_s = s;
    return 0;
}

/* ── Full redraw ─────────────────────────────────────────────────────── */
static void full_redraw(void) {
    display_clear(COL_BG);
    draw_map();
    draw_entities();
    draw_hud();
    display_flush();
}

/* ── Game loop ───────────────────────────────────────────────────────── */
static void game_loop(void) {
    full_redraw();

    while (!game_over) {
        if (handle_input()) {
            int choice = show_pause_menu();
            if (choice == MENU_EXIT) { exit_to_supervisor = 1; return; }
            if (choice == MENU_RESTART) { restart_game = 1; return; }
            full_redraw();
        }

        if (turn_taken) {
            move_enemies();
        }

        if (msg_timer > 0) msg_timer--;

        draw_map();
        draw_entities();
        draw_hud();
        display_flush();
        delay(FRAME_US);
    }
}

/* ── Entry point ─────────────────────────────────────────────────────── */
int main(void)
{
    printf("AkiraOS Pixel Dungeon v1.0");
    display_get_size(&SCR_W, &SCR_H);

    gpio_configure(BTN_UP,       GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_DOWN,     GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_LEFT,     GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_RIGHT,    GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_A,        GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_B,        GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_SETTINGS, GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);

    /* Title screen */
    display_clear(0x0000);
    display_text_large(56, 50, "PIXEL", COL_TITLE);
    display_text_large(28, 85, "DUNGEON", COL_DMG);
    display_text(66, 140, "Explore  Fight  Loot", COL_HUD_TXT);
    display_text(84, 170, "Press A to start", COL_HUD_TXT);
    display_text(80, 200, "AkiraOS Edition", COL_LABEL);
    display_flush();

    uint32_t seed = 1;
    while (!gpio_read(BTN_A) && !gpio_read(BTN_B)) {
        seed++;
        delay(20000);
    }
    rng = seed ^ 0xCAFEBEEF;
    rng_next(7); rng_next(7); rng_next(7);
    delay(100000);

    while (1) {
        restart_game = 0;
        exit_to_supervisor = 0;
        init_game();
        game_loop();

        if (exit_to_supervisor) {
            app_switch("supervisor");
            return 0;
        }
        if (restart_game) {
            seed++;
            rng = seed ^ 0xCAFEBEEF;
            continue;
        }
        break;
    }

    /* Death screen */
    display_clear(0x0000);
    display_text_large(68, 40, "YOU", COL_DMG);
    display_text_large(56, 75, "DIED", COL_DMG);
    display_text(100, 120, "FLOOR:", COL_LABEL);
    display_number(150, 120, p_floor, COL_HUD_TXT);
    display_text(100, 140, "LEVEL:", COL_LABEL);
    display_number(150, 140, p_level, COL_HUD_TXT);
    display_text(100, 160, "GOLD:", COL_LABEL);
    display_number(145, 160, p_gold, COL_GOLD);
    display_flush();
    delay(5000000);

    printf("Game over!");
    return 0;
}
