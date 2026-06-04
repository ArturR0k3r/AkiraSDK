/*
 * engine.h — Glasses Mystery: Core types, constants, rendering & input API
 */
#ifndef ENGINE_H
#define ENGINE_H

#include <stdint.h>

/* ─── Akira API externs (defined in akira_api.h, included only from engine.c) */
#ifndef _AKIRA_API_INCLUDED
extern int display_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
extern int display_rect_outline(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
extern int display_text(int32_t x, int32_t y, const char *text, uint32_t color);
extern int display_text_large(int32_t x, int32_t y, const char *text, uint32_t color);
extern int display_flush(void);
extern int display_clear(uint32_t color);
extern int display_bitmap(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t *data);
extern int display_bitmap_transparent(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t *data, uint32_t transparent_color);
extern int display_hline(int32_t x, int32_t y, int32_t w, uint32_t color);
extern int display_pixel(int32_t x, int32_t y, uint32_t color);
extern int display_number(int32_t x, int32_t y, int32_t num, uint32_t color);
extern int display_line(int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t color);
extern int display_circle(int32_t cx, int32_t cy, int32_t r, uint32_t color);
extern int display_circle_fill(int32_t cx, int32_t cy, int32_t r, uint32_t color);
extern int gpio_read(int32_t pin);
extern int gpio_configure(uint32_t pin, uint32_t flags);
extern int timer_create(void);
extern int timer_start(int32_t handle);
extern int timer_elapsed(int32_t handle);
extern int timer_free(int32_t handle);
extern int delay(uint32_t microseconds);
extern int storage_open(const char *name, int32_t flags);
extern int storage_read(int32_t handle, void *buf, int32_t size);
extern int storage_write(int32_t handle, const void *buf, int32_t size);
extern int storage_close(int32_t handle);
extern int app_switch(const char *name);
extern int display_get_size(int32_t *w_out, int32_t *h_out);
extern uint64_t get_time_ms(void);
void printf(const char *fmt, ...);
char *itoa(int value, char *str, int base);
#endif /* _AKIRA_API_INCLUDED */

/* ─── Display ─────────────────────────────────────────────────────────────── */
extern int32_t g_screen_w, g_screen_h;
#define SCREEN_W      g_screen_w
#define SCREEN_H      g_screen_h
#define TILE_SIZE     8
#define TILES_X       40                         /* SCREEN_W / TILE_SIZE */
#define TILES_Y       30                         /* SCREEN_H / TILE_SIZE */
#define TILE_PIXELS   (TILE_SIZE * TILE_SIZE)   /* 64 */
#define TILE_BYTES    (TILE_PIXELS * 2)         /* 128 bytes (RGB565) */

/* HUD uses bottom 3 tile rows when dialogue is hidden */
#define HUD_Y         (SCREEN_H - 24)          /* y=216 */
#define MAP_TILES_Y   27                        /* playable area = 27 rows */

/* ─── Sprite sizes ────────────────────────────────────────────────────────── */
#define SPRITE_W      16
#define SPRITE_H      16
#define SPRITE_PIXELS (SPRITE_W * SPRITE_H)     /* 256 */
#define SPRITE_BYTES  (SPRITE_PIXELS * 2)       /* 512 bytes */
#define TRANSPARENT   0x1FF8                    /* byte-swapped 0xF81F magenta */

/* ─── GPIO button pins (Akira Console layout) ─────────────────────────────── */
#define BTN_UP        4
#define BTN_DOWN      5
#define BTN_LEFT      7
#define BTN_RIGHT     6
#define BTN_A         15
#define BTN_B         16
#define BTN_SETTINGS  0   /* BTN.OK = GPIO0, active-low pull-up */

#define GPIO_INPUT     (1U << 0)
#define GPIO_PULL_DOWN (1U << 5)

/* ─── Input state ─────────────────────────────────────────────────────────── */
#define INPUT_UP      0
#define INPUT_DOWN    1
#define INPUT_LEFT    2
#define INPUT_RIGHT   3
#define INPUT_A       4
#define INPUT_B       5
#define INPUT_SETTINGS 6
#define INPUT_COUNT   7

typedef struct {
    uint8_t held[INPUT_COUNT];       /* 1 = currently pressed */
    uint8_t pressed[INPUT_COUNT];    /* 1 = just pressed this frame (rising edge) */
    uint8_t prev[INPUT_COUNT];       /* previous frame state */
} input_t;

/* ─── Game states ─────────────────────────────────────────────────────────── */
typedef enum {
    STATE_TITLE,
    STATE_PLAYING,
    STATE_DIALOGUE,
    STATE_INVENTORY,
    STATE_PAUSE,
    STATE_ENDING,
    STATE_TRANSITION
} game_state_t;

/* ─── Directions ──────────────────────────────────────────────────────────── */
typedef enum {
    DIR_DOWN  = 0,
    DIR_UP    = 1,
    DIR_LEFT  = 2,
    DIR_RIGHT = 3
} direction_t;

/* ─── Tile properties ─────────────────────────────────────────────────────── */
#define TILE_WALKABLE     0x00
#define TILE_SOLID        0x01
#define TILE_WATER        0x02
#define TILE_INTERACTIVE  0x04
#define TILE_DOOR         0x08
#define TILE_ITEM         0x10
#define TILE_ANOMALY      0x20
#define TILE_DARK         0x40   /* needs lantern */

/* ─── Tile IDs ────────────────────────────────────────────────────────────── */
/* Ground / Natural */
#define T_GRASS           0
#define T_GRASS2          1
#define T_DIRT            2
#define T_SAND            3
#define T_STONE           4
#define T_DARK_STONE      5
#define T_CAVE_FLOOR      6
/* Water */
#define T_WATER           7
#define T_WATER_DEEP      8
#define T_WATER_EDGE_N    9
#define T_WATER_EDGE_S    10
#define T_WATER_EDGE_E    11
#define T_WATER_EDGE_W    12
/* Paths */
#define T_PATH_H          13
#define T_PATH_V          14
#define T_PATH_CROSS      15
#define T_ROAD            16
#define T_SIDEWALK        17
/* Walls / Buildings */
#define T_WALL_TOP        18
#define T_WALL_SIDE       19
#define T_WALL_BOT        20
#define T_ROOF            21
#define T_WINDOW          22
#define T_DOOR_CLOSED     23
#define T_DOOR_OPEN       24
#define T_CHIMNEY         25
/* Indoor */
#define T_WOOD_FLOOR      26
#define T_CARPET          27
#define T_BOOKSHELF       28
#define T_TABLE           29
#define T_BED             30
#define T_CHAIR           31
#define T_FIREPLACE       32
#define T_STAIRS_UP       33
#define T_STAIRS_DOWN     34
/* Nature */
#define T_TREE_TL         35
#define T_TREE_TR         36
#define T_TREE_BL         37
#define T_TREE_BR         38
#define T_BUSH            39
#define T_FLOWER          40
#define T_ROCK            41
#define T_TALL_GRASS      42
/* Dungeon / Cave */
#define T_CAVE_WALL       43
#define T_CRYSTAL          44
#define T_TORCH           45
#define T_CHEST_CLOSED    46
#define T_CHEST_OPEN      47
#define T_LADDER          48
/* Special / Anomaly */
#define T_ANOMALY_GROUND  49
#define T_GLOW_TILE       50
#define T_PORTAL          51
#define T_BRIDGE_H        52
#define T_BRIDGE_V        53
/* UI / Misc */
#define T_FENCE_H         54
#define T_FENCE_V         55
#define T_SIGN            56
#define T_LAMP            57
#define T_BARREL          58
#define T_CRATE           59
#define NUM_TILES         60

/* ─── Screen IDs ──────────────────────────────────────────────────────────── */
/* Outdoor screens (grid) */
#define SCR_FOREST_NW       0
#define SCR_FOREST_N        1
#define SCR_FOREST_NE       2
#define SCR_MANSION_EXT     3
#define SCR_LAKE_W          4
#define SCR_FOREST_W        5
#define SCR_TOWN_NW         6
#define SCR_TOWN_NE         7
#define SCR_MANSION_EAST    8
#define SCR_LAKE_E          9
#define SCR_PARK            10
#define SCR_TOWN_SW         11
#define SCR_TOWN_SE         12
#define SCR_FOREST_PATH     13
#define SCR_FOREST_DEEP     14
#define SCR_CAVE_ENTRY      15
#define SCR_BEACH_W         16
#define SCR_BEACH_E         17
#define SCR_DOCKS           18
#define SCR_BEACH_COVE      19
#define SCR_CLIFFS          20
/* Indoor screens (teleport) */
#define SCR_HOUSE_GROUND    21
#define SCR_HOUSE_ATTIC     22
#define SCR_TOWN_HALL       23
#define SCR_SHOP            24
#define SCR_LIBRARY         25
#define SCR_OLD_CABIN       26
#define SCR_MANSION_HALL    27
#define SCR_MANSION_STUDY   28
#define SCR_MANSION_BASEMENT 29
#define SCR_CAVE_INSIDE     30
#define SCR_CAVE_PASSAGES   31
#define SCR_ANOMALY_CHAMBER 32
#define SCR_LAB             33
#define NUM_SCREENS         34

/* Directions for screen connections */
#define CONN_UP    0
#define CONN_DOWN  1
#define CONN_LEFT  2
#define CONN_RIGHT 3

/* ─── Item IDs ────────────────────────────────────────────────────────────── */
#define ITEM_NONE             0
#define ITEM_HOUSE_KEY        1
#define ITEM_DIARY            2
#define ITEM_LANTERN          3
#define ITEM_COMPASS          4
#define ITEM_MANSION_KEY      5
#define ITEM_LAB_KEYCARD      6
#define ITEM_CRYSTAL_1        7
#define ITEM_CRYSTAL_2        8
#define ITEM_CRYSTAL_3        9
#define ITEM_TACKLE_BOX       10
#define ITEM_ANOMALY_SAMPLE   11
#define ITEM_MAP_FRAGMENT     12
#define ITEM_DIARY_PAGE       13   /* generic; count tracked separately */
#define NUM_ITEM_TYPES        14

/* ─── Quest flags (bit positions in quest_flags) ──────────────────────────── */
#define QF_HAS_DIARY          (1U << 0)
#define QF_TALKED_MAYOR       (1U << 1)
#define QF_MAYOR_MET          QF_TALKED_MAYOR
#define QF_TALKED_WHITMORE    (1U << 2)
#define QF_WHITMORE_MET       QF_TALKED_WHITMORE
#define QF_FOUND_OLD_MAN      (1U << 3)
#define QF_OLD_MAN_MET        QF_FOUND_OLD_MAN
#define QF_HAS_COMPASS        (1U << 4)
#define QF_CRYSTAL_1          (1U << 5)
#define QF_CRYSTAL_2          (1U << 6)
#define QF_CRYSTAL_3          (1U << 7)
#define QF_HAS_MANSION_KEY    (1U << 8)
#define QF_MANSION_EXPLORED   (1U << 9)
#define QF_HAS_KEYCARD        (1U << 10)
#define QF_LAB_FOUND          (1U << 11)
#define QF_ENDING_CHOSEN      (1U << 12)
#define QF_ELARA_MET          (1U << 24)
#define QF_WHITMORE_QUEST     (1U << 25)
/* Side quests */
#define QF_PETE_QUEST_START   (1U << 13)
#define QF_PETE_QUEST         QF_PETE_QUEST_START
#define QF_PETE_QUEST_DONE    (1U << 14)
#define QF_LIBRARY_QUEST_START (1U << 15)
#define QF_LIBRARY_QUEST      QF_LIBRARY_QUEST_START
#define QF_LIBRARY_QUEST_DONE (1U << 16)
#define QF_TOMMY_QUEST_START  (1U << 17)
#define QF_TOMMY_QUEST        QF_TOMMY_QUEST_START
#define QF_TOMMY_QUEST_DONE   (1U << 18)
#define QF_ELARA_QUEST_START  (1U << 19)
#define QF_ELARA_QUEST        QF_ELARA_QUEST_START
#define QF_ELARA_QUEST_DONE   (1U << 20)
/* Misc flags */
#define QF_HOUSE_ENTERED      (1U << 21)
#define QF_HAS_LANTERN        (1U << 22)
#define QF_GHOST_MET          (1U << 23)

/* Ending types */
#define ENDING_NONE    0
#define ENDING_SEAL    1
#define ENDING_POWER   2
#define ENDING_BALANCE 3

/* ─── NPC IDs ─────────────────────────────────────────────────────────────── */
#define NPC_MAYOR       0
#define NPC_ADA         1
#define NPC_WHITMORE    2
#define NPC_PETE        3
#define NPC_ELARA       4
#define NPC_OLD_MAN     5
#define NPC_TOMMY       6
#define NPC_GHOST       7
#define NUM_NPCS        8

/* ─── Simple PRNG ─────────────────────────────────────────────────────────── */
extern uint32_t g_rng_seed;
static inline uint32_t rng_next(void) {
    g_rng_seed = g_rng_seed * 1103515245U + 12345U;
    return (g_rng_seed >> 16) & 0x7FFF;
}

/* ─── Save file magic ─────────────────────────────────────────────────────── */
#define SAVE_MAGIC  0x474D5356  /* "GMSV" */


/* ─── Engine functions ────────────────────────────────────────────────────── */

/* Input */
void input_init(input_t *inp);
void input_poll(input_t *inp);

/* Tile rendering */
void render_tile(int tx, int ty, uint8_t tile_id);
void render_screen(const uint8_t *decoded_tiles);
void render_screen_region(const uint8_t *decoded_tiles, int tx0, int ty0, int tx1, int ty1);

/* Sprite rendering */
void render_sprite(int px, int py, const uint16_t *sprite_data);

/* RLE decode */
void rle_decode(const uint8_t *rle_data, int rle_size, uint8_t *out, int out_size);

/* Tile collision */
uint8_t tile_get_property(uint8_t tile_id);
int tile_is_solid(uint8_t tile_id);

/* Screen transitions */
int screen_get_connection(int screen_id, int direction);

/* Anomaly effects */
void render_anomaly_effect(int tx, int ty, uint32_t frame_counter);

/* HUD */
void render_hud(void);

/* Title screen */
void render_title_screen(int selection);

/* Pause menu */
int render_pause_menu(int selection);

/* Ending screen */
void render_ending(int ending_type, uint32_t frame);

/* Inventory screen */
void render_inventory(void);

/* Render player/NPC sprites (data is in engine.c via assets.h) */
void render_player_sprite(int px, int py, int direction, int frame);
void render_npc_sprite(int px, int py, int npc_id, int frame);

/* Save/Load */
int save_game(void);
int load_game(void);

/* Color utilities */
static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

static inline uint16_t darken_color(uint16_t c, int factor) {
    /* factor: 0=black, 255=full brightness */
    uint16_t r = (c >> 11) & 0x1F;
    uint16_t g = (c >> 5) & 0x3F;
    uint16_t b = c & 0x1F;
    r = (r * factor) >> 8;
    g = (g * factor) >> 8;
    b = (b * factor) >> 8;
    return (r << 11) | (g << 5) | b;
}

#endif /* ENGINE_H */
