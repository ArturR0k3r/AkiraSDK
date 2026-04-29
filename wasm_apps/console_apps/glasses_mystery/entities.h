/*
 * entities.h — Glasses Mystery: Player, NPC, and item types
 */
#ifndef ENTITIES_H
#define ENTITIES_H

#include "engine.h"

/* ─── Player ──────────────────────────────────────────────────────────────── */
typedef struct {
    int16_t  px, py;             /* pixel position on screen */
    uint8_t  direction;          /* DIR_DOWN/UP/LEFT/RIGHT */
    uint8_t  anim_frame;         /* 0 or 1 */
    uint8_t  anim_counter;       /* ticks until next frame swap */
    uint8_t  current_screen;     /* SCR_* id */
    uint8_t  inventory[NUM_ITEM_TYPES]; /* count per item type */
    uint32_t quest_flags;        /* bit-packed quest state */
    uint8_t  diary_pages;        /* 0–10 */
    uint8_t  ending;             /* ENDING_NONE until chosen */
    uint8_t  moving;             /* 1 while walking this frame */
} player_t;

/* ─── NPC runtime state ──────────────────────────────────────────────────── */
typedef struct {
    int16_t px, py;              /* pixel position */
    uint8_t direction;
    uint8_t anim_frame;
    uint8_t active;              /* visible on current screen */
    uint8_t npc_id;              /* NPC_* constant */
} npc_state_t;

/* ─── Global state — defined in entities.c ────────────────────────────────── */
extern player_t    player;
extern npc_state_t npc_states[NUM_NPCS];

/* ─── Functions ───────────────────────────────────────────────────────────── */
/* Initialization */
void player_init(void);

/* Per-frame update — handles movement, collision, animation */
void player_update(const input_t *inp);

/* Render player sprite */
void player_render(void);

/* Set up NPC states for the given screen */
void npcs_init_for_screen(int screen_id);

/* Render all active NPCs */
void npcs_render(void);

/* Check if player is facing an NPC and pressing A — returns NPC id or -1 */
int check_npc_interaction(const input_t *inp);

/* Check if player is on/facing an item tile — returns item_id or ITEM_NONE */
int check_item_interaction(const input_t *inp);

/* Give item to player (updates inventory + quest flags as needed) */
void player_give_item(int item_id);

/* Check if player has a specific item */
int player_has_item(int item_id);

#endif /* ENTITIES_H */
