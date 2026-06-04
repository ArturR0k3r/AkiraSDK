/*
 * main.c — Glasses Mystery: Game loop entry point
 *
 * State machine: TITLE → PLAYING ↔ DIALOGUE / INVENTORY / PAUSE → ENDING
 */
#include "engine.h"
#include "entities.h"
#include "world.h"
#include "dialogue.h"

/* ─── Display dimensions ──────────────────────────────────────────────────── */
int32_t g_screen_w = 320, g_screen_h = 240;

/* ─── Game state ──────────────────────────────────────────────────────────── */
static game_state_t state = STATE_TITLE;
static input_t      inp;
static int          title_sel    = 0;
static int          pause_sel    = 0;
static uint32_t     frame_counter = 0;
static uint32_t     ending_frame  = 0;
static int          has_save     = 0;

/* ─── Frame pacing ── 30 fps ≈ 33333 µs per frame ────────────────────────── */
#define FRAME_US 20000  /* ~50 fps */

/* ─── Screen transition helper ────────────────────────────────────────────── */
static void goto_screen(int screen_id, int spawn_tx, int spawn_ty) {
    world_load_screen(screen_id);
    player.current_screen = (uint8_t)screen_id;
    player.px = (int16_t)(spawn_tx * TILE_SIZE);
    player.py = (int16_t)(spawn_ty * TILE_SIZE);
    npcs_init_for_screen(screen_id);
}

/* ─── Check screen-edge transitions ──────────────────────────────────────── */
static int check_screen_transition(void) {
    int dir = -1;
    int new_tx = -1, new_ty = -1;

    if (player.px <= 0) {
        dir = DIR_LEFT;
        new_tx = TILES_X - 3;
        new_ty = player.py / TILE_SIZE;
    } else if (player.px >= (TILES_X - 2) * TILE_SIZE) {
        dir = DIR_RIGHT;
        new_tx = 1;
        new_ty = player.py / TILE_SIZE;
    } else if (player.py <= 0) {
        dir = DIR_UP;
        new_tx = player.px / TILE_SIZE;
        new_ty = MAP_TILES_Y - 3;
    } else if (player.py >= (MAP_TILES_Y - 2) * TILE_SIZE) {
        dir = DIR_DOWN;
        new_tx = player.px / TILE_SIZE;
        new_ty = 1;
    }

    if (dir < 0) return 0;

    int next = screen_get_connection(player.current_screen, dir);
    if (next < 0) {
        /* No connection — clamp player */
        if (dir == DIR_LEFT)  player.px = 0;
        if (dir == DIR_RIGHT) player.px = (int16_t)((TILES_X - 2) * TILE_SIZE);
        if (dir == DIR_UP)    player.py = 0;
        if (dir == DIR_DOWN)  player.py = (int16_t)((MAP_TILES_Y - 2) * TILE_SIZE);
        return 0;
    }

    goto_screen(next, new_tx, new_ty);
    return 1;
}

/* ─── Check door transitions ─────────────────────────────────────────────── */
static int check_door_transition(const input_t *in) {
    if (!in->pressed[INPUT_A]) return 0;

    /* Tile player is standing on / facing */
    int stand_tx = (player.px + 8) / TILE_SIZE;
    int stand_ty = (player.py + 12) / TILE_SIZE;

    int face_tx = stand_tx, face_ty = stand_ty;
    switch (player.direction) {
        case DIR_UP:    face_ty--; break;
        case DIR_DOWN:  face_ty++; break;
        case DIR_LEFT:  face_tx--; break;
        case DIR_RIGHT: face_tx++; break;
    }

    /* Check facing tile and standing tile */
    const door_t *door = world_find_door(face_tx, face_ty);
    if (!door) door = world_find_door(stand_tx, stand_ty);
    if (!door) return 0;

    /* Check requirements */
    if (door->require_flag && !(player.quest_flags & door->require_flag)) {
        /* Show "need key" or "need lantern" dialogue */
        if (door->require_item == ITEM_MANSION_KEY)
            dialogue_start(DLG_LOCKED_DOOR);
        else
            dialogue_start(DLG_NEED_LANTERN);
        state = STATE_DIALOGUE;
        return 1;
    }
    if (door->require_item != ITEM_NONE && !player_has_item(door->require_item)) {
        dialogue_start(DLG_LOCKED_DOOR);
        state = STATE_DIALOGUE;
        return 1;
    }

    goto_screen(door->target_screen, door->spawn_x, door->spawn_y);
    return 1;
}

/* ─── Render items on ground ──────────────────────────────────────────────── */
static void render_ground_items(void) {
    int count = 0;
    const item_spawn_t *items = world_get_item_spawns(&count);
    if (!items) return;

    for (int i = 0; i < count; i++) {
        /* Skip collected items */
        if (items[i].hide_flag != 0 && (player.quest_flags & items[i].hide_flag))
            continue;

        int px = items[i].tile_x * TILE_SIZE;
        int py = items[i].tile_y * TILE_SIZE;

        /* Simple blinking effect */
        if ((frame_counter & 0x10) || items[i].item_id <= ITEM_DIARY) {
            /* Draw a small colored square as item indicator */
            uint16_t color = 0xFFE0; /* yellow default */
            if (items[i].item_id >= ITEM_CRYSTAL_1 && items[i].item_id <= ITEM_CRYSTAL_3)
                color = 0xA01F; /* purple for crystals */
            display_rect(px + 2, py + 2, 4, 4, color);
            display_rect_outline(px + 1, py + 1, 6, 6, 0xFFFF);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  MAIN ENTRY
 * ═══════════════════════════════════════════════════════════════════════════ */
int main(void) {
    display_get_size(&g_screen_w, &g_screen_h);

    /* Timer for frame pacing */
    int timer = timer_create();

    /* Init input */
    input_init(&inp);

    /* Init player */
    player_init();

    /* Try loading save to check if save exists */
    has_save = (load_game() == 0) ? 1 : 0;
    /* Reset player if no save — we just wanted to check */
    if (!has_save) player_init();

    /* Start on title screen */
    state = STATE_TITLE;

    /* ─── Main loop ───────────────────────────────────────────────────────── */
    while (1) {
        timer_start(timer);
        input_poll(&inp);
        frame_counter++;

        switch (state) {

        /* ── TITLE SCREEN ─────────────────────────────────────────────────── */
        case STATE_TITLE:
            render_title_screen(title_sel);

            if (inp.pressed[INPUT_UP] && title_sel > 0) title_sel--;
            if (inp.pressed[INPUT_DOWN] && title_sel < 1) title_sel++;

            if (inp.pressed[INPUT_A]) {
                if (title_sel == 0) {
                    /* New Game */
                    player_init();
                    world_load_screen(SCR_TOWN_NW);
                    npcs_init_for_screen(SCR_TOWN_NW);
                    state = STATE_PLAYING;
                } else if (title_sel == 1 && has_save) {
                    /* Continue */
                    if (load_game() == 0) {
                        world_load_screen(player.current_screen);
                        npcs_init_for_screen(player.current_screen);
                        state = STATE_PLAYING;
                    }
                }
            }
            break;

        /* ── PLAYING ──────────────────────────────────────────────────────── */
        case STATE_PLAYING:
            /* Update player movement */
            player_update(&inp);

            /* Check screen edge transitions */
            check_screen_transition();

            /* Check door transitions (A button) */
            if (check_door_transition(&inp)) break;

            /* Check NPC interaction */
            {
                int npc = check_npc_interaction(&inp);
                if (npc >= 0) {
                    int dlg_id = get_npc_dialogue_id(npc, player.quest_flags);
                    dialogue_start(dlg_id);
                    state = STATE_DIALOGUE;
                    break;
                }
            }

            /* Check item interaction */
            {
                int item = check_item_interaction(&inp);
                if (item > 0 && item < NUM_ITEM_TYPES) {
                    player_give_item(item);
                    /* Show appropriate dialogue */
                    if (item == ITEM_DIARY) dialogue_start(DLG_DIARY_FIND);
                    else if (item == ITEM_LANTERN) dialogue_start(DLG_LANTERN_FIND);
                    else if (item >= ITEM_CRYSTAL_1 && item <= ITEM_CRYSTAL_3)
                        dialogue_start(DLG_CHEST_CRYSTAL);
                    else dialogue_start(DLG_CHEST_CRYSTAL); /* generic pickup */
                    state = STATE_DIALOGUE;
                    break;
                } else if (item == -2) {
                    /* Chest opened */
                    dialogue_start(DLG_CHEST_CRYSTAL);
                    state = STATE_DIALOGUE;
                    break;
                } else if (item == -3) {
                    /* Sign */
                    if (player.current_screen <= SCR_TOWN_NE)
                        dialogue_start(DLG_SIGN_TOWN);
                    else
                        dialogue_start(DLG_SIGN_FOREST);
                    state = STATE_DIALOGUE;
                    break;
                }
            }

            /* Settings button → Pause */
            if (inp.pressed[INPUT_SETTINGS]) {
                pause_sel = 0;
                state = STATE_PAUSE;
                break;
            }

            /* ─── Render ──────────────────────────────────────────────────── */
            render_screen(current_tiles);

            /* Render anomaly effects on anomaly tiles */
            for (int ty = 0; ty < MAP_TILES_Y; ty++) {
                for (int tx = 0; tx < TILES_X; tx++) {
                    uint8_t tid = current_tiles[ty * TILES_X + tx];
                    if (tid == T_ANOMALY_GROUND || tid == T_GLOW_TILE || tid == T_PORTAL)
                        render_anomaly_effect(tx, ty, frame_counter);
                }
            }

            /* Render ground items */
            render_ground_items();

            /* Render NPCs */
            npcs_render();

            /* Render player */
            player_render();

            /* Render HUD */
            render_hud();
            break;

        /* ── DIALOGUE ─────────────────────────────────────────────────────── */
        case STATE_DIALOGUE:
            /* Keep game scene visible behind dialogue */
            render_screen(current_tiles);
            render_ground_items();
            npcs_render();
            player_render();
            render_hud();

            /* Dialogue update + render */
            dialogue_update(&inp);
            dialogue_render();

            if (!dialogue_is_active()) {
                /* Check if an ending was triggered */
                if (player.ending != ENDING_NONE) {
                    ending_frame = 0;
                    state = STATE_ENDING;
                } else {
                    state = STATE_PLAYING;
                }
            }
            break;

        /* ── INVENTORY ────────────────────────────────────────────────────── */
        case STATE_INVENTORY:
            render_screen(current_tiles);
            npcs_render();
            player_render();
            render_hud();
            render_inventory();

            if (inp.pressed[INPUT_B] || inp.pressed[INPUT_SETTINGS]) {
                state = STATE_PLAYING;
            }
            break;

        /* ── PAUSED ───────────────────────────────────────────────────────── */
        case STATE_PAUSE:
            render_screen(current_tiles);
            npcs_render();
            player_render();
            render_hud();
            render_pause_menu(pause_sel);

            if (inp.pressed[INPUT_UP] && pause_sel > 0) pause_sel--;
            if (inp.pressed[INPUT_DOWN] && pause_sel < 3) pause_sel++;

            if (inp.pressed[INPUT_A]) {
                switch (pause_sel) {
                case 0: /* Resume */
                    state = STATE_PLAYING;
                    break;
                case 1: /* Inventory */
                    state = STATE_INVENTORY;
                    break;
                case 2: /* Save */
                    if (save_game() == 0) {
                        has_save = 1;
                        /* Brief "Saved!" flash */
                        display_text(120, 120, "SAVED!", 0x07E0);
                        display_flush();
                        delay(500000); /* 0.5s */
                    }
                    state = STATE_PLAYING;
                    break;
                case 3: /* Exit to supervisor */
                    app_switch("supervisor");
                    return 0;
                }
            }

            if (inp.pressed[INPUT_B] || inp.pressed[INPUT_SETTINGS]) {
                state = STATE_PLAYING;
            }
            break;

        /* ── ENDING ───────────────────────────────────────────────────────── */
        case STATE_ENDING:
            render_ending(player.ending, ending_frame);
            ending_frame++;

            if (inp.pressed[INPUT_A] && ending_frame > 180) {
                state = STATE_TITLE;
                title_sel = 0;
                player_init();
            }
            break;

        /* ── TRANSITION (unused, handled inline) ──────────────────────────── */
        case STATE_TRANSITION:
            state = STATE_PLAYING;
            break;
        }

        /* Flush display */
        display_flush();

        /* Frame pacing: wait until FRAME_US elapsed, yielding CPU */
        {
            int elapsed = timer_elapsed(timer);
            if (elapsed < FRAME_US) {
                delay(FRAME_US - elapsed);
            }
        }
    }

    timer_free(timer);
    return 0;
}
