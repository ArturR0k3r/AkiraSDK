/*
 * Copyright (c) 2026 PenEngineering S.R.L
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file main.c
 * @brief nfc_test — exercises the nfc_* WASM API
 *
 * Menu of tests against the registered NFC tag (ST25DV): UID, RF field
 * presence, user-memory read/write, and FTM mailbox put/get/status.
 *
 * Controls:
 *   UP/DOWN  move selection
 *   A        run selected test
 *   B        exit to launcher
 */

#include "akira_api.h"

static int32_t SCR_W = 320;
static int32_t SCR_H = 240;

#define COL_BG   0x0000U
#define COL_FG   0xFFFFU

static const char *const TESTS[] = {
    "UID",
    "FIELD PRESENT",
    "READ MEM @0x0000",
    "WRITE MEM @0x0000",
    "MB ENABLE",
    "MB PUT \"AKIRA\"",
    "MB GET",
    "MB STATUS",
};
#define NUM_TESTS ((int)(sizeof(TESTS) / sizeof(TESTS[0])))

static char result[256];

/* ── tiny formatting helpers (no libc in this freestanding build) ──────── */
static int str_len(const char *s)
{
    int n = 0;
    while (s[n]) n++;
    return n;
}

static void str_append(char *dst, int *pos, const char *src)
{
    int i = 0;
    while (src[i]) dst[(*pos)++] = src[i++];
    dst[*pos] = 0;
}

static char nibble_hex(uint8_t v)
{
    return (char)(v < 10 ? '0' + v : 'A' + (v - 10));
}

static void append_hex_byte(char *dst, int *pos, uint8_t v)
{
    dst[(*pos)++] = nibble_hex((uint8_t)(v >> 4));
    dst[(*pos)++] = nibble_hex((uint8_t)(v & 0x0F));
    dst[*pos] = 0;
}

static void append_hex_buf(char *dst, int *pos, const uint8_t *buf, int len)
{
    for (int i = 0; i < len; i++)
    {
        if (i > 0) dst[(*pos)++] = ' ';
        append_hex_byte(dst, pos, buf[i]);
    }
}

static void append_int(char *dst, int *pos, int v)
{
    if (v < 0)
    {
        dst[(*pos)++] = '-';
        v = -v;
    }
    char tmp[12];
    int n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v > 0)
    {
        tmp[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (n > 0) dst[(*pos)++] = tmp[--n];
    dst[*pos] = 0;
}

/* ── one function per menu entry — each writes its outcome into result[] ─ */
static void run_uid(void)
{
    uint8_t uid[8];
    int ret = nfc_uid(uid);
    int pos = 0;
    if (ret < 0)
    {
        str_append(result, &pos, "nfc_uid failed: ");
        append_int(result, &pos, ret);
        return;
    }
    str_append(result, &pos, "UID: ");
    append_hex_buf(result, &pos, uid, 8);
}

static void run_field_present(void)
{
    int ret = nfc_field_present();
    int pos = 0;
    if (ret < 0)
    {
        str_append(result, &pos, "nfc_field_present failed: ");
        append_int(result, &pos, ret);
        return;
    }
    str_append(result, &pos, ret ? "RF field: PRESENT" : "RF field: ABSENT");
}

static void run_read_mem(void)
{
    uint8_t buf[8];
    int ret = nfc_read(0, buf, sizeof(buf));
    int pos = 0;
    if (ret < 0)
    {
        str_append(result, &pos, "nfc_read failed: ");
        append_int(result, &pos, ret);
        return;
    }
    str_append(result, &pos, "READ: ");
    append_hex_buf(result, &pos, buf, sizeof(buf));
}

static void run_write_mem(void)
{
    static const uint8_t payload[8] = {'A', 'K', 'I', 'R', 'A', 'T', 'S', 'T'};
    int ret = nfc_write(0, payload, sizeof(payload));
    int pos = 0;
    if (ret < 0)
    {
        str_append(result, &pos, "nfc_write failed: ");
        append_int(result, &pos, ret);
        return;
    }
    str_append(result, &pos, "WRITE OK (8 bytes @0x0000)");
}

static void run_mb_enable(void)
{
    int ret = nfc_mb_enable(1, 0);
    int pos = 0;
    if (ret < 0)
    {
        str_append(result, &pos, "nfc_mb_enable failed: ");
        append_int(result, &pos, ret);
        str_append(result, &pos, " (needs I2C session open)");
        return;
    }
    str_append(result, &pos, "FTM mailbox enabled");
}

static void run_mb_put(void)
{
    static const uint8_t msg[5] = {'A', 'k', 'i', 'r', 'a'};
    int ret = nfc_mb_put(msg, sizeof(msg));
    int pos = 0;
    if (ret < 0)
    {
        str_append(result, &pos, "nfc_mb_put failed: ");
        append_int(result, &pos, ret);
        return;
    }
    str_append(result, &pos, "MB PUT OK (5 bytes: \"Akira\")");
}

static void run_mb_get(void)
{
    uint8_t buf[32];
    int ret = nfc_mb_get(buf, sizeof(buf));
    int pos = 0;
    if (ret < 0)
    {
        str_append(result, &pos, "nfc_mb_get failed: ");
        append_int(result, &pos, ret);
        return;
    }
    str_append(result, &pos, "MB GET (");
    append_int(result, &pos, ret);
    str_append(result, &pos, " bytes): ");
    append_hex_buf(result, &pos, buf, ret);
}

static void run_mb_status(void)
{
    uint8_t ctrl = 0;
    unsigned int msg_len = 0;
    int ret = nfc_mb_status(&ctrl, &msg_len);
    int pos = 0;
    if (ret < 0)
    {
        str_append(result, &pos, "nfc_mb_status failed: ");
        append_int(result, &pos, ret);
        return;
    }
    str_append(result, &pos, "CTRL=0x");
    append_hex_byte(result, &pos, ctrl);
    str_append(result, &pos, " EN=");
    append_int(result, &pos, ctrl & 0x01);
    str_append(result, &pos, " HOST_PUT=");
    append_int(result, &pos, (ctrl >> 1) & 0x01);
    str_append(result, &pos, " RF_PUT=");
    append_int(result, &pos, (ctrl >> 2) & 0x01);
    str_append(result, &pos, " len=");
    append_int(result, &pos, (int)msg_len);
}

typedef void (*test_fn_t)(void);
static const test_fn_t TEST_FNS[] = {
    run_uid, run_field_present, run_read_mem, run_write_mem,
    run_mb_enable, run_mb_put, run_mb_get, run_mb_status,
};

/* ── word-wrapped result rendering (result[] can exceed one line) ──────── */
static void draw_wrapped(int x, int y, int max_w, const char *text, uint32_t color)
{
    int chars_per_line = max_w / 6; /* built-in font is ~6px wide per char */
    if (chars_per_line < 1) chars_per_line = 1;
    int len = str_len(text);
    int line = 0;
    for (int i = 0; i < len; i += chars_per_line, line++)
    {
        char buf[64];
        int n = 0;
        while (n < chars_per_line && (i + n) < len && n < (int)sizeof(buf) - 1)
        {
            buf[n] = text[i + n];
            n++;
        }
        buf[n] = 0;
        display_text(x, y + line * 12, buf, color);
    }
}

static void render(int selected)
{
    display_clear(COL_BG);

    display_text(8, 4, "NFC TEST", COL_FG);
    display_rect(0, 16, SCR_W, 1, COL_FG);

    int row_h = (SCR_H * 40 / 100) / NUM_TESTS;
    if (row_h < 12) row_h = 12;
    int list_y = 20;

    for (int i = 0; i < NUM_TESTS; i++)
    {
        int y = list_y + i * row_h;
        if (i == selected)
        {
            display_rect(0, y, SCR_W, row_h, COL_FG);
            display_text(8, y + row_h / 2 - 4, TESTS[i], COL_BG);
        }
        else
        {
            display_text(8, y + row_h / 2 - 4, TESTS[i], COL_FG);
        }
    }

    int result_y = list_y + NUM_TESTS * row_h + 8;
    display_rect(0, result_y - 4, SCR_W, 1, COL_FG);
    draw_wrapped(8, result_y, SCR_W - 16, result, COL_FG);

    display_text(4, SCR_H - 12, "UP/DN SELECT  A RUN  B EXIT", COL_FG);

    display_flush();
}

int main(void)
{
    display_get_size(&SCR_W, &SCR_H);

    int selected = 0;
    result[0] = 0;
    int pos = 0;
    str_append(result, &pos, "Select a test and press A");

    render(selected);

    int prev_mask = 0;
    while (1)
    {
        int mask = input_get_buttons();
        int pressed = mask & ~prev_mask;
        prev_mask = mask;

        int dirty = 0;
        if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_UP))
        {
            selected = (selected - 1 + NUM_TESTS) % NUM_TESTS;
            dirty = 1;
        }
        if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_DOWN))
        {
            selected = (selected + 1) % NUM_TESTS;
            dirty = 1;
        }
        if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_A))
        {
            result[0] = 0;
            TEST_FNS[selected]();
            dirty = 1;
        }
        if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_B))
        {
            break;
        }

        if (dirty) render(selected);
        delay(20000);
    }

    return 0;
}
