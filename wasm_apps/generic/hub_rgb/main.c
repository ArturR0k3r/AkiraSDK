/*
 * Copyright (c) 2026 PenEngineering S.R.L
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file main.c
 * @brief hub_rgb — drive the board's PWM RGB LED from AkiraHub or the phone
 * companion app, via the app_cmd_bridge IPC convention instead of BLE
 * GATT (rgb_controller), MQTT (ha_rgb), or Matter (matter_rgb).
 *
 * Hub sends `MSG_TYPE_CMD_CUSTOM`/AkiraPlatform "app.cmd", phone sends the
 * `apps.cmd` BLE op; both land on the "hub_rgb.cmd" IPC topic via
 * app_cmd_bridge_send(). This app subscribes to that topic itself.
 *
 * Payload is JSON text, not a binary struct: AkiraHub's cloud REST route
 * (`POST /admin/devices/:id/apps/:name/cmd`) does `JSON.stringify(command)`
 * on the way in and returns the reply as decoded UTF-8 text on the way out —
 * a binary struct would arrive there as opaque garbage. JSON keeps the same
 * payload working over the cloud route, AkiraPlatform's local hub, and the
 * BLE companion path, which are otherwise all just opaque byte pipes.
 *
 * Command:  {"on":1,"r":255,"g":0,"b":0,"bri":255}   (any subset of keys)
 * Reply:    same shape, echoing the full state actually applied.
 *
 *   channel 0 -> GPIO4 (Red)
 *   channel 1 -> GPIO5 (Green)
 *   channel 2 -> GPIO6 (Blue)
 *
 * Required capabilities: "ipc", "pwm"
 *
 * Build:  cd AkiraSDK/wasm_apps/generic/hub_rgb && make
 */

#include "akira_api.h"

#define PWM_FREQ_HZ 1000
#define CH_RED      0
#define CH_GREEN    1
#define CH_BLUE     2
#define RGB_INVERT  0

#define CMD_TOPIC "hub_rgb.cmd"
#define EVT_TOPIC "hub_rgb.evt"

#define CMD_BUF_LEN 128
#define EVT_BUF_LEN 64

typedef struct {
    int on;
    int r;
    int g;
    int b;
    int bri;
} rgb_state_t;

/* No stdlib here (akira_api.h only provides strlen/strcmp/itoa) — a hand
 * rolled substring search and integer scan cover the tiny fixed key set. */
static const char *find_sub(const char *hay, const char *needle)
{
    for (; *hay; hay++) {
        const char *h = hay;
        const char *n = needle;
        while (*n && *h == *n) { h++; n++; }
        if (!*n) return hay;
    }
    return 0;
}

/* Look for "key": followed by an unsigned int; leaves *out unchanged if absent. */
static void parse_field(const char *json, const char *key, int *out)
{
    const char *p = find_sub(json, key);
    if (!p) return;
    p += strlen(key);
    while (*p == '"' || *p == ':' || *p == ' ') p++;
    if (*p < '0' || *p > '9') return;
    int v = 0;
    while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
    *out = v;
}

static void parse_cmd(const char *json, rgb_state_t *s)
{
    parse_field(json, "\"on\"", &s->on);
    parse_field(json, "\"r\"", &s->r);
    parse_field(json, "\"g\"", &s->g);
    parse_field(json, "\"b\"", &s->b);
    parse_field(json, "\"bri\"", &s->bri);
}

static char *append_str(char *p, const char *s)
{
    while (*s) *p++ = *s++;
    return p;
}

static char *append_int(char *p, int v)
{
    itoa(v, p);
    while (*p) p++;
    return p;
}

static int build_evt(char *buf, const rgb_state_t *s)
{
    char *p = buf;
    p = append_str(p, "{\"on\":");
    p = append_int(p, s->on);
    p = append_str(p, ",\"r\":");
    p = append_int(p, s->r);
    p = append_str(p, ",\"g\":");
    p = append_int(p, s->g);
    p = append_str(p, ",\"b\":");
    p = append_int(p, s->b);
    p = append_str(p, ",\"bri\":");
    p = append_int(p, s->bri);
    p = append_str(p, "}");
    *p = '\0';
    return (int)(p - buf);
}

static int duty_from_u8(int v)
{
    int duty = (v * 100) / 255;
#if RGB_INVERT
    duty = 100 - duty;
#endif
    return duty;
}

static void apply_output(const rgb_state_t *s)
{
    if (!s->on) {
        pwm_set(CH_RED, PWM_FREQ_HZ, duty_from_u8(0));
        pwm_set(CH_GREEN, PWM_FREQ_HZ, duty_from_u8(0));
        pwm_set(CH_BLUE, PWM_FREQ_HZ, duty_from_u8(0));
        return;
    }
    int r = (s->r * s->bri) / 255;
    int g = (s->g * s->bri) / 255;
    int b = (s->b * s->bri) / 255;
    pwm_set(CH_RED, PWM_FREQ_HZ, duty_from_u8(r));
    pwm_set(CH_GREEN, PWM_FREQ_HZ, duty_from_u8(g));
    pwm_set(CH_BLUE, PWM_FREQ_HZ, duty_from_u8(b));
}

int main(void)
{
    /* ---- 1. Subscribe to our command topic so the hub/phone bridge can reach us ---- */
    int rc = msg_subscribe(CMD_TOPIC);
    if (rc < 0) {
        printf("hub_rgb: msg_subscribe failed (%d)", rc);
        return rc;
    }
    printf("hub_rgb: listening on " CMD_TOPIC);

    /* ---- 2. Start off ---- */
    rgb_state_t state = { .on = 0, .r = 255, .g = 255, .b = 255, .bri = 255 };
    apply_output(&state);

    /* ---- 3. Command loop ---- */
    char cmd_buf[CMD_BUF_LEN];
    char evt_buf[EVT_BUF_LEN];

    while (1) {
        int len = msg_recv(CMD_TOPIC, (uint8_t *)cmd_buf, sizeof(cmd_buf) - 1, 2000);
        if (len > 0) {
            cmd_buf[len] = '\0';
            parse_cmd(cmd_buf, &state);
            apply_output(&state);
            int evt_len = build_evt(evt_buf, &state);
            msg_publish(EVT_TOPIC, (const uint8_t *)evt_buf, (uint32_t)evt_len);
            printf("hub_rgb: on=%d bri=%d rgb=%d,%d,%d",
                   state.on, state.bri, state.r, state.g, state.b);
        }
    }

    return 0;
}
