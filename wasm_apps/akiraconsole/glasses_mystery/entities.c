/*
 * entities.c — Glasses Mystery: Player movement, NPC setup, interactions
 */
#include "engine.h"
#include "entities.h"
#include "world.h"
#include "dialogue.h"

/* ─── Globals ─────────────────────────────────────────────────────────────── */
player_t    player;
npc_state_t npc_states[NUM_NPCS];

/* Player speed (pixels per frame) */
#define PLAYER_SPEED 3
#define ANIM_INTERVAL 6  /* ticks between frame swaps */

/* ─── Helper: tile overlap check ──────────────────────────────────────────── */
static int px_to_tile(int px) { return px / TILE_SIZE; }

/* Check if a pixel-aligned box would collide with solid tiles */
static int check_collision(int px, int py) {
    /* Player hitbox: centered, smaller than sprite for forgiving collisions */
    int left   = px + 4;     /* 4px inset from left */
    int right  = px + 11;    /* 4px inset from right (16 - 5) */
    int top    = py + 8;     /* top half is head, walk through */
    int bottom = py + 15;    /* full height */

    /* Check all four corners of the hitbox */
    int tiles[4][2] = {
        { px_to_tile(left),  px_to_tile(top) },
        { px_to_tile(right), px_to_tile(top) },
        { px_to_tile(left),  px_to_tile(bottom) },
        { px_to_tile(right), px_to_tile(bottom) },
    };

    for (int i = 0; i < 4; i++) {
        int tile = world_get_tile(tiles[i][0], tiles[i][1]);
        if (tile_is_solid((uint8_t)tile))
            return 1;
    }
    return 0;
}

/* ─── Player init ─────────────────────────────────────────────────────────── */
void player_init(void) {
    player.px = 15 * TILE_SIZE;   /* start roughly center of town NW */
    player.py = 12 * TILE_SIZE;
    player.direction = DIR_DOWN;
    player.anim_frame = 0;
    player.anim_counter = 0;
    player.current_screen = SCR_TOWN_NW;
    player.quest_flags = 0;
    player.diary_pages = 0;
    player.ending = ENDING_NONE;
    player.moving = 0;
    for (int i = 0; i < NUM_ITEM_TYPES; i++)
        player.inventory[i] = 0;
}

/* ─── Player update ───────────────────────────────────────────────────────── */
void player_update(const input_t *inp) {
    int dx = 0, dy = 0;
    player.moving = 0;

    if (inp->held[INPUT_UP])    { dy = -PLAYER_SPEED; player.direction = DIR_UP; }
    if (inp->held[INPUT_DOWN])  { dy =  PLAYER_SPEED; player.direction = DIR_DOWN; }
    if (inp->held[INPUT_LEFT])  { dx = -PLAYER_SPEED; player.direction = DIR_LEFT; }
    if (inp->held[INPUT_RIGHT]) { dx =  PLAYER_SPEED; player.direction = DIR_RIGHT; }

    /* Try X movement — allow walking past screen edges for transitions */
    if (dx != 0) {
        int new_px = player.px + dx;
        /* Allow going 1 step past screen edges (transitions catch this) */
        if (new_px < -PLAYER_SPEED || new_px > (TILES_X - 1) * TILE_SIZE) {
            /* too far, clamp */
        } else if (new_px < 0 || new_px >= (TILES_X - 2) * TILE_SIZE) {
            /* at screen edge — allow movement, skip collision */
            player.px = (int16_t)new_px;
            player.moving = 1;
        } else if (!check_collision(new_px, player.py)) {
            player.px = (int16_t)new_px;
            player.moving = 1;
        }
    }

    /* Try Y movement */
    if (dy != 0) {
        int new_py = player.py + dy;
        if (new_py < -PLAYER_SPEED || new_py > (MAP_TILES_Y - 1) * TILE_SIZE) {
            /* too far */
        } else if (new_py < 0 || new_py >= (MAP_TILES_Y - 2) * TILE_SIZE) {
            /* at screen edge — allow movement, skip collision */
            player.py = (int16_t)new_py;
            player.moving = 1;
        } else if (!check_collision(player.px, new_py)) {
            player.py = (int16_t)new_py;
            player.moving = 1;
        }
    }

    /* Animation */
    if (player.moving) {
        player.anim_counter++;
        if (player.anim_counter >= ANIM_INTERVAL) {
            player.anim_counter = 0;
            player.anim_frame ^= 1;
        }
    } else {
        player.anim_frame = 0;
        player.anim_counter = 0;
    }
}

/* ─── Player render ───────────────────────────────────────────────────────── */
void player_render(void) {
    render_player_sprite(player.px, player.py, player.direction, player.anim_frame);
}

/* ─── NPC init for screen ─────────────────────────────────────────────────── */
void npcs_init_for_screen(int screen_id) {
    /* Clear all NPC states */
    for (int i = 0; i < NUM_NPCS; i++) {
        npc_states[i].active = 0;
        npc_states[i].anim_frame = 0;
    }

    int count = 0;
    const npc_spawn_t *spawns = world_get_npc_spawns(&count);
    if (!spawns) return;

    for (int i = 0; i < count; i++) {
        const npc_spawn_t *sp = &spawns[i];

        /* Check require flag: NPC appears only if flag is set (or 0 = always) */
        if (sp->require_flag != 0 && !(player.quest_flags & sp->require_flag))
            continue;

        /* Check hide flag: NPC hidden if flag is set */
        if (sp->hide_flag != 0 && (player.quest_flags & sp->hide_flag))
            continue;

        int id = sp->npc_id;
        if (id >= 0 && id < NUM_NPCS) {
            npc_states[id].active = 1;
            npc_states[id].px = sp->tile_x * TILE_SIZE;
            npc_states[id].py = sp->tile_y * TILE_SIZE;
            npc_states[id].direction = sp->direction;
            npc_states[id].npc_id = (uint8_t)id;
        }
    }
}

/* ─── NPC render — called from main loop ──────────────────────────────────── */
void npcs_render(void) {
    for (int i = 0; i < NUM_NPCS; i++) {
        if (!npc_states[i].active) continue;
        render_npc_sprite(npc_states[i].px, npc_states[i].py,
                          npc_states[i].npc_id, npc_states[i].anim_frame);
    }
}

/* ─── NPC interaction check ───────────────────────────────────────────────── */
int check_npc_interaction(const input_t *inp) {
    if (!inp->pressed[INPUT_A]) return -1;

    /* Calculate the tile the player is facing */
    int face_tx = px_to_tile(player.px + 8);
    int face_ty = px_to_tile(player.py + 8);

    switch (player.direction) {
        case DIR_UP:    face_ty -= 1; break;
        case DIR_DOWN:  face_ty += 2; break;
        case DIR_LEFT:  face_tx -= 1; break;
        case DIR_RIGHT: face_tx += 2; break;
    }

    /* Check each active NPC */
    for (int i = 0; i < NUM_NPCS; i++) {
        if (!npc_states[i].active) continue;
        int npc_tx = npc_states[i].px / TILE_SIZE;
        int npc_ty = npc_states[i].py / TILE_SIZE;

        /* NPC occupies a 2x2 tile area (16x16 px) */
        if (face_tx >= npc_tx && face_tx <= npc_tx + 1 &&
            face_ty >= npc_ty && face_ty <= npc_ty + 1) {
            return i;
        }
    }
    return -1;
}

/* ─── Item interaction check ──────────────────────────────────────────────── */
int check_item_interaction(const input_t *inp) {
    if (!inp->pressed[INPUT_A]) return ITEM_NONE;

    /* Check tile the player is facing */
    int face_tx = px_to_tile(player.px + 8);
    int face_ty = px_to_tile(player.py + 8);

    switch (player.direction) {
        case DIR_UP:    face_ty -= 1; break;
        case DIR_DOWN:  face_ty += 2; break;
        case DIR_LEFT:  face_tx -= 1; break;
        case DIR_RIGHT: face_tx += 2; break;
    }

    /* Also check tile player is standing on */
    int stand_tx = px_to_tile(player.px + 8);
    int stand_ty = px_to_tile(player.py + 12);

    /* Check item spawns on current screen */
    int count = 0;
    const item_spawn_t *items = world_get_item_spawns(&count);
    if (!items) return ITEM_NONE;

    for (int i = 0; i < count; i++) {
        /* Skip already-collected items */
        if (items[i].hide_flag != 0 && (player.quest_flags & items[i].hide_flag))
            continue;

        int ix = items[i].tile_x;
        int iy = items[i].tile_y;

        if ((face_tx == ix && face_ty == iy) ||
            (stand_tx == ix && stand_ty == iy)) {
            return items[i].item_id;
        }
    }

    /* Check interactive tiles (chests, signs, bookshelves) */
    int tile = world_get_tile(face_tx, face_ty);
    uint8_t prop = tile_get_property((uint8_t)tile);
    if (prop & TILE_INTERACTIVE) {
        if (tile == T_CHEST_CLOSED) {
            /* Replace with open chest visually */
            if (face_tx >= 0 && face_tx < TILES_X && face_ty >= 0 && face_ty < MAP_TILES_Y)
                current_tiles[face_ty * TILES_X + face_tx] = T_CHEST_OPEN;
            return -2; /* signals "chest opened" to caller */
        }
        if (tile == T_SIGN) return -3; /* sign interaction */
        if (tile == T_BOOKSHELF) return -4; /* bookshelf */
    }

    return ITEM_NONE;
}

/* ─── Give item to player ─────────────────────────────────────────────────── */
void player_give_item(int item_id) {
    if (item_id < 0 || item_id >= NUM_ITEM_TYPES) return;
    player.inventory[item_id]++;

    /* Set corresponding quest flags */
    switch (item_id) {
        case ITEM_DIARY:        player.quest_flags |= QF_HAS_DIARY; break;
        case ITEM_LANTERN:      player.quest_flags |= QF_HAS_LANTERN; break;
        case ITEM_COMPASS:      player.quest_flags |= QF_HAS_COMPASS; break;
        case ITEM_MANSION_KEY:  player.quest_flags |= QF_HAS_MANSION_KEY; break;
        case ITEM_LAB_KEYCARD:  player.quest_flags |= QF_HAS_KEYCARD; break;
        case ITEM_CRYSTAL_1:    player.quest_flags |= QF_CRYSTAL_1; break;
        case ITEM_CRYSTAL_2:    player.quest_flags |= QF_CRYSTAL_2; break;
        case ITEM_CRYSTAL_3:    player.quest_flags |= QF_CRYSTAL_3; break;
        case ITEM_DIARY_PAGE:   player.diary_pages++; break;
        case ITEM_TACKLE_BOX:   player.quest_flags |= QF_PETE_QUEST_DONE; break;
    }
}

/* ─── Check if player has item ────────────────────────────────────────────── */
int player_has_item(int item_id) {
    if (item_id < 0 || item_id >= NUM_ITEM_TYPES) return 0;
    return player.inventory[item_id] > 0;
}
