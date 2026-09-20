/**
 * @file main.c
 * @brief Pixel Dungeon Remastered — colour roguelike for AkiraOS
 *
 * Controls:
 *   DPAD     = Move / bump-attack adjacent enemies
 *   A        = Context action: descend stairs (opens shop), chest, shrine
 *   B        = Drink health potion
 *   X        = Open the scroll menu (teleport / fireball / freeze / reveal)
 *   SETTINGS = Pause menu (Resume / Character / Restart / Exit)
 *
 * Display: the palette is chosen at runtime from display_get_size().  The
 * 320x240 ST7789V gets a full RGB565 palette; the 400x240 Sharp Memory LCD
 * is 1-bit, so it keeps the original black/white scheme and expresses
 * torch-light falloff with outline-only tiles instead of dimmer colours.
 *
 * @copyright Copyright (c) 2026 PenEngineering S.R.L
 * @license Apache-2.0
 */

#include "akira_api.h"

/* ── Display ─────────────────────────────────────────────────────────── */
static int32_t SCR_W = 320;
static int32_t SCR_H = 240;
static int     g_mono = 0;   /* 1 = 1-bit Sharp panel, 0 = RGB565 */

/* ── Buttons ─────────────────────────────────────────────────────────── */
#define BTN_UP       4
#define BTN_DOWN     5
#define BTN_LEFT     6
#define BTN_RIGHT    7
#define BTN_A        15
#define BTN_B        16
#define BTN_X        17
#define BTN_SETTINGS 0

/* ── Map layout ──────────────────────────────────────────────────────── */
#define MAP_W    20
#define MAP_H    12
#define TILE_W   16
#define TILE_H   16
static int32_t MAP_OX = 0;
static int32_t MAP_OY = 32;   /* below HUD */
static int32_t HUD_H  = 30;
static int32_t MSG_H  = 16;

#define FRAME_US 33333

enum { T_WALL = 0, T_FLOOR, T_STAIRS, T_CHEST, T_SHRINE, T_SHRINE_USED };

static uint8_t map[MAP_H][MAP_W];
static uint8_t trap[MAP_H][MAP_W];   /* 0 none, 1 hidden, 2 revealed */
static uint8_t seen[MAP_H][MAP_W];   /* remembered (explored) */
static uint8_t vis[MAP_H][MAP_W];    /* lit by the torch right now */
static uint8_t litlvl[MAP_H][MAP_W]; /* 0 dark .. 4 bright — torch falloff */

/* ── Palette ─────────────────────────────────────────────────────────────
 * Runtime values, not macros, so one binary serves both panels.  "_D"
 * suffix = the dim variant used for remembered-but-unlit tiles.        */
static uint16_t C_BG;
/* tiles */
static uint16_t C_WALL, C_WALL_HI, C_WALL_SH, C_WALL_D, C_WALL_HI_D;
static uint16_t C_FLOOR, C_FLOOR_DOT, C_FLOOR_D;
static uint16_t C_STAIR, C_STAIR_CORE, C_STAIR_D;
static uint16_t C_CHEST, C_CHEST_TRIM, C_CHEST_D;
static uint16_t C_SHRINE, C_SHRINE_GLOW, C_SHRINE_USED_C, C_SHRINE_D;
static uint16_t C_TRAP;
/* player */
static uint16_t C_P_ARMOR, C_P_BODY, C_P_HELM, C_P_SKIN, C_SWORD, C_P_HIT;
/* enemies */
static uint16_t C_GOB, C_GOB_HI, C_ORC, C_ORC_HI, C_DEM, C_DEM_HI;
static uint16_t C_ARC, C_ARC_HI, C_BOSS, C_BOSS_HI;
static uint16_t C_MIMIC, C_MIMIC_HI, C_SUM, C_SUM_HI, C_HEALER, C_HEALER_HI;
static uint16_t C_EYE, C_FLASH;
/* items */
static uint16_t C_GOLD, C_GOLD_HI, C_POTION, C_POTION_HI, C_WEAPON, C_ARMOR_I;
static uint16_t C_SCROLL, C_SCROLL_HI;
/* hud + messages */
static uint16_t C_HUD_BG, C_HUD_LINE, C_TEXT, C_TEXT_DIM;
static uint16_t C_HP, C_HP_LOW, C_HP_BG, C_XP, C_XP_BG;
static uint16_t C_MSG_DMG, C_MSG_HEAL, C_MSG_ITEM, C_MSG_LEVEL, C_MSG_INFO;
static uint16_t C_BUFF, C_POISON, C_BURN, C_REGEN, C_FREEZE, C_TORCH;

static void palette_init(void)
{
    if (g_mono) {
        /* 1-bit Sharp panel: black walls with white edging, white floor,
         * black silhouettes.  Dim tiles are handled by draw_tile(). */
        C_BG = 0x0000;
        C_WALL = 0x0000; C_WALL_HI = 0xFFFF; C_WALL_SH = 0x0000;
        C_WALL_D = 0x0000; C_WALL_HI_D = 0xFFFF;
        C_FLOOR = 0xFFFF; C_FLOOR_DOT = 0x0000; C_FLOOR_D = 0x0000;
        C_STAIR = 0xFFFF; C_STAIR_CORE = 0x0000; C_STAIR_D = 0x0000;
        C_CHEST = 0x0000; C_CHEST_TRIM = 0xFFFF; C_CHEST_D = 0x0000;
        C_SHRINE = 0x0000; C_SHRINE_GLOW = 0xFFFF;
        C_SHRINE_USED_C = 0x0000; C_SHRINE_D = 0x0000;
        C_TRAP = 0x0000;

        C_P_ARMOR = 0x0000; C_P_BODY = 0x0000; C_P_HELM = 0x0000;
        C_P_SKIN = 0xFFFF; C_SWORD = 0x0000; C_P_HIT = 0xFFFF;

        C_GOB = 0x0000; C_GOB_HI = 0xFFFF;
        C_ORC = 0x0000; C_ORC_HI = 0xFFFF;
        C_DEM = 0x0000; C_DEM_HI = 0xFFFF;
        C_ARC = 0x0000; C_ARC_HI = 0xFFFF;
        C_BOSS = 0x0000; C_BOSS_HI = 0xFFFF;
        C_MIMIC = 0x0000; C_MIMIC_HI = 0xFFFF;
        C_SUM = 0x0000; C_SUM_HI = 0xFFFF;
        C_HEALER = 0x0000; C_HEALER_HI = 0xFFFF;
        C_EYE = 0xFFFF; C_FLASH = 0xFFFF;

        C_GOLD = 0x0000; C_GOLD_HI = 0xFFFF;
        C_POTION = 0x0000; C_POTION_HI = 0xFFFF;
        C_WEAPON = 0x0000; C_ARMOR_I = 0x0000;
        C_SCROLL = 0x0000; C_SCROLL_HI = 0xFFFF;

        C_HUD_BG = 0x0000; C_HUD_LINE = 0xFFFF;
        C_TEXT = 0xFFFF; C_TEXT_DIM = 0xFFFF;
        C_HP = 0xFFFF; C_HP_LOW = 0xFFFF; C_HP_BG = 0x0000;
        C_XP = 0xFFFF; C_XP_BG = 0x0000;
        C_MSG_DMG = 0xFFFF; C_MSG_HEAL = 0xFFFF; C_MSG_ITEM = 0xFFFF;
        C_MSG_LEVEL = 0xFFFF; C_MSG_INFO = 0xFFFF;
        C_BUFF = 0xFFFF;
        C_POISON = 0xFFFF; C_BURN = 0xFFFF; C_REGEN = 0xFFFF;
        C_FREEZE = 0xFFFF; C_TORCH = 0xFFFF;
    } else {
        /* RGB565 dungeon palette — warm torch-lit stone, cool shadows. */
        C_BG = 0x0000;
        C_WALL = 0x6B4D; C_WALL_HI = 0xA534; C_WALL_SH = 0x2124;
        C_WALL_D = 0x2965; C_WALL_HI_D = 0x4A49;
        C_FLOOR = 0x2945; C_FLOOR_DOT = 0x4A69; C_FLOOR_D = 0x10A2;
        C_STAIR = 0x07FF; C_STAIR_CORE = 0xFFFF; C_STAIR_D = 0x0410;
        C_CHEST = 0x9A60; C_CHEST_TRIM = 0xFFE0; C_CHEST_D = 0x4180;
        C_SHRINE = 0xA81F; C_SHRINE_GLOW = 0xFFFF;
        C_SHRINE_USED_C = 0x4208; C_SHRINE_D = 0x3009;
        C_TRAP = 0xF800;

        C_P_ARMOR = 0x4C9F; C_P_BODY = 0x2A5F; C_P_HELM = 0xBDF7;
        C_P_SKIN = 0xFE58; C_SWORD = 0xEF7D; C_P_HIT = 0xF800;

        C_GOB = 0x3E63; C_GOB_HI = 0x7FE0;
        C_ORC = 0x5B45; C_ORC_HI = 0x9CC0;
        C_DEM = 0xC000; C_DEM_HI = 0xFD20;
        C_ARC = 0x780F; C_ARC_HI = 0xF81F;
        C_BOSS = 0xF800; C_BOSS_HI = 0xFFE0;
        C_MIMIC = 0x9A60; C_MIMIC_HI = 0xF800;   /* chest colours, red tell */
        C_SUM = 0x8010; C_SUM_HI = 0xFD1F;        /* violet summoner */
        C_HEALER = 0x066C; C_HEALER_HI = 0x9FF3;  /* teal healer */
        C_EYE = 0xFFE0; C_FLASH = 0xFFFF;

        C_GOLD = 0xFFE0; C_GOLD_HI = 0xFFFF;
        C_POTION = 0xF81F; C_POTION_HI = 0xFFFF;
        C_WEAPON = 0xC618; C_ARMOR_I = 0xAD55;
        C_SCROLL = 0xFEB2; C_SCROLL_HI = 0xC408;  /* parchment + ribbon */

        C_HUD_BG = 0x10A2; C_HUD_LINE = 0x6B4D;
        C_TEXT = 0xFFFF; C_TEXT_DIM = 0x8410;
        C_HP = 0x07E0; C_HP_LOW = 0xF800; C_HP_BG = 0x3186;
        C_XP = 0x07FF; C_XP_BG = 0x3186;
        C_MSG_DMG = 0xF800; C_MSG_HEAL = 0x07E0; C_MSG_ITEM = 0xFFE0;
        C_MSG_LEVEL = 0xFD20; C_MSG_INFO = 0xC618;
        C_BUFF = 0xFD20;
        C_POISON = 0x2FE6; C_BURN = 0xFC00; C_REGEN = 0x07E6;
        C_FREEZE = 0x5DFF; C_TORCH = 0xFD40;
    }
}

/* ── Colour helper: scale an RGB565's brightness by lvl/4 (for torch dim) ─ */
static uint16_t dim565(uint16_t c, int lvl) {
    if (lvl >= 4) return c;
    if (lvl <= 0) return 0x0000;
    int r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
    r = r*lvl/4; g = g*lvl/4; b = b*lvl/4;
    return (uint16_t)((r<<11)|(g<<5)|b);
}

/* ── Equipment tables ────────────────────────────────────────────────── */
#define N_WEAPONS 6
static const char *WEAPON_NAME[N_WEAPONS] = {
    "Bare Fists", "Dagger", "Short Sword", "Battle Axe", "War Hammer", "Rune Blade"
};
static const int8_t WEAPON_ATK[N_WEAPONS] = { 0, 1, 2, 4, 6, 9 };

#define N_ARMORS 6
static const char *ARMOR_NAME[N_ARMORS] = {
    "Rags", "Leather", "Chain Mail", "Scale Mail", "Plate", "Rune Plate"
};
static const int8_t ARMOR_DEF[N_ARMORS] = { 0, 1, 2, 3, 5, 7 };

/* ── Entities ────────────────────────────────────────────────────────── */
#define MAX_ENEMIES  12
#define MAX_ITEMS     8

enum { E_GOBLIN = 0, E_ORC, E_DEMON, E_ARCHER, E_BOSS,
       E_MIMIC, E_SUMMONER, E_HEALER, E_TYPE_COUNT };

typedef struct {
    int16_t x, y;
    int16_t hp, max_hp;
    int8_t  atk;
    uint8_t alive;
    uint8_t type;
    uint8_t cd;      /* per-type action cooldown */
    uint8_t woke;    /* mimic: 0 = disguised as a chest, 1 = revealed */
} enemy_t;

enum { ITEM_NONE = 0, ITEM_GOLD, ITEM_POTION, ITEM_WEAPON, ITEM_ARMOR, ITEM_SCROLL };

typedef struct {
    int8_t  x, y;
    uint8_t type;
    uint8_t tier;    /* weapon/armour tier, or scroll kind for ITEM_SCROLL */
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
static int p_weapon, p_armor;     /* equipment tiers */
static int p_torch;               /* max light radius in tiles */
static int torch_fuel;            /* turns of full brightness left */
static int buff_atk, buff_turns;  /* shrine blessing */
static int game_over;
static int exit_flag, restart_flag;

/* Status effects (turns remaining) */
static int poison_turns;          /* loses HP each turn */
static int burn_turns;            /* loses more HP each turn, brief */
static int regen_turns;           /* gains HP each turn */
static int freeze_turns;          /* all enemies skip their turn */

/* Scrolls (consumables) */
enum { SCR_TELEPORT = 0, SCR_FIREBALL, SCR_FREEZE, SCR_REVEAL, N_SCROLL };
static const char *SCROLL_NAME[N_SCROLL] = {
    "Teleport", "Fireball", "Freeze", "Reveal Map"
};
static int scrolls[N_SCROLL];

/* High score, persisted in NVS */
static int hi_score;

#define TORCH_MAX_FUEL 60         /* turns before the torch starts to gutter */

static int eff_atk(void) { return p_atk + WEAPON_ATK[p_weapon] + buff_atk; }
static int eff_def(void) { return p_def + ARMOR_DEF[p_armor]; }

/* Effective torch radius shrinks as fuel runs low, so light is a resource.
 * (compute_fov() adds a small per-frame flicker on top of this.) */
static int eff_torch(void) {
    int r = p_torch;
    if (torch_fuel <= 0)       r -= 3;
    else if (torch_fuel <= 8)  r -= 2;
    else if (torch_fuel <= 20) r -= 1;
    if (r < 1) r = 1;
    return r;
}

static int cur_score(void) {
    return p_gold + p_xp + (p_level-1)*100 + (p_floor-1)*75;
}

/* ── Animation state ─────────────────────────────────────────────────── */
static uint8_t  anim_tick;
static uint8_t  enemy_flash[MAX_ENEMIES];
static uint8_t  player_flash;
static uint8_t  levelup_anim;
static uint8_t  pickup_anim;
static int8_t   pickup_x, pickup_y;
static uint8_t  shake;            /* screen-shake frames left */

/* Ranged-attack tracer: drawn for a few frames after an archer/boss fires */
static uint8_t  shot_timer;
static int8_t   shot_x0, shot_y0, shot_x1, shot_y1;

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

/**
 * @brief Draw a fresh, unpredictable seed.
 *
 * Primary source is the ESP32-S3 hardware RNG; falls back to uptime mixed
 * with @p mix when the "crypto" capability is unavailable.  A frame counter
 * alone is not enough — if the title-screen wait exits immediately the
 * dungeon layout is identical on every run.
 */
static uint32_t random_seed(uint32_t mix) {
    uint32_t s = 0;
    if (crypto_random(&s, sizeof(s)) == 0 && s != 0) return s;
    uint32_t up = (uint32_t)rtc_get_uptime_ms();
    return (up ^ (mix * 2654435761u)) + 1u;
}

static void reseed(uint32_t mix) {
    rng_state = random_seed(mix) ^ 0xCAFEBEEF;
    rng_next(7); rng_next(7); rng_next(7);
}

/* ── High score (persisted in NVS via the settings API) ──────────────── */
static int str_to_int(const char *s) {
    int v = 0;
    for (int i = 0; s[i] >= '0' && s[i] <= '9'; i++) v = v*10 + (s[i]-'0');
    return v;
}
static void int_to_str(int v, char *b) {
    char t[12]; int n = 0;
    if (v <= 0) { b[0]='0'; b[1]='\0'; return; }
    while (v > 0 && n < 11) { t[n++] = (char)('0' + v%10); v /= 10; }
    int p = 0; while (n > 0) b[p++] = t[--n];
    b[p] = '\0';
}
static void load_hi_score(void) {
    char buf[16];
    hi_score = (settings_get("pdungeon/hiscore", buf, sizeof(buf)) == 0)
             ? str_to_int(buf) : 0;
}
static void save_hi_score(void) {
    char buf[16];
    int_to_str(hi_score, buf);
    settings_set("pdungeon/hiscore", buf);
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

/* ── Field of view ───────────────────────────────────────────────────────
 * Bresenham line-of-sight from the player to every tile inside the torch
 * radius.  Walls block, but are themselves lit (the endpoint is not tested)
 * so rooms show their boundary.                                         */
static int los_clear(int x0, int y0, int x1, int y1) {
    int dx = x1 - x0, dy = y1 - y0;
    int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    int sx = dx > 0 ? 1 : -1, sy = dy > 0 ? 1 : -1;
    int x = x0, y = y0, err;

    if (adx >= ady) {
        err = adx / 2;
        for (int i = 0; i < adx; i++) {
            x += sx; err -= ady;
            if (err < 0) { y += sy; err += adx; }
            if (x == x1 && y == y1) return 1;
            if (map[y][x] == T_WALL) return 0;
        }
    } else {
        err = ady / 2;
        for (int i = 0; i < ady; i++) {
            y += sy; err -= adx;
            if (err < 0) { x += sx; err += ady; }
            if (x == x1 && y == y1) return 1;
            if (map[y][x] == T_WALL) return 0;
        }
    }
    return 1;
}

static void compute_fov(void) {
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) { vis[y][x] = 0; litlvl[y][x] = 0; }

    int r = eff_torch();
    /* Per-frame flicker so the edge of the light breathes. */
    if ((anim_tick & 7) == 0 && r > 1) r--;

    for (int y = py - r; y <= py + r; y++) {
        if (y < 0 || y >= MAP_H) continue;
        for (int x = px - r; x <= px + r; x++) {
            if (x < 0 || x >= MAP_W) continue;
            int dx = x - px, dy = y - py;
            int d2 = dx*dx + dy*dy;
            if (d2 > r*r + r) continue;              /* rounded radius */
            if (!los_clear(px, py, x, y)) continue;
            vis[y][x] = 1;
            seen[y][x] = 1;
            /* Smooth falloff: brightest underfoot, dimmer toward the edge. */
            int lvl = 4 - (int)(3 * d2 / (r*r + r + 1));
            if (lvl < 1) lvl = 1;
            litlvl[y][x] = (uint8_t)lvl;
            /* Spot nearby traps — the torch earns its keep */
            if (trap[y][x] == 1 && d2 <= 4) trap[y][x] = 2;
        }
    }
    vis[py][px] = 1; seen[py][px] = 1; litlvl[py][px] = 4;
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

/** Find a random empty floor tile not occupied by the player or an entity. */
static int find_free(int *ox, int *oy) {
    for (int t = 60; t > 0; t--) {
        int x = 1 + rng_next(MAP_W - 2);
        int y = 1 + rng_next(MAP_H - 2);
        if (map[y][x] != T_FLOOR) continue;
        if (x == px && y == py) continue;
        if (trap[y][x]) continue;
        int busy = 0;
        for (int i = 0; i < MAX_ENEMIES && !busy; i++)
            if (enemies[i].alive && enemies[i].x == x && enemies[i].y == y) busy = 1;
        for (int i = 0; i < MAX_ITEMS && !busy; i++)
            if (items[i].active && items[i].x == x && items[i].y == y) busy = 1;
        if (busy) continue;
        *ox = x; *oy = y;
        return 1;
    }
    return 0;
}

/** Roll an enemy type appropriate to the current depth. */
static int pick_enemy_type(void) {
    if (p_floor <= 2) return (rng_next(4) == 0) ? E_ARCHER : E_GOBLIN;
    if (p_floor <= 5) {
        int r = rng_next(12);
        if (r < 4) return E_GOBLIN;
        if (r < 6) return E_ORC;
        if (r < 8) return E_ARCHER;
        if (r < 10) return E_HEALER;
        return E_SUMMONER;
    }
    int r = rng_next(14);
    if (r < 3) return E_ORC;
    if (r < 6) return E_DEMON;
    if (r < 8) return E_ARCHER;
    if (r < 10) return E_SUMMONER;
    if (r < 12) return E_HEALER;
    return E_GOBLIN;
}

static void init_enemy(int i, int x, int y, int type) {
    enemy_t *e = &enemies[i];
    e->x = (int16_t)x; e->y = (int16_t)y;
    e->type = (uint8_t)type;
    e->alive = 1;
    e->cd = 0;

    e->woke = (type == E_MIMIC) ? 0 : 1;   /* mimics start disguised */

    int f = p_floor;
    switch (type) {
    case E_GOBLIN:   e->max_hp = (int16_t)(3 + f);       e->atk = (int8_t)(1 + f/2); break;
    case E_ORC:      e->max_hp = (int16_t)(6 + f*2);     e->atk = (int8_t)(2 + f/2); break;
    case E_DEMON:    e->max_hp = (int16_t)(5 + f + f/2); e->atk = (int8_t)(3 + f/2); break;
    case E_ARCHER:   e->max_hp = (int16_t)(3 + f);       e->atk = (int8_t)(2 + f/3); break;
    case E_MIMIC:    e->max_hp = (int16_t)(8 + f*2);     e->atk = (int8_t)(3 + f);   break;
    case E_SUMMONER: e->max_hp = (int16_t)(6 + f + f/2); e->atk = (int8_t)(1 + f/3); break;
    case E_HEALER:   e->max_hp = (int16_t)(5 + f);       e->atk = (int8_t)(1 + f/3); break;
    default:         e->max_hp = (int16_t)(25 + f*6);    e->atk = (int8_t)(4 + f);   break;
    }
    e->hp = e->max_hp;
}

static void generate_map(void) {
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) {
            map[y][x]  = T_WALL;
            trap[y][x] = 0;
            seen[y][x] = 0;
            vis[y][x]  = 0;
        }

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
            else             { gen_v(cy[i-1], cy[i], cx[i-1]); gen_h(cx[i-1], cx[i], cy[i]); }
        }
    }

    px = cx[0]; py = cy[0];

    for (int i = 0; i < MAX_ENEMIES; i++) enemies[i].alive = 0;
    for (int i = 0; i < MAX_ITEMS; i++)   items[i].active = 0;

    map[cy[nr-1]][cx[nr-1]] = T_STAIRS;

    int is_boss_floor = (p_floor % 5 == 0);

    /* Enemies */
    int ne = 3 + p_floor / 2;
    if (ne > MAX_ENEMIES - 1) ne = MAX_ENEMIES - 1;
    if (is_boss_floor) ne = ne / 2;

    int slot = 0;
    if (is_boss_floor) {
        int bx, by;
        if (find_free(&bx, &by)) { init_enemy(slot, bx, by, E_BOSS); slot++; }
    }
    for (int i = 0; i < ne && slot < MAX_ENEMIES; i++) {
        int ex, ey;
        if (!find_free(&ex, &ey)) break;
        init_enemy(slot, ex, ey, pick_enemy_type());
        slot++;
    }

    /* Mimic — a chest that bites.  From floor 2, ~1-in-3 floors. */
    if (p_floor >= 2 && slot < MAX_ENEMIES && rng_next(3) == 0) {
        int mx, my;
        if (find_free(&mx, &my)) { init_enemy(slot, mx, my, E_MIMIC); slot++; }
    }

    /* Items on the floor */
    int ni = 2 + rng_next(3);
    if (ni > MAX_ITEMS) ni = MAX_ITEMS;
    for (int i = 0; i < ni; i++) {
        int ix, iy;
        if (!find_free(&ix, &iy)) break;
        items[i].x = (int8_t)ix;
        items[i].y = (int8_t)iy;
        int r = rng_next(10);
        if (r < 2) { items[i].type = ITEM_SCROLL; items[i].tier = (uint8_t)rng_next(N_SCROLL); }
        else if (r < 5) { items[i].type = ITEM_POTION; items[i].tier = 0; }
        else { items[i].type = ITEM_GOLD; items[i].tier = 0; }
        items[i].active = 1;
    }

    /* Chests — one guaranteed every other floor, sometimes two */
    int nc = 1 + (rng_next(3) == 0 ? 1 : 0);
    for (int i = 0; i < nc; i++) {
        int chx, chy;
        if (find_free(&chx, &chy)) map[chy][chx] = T_CHEST;
    }

    /* Shrine — not on every floor, so finding one feels like luck */
    if (rng_next(3) != 0) {
        int shx, shy;
        if (find_free(&shx, &shy)) map[shy][shx] = T_SHRINE;
    }

    /* Traps — deeper floors are nastier */
    int nt = 2 + p_floor / 2;
    if (nt > 8) nt = 8;
    for (int i = 0; i < nt; i++) {
        int tx, ty;
        if (find_free(&tx, &ty)) trap[ty][tx] = 1;
    }

    /* Reset animation for the new floor */
    for (int i = 0; i < MAX_ENEMIES; i++) enemy_flash[i] = 0;
    for (int i = 0; i < MAX_FLOATS; i++) floats[i].timer = 0;
    player_flash = 0;
    shot_timer = 0;
    freeze_turns = 0;           /* frozen enemies thaw between floors */

    compute_fov();
}

/* ── Init ────────────────────────────────────────────────────────────── */
static void init_game(void) {
    p_hp=20; p_max_hp=20; p_atk=3; p_def=1;
    p_xp=0; p_xp_next=10; p_level=1;
    p_gold=0; p_potions=1; p_floor=1;
    p_weapon=1; p_armor=1;          /* start with a dagger and leather */
    p_torch=5; torch_fuel=TORCH_MAX_FUEL;
    buff_atk=0; buff_turns=0;
    poison_turns=0; burn_turns=0; regen_turns=0; freeze_turns=0;
    for (int i=0;i<N_SCROLL;i++) scrolls[i]=0;
    scrolls[SCR_TELEPORT]=1;        /* one escape scroll to start */
    game_over=0;
    msg_buf[0]='\0'; msg_timer=0;
    levelup_anim=0; pickup_anim=0; anim_tick=0;
    generate_map();
    set_msg("Welcome to the dungeon!", C_MSG_INFO);
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
        set_msg("*** LEVEL UP! ***", C_MSG_LEVEL);
    }
}

/* ── Loot ────────────────────────────────────────────────────────────── */
/** Drop an item at (x,y) if a free slot exists. */
static void drop_item(int x, int y, int type, int tier) {
    for (int i = 0; i < MAX_ITEMS; i++) {
        if (!items[i].active) {
            items[i].x = (int8_t)x;
            items[i].y = (int8_t)y;
            items[i].type = (uint8_t)type;
            items[i].tier = (uint8_t)tier;
            items[i].active = 1;
            return;
        }
    }
}

/** Tier of gear that should plausibly show up at the current depth. */
static int loot_tier(void) {
    int t = 1 + p_floor / 2 + rng_next(2);
    if (t > N_WEAPONS - 1) t = N_WEAPONS - 1;
    return t;
}

/* ── Combat ──────────────────────────────────────────────────────────── */
static void attack_enemy(int idx) {
    enemy_t *e = &enemies[idx];
    e->woke = 1;                 /* a struck mimic drops its disguise */
    int dmg = eff_atk() - rng_next(2);
    if (dmg < 1) dmg = 1;
    e->hp -= (int16_t)dmg;
    enemy_flash[idx] = 8;
    spawn_float(e->x, e->y, dmg, 0);

    if (e->hp <= 0) {
        e->alive = 0;
        int xp = e->max_hp + e->atk;
        if (e->type == E_BOSS) xp *= 2;
        p_xp += xp;

        /* Death drops */
        if (e->type == E_BOSS) {
            drop_item(e->x, e->y, (rng_next(2) ? ITEM_WEAPON : ITEM_ARMOR), loot_tier());
            set_msg("The boss falls! Loot drops.", C_MSG_LEVEL);
        } else if (e->type == E_MIMIC) {
            /* Mimics guard real treasure */
            drop_item(e->x, e->y, (rng_next(2) ? ITEM_WEAPON : ITEM_SCROLL),
                      rng_next(2) ? loot_tier() : rng_next(N_SCROLL));
            set_msg("The mimic burst! Loot!", C_MSG_LEVEL);
        } else {
            int r = rng_next(10);
            if (r < 2)      drop_item(e->x, e->y, ITEM_SCROLL, rng_next(N_SCROLL));
            else if (r < 4) drop_item(e->x, e->y, ITEM_GOLD, 0);
            else if (r < 6) drop_item(e->x, e->y, ITEM_POTION, 0);
            set_msg("Enemy slain!", C_MSG_ITEM);
        }
        check_level_up();
    } else {
        set_msg("Hit!", C_MSG_DMG);
    }
}

static void hurt_player(int dmg, const char *why) {
    if (dmg < 1) dmg = 1;
    p_hp -= dmg;
    player_flash = 8;
    shake = 4;
    spawn_float(px, py, dmg, 0);
    if (p_hp <= 0) { p_hp = 0; game_over = 1; set_msg("You died!", C_MSG_DMG); }
    else           { set_msg(why, C_MSG_DMG); }
}

static void enemy_attacks(int idx) {
    enemy_t *e = &enemies[idx];
    hurt_player(e->atk - eff_def() + rng_next(2), "Hit by enemy!");
    if (game_over) return;
    /* Demons set you alight; mimics leave a venomous bite. */
    if (e->type == E_DEMON && rng_next(2) == 0) {
        burn_turns = 4;
        set_msg("You are set ablaze!", C_BURN);
    } else if (e->type == E_MIMIC && rng_next(2) == 0) {
        poison_turns = 6;
        set_msg("The mimic's bite festers!", C_POISON);
    }
}

static void enemy_shoots(int idx) {
    enemy_t *e = &enemies[idx];
    shot_timer = 6;
    shot_x0 = (int8_t)e->x; shot_y0 = (int8_t)e->y;
    shot_x1 = (int8_t)px;   shot_y1 = (int8_t)py;
    hurt_player(e->atk - eff_def()/2 + rng_next(2), "Struck from afar!");
}

/* ── Enemy AI ────────────────────────────────────────────────────────── */
/** Step one tile toward (tx,ty); returns 1 if the move happened. */
static int step_toward(int i, int tx, int ty, int away) {
    enemy_t *e = &enemies[i];
    int ex = e->x, ey = e->y;
    int dx = tx - ex, dy = ty - ey;
    int nx = ex, ny = ey;

    if ((dx<0?-dx:dx) >= (dy<0?-dy:dy)) nx += (dx>0)?1:-1;
    else                                ny += (dy>0)?1:-1;
    if (away) { nx = ex - (nx - ex); ny = ey - (ny - ey); }

    if (nx<0||nx>=MAP_W||ny<0||ny>=MAP_H) return 0;
    if (map[ny][nx]==T_WALL) return 0;
    if (nx==px && ny==py) return 0;
    for (int j=0; j<MAX_ENEMIES; j++)
        if (j!=i && enemies[j].alive && enemies[j].x==nx && enemies[j].y==ny)
            return 0;

    e->x = (int16_t)nx; e->y = (int16_t)ny;
    return 1;
}

/** Spawn an enemy of @p type on a free tile adjacent to (ax,ay). */
static void spawn_adjacent(int type, int ax, int ay) {
    int slot = -1;
    for (int i = 0; i < MAX_ENEMIES; i++) if (!enemies[i].alive) { slot = i; break; }
    if (slot < 0) return;
    static const int ox[4] = {0,0,-1,1}, oy[4] = {-1,1,0,0};
    int s = rng_next(4);
    for (int k = 0; k < 4; k++) {
        int nx = ax + ox[(s+k)&3], ny = ay + oy[(s+k)&3];
        if (nx<0||nx>=MAP_W||ny<0||ny>=MAP_H) continue;
        if (map[ny][nx]==T_WALL) continue;
        if (nx==px && ny==py) continue;
        int busy = 0;
        for (int j=0;j<MAX_ENEMIES;j++)
            if (enemies[j].alive && enemies[j].x==nx && enemies[j].y==ny) { busy=1; break; }
        if (busy) continue;
        init_enemy(slot, nx, ny, type);
        return;
    }
}

/** Heal the most-wounded ally within 3 tiles of enemy i; return 1 if healed. */
static int heal_ally(int i) {
    int best = -1, worst = 0;
    for (int j = 0; j < MAX_ENEMIES; j++) {
        if (j==i || !enemies[j].alive) continue;
        int dx = enemies[j].x - enemies[i].x, dy = enemies[j].y - enemies[i].y;
        if ((dx<0?-dx:dx)+(dy<0?-dy:dy) > 3) continue;
        int deficit = enemies[j].max_hp - enemies[j].hp;
        if (deficit > worst) { worst = deficit; best = j; }
    }
    if (best < 0) return 0;
    int amt = 3 + p_floor/2;
    enemies[best].hp += (int16_t)amt;
    if (enemies[best].hp > enemies[best].max_hp) enemies[best].hp = enemies[best].max_hp;
    enemy_flash[best] = 6;
    spawn_float(enemies[best].x, enemies[best].y, amt, 1);
    return 1;
}

static void move_enemies(void) {
    /* Frozen enemies lose their whole turn. */
    if (freeze_turns > 0) { freeze_turns--; return; }

    for (int i = 0; i < MAX_ENEMIES; i++) {
        enemy_t *e = &enemies[i];
        if (!e->alive) continue;

        int dx = px - e->x, dy = py - e->y;
        int dist = (dx<0?-dx:dx) + (dy<0?-dy:dy);
        if (e->cd) e->cd--;

        switch (e->type) {
        case E_MIMIC:
            /* Disguised and motionless until the player is adjacent. */
            if (!e->woke) {
                if (dist <= 1) { e->woke = 1; enemy_attacks(i);
                                 set_msg("The chest was a MIMIC!", C_MSG_DMG); }
                break;
            }
            if (dist <= 1) enemy_attacks(i);
            else if (dist <= 6) step_toward(i, px, py, 0);
            break;

        case E_SUMMONER:
            /* Hangs back and conjures goblins on a cooldown. */
            if (dist <= 2) { step_toward(i, px, py, 1); break; }
            if (!e->cd && dist <= 7) {
                spawn_adjacent(E_GOBLIN, e->x, e->y);
                e->cd = 4;
                set_msg("The summoner calls for aid!", C_MSG_INFO);
            } else if (dist <= 8) {
                step_toward(i, px, py, 0);
            }
            break;

        case E_HEALER:
            /* Mends its allies and keeps well clear of the player. */
            if (!e->cd && heal_ally(i)) { e->cd = 2; break; }
            if (dist <= 3) step_toward(i, px, py, 1);
            else if (dist <= 1) enemy_attacks(i);
            break;

        case E_ARCHER:
            /* Keeps its distance and shoots down clear lines. */
            if (dist <= 1) { step_toward(i, px, py, 1); break; }
            if (dist <= 5 && los_clear(e->x, e->y, px, py)) {
                if (!e->cd) { enemy_shoots(i); e->cd = 2; }
                break;
            }
            if (dist <= 8) step_toward(i, px, py, 0);
            break;

        case E_ORC:
            /* Heavy and slow — acts every other turn. */
            if (e->cd) break;
            e->cd = 1;
            if (dist <= 1) enemy_attacks(i);
            else if (dist <= 8) step_toward(i, px, py, 0);
            break;

        case E_DEMON:
            /* Fast — closes two tiles per turn. */
            if (dist <= 1) { enemy_attacks(i); break; }
            if (dist <= 9) {
                step_toward(i, px, py, 0);
                dx = px - e->x; dy = py - e->y;
                dist = (dx<0?-dx:dx) + (dy<0?-dy:dy);
                if (dist <= 1) enemy_attacks(i);
                else           step_toward(i, px, py, 0);
            }
            break;

        case E_BOSS:
            /* Slams from two tiles away on a cooldown, otherwise mauls. */
            if (dist <= 1) { enemy_attacks(i); break; }
            if (dist <= 3 && !e->cd && los_clear(e->x, e->y, px, py)) {
                enemy_shoots(i); e->cd = 3; break;
            }
            step_toward(i, px, py, 0);
            break;

        default: /* E_GOBLIN */
            if (dist <= 1) enemy_attacks(i);
            else if (dist <= 7) step_toward(i, px, py, 0);
            break;
        }

        if (game_over) return;
    }
}

/* ── Traps ───────────────────────────────────────────────────────────── */
static void spring_trap(void) {
    if (!trap[py][px]) return;
    int was_hidden = (trap[py][px] == 1);
    trap[py][px] = 2;

    int dmg = 2 + p_floor / 2 + rng_next(3);
    hurt_player(dmg, was_hidden ? "A hidden trap!" : "You stepped on a trap!");
    /* Some traps are envenomed. */
    if (!game_over && rng_next(2) == 0) {
        poison_turns = 5;
        set_msg("Poison darts!", C_POISON);
    }
}

/* ── Pickup ──────────────────────────────────────────────────────────── */
static void try_pickup(void) {
    for (int i = 0; i < MAX_ITEMS; i++) {
        if (!items[i].active) continue;
        if (items[i].x != px || items[i].y != py) continue;

        switch (items[i].type) {
        case ITEM_GOLD: {
            int gold = 3 + rng_next(5) + p_floor;
            p_gold += gold;
            spawn_float(px, py, gold, 2);
            set_msg("Gold collected!", C_MSG_ITEM);
            break;
        }
        case ITEM_POTION:
            p_potions++;
            spawn_float(px, py, 1, 1);
            set_msg("Found a potion!", C_MSG_HEAL);
            break;

        case ITEM_WEAPON:
            if (items[i].tier > p_weapon) {
                p_weapon = items[i].tier;
                set_msg(WEAPON_NAME[p_weapon], C_MSG_LEVEL);
            } else {
                p_gold += 5 + p_floor;
                set_msg("Sold a lesser weapon.", C_MSG_ITEM);
            }
            break;

        case ITEM_ARMOR:
            if (items[i].tier > p_armor) {
                p_armor = items[i].tier;
                set_msg(ARMOR_NAME[p_armor], C_MSG_LEVEL);
            } else {
                p_gold += 5 + p_floor;
                set_msg("Sold lesser armour.", C_MSG_ITEM);
            }
            break;

        case ITEM_SCROLL: {
            int k = items[i].tier % N_SCROLL;
            scrolls[k]++;
            spawn_float(px, py, 1, 3);
            set_msg(SCROLL_NAME[k], C_MSG_LEVEL);
            break;
        }
        }

        pickup_anim=20; pickup_x=(int8_t)px; pickup_y=(int8_t)py;
        items[i].active=0;
    }
}

/* ── Chests & shrines ────────────────────────────────────────────────── */
static void open_chest(void) {
    map[py][px] = T_FLOOR;
    pickup_anim = 20; pickup_x = (int8_t)px; pickup_y = (int8_t)py;

    int r = rng_next(10);
    if (r < 3) {
        int gold = 15 + rng_next(20) + p_floor * 4;
        p_gold += gold;
        spawn_float(px, py, gold, 2);
        set_msg("The chest is full of gold!", C_MSG_ITEM);
    } else if (r < 5) {
        p_potions += 2;
        set_msg("Two potions inside!", C_MSG_HEAL);
    } else if (r < 8) {
        int t = loot_tier();
        if (t > p_weapon) { p_weapon = t; set_msg(WEAPON_NAME[t], C_MSG_LEVEL); }
        else              { p_gold += 10; set_msg("A dull blade. Sold.", C_MSG_ITEM); }
    } else {
        int t = loot_tier();
        if (t > p_armor) { p_armor = t; set_msg(ARMOR_NAME[t], C_MSG_LEVEL); }
        else             { p_gold += 10; set_msg("Rusted armour. Sold.", C_MSG_ITEM); }
    }
}

static void use_shrine(void) {
    map[py][px] = T_SHRINE_USED;
    levelup_anim = 30;

    switch (rng_next(5)) {
    case 0:
        buff_atk = 3; buff_turns = 25;
        set_msg("Blessed! +3 attack.", C_MSG_LEVEL);
        break;
    case 1:
        p_hp = p_max_hp;
        poison_turns = burn_turns = 0;         /* also cleanses */
        spawn_float(px, py, p_max_hp, 1);
        set_msg("The shrine heals and cleanses.", C_MSG_HEAL);
        break;
    case 2:
        if (p_torch < 8) p_torch++;
        torch_fuel = TORCH_MAX_FUEL;
        set_msg("Your torch blazes anew.", C_TORCH);
        break;
    case 3: {
        int k = rng_next(N_SCROLL);
        scrolls[k]++;
        set_msg(SCROLL_NAME[k], C_MSG_LEVEL);
        break;
    }
    default: {
        int gold = 20 + p_floor * 5;
        p_gold += gold;
        spawn_float(px, py, gold, 2);
        set_msg("The shrine grants fortune.", C_MSG_ITEM);
        break;
    }
    }
}

/* ── Scrolls ─────────────────────────────────────────────────────────── */
/** Apply scroll @p k.  Returns 1 if it was consumed (a turn passes). */
static int use_scroll(int k) {
    if (scrolls[k] <= 0) { set_msg("No such scroll.", C_MSG_INFO); return 0; }

    switch (k) {
    case SCR_TELEPORT: {
        int tx, ty;
        if (find_free(&tx, &ty)) { px = tx; py = ty; compute_fov(); }
        set_msg("Reality folds — teleport!", C_MSG_LEVEL);
        break;
    }
    case SCR_FIREBALL: {
        int hits = 0, dmg = 8 + p_floor*2 + p_level;
        for (int i = 0; i < MAX_ENEMIES; i++) {
            enemy_t *e = &enemies[i];
            if (!e->alive) continue;
            int dx = e->x - px, dy = e->y - py;
            if (dx*dx + dy*dy > 16) continue;      /* radius 4 blast */
            e->hp -= (int16_t)dmg; e->woke = 1; enemy_flash[i] = 8;
            spawn_float(e->x, e->y, dmg, 0);
            if (e->hp <= 0) {
                e->alive = 0;
                p_xp += e->max_hp + e->atk;
            }
            hits++;
        }
        check_level_up();
        set_msg(hits ? "Fireball erupts!" : "Fireball fizzles.", C_MSG_DMG);
        break;
    }
    case SCR_FREEZE:
        freeze_turns = 6;
        set_msg("The air freezes solid!", C_FREEZE);
        break;
    default: /* SCR_REVEAL */
        for (int y = 0; y < MAP_H; y++)
            for (int x = 0; x < MAP_W; x++) seen[y][x] = 1;
        set_msg("The map reveals itself.", C_MSG_LEVEL);
        break;
    }

    scrolls[k]--;
    return 1;
}

/* ── Drawing: tiles ──────────────────────────────────────────────────────
 * `lvl` is the torch light level 0..4.  0 = remembered but unlit (drawn with
 * the dim "_D" palette); 1..4 fade a tile's colours toward black at the edge
 * of the torchlight via dim565().  The 1-bit panel only has lit/unlit.     */
static uint16_t tint(uint16_t full, uint16_t dimc, int lvl) {
    if (g_mono) return lvl > 0 ? full : dimc;
    return lvl > 0 ? dim565(full, lvl) : dimc;
}

static void draw_wall(int sx, int sy, int y, int lvl) {
    if (g_mono) {
        int lit = lvl > 0;
        display_rect(sx, sy, TILE_W, TILE_H, C_WALL);
        uint16_t e = lit ? C_WALL_HI : C_WALL_HI_D;
        display_hline(sx, sy, TILE_W, e);
        display_vline(sx, sy, TILE_H, e);
        if (lit) {
            if ((y & 1) == 0) display_hline(sx,          sy+TILE_H/2, TILE_W,   e);
            else              display_hline(sx+TILE_W/2, sy+TILE_H/2, TILE_W/2, e);
        }
        return;
    }
    uint16_t body = tint(C_WALL,    C_WALL_D,    lvl);
    uint16_t edge = tint(C_WALL_HI, C_WALL_HI_D, lvl);
    display_rect(sx, sy, TILE_W, TILE_H, body);
    display_hline(sx, sy, TILE_W, edge);
    display_vline(sx, sy, TILE_H, edge);
    display_hline(sx, sy+TILE_H-1, TILE_W, dim565(C_WALL_SH, lvl>0?lvl:1));
    if (lvl > 0) {
        if ((y & 1) == 0) display_hline(sx,          sy+TILE_H/2, TILE_W,   edge);
        else              display_hline(sx+TILE_W/2, sy+TILE_H/2, TILE_W/2, edge);
    }
}

static void draw_floor(int sx, int sy, int lvl) {
    if (g_mono) {
        if (lvl > 0) {
            display_rect(sx, sy, TILE_W, TILE_H, C_FLOOR);
            display_pixel(sx+2,        sy+2,        C_FLOOR_DOT);
            display_pixel(sx+TILE_W-3, sy+TILE_H-3, C_FLOOR_DOT);
        } else {
            display_rect(sx, sy, TILE_W, TILE_H, 0x0000);
            display_pixel(sx+2,        sy+2,        0xFFFF);
            display_pixel(sx+TILE_W-3, sy+TILE_H-3, 0xFFFF);
        }
        return;
    }
    display_rect(sx, sy, TILE_W, TILE_H, tint(C_FLOOR, C_FLOOR_D, lvl));
    if (lvl > 0) {
        uint16_t d = dim565(C_FLOOR_DOT, lvl);
        display_pixel(sx+3,        sy+4,        d);
        display_pixel(sx+TILE_W-4, sy+TILE_H-5, d);
    }
}

static void draw_tile(int x, int y) {
    int sx = MAP_OX + x * TILE_W;
    int sy = MAP_OY + y * TILE_H;
    int lvl = litlvl[y][x];
    int lit = vis[y][x];

    if (!seen[y][x]) {                       /* never explored */
        display_rect(sx, sy, TILE_W, TILE_H, C_BG);
        return;
    }

    switch (map[y][x]) {
    case T_WALL:
        draw_wall(sx, sy, y, lvl);
        return;

    case T_STAIRS: {
        draw_floor(sx, sy, lvl);
        uint16_t c = tint(C_STAIR, C_STAIR_D, lvl);
        display_rect_outline(sx+1, sy+1, TILE_W-2, TILE_H-2, c);
        display_rect(sx+3,  sy+10, 10, 3, c);
        display_rect(sx+5,  sy+7,   8, 3, c);
        display_rect(sx+7,  sy+4,   6, 3, c);
        if (lit && ((anim_tick >> 3) & 1))
            display_rect(sx+9, sy+2, 4, 2, C_STAIR_CORE);
        return;
    }

    case T_CHEST: {
        draw_floor(sx, sy, lvl);
        uint16_t c  = tint(C_CHEST, C_CHEST_D, lvl);
        uint16_t tr = tint(C_CHEST_TRIM, C_CHEST_D, lvl);
        display_rect(sx+2, sy+6, TILE_W-4, TILE_H-8, c);
        display_rect(sx+2, sy+4, TILE_W-4, 3, c);          /* lid */
        display_hline(sx+2, sy+9, TILE_W-4, tr);
        display_rect(sx+TILE_W/2-1, sy+8, 2, 3, tr);
        return;
    }

    case T_SHRINE: {
        draw_floor(sx, sy, lvl);
        uint16_t c = tint(C_SHRINE, C_SHRINE_D, lvl);
        display_rect(sx+5, sy+6, 6, 8, c);
        display_rect(sx+3, sy+13, 10, 2, c);
        display_triangle_fill(sx+4, sy+6, sx+8, sy+1, sx+12, sy+6, c);
        if (lit && ((anim_tick >> 2) & 1))
            display_pixel(sx+8, sy+3, C_SHRINE_GLOW);
        return;
    }

    case T_SHRINE_USED: {
        draw_floor(sx, sy, lvl);
        uint16_t c = tint(C_SHRINE_USED_C, C_SHRINE_D, lvl);
        display_rect(sx+5, sy+8, 6, 6, c);
        display_rect(sx+3, sy+13, 10, 2, c);
        return;
    }

    default:
        draw_floor(sx, sy, lvl);
        break;
    }

    /* Revealed trap marker sits on top of the floor */
    if (trap[y][x] == 2 && lit) {
        display_rect_outline(sx+3, sy+3, TILE_W-6, TILE_H-6, C_TRAP);
        display_line(sx+4, sy+4, sx+TILE_W-5, sy+TILE_H-5, C_TRAP);
        display_line(sx+TILE_W-5, sy+4, sx+4, sy+TILE_H-5, C_TRAP);
    }
}

static void draw_map(void) {
    for (int y=0; y<MAP_H; y++)
        for (int x=0; x<MAP_W; x++)
            draw_tile(x, y);
}

/* ── Drawing: enemies ────────────────────────────────────────────────── */
static void draw_goblin(int sx, int sy, int flash) {
    uint16_t body = flash ? C_FLASH : C_GOB;
    uint16_t hi   = flash ? C_GOB   : C_GOB_HI;
    /* Pointy ears, round body — the weakest foe */
    display_triangle_fill(sx+1, sy+6, sx+4, sy+2, sx+6, sy+6, body);
    display_triangle_fill(sx+TILE_W-7, sy+6, sx+TILE_W-5, sy+2, sx+TILE_W-2, sy+6, body);
    display_circle_fill(sx+TILE_W/2, sy+8, 5, body);
    display_rect(sx+5, sy+6, 2, 2, C_EYE);
    display_rect(sx+TILE_W-7, sy+6, 2, 2, C_EYE);
    display_hline(sx+6, sy+11, 5, hi);            /* grin */
}

static void draw_orc(int sx, int sy, int flash) {
    uint16_t body = flash ? C_FLASH : C_ORC;
    uint16_t hi   = flash ? C_ORC   : C_ORC_HI;
    /* Wide shoulders, square head, tusks */
    display_rect(sx+1, sy+6, TILE_W-2, TILE_H-8, body);
    display_rect(sx+3, sy+2, TILE_W-6, 5, body);
    display_rect(sx,   sy+6, 2, 4, hi);           /* shoulder pads */
    display_rect(sx+TILE_W-2, sy+6, 2, 4, hi);
    display_rect(sx+4, sy+3, 2, 2, C_EYE);
    display_rect(sx+TILE_W-6, sy+3, 2, 2, C_EYE);
    display_rect(sx+5,        sy+7, 2, 3, hi);    /* tusks */
    display_rect(sx+TILE_W-7, sy+7, 2, 3, hi);
}

static void draw_demon(int sx, int sy, int flash) {
    uint16_t body = flash ? C_FLASH : C_DEM;
    uint16_t hi   = flash ? C_DEM   : C_DEM_HI;
    /* Horns + wings */
    display_triangle_fill(sx+1, sy+6, sx+3, sy+0, sx+6, sy+6, hi);
    display_triangle_fill(sx+TILE_W-7, sy+6, sx+TILE_W-4, sy+0, sx+TILE_W-2, sy+6, hi);
    display_rect(sx+3, sy+5, TILE_W-6, TILE_H-7, body);
    display_rect(sx,          sy+7, 3, 5, hi);    /* wings */
    display_rect(sx+TILE_W-3, sy+7, 3, 5, hi);
    display_rect(sx+5, sy+7, 2, 2, C_EYE);
    display_rect(sx+TILE_W-7, sy+7, 2, 2, C_EYE);
}

static void draw_archer(int sx, int sy, int flash) {
    uint16_t body = flash ? C_FLASH : C_ARC;
    uint16_t hi   = flash ? C_ARC   : C_ARC_HI;
    /* Hooded figure holding a bow — the bow reads at a glance */
    display_triangle_fill(sx+4, sy+8, sx+8, sy+1, sx+12, sy+8, body);  /* hood */
    display_rect(sx+5, sy+8, 6, TILE_H-10, body);
    display_rect(sx+6, sy+5, 2, 2, C_EYE);
    display_rect(sx+9, sy+5, 2, 2, C_EYE);
    /* Bow arc on the right */
    display_vline(sx+TILE_W-3, sy+3, 10, hi);
    display_pixel(sx+TILE_W-4, sy+2,  hi);
    display_pixel(sx+TILE_W-4, sy+13, hi);
}

static void draw_boss(int sx, int sy, int flash) {
    uint16_t body = flash ? C_FLASH : C_BOSS;
    uint16_t hi   = flash ? C_BOSS  : C_BOSS_HI;
    /* Fills the tile — crown of horns, heavy frame */
    display_rect(sx+1, sy+5, TILE_W-2, TILE_H-6, body);
    display_triangle_fill(sx,   sy+6, sx+3,  sy+0, sx+6,  sy+6, hi);
    display_triangle_fill(sx+5, sy+5, sx+8,  sy-1, sx+11, sy+5, hi);
    display_triangle_fill(sx+TILE_W-7, sy+6, sx+TILE_W-4, sy+0, sx+TILE_W-1, sy+6, hi);
    display_rect(sx+3, sy+7, 3, 3, C_EYE);
    display_rect(sx+TILE_W-6, sy+7, 3, 3, C_EYE);
    display_hline(sx+4, sy+12, TILE_W-8, hi);     /* snarl */
}

static void draw_mimic(int sx, int sy, int flash, int woke) {
    /* Disguised: an ordinary-looking chest.  Woke: chest with a toothy maw. */
    uint16_t body = flash ? C_FLASH : C_MIMIC;
    display_rect(sx+2, sy+6, TILE_W-4, TILE_H-8, body);
    display_rect(sx+2, sy+4, TILE_W-4, 3, body);
    display_hline(sx+2, sy+9, TILE_W-4, C_MIMIC_HI);
    if (woke) {
        /* Gaping mouth with teeth + eyes — the tell once it's awake */
        display_rect(sx+3, sy+8, TILE_W-6, 4, 0x0000);
        for (int t = 0; t < TILE_W-6; t += 3) {
            display_triangle_fill(sx+3+t, sy+8, sx+4+t, sy+11, sx+5+t, sy+8, C_MIMIC_HI);
            display_triangle_fill(sx+3+t, sy+12, sx+4+t, sy+9, sx+5+t, sy+12, C_MIMIC_HI);
        }
        display_rect(sx+3, sy+5, 2, 2, C_EYE);
        display_rect(sx+TILE_W-5, sy+5, 2, 2, C_EYE);
    }
}

static void draw_summoner(int sx, int sy, int flash) {
    uint16_t body = flash ? C_FLASH : C_SUM;
    uint16_t hi   = flash ? C_SUM   : C_SUM_HI;
    /* Robed caster with a raised staff */
    display_triangle_fill(sx+3, sy+TILE_H-1, sx+8, sy+3, sx+13, sy+TILE_H-1, body); /* robe */
    display_rect(sx+6, sy+2, 4, 4, body);          /* hood */
    display_rect(sx+7, sy+3, 2, 2, C_EYE);
    display_vline(sx+TILE_W-3, sy+2, 11, hi);      /* staff */
    display_circle_fill(sx+TILE_W-3, sy+2, 2,
                        (anim_tick & 4) ? C_SUM_HI : hi);  /* orb pulse */
}

static void draw_healer(int sx, int sy, int flash) {
    uint16_t body = flash ? C_FLASH : C_HEALER;
    uint16_t hi   = flash ? C_HEALER: C_HEALER_HI;
    /* Acolyte with a bright cross */
    display_triangle_fill(sx+3, sy+TILE_H-1, sx+8, sy+4, sx+13, sy+TILE_H-1, body);
    display_rect(sx+6, sy+2, 4, 4, body);
    display_rect(sx+7, sy+3, 2, 2, C_EYE);
    display_rect(sx+7, sy+8, 2, 6, hi);            /* cross vertical */
    display_rect(sx+5, sy+10, 6, 2, hi);           /* cross horizontal */
}

static void draw_enemy(int i) {
    enemy_t *e = &enemies[i];
    if (!e->alive) return;
    if (!vis[e->y][e->x]) return;      /* unlit enemies stay hidden */

    int sx = MAP_OX + e->x * TILE_W;
    int sy = MAP_OY + e->y * TILE_H;
    int fl = enemy_flash[i] > 0;

    switch (e->type) {
    case E_GOBLIN:   draw_goblin(sx, sy, fl); break;
    case E_ORC:      draw_orc(sx, sy, fl);    break;
    case E_DEMON:    draw_demon(sx, sy, fl);  break;
    case E_ARCHER:   draw_archer(sx, sy, fl); break;
    case E_MIMIC:    draw_mimic(sx, sy, fl, e->woke); break;
    case E_SUMMONER: draw_summoner(sx, sy, fl); break;
    case E_HEALER:   draw_healer(sx, sy, fl); break;
    default:         draw_boss(sx, sy, fl);   break;
    }

    /* A disguised mimic shows no HP bar — it looks like furniture. */
    if (e->type == E_MIMIC && !e->woke) return;

    /* HP pip bar along the bottom of the tile */
    int bar_w = TILE_W - 4;
    int hp_w  = bar_w * e->hp / e->max_hp;
    if (hp_w < 0) hp_w = 0;
    display_rect(sx+2, sy+TILE_H-3, bar_w, 2, C_HP_BG);
    display_rect(sx+2, sy+TILE_H-3, hp_w,  2,
                 (e->hp * 3 <= e->max_hp) ? C_HP_LOW : C_HP);
}

/* ── Drawing: player ─────────────────────────────────────────────────── */
static void draw_player(void) {
    int sx = MAP_OX + px * TILE_W;
    int sy = MAP_OY + py * TILE_H;
    int fl = player_flash > 0;

    /* Armour brightens with its tier; the sword grows with the weapon tier. */
    uint16_t armor = fl ? C_P_HIT : (g_mono ? C_P_ARMOR : dim565(C_P_ARMOR, 2 + p_armor/2));
    uint16_t body  = fl ? C_P_HIT : C_P_BODY;
    uint16_t helm  = fl ? C_FLASH : C_P_HELM;

    /* Legs */
    display_rect(sx+4, sy+11, 3, 4, body);
    display_rect(sx+TILE_W-7, sy+11, 3, 4, body);
    /* Torso with a shaded lower half + belt */
    display_rect(sx+3, sy+6, TILE_W-6, 6, armor);
    if (!g_mono) display_rect(sx+3, sy+9, TILE_W-6, 3, dim565(armor, 3));
    display_hline(sx+3, sy+10, TILE_W-6, C_STAIR);     /* belt glint */
    /* Shoulders */
    display_rect(sx+1,        sy+6, 2, 3, helm);
    display_rect(sx+TILE_W-3, sy+6, 2, 3, helm);
    /* Head + helmet + a plume for high armour tiers */
    display_rect(sx+5, sy+2, 6, 4, helm);
    display_hline(sx+5, sy+5, 6, C_P_SKIN);            /* visor slit */
    if (p_armor >= 4) display_rect(sx+7, sy-1, 2, 3, C_MSG_LEVEL);

    /* Sword: blade length scales with weapon tier; higher tiers glow. */
    int blade = 5 + p_weapon;
    if (blade > 12) blade = 12;
    uint16_t sword = (p_weapon >= 4 && !g_mono) ? C_STAIR : C_SWORD;
    display_vline(sx+TILE_W-2, sy+11-blade, blade, sword);
    display_hline(sx+TILE_W-4, sy+11-blade+2, 4, C_P_HELM);  /* crossguard */
    if (p_weapon >= 3) display_pixel(sx+TILE_W-2, sy+10-blade, 0xFFFF);

    /* Blessing aura */
    if (buff_turns > 0 && ((anim_tick >> 2) & 1))
        display_rect_outline(sx, sy, TILE_W, TILE_H, C_BUFF);
}

/* ── Drawing: items ──────────────────────────────────────────────────── */
static void draw_items(void) {
    for (int i = 0; i < MAX_ITEMS; i++) {
        if (!items[i].active) continue;
        if (!vis[items[i].y][items[i].x]) continue;

        int sx = MAP_OX + items[i].x * TILE_W;
        int sy = MAP_OY + items[i].y * TILE_H;
        int cx = sx + TILE_W/2, cy = sy + TILE_H/2;

        switch (items[i].type) {
        case ITEM_GOLD:
            display_circle_fill(cx, cy, 4, C_GOLD);
            display_vline(cx, cy-3, 6, C_GOLD_HI);
            if (anim_tick & 16) display_pixel(cx+2, cy-3, C_GOLD_HI);
            break;

        case ITEM_POTION: {
            int fx = cx - 2, fy = cy - 5;
            display_rect(fx,   fy-1, 4, 2, C_POTION_HI);   /* stopper */
            display_rect(fx+1, fy,   2, 2, C_POTION);      /* neck */
            display_rect(fx-1, fy+2, 6, 6, C_POTION);      /* body */
            display_pixel(fx,  fy+3, C_POTION_HI);         /* shine */
            break;
        }

        case ITEM_WEAPON:
            display_vline(cx, cy-5, 9, C_WEAPON);
            display_hline(cx-3, cy+2, 7, C_WEAPON);
            display_pixel(cx, cy-6, C_GOLD_HI);
            break;

        case ITEM_ARMOR:
            display_rect(cx-4, cy-4, 8, 7, C_ARMOR_I);
            display_triangle_fill(cx-4, cy+3, cx, cy+6, cx+4, cy+3, C_ARMOR_I);
            display_pixel(cx-2, cy-2, C_TEXT);
            break;

        default: /* ITEM_SCROLL — a rolled parchment with a ribbon */
            display_rect(cx-4, cy-3, 8, 6, C_SCROLL);
            display_vline(cx-4, cy-3, 6, C_SCROLL_HI);
            display_vline(cx+3, cy-3, 6, C_SCROLL_HI);
            display_hline(cx-1, cy-4, 2, C_SCROLL_HI);   /* ribbon */
            break;
        }
    }
}

static void draw_floats_fn(void) {
    for (int i = 0; i < MAX_FLOATS; i++) {
        if (!floats[i].timer) continue;
        int rise = (28 - floats[i].timer) / 2;
        uint16_t col;
        switch (floats[i].col_idx) {
        case 1:  col = C_MSG_HEAL;  break;
        case 2:  col = C_MSG_ITEM;  break;
        case 3:  col = C_MSG_LEVEL; break;
        default: col = C_MSG_DMG;   break;
        }
        display_number(floats[i].sx, floats[i].sy - rise, floats[i].val, col);
        floats[i].timer--;
    }
}

/** Tracer line for a ranged attack that just landed. */
static void draw_shot(void) {
    if (!shot_timer) return;
    int x0 = MAP_OX + shot_x0 * TILE_W + TILE_W/2;
    int y0 = MAP_OY + shot_y0 * TILE_H + TILE_H/2;
    int x1 = MAP_OX + shot_x1 * TILE_W + TILE_W/2;
    int y1 = MAP_OY + shot_y1 * TILE_H + TILE_H/2;
    display_line(x0, y0, x1, y1, (shot_timer & 1) ? C_MSG_DMG : C_TEXT);
    shot_timer--;
}

/* ── HUD ─────────────────────────────────────────────────────────────── */
static int scroll_total(void) {
    int n = 0; for (int i=0;i<N_SCROLL;i++) n += scrolls[i]; return n;
}

static void draw_hud(void) {
    display_rect(0, 0, SCR_W, HUD_H, C_HUD_BG);
    display_hline(0, HUD_H, SCR_W, C_HUD_LINE);

    /* ── Row 1: HP / XP bars, level, floor ── */
    uint16_t hp_col = (p_hp * 3 <= p_max_hp) ? C_HP_LOW : C_HP;
    display_text(3, 3, "HP", hp_col);
    display_rect(22, 2, 66, 10, C_HP_BG);
    int hw = 66 * p_hp / p_max_hp;
    if (hw < 0) hw = 0;
    display_rect(22, 2, hw, 10, hp_col);
    if (!g_mono && hw > 0) { display_hline(22, 2, hw, 0xFFFF);          /* gloss */
                             display_hline(22, 11, hw, dim565(hp_col,2)); }
    display_rect_outline(22, 2, 66, 10, hp_col);

    display_text(94, 3, "XP", C_XP);
    display_rect(113, 2, 40, 10, C_XP_BG);
    int xw = 40 * p_xp / p_xp_next;
    display_rect(113, 2, xw, 10, C_XP);
    if (!g_mono && xw > 0) { display_hline(113, 2, xw, 0xFFFF);
                             display_hline(113, 11, xw, dim565(C_XP,2)); }
    display_rect_outline(113, 2, 40, 10, C_XP);

    display_text(160, 3, "Lv", C_TEXT_DIM);
    display_number(176, 3, p_level, C_TEXT);
    display_text(200, 3, "F", C_TEXT_DIM);
    display_number(210, 3, p_floor, C_TEXT);

    /* Torch fuel gauge, right-aligned on row 1 */
    int tf_x = SCR_W - 60;
    display_rect(tf_x, 3, 8, 8, C_TORCH);             /* torch stub */
    int fg = 44 * (torch_fuel > 0 ? torch_fuel : 0) / TORCH_MAX_FUEL;
    if (fg > 44) fg = 44;
    display_rect(tf_x+12, 3, 44, 8, C_HP_BG);
    display_rect(tf_x+12, 3, fg, 8, (torch_fuel <= 8) ? C_HP_LOW : C_TORCH);
    display_rect_outline(tf_x+12, 3, 44, 8, C_HUD_LINE);

    /* ── Row 2: gold, potions, scrolls, status effects ── */
    int y2 = 15;
    display_circle_fill(6, y2+4, 4, C_GOLD);
    display_number(13, y2, p_gold, C_TEXT);

    display_rect(66, y2+2, 3, 4, C_POTION);
    display_hline(64, y2+1, 7, C_POTION_HI);
    display_number(74, y2, p_potions, C_TEXT);

    display_rect(104, y2, 6, 8, C_SCROLL);            /* scroll icon */
    display_vline(104, y2, 8, C_SCROLL_HI);
    display_number(114, y2, scroll_total(), C_TEXT);

    /* Status badges, laid out left→right only when active */
    int bx = 150;
    if (buff_turns > 0)   { display_text(bx, y2, "ATK",  C_BUFF);   bx += 26; }
    if (poison_turns > 0) { display_text(bx, y2, "PSN",  C_POISON); bx += 26; }
    if (burn_turns > 0)   { display_text(bx, y2, "BRN",  C_BURN);   bx += 26; }
    if (regen_turns > 0)  { display_text(bx, y2, "REG",  C_REGEN);  bx += 26; }
    if (freeze_turns > 0) { display_text(bx, y2, "FRZ",  C_FREEZE); bx += 26; }

    /* Message bar */
    if (msg_timer > 0) {
        display_rect(0, SCR_H-MSG_H, SCR_W, MSG_H, C_HUD_BG);
        display_hline(0, SCR_H-MSG_H-1, SCR_W, C_HUD_LINE);
        display_text(4, SCR_H-MSG_H+3, msg_buf, msg_color);
        msg_timer--;
    }

    /* Level-up flash border */
    if (levelup_anim > 0) {
        uint16_t lc = (levelup_anim & 4) ? C_MSG_LEVEL : C_BG;
        display_rect_outline(0, HUD_H+1, SCR_W, SCR_H-HUD_H-2, lc);
        display_rect_outline(1, HUD_H+2, SCR_W-2, SCR_H-HUD_H-4, lc);
        levelup_anim--;
    }
}

/* ── Character sheet ─────────────────────────────────────────────────── */
static void show_character(void) {
    int bx = SCR_W*12/100, by = SCR_H*12/100;
    int bw = SCR_W*76/100, bh = SCR_H*76/100;
    int pa=1, pb=1, ps=1;

    while (1) {
        display_rounded_rect_fill(bx, by, bw, bh, 6, C_HUD_BG);
        display_rounded_rect(bx, by, bw, bh, 6, C_HUD_LINE);
        display_text_large(bx+12, by+8, "CHARACTER", C_MSG_LEVEL);
        display_hline(bx+8, by+28, bw-16, C_HUD_LINE);

        int ly = by + 36;
        display_text(bx+12, ly, "Weapon:", C_TEXT_DIM);
        display_text(bx+90, ly, WEAPON_NAME[p_weapon], C_WEAPON);
        display_text(bx+bw-52, ly, "+", C_TEXT_DIM);
        display_number(bx+bw-44, ly, WEAPON_ATK[p_weapon], C_TEXT);

        ly += 16;
        display_text(bx+12, ly, "Armour:", C_TEXT_DIM);
        display_text(bx+90, ly, ARMOR_NAME[p_armor], C_ARMOR_I);
        display_text(bx+bw-52, ly, "+", C_TEXT_DIM);
        display_number(bx+bw-44, ly, ARMOR_DEF[p_armor], C_TEXT);

        ly += 22;
        display_text(bx+12, ly, "Attack", C_TEXT_DIM);
        display_number(bx+90, ly, eff_atk(), C_MSG_DMG);
        display_text(bx+140, ly, "Defence", C_TEXT_DIM);
        display_number(bx+210, ly, eff_def(), C_XP);

        ly += 16;
        display_text(bx+12, ly, "Torch", C_TEXT_DIM);
        display_number(bx+90, ly, p_torch, C_MSG_ITEM);
        display_text(bx+140, ly, "Potions", C_TEXT_DIM);
        display_number(bx+210, ly, p_potions, C_MSG_HEAL);

        ly += 16;
        display_text(bx+12, ly, "Gold", C_TEXT_DIM);
        display_number(bx+90, ly, p_gold, C_GOLD);
        display_text(bx+140, ly, "Floor", C_TEXT_DIM);
        display_number(bx+210, ly, p_floor, C_TEXT);

        if (buff_turns > 0) {
            ly += 16;
            display_text(bx+12, ly, "Blessed for", C_BUFF);
            display_number(bx+110, ly, buff_turns, C_BUFF);
            display_text(bx+140, ly, "turns", C_BUFF);
        }

        display_hline(bx+8, by+bh-22, bw-16, C_HUD_LINE);
        display_text(bx+12, by+bh-16, "A / B: back", C_TEXT_DIM);
        display_flush();

        int a=gpio_read(BTN_A);
        int b=gpio_read(BTN_B), s=gpio_read(BTN_SETTINGS);
        if ((a && !pa) || (b && !pb) || (s && !ps)) return;
        pa=a; pb=b; ps=s;
        delay(20000);
    }
}

/* ── Scroll menu ─────────────────────────────────────────────────────────
 * Returns 1 if a scroll was cast (the caller then spends the turn). */
static int show_scroll_menu(void) {
    int cur=0, pu=1,pd=1,pa=1,pb=1,ps=1;
    int bx = SCR_W*16/100, by = SCR_H*16/100;
    int bw = SCR_W*68/100, bh = SCR_H*62/100;

    if (scroll_total() == 0) { set_msg("You have no scrolls.", C_MSG_INFO); return 0; }

    while (1) {
        display_rounded_rect_fill(bx, by, bw, bh, 6, C_HUD_BG);
        display_rounded_rect(bx, by, bw, bh, 6, C_HUD_LINE);
        display_text_large(bx+14, by+8, "SCROLLS", C_SCROLL);
        display_hline(bx+8, by+30, bw-16, C_HUD_LINE);

        for (int i=0; i<N_SCROLL; i++) {
            int iy = by+38+i*22;
            int have = scrolls[i];
            uint16_t fg = have ? C_TEXT : C_TEXT_DIM;
            if (i==cur) { display_rect(bx+8, iy-2, bw-16, 20, C_XP);
                          fg = C_BG; }
            display_text(bx+16, iy+3, SCROLL_NAME[i], fg);
            display_text(bx+bw-40, iy+3, "x", i==cur?C_BG:C_TEXT_DIM);
            display_number(bx+bw-30, iy+3, have, fg);
        }
        display_hline(bx+8, by+bh-22, bw-16, C_HUD_LINE);
        display_text(bx+14, by+bh-16, "A cast  B back", C_TEXT_DIM);
        display_flush();

        int u=gpio_read(BTN_UP), d=gpio_read(BTN_DOWN);
        int a=gpio_read(BTN_A),  b=gpio_read(BTN_B), s=gpio_read(BTN_SETTINGS);
        if ((b && !pb) || (s && !ps)) return 0;
        if (u && !pu) cur = (cur>0) ? cur-1 : N_SCROLL-1;
        if (d && !pd) cur = (cur<N_SCROLL-1) ? cur+1 : 0;
        if (a && !pa) {
            if (scrolls[cur] > 0) return use_scroll(cur);
            set_msg("None of those left.", C_MSG_INFO);
        }
        pu=u; pd=d; pa=a; pb=b; ps=s;
        delay(20000);
    }
}

/* ── Merchant (shown on descending the stairs) ───────────────────────────── */
enum { SHOP_POTION=0, SHOP_OIL, SHOP_SCROLL, SHOP_WEAPON, SHOP_ARMOR,
       SHOP_LEAVE, SHOP_COUNT };

static int shop_price(int item) {
    switch (item) {
    case SHOP_POTION: return 10 + p_floor*2;
    case SHOP_OIL:    return 8  + p_floor*2;
    case SHOP_SCROLL: return 15 + p_floor*3;
    case SHOP_WEAPON: return 25 + p_floor*6;
    case SHOP_ARMOR:  return 25 + p_floor*6;
    default:          return 0;
    }
}

static void shop_buy(int item) {
    int price = shop_price(item);
    if (item != SHOP_LEAVE && p_gold < price) { set_msg("Not enough gold.", C_MSG_DMG); return; }

    switch (item) {
    case SHOP_POTION: p_gold -= price; p_potions++; set_msg("Bought a potion.", C_MSG_HEAL); break;
    case SHOP_OIL:    p_gold -= price; torch_fuel = TORCH_MAX_FUEL; set_msg("Torch refuelled.", C_TORCH); break;
    case SHOP_SCROLL: { p_gold -= price; int k = rng_next(N_SCROLL); scrolls[k]++;
                        set_msg(SCROLL_NAME[k], C_MSG_LEVEL); break; }
    case SHOP_WEAPON:
        if (p_weapon >= N_WEAPONS-1) { set_msg("Weapon already the best.", C_MSG_INFO); return; }
        p_gold -= price; p_weapon++; set_msg(WEAPON_NAME[p_weapon], C_MSG_LEVEL); break;
    case SHOP_ARMOR:
        if (p_armor >= N_ARMORS-1) { set_msg("Armour already the best.", C_MSG_INFO); return; }
        p_gold -= price; p_armor++; set_msg(ARMOR_NAME[p_armor], C_MSG_LEVEL); break;
    default: break;
    }
}

static void shop_screen(void) {
    int cur=0, pu=1,pd=1,pa=1,pb=1,ps=1;
    static const char *label[SHOP_COUNT] = {
        "Potion", "Torch Oil", "Scroll (?)", "Upgrade Weapon", "Upgrade Armour", "Leave"
    };

    /* Debounce the descend press. */
    while (gpio_read(BTN_A)) delay(10000);
    delay(40000);

    while (1) {
        display_clear(C_BG);
        int bx = SCR_W*8/100, by = SCR_H*8/100, bw = SCR_W*84/100, bh = SCR_H*84/100;
        display_rounded_rect_fill(bx, by, bw, bh, 8, C_HUD_BG);
        display_rounded_rect(bx, by, bw, bh, 8, C_HUD_LINE);
        display_text_large(bx+14, by+8, "MERCHANT", C_GOLD);
        display_circle_fill(bx+bw-64, by+14, 4, C_GOLD);
        display_number(bx+bw-54, by+10, p_gold, C_TEXT);
        display_hline(bx+8, by+30, bw-16, C_HUD_LINE);

        for (int i=0; i<SHOP_COUNT; i++) {
            int iy = by+38+i*20;
            uint16_t fg = (i==cur) ? C_BG : C_TEXT;
            if (i==cur) display_rect(bx+8, iy-2, bw-16, 18, C_XP);
            display_text(bx+16, iy+2, label[i], fg);
            if (i != SHOP_LEAVE) {
                int price = shop_price(i);
                display_text(bx+bw-70, iy+2, "g", (i==cur)?C_BG:C_GOLD);
                display_number(bx+bw-62, iy+2, price, fg);
            }
        }
        display_hline(bx+8, by+bh-22, bw-16, C_HUD_LINE);
        display_text(bx+14, by+bh-16, "A buy   B / Leave: descend", C_TEXT_DIM);
        display_flush();

        int u=gpio_read(BTN_UP), d=gpio_read(BTN_DOWN);
        int a=gpio_read(BTN_A),  b=gpio_read(BTN_B), s=gpio_read(BTN_SETTINGS);
        if ((b && !pb) || (s && !ps)) return;
        if (u && !pu) cur = (cur>0) ? cur-1 : SHOP_COUNT-1;
        if (d && !pd) cur = (cur<SHOP_COUNT-1) ? cur+1 : 0;
        if (a && !pa) { if (cur == SHOP_LEAVE) return; shop_buy(cur); }
        pu=u; pd=d; pa=a; pb=b; ps=s;
        delay(20000);
    }
}

/* ── Pause menu ──────────────────────────────────────────────────────── */
enum { MENU_RESUME=0, MENU_CHAR, MENU_RESTART, MENU_EXIT, MENU_COUNT };
static const char *menu_labels[] = { "Resume", "Character", "Restart", "Exit" };

static int show_pause_menu(void) {
    int cur=0;
    int pu=1,pd=1,pa=1,pb=1,ps=1;
    int box_x = SCR_W*25/100, box_y = SCR_H*18/100;
    int box_w = SCR_W*50/100, box_h = SCR_H*64/100;

    while (1) {
        display_rounded_rect_fill(box_x, box_y, box_w, box_h, 6, C_HUD_BG);
        display_rounded_rect(box_x, box_y, box_w, box_h, 6, C_HUD_LINE);
        display_text_large(box_x+22, box_y+10, "PAUSED", C_MSG_LEVEL);
        display_hline(box_x+8, box_y+32, box_w-16, C_HUD_LINE);

        for (int i=0; i<MENU_COUNT; i++) {
            int iy = box_y+40+i*20;
            if (i==cur) {
                display_rect(box_x+10, iy, box_w-20, 18, C_XP);
                display_text(box_x+22, iy+4, menu_labels[i], C_BG);
            } else {
                display_text(box_x+22, iy+4, menu_labels[i], C_TEXT_DIM);
            }
        }
        display_flush();

        int u=gpio_read(BTN_UP), d=gpio_read(BTN_DOWN);
        int a=gpio_read(BTN_A),  b=gpio_read(BTN_B), s=gpio_read(BTN_SETTINGS);
        if (s && !ps) return MENU_RESUME;
        if (u && !pu) cur = (cur>0) ? cur-1 : MENU_COUNT-1;
        if (d && !pd) cur = (cur<MENU_COUNT-1) ? cur+1 : 0;
        if ((a && !pa) || (b && !pb)) {
            if (cur == MENU_CHAR) { show_character(); pa=1; pb=1; }
            else                  { return cur; }
        }
        pu=u; pd=d; pa=a; pb=b; ps=s;
        delay(20000);
    }
}

/* ── Input ───────────────────────────────────────────────────────────── */
static int pu,pd,pl,pr,pa,pb,ps,pxb;   /* pxb = prev X (px is the player!) */
static int turn_taken;

static int handle_input(void) {
    int u=gpio_read(BTN_UP),   d=gpio_read(BTN_DOWN);
    int l=gpio_read(BTN_LEFT), r=gpio_read(BTN_RIGHT);
    int a=gpio_read(BTN_A),    b=gpio_read(BTN_B), s=gpio_read(BTN_SETTINGS);
    int xb=gpio_read(BTN_X);

    turn_taken=0;
    int nx=px, ny=py;
    if      (u&&!pu) { ny--; turn_taken=1; }
    else if (d&&!pd) { ny++; turn_taken=1; }
    else if (l&&!pl) { nx--; turn_taken=1; }
    else if (r&&!pr) { nx++; turn_taken=1; }

    if (turn_taken) {
        if (nx>=0&&nx<MAP_W&&ny>=0&&ny<MAP_H&&map[ny][nx]!=T_WALL) {
            int hit=-1;
            for (int i=0; i<MAX_ENEMIES; i++)
                if (enemies[i].alive && enemies[i].x==nx && enemies[i].y==ny)
                    { hit=i; break; }
            if (hit>=0) {
                attack_enemy(hit);
            } else {
                px=nx; py=ny;
                compute_fov();
                spring_trap();
                if (!game_over) try_pickup();
            }
        }
    }

    /* A — context action on the tile underfoot */
    if (a&&!pa) {
        if (map[py][px]==T_STAIRS) {
            shop_screen();              /* trade before the descent */
            p_floor++;
            generate_map();
            set_msg("Descending deeper...", C_MSG_INFO);
            turn_taken=1;
        } else if (map[py][px]==T_CHEST) {
            open_chest();
            turn_taken=1;
        } else if (map[py][px]==T_SHRINE) {
            use_shrine();
            turn_taken=1;
        }
    }

    /* X — open the scroll menu */
    if (xb&&!pxb) {
        if (show_scroll_menu()) turn_taken=1;
    }

    if (b&&!pb) {
        if (p_potions>0 && p_hp<p_max_hp) {
            p_potions--;
            int heal = 8 + p_level*2;
            p_hp += heal;
            if (p_hp>p_max_hp) p_hp=p_max_hp;
            spawn_float(px, py, heal, 1);
            set_msg("Potion! HP restored.", C_MSG_HEAL);
            turn_taken=1;
        } else if (p_potions<=0) {
            set_msg("No potions!", C_MSG_INFO);
        } else {
            set_msg("HP is full.", C_MSG_INFO);
        }
    }

    if (s&&!ps) { ps=s; return 1; }
    pu=u; pd=d; pl=l; pr=r; pa=a; pb=b; ps=s; pxb=xb;
    return 0;
}

/* ── Per-turn upkeep ─────────────────────────────────────────────────── */
static void end_turn(void) {
    if (buff_turns > 0) {
        buff_turns--;
        if (buff_turns == 0) { buff_atk = 0; set_msg("The blessing fades.", C_MSG_INFO); }
    }

    /* Damage-over-time.  Burn is fiercer than poison but shorter. */
    if (burn_turns > 0) {
        burn_turns--;
        hurt_player(2 + p_floor/3, "Burning!");
        if (game_over) return;
    }
    if (poison_turns > 0) {
        poison_turns--;
        hurt_player(1 + p_floor/4, "Poison courses through you.");
        if (game_over) return;
    }
    if (regen_turns > 0) {
        regen_turns--;
        if (p_hp < p_max_hp) { p_hp += 2; if (p_hp > p_max_hp) p_hp = p_max_hp;
                               spawn_float(px, py, 2, 1); }
    }

    /* The torch burns down; warn as it gutters. */
    if (torch_fuel > 0) {
        torch_fuel--;
        if (torch_fuel == 8)  set_msg("Your torch is guttering...", C_TORCH);
        if (torch_fuel == 0)  set_msg("Your torch dies! It's dark.", C_MSG_DMG);
    }
}

/* ── Animation tick ──────────────────────────────────────────────────── */
static void tick_anims(void) {
    anim_tick++;
    for (int i=0; i<MAX_ENEMIES; i++)
        if (enemy_flash[i]) enemy_flash[i]--;
    if (player_flash) player_flash--;
    if (pickup_anim)  pickup_anim--;
    if (shake)        shake--;
}

/* ── Pickup sparkle overlay ──────────────────────────────────────────── */
static void draw_pickup_sparkle(void) {
    if (!pickup_anim) return;
    int sx = MAP_OX + pickup_x * TILE_W + TILE_W/2;
    int sy = MAP_OY + pickup_y * TILE_H + TILE_H/2;
    int r  = pickup_anim / 3 + 1;
    display_circle(sx, sy, r, C_GOLD_HI);
    if (pickup_anim > 10) display_circle(sx, sy, r + 2, C_MSG_ITEM);
}

/* ── Full frame ──────────────────────────────────────────────────────── */
static void draw_frame(void) {
    display_clear(C_BG);

    /* Screen shake: nudge the map origin for a few frames after a hit. */
    int32_t ox = MAP_OX, oy = MAP_OY;
    if (shake && !g_mono) {
        MAP_OX += (anim_tick & 1) ? 2 : -2;
        MAP_OY += (anim_tick & 2) ? 1 : -1;
    }

    draw_map();
    draw_items();
    for (int i=0; i<MAX_ENEMIES; i++) draw_enemy(i);
    draw_player();
    draw_shot();
    draw_pickup_sparkle();
    draw_floats_fn();

    MAP_OX = ox; MAP_OY = oy;   /* restore before HUD so it stays put */

    /* Low-HP vignette — a pulsing red frame around the play area. */
    if (!g_mono && p_hp*4 <= p_max_hp && p_hp > 0 && (anim_tick & 4)) {
        display_rect_outline(0, HUD_H+1, SCR_W, SCR_H-HUD_H-2, C_MSG_DMG);
        display_rect_outline(1, HUD_H+2, SCR_W-2, SCR_H-HUD_H-4, C_MSG_DMG);
    }

    draw_hud();
    display_flush();
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
        if (turn_taken) {
            move_enemies();
            end_turn();
            compute_fov();
        }
        tick_anims();
        draw_frame();
        delay(FRAME_US);
    }
}

/* ── Title screen ────────────────────────────────────────────────────── */
static void draw_title(void) {
    display_clear(C_BG);

    /* Torch-lit stone border */
    for (int x=0; x<SCR_W; x+=TILE_W) {
        draw_wall(x, 0, 0, 1);
        draw_wall(x, SCR_H-TILE_H, 1, 1);
    }
    for (int y=TILE_H; y<SCR_H-TILE_H; y+=TILE_H) {
        draw_wall(0, y, y/TILE_H, 1);
        draw_wall(SCR_W-TILE_W, y, y/TILE_H, 1);
    }

    display_text_large(SCR_W*22/100, SCR_H*17/100, "PIXEL",   C_MSG_ITEM);
    display_text_large(SCR_W*11/100, SCR_H*29/100, "DUNGEON", C_MSG_LEVEL);
    display_hline(SCR_W*8/100, SCR_H*41/100, SCR_W*84/100, C_HUD_LINE);

    display_text(SCR_W*20/100, SCR_H*44/100, "Explore  Fight  Survive", C_MSG_INFO);

    display_text(SCR_W*16/100, SCR_H*54/100, "DPAD: Move / Attack",   C_TEXT);
    display_text(SCR_W*16/100, SCR_H*60/100, "A: Stairs/Chest/Shrine", C_TEXT);
    display_text(SCR_W*16/100, SCR_H*66/100, "B: Potion   X: Scrolls", C_TEXT);
    display_text(SCR_W*16/100, SCR_H*72/100, "SETTINGS: Pause",        C_TEXT);

    display_hline(SCR_W*8/100, SCR_H*78/100, SCR_W*84/100, C_HUD_LINE);
    if (hi_score > 0) {
        display_text(SCR_W*24/100, SCR_H*81/100, "Best Score:", C_GOLD);
        display_number(SCR_W*62/100, SCR_H*81/100, hi_score, C_GOLD);
    }
    display_text(SCR_W*25/100, SCR_H*87/100, "Press A to begin!", C_MSG_ITEM);
    display_text(SCR_W*24/100, SCR_H*93/100, "AkiraOS Edition",   C_TEXT_DIM);
    display_flush();
}

/* ── Death screen ────────────────────────────────────────────────────── */
static void draw_death(void) {
    display_clear(C_BG);
    display_rect_outline(10, 10, SCR_W-20, SCR_H-20, C_MSG_DMG);
    display_rect_outline(12, 12, SCR_W-24, SCR_H-24, C_MSG_DMG);
    display_text_large(SCR_W*28/100, SCR_H*14/100, "YOU",  C_MSG_DMG);
    display_text_large(SCR_W*24/100, SCR_H*26/100, "DIED", C_MSG_DMG);
    display_hline(SCR_W*9/100, SCR_H*38/100, SCR_W*82/100, C_HUD_LINE);

    int score = cur_score();
    int best  = (score > hi_score) ? score : hi_score;

    display_text(SCR_W*25/100, SCR_H*42/100, "Floor  :", C_TEXT_DIM);
    display_number(SCR_W*52/100, SCR_H*42/100, p_floor, C_TEXT);
    display_text(SCR_W*25/100, SCR_H*48/100, "Level  :", C_TEXT_DIM);
    display_number(SCR_W*52/100, SCR_H*48/100, p_level, C_MSG_LEVEL);
    display_text(SCR_W*25/100, SCR_H*54/100, "Gold   :", C_TEXT_DIM);
    display_number(SCR_W*52/100, SCR_H*54/100, p_gold, C_GOLD);
    display_text(SCR_W*25/100, SCR_H*60/100, "Weapon :", C_TEXT_DIM);
    display_text(SCR_W*52/100, SCR_H*60/100, WEAPON_NAME[p_weapon], C_WEAPON);

    display_text(SCR_W*25/100, SCR_H*68/100, "Score  :", C_TEXT_DIM);
    display_number(SCR_W*52/100, SCR_H*68/100, score, C_TEXT);
    display_text(SCR_W*25/100, SCR_H*74/100, "Best   :", C_TEXT_DIM);
    display_number(SCR_W*52/100, SCR_H*74/100, best, C_GOLD);
    if (score >= best && score > 0)
        display_text(SCR_W*58/100, SCR_H*74/100, "NEW!", C_MSG_LEVEL);

    display_hline(SCR_W*9/100, SCR_H*81/100, SCR_W*82/100, C_HUD_LINE);
    display_text(SCR_W*23/100, SCR_H*86/100, "Press A to restart", C_MSG_INFO);
    display_flush();
}

/* ── Entry point ─────────────────────────────────────────────────────── */
int main(void) {
    printf("AkiraOS Pixel Dungeon Remastered");

    display_get_size(&SCR_W, &SCR_H);
    g_mono = (SCR_W >= 400);      /* Sharp Memory LCD is 400x240 and 1-bit */
    palette_init();

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
    gpio_configure(BTN_X,        GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_SETTINGS, GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);

    load_hi_score();
    draw_title();

    uint32_t frames = 1;
    while (!gpio_read(BTN_A) && !gpio_read(BTN_B)) { frames++; delay(20000); }
    reseed(frames);
    delay(80000);

    while (1) {
        restart_flag=0; exit_flag=0;
        init_game();
        game_loop();

        if (exit_flag) { app_switch("supervisor"); return 0; }
        if (restart_flag) { reseed(++frames); continue; }

        /* Death — bank the score if it's a new best. */
        int score = cur_score();
        if (score > hi_score) { hi_score = score; save_hi_score(); }

        draw_death();
        while (!gpio_read(BTN_A) && !gpio_read(BTN_B)) delay(20000);
        delay(100000);
        reseed(++frames);
    }

    return 0;
}
