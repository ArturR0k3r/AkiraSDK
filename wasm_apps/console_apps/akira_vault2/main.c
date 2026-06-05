/*
 * main.c — AkiraVault entry point
 * Event loop: GPIO polling + auto-lock timer + BLE HID + TOTP tick
 * SPDX-License-Identifier: Apache-2.0
 */
#include "vault.h"
#include "../include/akira_api.h"

/* vault.h already declares ui_tick and uint32_zero_pad_6 */

/* ── RTC time via AkiraOS ─────────────────────────────────────────────── */
static int       g_timer     = -1;
static uint64_t  g_unix_base = 0;   /* unix time when timer was last synced */
static int       g_ms_base   = 0;

static uint64_t get_unix(void) {
    if(g_timer < 0) return 0;
    int elapsed_ms = timer_elapsed(g_timer) - g_ms_base;
    if(elapsed_ms < 0) elapsed_ms = 0;
    return g_unix_base + (uint64_t)elapsed_ms / 1000;
}

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

/* ── Button GPIO polling (same pattern as space_invaders) ─────────────── */
static int prev_up=0,prev_dn=0,prev_lft=0,prev_rgt=0,prev_cen=0,prev_set=0;
static int center_hold_ms = 0;
static int center_hold_start = 0;
#define LONG_PRESS_MS 800

static int read_key(int *out_long) {
    int up  = gpio_read(PIN_UP);
    int dn  = gpio_read(PIN_DOWN);
    int lft = gpio_read(PIN_LEFT);
    int rgt = gpio_read(PIN_RIGHT);
    int cen = gpio_read(PIN_CENTER);
    int set = gpio_read(PIN_SETTINGS); /* active-low: 1=pressed */

    int key = KEY_NONE;
    *out_long = 0;

    /* Rising edges */
    if(up  && !prev_up)  key = KEY_UP;
    if(dn  && !prev_dn)  key = KEY_DOWN;
    if(lft && !prev_lft) key = KEY_LEFT;
    if(rgt && !prev_rgt) key = KEY_RIGHT;
    if(set && !prev_set) key = KEY_CENTER; /* SETTINGS = emergency lock */

    /* CENTER with long-press detection */
    if(cen && !prev_cen) center_hold_start = (g_timer>=0) ? timer_elapsed(g_timer) : 0;
    if(cen && prev_cen && g_timer>=0) {
        int held = timer_elapsed(g_timer) - center_hold_start;
        if(held >= LONG_PRESS_MS && !(*out_long)) {
            key = KEY_CENTER; *out_long = 1;
        }
    }
    if(cen && !prev_cen) { key = KEY_CENTER; }

    prev_up=up; prev_dn=dn; prev_lft=lft; prev_rgt=rgt;
    prev_cen=cen; prev_set=set;
    return key;
}

/* ── BLE HID init ─────────────────────────────────────────────────────── */
static int g_ble_ready = 0;
static void ble_init_if_needed(void) {
    if(!g_vault.ble_enabled || g_ble_ready || g_locked) return;
    if(hid_init(HID_TRANSPORT_BLE, HID_DEVICE_KEYBOARD) == 0) g_ble_ready = 1;
}

/* ── Main event loop ──────────────────────────────────────────────────── */
int main(void) {
    /* Display init */
    display_get_size(&GW, &GH);

    /* GPIO init */
    gpio_configure(PIN_UP,       GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(PIN_DOWN,     GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(PIN_LEFT,     GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(PIN_RIGHT,    GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(PIN_CENTER,   GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(PIN_SETTINGS, GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);

    /* Monotonic timer */
    g_timer = timer_create();
    if(g_timer >= 0) timer_start(g_timer);

    /* RTC seed — try to load from settings */
    char ts[24];
    if(settings_get("vault/unix", ts, sizeof(ts)) == 0) {
        uint64_t v=0;
        for(int i=0;ts[i]>='0'&&ts[i]<='9';i++) v=v*10+(ts[i]-'0');
        if(v > 1700000000ULL) {
            g_unix_base = v;
            g_ms_base = (g_timer>=0) ? timer_elapsed(g_timer) : 0;
        }
    }

    /* Boot screen */
    g_screen = SCR_BOOT;
    ui_draw();
    delay(1200000); /* 1.2s boot splash */

    /* Route based on vault presence */
    if(vault_exists()) {
        g_screen = SCR_UNLOCK;
    } else {
        g_screen = SCR_SETUP_WELCOME;
        /* Generate fresh entropy for new vault */
        /* Entropy generation requires a create-first flow */
        extern void rng_fill(uint8_t*, int);
        /* We can't call rng_fill yet (declared static in vault_store.c) —
         * vault_create() handles it. Just go to welcome. */
    }
    ui_init();

    int dirty = 1;
    int last_draw_ms = 0;

    while(1) {
        int now_ms = (g_timer>=0) ? timer_elapsed(g_timer) : 0;

        /* Auto-lock */
        if(autolock_expired()) {
            vault_lock();
            dirty = 1;
        }

        /* BLE HID */
        ble_init_if_needed();

        /* TOTP/FIDO2 tick every ~500ms */
        static int last_tick_ms = 0;
        if(now_ms - last_tick_ms >= 500) {
            ui_tick(get_unix());
            last_tick_ms = now_ms;
            if(g_screen == SCR_TOTP_CODE || g_screen == SCR_FIDO2_APPROVE)
                dirty = 1;
        }

        /* Save time periodically to settings */
        static int last_save_ms = 0;
        if(now_ms - last_save_ms >= 60000 && !g_locked) {
            char ts2[24]; int v=(int)(get_unix()&0x7FFFFFFF);
            int i=0; if(!v){ts2[i++]='0';}
            else{int n=v;char tmp[12];int ti=0;while(n){tmp[ti++]='0'+n%10;n/=10;}
                 while(ti-->0) ts2[i++]=tmp[ti];}
            ts2[i]='\0';
            settings_set("vault/unix", ts2);
            last_save_ms = now_ms;
        }

        /* Input — poll at ~50 fps */
        int long_press = 0;
        int key = read_key(&long_press);
        if(key != KEY_NONE) {
            activity();
            ui_handle_key(key, long_press);
            dirty = 1;
        }

        /* Draw at max 20 fps (50ms) to avoid hammering display */
        if(dirty && (now_ms - last_draw_ms >= 50)) {
            ui_draw();
            last_draw_ms = now_ms;
            dirty = 0;
        }

        /* Save vault if dirty */
        if(g_vault_dirty && !g_locked) {
            vault_save();
        }

        delay(20000); /* 20ms = 50Hz poll */
    }
    return 0;
}
