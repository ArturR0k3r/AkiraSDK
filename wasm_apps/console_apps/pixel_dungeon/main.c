/**
 * @file main.c
 * @brief Pixel Dungeon Remastered — roguelike for AkiraOS
 *
 * Controls:
 *   DPAD   = Move / bump-attack adjacent enemies
 *   A      = Use stairs (stand on them)
 *   B      = Drink health potion
 *   SETTINGS = Pause menu
 *
 * @copyright Copyright (c) 2026 PenEngineering S.R.L
 * @license Apache-2.0
 */

#include "akira_api.h"

/* ── Display ─────────────────────────────────────────────────────────── */
static int32_t SCR_W = 320;
static int32_t SCR_H = 240;

/* ── Buttons ─────────────────────────────────────────────────────────── */
#define BTN_UP       4
#define BTN_DOWN     5
#define BTN_LEFT     6
#define BTN_RIGHT    7
#define BTN_A        15
#define BTN_B        16
#define BTN_SETTINGS 0

/* ── Map layout ──────────────────────────────────────────────────────── */
#define MAP_W    20
#define MAP_H    13
#define TILE_W   16
#define TILE_H   16
static int32_t MAP_OX = 0;
static int32_t MAP_OY = 32;   /* below HUD */
static int32_t HUD_H  = 30;
static int32_t MSG_H  = 16;

#define FRAME_US 33333

enum { T_WALL = 0, T_FLOOR, T_STAIRS };
static uint8_t map[MAP_H][MAP_W];

/* ── Monochrome color palette (pure black & white only) ─────────────── */
#define COL_BG          0x0000  /* black */
#define COL_WHITE       0xFFFF  /* white */
#define COL_BLACK       0x0000  /* alias */

/* Tiles */
#define COL_WALL        0x0000  /* black solid wall */
#define COL_WALL_HI     0xFFFF  /* white edge — gives stone block depth */
#define COL_WALL_SH     0x0000
#define COL_FLOOR       0xFFFF  /* white open space */
#define COL_FLOOR_VAR   0xFFFF
#define COL_STAIR_BG    0xFFFF
#define COL_STAIR_RIM   0x0000
#define COL_STAIR_CORE  0xFFFF

/* Player: black silhouette on white floor */
#define COL_PLAYER      0x0000
#define COL_P_BODY      0x0000
#define COL_P_HELM      0x0000
#define COL_P_HIT       0xFFFF  /* invert (white silhouette) when hit */
#define COL_SWORD       0x0000
#define COL_SWORD_GLOW  0xFFFF

/* Enemies: black silhouettes — shapes distinguish types */
#define COL_GOB_BODY    0x0000
#define COL_GOB_HI      0xFFFF  /* white detail on goblin */
#define COL_ORC_BODY    0x0000
#define COL_ORC_HI      0xFFFF
#define COL_DEM_BODY    0x0000
#define COL_DEM_HI      0xFFFF
#define COL_EYE_YEL     0xFFFF  /* white eyes pop on black body */
#define COL_EYE_RED     0xFFFF
#define COL_TUSK        0xFFFF
#define COL_HORN        0xFFFF
#define COL_FLASH       0xFFFF  /* hit = white = silhouette inverts */

/* Items: black symbols on white floor */
#define COL_COIN        0x0000
#define COL_COIN_SH     0x0000
#define COL_POTION      0x0000
#define COL_POT_NECK    0x0000
#define COL_POT_SHINE   0xFFFF  /* white shine detail */

/* HUD: white text / bars on black */
#define COL_HUD_BG      0x0000
#define COL_HUD_LINE    0xFFFF
#define COL_HP_FULL     0xFFFF
#define COL_HP_MED      0xFFFF
#define COL_HP_LOW      0xFFFF
#define COL_HP_BG       0x0000
#define COL_XP_FG       0xFFFF
#define COL_XP_BG       0x0000
#define COL_GOLD_HUD    0xFFFF
#define COL_POT_HUD     0xFFFF
#define COL_LVL_HUD     0xFFFF
#define COL_FLOOR_HUD   0xFFFF

/* Messages: all white on black bar */
#define COL_MSG_DMG     0xFFFF
#define COL_MSG_HEAL    0xFFFF
#define COL_MSG_ITEM    0xFFFF
#define COL_MSG_LEVEL   0xFFFF
#define COL_MSG_INFO    0xFFFF

/* ── Entities ────────────────────────────────────────────────────────── */
#define MAX_ENEMIES  12
#define MAX_ITEMS     8

typedef struct {
    int16_t x, y;
    int16_t hp, max_hp;
    int8_t  atk;
    uint8_t alive;
    uint16_t color_body;
    uint16_t color_hi;
    uint8_t  type;   /* 0=goblin 1=orc 2=demon */
} enemy_t;

enum { ITEM_NONE = 0, ITEM_GOLD, ITEM_POTION };

typedef struct {
    int8_t x, y;
    uint8_t type;
    uint8_t active;
} item_t;

static enemy_t enemies[MAX_ENEMIES];
static item_t  items[MAX_ITEMS];

/* ── Player ──────────────────────────────────────────────────────────── */
static int px, py;
static int p_hp, p_max_hp, p_atk, p_def;
static int p_xp, p_xp_next, p_level;
static int p_gold, p_potions;
static int p_floor;
static int game_over;
static int exit_flag, restart_flag;

/* ── Animation state ─────────────────────────────────────────────────── */
static uint8_t  anim_tick;
static uint8_t  enemy_flash[MAX_ENEMIES];  /* white flash countdown */
static uint8_t  player_flash;
static uint8_t  levelup_anim;
static uint8_t  pickup_anim;
static int8_t   pickup_x, pickup_y;

/* Floating damage numbers */
#define MAX_FLOATS 4
typedef struct {
    int16_t sx, sy;
    int16_t val;
    uint8_t timer;
    uint8_t col_idx; /* 0=dmg 1=heal 2=item 3=level */
} float_t;
static float_t floats[MAX_FLOATS];

/* ── Message log ─────────────────────────────────────────────────────── */
static char msg_buf[44];
static uint8_t msg_timer;
static uint16_t msg_color;

static void set_msg(const char *s, uint16_t col) {
    int i;
    for (i = 0; s[i] && i < 43; i++) msg_buf[i] = s[i];
    msg_buf[i] = '\0';
    msg_timer = 80;
    msg_color = col;
}

/* ── PRNG ────────────────────────────────────────────────────────────── */
static uint32_t rng_state = 99999;
static int rng_next(int mod) {
    rng_state = rng_state * 1103515245u + 12345u;
    return (int)((rng_state >> 16) & 0x7FFF) % mod;
}

/* ── Floating numbers ────────────────────────────────────────────────── */
static void spawn_float(int tx, int ty, int val, int col_idx) {
    for (int i = 0; i < MAX_FLOATS; i++) {
        if (!floats[i].timer) {
            floats[i].sx  = (int16_t)(MAP_OX + tx * TILE_W + TILE_W/2 - 4);
            floats[i].sy  = (int16_t)(MAP_OY + ty * TILE_H);
            floats[i].val = (int16_t)val;
            floats[i].timer = 28;
            floats[i].col_idx = (uint8_t)col_idx;
            return;
        }
    }
}

/* ── Dungeon generation ──────────────────────────────────────────────── */
static void gen_room(int rx, int ry, int rw, int rh) {
    for (int y = ry; y < ry + rh && y < MAP_H; y++)
        for (int x = rx; x < rx + rw && x < MAP_W; x++)
            map[y][x] = T_FLOOR;
}

static void gen_h(int x1, int x2, int y) {
    int a = x1 < x2 ? x1 : x2, b = x1 < x2 ? x2 : x1;
    for (int x = a; x <= b && x < MAP_W; x++)
        if (y >= 0 && y < MAP_H) map[y][x] = T_FLOOR;
}

static void gen_v(int y1, int y2, int x) {
    int a = y1 < y2 ? y1 : y2, b = y1 < y2 ? y2 : y1;
    for (int y = a; y <= b && y < MAP_H; y++)
        if (x >= 0 && x < MAP_W) map[y][x] = T_FLOOR;
}

static void generate_map(void) {
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++)
            map[y][x] = T_WALL;

    int nr = 4 + rng_next(3);
    int cx[7], cy[7];

    for (int i = 0; i < nr; i++) {
        int rw = 3 + rng_next(4);
        int rh = 3 + rng_next(3);
        int rx = 1 + rng_next(MAP_W - rw - 2);
        int ry = 1 + rng_next(MAP_H - rh - 2);
        gen_room(rx, ry, rw, rh);
        cx[i] = rx + rw/2;
        cy[i] = ry + rh/2;
        if (i > 0) {
            if (rng_next(2)) { gen_h(cx[i-1], cx[i], cy[i-1]); gen_v(cy[i-1], cy[i], cx[i]); }
            else              { gen_v(cy[i-1], cy[i], cx[i-1]); gen_h(cx[i-1], cx[i], cy[i]); }
        }
    }

    px = cx[0]; py = cy[0];
    map[cy[nr-1]][cx[nr-1]] = T_STAIRS;

    int ne = 3 + p_floor;
    if (ne > MAX_ENEMIES) ne = MAX_ENEMIES;
    for (int i = 0; i < MAX_ENEMIES; i++) enemies[i].alive = 0;

    for (int i = 0; i < ne; i++) {
        for (int t = 50; t > 0; t--) {
            int ex = 1 + rng_next(MAP_W-2);
            int ey = 1 + rng_next(MAP_H-2);
            if (map[ey][ex] == T_FLOOR && !(ex==px && ey==py)) {
                enemies[i].x = (int16_t)ex;
                enemies[i].y = (int16_t)ey;
                enemies[i].max_hp = (int16_t)(2 + p_floor + rng_next(p_floor+1));
                enemies[i].hp     = enemies[i].max_hp;
                enemies[i].atk    = (int8_t)(1 + p_floor/2 + rng_next(2));
                enemies[i].alive  = 1;
                if (p_floor <= 2) {
                    enemies[i].type = 0; /* goblin */
                    enemies[i].color_body = COL_GOB_BODY;
                    enemies[i].color_hi   = COL_GOB_HI;
                } else if (p_floor <= 5) {
                    enemies[i].type = 1; /* orc */
                    enemies[i].color_body = COL_ORC_BODY;
                    enemies[i].color_hi   = COL_ORC_HI;
                } else {
                    enemies[i].type = 2; /* demon */
                    enemies[i].color_body = COL_DEM_BODY;
                    enemies[i].color_hi   = COL_DEM_HI;
                }
                break;
            }
        }
    }

    int ni = 2 + rng_next(3);
    if (ni > MAX_ITEMS) ni = MAX_ITEMS;
    for (int i = 0; i < MAX_ITEMS; i++) items[i].active = 0;

    for (int i = 0; i < ni; i++) {
        for (int t = 50; t > 0; t--) {
            int ix = 1 + rng_next(MAP_W-2);
            int iy = 1 + rng_next(MAP_H-2);
            if (map[iy][ix] == T_FLOOR && !(ix==px && iy==py)) {
                items[i].x = (int8_t)ix;
                items[i].y = (int8_t)iy;
                items[i].type   = (rng_next(3) == 0) ? ITEM_POTION : ITEM_GOLD;
                items[i].active = 1;
                break;
            }
        }
    }

    /* Reset all animation on new floor */
    for (int i = 0; i < MAX_ENEMIES; i++) enemy_flash[i] = 0;
    for (int i = 0; i < MAX_FLOATS; i++) floats[i].timer = 0;
    player_flash = 0;
}

/* ── Init ────────────────────────────────────────────────────────────── */
static void init_game(void) {
    p_hp=20; p_max_hp=20; p_atk=3; p_def=1;
    p_xp=0; p_xp_next=10; p_level=1;
    p_gold=0; p_potions=1; p_floor=1;
    game_over=0;
    msg_buf[0]='\0'; msg_timer=0;
    levelup_anim=0; pickup_anim=0; anim_tick=0;
    generate_map();
}

/* ── Level up ────────────────────────────────────────────────────────── */
static void check_level_up(void) {
    while (p_xp >= p_xp_next) {
        p_xp -= p_xp_next;
        p_level++;
        p_max_hp += 5;
        p_hp = p_max_hp;
        p_atk++;
        p_def++;
        p_xp_next += 5 + p_level * 2;
        levelup_anim = 50;
        set_msg("*** LEVEL UP! ***", COL_MSG_LEVEL);
    }
}

/* ── Combat ──────────────────────────────────────────────────────────── */
static void attack_enemy(int idx) {
    enemy_t *e = &enemies[idx];
    int dmg = p_atk - rng_next(2);
    if (dmg < 1) dmg = 1;
    e->hp -= (int16_t)dmg;
    enemy_flash[idx] = 8;
    spawn_float(e->x, e->y, dmg, 0);

    if (e->hp <= 0) {
        e->alive = 0;
        int xp = e->max_hp + e->atk;
        p_xp += xp;
        set_msg("Enemy slain!", COL_MSG_ITEM);
        check_level_up();
    } else {
        set_msg("Hit!", COL_MSG_DMG);
    }
}

static void enemy_attacks(int idx) {
    enemy_t *e = &enemies[idx];
    int dmg = e->atk - p_def + rng_next(2);
    if (dmg < 1) dmg = 1;
    p_hp -= dmg;
    player_flash = 8;
    spawn_float(px, py, dmg, 0);
    if (p_hp <= 0) { p_hp=0; game_over=1; set_msg("You died!", COL_MSG_DMG); }
    else { set_msg("Hit by enemy!", COL_MSG_DMG); }
}

/* ── Enemy AI ────────────────────────────────────────────────────────── */
static void move_enemies(void) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].alive) continue;
        int ex = enemies[i].x, ey = enemies[i].y;
        int dx = px - ex, dy = py - ey;
        int dist = (dx<0?-dx:dx) + (dy<0?-dy:dy);
        if (dist <= 1) { enemy_attacks(i); continue; }
        if (dist > 7) continue;

        int nx = ex, ny = ey;
        if ((dx<0?-dx:dx) >= (dy<0?-dy:dy)) nx += (dx>0)?1:-1;
        else                                  ny += (dy>0)?1:-1;

        if (nx<0||nx>=MAP_W||ny<0||ny>=MAP_H) continue;
        if (map[ny][nx]==T_WALL) continue;
        if (nx==px && ny==py) { enemy_attacks(i); continue; }

        int blocked=0;
        for (int j=0; j<MAX_ENEMIES; j++)
            if (j!=i && enemies[j].alive && enemies[j].x==nx && enemies[j].y==ny)
                { blocked=1; break; }
        if (!blocked) { enemies[i].x=(int16_t)nx; enemies[i].y=(int16_t)ny; }
    }
}

/* ── Pickup ──────────────────────────────────────────────────────────── */
static void try_pickup(void) {
    for (int i = 0; i < MAX_ITEMS; i++) {
        if (!items[i].active) continue;
        if (items[i].x==px && items[i].y==py) {
            if (items[i].type==ITEM_GOLD) {
                int g = 3 + rng_next(5) + p_floor;
                p_gold += g;
                spawn_float(px, py, g, 2);
                set_msg("Gold collected!", COL_MSG_ITEM);
            } else {
                p_potions++;
                spawn_float(px, py, 1, 1);
                set_msg("Found a potion!", COL_MSG_HEAL);
            }
            pickup_anim=20; pickup_x=(int8_t)px; pickup_y=(int8_t)py;
            items[i].active=0;
        }
    }
}

/* ── Drawing: tiles ──────────────────────────────────────────────────── */
static void draw_tile(int x, int y) {
    int sx = MAP_OX + x * TILE_W;
    int sy = MAP_OY + y * TILE_H;

    switch (map[y][x]) {
    case T_WALL:
        /* Solid black fill */
        display_rect(sx, sy, TILE_W, TILE_H, COL_BLACK);
        /* White top + left edge → raised stone block look */
        display_hline(sx, sy, TILE_W, COL_WHITE);
        display_vline(sx, sy, TILE_H, COL_WHITE);
        /* Horizontal mortar line (half-brick offset per row) */
        if ((y&1)==0)
            display_hline(sx,        sy+TILE_H/2, TILE_W,   COL_WHITE);
        else
            display_hline(sx+TILE_W/2, sy+TILE_H/2, TILE_W/2, COL_WHITE);
        break;

    case T_FLOOR:
        /* White floor with tiny black corner dots for texture */
        display_rect(sx, sy, TILE_W, TILE_H, COL_WHITE);
        display_pixel(sx+2,        sy+2,        COL_BLACK);
        display_pixel(sx+TILE_W-3, sy+TILE_H-3, COL_BLACK);
        break;

    case T_STAIRS: {
        /* White tile with animated black X / portal symbol */
        int pulse = (anim_tick >> 3) & 1;
        display_rect(sx, sy, TILE_W, TILE_H, COL_WHITE);
        /* Black border */
        display_rect_outline(sx+1, sy+1, TILE_W-2, TILE_H-2, COL_BLACK);
        /* Inner square */
        display_rect(sx+4, sy+4, TILE_W-8, TILE_H-8, COL_BLACK);
        /* Pulsing centre dot */
        if (pulse)
            display_rect(sx+6, sy+6, TILE_W-12, TILE_H-12, COL_WHITE);
        break;
    }
    }
}

static void draw_map(void) {
    for (int y=0; y<MAP_H; y++)
        for (int x=0; x<MAP_W; x++)
            draw_tile(x, y);
}

/* ── Drawing: entities ───────────────────────────────────────────────── */

/*
 * All entities are drawn as black silhouettes on the white floor.
 * flash=1 inverts the silhouette to white (visible "hit" effect on white bg).
 * Each enemy type has a clearly distinct silhouette shape.
 */

static void draw_goblin(int sx, int sy, int flash) {
    /* Small round body with pointy ears — clearly the weakest foe */
    uint16_t fg = flash ? COL_WHITE : COL_BLACK;
    uint16_t bg = flash ? COL_BLACK : COL_WHITE;
    /* Tile background */
    display_rect(sx, sy, TILE_W, TILE_H, bg);
    /* Pointy ears (triangles) */
    display_triangle_fill(sx+1, sy+5, sx+4, sy+1, sx+5, sy+5, fg);
    display_triangle_fill(sx+TILE_W-6, sy+5, sx+TILE_W-5, sy+1, sx+TILE_W-2, sy+5, fg);
    /* Round body */
    display_rect(sx+3, sy+3, TILE_W-6, TILE_H-6, fg);
    /* White eye whites on black body */
    display_rect(sx+4, sy+5, 3, 3, bg);
    display_rect(sx+TILE_W-7, sy+5, 3, 3, bg);
    /* Black pupils */
    display_pixel(sx+5, sy+6, fg);
    display_pixel(sx+TILE_W-6, sy+6, fg);
    /* Toothy grin (alternating pixels) */
    display_pixel(sx+5,  sy+10, bg);
    display_pixel(sx+7,  sy+10, bg);
    display_pixel(sx+9,  sy+10, bg);
    display_pixel(sx+11, sy+10, bg);
}

static void draw_orc(int sx, int sy, int flash) {
    /* Wide, square body — bigger and heavier than goblin */
    uint16_t fg = flash ? COL_WHITE : COL_BLACK;
    uint16_t bg = flash ? COL_BLACK : COL_WHITE;
    display_rect(sx, sy, TILE_W, TILE_H, bg);
    /* Wide shoulders + thick body */
    display_rect(sx, sy+4, TILE_W, TILE_H-6, fg);
    /* Head (square) */
    display_rect(sx+2, sy+1, TILE_W-4, 5, fg);
    /* White eyes (wide-set, angry) */
    display_rect(sx+3, sy+2, 2, 2, bg);
    display_rect(sx+TILE_W-5, sy+2, 2, 2, bg);
    /* White tusks protruding below face */
    display_rect(sx+4,        sy+6, 2, 4, bg);
    display_rect(sx+TILE_W-6, sy+6, 2, 4, bg);
}

static void draw_demon(int sx, int sy, int flash) {
    /* Horned winged shape — most menacing */
    uint16_t fg = flash ? COL_WHITE : COL_BLACK;
    uint16_t bg = flash ? COL_BLACK : COL_WHITE;
    display_rect(sx, sy, TILE_W, TILE_H, bg);
    /* Swept horn left */
    display_triangle_fill(sx+1, sy+5, sx+3, sy+0, sx+6, sy+5, fg);
    /* Swept horn right */
    display_triangle_fill(sx+TILE_W-7, sy+5, sx+TILE_W-4, sy+0, sx+TILE_W-2, sy+5, fg);
    /* Body */
    display_rect(sx+2, sy+4, TILE_W-4, TILE_H-6, fg);
    /* Wide wings (side bars) */
    display_rect(sx,          sy+6, 3, 5, fg);
    display_rect(sx+TILE_W-3, sy+6, 3, 5, fg);
    /* White glowing eyes */
    display_rect(sx+4, sy+6, 3, 2, bg);
    display_rect(sx+TILE_W-7, sy+6, 3, 2, bg);
    /* Snarl line */
    display_hline(sx+5, sy+10, TILE_W-10, bg);
}

static void draw_enemy(int i) {
    enemy_t *e = &enemies[i];
    if (!e->alive) return;
    int sx = MAP_OX + e->x * TILE_W;
    int sy = MAP_OY + e->y * TILE_H;
    int fl = enemy_flash[i] > 0;

    switch (e->type) {
    case 0: draw_goblin(sx, sy, fl); break;
    case 1: draw_orc(sx, sy, fl);    break;
    case 2: draw_demon(sx, sy, fl);  break;
    }

    /* HP bar: white outline, white fill proportional to remaining HP
     * drawn inside the tile so it overlaps the silhouette bottom edge */
    int bar_w = TILE_W - 4;
    int hp_w  = bar_w * e->hp / e->max_hp;
    display_rect(sx+2, sy+TILE_H-3, bar_w, 2, COL_BLACK);  /* empty = black */
    display_rect(sx+2, sy+TILE_H-3, hp_w,  2, COL_WHITE);  /* filled = white */
}

static void draw_player(void) {
    int sx = MAP_OX + px * TILE_W;
    int sy = MAP_OY + py * TILE_H;
    int fl = player_flash > 0;

    /* On hit: invert (white silhouette on black tile) */
    uint16_t fg = fl ? COL_WHITE : COL_BLACK;
    uint16_t bg = fl ? COL_BLACK : COL_WHITE;

    /* Clear tile */
    display_rect(sx, sy, TILE_W, TILE_H, bg);

    /* Legs */
    display_rect(sx+3, sy+10, 3, 5, fg);
    display_rect(sx+TILE_W-6, sy+10, 3, 5, fg);
    /* Torso armor */
    display_rect(sx+2, sy+5, TILE_W-4, 6, fg);
    /* Shoulder pads */
    display_rect(sx,          sy+5, 3, 3, fg);
    display_rect(sx+TILE_W-3, sy+5, 3, 3, fg);
    /* Head / helmet */
    display_rect(sx+3, sy+1, TILE_W-6, 5, fg);
    /* Helmet crest stripe */
    display_hline(sx+3, sy+1, TILE_W-6, bg);
    /* Visor slit (opposite color = visible) */
    display_hline(sx+5, sy+3, TILE_W-10, bg);
    /* Sword (right side, slightly outside body) */
    display_rect(sx+TILE_W-2, sy+3, 2, 8, fg);
    display_hline(sx+TILE_W-5, sy+6, 5, fg);  /* guard */
}

static void draw_items(void) {
    for (int i = 0; i < MAX_ITEMS; i++) {
        if (!items[i].active) continue;
        int sx = MAP_OX + items[i].x * TILE_W;
        int sy = MAP_OY + items[i].y * TILE_H;

        /* Items sit on white floor → draw as black symbols */
        if (items[i].type == ITEM_GOLD) {
            /* Coin: filled circle approximation with $ mark */
            int cx = sx + TILE_W/2, cy = sy + TILE_H/2;
            display_circle_fill(cx, cy, 4, COL_BLACK);
            /* White $ detail */
            display_vline(cx, cy-3, 6, COL_WHITE);
            display_hline(cx-2, cy-1, 4, COL_WHITE);
            display_hline(cx-2, cy+1, 4, COL_WHITE);
            /* Blinking glint */
            if (anim_tick & 16)
                display_pixel(cx+2, cy-3, COL_WHITE);
        } else {
            /* Potion flask: distinctive cross/flask shape */
            int fx = sx + TILE_W/2 - 2, fy = sy + TILE_H/2 - 5;
            /* Neck */
            display_rect(fx+1, fy,   2, 2, COL_BLACK);
            /* Stopper */
            display_rect(fx,   fy-1, 4, 2, COL_BLACK);
            /* Round body */
            display_rect(fx-1, fy+2, 6, 6, COL_BLACK);
            /* White shine dot */
            display_pixel(fx+1, fy+3, COL_WHITE);
            /* Pulsing outline */
            if (anim_tick & 8)
                display_rect_outline(fx-2, fy+1, 8, 8, COL_BLACK);
        }
    }
}

static void draw_floats_fn(void) {
    static const uint16_t float_cols[4] = {
        COL_MSG_DMG, COL_MSG_HEAL, COL_MSG_ITEM, COL_MSG_LEVEL
    };
    for (int i = 0; i < MAX_FLOATS; i++) {
        if (!floats[i].timer) continue;
        int rise = (28 - floats[i].timer) / 2;
        uint16_t col = float_cols[floats[i].col_idx & 3];
        display_number(floats[i].sx, floats[i].sy - rise, floats[i].val, col);
        floats[i].timer--;
    }
}

/* ── HUD ─────────────────────────────────────────────────────────────── */
static void draw_hud(void) {
    /* Background */
    display_rect(0, 0, SCR_W, HUD_H, COL_HUD_BG);
    display_hline(0, HUD_H, SCR_W, COL_HUD_LINE);

    /* HP bar — monochrome: white fill, border flashes on low HP */
    uint16_t hp_col = COL_WHITE;

    display_text(3, 6, "HP", hp_col);
    display_rect(22, 5, 70, 11, COL_HP_BG);
    int hw = 70 * p_hp / p_max_hp;
    if (hw < 0) hw = 0;
    display_rect(22, 5, hw, 11, hp_col);
    display_rect_outline(22, 5, 70, 11, hp_col);

    /* XP bar */
    display_text(96, 6, "XP", COL_XP_FG);
    display_rect(115, 5, 45, 11, COL_XP_BG);
    int xw = 45 * p_xp / p_xp_next;
    display_rect(115, 5, xw, 11, COL_XP_FG);
    display_rect_outline(115, 5, 45, 11, COL_XP_FG);

    /* Level */
    display_text(164, 6, "Lv", COL_LVL_HUD);
    display_number(180, 6, p_level, COL_WHITE);

    /* Floor */
    display_text(204, 6, "F", COL_FLOOR_HUD);
    display_number(214, 6, p_floor, COL_WHITE);

    /* Gold: coin icon (circle outline) */
    display_circle(236, 10, 4, COL_WHITE);
    display_number(244, 6, p_gold, COL_WHITE);

    /* Potions: flask icon */
    display_rect(278, 8, 3, 4, COL_WHITE);
    display_hline(276, 7, 7, COL_WHITE);
    display_number(288, 6, p_potions, COL_WHITE);

    /* Message bar */
    if (msg_timer > 0) {
        display_rect(0, SCR_H-MSG_H, SCR_W, MSG_H, COL_HUD_BG);
        display_hline(0, SCR_H-MSG_H-1, SCR_W, COL_HUD_LINE);
        display_text(4, SCR_H-MSG_H+3, msg_buf, msg_color);
        msg_timer--;
    }

    /* Level-up flash border — alternates white/black for visibility */
    if (levelup_anim > 0) {
        uint16_t lc = (levelup_anim & 4) ? COL_WHITE : COL_BLACK;
        display_rect_outline(0, HUD_H+1, SCR_W, SCR_H-HUD_H-2, lc);
        display_rect_outline(1, HUD_H+2, SCR_W-2, SCR_H-HUD_H-4, lc);
        levelup_anim--;
    }
}

/* ── Pause menu ──────────────────────────────────────────────────────── */
enum { MENU_RESUME=0, MENU_RESTART, MENU_EXIT, MENU_COUNT };
static const char *menu_labels[] = { "Resume", "Restart", "Exit" };

static int show_pause_menu(void) {
    int cur=0;
    int pu=1,pd=1,pa=1,ps=1;
    int box_x = SCR_W*25/100, box_y = SCR_H*23/100;
    int box_w = SCR_W*50/100, box_h = SCR_H*54/100;
    while (1) {
        display_rounded_rect_fill(box_x, box_y, box_w, box_h, 6, COL_BLACK);
        display_rounded_rect(box_x, box_y, box_w, box_h, 6, COL_HUD_LINE);
        display_text_large(box_x+30, box_y+10, "PAUSED", COL_FLOOR_HUD);
        display_hline(box_x+8, box_y+30, box_w-16, COL_HUD_LINE);
        for (int i=0; i<MENU_COUNT; i++) {
            uint16_t cl = (i==cur) ? COL_WHITE : COL_MSG_INFO;
            if (i==cur) {
                display_rect(box_x+10, box_y+38+i*22, box_w-20, 18, COL_WHITE);
                display_rect_outline(box_x+10, box_y+38+i*22, box_w-20, 18, COL_FLOOR_HUD);
                display_text(box_x+22, box_y+42+i*22, menu_labels[i], COL_BLACK);
            } else {
                display_text(box_x+22, box_y+42+i*22, menu_labels[i], cl);
            }
        }
        display_flush();

        int u=gpio_read(BTN_UP), d=gpio_read(BTN_DOWN);
        int a=gpio_read(BTN_A),  b=gpio_read(BTN_B), s=gpio_read(BTN_SETTINGS);
        if (s && !ps) return MENU_RESUME;
        if (u && !pu) cur = (cur>0) ? cur-1 : MENU_COUNT-1;
        if (d && !pd) cur = (cur<MENU_COUNT-1) ? cur+1 : 0;
        if ((a && !pa) || (b && !pa)) return cur;
        pu=u; pd=d; pa=a; ps=s;
        delay(20000);
    }
}

/* ── Input ───────────────────────────────────────────────────────────── */
static int pu,pd,pl,pr,pa,pb,ps;
static int turn_taken;

static int handle_input(void) {
    int u=gpio_read(BTN_UP),   d=gpio_read(BTN_DOWN);
    int l=gpio_read(BTN_LEFT), r=gpio_read(BTN_RIGHT);
    int a=gpio_read(BTN_A),    b=gpio_read(BTN_B), s=gpio_read(BTN_SETTINGS);

    turn_taken=0;
    int nx=px, ny=py;
    if (u&&!pu) { ny--; turn_taken=1; }
    else if (d&&!pd) { ny++; turn_taken=1; }
    else if (l&&!pl) { nx--; turn_taken=1; }
    else if (r&&!pr) { nx++; turn_taken=1; }

    if (turn_taken) {
        if (nx>=0&&nx<MAP_W&&ny>=0&&ny<MAP_H&&map[ny][nx]!=T_WALL) {
            int hit=-1;
            for (int i=0; i<MAX_ENEMIES; i++)
                if (enemies[i].alive && enemies[i].x==nx && enemies[i].y==ny)
                    { hit=i; break; }
            if (hit>=0) attack_enemy(hit);
            else { px=nx; py=ny; try_pickup(); }
        }
    }

    if (a&&!pa) {
        if (map[py][px]==T_STAIRS) {
            p_floor++;
            generate_map();
            set_msg("Descending deeper...", COL_FLOOR_HUD);
            turn_taken=1;
        }
    }

    if (b&&!pb) {
        if (p_potions>0 && p_hp<p_max_hp) {
            p_potions--;
            int heal = 8 + p_level*2;
            p_hp += heal;
            if (p_hp>p_max_hp) p_hp=p_max_hp;
            spawn_float(px, py, heal, 1);
            set_msg("Potion! HP restored.", COL_MSG_HEAL);
        } else if (p_potions<=0) {
            set_msg("No potions!", COL_MSG_INFO);
        } else {
            set_msg("HP is full.", COL_MSG_INFO);
        }
    }

    if (s&&!ps) { ps=s; return 1; }
    pu=u; pd=d; pl=l; pr=r; pa=a; pb=b; ps=s;
    return 0;
}

/* ── Animation tick ──────────────────────────────────────────────────── */
static void tick_anims(void) {
    anim_tick++;
    for (int i=0; i<MAX_ENEMIES; i++)
        if (enemy_flash[i]) enemy_flash[i]--;
    if (player_flash) player_flash--;
    if (pickup_anim)  pickup_anim--;
}

/* ── Full frame ──────────────────────────────────────────────────────── */
static void draw_frame(void) {
    display_clear(COL_BG);
    draw_map();
    draw_items();
    for (int i=0; i<MAX_ENEMIES; i++) draw_enemy(i);
    draw_player();
    draw_floats_fn();
    draw_hud();
    display_flush();
}

/* ── Pickup sparkle overlay ──────────────────────────────────────────── */
static void draw_pickup_sparkle(void) {
    if (!pickup_anim) return;
    int sx = MAP_OX + pickup_x * TILE_W + TILE_W/2;
    int sy = MAP_OY + pickup_y * TILE_H + TILE_H/2;
    int r  = pickup_anim / 3 + 1;
    /* Black expanding ring on white floor */
    display_circle(sx, sy, r, COL_BLACK);
    if (pickup_anim > 10)
        display_circle(sx, sy, r + 2, COL_BLACK);
}

/* ── Game loop ───────────────────────────────────────────────────────── */
static void game_loop(void) {
    draw_frame();
    while (!game_over) {
        if (handle_input()) {
            int c = show_pause_menu();
            if (c==MENU_EXIT)    { exit_flag=1; return; }
            if (c==MENU_RESTART) { restart_flag=1; return; }
        }
        if (turn_taken) move_enemies();
        tick_anims();
        draw_frame();
        draw_pickup_sparkle();
        if (pickup_anim) display_flush();
        delay(FRAME_US);
    }
}

/* ── Title screen ────────────────────────────────────────────────────── */
static void draw_title(void) {
    display_clear(COL_BG);

    /* Decorative walls around edges */
    for (int x=0; x<SCR_W; x+=8) {
        display_rect(x, 0, 8, 8, COL_WALL);
        display_rect(x, SCR_H-8, 8, 8, COL_WALL);
    }
    for (int y=8; y<SCR_H-8; y+=8) {
        display_rect(0, y, 8, 8, COL_WALL);
        display_rect(SCR_W-8, y, 8, 8, COL_WALL);
    }

    /* Title */
    display_text_large(SCR_W*22/100, SCR_H*19/100, "PIXEL", COL_FLOOR_HUD);
    display_text_large(SCR_W*11/100, SCR_H*31/100, "DUNGEON", COL_LVL_HUD);
    display_hline(SCR_W*6/100, SCR_H*43/100, SCR_W*88/100, COL_HUD_LINE);

    /* Subtitle */
    display_text(SCR_W*22/100, SCR_H*48/100, "Explore  Fight  Survive", COL_MSG_INFO);

    /* Controls hint */
    display_text(SCR_W*16/100, SCR_H*60/100, "DPAD:Move/Attack", COL_WHITE);
    display_text(SCR_W*16/100, SCR_H*67/100, "A:Stairs  B:Potion", COL_WHITE);
    display_text(SCR_W*16/100, SCR_H*73/100, "SETTINGS:Pause", COL_WHITE);

    display_hline(SCR_W*6/100, SCR_H*80/100, SCR_W*88/100, COL_HUD_LINE);
    display_text(SCR_W*25/100, SCR_H*83/100, "Press A to begin!", COL_FLOOR_HUD);
    display_text(SCR_W*24/100, SCR_H*90/100, "AkiraOS Edition", COL_MSG_INFO);
    display_flush();
}

/* ── Death screen ────────────────────────────────────────────────────── */
static void draw_death(void) {
    display_clear(COL_BG);
    display_rect_outline(10, 10, SCR_W-20, SCR_H-20, COL_WHITE);
    display_rect_outline(12, 12, SCR_W-24, SCR_H-24, COL_WHITE);
    display_text_large(SCR_W*28/100, SCR_H*15/100, "YOU", COL_MSG_DMG);
    display_text_large(SCR_W*24/100, SCR_H*27/100, "DIED", COL_MSG_DMG);
    display_hline(SCR_W*9/100, SCR_H*40/100, SCR_W*82/100, COL_HUD_LINE);

    display_text(SCR_W*25/100, SCR_H*45/100, "Final Stats:", COL_WHITE);
    display_text(SCR_W*25/100, SCR_H*52/100, "Floor  :", COL_MSG_INFO);
    display_number(SCR_W*50/100, SCR_H*52/100, p_floor, COL_WHITE);
    display_text(SCR_W*25/100, SCR_H*59/100, "Level  :", COL_MSG_INFO);
    display_number(SCR_W*50/100, SCR_H*59/100, p_level, COL_LVL_HUD);
    display_text(SCR_W*25/100, SCR_H*66/100, "Gold   :", COL_MSG_INFO);
    display_number(SCR_W*50/100, SCR_H*66/100, p_gold, COL_GOLD_HUD);
    display_text(SCR_W*25/100, SCR_H*73/100, "Potions:", COL_MSG_INFO);
    display_number(SCR_W*50/100, SCR_H*73/100, p_potions, COL_POT_HUD);

    display_hline(SCR_W*9/100, SCR_H*81/100, SCR_W*82/100, COL_HUD_LINE);
    display_text(SCR_W*23/100, SCR_H*85/100, "Press A to restart", COL_MSG_INFO);
    display_flush();
}

/* ── Entry point ─────────────────────────────────────────────────────── */
int main(void) {
    printf("AkiraOS Pixel Dungeon Remastered");

    display_get_size(&SCR_W, &SCR_H);
    HUD_H = SCR_H * 12 / 100;
    MSG_H = SCR_H * 7 / 100;
    MAP_OX = (SCR_W - MAP_W * TILE_W) / 2;
    MAP_OY = HUD_H + 2;

    gpio_configure(BTN_UP,       GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_DOWN,     GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_LEFT,     GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_RIGHT,    GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_A,        GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_B,        GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_SETTINGS, GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);

    draw_title();

    uint32_t seed = 1;
    while (!gpio_read(BTN_A) && !gpio_read(BTN_B)) { seed++; delay(20000); }
    rng_state = seed ^ 0xCAFEBEEF;
    rng_next(7); rng_next(7); rng_next(7);
    delay(80000);

    while (1) {
        restart_flag=0; exit_flag=0;
        init_game();
        game_loop();

        if (exit_flag) { app_switch("supervisor"); return 0; }
        if (restart_flag) { seed++; rng_state=seed^0xCAFEBEEF; continue; }

        /* Death */
        draw_death();
        while (!gpio_read(BTN_A) && !gpio_read(BTN_B)) delay(20000);
        delay(100000);
        seed++;
        rng_state = seed ^ 0xCAFEBEEF;
    }

    return 0;
}
