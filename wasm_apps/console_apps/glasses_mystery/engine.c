/*
 * engine.c — Glasses Mystery: Core rendering, input, save/load
 * This is the ONLY file that includes assets.h (to keep static const data single-copy).
 */
#include "akira_api.h"
#define _AKIRA_API_INCLUDED
#include "engine.h"
#include "entities.h"
#include "world.h"
#include "dialogue.h"
#include "assets.h"

/* ─── Globals ─────────────────────────────────────────────────────────────── */
uint32_t g_rng_seed = 42;

/* ─── Input ───────────────────────────────────────────────────────────────── */
void input_init(input_t *inp) {
    /* Configure GPIO pins as inputs with pull-down */
    gpio_configure(BTN_UP,       GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);
    gpio_configure(BTN_DOWN,     GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);
    gpio_configure(BTN_LEFT,     GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);
    gpio_configure(BTN_RIGHT,    GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);
    gpio_configure(BTN_A,        GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);
    gpio_configure(BTN_B,        GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);
    gpio_configure(BTN_SETTINGS, GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);

    for (int i = 0; i < INPUT_COUNT; i++) {
        inp->held[i] = 0;
        inp->pressed[i] = 0;
        inp->prev[i] = 0;
    }
}

void input_poll(input_t *inp) {
    /* Save previous state */
    for (int i = 0; i < INPUT_COUNT; i++)
        inp->prev[i] = inp->held[i];

    /* Read GPIO */
    inp->held[INPUT_UP]       = gpio_read(BTN_UP)       ? 1 : 0;
    inp->held[INPUT_DOWN]     = gpio_read(BTN_DOWN)     ? 1 : 0;
    inp->held[INPUT_LEFT]     = gpio_read(BTN_LEFT)     ? 1 : 0;
    inp->held[INPUT_RIGHT]    = gpio_read(BTN_RIGHT)    ? 1 : 0;
    inp->held[INPUT_A]        = gpio_read(BTN_A)        ? 1 : 0;
    inp->held[INPUT_B]        = gpio_read(BTN_B)        ? 1 : 0;
    inp->held[INPUT_SETTINGS] = gpio_read(BTN_SETTINGS) ? 1 : 0;

    /* Detect rising edges */
    for (int i = 0; i < INPUT_COUNT; i++)
        inp->pressed[i] = (inp->held[i] && !inp->prev[i]) ? 1 : 0;
}

/* ─── Tile rendering ──────────────────────────────────────────────────────── */
void render_tile(int tx, int ty, uint8_t tile_id) {
    if (tile_id >= NUM_TILES) return;
    int px = tx * TILE_SIZE;
    int py = ty * TILE_SIZE;
    display_bitmap(px, py, TILE_SIZE, TILE_SIZE,
                   tile_gfx[tile_id], TILE_BYTES);
}

void render_screen(const uint8_t *decoded_tiles) {
    for (int ty = 0; ty < MAP_TILES_Y; ty++) {
        for (int tx = 0; tx < TILES_X; tx++) {
            uint8_t tid = decoded_tiles[ty * TILES_X + tx];
            render_tile(tx, ty, tid);
        }
    }
}

void render_screen_region(const uint8_t *decoded_tiles, int tx0, int ty0, int tx1, int ty1) {
    for (int ty = ty0; ty < ty1 && ty < MAP_TILES_Y; ty++) {
        for (int tx = tx0; tx < tx1 && tx < TILES_X; tx++) {
            if (tx >= 0 && ty >= 0) {
                uint8_t tid = decoded_tiles[ty * TILES_X + tx];
                render_tile(tx, ty, tid);
            }
        }
    }
}

/* ─── Sprite rendering ────────────────────────────────────────────────────── */
void render_sprite(int px, int py, const uint16_t *sprite_data) {
    display_bitmap_transparent(px, py, SPRITE_W, SPRITE_H,
                               sprite_data, SPRITE_BYTES, TRANSPARENT);
}

/* Accessor: render a player sprite by direction/frame (data in assets.h) */
void render_player_sprite(int px, int py, int direction, int frame) {
    int idx = direction * 2 + (frame & 1);
    if (idx < 0 || idx >= 8) idx = 0;
    render_sprite(px, py, player_sprites[idx]);
}

/* Accessor: render an NPC sprite by npc_id/frame (data in assets.h) */
void render_npc_sprite(int px, int py, int npc_id, int frame) {
    int idx = npc_id * 2 + (frame & 1);
    if (idx < 0 || idx >= 16) idx = 0;
    render_sprite(px, py, npc_sprites[idx]);
}

/* ─── RLE decode ──────────────────────────────────────────────────────────── */
/* Format: [count, tile_id, count, tile_id, ...] */
void rle_decode(const uint8_t *rle_data, int rle_size, uint8_t *out, int out_size) {
    int pos = 0;
    int i = 0;
    while (i < rle_size - 1 && pos < out_size) {
        uint8_t count = rle_data[i];
        uint8_t tile  = rle_data[i + 1];
        i += 2;
        for (int j = 0; j < count && pos < out_size; j++) {
            out[pos++] = tile;
        }
    }
    /* Fill remainder with grass */
    while (pos < out_size) out[pos++] = T_GRASS;
}

/* ─── Tile properties ─────────────────────────────────────────────────────── */
uint8_t tile_get_property(uint8_t tile_id) {
    if (tile_id >= NUM_TILES) return TILE_SOLID;
    return tile_props[tile_id];
}

int tile_is_solid(uint8_t tile_id) {
    return (tile_get_property(tile_id) & TILE_SOLID) != 0;
}

/* ─── Screen connections ──────────────────────────────────────────────────── */
int screen_get_connection(int screen_id, int direction) {
    if (screen_id < 0 || screen_id >= NUM_SCREENS) return -1;
    if (direction < 0 || direction > 3) return -1;
    return world_screens[screen_id].connections[direction];
}

/* ─── Anomaly effect ──────────────────────────────────────────────────────── */
void render_anomaly_effect(int tx, int ty, uint32_t frame_counter) {
    int px = tx * TILE_SIZE;
    int py = ty * TILE_SIZE;

    /* Pulsating purple glow over the tile */
    uint8_t phase = (uint8_t)((frame_counter >> 2) & 0x07);
    uint16_t colors[4] = {0x780F, 0x8810, 0xA01F, 0xC81F};
    uint16_t color = colors[phase & 3];

    /* Draw small glowing pixels scattered on the tile */
    int seed = tx * 31 + ty * 17 + (int)(frame_counter >> 3);
    for (int i = 0; i < 4; i++) {
        int ox = (seed + i * 7) % TILE_SIZE;
        int oy = (seed + i * 13) % TILE_SIZE;
        display_pixel(px + ox, py + oy, color);
    }
}

/* ─── HUD ─────────────────────────────────────────────────────────────────── */
void render_hud(void) {
    /* Dark bar at bottom */
    display_rect(0, HUD_Y, SCREEN_W, SCREEN_H - HUD_Y, 0x1082);

    /* Diary pages count */
    char buf[32];
    buf[0] = 'P'; buf[1] = ':';
    itoa(player.diary_pages, buf + 2);
    display_text(4, HUD_Y + 4, buf, 0xFFFF);

    /* Crystal count */
    int crystals = 0;
    if (player.quest_flags & QF_CRYSTAL_1) crystals++;
    if (player.quest_flags & QF_CRYSTAL_2) crystals++;
    if (player.quest_flags & QF_CRYSTAL_3) crystals++;
    buf[0] = 'C'; buf[1] = ':';
    itoa(crystals, buf + 2);
    display_text(60, HUD_Y + 4, buf, 0x07FF);

    /* Lantern indicator */
    if (player.quest_flags & QF_HAS_LANTERN) {
        display_text(120, HUD_Y + 4, "LNT", 0xFD20);
    }

    /* Compass indicator */
    if (player.quest_flags & QF_HAS_COMPASS) {
        display_text(170, HUD_Y + 4, "CMP", 0x07E0);
    }

    /* Screen name hint (shortened) */
    const char *screen_names[] = {
        "Forest NW","Forest N","Forest NE","Mansion","Lake W",
        "Forest W","Town NW","Town NE","Mansion E","Lake E",
        "Park","Town SW","Town SE","F.Path","F.Deep",
        "Cave Ent","Beach W","Beach E","Docks","Cove",
        "Cliffs","House 1F","House 2F","Town Hall","Shop",
        "Library","Cabin","Mansion H","Study","Basement",
        "Cave","Passages","Chamber","Lab"
    };
    if (player.current_screen < NUM_SCREENS) {
        display_text(220, HUD_Y + 4, screen_names[player.current_screen], 0xBDF7);
    }

    /* Separator line */
    display_hline(0, HUD_Y, SCREEN_W, 0x4A49);
}

/* ─── Title screen ────────────────────────────────────────────────────────── */
void render_title_screen(int selection) {
    display_clear(0x0000);

    /* Title text */
    display_text_large(60, 40, "GLASSES", 0xA01F);
    display_text_large(56, 70, "MYSTERY", 0x07FF);

    /* Subtitle */
    display_text(68, 110, "A town of secrets...", 0x7BEF);

    /* Menu options */
    uint16_t col_new  = (selection == 0) ? 0xFFFF : 0x7BEF;
    uint16_t col_load = (selection == 1) ? 0xFFFF : 0x7BEF;

    display_text(120, 150, "New Game", col_new);
    display_text(116, 170, "Continue", col_load);

    /* Selection arrow */
    int arrow_y = (selection == 0) ? 150 : 170;
    display_text(100, arrow_y, ">", 0xFFFF);

    display_text(70, 220, "AkiraOS - 2025", 0x39E7);
}

/* ─── Pause menu ──────────────────────────────────────────────────────────── */
int render_pause_menu(int selection) {
    /* Semi-transparent dark overlay */
    display_rect(60, 60, 200, 120, 0x1082);
    display_rect_outline(60, 60, 200, 120, 0x7BEF);

    display_text_large(110, 70, "PAUSED", 0xFFFF);

    const char *opts[] = {"Resume", "Inventory", "Save Game", "Exit"};
    for (int i = 0; i < 4; i++) {
        uint16_t c = (i == selection) ? 0xFFFF : 0x7BEF;
        display_text(120, 100 + i * 16, opts[i], c);
        if (i == selection)
            display_text(104, 100 + i * 16, ">", 0xFFFF);
    }
    return selection; /* caller handles input */
}

/* ─── Inventory screen ────────────────────────────────────────────────────── */
void render_inventory(void) {
    display_rect(20, 20, 280, 200, 0x1082);
    display_rect_outline(20, 20, 280, 200, 0x7BEF);

    display_text_large(100, 26, "ITEMS", 0xFFFF);

    int row = 0;
    for (int i = 1; i < NUM_ITEM_TYPES; i++) {
        if (player.inventory[i] > 0) {
            int y = 52 + row * 14;
            /* Draw item icon (8x8) */
            display_bitmap_transparent(30, y, 8, 8,
                item_sprites[i], 128, TRANSPARENT);
            /* Item name */
            display_text(44, y, item_names[i], 0xFFFF);
            /* Count */
            if (player.inventory[i] > 1) {
                char cbuf[8];
                cbuf[0] = 'x';
                itoa(player.inventory[i], cbuf + 1);
                display_text(200, y, cbuf, 0xBDF7);
            }
            row++;
        }
    }
    if (row == 0) {
        display_text(90, 100, "No items yet.", 0x7BEF);
    }

    display_text(80, 200, "Press B to close", 0x7BEF);
}

/* ─── Ending screen ───────────────────────────────────────────────────────── */
void render_ending(int ending_type, uint32_t frame) {
    display_clear(0x0000);

    const char *title = "";
    const char *line1 = "";
    const char *line2 = "";
    const char *line3 = "";
    uint16_t color = 0xFFFF;

    switch (ending_type) {
        case ENDING_SEAL:
            title = "THE SEAL";
            line1 = "You sealed the anomaly.";
            line2 = "Glasses returns to peace.";
            line3 = "But at what cost?";
            color = 0x07FF;
            break;
        case ENDING_POWER:
            title = "THE POWER";
            line1 = "You harnessed the anomaly.";
            line2 = "Unlimited power is yours.";
            line3 = "The town trembles...";
            color = 0xF800;
            break;
        case ENDING_BALANCE:
            title = "BALANCE";
            line1 = "You found harmony.";
            line2 = "Anomaly and town coexist.";
            line3 = "A new era begins.";
            color = 0x07E0;
            break;
    }

    /* Fade in effect using frame counter */
    if (frame < 60) {
        uint16_t fade = (uint16_t)((frame * 4) > 255 ? 255 : frame * 4);
        color = darken_color(color, (int)fade);
    }

    display_text_large(80, 50, title, color);
    display_text(60, 110, line1, darken_color(0xBDF7, frame < 90 ? (int)(frame * 3) : 255));
    display_text(50, 135, line2, darken_color(0xBDF7, frame < 120 ? (int)(frame * 2) : 255));
    display_text(60, 160, line3, darken_color(0xBDF7, frame < 150 ? (int)(frame * 2) : 255));

    if (frame > 180) {
        display_text(70, 210, "Press A to return", 0x7BEF);
    }
}

/* ─── Save / Load ─────────────────────────────────────────────────────────── */
typedef struct {
    uint32_t magic;
    uint8_t  screen;
    int16_t  px, py;
    uint8_t  direction;
    uint8_t  inventory[NUM_ITEM_TYPES];
    uint32_t quest_flags;
    uint8_t  diary_pages;
    uint8_t  ending;
} save_data_t;

int save_game(void) {
    save_data_t sd;
    sd.magic = SAVE_MAGIC;
    sd.screen = player.current_screen;
    sd.px = player.px;
    sd.py = player.py;
    sd.direction = player.direction;
    for (int i = 0; i < NUM_ITEM_TYPES; i++)
        sd.inventory[i] = player.inventory[i];
    sd.quest_flags = player.quest_flags;
    sd.diary_pages = player.diary_pages;
    sd.ending = player.ending;

    int fd = storage_open("save.dat", STORAGE_O_WRITE);
    if (fd < 0) return -1;
    int ret = storage_write(fd, &sd, sizeof(sd));
    storage_close(fd);
    return (ret == sizeof(sd)) ? 0 : -1;
}

int load_game(void) {
    save_data_t sd;
    int fd = storage_open("save.dat", STORAGE_O_READ);
    if (fd < 0) return -1;
    int ret = storage_read(fd, &sd, sizeof(sd));
    storage_close(fd);
    if (ret != sizeof(sd)) return -1;
    if (sd.magic != SAVE_MAGIC) return -1;

    player.current_screen = sd.screen;
    player.px = sd.px;
    player.py = sd.py;
    player.direction = sd.direction;
    for (int i = 0; i < NUM_ITEM_TYPES; i++)
        player.inventory[i] = sd.inventory[i];
    player.quest_flags = sd.quest_flags;
    player.diary_pages = sd.diary_pages;
    player.ending = sd.ending;
    return 0;
}
