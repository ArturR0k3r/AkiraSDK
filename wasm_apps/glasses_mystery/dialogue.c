/*
 * dialogue.c — Glasses Mystery: All game text and dialogue system
 */
#include "engine.h"
#include "entities.h"
#include "dialogue.h"

/* ─── Dialogue runtime state ──────────────────────────────────────────────── */
dialogue_state_t dlg;

/* ─── Ending choices ──────────────────────────────────────────────────────── */
const choice_option_t ending_choices[3] = {
    { "Seal the anomaly",  DLG_ENDING_SEAL,    0 },
    { "Absorb its power",  DLG_ENDING_POWER,   0 },
    { "Seek balance",      DLG_ENDING_BALANCE, 0 },
};

/* ─── All dialogue lines ─ 64 entries ─────────────────────────────────────── */
const dialogue_line_t all_dialogues[DLG_TOTAL] = {
    /* 0 DLG_DIARY_FIND */
    { 0, "You found an old diary...", 1, QF_HAS_DIARY, ITEM_DIARY },
    /* 1 */
    { 0, "Entries mention strange events in Glasses.", -1, 0, ITEM_NONE },

    /* 2 DLG_MAYOR_FIRST */
    { "Mayor", "Welcome to Glasses! I'm the Mayor.", 3, QF_MAYOR_MET, ITEM_NONE },
    /* 3 */
    { "Mayor", "This town has... peculiarities.", 4, 0, ITEM_NONE },
    /* 4 */
    { "Mayor", "Talk to the townsfolk. Be careful.", -1, 0, ITEM_NONE },

    /* 5 DLG_MAYOR_AFTER_DIARY */
    { "Mayor", "You found a diary? Interesting...", 6, 0, ITEM_NONE },
    /* 6 */
    { "Mayor", "The original founder wrote about anomalies.", 7, 0, ITEM_NONE },
    /* 7 */
    { "Mayor", "Visit Whitmore at the library.", 8, 0, ITEM_NONE },
    /* 8 */
    { "Mayor", "He knows more about this town's history.", -1, QF_WHITMORE_QUEST, ITEM_NONE },

    /* 9 DLG_WHITMORE_FIRST */
    { "Whitmore", "Ah, a curious soul! I study anomalies.", 10, QF_WHITMORE_MET, ITEM_NONE },
    /* 10 */
    { "Whitmore", "The crystals are key to understanding.", -1, 0, ITEM_NONE },

    /* 11 DLG_WHITMORE_GIVES_KEY */
    { "Whitmore", "You've proven yourself trustworthy.", 12, 0, ITEM_NONE },
    /* 12 */
    { "Whitmore", "Take this key to the mansion.", 13, 0, ITEM_MANSION_KEY },
    /* 13 */
    { "Whitmore", "Answers await within.", -1, QF_HAS_MANSION_KEY, ITEM_NONE },

    /* 14 DLG_OLD_MAN_FIRST */
    { "Old Man", "The forest watches, child...", 15, QF_OLD_MAN_MET, ITEM_NONE },
    /* 15 */
    { "Old Man", "I've lived here since before the anomalies.", 16, 0, ITEM_NONE },
    /* 16 */
    { "Old Man", "Find the lantern. You'll need it.", 17, 0, ITEM_NONE },
    /* 17 */
    { "Old Man", "Dark places hide dark truths.", -1, 0, ITEM_NONE },

    /* 18 DLG_ELARA_FIRST */
    { "Elara", "Finally! Someone else who senses it!", 19, QF_ELARA_MET, ITEM_NONE },
    /* 19 */
    { "Elara", "I've been studying the energy readings.", 20, 0, ITEM_NONE },
    /* 20 */
    { "Elara", "Three crystals resonate with the anomaly.", -1, 0, ITEM_NONE },

    /* 21 DLG_GHOST_FIRST */
    { "???", "You can... see me?", 22, QF_GHOST_MET, ITEM_NONE },
    /* 22 */
    { "Ghost", "I am what remains of the first researcher.", 23, 0, ITEM_NONE },
    /* 23 */
    { "Ghost", "The portal must be dealt with. Choose wisely.", -2, 0, ITEM_NONE },

    /* 24 DLG_ANOMALY_CHOICE — this line shown before choices */
    { 0, "The portal pulses with energy...", 25, 0, ITEM_NONE },
    /* 25 */
    { 0, "What will you do?", -2, 0, ITEM_NONE },

    /* 26 DLG_PETE_QUEST_START */
    { "Pete", "Hey! I lost my tackle box at the beach.", -1, QF_PETE_QUEST, ITEM_NONE },

    /* 27 DLG_PETE_QUEST_DONE */
    { "Pete", "You found it! Thanks a bunch!", 28, 0, ITEM_NONE },
    /* 28 */
    { "Pete", "Here, take this. Found it fishing.", -1, QF_PETE_QUEST_DONE, ITEM_DIARY_PAGE },

    /* 29 DLG_LIBRARY_QUEST_START */
    { "Whitmore", "Could you find a missing book? Check the cabin.", -1, QF_LIBRARY_QUEST, ITEM_NONE },

    /* 30 DLG_LIBRARY_QUEST_DONE */
    { "Whitmore", "Excellent! This book confirms my theories.", -1, QF_LIBRARY_QUEST_DONE, ITEM_NONE },

    /* 31 DLG_TOMMY_QUEST_START */
    { "Tommy", "Hey mister! I saw a weird glowing thing!", 32, QF_TOMMY_QUEST, ITEM_NONE },

    /* 32 DLG_TOMMY_QUEST_DONE - reusing as generic response */
    { "Tommy", "It was near the old cabin! So cool!", -1, 0, ITEM_NONE },

    /* 33 — filler */
    { 0, "...", -1, 0, ITEM_NONE },

    /* 34 DLG_ADA_GENERIC */
    { "Ada", "Need supplies? I've got what you need.", 35, 0, ITEM_NONE },
    /* 35 */
    { "Ada", "Be safe out there!", -1, 0, ITEM_NONE },

    /* 36 DLG_PETE_GENERIC */
    { "Pete", "The fish aren't biting today...", -1, 0, ITEM_NONE },

    /* 37 DLG_TOMMY_GENERIC */
    { "Tommy", "This town is so boring! Nothing happens!", -1, 0, ITEM_NONE },

    /* 38 DLG_MAYOR_GENERIC */
    { "Mayor", "Everything is under control. Mostly.", -1, 0, ITEM_NONE },

    /* 39 DLG_WHITMORE_GENERIC */
    { "Whitmore", "So much to learn, so little time...", -1, 0, ITEM_NONE },

    /* 40 DLG_OLD_MAN_GENERIC */
    { "Old Man", "The forest speaks to those who listen.", -1, 0, ITEM_NONE },

    /* 41 DLG_ELARA_GENERIC */
    { "Elara", "The readings are off the charts!", -1, 0, ITEM_NONE },

    /* 42 DLG_GHOST_GENERIC */
    { "Ghost", "Time is running out...", -1, 0, ITEM_NONE },

    /* 43 DLG_ELARA_QUEST_START */
    { "Elara", "I need crystal samples from the cave!", -1, QF_ELARA_QUEST, ITEM_NONE },

    /* 44 DLG_ELARA_QUEST_DONE */
    { "Elara", "These readings are incredible! Thank you!", -1, QF_ELARA_QUEST_DONE, ITEM_NONE },

    /* 45 DLG_ENDING_SEAL */
    { 0, "You channel the crystals to seal the portal.", 46, 0, ITEM_NONE },
    /* 46 */
    { 0, "The anomaly collapses in on itself.", 47, 0, ITEM_NONE },
    /* 47 */
    { 0, "Glasses returns to quiet normalcy.", 48, 0, ITEM_NONE },
    /* 48 */
    { 0, "ENDING: The Sealed Gate", -1, 0, ITEM_NONE },

    /* 49 DLG_ENDING_POWER */
    { 0, "You absorb the anomaly's power!", 50, 0, ITEM_NONE },
    /* 50 */
    { 0, "Energy surges through your veins.", 51, 0, ITEM_NONE },
    /* 51 */
    { 0, "But at what cost?", 52, 0, ITEM_NONE },
    /* 52 */
    { 0, "ENDING: The New Anomaly", -1, 0, ITEM_NONE },

    /* 53 DLG_ENDING_BALANCE */
    { 0, "You harmonize with the anomaly.", 54, 0, ITEM_NONE },
    /* 54 */
    { 0, "A bridge between worlds forms.", 55, 0, ITEM_NONE },
    /* 55 */
    { 0, "Glasses becomes a place of wonder.", 56, 0, ITEM_NONE },
    /* 56 */
    { 0, "ENDING: The Bridge Between", -1, 0, ITEM_NONE },

    /* 57 DLG_CHEST_CRYSTAL */
    { 0, "Found a glowing crystal inside!", -1, 0, ITEM_NONE },

    /* 58 DLG_LANTERN_FIND */
    { 0, "You found an old lantern! It still works.", -1, QF_HAS_LANTERN, ITEM_LANTERN },

    /* 59 DLG_SIGN_TOWN */
    { 0, "Welcome to Glasses! Pop. 42", -1, 0, ITEM_NONE },

    /* 60 DLG_SIGN_FOREST */
    { 0, "DANGER: Strange activity reported ahead.", -1, 0, ITEM_NONE },

    /* 61 DLG_SIGN_BEACH */
    { 0, "Glasses Beach - No Swimming After Dark", -1, 0, ITEM_NONE },

    /* 62 DLG_LOCKED_DOOR */
    { 0, "The door is locked. You need a key.", -1, 0, ITEM_NONE },

    /* 63 DLG_NEED_LANTERN */
    { 0, "It's too dark to go further without a light.", -1, 0, ITEM_NONE },
};

/* ═══════════════════════════════════════════════════════════════════════════
 *  Dialogue system functions
 * ═══════════════════════════════════════════════════════════════════════════ */

void dialogue_start(int line_id) {
    if (line_id < 0 || line_id >= DLG_TOTAL) return;
    dlg.current_line = (int16_t)line_id;
    dlg.active = 1;
    dlg.char_pos = 0;
    dlg.tick = 0;
    dlg.in_choice = 0;
    dlg.choice_sel = 0;
    dlg.choice_count = 0;
    dlg.choices = 0;

    /* Apply immediate effects */
    const dialogue_line_t *line = &all_dialogues[line_id];
    if (line->set_flag)
        player.quest_flags |= line->set_flag;
    if (line->give_item != ITEM_NONE)
        player_give_item(line->give_item);
}

void dialogue_update(const input_t *inp) {
    if (!dlg.active) return;

    const dialogue_line_t *line = &all_dialogues[dlg.current_line];

    /* Typewriter effect */
    dlg.tick++;
    if (dlg.tick >= 2) {
        dlg.tick = 0;
        /* Count text length manually (no strlen) */
        int len = 0;
        const char *p = line->text;
        while (p[len]) len++;

        if (dlg.char_pos < len) {
            dlg.char_pos++;
        }
    }

    if (dlg.in_choice) {
        /* Navigate choices */
        if (inp->pressed[INPUT_UP] && dlg.choice_sel > 0)
            dlg.choice_sel--;
        if (inp->pressed[INPUT_DOWN] && dlg.choice_sel < dlg.choice_count - 1)
            dlg.choice_sel++;

        if (inp->pressed[INPUT_A]) {
            const choice_option_t *ch = &dlg.choices[dlg.choice_sel];
            if (ch->set_flag)
                player.quest_flags |= ch->set_flag;

            /* Set ending based on choice */
            if (dlg.choice_sel == 0) player.ending = ENDING_SEAL;
            else if (dlg.choice_sel == 1) player.ending = ENDING_POWER;
            else player.ending = ENDING_BALANCE;

            /* Jump to chosen dialogue line */
            dlg.in_choice = 0;
            dlg.current_line = ch->next_dialogue_id;
            dlg.char_pos = 0;
            dlg.tick = 0;

            const dialogue_line_t *next = &all_dialogues[dlg.current_line];
            if (next->set_flag)
                player.quest_flags |= next->set_flag;
            if (next->give_item != ITEM_NONE)
                player_give_item(next->give_item);
        }
        return;
    }

    /* A button: skip typewriter or advance */
    if (inp->pressed[INPUT_A]) {
        int len = 0;
        const char *p = line->text;
        while (p[len]) len++;

        if (dlg.char_pos < len) {
            /* Skip to full text */
            dlg.char_pos = (uint8_t)len;
        } else {
            /* Advance to next line */
            int16_t next = line->next_id;
            if (next == -1) {
                /* End of dialogue */
                dlg.active = 0;
            } else if (next == -2) {
                /* Show choices (ending choice) */
                dlg.in_choice = 1;
                dlg.choices = ending_choices;
                dlg.choice_count = 3;
                dlg.choice_sel = 0;
            } else {
                dlg.current_line = next;
                dlg.char_pos = 0;
                dlg.tick = 0;

                const dialogue_line_t *nl = &all_dialogues[next];
                if (nl->set_flag)
                    player.quest_flags |= nl->set_flag;
                if (nl->give_item != ITEM_NONE)
                    player_give_item(nl->give_item);
            }
        }
    }

    /* B button: close dialogue */
    if (inp->pressed[INPUT_B]) {
        dlg.active = 0;
    }
}

void dialogue_render(void) {
    if (!dlg.active) return;

    /* Dark box at bottom of screen (above HUD) */
    int box_y = SCREEN_H - 72;   /* 48px tall box + margin */
    int box_h = 48;
    display_rect(0, box_y, SCREEN_W, box_h, 0x0000); /* black bg */
    display_rect_outline(0, box_y, SCREEN_W, box_h, 0xFFFF);

    const dialogue_line_t *line = &all_dialogues[dlg.current_line];

    /* Speaker name in yellow */
    if (line->speaker) {
        display_text(8, box_y + 4, line->speaker, 0xFFE0); /* yellow */
    }

    /* Text with typewriter effect — we need to render only char_pos chars.
       Since display_text doesn't support partial, we'll copy to a buffer. */
    char buf[64];
    int i;
    for (i = 0; i < dlg.char_pos && i < 63 && line->text[i]; i++)
        buf[i] = line->text[i];
    buf[i] = '\0';

    int text_y = line->speaker ? box_y + 18 : box_y + 8;
    display_text(8, text_y, buf, 0xFFFF);

    /* If text is too long, show second line */
    if (dlg.char_pos > 35 && line->text[35]) {
        char buf2[64];
        int j;
        for (j = 0; j + 35 < dlg.char_pos && j < 63 && line->text[j + 35]; j++)
            buf2[j] = line->text[j + 35];
        buf2[j] = '\0';
        display_text(8, text_y + 12, buf2, 0xFFFF);
    }

    /* "A" prompt when text is fully shown */
    int len = 0;
    const char *p = line->text;
    while (p[len]) len++;
    if (dlg.char_pos >= len && !dlg.in_choice) {
        /* Blinking indicator */
        if ((dlg.tick & 4) == 0)
            display_text(SCREEN_W - 24, box_y + box_h - 14, "->", 0x07E0);
    }

    /* Choice rendering */
    if (dlg.in_choice && dlg.choices) {
        int cy = box_y + 4;
        for (int c = 0; c < dlg.choice_count; c++) {
            uint16_t color = (c == dlg.choice_sel) ? 0xFFE0 : 0xC618;
            if (c == dlg.choice_sel)
                display_text(8, cy, ">", 0xFFE0);
            display_text(20, cy, dlg.choices[c].text, color);
            cy += 14;
        }
    }
}

int dialogue_is_active(void) {
    return dlg.active;
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Pick dialogue based on NPC + quest state
 * ═══════════════════════════════════════════════════════════════════════════ */
int get_npc_dialogue_id(int npc_id, uint32_t qf) {
    switch (npc_id) {
    case NPC_MAYOR:
        if (!(qf & QF_MAYOR_MET))       return DLG_MAYOR_FIRST;
        if (qf & QF_HAS_DIARY)          return DLG_MAYOR_AFTER_DIARY;
        return DLG_MAYOR_GENERIC;

    case NPC_ADA:
        return DLG_ADA_GENERIC;

    case NPC_WHITMORE:
        if (!(qf & QF_WHITMORE_MET))    return DLG_WHITMORE_FIRST;
        if ((qf & QF_WHITMORE_QUEST) && !(qf & QF_HAS_MANSION_KEY))
            return DLG_WHITMORE_GIVES_KEY;
        if (!(qf & QF_LIBRARY_QUEST))   return DLG_LIBRARY_QUEST_START;
        if ((qf & QF_LIBRARY_QUEST) && !(qf & QF_LIBRARY_QUEST_DONE))
            return DLG_LIBRARY_QUEST_DONE;
        return DLG_WHITMORE_GENERIC;

    case NPC_PETE:
        if (!(qf & QF_PETE_QUEST))      return DLG_PETE_QUEST_START;
        if ((qf & QF_PETE_QUEST) && !(qf & QF_PETE_QUEST_DONE)) {
            if (player_has_item(ITEM_TACKLE_BOX))
                return DLG_PETE_QUEST_DONE;
            return DLG_PETE_GENERIC;
        }
        return DLG_PETE_GENERIC;

    case NPC_ELARA:
        if (!(qf & QF_ELARA_MET))       return DLG_ELARA_FIRST;
        if (!(qf & QF_ELARA_QUEST))     return DLG_ELARA_QUEST_START;
        if ((qf & QF_ELARA_QUEST) && !(qf & QF_ELARA_QUEST_DONE) &&
            (qf & QF_CRYSTAL_1))        return DLG_ELARA_QUEST_DONE;
        return DLG_ELARA_GENERIC;

    case NPC_OLD_MAN:
        if (!(qf & QF_OLD_MAN_MET))     return DLG_OLD_MAN_FIRST;
        return DLG_OLD_MAN_GENERIC;

    case NPC_TOMMY:
        if (!(qf & QF_TOMMY_QUEST))     return DLG_TOMMY_QUEST_START;
        return DLG_TOMMY_GENERIC;

    case NPC_GHOST:
        if (!(qf & QF_GHOST_MET))       return DLG_GHOST_FIRST;
        return DLG_GHOST_GENERIC;
    }
    return DLG_SIGN_TOWN; /* fallback */
}
