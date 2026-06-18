/*
 * Copyright (c) 2026 PenEngineering S.R.L
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file main.c
 * @brief akira.badge — DEF CON / conference badge mode
 *
 * Three-screen badge application:
 *
 *   IDENTITY  — displays handle (large), name, org, role, identicon
 *               fingerprint, and BLE peer count.
 *
 *   PEERS     — scrollable list of recently seen badges exchanged via BLE.
 *               Each badge app advertises its handle; when a peer connects
 *               and writes a card-exchange characteristic we add them here.
 *
 *   CRYPTO    — crypto challenge display: DJB2 fingerprint of the handle,
 *               XOR-masked challenge value, and a visual identicon.
 *
 * Identity fields are read from NVS settings (set once via shell or settings app):
 *   badge/handle  — callsign, max 16 chars  (default: "UNKNOWN")
 *   badge/name    — real name,  max 32 chars (default: "")
 *   badge/org     — org/group,  max 16 chars (default: "AKIRA")
 *   badge/role    — role label, max 16 chars (default: "HACKER")
 *
 * BLE service UUID (128-bit): AKBA0000-DEAD-BEEF-CAFE-BABE00000001
 * Card-exchange characteristic: AKBA0001-DEAD-BEEF-CAFE-BABE00000001
 *   Properties: WRITE | NOTIFY
 *   Payload (32 bytes): [0-15] handle, [16-23] org, [24-27] fp_hash LE,
 *                       [28-31] reserved
 *
 * Controls:
 *   LEFT / RIGHT   switch screen
 *   UP / DOWN      scroll peer list (PEERS screen)
 *   A              exchange card with nearest peer (BLE write)
 *   B              back / cancel
 *   Y              reset peer list
 */

#include "akira_api.h"

/* ── Display constants ────────────────────────────────────────────────── */
#define SCR_W   320
#define SCR_H   240
#define HDR_H    20
#define FTR_Y   220
#define FTR_H    20
#define BODY_Y   HDR_H
#define BODY_H  (FTR_Y - HDR_H)   /* 200 px */

/* ── Colors ───────────────────────────────────────────────────────────── */
#define COL_BG       0x0000
#define COL_HDR      0x1082
#define COL_FTR      0x0841
#define COL_SEP      0x2945
#define COL_TITLE    0x07FF   /* cyan */
#define COL_WHITE    0xFFFF
#define COL_GRAY     0x7BEF
#define COL_DIM      0x39E7
#define COL_YELLOW   0xFFE0
#define COL_GREEN    0x07E0
#define COL_RED      0xF800
#define COL_ORANGE   0xFD20
#define COL_BLUE     0x001F
#define COL_MAGENTA  0xF81F
#define COL_PURPLE   0x801F
#define COL_BORDER   0x4208

/* ── Screen IDs ───────────────────────────────────────────────────────── */
#define SCREEN_IDENTITY  0
#define SCREEN_PEERS     1
#define SCREEN_CRYPTO    2
#define N_SCREENS        3

/* ── Peer list ────────────────────────────────────────────────────────── */
#define MAX_PEERS  8

typedef struct {
    char     handle[17];
    char     org[9];
    uint32_t fp;
    int      active;
    int32_t  seen_ticks;  /* timer_elapsed value when last seen */
} peer_t;

/* ── Global state ─────────────────────────────────────────────────────── */
static char   g_handle[17] = "UNKNOWN";
static char   g_name[33]   = "";
static char   g_org[17]    = "AKIRA";
static char   g_role[17]   = "HACKER";
static uint32_t g_fp       = 0;

static int     cur_screen  = SCREEN_IDENTITY;
static int     peer_scroll = 0;
static int     ble_ok      = 0;
static int     ble_connected = 0;

static peer_t  peers[MAX_PEERS];
static int     peer_count  = 0;

static int     ble_svc_h   = -1;
static int     ble_char_h  = -1;

static int32_t uptime_timer = -1;

static uint32_t prev_btns  = 0;

/* ── Identicon palette (8 badge colors derived from fp bits) ──────────── */
static const uint16_t BADGE_COLORS[8] = {
    0x07FF,  /* cyan    */
    0x07E0,  /* green   */
    0xFFE0,  /* yellow  */
    0xFD20,  /* orange  */
    0xF800,  /* red     */
    0xF81F,  /* magenta */
    0x801F,  /* purple  */
    0x001F,  /* blue    */
};

/* ── String helpers ───────────────────────────────────────────────────── */
static char *str_put(char *d, const char *s)
{
    while (*s) *d++ = *s++;
    *d = '\0';
    return d;
}

static char *int_to_buf(char *p, int32_t v)
{
    if (v < 0) { *p++ = '-'; v = -v; }
    char tmp[12]; int n = 0;
    if (v == 0) { *p++ = '0'; *p = '\0'; return p; }
    while (v > 0) { tmp[n++] = '0' + (int)(v % 10); v /= 10; }
    while (n > 0) *p++ = tmp[--n];
    *p = '\0';
    return p;
}

static char *uint_to_buf(char *p, uint32_t v)
{
    char tmp[12]; int n = 0;
    if (v == 0) { *p++ = '0'; *p = '\0'; return p; }
    while (v > 0) { tmp[n++] = '0' + (int)(v % 10); v /= 10; }
    while (n > 0) *p++ = tmp[--n];
    *p = '\0';
    return p;
}

static void hex_byte(char *p, uint8_t b)
{
    static const char h[] = "0123456789ABCDEF";
    p[0] = h[b >> 4]; p[1] = h[b & 0xF]; p[2] = '\0';
}

static char *uint32_to_hex(char *p, uint32_t v)
{
    char tmp[3];
    hex_byte(tmp, (uint8_t)(v >> 24)); p = str_put(p, tmp);
    hex_byte(tmp, (uint8_t)(v >> 16)); p = str_put(p, tmp);
    hex_byte(tmp, (uint8_t)(v >>  8)); p = str_put(p, tmp);
    hex_byte(tmp, (uint8_t)(v >>  0)); p = str_put(p, tmp);
    return p;
}

static int str_len(const char *s)
{
    int n = 0; while (s[n]) n++; return n;
}

static void str_copy_n(char *dst, const char *src, int max)
{
    int i = 0;
    while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static int str_eq(const char *a, const char *b)
{
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

/* ── DJB2 hash (fingerprint) ──────────────────────────────────────────── */
static uint32_t djb2(const char *s)
{
    uint32_t h = 5381;
    while (*s) { h = ((h << 5) + h) ^ (uint8_t)*s++; }
    return h;
}

/* ── Draw identicon (8x8 mirrored, cell_px size) ─────────────────────── */
static void draw_identicon(int cx, int cy, int cell_px, uint32_t hash)
{
    uint16_t fg = BADGE_COLORS[(hash >> 24) & 0x7];
    uint16_t bg = 0x0841;  /* very dark badge background */

    /* Background square */
    display_rect(cx - 4 * cell_px, cy - 4 * cell_px,
                 8 * cell_px, 8 * cell_px, bg);

    /* 8 rows x 4 cols, each bit in hash -> filled or not, mirrored */
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 4; col++) {
            int bit = (row * 4 + col);
            if ((hash >> bit) & 1U) {
                int x_lo = cx - 4 * cell_px + col * cell_px;
                int x_hi = cx + (3 - col) * cell_px;
                int y0   = cy - 4 * cell_px + row * cell_px;
                display_rect(x_lo, y0, cell_px - 1, cell_px - 1, fg);
                display_rect(x_hi, y0, cell_px - 1, cell_px - 1, fg);
            }
        }
    }

    /* Outer border */
    display_rect_outline(cx - 4 * cell_px, cy - 4 * cell_px,
                         8 * cell_px, 8 * cell_px, fg);
}

/* ── Draw standard header ─────────────────────────────────────────────── */
static void draw_header(const char *title)
{
    display_rect(0, 0, SCR_W, HDR_H, COL_HDR);
    display_rect(0, HDR_H - 1, SCR_W, 1, COL_SEP);

    /* Screen tab indicators */
    static const char *tabs[] = { "ID", "PEERS", "CRYPTO" };
    int tx = 4;
    for (int i = 0; i < N_SCREENS; i++) {
        uint16_t c = (i == cur_screen) ? COL_TITLE : COL_DIM;
        display_text(tx, 5, tabs[i], c);
        tx += str_len(tabs[i]) * 7 + 8;
    }

    /* BLE indicator (right side) */
    if (ble_ok) {
        char blebuf[8];
        char *p = str_put(blebuf, "BLE");
        if (ble_connected) { p = str_put(p, ":"); uint_to_buf(p, (uint32_t)peer_count); }
        display_text(SCR_W - 50, 5, blebuf, ble_connected ? COL_GREEN : COL_DIM);
    }
    (void)title;
}

/* ── Draw footer ──────────────────────────────────────────────────────── */
static void draw_footer(const char *hint)
{
    display_rect(0, FTR_Y, SCR_W, FTR_H, COL_FTR);
    display_rect(0, FTR_Y, SCR_W, 1, COL_SEP);
    display_text(4, FTR_Y + 5, hint, COL_DIM);
}

/* ── Animated border for identity screen ─────────────────────────────── */
static void draw_badge_border(uint16_t color)
{
    /* Dashed decorative border inside the content area */
    int x0 = 2, y0 = BODY_Y + 2;
    int w  = SCR_W - 4, h = BODY_H - 4;
    display_rect_outline(x0, y0, w, h, color);
    display_rect_outline(x0 + 2, y0 + 2, w - 4, h - 4, COL_BORDER);

    /* Corner accents */
    display_rect(x0, y0, 8, 2, color);
    display_rect(x0, y0, 2, 8, color);
    display_rect(x0 + w - 8, y0, 8, 2, color);
    display_rect(x0 + w - 2, y0, 2, 8, color);
    display_rect(x0, y0 + h - 2, 8, 2, color);
    display_rect(x0, y0 + h - 8, 2, 8, color);
    display_rect(x0 + w - 8, y0 + h - 2, 8, 2, color);
    display_rect(x0 + w - 2, y0 + h - 8, 2, 8, color);
}

/* ── IDENTITY screen ──────────────────────────────────────────────────── */
static void render_identity(void)
{
    display_rect(0, BODY_Y, SCR_W, BODY_H, COL_BG);

    uint16_t accent = BADGE_COLORS[(g_fp >> 24) & 0x7];
    draw_badge_border(accent);

    /* Identicon — left side, vertically centered */
    int ico_x  = 48;
    int ico_y  = BODY_Y + BODY_H / 2;
    draw_identicon(ico_x, ico_y, 7, g_fp);  /* 7px cell -> 56x56 */

    /* Handle (large text, right of identicon) */
    int text_x = 110;
    display_text_large(text_x, BODY_Y + 20, g_handle, accent);

    /* Name */
    if (str_len(g_name) > 0)
        display_text(text_x, BODY_Y + 46, g_name, COL_WHITE);

    /* Org / role */
    {
        char buf[36];
        char *p = str_put(buf, g_org);
        if (str_len(g_role) > 0) { p = str_put(p, " / "); str_put(p, g_role); }
        display_text(text_x, BODY_Y + 62, buf, COL_GRAY);
    }

    /* Separator */
    display_rect(text_x, BODY_Y + 78, SCR_W - text_x - 8, 1, COL_SEP);

    /* Fingerprint */
    {
        char buf[24];
        char *p = str_put(buf, "KEY:");
        uint32_to_hex(p, g_fp);
        display_text(text_x, BODY_Y + 84, buf, COL_DIM);
    }

    /* Challenge hint */
    {
        char buf[24];
        char *p = str_put(buf, "CHG:");
        uint32_to_hex(p, g_fp ^ 0xDEADBEEFU);
        display_text(text_x, BODY_Y + 96, buf, COL_DIM);
    }

    /* Peer count */
    {
        char buf[32];
        char *p = str_put(buf, "PEERS: ");
        p = uint_to_buf(p, (uint32_t)peer_count);
        str_put(p, ble_ok ? " (BLE ON)" : " (BLE OFF)");
        display_text(text_x, BODY_Y + 116, buf, COL_GRAY);
    }

    /* "AKIRA BADGE" watermark bottom-right */
    display_text(SCR_W - 88, FTR_Y - 14, "AKIRA BADGE", COL_BORDER);
}

/* ── PEERS screen ─────────────────────────────────────────────────────── */
#define PEER_ROW_H  22
#define PEER_VISIBLE ((BODY_H - 4) / PEER_ROW_H)  /* ~9 */

static void render_peers(void)
{
    display_rect(0, BODY_Y, SCR_W, BODY_H, COL_BG);

    if (peer_count == 0) {
        display_text(SCR_W / 2 - 56, BODY_Y + BODY_H / 2 - 8,
                     "No peers seen yet.", COL_DIM);
        display_text(SCR_W / 2 - 72, BODY_Y + BODY_H / 2 + 8,
                     "BLE scanning for badges...", COL_DIM);
        return;
    }

    int visible = PEER_VISIBLE;
    int max_scroll = peer_count - visible;
    if (max_scroll < 0) max_scroll = 0;
    if (peer_scroll > max_scroll) peer_scroll = max_scroll;

    for (int i = 0; i < visible && (peer_scroll + i) < peer_count; i++) {
        peer_t *p = &peers[peer_scroll + i];
        int ry = BODY_Y + 4 + i * PEER_ROW_H;

        uint16_t accent = BADGE_COLORS[(p->fp >> 24) & 0x7];

        /* Bullet */
        display_circle_fill(8, ry + 8, 4, accent);

        /* Handle */
        display_text(18, ry + 4, p->handle, COL_WHITE);

        /* Org */
        if (str_len(p->org) > 0)
            display_text(140, ry + 4, p->org, COL_GRAY);

        /* Fingerprint (short) */
        char fpbuf[12];
        char *fp_p = str_put(fpbuf, "#");
        hex_byte(fp_p, (uint8_t)(p->fp >> 24)); fp_p += 2;
        hex_byte(fp_p, (uint8_t)(p->fp >> 16));
        display_text(230, ry + 4, fpbuf, COL_DIM);

        /* Separator */
        if (i < visible - 1)
            display_rect(8, ry + PEER_ROW_H - 1, SCR_W - 16, 1, COL_SEP);
    }

    /* Scroll indicator */
    if (peer_count > visible) {
        char sbuf[8];
        char *sp = uint_to_buf(sbuf, (uint32_t)(peer_scroll + 1));
        str_put(sp, "/");
        /* can't concat easily, just show row */
        display_text(SCR_W - 32, BODY_Y + 4, sbuf, COL_DIM);
    }
}

/* ── CRYPTO screen ────────────────────────────────────────────────────── */
static void render_crypto(void)
{
    display_rect(0, BODY_Y, SCR_W, BODY_H, COL_BG);

    /* Large identicon centered */
    int ico_cx = SCR_W / 2;
    int ico_cy = BODY_Y + 78;
    draw_identicon(ico_cx, ico_cy, 8, g_fp);  /* 8px cell -> 64x64 */

    /* Title */
    display_text(SCR_W / 2 - 56, BODY_Y + 6, "CRYPTO CHALLENGE", COL_TITLE);
    display_rect(0, BODY_Y + 18, SCR_W, 1, COL_SEP);

    /* Challenge value */
    {
        char buf[24];
        char *p = str_put(buf, "CHALLENGE: ");
        uint32_to_hex(p, g_fp ^ 0xDEADBEEFU);
        display_text(8, BODY_Y + 22, buf, COL_YELLOW);
    }

    /* Algorithm hint */
    display_text(8, BODY_Y + 34, "ALGO: djb2(handle) XOR 0xDEADBEEF", COL_DIM);

    /* Fingerprint (below identicon) */
    display_text(SCR_W / 2 - 56, ico_cy + 44, "FINGERPRINT:", COL_GRAY);
    {
        char buf[12];
        uint32_to_hex(buf, g_fp);
        display_text(SCR_W / 2 - 28, ico_cy + 56, buf, COL_WHITE);
    }

    /* Handle reminder */
    {
        char buf[24];
        char *p = str_put(buf, "HANDLE: ");
        str_put(p, g_handle);
        display_text(8, FTR_Y - 20, buf, COL_GRAY);
    }
}

/* ── Render current screen ────────────────────────────────────────────── */
static void render_screen(void)
{
    draw_header(0);

    switch (cur_screen) {
    case SCREEN_IDENTITY:
        render_identity();
        draw_footer("L/R:SCREEN  A:EXCHANGE  Y:REFRESH");
        break;
    case SCREEN_PEERS:
        render_peers();
        draw_footer("U/D:SCROLL  L/R:SCREEN  Y:CLEAR");
        break;
    case SCREEN_CRYPTO:
        render_crypto();
        draw_footer("L/R:SCREEN  A:SHOW-HINT");
        break;
    }

    display_flush();
}

/* ── Encode card-exchange packet ──────────────────────────────────────── */
static void make_card_packet(uint8_t pkt[32])
{
    int i;
    /* [0-15] handle */
    for (i = 0; i < 16; i++) pkt[i] = (i < str_len(g_handle)) ? (uint8_t)g_handle[i] : 0;
    /* [16-23] org */
    for (i = 0; i < 8; i++)  pkt[16+i] = (i < str_len(g_org)) ? (uint8_t)g_org[i] : 0;
    /* [24-27] fingerprint LE */
    pkt[24] = (uint8_t)(g_fp & 0xFF);
    pkt[25] = (uint8_t)((g_fp >> 8) & 0xFF);
    pkt[26] = (uint8_t)((g_fp >> 16) & 0xFF);
    pkt[27] = (uint8_t)((g_fp >> 24) & 0xFF);
    /* [28-31] reserved */
    pkt[28] = pkt[29] = pkt[30] = pkt[31] = 0;
}

/* ── Parse incoming card-exchange packet ──────────────────────────────── */
static void parse_card_packet(const uint8_t *pkt, int len)
{
    if (len < 28) return;

    char  peer_handle[17];
    char  peer_org[9];
    uint32_t peer_fp;

    /* Extract fields */
    int i;
    for (i = 0; i < 16; i++) peer_handle[i] = (char)pkt[i];
    peer_handle[16] = '\0';
    for (i = 0; i < 8; i++) peer_org[i] = (char)pkt[16 + i];
    peer_org[8] = '\0';
    peer_fp = (uint32_t)pkt[24]
            | ((uint32_t)pkt[25] << 8)
            | ((uint32_t)pkt[26] << 16)
            | ((uint32_t)pkt[27] << 24);

    /* Skip self */
    if (str_eq(peer_handle, g_handle)) return;

    /* Update existing entry or add new */
    int32_t now_ms = (uptime_timer >= 0) ? timer_elapsed(uptime_timer) : 0;
    for (i = 0; i < peer_count; i++) {
        if (str_eq(peers[i].handle, peer_handle)) {
            str_copy_n(peers[i].org, peer_org, sizeof(peers[i].org));
            peers[i].fp          = peer_fp;
            peers[i].seen_ticks  = now_ms;
            peers[i].active      = 1;
            return;
        }
    }

    /* New peer — add to list */
    if (peer_count < MAX_PEERS) {
        peer_t *p = &peers[peer_count++];
        str_copy_n(p->handle, peer_handle, sizeof(p->handle));
        str_copy_n(p->org,    peer_org,    sizeof(p->org));
        p->fp         = peer_fp;
        p->seen_ticks = now_ms;
        p->active     = 1;
    }
}

/* ── BLE init ─────────────────────────────────────────────────────────── */
static void init_ble(void)
{
    /* Create badge exchange service */
    ble_svc_h = ble_service_create(
        "AKBA0000-DEAD-BEEF-CAFE-BABE00000001");
    if (ble_svc_h < 0) return;

    ble_char_h = ble_char_create(
        "AKBA0001-DEAD-BEEF-CAFE-BABE00000001",
        BLE_PROP_READ | BLE_PROP_WRITE | BLE_PROP_NOTIFY,
        32);
    if (ble_char_h < 0) return;

    ble_service_add_char(ble_svc_h, ble_char_h);
    ble_add_service(ble_svc_h);

    /* Advertise as the handle name so others can see us by scanning */
    ble_set_local_name(g_handle);
    ble_set_advertised_service(ble_svc_h);

    if (ble_init() == 0 && ble_advertise() == 0)
        ble_ok = 1;
}

/* ── Poll BLE events ──────────────────────────────────────────────────── */
static int poll_ble(void)
{
    uint8_t buf[68];
    int evt = ble_event_pop(buf, sizeof(buf));
    if (evt <= 0) return 0;

    switch (evt) {
    case BLE_EVT_CONNECTED:
        ble_connected = 1;
        /* Send our card as soon as peer connects */
        if (ble_char_h >= 0) {
            uint8_t pkt[32];
            make_card_packet(pkt);
            ble_char_write(ble_char_h, pkt, 32);
        }
        break;

    case BLE_EVT_DISCONNECTED:
        ble_connected = 0;
        break;

    case BLE_EVT_CHAR_WRITTEN: {
        int data_len = (int)buf[2] | ((int)buf[3] << 8);
        if (data_len > 0 && data_len <= 32)
            parse_card_packet(buf + 4, data_len);
        break;
    }

    default:
        break;
    }

    return 1;  /* event was processed */
}

/* ── Handle button input ──────────────────────────────────────────────── */
static void handle_buttons(int *need_redraw)
{
    uint32_t btns    = (uint32_t)input_get_buttons();
    uint32_t pressed = btns & ~prev_btns;
    prev_btns = btns;

    if (!pressed) return;

    if (pressed & AKIRA_BTN_RIGHT) {
        cur_screen = (cur_screen + 1) % N_SCREENS;
        *need_redraw = 1;
    }
    if (pressed & AKIRA_BTN_LEFT) {
        cur_screen = (cur_screen + N_SCREENS - 1) % N_SCREENS;
        *need_redraw = 1;
    }

    if (cur_screen == SCREEN_PEERS) {
        if (pressed & AKIRA_BTN_DOWN) {
            if (peer_scroll < peer_count - PEER_VISIBLE)
                peer_scroll++;
            *need_redraw = 1;
        }
        if (pressed & AKIRA_BTN_UP) {
            if (peer_scroll > 0) peer_scroll--;
            *need_redraw = 1;
        }
        if (pressed & AKIRA_BTN_Y) {
            peer_count  = 0;
            peer_scroll = 0;
            *need_redraw = 1;
        }
    }

    if (pressed & AKIRA_BTN_Y && cur_screen == SCREEN_IDENTITY) {
        /* Refresh identity from settings */
        settings_get("badge/handle", g_handle, sizeof(g_handle));
        settings_get("badge/name",   g_name,   sizeof(g_name));
        settings_get("badge/org",    g_org,    sizeof(g_org));
        settings_get("badge/role",   g_role,   sizeof(g_role));
        g_fp = djb2(g_handle);
        *need_redraw = 1;
    }

    if (pressed & AKIRA_BTN_A && ble_ok && ble_connected) {
        /* Push card to connected peer */
        uint8_t pkt[32];
        make_card_packet(pkt);
        ble_char_write(ble_char_h, pkt, 32);
    }
}

/* ── Entry point ──────────────────────────────────────────────────────── */
int main(void)
{
    printf("akira.badge v1.0");

    /* Load identity from persistent settings */
    settings_get("badge/handle", g_handle, sizeof(g_handle));
    settings_get("badge/name",   g_name,   sizeof(g_name));
    settings_get("badge/org",    g_org,    sizeof(g_org));
    settings_get("badge/role",   g_role,   sizeof(g_role));

    /* Compute fingerprint */
    g_fp = djb2(g_handle);

    /* Start uptime timer for peer age tracking */
    uptime_timer = timer_create();
    if (uptime_timer >= 0) timer_start(uptime_timer);

    /* Init BLE */
    init_ble();

    /* Initial render */
    display_clear(COL_BG);
    render_screen();

    int ble_poll_counter = 0;

    while (1) {
        int need_redraw = 0;

        handle_buttons(&need_redraw);

        /* Poll BLE events every ~5 loop iterations */
        if (ble_ok) {
            ble_poll_counter++;
            if (ble_poll_counter >= 5) {
                ble_poll_counter = 0;
                if (poll_ble()) need_redraw = 1;
            }
        }

        if (need_redraw)
            render_screen();

        delay(20000);  /* ~50 Hz event loop */
    }

    return 0;
}
