/*
 * Copyright (c) 2025 AkiraOS Contributors
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * @file supervisor.c
 * @brief AkiraOS Supervisor — foreground app launcher.
 *
 * Displays a scrollable app list with state badges.
 *   UP/DOWN  — navigate selection
 *   A button — switch to selected app (supervisor exits, app starts)
 *   B button — stop selected app (if running)
 *   Lifecycle IPC events reactively update the list (non-blocking poll).
 *
 * Launch model: app_switch(target) + return 0.
 *   Supervisor exits cleanly after handing off to the target.  The target
 *   app returns by calling app_switch("supervisor") + return 0, which
 *   starts a fresh supervisor instance.
 *
 * Capabilities: display.write, gpio.read, app.control, ipc
 */

#include "include/akira_api.h"

/* ── Button pins (gpio0, ACTIVE_HIGH with PULL_DOWN) ─────────────────── */
#define PIN_UP    4
#define PIN_DOWN  5
#define PIN_A     15   /* Start selected app */
#define PIN_B     16   /* Stop selected app  */

/* ── Display geometry ────────────────────────────────────────────────── */
#define DISPLAY_W    320
#define DISPLAY_H    240
#define TITLE_H       28
#define ROW_H         28
#define MAX_APPS      10
#define NAME_LEN      32
#define LIST_BUF      (MAX_APPS * (NAME_LEN + 16))
#define FOOTER_H      18
#define FOOTER_Y      (DISPLAY_H - FOOTER_H)

/* ── Palette ─────────────────────────────────────────────────────────── */
#define C_HEADER   0x2945
#define C_SEL_BG   COLOR_BLUE
#define C_RUN      COLOR_GREEN
#define C_STOP     COLOR_ORANGE
#define C_ERR      COLOR_RED
#define C_DIM      COLOR_GRAY
#define C_READY    COLOR_DARK_GRAY

/* ── App state constants (match APP_STATE_* in akira_api.h) ──────────── */
#define STATE_NEW       0
#define STATE_INSTALLED 1
#define STATE_RUNNING   2
#define STATE_STOPPED   3
#define STATE_ERROR     4
#define STATE_FAILED    5

/* ── String helpers (no libc) ────────────────────────────────────────── */
static int seq(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return (*a == '\0') && (*b == '\0');
}

static int starts_with(const char *s, const char *prefix) {
    while (*prefix) { if (*s++ != *prefix++) return 0; }
    return 1;
}

static void scopy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

/* ── Button edge detection ───────────────────────────────────────────── */
static int prev_up, prev_dn, prev_a, prev_b;

static void buttons_init(void) {
    gpio_configure(PIN_UP,   GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(PIN_DOWN, GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(PIN_A,    GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(PIN_B,    GPIO_INPUT | GPIO_PULL_DOWN);
}

typedef struct { int up, dn, a, b; } btns_t;

static btns_t buttons_poll(void) {
    int cup = gpio_read(PIN_UP)   == 1;
    int cdn = gpio_read(PIN_DOWN) == 1;
    int ca  = gpio_read(PIN_A)    == 1;
    int cb  = gpio_read(PIN_B)    == 1;
    btns_t e = { cup & ~prev_up, cdn & ~prev_dn, ca & ~prev_a, cb & ~prev_b };
    prev_up = cup;  prev_dn = cdn;  prev_a = ca;  prev_b = cb;
    return e;
}

/* ── App list cache ──────────────────────────────────────────────────── */
static char g_names[MAX_APPS][NAME_LEN];
static int  g_states[MAX_APPS];
static int  g_count;
static char g_buf[LIST_BUF];
static char g_self[NAME_LEN];

static int parse_state(const char *s) {
    if (starts_with(s, "RUNNING"))  return STATE_RUNNING;
    if (starts_with(s, "STOPPED"))  return STATE_STOPPED;
    if (starts_with(s, "ERROR"))    return STATE_ERROR;
    if (starts_with(s, "FAILED"))   return STATE_FAILED;
    return STATE_INSTALLED;
}

/* Full refresh of app list from app_manager */
static void refresh_list(void) {
    int cnt = app_list((uint8_t *)g_buf, LIST_BUF);
    if (cnt < 0) { g_count = 0; return; }
    if (cnt > MAX_APPS) cnt = MAX_APPS;
    g_count = 0;
    char *p = g_buf;
    for (int i = 0; i < cnt; i++) {
        char *col = p;
        while (*col && *col != ':' && *col != '\n') col++;
        if (!*col || *col == '\n') break;
        int nl = (int)(col - p);
        if (nl >= NAME_LEN) nl = NAME_LEN - 1;
        for (int j = 0; j < nl; j++) g_names[g_count][j] = p[j];
        g_names[g_count][nl] = '\0';
        char sb[16]; int si = 0; char *ss = col + 1;
        while (ss[si] && ss[si] != '\n' && si < 15) { sb[si] = ss[si]; si++; }
        sb[si] = '\0';
        g_states[g_count++] = parse_state(sb);
        p = ss + si;
        if (*p == '\n') p++;
    }
}

/* Apply a single lifecycle event to the local cache for reactive updates.
 * Returns 1 if a known entry was updated, 0 if the app was not found
 * (caller should do a full refresh_list on the next suitable cycle). */
static int apply_lifecycle_event(const akira_lifecycle_event_t *ev) {
    for (int i = 0; i < g_count; i++) {
        if (seq(g_names[i], ev->name)) {
            g_states[i] = ev->state;
            return 1;
        }
    }
    return 0; /* unknown app (newly installed?) — refresh needed */
}

/* Navigation helpers — skip self-entry */
static int next_down(int cur) {
    int n = cur + 1;
    while (n < g_count && seq(g_names[n], g_self)) n++;
    return (n < g_count) ? n : cur;
}
static int next_up(int cur) {
    int n = cur - 1;
    while (n >= 0 && seq(g_names[n], g_self)) n--;
    return (n >= 0) ? n : cur;
}

/* ── Drawing ─────────────────────────────────────────────────────────── */
static void draw_header(void) {
    display_rect(0, 0, DISPLAY_W, TITLE_H, C_HEADER);
    display_text_large(8, 6, "AkiraOS", COLOR_WHITE);
    display_hline(0, TITLE_H, DISPLAY_W, COLOR_LIGHT_GRAY);
}

static void draw_footer(const char *msg, uint32_t col) {
    display_rect(0, FOOTER_Y, DISPLAY_W, FOOTER_H, C_HEADER);
    display_hline(0, FOOTER_Y, DISPLAY_W, COLOR_LIGHT_GRAY);
    display_text(4, FOOTER_Y + 2, msg, col);
}

static void draw_launcher(int sel) {
    display_clear(COLOR_BLACK);
    draw_header();

    if (g_count == 0) {
        display_text(10, TITLE_H + 14, "No apps installed.", C_DIM);
        draw_footer("A:Start  B:Stop  UP/DN:Navigate", C_DIM);
        display_flush();
        return;
    }

    for (int i = 0; i < g_count; i++) {
        if (seq(g_names[i], g_self)) continue; /* never show self */
        int y  = TITLE_H + 4 + i * ROW_H;
        uint32_t bg = (i == sel) ? C_SEL_BG : COLOR_BLACK;
        display_rect(0, y, DISPLAY_W, ROW_H - 2, bg);
        display_text(8, y + 7, g_names[i], COLOR_WHITE);

        const char *badge;
        uint32_t    bc;
        switch (g_states[i]) {
        case STATE_RUNNING:   badge = "RUNNING"; bc = C_RUN;   break;
        case STATE_STOPPED:   badge = "STOPPED"; bc = C_STOP;  break;
        case STATE_ERROR:     badge = "ERROR";   bc = C_ERR;   break;
        case STATE_FAILED:    badge = "FAILED";  bc = C_ERR;   break;
        default:              badge = "READY";   bc = C_READY; break;
        }
        display_text(DISPLAY_W - 68, y + 7, badge, bc);
    }

    draw_footer("A:Start  B:Stop  UP/DN:Navigate", C_DIM);
    display_flush();
}

/* ── Main ────────────────────────────────────────────────────────────── */
int main(void)
{
    printf("[supervisor] starting\n");

    buttons_init();
    app_get_self_name((uint8_t *)g_self, NAME_LEN);

    /* Subscribe to the system lifecycle topic for reactive badge updates */
    msg_subscribe("akira.lifecycle");

    /* Initial list */
    refresh_list();

    /* Find first valid selection (skip self) */
    int sel = 0;
    while (sel < g_count && seq(g_names[sel], g_self)) sel++;
    if (sel >= g_count) sel = 0;

    draw_launcher(sel);

    while (1) {
        btns_t e = buttons_poll();

        /* Non-blocking drain of lifecycle events — update list cache */
        akira_lifecycle_event_t ev;
        int need_redraw = 0;
        while (msg_try_recv("akira.lifecycle",
                            (uint8_t *)&ev, sizeof(ev)) == (int)sizeof(ev)) {
            int known = apply_lifecycle_event(&ev);
            if (!known) {
                /* A new/unknown app appeared — full refresh */
                refresh_list();
            }
            need_redraw = 1;
        }
        if (need_redraw) draw_launcher(sel);

        /* Navigation */
        if (e.up) {
            int ns = next_up(sel);
            if (ns != sel) { sel = ns; draw_launcher(sel); }
        }
        if (e.dn) {
            int ns = next_down(sel);
            if (ns != sel) { sel = ns; draw_launcher(sel); }
        }

        /* A: switch to selected app.
         * app_switch() starts the target; we then return 0 to exit cleanly.
         * The target returns to supervisor via app_switch("supervisor") + return 0,
         * which starts a fresh supervisor instance.                              */
        if (e.a && sel >= 0 && sel < g_count && !seq(g_names[sel], g_self)) {
            if (g_states[sel] != STATE_RUNNING) {
                /* Flush our display before handing off — avoids concurrent
                 * DMA to the shared PSRAM framebuffer on ESP32-S3.          */
                draw_footer("Launching...", C_RUN);
                display_flush();
                int r = app_switch(g_names[sel]);
                if (r == 0) {
                    return 0; /* clean exit — target app is now running */
                }
                /* Switch failed — show error and stay in launcher */
                draw_footer("Launch failed!", C_ERR);
                display_flush();
                delay(800000);
                draw_launcher(sel);
            }
        }

        /* B: stop selected app (only if running) */
        if (e.b && sel >= 0 && sel < g_count && !seq(g_names[sel], g_self)) {
            if (g_states[sel] == STATE_RUNNING) {
                app_stop(g_names[sel]);
                /* Optimistically update cache; IPC event will confirm */
                g_states[sel] = STATE_STOPPED;
                draw_launcher(sel);
            }
        }

        delay(33333); /* ~30 Hz poll */
    }

    return 0;
}
