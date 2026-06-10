/*
 * main.c — RF Chat entry point + event loop
 * Two-console radio messenger over CC1121 sub-GHz.
 *
 * Controls:
 *   CHAT:    A=compose  X=options  UP/DOWN=scroll
 *   COMPOSE: D-pad=navigate keyboard  A=select  B=backspace  Y=send
 *   OPTIONS: UP/DOWN=+/-100kHz  LEFT/RIGHT=+/-1MHz  A=save  B=cancel
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "akira_api.h"  /* include in ONE translation unit only */
#include "app.h"

/* ── Global state definitions ───────────────────────────────────────────── */
int32_t     GW = 0, GH = 0;
chat_msg_t  g_msgs[MAX_MSGS];
int         g_msg_count  = 0;
int         g_scroll     = 0;

char        g_compose[MAX_MSG_LEN + 1];
int         g_compose_len = 0;

int         g_kb_row = 0;
int         g_kb_col = 0;

uint32_t    g_freq_hz   = FREQ_DEFAULT;
uint32_t    g_opt_freq  = FREQ_DEFAULT;
int         g_opt_chip  = RF_CHIP_LR2021;
app_state_t g_state        = STATE_RADIO_SEL;
int8_t      g_rssi         = 0;
int         g_rf_chip      = RF_CHIP_CC1121;
int         g_radio_cursor = 0;
int         g_opt_cursor   = 0;
int         g_last_tx_ms   = -1000;
char        g_my_name[MAX_NAME_LEN + 1] = "User";
int         g_my_name_len  = 4;
static int  s_tmr          = -1;
static int  g_radio_inited = 0;

/* ── Button indices ─────────────────────────────────────────────────────── */
#define BTN_UP    0
#define BTN_DOWN  1
#define BTN_LEFT  2
#define BTN_RIGHT 3
#define BTN_A     4
#define BTN_B     5
#define BTN_X     6
#define BTN_Y     7
#define BTN_COUNT 8

/* GPIO pins matching hardware (active-low, external pull-up) */
static const int BTN_PINS[BTN_COUNT] = { 4, 5, 6, 7, 15, 16, 17, 41 };

/* ── Button debounce state ──────────────────────────────────────────────── */
#define DB_FRAMES 3   /* 3 × 20 ms = 60 ms debounce */

static int btn_cnt[BTN_COUNT];   /* consecutive high-read count */
static int btn_st[BTN_COUNT];    /* debounced state (1=pressed) */
static int btn_prev[BTN_COUNT];  /* state last frame */
static int btn_hold[BTN_COUNT];  /* frames held while debounced */

static void btns_init(void) {
    for (int i = 0; i < BTN_COUNT; i++)
        gpio_configure(BTN_PINS[i], GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);
}

static void btns_poll(void) {
    for (int i = 0; i < BTN_COUNT; i++) {
        int raw = gpio_read(BTN_PINS[i]);
        btn_prev[i] = btn_st[i];
        if (raw) btn_cnt[i]++;
        else     btn_cnt[i] = 0;
        btn_st[i]   = (btn_cnt[i] >= DB_FRAMES) ? 1 : 0;
        btn_hold[i] = btn_st[i] ? btn_hold[i] + 1 : 0;
    }
}

/* Edge detectors */
static int rose(int i)  { return  btn_st[i] && !btn_prev[i]; }
static int held(int i)  { return  btn_st[i]; }
static int in_kb_state(void) { return g_state == STATE_COMPOSE || g_state == STATE_NAME_EDIT; }

/* ── QWERTY row data ────────────────────────────────────────────────────── */
static const char *KB_CHARS[]   = { "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM" };
static const int   KB_ROW_LEN[] = { 10, 9, 7, 3 };

/* ── String helpers ─────────────────────────────────────────────────────── */
static void smemcpy(void *dst, const void *src, int n) {
    char *d = (char *)dst;
    const char *s = (const char *)src;
    while (n--) *d++ = *s++;
}

/* ── Settings persistence ───────────────────────────────────────────────── */
#define SETTINGS_FREQ_KEY "rf_chat/freq"
#define SETTINGS_NAME_KEY "rf_chat/name"

static void name_save(void) {
    settings_set(SETTINGS_NAME_KEY, g_my_name);
}

static void name_load(void) {
    char buf[MAX_NAME_LEN + 2];
    if (settings_get(SETTINGS_NAME_KEY, buf, sizeof(buf)) != 0) return;
    int len = 0;
    while (buf[len] && len < MAX_NAME_LEN) len++;
    if (len == 0) return;
    smemcpy(g_my_name, buf, len);
    g_my_name[len] = '\0';
    g_my_name_len  = len;
}

static void freq_save(void) {
    char buf[12];
    uint32_t v = g_freq_hz;
    int i = 0;
    if (!v) {
        buf[i++] = '0';
    } else {
        char tmp[12]; int ti = 0;
        while (v) { tmp[ti++] = (char)('0' + v % 10); v /= 10; }
        while (ti--) buf[i++] = tmp[ti];
    }
    buf[i] = '\0';
    settings_set(SETTINGS_FREQ_KEY, buf);
}

static uint32_t freq_load(void) {
    char buf[12];
    if (settings_get(SETTINGS_FREQ_KEY, buf, sizeof(buf)) != 0)
        return FREQ_DEFAULT;
    uint32_t v = 0;
    for (int i = 0; buf[i] >= '0' && buf[i] <= '9'; i++)
        v = v * 10u + (uint32_t)(buf[i] - '0');
    if (v < FREQ_MIN || v > FREQ_MAX) return FREQ_DEFAULT;
    return v;
}

/* ── Message management ─────────────────────────────────────────────────── */
void msg_add(const char *text, uint8_t len, uint8_t sent, int8_t rssi,
             const char *sender, uint8_t sender_len) {
    if (!len || len > MAX_MSG_LEN) return;

    chat_msg_t *m;
    if (g_msg_count < MAX_MSGS) {
        m = &g_msgs[g_msg_count++];
    } else {
        for (int i = 0; i < MAX_MSGS - 1; i++) g_msgs[i] = g_msgs[i + 1];
        m = &g_msgs[MAX_MSGS - 1];
    }

    smemcpy(m->text, text, len);
    m->text[len] = '\0';
    m->len  = len;
    m->sent = sent;
    m->rssi = rssi;

    if (sender && sender_len > 0) {
        uint8_t nl = sender_len > MAX_NAME_LEN ? MAX_NAME_LEN : sender_len;
        smemcpy(m->sender, sender, nl);
        m->sender[nl] = '\0';
    } else {
        m->sender[0] = '\0';
    }

    g_scroll = 0;
}

void msg_send(void) {
    if (!g_compose_len) return;
    radio_send(g_compose, (uint8_t)g_compose_len,
               g_my_name, (uint8_t)g_my_name_len);
    g_last_tx_ms = (s_tmr >= 0) ? timer_elapsed(s_tmr) : 0;
    msg_add(g_compose, (uint8_t)g_compose_len, 1, 0, g_my_name, (uint8_t)g_my_name_len);
    g_compose[0]  = '\0';
    g_compose_len = 0;
    g_state       = STATE_CHAT;
}

/* ── Keyboard navigation ────────────────────────────────────────────────── */
static void kb_move(int dir) { /* 0=up 1=down 2=left 3=right */
    switch (dir) {
    case 0:
        if (g_kb_row > 0) {
            g_kb_row--;
            if (g_kb_col >= KB_ROW_LEN[g_kb_row])
                g_kb_col = KB_ROW_LEN[g_kb_row] - 1;
        }
        break;
    case 1:
        if (g_kb_row < KB_ROWS - 1) {
            g_kb_row++;
            if (g_kb_col >= KB_ROW_LEN[g_kb_row])
                g_kb_col = KB_ROW_LEN[g_kb_row] - 1;
        }
        break;
    case 2:
        g_kb_col = (g_kb_col > 0) ? g_kb_col - 1 : KB_ROW_LEN[g_kb_row] - 1;
        break;
    case 3:
        g_kb_col = (g_kb_col < KB_ROW_LEN[g_kb_row] - 1) ? g_kb_col + 1 : 0;
        break;
    }
}

static void kb_select(void) {
    int name_mode = (g_state == STATE_NAME_EDIT);
    int max_len   = name_mode ? MAX_NAME_LEN : MAX_MSG_LEN;
    if (g_kb_row < 3) {
        if (g_compose_len < max_len) {
            g_compose[g_compose_len++] = KB_CHARS[g_kb_row][g_kb_col];
            g_compose[g_compose_len]   = '\0';
        }
    } else {
        switch (g_kb_col) {
        case 0: /* SPC */
            if (g_compose_len < max_len) {
                g_compose[g_compose_len++] = ' ';
                g_compose[g_compose_len]   = '\0';
            }
            break;
        case 1: /* DEL */
            if (g_compose_len > 0) g_compose[--g_compose_len] = '\0';
            break;
        case 2: /* SEND / SAVE */
            if (name_mode) {
                int nl = g_compose_len > MAX_NAME_LEN ? MAX_NAME_LEN : g_compose_len;
                if (nl > 0) {
                    for (int i = 0; i < nl; i++) g_my_name[i] = g_compose[i];
                    g_my_name[nl] = '\0';
                    g_my_name_len = nl;
                    name_save();
                }
                g_compose[0] = '\0'; g_compose_len = 0;
                g_state = STATE_OPTIONS;
            } else {
                msg_send();
            }
            break;
        }
    }
}

/* ── DAS: after DAS_DELAY frames start repeating every DAS_RPT frames ──── */
#define DAS_DELAY  18   /* ~360 ms */
#define DAS_RPT     4   /* ~80 ms  */

static int dpad_should_fire(int btn_idx) {
    int h = btn_hold[btn_idx];
    if (!h) return 0;
    return (h == DAS_DELAY) ||
           (h > DAS_DELAY && ((h - DAS_DELAY) % DAS_RPT) == 0);
}

/* ── Main loop ──────────────────────────────────────────────────────────── */
int main(void) {
    /* Must call display_get_size first — activates the display driver for
     * this WASM app (all other apps do this as their very first call). */
    display_get_size(&GW, &GH);

    s_tmr = timer_create();
    if (s_tmr >= 0) timer_start(s_tmr);

    g_freq_hz  = freq_load();
    g_opt_freq = g_freq_hz;
    g_opt_chip = g_rf_chip;
    name_load();

    btns_init();
    /* radio_init deferred until user confirms chip on STATE_RADIO_SEL screen */

    int dirty        = 1;
    int last_draw_ms = 0;

    while (1) {
        int now_ms = (s_tmr >= 0) ? timer_elapsed(s_tmr) : 0;

        /* ── Button poll — always first for responsiveness ──────────────── */
        btns_poll();

        /* ─ D-pad: edge (single press) ─────────────────────────────────── */
        if (rose(BTN_UP) || rose(BTN_DOWN) || rose(BTN_LEFT) || rose(BTN_RIGHT))
            dirty = 1;

        if (rose(BTN_UP)) {
            if      (in_kb_state())    kb_move(0);
            else if (g_state == STATE_CHAT)       { if (g_scroll < g_msg_count - 1) g_scroll++; }
            else if (g_state == STATE_OPTIONS)    { if (g_opt_cursor > 0) g_opt_cursor--; }
        }
        if (rose(BTN_DOWN)) {
            if      (in_kb_state())    kb_move(1);
            else if (g_state == STATE_CHAT)       { if (g_scroll > 0) g_scroll--; }
            else if (g_state == STATE_OPTIONS)    { if (g_opt_cursor < 2) g_opt_cursor++; }
        }
        if (rose(BTN_LEFT)) {
            if      (in_kb_state())    kb_move(2);
            else if (g_state == STATE_OPTIONS) {
                if      (g_opt_cursor == 0 && g_opt_freq >= FREQ_MIN + FREQ_STEP_SM) g_opt_freq -= FREQ_STEP_SM;
                else if (g_opt_cursor == 1) g_opt_chip = (g_opt_chip == RF_CHIP_CC1121) ? RF_CHIP_LR2021 : RF_CHIP_CC1121;
            }
            else if (g_state == STATE_RADIO_SEL)  { g_radio_cursor = 0; dirty = 1; }
        }
        if (rose(BTN_RIGHT)) {
            if      (in_kb_state())    kb_move(3);
            else if (g_state == STATE_OPTIONS) {
                if      (g_opt_cursor == 0 && g_opt_freq + FREQ_STEP_SM <= FREQ_MAX) g_opt_freq += FREQ_STEP_SM;
                else if (g_opt_cursor == 1) g_opt_chip = (g_opt_chip == RF_CHIP_CC1121) ? RF_CHIP_LR2021 : RF_CHIP_CC1121;
            }
            else if (g_state == STATE_RADIO_SEL)  { g_radio_cursor = 1; dirty = 1; }
        }

        /* ─ D-pad: hold DAS repeat ──────────────────────────────────────── */
        if (held(BTN_UP) && dpad_should_fire(BTN_UP)) {
            if (in_kb_state())         kb_move(0);
            else if (g_state == STATE_CHAT)        { if (g_scroll < g_msg_count - 1) g_scroll++; }
            else if (g_state == STATE_OPTIONS)     { if (g_opt_freq + FREQ_STEP_SM <= FREQ_MAX)  g_opt_freq += FREQ_STEP_SM; }
            dirty = 1;
        }
        if (held(BTN_DOWN) && dpad_should_fire(BTN_DOWN)) {
            if (in_kb_state())         kb_move(1);
            else if (g_state == STATE_CHAT)        { if (g_scroll > 0) g_scroll--; }
            else if (g_state == STATE_OPTIONS)     { if (g_opt_freq >= FREQ_MIN + FREQ_STEP_SM)  g_opt_freq -= FREQ_STEP_SM; }
            dirty = 1;
        }
        if (held(BTN_LEFT) && dpad_should_fire(BTN_LEFT)) {
            if (in_kb_state())         kb_move(2);
            else if (g_state == STATE_OPTIONS && g_opt_cursor == 0) {
                if (g_opt_freq >= FREQ_MIN + FREQ_STEP_SM) g_opt_freq -= FREQ_STEP_SM;
            }
            dirty = 1;
        }
        if (held(BTN_RIGHT) && dpad_should_fire(BTN_RIGHT)) {
            if (in_kb_state())         kb_move(3);
            else if (g_state == STATE_OPTIONS && g_opt_cursor == 0) {
                if (g_opt_freq + FREQ_STEP_SM <= FREQ_MAX) g_opt_freq += FREQ_STEP_SM;
            }
            dirty = 1;
        }

        /* ─ Action buttons ──────────────────────────────────────────────── */
        if (rose(BTN_A)) {
            dirty = 1;
            switch (g_state) {
            case STATE_RADIO_SEL:
                /* Confirm chip selection, init radio, enter chat */
                g_rf_chip = (g_radio_cursor == 0) ? RF_CHIP_CC1121 : RF_CHIP_LR2021;
                radio_select(g_rf_chip, g_freq_hz);
                g_radio_inited = 1;
                g_state = STATE_CHAT;
                break;
            case STATE_CHAT:
                g_state = STATE_COMPOSE;
                break;
            case STATE_COMPOSE:
                kb_select();
                break;
            case STATE_OPTIONS:
                if (g_opt_cursor == 1) {
                    g_opt_chip = (g_opt_chip == RF_CHIP_CC1121) ? RF_CHIP_LR2021 : RF_CHIP_CC1121;
                } else if (g_opt_cursor == 2) {
                    int nl = g_my_name_len;
                    for (int i = 0; i < nl; i++) g_compose[i] = g_my_name[i];
                    g_compose[nl] = '\0';
                    g_compose_len = nl;
                    g_state = STATE_NAME_EDIT;
                }
                break;
            case STATE_NAME_EDIT:
                kb_select();
                break;
            }
        }

        if (rose(BTN_B)) {
            dirty = 1;
            switch (g_state) {
            case STATE_RADIO_SEL:
                break; /* B does nothing on selection screen */
            case STATE_CHAT:
                break;
            case STATE_COMPOSE:
                if (g_compose_len > 0) {
                    g_compose[--g_compose_len] = '\0';
                } else {
                    g_state = STATE_CHAT;
                }
                break;
            case STATE_OPTIONS: {
                int chip_ch = (g_rf_chip != g_opt_chip);
                int freq_ch = (g_freq_hz != g_opt_freq);
                g_freq_hz = g_opt_freq;
                g_rf_chip = g_opt_chip;
                if (chip_ch) radio_select(g_rf_chip, g_freq_hz);
                else if (freq_ch) radio_set_freq(g_freq_hz);
                if (freq_ch) freq_save();
                g_state = STATE_CHAT;
                break;
            }
            case STATE_NAME_EDIT:
                if (g_compose_len > 0) {
                    g_compose[--g_compose_len] = '\0';
                } else {
                    g_compose[0] = '\0';
                    g_state = STATE_OPTIONS;
                }
                break;
            }
        }

        if (rose(BTN_X)) {
            dirty = 1;
            if (g_state == STATE_CHAT) {
                g_opt_freq   = g_freq_hz;
                g_opt_chip   = g_rf_chip;
                g_opt_cursor = 0;
                g_state      = STATE_OPTIONS;
            } else if (g_state == STATE_OPTIONS) {
                /* Discard staged changes and return */
                g_opt_freq = g_freq_hz;
                g_opt_chip = g_rf_chip;
                g_state    = STATE_CHAT;
            }
        }

        if (rose(BTN_Y)) {
            dirty = 1;
            if (g_state == STATE_COMPOSE) {
                msg_send();
            } else if (g_state == STATE_NAME_EDIT) {
                /* Save name */
                int nl = g_compose_len > MAX_NAME_LEN ? MAX_NAME_LEN : g_compose_len;
                if (nl > 0) {
                    for (int i = 0; i < nl; i++) g_my_name[i] = g_compose[i];
                    g_my_name[nl] = '\0';
                    g_my_name_len = nl;
                    name_save();
                }
                g_compose[0] = '\0'; g_compose_len = 0;
                g_state = STATE_OPTIONS;
            }
        }

        /* ── Redraw: max 25 fps ─────────────────────────────────────────── */
        if (dirty && (now_ms - last_draw_ms >= 40)) {
            ui_draw();
            last_draw_ms = now_ms;
            dirty        = 0;
        }

        /* ── Always poll RF when ready — rf_recv_pop is non-blocking ──────── */
        if (g_radio_inited) {
            rf_pkt_t pkt;
            if (radio_poll(&pkt)) {
                g_rssi = radio_rssi();
                msg_add(pkt.data + pkt.name_len, pkt.msg_len, 0, g_rssi,
                        pkt.data, pkt.name_len);
                dirty = 1;
            }
        }

        delay(20000); /* 20 ms tick */
    }
    return 0;
}
