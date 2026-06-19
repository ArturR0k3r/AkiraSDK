/*
 * lora_meshtastic - Meshtastic-compatible LoRa node for AkiraOS
 *
 * Joins the Meshtastic open mesh network (100k+ devices).
 * Sends/receives TEXT and POSITION packets interoperably with
 * T-Beam, Heltec, RAK, and other Meshtastic hardware.
 *
 * Radio (LR2021): LongFast US — SF10, BW=250kHz, CR=4/8, 906.875MHz
 * Encryption:     AES-256-CTR, key from NVS "mesh/ch0/key"
 *                 Default LongFast PSK "AQ==" → {0x01, 0x00×31}
 * Protobuf:       Minimal inline encoder/decoder (no external lib)
 *
 * Capabilities: display.write, input.read, settings.*, rtc.read,
 *               rf.transceive, crypto, ipc, app.switch
 */

#include "akira_api.h"
#include <stdint.h>

/* ── Radio configuration ──────────────────────────────────────────────── */

#define MESH_FREQ_HZ        906875000u
#define MESH_SF             10
#define MESH_BW_HZ          250000u
#define MESH_CR             8
#define MESH_TX_DBM         17
#define MESH_BROADCAST      0xFFFFFFFFu

#define PORTNUM_TEXT        1
#define PORTNUM_POSITION    3
#define PORTNUM_NODEINFO    4

/* ── OTA packet header (16 bytes, packed) ─────────────────────────────── */

typedef struct __attribute__((packed)) {
    uint32_t to;
    uint32_t from;
    uint32_t id;
    uint8_t  channel;
    uint8_t  next_hop;
    uint8_t  relay_node;
    uint8_t  flags;
} mesh_hdr_t;

#define MESH_HDR_SIZE    16
#define MESH_MAX_PAYLOAD 239
#define MESH_MAX_PKT     255

/* ── Message log ──────────────────────────────────────────────────────── */

#define MSG_MAX      16
#define MSG_TEXT_LEN 64
#define NODE_MAX     8
#define NODE_NAME_LEN 20

typedef struct {
    uint32_t from;
    int8_t   rssi;
    uint8_t  hops;
    uint8_t  portnum;
    char     text[MSG_TEXT_LEN];
    uint32_t ts;
} mesh_msg_t;

typedef struct {
    uint32_t node_id;
    char     long_name[NODE_NAME_LEN];
    char     short_name[5];
    int8_t   rssi;
    uint32_t last_seen_ms;
    int32_t  lat_i;
    int32_t  lon_i;
} mesh_node_t;

static mesh_msg_t  g_msgs[MSG_MAX];
static int         g_msg_count;
static int         g_msg_head;

static mesh_node_t g_nodes[NODE_MAX];
static int         g_node_count;

/* ── Crypto / settings ────────────────────────────────────────────────── */

#define KEY_HEX_LEN 64

static uint8_t  g_channel_key[32];
static int      g_key_loaded;
static uint32_t g_my_node_id;
static uint32_t g_pkt_id_counter;

/* Default LongFast PSK "AQ==" = {0x01, 0x00 × 31} */
static const char LONGFAST_KEY_HEX[] =
    "0100000000000000000000000000000000000000000000000000000000000000";

/* ── States ────────────────────────────────────────────────────────────── */

#define STATE_LIVE   0
#define STATE_DETAIL 1
#define STATE_NODES  2
#define STATE_SEND   3

static int g_state      = STATE_LIVE;
static int g_scroll     = 0;
static int g_sel        = 0;

static uint8_t g_rx_buf[MESH_MAX_PKT];
static uint8_t g_tx_buf[MESH_MAX_PKT];

static char g_my_text[MSG_TEXT_LEN]   = "Hello from AkiraOS!";
static char g_my_name[NODE_NAME_LEN]  = "AkiraNode";
static char g_my_short[5]             = "AKRA";

/* ── Hex helpers ──────────────────────────────────────────────────────── */

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int hex_decode(const char *hex, uint8_t *out, int out_len)
{
    for (int i = 0; i < out_len; i++) {
        int hi = hex_nibble(hex[i * 2]);
        int lo = hex_nibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return -1;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return 0;
}

static void hex_encode(const uint8_t *bin, int len, char *out)
{
    static const char HEX[] = "0123456789abcdef";
    for (int i = 0; i < len; i++) {
        out[i * 2]     = HEX[bin[i] >> 4];
        out[i * 2 + 1] = HEX[bin[i] & 0xF];
    }
    out[len * 2] = '\0';
}

/* ── Protobuf helpers ─────────────────────────────────────────────────── */

static uint64_t pb_varint(const uint8_t **p, const uint8_t *end)
{
    uint64_t v = 0;
    int shift = 0;
    while (*p < end) {
        uint8_t b = *(*p)++;
        v |= (uint64_t)(b & 0x7F) << shift;
        if (!(b & 0x80)) break;
        shift += 7;
    }
    return v;
}

static void pb_skip(const uint8_t **p, const uint8_t *end, int wire)
{
    if (wire == 0) {
        while (*p < end && (*(*p)++ & 0x80));
    } else if (wire == 2) {
        uint64_t len = pb_varint(p, end);
        if (*p + len <= end) *p += len;
    } else if (wire == 5) {
        if (*p + 4 <= end) *p += 4;
    } else if (wire == 1) {
        if (*p + 8 <= end) *p += 8;
    }
}

static int enc_varint(uint8_t *buf, uint64_t v)
{
    int n = 0;
    do {
        buf[n++] = (v & 0x7F) | (v > 0x7F ? 0x80 : 0);
        v >>= 7;
    } while (v);
    return n;
}

static int enc_tag(uint8_t *buf, int field, int wire)
{
    return enc_varint(buf, ((uint64_t)field << 3) | wire);
}

static int enc_bytes(uint8_t *buf, int field, const uint8_t *data, int len)
{
    int n = enc_tag(buf, field, 2);
    n += enc_varint(buf + n, len);
    for (int i = 0; i < len; i++) buf[n + i] = data[i];
    return n + len;
}

static int enc_str(uint8_t *buf, int field, const char *s)
{
    int slen = 0;
    while (s[slen]) slen++;
    return enc_bytes(buf, field, (const uint8_t *)s, slen);
}

static int enc_varint_field(uint8_t *buf, int field, uint64_t v)
{
    int n = enc_tag(buf, field, 0);
    n += enc_varint(buf + n, v);
    return n;
}

/* ── Protobuf decoders ────────────────────────────────────────────────── */

static void decode_position(const uint8_t *data, int len, mesh_node_t *node)
{
    const uint8_t *p = data, *end = data + len;
    while (p < end) {
        uint64_t tag = pb_varint(&p, end);
        int field = (int)(tag >> 3);
        int wire  = (int)(tag & 7);
        if (field == 1 && wire == 0) {
            node->lat_i = (int32_t)(uint32_t)pb_varint(&p, end);
        } else if (field == 2 && wire == 0) {
            node->lon_i = (int32_t)(uint32_t)pb_varint(&p, end);
        } else {
            pb_skip(&p, end, wire);
        }
    }
}

static void decode_nodeinfo(const uint8_t *data, int len, mesh_node_t *node)
{
    const uint8_t *p = data, *end = data + len;
    while (p < end) {
        uint64_t tag = pb_varint(&p, end);
        int field = (int)(tag >> 3);
        int wire  = (int)(tag & 7);
        if (field == 2 && wire == 2) {
            uint64_t slen = pb_varint(&p, end);
            int copy = (int)slen < (NODE_NAME_LEN - 1) ? (int)slen : (NODE_NAME_LEN - 1);
            for (int i = 0; i < copy; i++) node->long_name[i] = ((char *)p)[i];
            node->long_name[copy] = '\0';
            p += slen;
        } else if (field == 3 && wire == 2) {
            uint64_t slen = pb_varint(&p, end);
            int copy = (int)slen < 4 ? (int)slen : 4;
            for (int i = 0; i < copy; i++) node->short_name[i] = ((char *)p)[i];
            node->short_name[copy] = '\0';
            p += slen;
        } else {
            pb_skip(&p, end, wire);
        }
    }
}

/* ── Node tracker ─────────────────────────────────────────────────────── */

static mesh_node_t *node_get_or_add(uint32_t node_id)
{
    for (int i = 0; i < g_node_count; i++) {
        if (g_nodes[i].node_id == node_id) return &g_nodes[i];
    }
    int slot;
    if (g_node_count < NODE_MAX) {
        slot = g_node_count++;
    } else {
        /* Evict oldest (slot 0) */
        for (int i = 0; i < NODE_MAX - 1; i++) g_nodes[i] = g_nodes[i + 1];
        slot = NODE_MAX - 1;
    }
    mesh_node_t *n = &g_nodes[slot];
    for (int i = 0; i < (int)sizeof(*n); i++) ((uint8_t *)n)[i] = 0;
    n->node_id = node_id;
    static const char HEX[] = "0123456789ABCDEF";
    n->short_name[0] = HEX[(node_id >> 12) & 0xF];
    n->short_name[1] = HEX[(node_id >>  8) & 0xF];
    n->short_name[2] = HEX[(node_id >>  4) & 0xF];
    n->short_name[3] = HEX[(node_id      ) & 0xF];
    n->short_name[4] = '\0';
    return n;
}

/* ── Message log ──────────────────────────────────────────────────────── */

static mesh_msg_t *msg_alloc(void)
{
    if (g_msg_count < MSG_MAX) {
        return &g_msgs[g_msg_count++];
    }
    mesh_msg_t *slot = &g_msgs[g_msg_head];
    g_msg_head = (g_msg_head + 1) % MSG_MAX;
    return slot;
}

static mesh_msg_t *msg_get(int idx)
{
    int total = g_msg_count < MSG_MAX ? g_msg_count : MSG_MAX;
    if (idx >= total) return (void *)0;
    int abs_idx;
    if (g_msg_count < MSG_MAX) {
        abs_idx = g_msg_count - 1 - idx;
    } else {
        abs_idx = (g_msg_head - 1 - idx + MSG_MAX * 2) % MSG_MAX;
    }
    return &g_msgs[abs_idx];
}

/* ── CTR crypto ───────────────────────────────────────────────────────── */

static int mesh_ctr(const mesh_hdr_t *hdr, const uint8_t *in, int in_len, uint8_t *out)
{
    uint8_t nonce[16];
    for (int i = 0; i < 16; i++) nonce[i] = 0;
    nonce[0]  = (uint8_t)(hdr->id);
    nonce[1]  = (uint8_t)(hdr->id >> 8);
    nonce[2]  = (uint8_t)(hdr->id >> 16);
    nonce[3]  = (uint8_t)(hdr->id >> 24);
    nonce[8]  = (uint8_t)(hdr->from);
    nonce[9]  = (uint8_t)(hdr->from >> 8);
    nonce[10] = (uint8_t)(hdr->from >> 16);
    nonce[11] = (uint8_t)(hdr->from >> 24);
    return crypto_aes256_ctr(g_channel_key, nonce, in, (uint32_t)in_len, out);
}

/* ── Packet TX ────────────────────────────────────────────────────────── */

static uint32_t next_pkt_id(void) { return ++g_pkt_id_counter; }

static int mesh_send(uint32_t to, const uint8_t *payload, int payload_len)
{
    if (payload_len <= 0 || payload_len > MESH_MAX_PAYLOAD) return -1;

    mesh_hdr_t hdr;
    hdr.to         = to;
    hdr.from       = g_my_node_id;
    hdr.id         = next_pkt_id();
    hdr.channel    = 0;
    hdr.next_hop   = 0;
    hdr.relay_node = 0;
    hdr.flags      = 3;

    uint8_t enc[MESH_MAX_PAYLOAD];
    if (g_key_loaded) {
        if (mesh_ctr(&hdr, payload, payload_len, enc) != 0) return -1;
    } else {
        for (int i = 0; i < payload_len; i++) enc[i] = payload[i];
    }

    for (int i = 0; i < MESH_HDR_SIZE; i++) g_tx_buf[i] = ((uint8_t *)&hdr)[i];
    for (int i = 0; i < payload_len; i++) g_tx_buf[MESH_HDR_SIZE + i] = enc[i];
    return rf_send((uint32_t)(uintptr_t)g_tx_buf, (uint32_t)(MESH_HDR_SIZE + payload_len));
}

static int mesh_send_data(uint32_t to, int portnum, const uint8_t *data, int data_len)
{
    uint8_t pb[MESH_MAX_PAYLOAD];
    int n = 0;
    n += enc_varint_field(pb + n, 1, (uint64_t)portnum);
    n += enc_bytes(pb + n, 2, data, data_len);
    return mesh_send(to, pb, n);
}

static int mesh_send_nodeinfo(void)
{
    char id_str[10];
    id_str[0] = '!';
    uint8_t id_b[4] = {
        (uint8_t)(g_my_node_id >> 24), (uint8_t)(g_my_node_id >> 16),
        (uint8_t)(g_my_node_id >> 8),  (uint8_t)(g_my_node_id),
    };
    hex_encode(id_b, 4, id_str + 1);
    id_str[9] = '\0';

    uint8_t user[64];
    int n = 0;
    n += enc_str(user + n, 1, id_str);
    n += enc_str(user + n, 2, g_my_name);
    n += enc_str(user + n, 3, g_my_short);
    n += enc_varint_field(user + n, 9, 0);
    return mesh_send_data(MESH_BROADCAST, PORTNUM_NODEINFO, user, n);
}

static int mesh_send_text(const char *text)
{
    int tlen = 0;
    while (text[tlen]) tlen++;
    return mesh_send_data(MESH_BROADCAST, PORTNUM_TEXT, (const uint8_t *)text, tlen);
}

/* ── Packet RX ────────────────────────────────────────────────────────── */

static void handle_pkt(const uint8_t *pkt, int pkt_len)
{
    if (pkt_len < MESH_HDR_SIZE) return;

    mesh_hdr_t hdr;
    for (int i = 0; i < MESH_HDR_SIZE; i++) ((uint8_t *)&hdr)[i] = pkt[i];

    if (hdr.from == g_my_node_id) return;

    const uint8_t *enc_payload = pkt + MESH_HDR_SIZE;
    int enc_len = pkt_len - MESH_HDR_SIZE;
    if (enc_len <= 0 || enc_len > MESH_MAX_PAYLOAD) return;

    uint8_t plain[MESH_MAX_PAYLOAD];
    if (g_key_loaded) {
        if (mesh_ctr(&hdr, enc_payload, enc_len, plain) != 0) return;
    } else {
        for (int i = 0; i < enc_len; i++) plain[i] = enc_payload[i];
    }

    const uint8_t *p = plain, *end = plain + enc_len;
    int portnum = 0;
    const uint8_t *inner = (void *)0;
    int inner_len = 0;

    while (p < end) {
        uint64_t tag = pb_varint(&p, end);
        int field = (int)(tag >> 3);
        int wire  = (int)(tag & 7);
        if (field == 1 && wire == 0) {
            portnum = (int)pb_varint(&p, end);
        } else if (field == 2 && wire == 2) {
            uint64_t slen = pb_varint(&p, end);
            inner = p;
            inner_len = (int)slen;
            p += slen;
        } else {
            pb_skip(&p, end, wire);
        }
    }

    if (!inner || inner_len <= 0) return;

    mesh_node_t *node = node_get_or_add(hdr.from);
    node->last_seen_ms = (uint32_t)rtc_get_uptime_ms();
    int16_t raw_rssi = 0;
    rf_get_rssi(&raw_rssi);
    node->rssi = (int8_t)raw_rssi;
    uint8_t hops = hdr.flags & 0x07;

    if (portnum == PORTNUM_TEXT) {
        mesh_msg_t *msg = msg_alloc();
        msg->from    = hdr.from;
        msg->rssi    = node->rssi;
        msg->hops    = hops;
        msg->portnum = PORTNUM_TEXT;
        msg->ts      = node->last_seen_ms;
        int copy = inner_len < (MSG_TEXT_LEN - 1) ? inner_len : (MSG_TEXT_LEN - 1);
        for (int i = 0; i < copy; i++) msg->text[i] = ((char *)inner)[i];
        msg->text[copy] = '\0';

    } else if (portnum == PORTNUM_POSITION) {
        decode_position(inner, inner_len, node);
        mesh_msg_t *msg = msg_alloc();
        msg->from    = hdr.from;
        msg->rssi    = node->rssi;
        msg->hops    = hops;
        msg->portnum = PORTNUM_POSITION;
        msg->ts      = node->last_seen_ms;
        /* Simple position label */
        char tmp[MSG_TEXT_LEN];
        tmp[0]='P'; tmp[1]='O'; tmp[2]='S'; tmp[3]=' ';
        int32_t lat = node->lat_i / 10000000;
        int32_t lon = node->lon_i / 10000000;
        int ti = 4;
        if (lat < 0) { tmp[ti++] = '-'; lat = -lat; }
        for (int k = 2; k >= 0; k--) { tmp[ti + k] = '0' + lat % 10; lat /= 10; }
        ti += 3;
        tmp[ti++] = ',';
        if (lon < 0) { tmp[ti++] = '-'; lon = -lon; }
        for (int k = 3; k >= 0; k--) { tmp[ti + k] = '0' + lon % 10; lon /= 10; }
        ti += 4;
        tmp[ti] = '\0';
        for (int i = 0; i <= ti; i++) msg->text[i] = tmp[i];

    } else if (portnum == PORTNUM_NODEINFO) {
        decode_nodeinfo(inner, inner_len, node);
        mesh_msg_t *msg = msg_alloc();
        msg->from    = hdr.from;
        msg->rssi    = node->rssi;
        msg->hops    = hops;
        msg->portnum = PORTNUM_NODEINFO;
        msg->ts      = node->last_seen_ms;
        /* "NI <short> <long>" */
        char tmp[MSG_TEXT_LEN];
        tmp[0]='N'; tmp[1]='I'; tmp[2]=' ';
        int ti = 3;
        for (int k = 0; k < 4 && node->short_name[k]; k++) tmp[ti++] = node->short_name[k];
        tmp[ti++] = ' ';
        for (int k = 0; k < NODE_NAME_LEN - 1 && node->long_name[k] && ti < MSG_TEXT_LEN - 1; k++)
            tmp[ti++] = node->long_name[k];
        tmp[ti] = '\0';
        for (int i = 0; i <= ti; i++) msg->text[i] = tmp[i];
    }
}

/* ── Display ──────────────────────────────────────────────────────────── */

#define DW    320
#define DH    240
#define HDR_H 16
#define ROW_H 14
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

static void draw_hdr(const char *title, int rx_count)
{
    display_rect(0, 0, DW, HDR_H, COL_HDR);
    display_text(4, 3, title, COL_TXT);
    /* "RX:NNN" right-aligned */
    char buf[8];
    buf[0]='R'; buf[1]='X'; buf[2]=':';
    int n = rx_count, pos = 6;
    buf[6] = '\0'; buf[5] = '0';
    if (n == 0) { buf[5] = '0'; pos = 5; }
    else { while (n && pos > 3) { buf[--pos] = '0' + n % 10; n /= 10; } }
    display_text(DW - 52, 3, buf + pos, COL_DIM);
}

static void draw_footer(const char *hint)
{
    display_rect(0, DH - 14, DW, 14, COL_HDR);
    display_text(4, DH - 11, hint, COL_DIM);
}

static const char *rssi_bars(int8_t rssi)
{
    if (rssi >= -60) return "[||||]";
    if (rssi >= -70) return "[||| ]";
    if (rssi >= -80) return "[||  ]";
    if (rssi >= -90) return "[|   ]";
    return "[    ]";
}

static void fmt_nodeid(uint32_t id, char *out)
{
    out[0] = '!';
    uint8_t b[4] = {
        (uint8_t)(id >> 24), (uint8_t)(id >> 16),
        (uint8_t)(id >> 8),  (uint8_t)(id),
    };
    hex_encode(b, 4, out + 1);
    out[9] = '\0';
}

/* ── Live feed ─────────────────────────────────────────────────────────── */

static void draw_live(void)
{
    int total = g_msg_count < MSG_MAX ? g_msg_count : MSG_MAX;
    int rows  = (DH - HDR_H - 14) / ROW_H;

    draw_hdr("MESHTASTIC", g_msg_count);
    display_rect(0, HDR_H, DW, DH - HDR_H - 14, COL_BG);

    if (total == 0) {
        display_text(8, HDR_H + 20, "Listening for packets...", COL_DIM);
        display_text(8, HDR_H + 36, "SF10 BW250 CR4/8", COL_DIM);
        display_text(8, HDR_H + 52, "906.875 MHz", COL_DIM);
    }

    for (int i = 0; i < rows && (i + g_scroll) < total; i++) {
        mesh_msg_t *msg = msg_get(i + g_scroll);
        if (!msg) break;
        int y = HDR_H + i * ROW_H;
        uint16_t bg = (i + g_scroll == g_sel) ? COL_SEL : COL_BG;
        display_rect(0, y, DW, ROW_H, bg);

        /* Port tag */
        const char *tag;
        if (msg->portnum == PORTNUM_TEXT)     tag = "TXT";
        else if (msg->portnum == PORTNUM_POSITION) tag = "POS";
        else                                   tag = "NFO";
        display_text(2, y + 3, tag, COL_ACC);

        /* Short name from node table */
        char sname[6];
        sname[0]='?'; sname[1]='?'; sname[2]='?'; sname[3]='?'; sname[4]='\0';
        for (int k = 0; k < g_node_count; k++) {
            if (g_nodes[k].node_id == msg->from) {
                for (int j = 0; j < 5; j++) sname[j] = g_nodes[k].short_name[j];
                break;
            }
        }
        display_text(34, y + 3, sname, COL_TXT);

        /* Text preview (up to 24 chars) */
        char preview[26];
        int plen = 0;
        while (plen < 24 && msg->text[plen]) { preview[plen] = msg->text[plen]; plen++; }
        preview[plen] = '\0';
        display_text(76, y + 3, preview, COL_DIM);

        display_text(DW - 40, y + 3, rssi_bars(msg->rssi), COL_OK);
    }

    draw_footer("[A]Detail [B]Nodes [Y]Send [X]NODEINFO");
}

/* ── Detail view ───────────────────────────────────────────────────────── */

static void draw_detail(void)
{
    mesh_msg_t *msg = msg_get(g_sel);
    if (!msg) { g_state = STATE_LIVE; return; }

    display_rect(0, 0, DW, DH, COL_BG);
    draw_hdr("MESSAGE DETAIL", g_msg_count);

    int y = HDR_H + 4;

    display_text(4, y, "From:", COL_ACC);
    char nodeid[10];
    fmt_nodeid(msg->from, nodeid);
    display_text(52, y, nodeid, COL_TXT);
    y += ROW_H;

    for (int k = 0; k < g_node_count; k++) {
        if (g_nodes[k].node_id == msg->from && g_nodes[k].long_name[0]) {
            display_text(4, y, "Name:", COL_ACC);
            display_text(52, y, g_nodes[k].long_name, COL_TXT);
            y += ROW_H;
            break;
        }
    }

    display_rect(0, y, DW, 1, COL_SEP);
    y += 4;

    /* Wrapped text */
    const char *t = msg->text;
    int tlen = 0;
    while (t[tlen]) tlen++;
    while (tlen > 0 && y < DH - 20) {
        int chunk = tlen < 42 ? tlen : 42;
        char line[44];
        for (int i = 0; i < chunk; i++) line[i] = t[i];
        line[chunk] = '\0';
        display_text(4, y, line, COL_TXT);
        t    += chunk;
        tlen -= chunk;
        y    += ROW_H;
    }

    display_rect(0, y, DW, 1, COL_SEP);
    y += 4;

    /* RSSI */
    int8_t r = msg->rssi;
    char rssi_str[8];
    rssi_str[0] = r < 0 ? '-' : '+';
    int rv = r < 0 ? -r : r;
    rssi_str[1] = '0' + rv / 100;
    rssi_str[2] = '0' + (rv / 10) % 10;
    rssi_str[3] = '0' + rv % 10;
    rssi_str[4] = 'd'; rssi_str[5] = 'B'; rssi_str[6] = 'm'; rssi_str[7] = '\0';
    display_text(4, y, "RSSI:", COL_ACC);
    display_text(52, y, rssi_str, COL_TXT);
    char hops_str[4];
    hops_str[0] = 'H'; hops_str[1] = '0' + msg->hops; hops_str[2] = '\0';
    display_text(120, y, hops_str, COL_DIM);

    draw_footer("[B]Back");
}

/* ── Nodes view ────────────────────────────────────────────────────────── */

static void draw_nodes(void)
{
    display_rect(0, 0, DW, DH, COL_BG);
    draw_hdr("KNOWN NODES", g_node_count);

    int rows = (DH - HDR_H - 14) / ROW_H;
    uint32_t now = (uint32_t)rtc_get_uptime_ms();

    if (g_node_count == 0) {
        display_text(8, HDR_H + 20, "No nodes heard yet", COL_DIM);
    }

    for (int i = 0; i < rows && (i + g_scroll) < g_node_count; i++) {
        mesh_node_t *n = &g_nodes[i + g_scroll];
        int y = HDR_H + i * ROW_H;
        uint16_t bg = (i + g_scroll == g_sel) ? COL_SEL : COL_BG;
        display_rect(0, y, DW, ROW_H, bg);

        display_text(4, y + 3, n->short_name, COL_ACC);

        if (n->long_name[0]) {
            display_text(40, y + 3, n->long_name, COL_TXT);
        } else {
            char nid[10];
            fmt_nodeid(n->node_id, nid);
            display_text(40, y + 3, nid, COL_DIM);
        }

        uint32_t age_s = (now - n->last_seen_ms) / 1000;
        char age[4];
        if (age_s < 60) {
            age[0] = '0' + (int)(age_s % 60);
            age[1] = 's'; age[2] = '\0';
        } else if (age_s < 3600) {
            age[0] = '0' + (int)((age_s / 60) % 10);
            age[1] = 'm'; age[2] = '\0';
        } else {
            age[0] = '0' + (int)((age_s / 3600) % 10);
            age[1] = 'h'; age[2] = '\0';
        }
        display_text(DW - 48, y + 3, age, COL_DIM);
        display_text(DW - 28, y + 3, rssi_bars(n->rssi), COL_OK);
    }

    draw_footer("[B]Back");
}

/* ── Send view ─────────────────────────────────────────────────────────── */

static void draw_send(void)
{
    display_rect(0, 0, DW, DH, COL_BG);
    draw_hdr("SEND MESSAGE", g_msg_count);

    display_text(8, HDR_H + 10, "Message:", COL_ACC);
    display_text(8, HDR_H + 26, g_my_text, COL_TXT);
    display_rect(0, HDR_H + 44, DW, 1, COL_SEP);
    display_text(8, HDR_H + 50, "Node ID:", COL_ACC);
    char nid[10];
    fmt_nodeid(g_my_node_id, nid);
    display_text(72, HDR_H + 50, nid, COL_TXT);
    display_text(8, HDR_H + 66, "Name:", COL_ACC);
    display_text(56, HDR_H + 66, g_my_name, COL_TXT);
    display_text(8, HDR_H + 90, "[A] Send text broadcast", COL_OK);
    display_text(8, HDR_H + 106, "[B] Cancel", COL_DIM);
    if (!g_key_loaded) {
        display_text(8, HDR_H + 130, "WARNING: No channel key", COL_WARN);
        display_text(8, HDR_H + 146, "Set mesh/ch0/key in settings", COL_DIM);
    }
    draw_footer("[A]Send [B]Cancel");
}

/* ── Settings load ────────────────────────────────────────────────────── */

static void load_settings(void)
{
    char hex_key[KEY_HEX_LEN + 1];
    if (settings_get("mesh/ch0/key", hex_key, sizeof(hex_key)) == 0) {
        if (hex_decode(hex_key, g_channel_key, 32) == 0) g_key_loaded = 1;
    } else {
        if (hex_decode(LONGFAST_KEY_HEX, g_channel_key, 32) == 0) g_key_loaded = 1;
    }

    char id_str[12];
    if (settings_get("mesh/nodeid", id_str, sizeof(id_str)) == 0) {
        uint8_t id_b[4];
        if (hex_decode(id_str, id_b, 4) == 0) {
            g_my_node_id = ((uint32_t)id_b[0] << 24) | ((uint32_t)id_b[1] << 16) |
                           ((uint32_t)id_b[2] <<  8) | (uint32_t)id_b[3];
        }
    }
    if (g_my_node_id == 0 || g_my_node_id == 0xFFFFFFFFu) {
        crypto_random(&g_my_node_id, 4);
        g_my_node_id &= 0xFFFFFFFEu;
        uint8_t id_b[4] = {
            (uint8_t)(g_my_node_id >> 24), (uint8_t)(g_my_node_id >> 16),
            (uint8_t)(g_my_node_id >>  8), (uint8_t)(g_my_node_id),
        };
        char hex_id[9];
        hex_encode(id_b, 4, hex_id);
        settings_set("mesh/nodeid", hex_id);
    }

    crypto_random(&g_pkt_id_counter, 4);

    char name_buf[NODE_NAME_LEN];
    if (settings_get("mesh/name",  name_buf, sizeof(name_buf)) == 0)
        for (int i = 0; i < NODE_NAME_LEN; i++) g_my_name[i] = name_buf[i];
    char short_buf[5];
    if (settings_get("mesh/short", short_buf, sizeof(short_buf)) == 0)
        for (int i = 0; i < 5; i++) g_my_short[i] = short_buf[i];
    char txt_buf[MSG_TEXT_LEN];
    if (settings_get("mesh/txtext", txt_buf, sizeof(txt_buf)) == 0)
        for (int i = 0; i < MSG_TEXT_LEN; i++) g_my_text[i] = txt_buf[i];
}

/* ── Radio init ───────────────────────────────────────────────────────── */

static int radio_init(void)
{
    if (rf_select(AKIRA_RF_CHIP_LR2021)        != 0) return -1;
    if (rf_set_modulation(RADIO_MOD_LORA)       != 0) return -1;
    if (rf_set_frequency(MESH_FREQ_HZ)          != 0) return -1;
    if (rf_set_spreading_factor(MESH_SF)        != 0) return -1;
    if (rf_set_bandwidth(MESH_BW_HZ)            != 0) return -1;
    if (rf_set_coding_rate(MESH_CR)             != 0) return -1;
    if (rf_set_power(MESH_TX_DBM)               != 0) return -1;
    return 0;
}

/* ── Entry point ──────────────────────────────────────────────────────── */

int main(void)
{
    load_settings();

    if (radio_init() != 0) {
        display_rect(0, 0, DW, DH, COL_BG);
        draw_hdr("MESHTASTIC", 0);
        display_text(8, HDR_H + 20, "Radio init failed", COL_ERR);
        display_text(8, HDR_H + 36, "LR2021 required", COL_DIM);
        display_flush();
        while (1) {
            if (input_get_buttons() & AKIRA_BTN_B) { app_switch("supervisor"); return 0; }
            delay(100);
        }
    }

    mesh_send_nodeinfo();

    uint32_t last_redraw = 0;
    int needs_redraw = 1;
    int prev_buttons = 0;

    while (1) {
        int n = rf_recv_pop(g_rx_buf, (uint32_t)MESH_MAX_PKT, 0);
        if (n > 0) {
            handle_pkt(g_rx_buf, n);
            needs_redraw = 1;
        }

        int btns    = input_get_buttons();
        int pressed = btns & ~prev_buttons;
        prev_buttons = btns;

        if (pressed) {
            needs_redraw = 1;
            int rows = (DH - HDR_H - 14) / ROW_H;

            if (g_state == STATE_LIVE) {
                int total = g_msg_count < MSG_MAX ? g_msg_count : MSG_MAX;
                if ((pressed & AKIRA_BTN_UP) && g_sel > 0) g_sel--;
                if ((pressed & AKIRA_BTN_DOWN) && g_sel < total - 1) g_sel++;
                if ((pressed & AKIRA_BTN_A) && total > 0) g_state = STATE_DETAIL;
                if (pressed & AKIRA_BTN_B) { g_sel = 0; g_scroll = 0; g_state = STATE_NODES; }
                if (pressed & AKIRA_BTN_Y) g_state = STATE_SEND;
                if (pressed & AKIRA_BTN_X) mesh_send_nodeinfo();
                if (g_sel < g_scroll) g_scroll = g_sel;
                if (g_sel >= g_scroll + rows) g_scroll = g_sel - rows + 1;

            } else if (g_state == STATE_DETAIL) {
                if (pressed & AKIRA_BTN_B) g_state = STATE_LIVE;

            } else if (g_state == STATE_NODES) {
                if ((pressed & AKIRA_BTN_UP) && g_sel > 0) g_sel--;
                if ((pressed & AKIRA_BTN_DOWN) && g_sel < g_node_count - 1) g_sel++;
                if (pressed & AKIRA_BTN_B) { g_sel = 0; g_scroll = 0; g_state = STATE_LIVE; }

            } else if (g_state == STATE_SEND) {
                if (pressed & AKIRA_BTN_A) {
                    mesh_send_text(g_my_text);
                    mesh_msg_t *msg = msg_alloc();
                    msg->from    = g_my_node_id;
                    msg->rssi    = 0;
                    msg->hops    = 0;
                    msg->portnum = PORTNUM_TEXT;
                    msg->ts      = (uint32_t)rtc_get_uptime_ms();
                    for (int i = 0; i < MSG_TEXT_LEN; i++) msg->text[i] = g_my_text[i];
                    g_state = STATE_LIVE;
                }
                if (pressed & AKIRA_BTN_B) g_state = STATE_LIVE;
            }
        }

        uint32_t now_ms = (uint32_t)rtc_get_uptime_ms();
        if (needs_redraw || (now_ms - last_redraw) > 100) {
            switch (g_state) {
            case STATE_LIVE:   draw_live();   break;
            case STATE_DETAIL: draw_detail(); break;
            case STATE_NODES:  draw_nodes();  break;
            case STATE_SEND:   draw_send();   break;
            default: break;
            }
            display_flush();
            last_redraw  = now_ms;
            needs_redraw = 0;
        }

        delay(20);
    }

    return 0;
}
