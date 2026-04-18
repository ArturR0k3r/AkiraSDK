/*
 * dialogue.h — Glasses Mystery: Dialogue system types and all game text
 */
#ifndef DIALOGUE_H
#define DIALOGUE_H

#include "engine.h"

/* ─── Dialogue line ───────────────────────────────────────────────────────── */
typedef struct {
    const char *speaker;         /* NPC name (NULL = narrator/item) */
    const char *text;            /* dialogue text */
    int16_t next_id;             /* next line: >=0 = line id, -1 = end, -2 = show choices */
    uint32_t set_flag;           /* quest flag to set when shown (0=none) */
    uint8_t  give_item;          /* item to give (ITEM_NONE=none) */
} dialogue_line_t;

/* ─── Choice option ───────────────────────────────────────────────────────── */
typedef struct {
    const char *text;
    int16_t next_dialogue_id;    /* jump to this line after choosing */
    uint32_t set_flag;           /* flag to set on choice */
} choice_option_t;

/* ─── Dialogue runtime state ─────────────────────────────────────────────── */
typedef struct {
    int16_t current_line;        /* current dialogue_line index */
    uint8_t active;              /* 1 = dialogue box visible */
    uint8_t char_pos;            /* typewriter position */
    uint8_t tick;                /* animation tick */
    uint8_t in_choice;           /* 1 = showing choices */
    uint8_t choice_sel;          /* selected choice index */
    uint8_t choice_count;        /* number of choices available */
    const choice_option_t *choices; /* pointer to current choice set */
} dialogue_state_t;

/* ─── Dialogue line ID constants ──────────────────────────────────────────── */
#define DLG_DIARY_FIND          0
#define DLG_MAYOR_FIRST         2
#define DLG_MAYOR_AFTER_DIARY   5
#define DLG_WHITMORE_FIRST      9
#define DLG_WHITMORE_GIVES_KEY  11
#define DLG_OLD_MAN_FIRST       14
#define DLG_ELARA_FIRST         18
#define DLG_GHOST_FIRST         21
#define DLG_ANOMALY_CHOICE      24
#define DLG_PETE_QUEST_START    26
#define DLG_PETE_QUEST_DONE     27
#define DLG_LIBRARY_QUEST_START 29
#define DLG_LIBRARY_QUEST_DONE  30
#define DLG_TOMMY_QUEST_START   31
#define DLG_TOMMY_QUEST_DONE    32
#define DLG_ADA_GENERIC         34
#define DLG_PETE_GENERIC        36
#define DLG_TOMMY_GENERIC       37
#define DLG_MAYOR_GENERIC       38
#define DLG_WHITMORE_GENERIC    39
#define DLG_OLD_MAN_GENERIC     40
#define DLG_ELARA_GENERIC       41
#define DLG_GHOST_GENERIC       42
#define DLG_ELARA_QUEST_START   43
#define DLG_ELARA_QUEST_DONE    44
#define DLG_ENDING_SEAL         45
#define DLG_ENDING_POWER        49
#define DLG_ENDING_BALANCE      53
#define DLG_CHEST_CRYSTAL       57
#define DLG_LANTERN_FIND        58
#define DLG_SIGN_TOWN           59
#define DLG_SIGN_FOREST         60
#define DLG_SIGN_BEACH          61
#define DLG_LOCKED_DOOR         62
#define DLG_NEED_LANTERN        63
#define DLG_TOTAL               64

/* ─── Globals — defined in dialogue.c ─────────────────────────────────────── */
extern dialogue_state_t dlg;
extern const dialogue_line_t all_dialogues[DLG_TOTAL];

/* ─── Ending choice options ───────────────────────────────────────────────── */
extern const choice_option_t ending_choices[3];

/* ─── Functions ───────────────────────────────────────────────────────────── */
void dialogue_start(int line_id);
void dialogue_update(const input_t *inp);
void dialogue_render(void);
int  dialogue_is_active(void);

/* Pick the right dialogue ID for an NPC based on current quest state */
int get_npc_dialogue_id(int npc_id, uint32_t quest_flags);

#endif /* DIALOGUE_H */
