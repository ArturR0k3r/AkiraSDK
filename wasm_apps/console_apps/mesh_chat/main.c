/*
 * mesh_chat - Multi-hop group chat over AkiraMesh
 *
 * Uses the on-device AkiraMesh stack (AODV route discovery, per-hop ACK,
 * duplicate suppression) via the mesh_* SDK API — the OS owns the LoRa radio
 * and does the routing, so this app only deals with text payloads.
 *
 * Pick a node id on the setup screen (each board needs a different one), then:
 *   CHAT  : scroll the received-message log
 *     Y   -> compose a canned message; A broadcasts it, B cancels
 *     X   -> node list + mesh stats
 *     B   -> exit to supervisor
 *   NODES : discovered peers with hop count / RSSI / age; B back to chat
 *
 * Capabilities: mesh, display.write, input.read, rtc.read, app.switch
 */

#include "akira_api.h"
#include "akira_console.h"
#include <stdint.h>

/* ── Screen geometry ──────────────────────────────────────────────────── */
static int32_t DW = 320;
static int32_t DH = 240;
#define HDR_H 16
#define ROW_H 14

#define COL_BG   CONSOLE_COLOR_BG
#define COL_SEP  CONSOLE_COLOR_SEP
#define COL_TXT  CONSOLE_COLOR_TEXT
#define COL_DIM  CONSOLE_COLOR_DIM
#define COL_ACC  CONSOLE_COLOR_ACCENT
#define COL_OK   CONSOLE_COLOR_OK
#define COL_WARN CONSOLE_COLOR_WARN
#define COL_ERR  CONSOLE_COLOR_ERR
#define COL_SEL  CONSOLE_COLOR_SEL_BG

/* ── App model ────────────────────────────────────────────────────────── */
#define MSG_MAX      32
#define MSG_TEXT_LEN 48

typedef struct {
    uint8_t  from;                 /* sender short id (0 = self) */
    char     text[MSG_TEXT_LEN];   /* null-terminated */
    uint32_t ts;                   /* uptime ms when logged */
} chat_msg_t;

static chat_msg_t g_msgs[MSG_MAX];
static int        g_msg_count;     /* total ever logged (ring index base) */

/* Canned messages selectable in compose mode (no soft keyboard needed). */
static const char *const PRESETS[] = {
    "Hello mesh",
    "Ping",
    "ACK",
    "On my way",
    "Standing by",
    "SOS test",
};
#define PRESET_COUNT ((int)(sizeof(PRESETS) / sizeof(PRESETS[0])))

enum { ST_SETUP, ST_CHAT, ST_COMPOSE, ST_NODES };
static int g_state = ST_SETUP;

static int     g_node_id = 1;      /* chosen on setup screen, 1..254 */
static int     g_sel;              /* chat scroll / compose preset / node sel */
static int     g_scroll;
static uint8_t g_rx_id[AKIRA_MESH_NODE_ID_LEN];
static uint8_t g_rx_buf[AKIRA_MESH_MAX_PAYLOAD + 1];

static akira_mesh_node_t g_nodes[16];
static akira_mesh_stats_t g_stats;

/* ── Helpers ──────────────────────────────────────────────────────────── */
static int str_len(const char *s)
{
    int n = 0;
    while (s[n]) n++;
    return n;
}

static void str_copy(char *dst, const char *src, int cap)
{
    int i = 0;
    for (; i < cap - 1 && src[i]; i++) dst[i] = src[i];
    dst[i] = 0;
}

/* Minimal unsigned-to-decimal, returns pointer into the caller's buffer. */
static const char *u_str(char *buf, uint32_t v)
{
    char tmp[12];
    int i = 0;
    if (v == 0) tmp[i++] = '0';
    while (v) { tmp[i++] = (char)('0' + v % 10); v /= 10; }
    int j = 0;
    while (i) buf[j++] = tmp[--i];
    buf[j] = 0;
    return buf;
}

/* Minimal signed-to-decimal (RSSI is negative dBm). */
static const char *i_str(char *buf, int32_t v)
{
    int neg = v < 0;
    uint32_t uv = neg ? (uint32_t)(-v) : (uint32_t)v;
    char tmp[13];
    int i = 0;
    if (uv == 0) tmp[i++] = '0';
    while (uv) { tmp[i++] = (char)('0' + uv % 10); uv /= 10; }
    int j = 0;
    if (neg) buf[j++] = '-';
    while (i) buf[j++] = tmp[--i];
    buf[j] = 0;
    return buf;
}

static void hex2(char *buf, uint8_t v)
{
    const char *h = "0123456789abcdef";
    buf[0] = h[(v >> 4) & 0xF];
    buf[1] = h[v & 0xF];
    buf[2] = 0;
}

static chat_msg_t *msg_push(void)
{
    chat_msg_t *m = &g_msgs[g_msg_count % MSG_MAX];
    g_msg_count++;
    return m;
}

static int msg_visible(void)
{
    return g_msg_count < MSG_MAX ? g_msg_count : MSG_MAX;
}

/* Oldest-first index i (0..visible-1) -> ring slot. */
static chat_msg_t *msg_at(int i)
{
    int base = g_msg_count < MSG_MAX ? 0 : g_msg_count % MSG_MAX;
    return &g_msgs[(base + i) % MSG_MAX];
}

/* ── Mesh I/O ─────────────────────────────────────────────────────────── */
static void log_msg(uint8_t from, const char *text)
{
    chat_msg_t *m = msg_push();
    m->from = from;
    str_copy(m->text, text, MSG_TEXT_LEN);
    m->ts = (uint32_t)rtc_get_uptime_ms();
}

static void send_preset(int idx)
{
    const char *txt = PRESETS[idx];
    mesh_broadcast(txt, (uint32_t)str_len(txt), 3);
    log_msg(0, txt);   /* echo own message locally */
}

static void poll_rx(void)
{
    int n = mesh_recv_pop(g_rx_id, g_rx_buf, AKIRA_MESH_MAX_PAYLOAD, 0);
    if (n <= 0) return;
    if (n > MSG_TEXT_LEN - 1) n = MSG_TEXT_LEN - 1;
    g_rx_buf[n] = 0;
    log_msg(g_rx_id[AKIRA_MESH_NODE_ID_LEN - 1], (const char *)g_rx_buf);
}

/* ── Drawing ──────────────────────────────────────────────────────────── */
static void draw_hdr(const char *title)
{
    display_rect(0, 0, DW, HDR_H, COL_BG);
    display_text(4, 3, title, COL_TXT);
    char idbuf[3], line[16];
    hex2(idbuf, (uint8_t)g_node_id);
    line[0] = 'n'; line[1] = 'o'; line[2] = 'd'; line[3] = 'e'; line[4] = ' ';
    line[5] = idbuf[0]; line[6] = idbuf[1]; line[7] = 0;
    display_text(DW - DW * 16 / 100, 3, line, COL_ACC);
}

static void draw_footer(const char *hint)
{
    display_rect(0, DH - 14, DW, 14, COL_BG);
    display_text(4, DH - 11, hint, COL_DIM);
}

static void draw_setup(void)
{
    display_rect(0, 0, DW, DH, COL_BG);
    draw_hdr("MESH CHAT");
    display_text(8, HDR_H + 20, "Set this node's id:", COL_DIM);

    char idbuf[3];
    hex2(idbuf, (uint8_t)g_node_id);
    display_text_large(DW * 44 / 100, HDR_H + 44, idbuf, COL_ACC);

    display_text(8, HDR_H + 90, "Each board needs a different id.", COL_DIM);
    draw_footer("UP/DOWN id   A start   B exit");
    display_flush();
}

static void draw_chat(void)
{
    display_rect(0, HDR_H, DW, DH - HDR_H - 14, COL_BG);
    draw_hdr("CHAT");

    int rows  = (DH - HDR_H - 14) / ROW_H;
    int total = msg_visible();
    if (total == 0) {
        display_text(8, HDR_H + 20, "No messages yet.", COL_DIM);
        display_text(8, HDR_H + 36, "Press Y to broadcast one.", COL_DIM);
    }
    for (int r = 0; r < rows && (r + g_scroll) < total; r++) {
        chat_msg_t *m = msg_at(r + g_scroll);
        int y = HDR_H + r * ROW_H;
        char tag[8];
        if (m->from == 0) {
            tag[0] = 'm'; tag[1] = 'e'; tag[2] = 0;
        } else {
            char h[3];
            hex2(h, m->from);
            tag[0] = h[0]; tag[1] = h[1]; tag[2] = 0;
        }
        display_text(4, y + 3, tag, m->from == 0 ? COL_OK : COL_ACC);
        display_text(DW * 11 / 100, y + 3, m->text, COL_TXT);
    }
    draw_footer("Y send   X nodes   B exit");
    display_flush();
}

static void draw_compose(void)
{
    display_rect(0, 0, DW, DH, COL_BG);
    draw_hdr("COMPOSE");
    display_text(8, HDR_H + 16, "Select message:", COL_DIM);
    for (int i = 0; i < PRESET_COUNT; i++) {
        int y = HDR_H + 34 + i * ROW_H;
        /* mono display: selected row is inverse (white fill, black text) */
        if (i == g_sel) display_rect(0, y, DW, ROW_H, COL_TXT);
        display_text(12, y + 3, PRESETS[i], i == g_sel ? COL_BG : COL_TXT);
    }
    draw_footer("UP/DOWN pick   A broadcast   B back");
    display_flush();
}

static void draw_nodes(void)
{
    display_rect(0, 0, DW, DH, COL_BG);
    draw_hdr("NODES");

    int count = mesh_get_nodes(g_nodes, 16);
    if (count < 0) count = 0;
    mesh_get_stats(&g_stats);

    uint32_t now = (uint32_t)rtc_get_uptime_ms();
    if (count == 0) {
        display_text(8, HDR_H + 20, "No peers heard yet.", COL_DIM);
    }
    int rows = (DH - HDR_H - 28) / ROW_H;
    for (int r = 0; r < rows && (r + g_scroll) < count; r++) {
        akira_mesh_node_t *n = &g_nodes[r + g_scroll];
        int y = HDR_H + r * ROW_H;
        char idbuf[3];
        hex2(idbuf, n->node_id[AKIRA_MESH_NODE_ID_LEN - 1]);
        display_text(4, y + 3, idbuf, COL_ACC);
        display_text(DW * 9 / 100, y + 3, n->name[0] ? n->name : "-", COL_TXT);

        char buf[12], line[20];
        display_text(DW * 52 / 100, y + 3, u_str(buf, n->hop_count), COL_DIM);
        display_text(DW * 62 / 100, y + 3, i_str(buf, n->rssi), COL_TXT);

        uint32_t age = (now - n->last_seen) / 1000u;
        int p = 0;
        const char *as = u_str(buf, age);
        while (as[p]) { line[p] = as[p]; p++; }
        line[p++] = 's'; line[p] = 0;
        display_text(DW * 81 / 100, y + 3, line, COL_DIM);
    }

    /* stats footer line */
    char a[12], b[12], line[40];
    int p = 0;
    const char *s1 = u_str(a, g_stats.messages_received);
    const char *s2 = u_str(b, g_stats.messages_forwarded);
    const char pre[] = "rx ";
    for (int i = 0; pre[i]; i++) line[p++] = pre[i];
    for (int i = 0; s1[i]; i++) line[p++] = s1[i];
    const char mid[] = "  fwd ";
    for (int i = 0; mid[i]; i++) line[p++] = mid[i];
    for (int i = 0; s2[i]; i++) line[p++] = s2[i];
    line[p] = 0;
    display_rect(0, DH - 28, DW, 14, COL_BG);
    display_text(4, DH - 25, line, COL_OK);

    draw_footer("UP/DOWN scroll   B back");
    display_flush();
}

/* ── Main ─────────────────────────────────────────────────────────────── */
int main(void)
{
    display_get_size(&DW, &DH);

    int prev_buttons = 0;
    int needs_redraw = 1;
    uint32_t last_redraw = 0;

    while (1) {
        if (g_state != ST_SETUP) poll_rx();

        int btns    = input_get_buttons();
        int pressed = btns & ~prev_buttons;
        prev_buttons = btns;

        if (pressed) {
            needs_redraw = 1;
            int rows = (DH - HDR_H - 14) / ROW_H;

            if (g_state == ST_SETUP) {
                if ((pressed & AKIRA_BTN_UP)   && g_node_id < 254) g_node_id++;
                if ((pressed & AKIRA_BTN_DOWN) && g_node_id > 1)   g_node_id--;
                if (pressed & AKIRA_BTN_A) {
                    if (mesh_init(g_node_id, 0, AKIRA_MESH_ROLE_NODE, 5000) == 0 &&
                        mesh_start() == 0) {
                        g_state = ST_CHAT;
                    } else {
                        display_rect(0, HDR_H, DW, DH - HDR_H - 14, COL_BG);
                        display_text(8, HDR_H + 60, "mesh init failed", COL_ERR);
                        display_text(8, HDR_H + 76, "LR2021 required", COL_DIM);
                        display_flush();
                    }
                }
                if (pressed & AKIRA_BTN_B) { app_switch("supervisor"); return 0; }

            } else if (g_state == ST_CHAT) {
                int total = msg_visible();
                if ((pressed & AKIRA_BTN_UP)   && g_scroll > 0) g_scroll--;
                if ((pressed & AKIRA_BTN_DOWN) && g_scroll < total - rows) g_scroll++;
                if (pressed & AKIRA_BTN_Y) { g_sel = 0; g_state = ST_COMPOSE; }
                if (pressed & AKIRA_BTN_X) { g_scroll = 0; g_state = ST_NODES; }
                if (pressed & AKIRA_BTN_B) { mesh_stop(); app_switch("supervisor"); return 0; }

            } else if (g_state == ST_COMPOSE) {
                if ((pressed & AKIRA_BTN_UP)   && g_sel > 0) g_sel--;
                if ((pressed & AKIRA_BTN_DOWN) && g_sel < PRESET_COUNT - 1) g_sel++;
                if (pressed & AKIRA_BTN_A) {
                    send_preset(g_sel);
                    /* jump chat view to newest */
                    int t = msg_visible();
                    g_scroll = t > rows ? t - rows : 0;
                    g_state = ST_CHAT;
                }
                if (pressed & AKIRA_BTN_B) g_state = ST_CHAT;

            } else if (g_state == ST_NODES) {
                int count = mesh_get_nodes(g_nodes, 16);
                if (count < 0) count = 0;
                if ((pressed & AKIRA_BTN_UP)   && g_scroll > 0) g_scroll--;
                if ((pressed & AKIRA_BTN_DOWN) && g_scroll < count - 1) g_scroll++;
                if (pressed & AKIRA_BTN_B) { g_scroll = 0; g_state = ST_CHAT; }
            }
        }

        uint32_t now_ms = (uint32_t)rtc_get_uptime_ms();
        if (needs_redraw || (now_ms - last_redraw) > 200) {
            switch (g_state) {
            case ST_SETUP:   draw_setup();   break;
            case ST_CHAT:    draw_chat();    break;
            case ST_COMPOSE: draw_compose(); break;
            case ST_NODES:   draw_nodes();   break;
            default: break;
            }
            last_redraw  = now_ms;
            needs_redraw = 0;
        }

        delay(20);   /* yield between polls (matches console-app convention) */
    }
    return 0;
}
