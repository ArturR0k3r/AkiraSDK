/*
 * key_main.c — AkiraKey entry point + event loop
 * SPDX-License-Identifier: Apache-2.0
 */
#include "key.h"

static int g_timer = -1;

/* ── Unix time tracking ──────────────────────────────────────────────── */
static uint64_t g_unix_base = 0;
static int      g_ms_base   = 0;

static uint64_t get_unix(void) {
    if (g_timer < 0) return 0;
    int e = timer_elapsed(g_timer) - g_ms_base;
    return g_unix_base + (uint64_t)(e > 0 ? e : 0) / 1000;
}

static void touch_activity(int now_ms);  /* defined below auto-lock section */

/* ── Button GPIO polling (same pattern as space_invaders / all console apps) */
#define NUM_BTNS 7
static const int PINS[NUM_BTNS] = {PIN_UP,PIN_DOWN,PIN_LEFT,PIN_RIGHT,PIN_A,PIN_B,PIN_SET};
static const int MAP[NUM_BTNS]  = {KEY_UP,KEY_DOWN,KEY_LEFT,KEY_RIGHT,KEY_A,KEY_B,KEY_SET};
static int g_prev[NUM_BTNS];

static int handle_input_events(int now_ms) {
    int got = 0;
    for (int i = 0; i < NUM_BTNS; i++) {
        int v = gpio_read(PINS[i]);
        if (v && !g_prev[i]) {
            touch_activity(now_ms);
            ui_handle_key(MAP[i], 0);
            got = 1;
        }
        g_prev[i] = v;
    }
    return got;
}

static int any_btn_pressed(void) {
    for (int i = 0; i < NUM_BTNS; i++)
        if (gpio_read(PINS[i])) return 1;
    return 0;
}

/* ── BLE (deferred init) ─────────────────────────────────────────────── */
static int g_ble_ready = 0;
static void ble_init_if_needed(void) {
    if (!g_key_vault.ble_enabled || g_ble_ready || g_locked) return;
    if (hid_init(HID_TRANSPORT_BLE, HID_DEVICE_KEYBOARD) == 0) g_ble_ready = 1;
}

/* ── CTAPHID constants ───────────────────────────────────────────────── */
#define CTAPHID_CMD_PING      0x81u
#define CTAPHID_CMD_MSG       0x83u
#define CTAPHID_CMD_INIT      0x86u
#define CTAPHID_CMD_CBOR      0x90u
#define CTAPHID_CMD_CANCEL    0x91u
#define CTAPHID_CMD_ERROR     0xBFu

#define CTAPHID_ERR_INVALID_CMD  0x01u
#define CTAPHID_ERR_CHANNEL_BUSY 0x06u

#define CTAP2_OK                  0x00u
#define CTAP2_ERR_OPERATION_DENIED 0x27u
#define CTAP2_ERR_NOT_ALLOWED     0x30u
#define CTAP1_ERR_INVALID_COMMAND 0x01u

static uint32_t g_fido_next_cid = 0x00000001u;

/* ── CTAPHID frame handler ───────────────────────────────────────────── */
static uint8_t g_fido_buf[64];

static void ctaphid_send(const uint8_t *cid, uint8_t cmd,
                         const uint8_t *data, int dlen) {
    uint8_t resp[64];
    for (int i = 0; i < 64; i++) resp[i] = 0;
    resp[0] = cid[0]; resp[1] = cid[1]; resp[2] = cid[2]; resp[3] = cid[3];
    resp[4] = cmd;
    resp[5] = (uint8_t)((dlen >> 8) & 0xFF);
    resp[6] = (uint8_t)(dlen & 0xFF);
    for (int i = 0; i < dlen && i < 57; i++) resp[7+i] = data[i];
    hid_fido_send(resp, 64);
}

static void ctaphid_send_error(const uint8_t *cid, uint8_t err) {
    ctaphid_send(cid, CTAPHID_CMD_ERROR, &err, 1);
}

static void ctaphid_handle_init(const uint8_t *frame) {
    /* Assign new channel; echo 8-byte nonce; report protocol version */
    uint32_t new_cid = g_fido_next_cid++;
    uint8_t data[17];
    for (int i = 0; i < 8; i++) data[i] = frame[7+i];    /* nonce echo */
    data[ 8] = (uint8_t)(new_cid >> 24);
    data[ 9] = (uint8_t)(new_cid >> 16);
    data[10] = (uint8_t)(new_cid >>  8);
    data[11] = (uint8_t)(new_cid);
    data[12] = 0x02u;   /* CTAPHID protocol version */
    data[13] = 0x01u;   /* device major */
    data[14] = 0x01u;   /* device minor */
    data[15] = 0x00u;   /* device build */
    data[16] = 0x0Cu;   /* CAPABILITY_CBOR | CAPABILITY_NMSG (no U2F MSG) */
    /* Response CID = same as request (broadcast or assigned) */
    ctaphid_send(frame, CTAPHID_CMD_INIT, data, 17);
}

/*
 * Minimal CBOR-encoded authenticatorGetInfo response.
 * versions: ["FIDO_2_0","U2F_V2"], aaguid: "akirakey00000000",
 * options: {rk:false, up:true}, maxMsgSize: 1200
 */
static const uint8_t GETINFO_CBOR[] = {
    CTAP2_OK,
    0xA4,               /* map(4)             */
    0x01,               /* key 1: versions    */
    0x81,               /* array(1)           */
    0x68,'F','I','D','O','_','2','_','0',   /* "FIDO_2_0" */
    0x03,               /* key 3: aaguid      */
    0x81, 0x50,         /* array(1), bytes(16)*/
    'a','k','i','r','a','k','e','y','0','0','0','0','0','0','0','0',
    0x04,               /* key 4: options     */
    0xA2,               /* map(2)             */
    0x62,'r','k', 0xF4, /* "rk": false        */
    0x62,'u','p', 0xF5, /* "up": true         */
    0x06,               /* key 6: maxMsgSize  */
    0x19, 0x04, 0xB0,   /* 1200               */
};

static void ctaphid_handle_cbor(const uint8_t *frame) {
    uint8_t ctap_cmd = frame[7];
    if (ctap_cmd == 0x04u) { /* authenticatorGetInfo */
        ctaphid_send(frame, CTAPHID_CMD_CBOR,
                     GETINFO_CBOR, (int)sizeof(GETINFO_CBOR));
    } else {
        /* MakeCredential/GetAssertion/other — deny until full impl */
        uint8_t err = (ctap_cmd == 0x01u || ctap_cmd == 0x02u)
                      ? CTAP2_ERR_OPERATION_DENIED : CTAP1_ERR_INVALID_COMMAND;
        ctaphid_send(frame, CTAPHID_CMD_CBOR, &err, 1);
    }
}

static void fido_poll(void) {
    int n = hid_fido_recv(g_fido_buf, 64);
    if (n <= 0) return;

    uint8_t cmd = g_fido_buf[4];

    /* INIT and PING always succeed — they don't touch the vault */
    if (cmd == CTAPHID_CMD_INIT) { ctaphid_handle_init(g_fido_buf); return; }
    if (cmd == CTAPHID_CMD_PING) { hid_fido_send(g_fido_buf, 64); return; }
    if (cmd == CTAPHID_CMD_CANCEL) { return; }

    /* CBOR GetInfo (0x04) is safe to answer while locked */
    if (cmd == CTAPHID_CMD_CBOR && g_fido_buf[7] == 0x04u) {
        ctaphid_handle_cbor(g_fido_buf);
        return;
    }

    /* Everything else requires the vault to be unlocked */
    if (g_locked) {
        uint8_t err = CTAP2_ERR_NOT_ALLOWED;
        ctaphid_send(g_fido_buf, CTAPHID_CMD_ERROR, &err, 1);
        return;
    }

    switch (cmd) {
    case CTAPHID_CMD_CBOR:
        ctaphid_handle_cbor(g_fido_buf);
        break;
    default:
        ctaphid_send_error(g_fido_buf, CTAPHID_ERR_INVALID_CMD);
        break;
    }
}

/* ── Auto-lock ───────────────────────────────────────────────────────── */
#define AUTO_LOCK_MS (5 * 60 * 1000)
static int g_last_active_ms = 0;

static void touch_activity(int now_ms) { g_last_active_ms = now_ms; }

static void check_auto_lock(int now_ms) {
    if (g_locked) return;
    if (now_ms - g_last_active_ms > AUTO_LOCK_MS) {
        kstore_lock();
        g_screen = SCR_PIN_ENTRY;
        ui_init();
    }
}

/* ── Persist unix timestamp ──────────────────────────────────────────── */
static void save_unix(int now_ms) {
    static int last_save_ms = 0;
    if (now_ms - last_save_ms < 60000) return;
    last_save_ms = now_ms;
    uint64_t ut = get_unix();
    char ts[24]; int idx = 0;
    if (!ut) { ts[idx++] = '0'; }
    else {
        char tmp[20]; int ti = 0; uint64_t n = ut;
        while (n) { tmp[ti++] = '0' + n % 10; n /= 10; }
        while (ti-- > 0) ts[idx++] = tmp[ti];
    }
    ts[idx] = '\0';
    settings_set("key/unix", ts);
}

/* ── Main ────────────────────────────────────────────────────────────── */
#define BOOT_USB_TIMEOUT_MS 3000

static void boot_transition(void) {
    if (kstore_exists() && kstore_load_hdr() == 0)
        g_screen = SCR_PIN_ENTRY;
    else
        g_screen = SCR_PIN_SETUP_1;
    ui_init();
}

int main(void) {
    display_get_size(&GW, &GH);
    g_timer = timer_create();
    if (g_timer >= 0) timer_start(g_timer);

    /* Restore unix base from settings */
    char ts[24];
    if (settings_get("key/unix", ts, sizeof(ts)) == 0) {
        uint64_t v = 0;
        for (int i = 0; ts[i] >= '0' && ts[i] <= '9'; i++) v = v*10 + (ts[i]-'0');
        if (v > 1700000000ULL) {
            g_unix_base = v;
            g_ms_base   = (g_timer >= 0) ? timer_elapsed(g_timer) : 0;
        }
    }

    hid_init(HID_TRANSPORT_USB, HID_DEVICE_KEYBOARD);

    g_screen          = SCR_BOOT;
    g_boot_usb_status = 0;

    int dirty = 1, last_draw_ms = 0, last_tick_ms = 0;
    int boot_start_ms = (g_timer >= 0) ? timer_elapsed(g_timer) : 0;
    int now_ms = boot_start_ms;
    g_last_active_ms  = now_ms;

    while (!g_exit) {
        now_ms = (g_timer >= 0) ? timer_elapsed(g_timer) : 0;

        /* ── Boot phase: non-blocking USB detection ── */
        if (g_screen == SCR_BOOT) {
            if (g_boot_usb_status == 0) {
                if (hid_is_connected())
                    g_boot_usb_status = 1;
                else if (now_ms - boot_start_ms >= BOOT_USB_TIMEOUT_MS)
                    g_boot_usb_status = -1;
            }

            /* Any button press skips straight through */
            int skip = (g_boot_usb_status != 0) || any_btn_pressed();

            dirty = 1;
            if (skip) {
                if (g_boot_usb_status == 0) g_boot_usb_status = -1;
                ui_draw(); /* show final status briefly before transitioning */
                boot_transition();
            } else {
                ui_draw(); /* spinner */
                delay(10000);
                continue;
            }
        }

        fido_poll();
        ble_init_if_needed();
        check_auto_lock(now_ms);
        save_unix(now_ms);

        if (now_ms - last_tick_ms >= 500) {
            ui_tick(get_unix()); last_tick_ms = now_ms;
            if (g_screen == SCR_TOTP_VIEW) dirty = 1;
        }

        if (handle_input_events(now_ms)) dirty = 1;

        if (dirty && (now_ms - last_draw_ms >= 33)) {
            ui_draw(); last_draw_ms = now_ms; dirty = 0;
        }

        if (g_dirty) kstore_save();

        delay(10000);
    }

    kstore_lock();
    hid_disable();
    return 0;
}
