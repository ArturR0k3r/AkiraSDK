/*
 * mesh_hub - Live mesh cockpit over AkiraMesh
 *
 * A single app that drives the on-device AkiraMesh stack (multi-hop AODV,
 * per-hop ACK, dup-suppression) through the mesh_* SDK API. The OS owns the
 * LoRa radio and the routing; this app is pure UI over application payloads.
 *
 * Screens (LEFT/RIGHT to switch):
 *   GRAPH : radial topology map — self at center, a spoke to each peer whose
 *           LENGTH encodes link RSSI (short = strong, long = weak), reference
 *           strength rings, live pulse ring on recent activity. Display is
 *           1-bit mono, so nothing is color-coded — distance/shape carry it.
 *   CHAT  : scrolling message log; Y compose (canned msgs), A broadcast.
 *   STATS : mesh counters (rx / tx / forwarded / routes / nodes).
 *
 * Setup screen picks this node's id (each board needs a distinct one).
 *
 * Screen size is queried at runtime (display_get_size) — the target is a Sharp
 * LS027B7DH01 Memory LCD (400x240 mono), not a 320x240 color panel, so nothing
 * is hardcoded to a resolution.
 *
 * Capabilities: mesh, display.write, input.read, rtc.read, app.switch
 */

#include "akira_api.h"
#include "akira_console.h"
#include <stdint.h>

/* ── Fixed metrics (font/chrome heights, resolution-independent) ───────── */
#define HDR_H 16
#define ROW_H 14
#define FOOT_H 14

/* ── Runtime screen geometry (filled by layout_init from display_get_size) */
static int32_t g_w   = 400;   /* fallback = Sharp LS027B7DH01 */
static int32_t g_h   = 240;
static int32_t g_cx;          /* graph center x */
static int32_t g_cy;          /* graph center y */
static int32_t g_rmin;        /* strongest-signal ring radius */
static int32_t g_rmax;        /* weakest-signal ring radius   */

static void layout_init(void)
{
    int32_t w = 0, h = 0;
    if (display_get_size(&w, &h) == 0 && w > 0 && h > 0) {
        g_w = w;
        g_h = h;
    }
    g_cx = g_w / 2;
    int32_t usable_h = g_h - HDR_H - FOOT_H;   /* between header and footer */
    g_cy = HDR_H + usable_h / 2;

    /* Largest radius that keeps a dot + label inside the usable area. */
    int32_t half = (g_w / 2 < usable_h / 2) ? g_w / 2 : usable_h / 2;
    g_rmax = half - 18;
    if (g_rmax < 40) g_rmax = 40;
    g_rmin = 28;
    if (g_rmin > g_rmax - 10) g_rmin = g_rmax / 2;
}

#define COL_BG   CONSOLE_COLOR_BG
#define COL_HDR  CONSOLE_COLOR_HEADER
#define COL_SEP  CONSOLE_COLOR_SEP
#define COL_TXT  CONSOLE_COLOR_TEXT
#define COL_DIM  CONSOLE_COLOR_DIM
#define COL_ACC  CONSOLE_COLOR_ACCENT
#define COL_OK   CONSOLE_COLOR_OK
#define COL_WARN CONSOLE_COLOR_WARN
#define COL_ERR  CONSOLE_COLOR_ERR
#define COL_SEL  CONSOLE_COLOR_SEL_BG

/* ── Model ────────────────────────────────────────────────────────────── */
#define MSG_MAX      32
#define MSG_TEXT_LEN 48
#define NODE_MAX     16

typedef struct {
    uint8_t  from;                 /* 0 = self */
    char     text[MSG_TEXT_LEN];
    uint32_t ts;
} chat_msg_t;

static chat_msg_t g_msgs[MSG_MAX];
static int        g_msg_count;

static const char *const PRESETS[] = {
    "Hello mesh", "Ping", "ACK", "On my way", "Standing by", "SOS test",
};
#define PRESET_COUNT ((int)(sizeof(PRESETS) / sizeof(PRESETS[0])))

enum { ST_SETUP, ST_GRAPH, ST_CHAT, ST_COMPOSE, ST_STATS };
#define SCREEN_COUNT 3   /* GRAPH, CHAT, STATS cycle with LEFT/RIGHT */
static const int SCREENS[SCREEN_COUNT] = { ST_GRAPH, ST_CHAT, ST_STATS };

static int g_state    = ST_SETUP;
static int g_screen   = 0;         /* index into SCREENS */
static int g_node_id  = 1;
static int g_sel;                  /* compose preset / chat scroll */
static int g_scroll;

static uint8_t g_rx_id[AKIRA_MESH_NODE_ID_LEN];
static uint8_t g_rx_buf[AKIRA_MESH_MAX_PAYLOAD + 1];

static akira_mesh_node_t  g_nodes[NODE_MAX];
static akira_mesh_stats_t g_stats;

/* 24-step unit circle, Q7 fixed point (-127..127). cos(a) = sin(a + 6 steps). */
static const int8_t SIN24[24] = {
    0,  33,  64,  90, 110, 123, 127, 123, 110,  90,  64,  33,
    0, -33, -64, -90,-110,-123,-127,-123,-110, -90, -64, -33,
};
static int isin(int step) { return SIN24[((step % 24) + 24) % 24]; }
static int icos(int step) { return isin(step + 6); }

/* ── Small string helpers (nostdlib) ──────────────────────────────────── */
static int str_len(const char *s) { int n = 0; while (s[n]) n++; return n; }

static void str_copy(char *dst, const char *src, int cap)
{
    int i = 0;
    for (; i < cap - 1 && src[i]; i++) dst[i] = src[i];
    dst[i] = 0;
}

static void hex2(char *buf, uint8_t v)
{
    const char *h = "0123456789abcdef";
    buf[0] = h[(v >> 4) & 0xF];
    buf[1] = h[v & 0xF];
    buf[2] = 0;
}

/* ── Chat ring buffer ─────────────────────────────────────────────────── */
static chat_msg_t *msg_push(void)
{
    chat_msg_t *m = &g_msgs[g_msg_count % MSG_MAX];
    g_msg_count++;
    return m;
}
static int msg_visible(void) { return g_msg_count < MSG_MAX ? g_msg_count : MSG_MAX; }
static chat_msg_t *msg_at(int i)
{
    int base = g_msg_count < MSG_MAX ? 0 : g_msg_count % MSG_MAX;
    return &g_msgs[(base + i) % MSG_MAX];
}

static void log_msg(uint8_t from, const char *text)
{
    chat_msg_t *m = msg_push();
    m->from = from;
    str_copy(m->text, text, MSG_TEXT_LEN);
    m->ts = (uint32_t)rtc_get_uptime_ms();
}

/* ── Mesh I/O ─────────────────────────────────────────────────────────── */
static void send_preset(int idx)
{
    const char *txt = PRESETS[idx];
    mesh_broadcast(txt, (uint32_t)str_len(txt), 3);
    log_msg(0, txt);
}

static void poll_rx(void)
{
    int n = mesh_recv_pop(g_rx_id, g_rx_buf, AKIRA_MESH_MAX_PAYLOAD, 0);
    if (n <= 0) return;
    if (n > MSG_TEXT_LEN - 1) n = MSG_TEXT_LEN - 1;
    g_rx_buf[n] = 0;
    log_msg(g_rx_id[AKIRA_MESH_NODE_ID_LEN - 1], (const char *)g_rx_buf);
}

/* Spoke length encodes link RSSI (display is 1-bit mono — no color, so signal
 * strength is shown as distance from center). Strong signal = short spoke
 * (peer sits near you); weak = long spoke (peer far out). Unknown = farthest. */
static int rssi_radius(int8_t rssi)
{
    if (rssi == 0) return g_rmax;                /* unknown → farthest */
    int s = rssi;
    if (s > -30)  s = -30;                       /* clamp very-strong */
    if (s < -120) s = -120;                      /* clamp very-weak   */
    /* -30 dBm -> g_rmin, -120 dBm -> g_rmax, linear over the 90 dB span. */
    return g_rmin + ((-30 - s) * (g_rmax - g_rmin)) / 90;
}

/* ── Chrome ───────────────────────────────────────────────────────────── */
static void draw_hdr(const char *title)
{
    display_rect(0, 0, g_w, HDR_H, COL_HDR);
    display_text(4, 3, title, COL_TXT);

    /* screen tabs on the right */
    static const char *const TABS[SCREEN_COUNT] = { "GRAPH", "CHAT", "STATS" };
    int x = g_w - 3;
    for (int i = SCREEN_COUNT - 1; i >= 0; i--) {
        int w = str_len(TABS[i]) * 6 + 6;
        x -= w;
        /* mono display: selected tab is inverse (white fill, black label) */
        if (i == g_screen) display_rect(x, 1, w, HDR_H - 2, COL_TXT);
        display_text(x + 3, 3, TABS[i], i == g_screen ? COL_BG : COL_TXT);
        x -= 2;
    }
    display_hline(0, HDR_H - 1, g_w, COL_TXT);   /* crisp header underline */
}

static void draw_footer(const char *hint)
{
    display_hline(0, g_h - FOOT_H, g_w, COL_TXT); /* crisp footer overline */
    display_rect(0, g_h - FOOT_H + 1, g_w, FOOT_H - 1, COL_HDR);
    display_text(4, g_h - 11, hint, COL_DIM);
}

/* ── Setup screen ─────────────────────────────────────────────────────── */
static void draw_setup(void)
{
    display_rect(0, 0, g_w, g_h, COL_BG);
    draw_hdr("MESH HUB");
    display_text(g_cx - 54, HDR_H + 16, "SET THIS NODE ID", COL_TXT);

    /* framed id box with up/down chevrons */
    int bw = 64, bh = 40;
    int bx = g_cx - bw / 2, by = HDR_H + 34;
    display_rounded_rect(bx, by, bw, bh, 4, COL_TXT);
    char idbuf[3];
    hex2(idbuf, (uint8_t)g_node_id);
    display_text_large(g_cx - 14, by + 12, idbuf, COL_TXT);
    /* up chevron above, down chevron below */
    display_triangle(g_cx, by - 12, g_cx - 6, by - 4, g_cx + 6, by - 4, COL_TXT);
    display_triangle(g_cx, by + bh + 12, g_cx - 6, by + bh + 4, g_cx + 6, by + bh + 4, COL_TXT);

    display_text(g_cx - 84, by + bh + 24, "each board needs a distinct id", COL_DIM);
    draw_footer("UP/DOWN id   A start   B exit");
    display_flush();
}

/* ── Graph screen ─────────────────────────────────────────────────────── */
static void draw_graph(void)
{
    display_rect(0, HDR_H, g_w, g_h - HDR_H - FOOT_H, COL_BG);
    draw_hdr("GRAPH");

    int count = mesh_get_nodes(g_nodes, NODE_MAX);
    if (count < 0) count = 0;
    uint32_t now = (uint32_t)rtc_get_uptime_ms();

    /* Radar backdrop: signal-strength rings (inner=strong, outer=weak) plus
     * faint N/E/S/W axis ticks from the center out to the outer ring. */
    display_circle(g_cx, g_cy, g_rmin, COL_SEP);
    display_circle(g_cx, g_cy, (g_rmin + g_rmax) / 2, COL_SEP);
    display_circle(g_cx, g_cy, g_rmax, COL_SEP);
    display_hline(g_cx - g_rmax, g_cy, g_rmax * 2, COL_SEP);
    display_vline(g_cx, g_cy - g_rmax, g_rmax * 2, COL_SEP);
    display_text(g_cx + 2, g_cy - g_rmin - 9, "near", COL_DIM);
    display_text(g_cx + 2, g_cy - g_rmax + 1, "far",  COL_DIM);

    /* spokes + peer dots — spoke length = RSSI (closer dot = stronger link) */
    for (int i = 0; i < count; i++) {
        akira_mesh_node_t *n = &g_nodes[i];
        int r    = rssi_radius(n->rssi);
        int step = (count > 0) ? (i * 24) / count : 0;
        int px = g_cx + (r * icos(step)) / 127;
        int py = g_cy + (r * isin(step)) / 127;

        display_line(g_cx, g_cy, px, py, COL_TXT);

        /* pulse ring if heard in the last 2s */
        if ((now - n->last_seen) < 2000u)
            display_circle(px, py, 10, COL_TXT);

        /* fixed white dot with black id inside (only legible mono combo) */
        display_circle_fill(px, py, 7, COL_TXT);
        char idbuf[3];
        hex2(idbuf, n->node_id[AKIRA_MESH_NODE_ID_LEN - 1]);
        display_text(px - 5, py - 3, idbuf, COL_BG);
    }

    /* self at center — filled dot + outer ring marks "you are here" */
    display_circle_fill(g_cx, g_cy, 8, COL_TXT);
    display_circle(g_cx, g_cy, 12, COL_TXT);
    char me[3];
    hex2(me, (uint8_t)g_node_id);
    display_text(g_cx - 5, g_cy - 3, me, COL_BG);

    /* boxed peer-count badge, top-left */
    display_rect_outline(4, HDR_H + 3, 58, 13, COL_TXT);
    display_text(8, HDR_H + 5, "peers", COL_DIM);
    display_number(46, HDR_H + 5, count, COL_TXT);

    draw_footer("LEFT/RIGHT screen   B exit");
    display_flush();
}

/* ── Chat screen ──────────────────────────────────────────────────────── */
static void draw_chat(void)
{
    display_rect(0, HDR_H, g_w, g_h - HDR_H - FOOT_H, COL_BG);
    draw_hdr("CHAT");

    int rows  = (g_h - HDR_H - FOOT_H) / ROW_H;
    int total = msg_visible();
    if (total == 0) {
        display_text(8, HDR_H + 20, "No messages yet.", COL_DIM);
        display_text(8, HDR_H + 36, "Press Y to broadcast one.", COL_DIM);
    }
    /* message bubbles: own msgs right-aligned, peers left-aligned with id tag */
    for (int r = 0; r < rows && (r + g_scroll) < total; r++) {
        chat_msg_t *m = msg_at(r + g_scroll);
        int y  = HDR_H + 1 + r * ROW_H;
        int tw = str_len(m->text) * 6;
        int bw = tw + 10;
        int maxw = g_w - 44;
        if (bw > maxw) bw = maxw;

        if (m->from == 0) {
            int bx = g_w - bw - 4;
            display_rounded_rect(bx, y, bw, ROW_H - 2, 3, COL_TXT);
            display_text(bx + 5, y + 2, m->text, COL_TXT);
        } else {
            char tag[3];
            hex2(tag, m->from);
            display_text(2, y + 2, tag, COL_ACC);
            int bx = 20;
            display_rounded_rect(bx, y, bw, ROW_H - 2, 3, COL_TXT);
            display_text(bx + 5, y + 2, m->text, COL_TXT);
        }
    }
    draw_footer("Y send   LEFT/RIGHT screen   B exit");
    display_flush();
}

static void draw_compose(void)
{
    display_rect(0, 0, g_w, g_h, COL_BG);
    draw_hdr("COMPOSE");
    display_text(8, HDR_H + 16, "Select message:", COL_DIM);
    for (int i = 0; i < PRESET_COUNT; i++) {
        int y = HDR_H + 34 + i * ROW_H;
        /* mono display: selected row is inverse (white fill, black text) */
        if (i == g_sel) display_rect(0, y, g_w, ROW_H, COL_TXT);
        display_text(12, y + 3, PRESETS[i], i == g_sel ? COL_BG : COL_TXT);
    }
    draw_footer("UP/DOWN pick   A broadcast   B back");
    display_flush();
}

/* ── Stats screen ─────────────────────────────────────────────────────── */
/* One row: label, a proportional bar (relative to the largest counter), value. */
static void stat_row(int y, const char *label, uint32_t v, uint32_t vmax)
{
    display_text(14, y, label, COL_TXT);
    int bx  = 150;
    int bw  = g_w - bx - 52;
    display_rect_outline(bx, y, bw, 8, COL_SEP);          /* bar track */
    int fill = (vmax > 0) ? (int)((uint64_t)v * (bw - 2) / vmax) : 0;
    if (fill > 0) display_rect(bx + 1, y + 1, fill, 6, COL_TXT);  /* bar fill */
    display_number(g_w - 46, y, (int32_t)v, COL_TXT);     /* value */
}

static void draw_stats(void)
{
    display_rect(0, HDR_H, g_w, g_h - HDR_H - FOOT_H, COL_BG);
    draw_hdr("STATS");
    mesh_get_stats(&g_stats);

    uint32_t vals[6] = {
        g_stats.nodes_discovered, g_stats.messages_received, g_stats.messages_sent,
        g_stats.messages_forwarded, g_stats.routes_active, g_stats.apps_distributed,
    };
    uint32_t vmax = 1;
    for (int i = 0; i < 6; i++) if (vals[i] > vmax) vmax = vals[i];

    /* framed panel */
    display_rect_outline(6, HDR_H + 5, g_w - 12, g_h - HDR_H - FOOT_H - 10, COL_TXT);

    static const char *const LBL[6] = {
        "Nodes discovered", "Messages received", "Messages sent",
        "Messages forwarded", "Active routes", "Apps distributed",
    };
    int y = HDR_H + 14;
    for (int i = 0; i < 6; i++) { stat_row(y, LBL[i], vals[i], vmax); y += ROW_H + 6; }

    draw_footer("LEFT/RIGHT screen   B exit");
    display_flush();
}

/* ── Main ─────────────────────────────────────────────────────────────── */
static void redraw(int state)
{
    switch (state) {
    case ST_SETUP:   draw_setup();   break;
    case ST_GRAPH:   draw_graph();   break;
    case ST_CHAT:    draw_chat();    break;
    case ST_COMPOSE: draw_compose(); break;
    case ST_STATS:   draw_stats();   break;
    default: break;
    }
}

int main(void)
{
    int prev_buttons = 0;
    int needs_redraw = 1;
    uint32_t last_redraw = 0;

    layout_init();

    while (1) {
        if (g_state != ST_SETUP) poll_rx();

        int btns    = input_get_buttons();
        int pressed = btns & ~prev_buttons;
        prev_buttons = btns;

        if (pressed) {
            needs_redraw = 1;
            int rows = (g_h - HDR_H - FOOT_H) / ROW_H;

            if (g_state == ST_SETUP) {
                if ((pressed & AKIRA_BTN_UP)   && g_node_id < 254) g_node_id++;
                if ((pressed & AKIRA_BTN_DOWN) && g_node_id > 1)   g_node_id--;
                if (pressed & AKIRA_BTN_A) {
                    if (mesh_init(g_node_id, 0, AKIRA_MESH_ROLE_NODE, 5000) == 0 &&
                        mesh_start() == 0) {
                        g_screen = 0;
                        g_state  = ST_GRAPH;
                    } else {
                        display_rect(0, HDR_H, g_w, g_h - HDR_H - FOOT_H, COL_BG);
                        display_text(8, HDR_H + 60, "mesh init failed", COL_ERR);
                        display_text(8, HDR_H + 76, "LR2021 required", COL_DIM);
                        display_flush();
                    }
                }
                if (pressed & AKIRA_BTN_B) { app_switch("supervisor"); return 0; }

            } else if (g_state == ST_COMPOSE) {
                if ((pressed & AKIRA_BTN_UP)   && g_sel > 0) g_sel--;
                if ((pressed & AKIRA_BTN_DOWN) && g_sel < PRESET_COUNT - 1) g_sel++;
                if (pressed & AKIRA_BTN_A) {
                    send_preset(g_sel);
                    int t = msg_visible();
                    g_scroll = t > rows ? t - rows : 0;
                    g_state = ST_CHAT;
                }
                if (pressed & AKIRA_BTN_B) g_state = ST_CHAT;

            } else {
                /* GRAPH / CHAT / STATS shared nav */
                if (pressed & AKIRA_BTN_RIGHT) {
                    g_screen = (g_screen + 1) % SCREEN_COUNT;
                    g_state  = SCREENS[g_screen];
                    g_scroll = 0;
                }
                if (pressed & AKIRA_BTN_LEFT) {
                    g_screen = (g_screen + SCREEN_COUNT - 1) % SCREEN_COUNT;
                    g_state  = SCREENS[g_screen];
                    g_scroll = 0;
                }
                if (pressed & AKIRA_BTN_B) { mesh_stop(); app_switch("supervisor"); return 0; }

                if (g_state == ST_CHAT) {
                    int total = msg_visible();
                    if ((pressed & AKIRA_BTN_UP)   && g_scroll > 0) g_scroll--;
                    if ((pressed & AKIRA_BTN_DOWN) && g_scroll < total - rows) g_scroll++;
                    if (pressed & AKIRA_BTN_Y) { g_sel = 0; g_state = ST_COMPOSE; }
                }
            }
        }

        uint32_t now_ms = (uint32_t)rtc_get_uptime_ms();
        /* graph animates (pulses), so refresh it faster than the static screens */
        uint32_t period = (g_state == ST_GRAPH) ? 300u : 500u;
        if (needs_redraw || (now_ms - last_redraw) > period) {
            redraw(g_state);
            last_redraw  = now_ms;
            needs_redraw = 0;
        }

        delay(20);
    }
    return 0;
}
