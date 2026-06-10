/*
 * radio.c — RF Chat radio layer using AkiraOS rf_* API
 * SPDX-License-Identifier: Apache-2.0
 */
#include "radio.h"

/* printf_native: raw host log call (no format string).
 * Declared here to avoid including akira_api.h in this TU
 * (akira_api.h defines printf as a non-static global, causing
 *  duplicate-symbol link errors across multiple object files). */
extern int printf_native(const char *msg);

/* ── Log helpers ────────────────────────────────────────────────────────── */
static void log_buf(const char *prefix, const uint8_t *data, int len,
                    const char *suffix) {
    /* Build: "<prefix>[<len>] <up to 32 chars of text><suffix>" */
    char msg[128];
    int  i = 0;

    /* prefix */
    for (int k = 0; prefix[k] && i < 64; k++) msg[i++] = prefix[k];

    /* "[len] " */
    msg[i++] = '[';
    int v = len, digits = 0;
    char tmp[8];
    if (!v) { tmp[digits++] = '0'; }
    else { while (v) { tmp[digits++] = (char)('0' + v % 10); v /= 10; } }
    for (int k = digits - 1; k >= 0; k--) msg[i++] = tmp[k];
    msg[i++] = ']'; msg[i++] = ' ';

    /* text payload (printable only, max 40 chars) */
    int show = len < 40 ? len : 40;
    for (int k = 0; k < show; k++) {
        unsigned char c = (unsigned char)data[k];
        msg[i++] = (c >= 0x20 && c < 0x7F) ? (char)c : '.';
    }
    if (len > 40) { msg[i++] = '.'; msg[i++] = '.'; msg[i++] = '.'; }

    /* suffix */
    for (int k = 0; suffix[k] && i < 120; k++) msg[i++] = suffix[k];

    msg[i] = '\0';
    printf_native(msg);
}

static void log_str(const char *msg) { printf_native(msg); }

/* ── Public API ─────────────────────────────────────────────────────────── */

static void log_rc(const char *label, int rc) {
    char msg[48];
    int i = 0;
    for (int k = 0; label[k] && i < 32; k++) msg[i++] = label[k];
    msg[i++] = '=';
    int v = rc < 0 ? -rc : rc;
    if (rc < 0) msg[i++] = '-';
    char tmp[8]; int d = 0;
    if (!v) { tmp[d++] = '0'; }
    else { int n = v; while (n) { tmp[d++] = (char)('0' + n % 10); n /= 10; } }
    for (int k = d - 1; k >= 0; k--) msg[i++] = tmp[k];
    msg[i] = '\0';
    printf_native(msg);
}

void radio_init(uint32_t freq_hz) {
    log_str("[RF] init");
    log_rc("[RF] set_frequency rc", rf_set_frequency(freq_hz));
    log_rc("[RF] set_power rc",     rf_set_power(14));
    log_str("[RF] init done");
}

void radio_set_freq(uint32_t freq_hz) {
    /* Log new freq as plain MHz integer */
    char msg[32];
    int i = 0;
    msg[i++] = '['; msg[i++] = 'R'; msg[i++] = 'F'; msg[i++] = ']';
    msg[i++] = ' '; msg[i++] = 'f'; msg[i++] = 'r'; msg[i++] = 'e';
    msg[i++] = 'q'; msg[i++] = '=';
    uint32_t mhz = freq_hz / 1000000u;
    char tmp[8]; int d = 0;
    if (!mhz) { tmp[d++] = '0'; }
    else { uint32_t n = mhz; while (n) { tmp[d++] = (char)('0' + n % 10); n /= 10; } }
    for (int k = d - 1; k >= 0; k--) msg[i++] = tmp[k];
    msg[i++] = 'M'; msg[i++] = 'H'; msg[i++] = 'z'; msg[i] = '\0';
    printf_native(msg);

    rf_set_frequency(freq_hz);
}

int radio_send(const char *text, uint8_t len) {
    if (len > CHAT_MSG_MAX) len = CHAT_MSG_MAX;

    uint8_t buf[3 + CHAT_MSG_MAX];
    buf[0] = CHAT_MAGIC_0;
    buf[1] = CHAT_MAGIC_1;
    buf[2] = len;
    for (uint8_t i = 0; i < len; i++) buf[3 + i] = (uint8_t)text[i];

    log_buf("[RF TX] ", (const uint8_t *)text, len, "");

    int rc = rf_send((uint32_t)(uint8_t *)buf, (uint32_t)(3 + len));
    if (rc < 0) {
        char emsg[32];
        int ei = 0;
        const char *pfx = "[RF TX] ERROR rc=";
        for (int k = 0; pfx[k]; k++) emsg[ei++] = pfx[k];
        int ev = rc < 0 ? -rc : rc;
        if (rc < 0) emsg[ei++] = '-';
        char etmp[8]; int ed = 0;
        if (!ev) { etmp[ed++] = '0'; }
        else { int n = ev; while (n) { etmp[ed++] = (char)('0' + n % 10); n /= 10; } }
        for (int k = ed - 1; k >= 0; k--) emsg[ei++] = etmp[k];
        emsg[ei] = '\0';
        printf_native(emsg);
    }
    return rc;
}

/* Log first bytes as hex to diagnose framing */
static void log_hex(const char *prefix, const uint8_t *data, int len) {
    char msg[80];
    int i = 0;
    for (int k = 0; prefix[k] && i < 32; k++) msg[i++] = prefix[k];
    int show = len < 8 ? len : 8;
    for (int k = 0; k < show; k++) {
        uint8_t b = data[k];
        msg[i++] = ' ';
        msg[i++] = "0123456789ABCDEF"[b >> 4];
        msg[i++] = "0123456789ABCDEF"[b & 0xF];
    }
    msg[i] = '\0';
    printf_native(msg);
}

uint8_t radio_poll(rf_pkt_t *out) {
    uint8_t buf[1 + 3 + CHAT_MSG_MAX]; /* +1 for possible firmware prefix byte */
    int n = rf_receive((uint32_t)(uint8_t *)buf, (uint32_t)sizeof(buf), 0);
    if (n < 0) {
        if (n != -11) log_rc("[RF RX] rc", n);
        return 0;
    }
    if (n == 0) return 0;

    log_hex("[RF RX] hex", buf, n);

    /* Find magic — firmware may prepend 1 header byte */
    int off = 0;
    if (n >= 4 && buf[0] != CHAT_MAGIC_0 && buf[1] == CHAT_MAGIC_0 && buf[2] == CHAT_MAGIC_1)
        off = 1;

    if (n - off < 3) { log_str("[RF RX] drop: too short"); return 0; }
    if (buf[off] != CHAT_MAGIC_0 || buf[off+1] != CHAT_MAGIC_1) {
        log_str("[RF RX] drop: bad magic");
        return 0;
    }

    uint8_t txt_len = buf[off + 2];
    if (txt_len > CHAT_MSG_MAX || (int)(off + 3 + txt_len) > n) {
        log_str("[RF RX] drop: bad length");
        return 0;
    }

    out->magic[0] = CHAT_MAGIC_0;
    out->magic[1] = CHAT_MAGIC_1;
    out->len      = txt_len;
    for (uint8_t i = 0; i < txt_len; i++) out->text[i] = (char)buf[off + 3 + i];

    log_buf("[RF RX] ok ", (const uint8_t *)out->text, txt_len, "");
    return 1;
}

int8_t radio_rssi(void) {
    return (int8_t)rf_get_rssi();
}
