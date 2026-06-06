/*
 * main.c — AkiraVault Cold Wallet entry point
 * Event loop: GPIO polling + auto-lock timer
 * SPDX-License-Identifier: Apache-2.0
 */
#include "vault.h"
#include "../include/akira_api.h"

static int      g_timer    = -1;

/* ── Auto-lock ─────────────────────────────────────────────────────────── */
static int g_last_activity_ms = 0;

static void activity(void) {
    if(g_timer >= 0) g_last_activity_ms = timer_elapsed(g_timer);
}

static int autolock_expired(void) {
    if(g_locked || g_vault.autolock_s == 0 || g_timer < 0) return 0;
    int idle_ms = timer_elapsed(g_timer) - g_last_activity_ms;
    return idle_ms > (int)(g_vault.autolock_s * 1000);
}

/* ── Button GPIO polling ──────────────────────────────────────────────── */
static int prev_up=0,prev_dn=0,prev_lft=0,prev_rgt=0,prev_cen=0,prev_set=0;
static int center_hold_start = 0;
#define LONG_PRESS_MS 800

static int read_key(int *out_long) {
    int up  = gpio_read(PIN_UP);
    int dn  = gpio_read(PIN_DOWN);
    int lft = gpio_read(PIN_LEFT);
    int rgt = gpio_read(PIN_RIGHT);
    int cen = gpio_read(PIN_CENTER);
    int set = gpio_read(PIN_SETTINGS); /* GPIO_ACTIVE_LOW: gpio_read returns logical 1=pressed */

    int key = KEY_NONE;
    *out_long = 0;

    if(up  && !prev_up)  key = KEY_UP;
    if(dn  && !prev_dn)  key = KEY_DOWN;
    if(lft && !prev_lft) key = KEY_LEFT;
    if(rgt && !prev_rgt) key = KEY_RIGHT;
    if(set && !prev_set) key = KEY_LEFT; /* SETTINGS = back / lock */

    /* CENTER with long-press */
    if(cen && !prev_cen) center_hold_start = (g_timer>=0)?timer_elapsed(g_timer):0;
    if(cen && prev_cen && g_timer>=0){
        int held = timer_elapsed(g_timer) - center_hold_start;
        if(held >= LONG_PRESS_MS){ key = KEY_CENTER; *out_long = 1; }
    }
    if(cen && !prev_cen) key = KEY_CENTER;

    prev_up=up; prev_dn=dn; prev_lft=lft; prev_rgt=rgt;
    prev_cen=cen; prev_set=set;
    return key;
}

/* ── Main event loop ──────────────────────────────────────────────────── */
int main(void) {
    display_get_size(&GW, &GH);

    gpio_configure(PIN_UP,       GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(PIN_DOWN,     GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(PIN_LEFT,     GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(PIN_RIGHT,    GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(PIN_CENTER,   GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(PIN_SETTINGS, GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);

    g_timer = timer_create();
    if(g_timer >= 0) timer_start(g_timer);

    g_screen = SCR_BOOT;
    ui_draw();
    delay(1200000);

    if(vault_exists()) g_screen = SCR_UNLOCK;
    else               g_screen = SCR_SETUP_WELCOME;
    ui_init();

    int dirty = 1, last_draw_ms = 0;

    while(1) {
        int now_ms = (g_timer>=0) ? timer_elapsed(g_timer) : 0;

        if(autolock_expired()){ vault_lock(); dirty=1; }

        int long_press=0;
        int key=read_key(&long_press);
        if(key != KEY_NONE){ activity(); ui_handle_key(key,long_press); dirty=1; }

        if(dirty && (now_ms - last_draw_ms >= 50)){
            ui_draw();
            last_draw_ms=now_ms; dirty=0;
        }

        if(g_vault_dirty && !g_locked) vault_save();

        delay(20000);
    }
    return 0;
}
