/**
 * @file main.c
 * @brief Agent Guard — a physical approve button for AI agents
 *
 * An AI agent (Claude Code, an MCP client, any script) asks before it does
 * something it can't undo: push, deploy, delete, pay.  The request appears
 * on the console; the owner holds A+B to approve or presses X to deny, and
 * the answer comes back signed by a key that never leaves the SE050 secure
 * element.  Whoever executes the action can check that signature, so the
 * approval can't be forged by the agent or by the computer it runs on.
 *
 * Protocol (HTTP/1.1 on TCP port 8787, one request per connection):
 *   GET  /v1/info     key type, public key and fingerprint (no auth)
 *   POST /v1/ping     check the pairing code
 *   POST /v1/approve  {"agent","action","summary","details","risk",
 *                      "nonce","timeout"}  -> waits for the owner
 * POSTs carry X-Akira-Auth: hex HMAC-SHA256(K, "METHOD PATH\n" + body),
 * K = SHA-256("akira-agent-guard/pair\n" + pairing code).  The reply to an
 * approve is signed (ECDSA P-256, raw r||s) over SHA-256 of:
 *   "AKIRA-AGENT-GUARD/1\ndecision:<d>\ncounter:<n>\ntime:<t>\nrequest:<hex>\n"
 * where <hex> is SHA-256 of the exact request body.  Host tools and the
 * guide live in AkiraSDK/tools/agent_guard/.
 *
 * Controls: hold A+B approve, X deny, UP/DOWN scroll a long request;
 * idle: X pair, Y history, HOME exit.
 *
 * Needs the console on Wi-Fi (Settings > Wi-Fi).  Uses the SE050 when the
 * firmware exposes it (CONFIG_AKIRA_SE050_U2F); otherwise falls back to a
 * software key and says so on screen.
 *
 * @copyright Copyright (c) 2026 PenEngineering S.R.L
 * @license Apache-2.0
 */

#include "akira_api.h"
#include "../../common/akira_game.h"
#include "../../common/akira_spr.h"

/* SE050-backed P-256 (non-exportable).  Firmware with the SE050 exports
 * always provides these; they return -ENOTSUP when no SE050 is fitted. */
extern int crypto_p256_keygen_se050(int32_t slot, uint8_t *pub_out);
extern int crypto_p256_sign_se050(int32_t slot, const uint8_t *hash, uint8_t *sig_out);

#define AG_VERSION   "1.0.0"
#define AG_PORT      8787
#define AG_SLOT      0xA0 /* first SE050 key slot we try */
#define MAX_CLIENTS  2
#define RX_RING      (NET_RING_HDR_SIZE + 4096)
#define TX_RING      (NET_RING_HDR_SIZE + 2048)
#define REQ_MAX      3072
#define BODY_MAX     2048
#define FRAME_MAX    1400 /* per TX ring frame (firmware cap 1500) */
#define HOLD_MS      1200

static int32_t SW = 320, SH = 240;

/* ── Small helpers ───────────────────────────────────────────────────── */
static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

static void mcopy(void *d, const void *s, int n)
{
    volatile uint8_t *dd = (volatile uint8_t *)d; /* volatile: no memcpy import */
    const uint8_t *ss = (const uint8_t *)s;
    for (int i = 0; i < n; i++) dd[i] = ss[i];
}
static void mzero(void *d, int n)
{
    volatile uint8_t *dd = (volatile uint8_t *)d;
    for (int i = 0; i < n; i++) dd[i] = 0;
}
static void scopy(char *d, const char *s, int cap)
{
    int i = 0;
    for (; s[i] && i < cap - 1; i++) d[i] = s[i];
    d[i] = 0;
}
static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

static const char HEX[] = "0123456789abcdef";
static void to_hex(char *out, const uint8_t *b, int n)
{
    for (int i = 0; i < n; i++) {
        out[2 * i] = HEX[b[i] >> 4];
        out[2 * i + 1] = HEX[b[i] & 15];
    }
    out[2 * n] = 0;
}
static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    c = lower(c);
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}
static int from_hex(uint8_t *out, const char *s, int n)
{
    for (int i = 0; i < n; i++) {
        int a = hexval(s[2 * i]), b = a < 0 ? -1 : hexval(s[2 * i + 1]);
        if (a < 0 || b < 0) return -1;
        out[i] = (uint8_t)(a << 4 | b);
    }
    return 0;
}

static int load_str(const char *key, char *buf, int len)
{
    buf[0] = 0;
    if (settings_get(key, buf, len) != 0) {
        buf[0] = 0;
        return -1;
    }
    buf[len - 1] = 0;
    return 0;
}

/* ── Keys and pairing ────────────────────────────────────────────────── */
enum { KEY_NONE, KEY_SE050, KEY_SOFT };
static int key_kind, key_slot;
static uint8_t pub[65];
static uint8_t soft_priv[32];
static char fp_str[24];   /* "AB12 CD34 EF56 7890" */
static char pair_code[9]; /* 8 chars from CODE_ALPHA */
static uint8_t pair_key[32];
static uint32_t counter, n_ok, n_no, n_badauth;
static int key_err;       /* last keygen/sign error (shown on screen) */

static const char CODE_ALPHA[] = "23456789ABCDEFGHJKMNPQRSTUVXYZ"; /* 30: no 0 1 I L O W */

static void derive_pair_key(void)
{
    static const char pfx[] = "akira-agent-guard/pair\n";
    char buf[40];
    int n = 0;
    for (int i = 0; pfx[i]; i++) buf[n++] = pfx[i];
    for (int i = 0; i < 8; i++) buf[n++] = pair_code[i];
    crypto_sha256(buf, (uint32_t)n, pair_key);
}

static void new_pair_code(void)
{
    uint8_t r[8];
    crypto_random(r, sizeof(r));
    for (int i = 0; i < 8; i++) pair_code[i] = CODE_ALPHA[r[i] % 30];
    pair_code[8] = 0;
    settings_set("ag/code", pair_code);
    derive_pair_key();
}

static void make_fingerprint(void)
{
    uint8_t h[32];
    crypto_sha256(pub, 65, h);
    int n = 0;
    for (int i = 0; i < 8; i++) {
        fp_str[n++] = "0123456789ABCDEF"[h[i] >> 4];
        fp_str[n++] = "0123456789ABCDEF"[h[i] & 15];
        if (i & 1 && i < 7) fp_str[n++] = ' ';
    }
    fp_str[n] = 0;
}

static void save_counters(void)
{
    g_save_int("ag/ctr", (int)counter);
    g_save_int("ag/nok", (int)n_ok);
    g_save_int("ag/nno", (int)n_no);
}

/* Load the key, or make one: SE050 first, software if there is none. */
static int setup_keys(void)
{
    char kind[4], hex[140];
    int fresh_soft = 0;
    load_str("ag/key", kind, sizeof(kind));
    if (load_str("ag/pub", hex, sizeof(hex)) == 0 && strlen(hex) == 130 && from_hex(pub, hex, 65) == 0) {
        if (kind[0] == 's' && kind[1] == 'e') {
            key_kind = KEY_SE050;
            key_slot = g_load_int("ag/slot", AG_SLOT);
        } else if (kind[0] == 's' && kind[1] == 'w' && load_str("ag/sk", hex, sizeof(hex)) == 0 &&
                   strlen(hex) == 64 && from_hex(soft_priv, hex, 32) == 0) {
            key_kind = KEY_SOFT;
        }
    }
    if (key_kind == KEY_NONE) {
        /* A slot whose object can't be regenerated is skipped, not fatal. */
        int rc = -1;
        for (int s = AG_SLOT; s < AG_SLOT + 8; s++) {
            rc = crypto_p256_keygen_se050(s, pub);
            if (rc == 0) {
                key_kind = KEY_SE050;
                key_slot = s;
                break;
            }
            if (rc == -95 /* ENOTSUP */ || rc == -19 /* ENODEV */) break;
        }
        if (key_kind == KEY_SE050) {
            settings_set("ag/key", "se");
            g_save_int("ag/slot", key_slot);
        } else {
            key_err = rc;
            if (crypto_p256_keygen(soft_priv, pub) != 0) return -1;
            key_kind = KEY_SOFT;
            to_hex(hex, soft_priv, 32);
            settings_set("ag/sk", hex);
            settings_set("ag/key", "sw");
            fresh_soft = 1;
        }
        to_hex(hex, pub, 65);
        settings_set("ag/pub", hex);
    }
    make_fingerprint();
    return fresh_soft;
}

/* ── Minimal JSON reader for the request object ──────────────────────── */
typedef struct {
    char agent[32];
    char action[40];
    char summary[256];
    char details[1024];
    char risk[8];
    char nonce[72];
    int timeout;
} areq_t;

static const char *jp, *je;

static void jws(void)
{
    while (jp < je && (*jp == ' ' || *jp == '\t' || *jp == '\r' || *jp == '\n')) jp++;
}

/* Parse a JSON string at jp (on the opening quote) into out (cap bytes,
 * may be 0 to skip).  Only printable ASCII is kept: anything else becomes
 * '?', so the screen shows exactly what is signed, never a lookalike. */
static int jstr(char *out, int cap)
{
    int n = 0;
    if (jp >= je || *jp != '"') return -1;
    jp++;
    while (jp < je && *jp != '"') {
        unsigned char c = (unsigned char)*jp++;
        int ch;
        if (c == '\\') {
            if (jp >= je) return -1;
            char e = *jp++;
            switch (e) {
            case 'n': ch = '\n'; break;
            case 't': ch = ' '; break;
            case 'r': case 'b': case 'f': ch = ' '; break;
            case 'u': {
                if (je - jp < 4) return -1;
                int v = 0;
                for (int i = 0; i < 4; i++) {
                    int h = hexval(jp[i]);
                    if (h < 0) return -1;
                    v = v << 4 | h;
                }
                jp += 4;
                ch = (v >= 32 && v < 127) ? v : '?';
                break;
            }
            default: ch = e; break; /* \" \\ \/ */
            }
        } else if (c >= 0x80) {
            while (jp < je && ((unsigned char)*jp & 0xC0) == 0x80) jp++; /* rest of a UTF-8 char */
            ch = '?';
        } else if (c < 32) {
            ch = ' ';
        } else {
            ch = c;
        }
        if (out && n < cap - 1) out[n++] = (char)ch;
    }
    if (jp >= je) return -1;
    jp++;
    if (out) out[n] = 0;
    return 0;
}

static int jskip(void)
{
    jws();
    if (jp >= je) return -1;
    if (*jp == '"') return jstr(0, 0);
    if (*jp == '{' || *jp == '[') {
        int depth = 0;
        while (jp < je) {
            if (*jp == '"') {
                if (jstr(0, 0)) return -1;
                continue;
            }
            if (*jp == '{' || *jp == '[') depth++;
            else if (*jp == '}' || *jp == ']') {
                if (--depth == 0) {
                    jp++;
                    return 0;
                }
            }
            jp++;
        }
        return -1;
    }
    while (jp < je && *jp != ',' && *jp != '}' && *jp != ']') jp++;
    return 0;
}

static int keyeq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

static int parse_request(const char *body, int len, areq_t *r)
{
    mzero(r, sizeof(*r));
    r->timeout = 120;
    jp = body;
    je = body + len;
    jws();
    if (jp >= je || *jp != '{') return -1;
    jp++;
    for (;;) {
        jws();
        if (jp < je && *jp == '}') break;
        char key[16];
        if (jstr(key, sizeof(key))) return -1;
        jws();
        if (jp >= je || *jp != ':') return -1;
        jp++;
        jws();
        char *dst = 0;
        int cap = 0;
        if (keyeq(key, "agent")) { dst = r->agent; cap = sizeof(r->agent); }
        else if (keyeq(key, "action")) { dst = r->action; cap = sizeof(r->action); }
        else if (keyeq(key, "summary")) { dst = r->summary; cap = sizeof(r->summary); }
        else if (keyeq(key, "details")) { dst = r->details; cap = sizeof(r->details); }
        else if (keyeq(key, "risk")) { dst = r->risk; cap = sizeof(r->risk); }
        else if (keyeq(key, "nonce")) { dst = r->nonce; cap = sizeof(r->nonce); }
        if (dst && jp < je && *jp == '"') {
            if (jstr(dst, cap)) return -1;
        } else if (keyeq(key, "timeout") && jp < je && *jp >= '0' && *jp <= '9') {
            int v = 0;
            while (jp < je && *jp >= '0' && *jp <= '9') v = v * 10 + (*jp++ - '0');
            r->timeout = v;
        } else if (jskip()) {
            return -1;
        }
        jws();
        if (jp < je && *jp == ',') {
            jp++;
            continue;
        }
        if (jp < je && *jp == '}') break;
        return -1;
    }
    if (!r->summary[0]) return -2;
    int nl = strlen(r->nonce);
    if (nl < 8 || nl > 64) return -3;
    if (!r->agent[0]) scopy(r->agent, "An agent", sizeof(r->agent));
    if (!r->action[0]) scopy(r->action, "Action", sizeof(r->action));
    if (!(keyeq(r->risk, "low") || keyeq(r->risk, "medium") || keyeq(r->risk, "high")))
        scopy(r->risk, "medium", sizeof(r->risk));
    if (r->timeout < 10) r->timeout = 10;
    if (r->timeout > 600) r->timeout = 600;
    return 0;
}

/* ── Network ─────────────────────────────────────────────────────────── */
enum { C_FREE, C_READ, C_WAIT, C_CLOSING };
typedef struct {
    int h, state;
    uint32_t t_open, t_close;
    int len;
    char req[REQ_MAX + 1];
} client_t;
static client_t clients[MAX_CLIENTS];
static uint8_t rx_ring[MAX_CLIENTS][RX_RING];
static uint8_t tx_ring[MAX_CLIENTS][TX_RING];
static int listener = -1, net_up;
static char ip_str[20];
static uint32_t next_net_try;

/* Approval in progress (one at a time). */
static struct {
    int active, client;
    areq_t r;
    uint8_t hash[32];
    char code[10]; /* "3FA2-9C1E" */
    uint32_t t0, deadline;
    int hold_ms, scroll, lines, withdrawn;
} P;

/* Recent request hashes: an exact replay is refused. */
static uint8_t seen[32][8];
static int seen_n;

static char outbuf[2304];

static void client_close(int i)
{
    if (clients[i].h >= 0) net_close(clients[i].h);
    clients[i].h = -1;
    clients[i].state = C_FREE;
    clients[i].len = 0;
}

static int tx_drained(int i)
{
    volatile uint32_t *wi = (volatile uint32_t *)(tx_ring[i] + 0);
    volatile uint32_t *ri = (volatile uint32_t *)(tx_ring[i] + 4);
    return *wi == *ri;
}

static void respond(int i, int status, const char *json)
{
    client_t *c = &clients[i];
    if (c->h < 0) return;
    const char *reason = status == 200 ? "OK" : status == 400 ? "Bad Request" : status == 401 ? "Unauthorized"
                       : status == 404 ? "Not Found" : status == 409 ? "Conflict" : status == 413 ? "Payload Too Large"
                       : "Internal Server Error";
    char head[160];
    head[0] = 0;
    g_cat(head, "HTTP/1.1 ");
    g_cat_int(head, status);
    g_cat(head, " ");
    g_cat(head, reason);
    g_cat(head, "\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: ");
    g_cat_int(head, strlen(json));
    g_cat(head, "\r\n\r\n");
    outbuf[0] = 0;
    g_cat(outbuf, head);
    int n = strlen(outbuf), jl = strlen(json);
    if (n + jl >= (int)sizeof(outbuf)) jl = (int)sizeof(outbuf) - 1 - n;
    mcopy(outbuf + n, json, jl);
    n += jl;
    for (int off = 0; off < n; off += FRAME_MAX)
        net_ring_write(tx_ring[i], TX_RING, (const uint8_t *)outbuf + off, imin(FRAME_MAX, n - off));
    net_tx_flush(c->h);
    c->state = C_CLOSING;
    c->t_close = g_now();
}

static void respond_err(int i, int status, const char *msg)
{
    char j[96];
    j[0] = 0;
    g_cat(j, "{\"error\":\"");
    g_cat(j, msg);
    g_cat(j, "\"}");
    respond(i, status, j);
}

static int mac_ok(const char *method, const char *path, const char *body, int blen, const char *auth_hex)
{
    static char data[BODY_MAX + 48];
    uint8_t mac[32], want[32];
    int n = 0;
    if (strlen(auth_hex) != 64 || from_hex(want, auth_hex, 32)) return 0;
    for (int i = 0; method[i] && n < 8; i++) data[n++] = method[i];
    data[n++] = ' ';
    for (int i = 0; path[i] && n < 40; i++) data[n++] = path[i];
    data[n++] = '\n';
    mcopy(data + n, body, blen);
    n += blen;
    if (crypto_hmac_sha256(pair_key, 32, data, (uint32_t)n, mac) != 0) return 0;
    uint8_t d = 0;
    for (int i = 0; i < 32; i++) d |= (uint8_t)(mac[i] ^ want[i]);
    return d == 0;
}

static void reply_info(int i)
{
    static char j[400];
    char ph[132];
    to_hex(ph, pub, 65);
    j[0] = 0;
    g_cat(j, "{\"app\":\"agent_guard\",\"version\":\"" AG_VERSION "\",\"protocol\":1,\"key\":\"");
    g_cat(j, key_kind == KEY_SE050 ? "se050" : "software");
    g_cat(j, "\",\"pub\":\"");
    g_cat(j, ph);
    g_cat(j, "\",\"fp\":\"");
    g_cat(j, fp_str);
    g_cat(j, P.active ? "\",\"busy\":true}" : "\",\"busy\":false}");
    respond(i, 200, j);
}

static void start_approval(int i, const char *body, int blen)
{
    areq_t *r = &P.r;
    int pr = parse_request(body, blen, r);
    if (pr == -2) { respond_err(i, 400, "summary is required"); return; }
    if (pr == -3) { respond_err(i, 400, "nonce must be 8..64 characters"); return; }
    if (pr) { respond_err(i, 400, "malformed JSON"); return; }
    crypto_sha256(body, (uint32_t)blen, P.hash);
    for (int k = 0; k < seen_n; k++) {
        int same = 1;
        for (int b = 0; b < 8; b++) same &= seen[k][b] == P.hash[b];
        if (same) { respond_err(i, 409, "replayed request"); return; }
    }
    mcopy(seen[seen_n % 32], P.hash, 8);
    seen_n = seen_n < 32 ? seen_n + 1 : 32;

    P.active = 1;
    P.client = i;
    P.withdrawn = 0;
    P.hold_ms = 0;
    P.scroll = 0;
    P.t0 = g_now();
    P.deadline = P.t0 + (uint32_t)r->timeout * 1000u;
    static const char HU[] = "0123456789ABCDEF";
    int n = 0;
    for (int b = 0; b < 4; b++) {
        if (b == 2) P.code[n++] = '-';
        P.code[n++] = HU[P.hash[b] >> 4];
        P.code[n++] = HU[P.hash[b] & 15];
    }
    P.code[n] = 0;
    clients[i].state = C_WAIT;
    g_flush_input();
}

static void handle_request(int i)
{
    client_t *c = &clients[i];
    c->req[c->len] = 0;
    /* end of headers */
    int he = -1;
    for (int k = 0; k + 3 < c->len; k++)
        if (c->req[k] == '\r' && c->req[k + 1] == '\n' && c->req[k + 2] == '\r' && c->req[k + 3] == '\n') {
            he = k + 4;
            break;
        }
    if (he < 0) {
        if (c->len >= REQ_MAX) respond_err(i, 413, "request too large");
        return;
    }
    char method[8], path[40], auth[72];
    int clen = 0, k = 0, m = 0;
    while (k < he && c->req[k] != ' ' && m < 7) method[m++] = c->req[k++];
    method[m] = 0;
    k++;
    m = 0;
    while (k < he && c->req[k] != ' ' && c->req[k] != '\r' && m < 39) path[m++] = c->req[k++];
    path[m] = 0;
    auth[0] = 0;
    /* headers, case-insensitive */
    while (k < he) {
        while (k < he && c->req[k] != '\n') k++;
        k++;
        if (k >= he) break;
        static const char CL[] = "content-length:", AU[] = "x-akira-auth:";
        int ok1 = 1, ok2 = 1;
        for (int t = 0; CL[t]; t++) ok1 &= lower(c->req[k + t]) == CL[t];
        for (int t = 0; AU[t]; t++) ok2 &= lower(c->req[k + t]) == AU[t];
        if (ok1) {
            int p = k + 15;
            while (c->req[p] == ' ') p++;
            while (c->req[p] >= '0' && c->req[p] <= '9') clen = clen * 10 + (c->req[p++] - '0');
        } else if (ok2) {
            int p = k + 13, a = 0;
            while (c->req[p] == ' ') p++;
            while (a < 70 && hexval(c->req[p]) >= 0) auth[a++] = c->req[p++];
            auth[a] = 0;
        }
    }
    if (clen > BODY_MAX) { respond_err(i, 413, "body too large"); return; }
    if (c->len < he + clen) return; /* body still arriving */
    const char *body = c->req + he;

    if (keyeq(method, "GET") && keyeq(path, "/v1/info")) {
        reply_info(i);
        return;
    }
    if (!keyeq(method, "POST") || !(keyeq(path, "/v1/ping") || keyeq(path, "/v1/approve"))) {
        respond_err(i, 404, "unknown endpoint");
        return;
    }
    if (!mac_ok(method, path, body, clen, auth)) {
        n_badauth++;
        respond_err(i, 401, "bad or missing X-Akira-Auth (pair again)");
        return;
    }
    if (keyeq(path, "/v1/ping")) {
        respond(i, 200, "{\"ok\":true}");
        return;
    }
    if (P.active) {
        respond_err(i, 409, "busy: another request is on screen");
        return;
    }
    start_approval(i, body, clen);
}

static void net_poll(void)
{
    uint32_t now = g_now();
    if (!net_up) {
        if ((int32_t)(now - next_net_try) < 0) return;
        next_net_try = now + 1500;
        char ip[20];
        if (net_get_ip(ip, sizeof(ip)) != 0 || !ip[0]) return;
        scopy(ip_str, ip, sizeof(ip_str));
        listener = net_open(NET_TYPE_TCP);
        if (listener < 0) return;
        if (net_bind(listener, AG_PORT) < 0 || net_listen(listener, 2) < 0) {
            net_close(listener);
            listener = -1;
            return;
        }
        net_up = 1;
    }

    uint8_t ev[4];
    for (int guard = 0; guard < 16 && net_event_pop(ev, sizeof(ev)) >= 2; guard++) {
        int type = ev[0], h = ev[1], extra = ev[2] | ev[3] << 8;
        if (type == NET_EVT_ACCEPT && h == listener) {
            int slot = -1;
            for (int i = 0; i < MAX_CLIENTS; i++)
                if (clients[i].state == C_FREE) {
                    slot = i;
                    break;
                }
            if (slot < 0) {
                net_close(extra);
                continue;
            }
            client_t *c = &clients[slot];
            c->h = extra;
            c->len = 0;
            c->state = C_READ;
            c->t_open = now;
            if (net_tx_bind(extra, tx_ring[slot], TX_RING) < 0 || net_rx_bind(extra, rx_ring[slot], RX_RING) < 0)
                client_close(slot);
            continue;
        }
        int i = -1;
        for (int k = 0; k < MAX_CLIENTS; k++)
            if (clients[k].state != C_FREE && clients[k].h == h) i = k;
        if (i < 0) continue;
        client_t *c = &clients[i];
        if (type == NET_EVT_DATA_READY) {
            static uint8_t tmp[1600];
            for (;;) {
                int n = net_ring_read(rx_ring[i], RX_RING, tmp, sizeof(tmp));
                if (n <= 0) break;
                if (c->state != C_READ) continue; /* nothing more expected */
                int room = REQ_MAX - c->len;
                if (n > room) n = room;
                mcopy(c->req + c->len, tmp, n);
                c->len += n;
            }
            if (c->state == C_READ) handle_request(i);
        } else if (type == NET_EVT_DISCONNECTED || type == NET_EVT_ERROR) {
            if (P.active && P.client == i) {
                P.withdrawn = 1; /* the agent gave up: drop the card */
                P.client = -1;
            }
            client_close(i);
        }
    }

    for (int i = 0; i < MAX_CLIENTS; i++) {
        client_t *c = &clients[i];
        if (c->state == C_READ && now - c->t_open > 10000) client_close(i); /* slow or idle client */
        if (c->state == C_CLOSING && ((tx_drained(i) && now - c->t_close > 150) || now - c->t_close > 3000))
            client_close(i);
    }
    char ip[20];
    if ((now & 0x3FF) < 40 && net_get_ip(ip, sizeof(ip)) == 0 && ip[0]) scopy(ip_str, ip, sizeof(ip_str));
}

/* ── Decisions ───────────────────────────────────────────────────────── */
enum { D_APPROVED, D_DENIED, D_TIMEOUT, D_WITHDRAWN };
static const char *const DNAME[] = {"approved", "denied", "timeout", "cancelled"};

typedef struct {
    char action[20];
    char summary[44];
    uint8_t decision;
    uint32_t t;
} hist_t;
static hist_t hist[8];
static int hist_n;

static int flash_decision = -1;
static uint32_t flash_until;
static char flash_note[40];

static void add_history(int d)
{
    for (int k = 7; k > 0; k--) mcopy(&hist[k], &hist[k - 1], sizeof(hist_t));
    scopy(hist[0].action, P.r.action, sizeof(hist[0].action));
    scopy(hist[0].summary, P.r.summary, sizeof(hist[0].summary));
    for (int k = 0; hist[0].summary[k]; k++)
        if (hist[0].summary[k] == '\n') hist[0].summary[k] = ' ';
    hist[0].decision = (uint8_t)d;
    hist[0].t = g_now() / 1000;
    if (hist_n < 8) hist_n++;
}

/* Sign msg with the device key.  0 on success. */
static int sign_msg(const char *msg, int len, uint8_t sig[64])
{
    if (key_kind == KEY_SE050) {
        uint8_t h[32];
        crypto_sha256(msg, (uint32_t)len, h);
        /* AkiraOS asks for a Seal the first time per run (VAULT_SIGN). */
        return crypto_p256_sign_se050(key_slot, h, sig);
    }
    return crypto_p256_sign(soft_priv, msg, (uint32_t)len, sig);
}

static void finish(int d)
{
    flash_note[0] = 0;
    if (d == D_WITHDRAWN) {
        add_history(d);
        flash_decision = d;
        flash_until = g_now() + 1500;
        scopy(flash_note, "the agent stopped waiting", sizeof(flash_note));
        P.active = 0;
        return;
    }
    char rh[66], sh[130];
    to_hex(rh, P.hash, 32);
    int t = rtc_get_unix_time();
    if (t < 0) t = 0;
    int signed_ok = 0, rc = 0;
    uint8_t sig[64];
    if (d == D_APPROVED || d == D_DENIED) {
        counter++;
        static char msg[200];
        msg[0] = 0;
        g_cat(msg, "AKIRA-AGENT-GUARD/1\ndecision:");
        g_cat(msg, DNAME[d]);
        g_cat(msg, "\ncounter:");
        g_cat_int(msg, (int)counter);
        g_cat(msg, "\ntime:");
        g_cat_int(msg, t);
        g_cat(msg, "\nrequest:");
        g_cat(msg, rh);
        g_cat(msg, "\n");
        flash_decision = d;
        rc = sign_msg(msg, strlen(msg), sig);
        signed_ok = rc == 0;
        if (!signed_ok) key_err = rc;
        if (d == D_APPROVED) n_ok++;
        else n_no++;
        save_counters();
    }
    if (P.client >= 0 && clients[P.client].state == C_WAIT) {
        static char j[480];
        j[0] = 0;
        if (d == D_APPROVED && !signed_ok) {
            /* Approved on screen but not signed (e.g. the Seal was refused):
             * report it as a denial so nothing runs on an unsigned yes. */
            g_cat(j, "{\"decision\":\"denied\",\"error\":\"signing failed (");
            g_cat_int(j, rc);
            g_cat(j, rc == -13 ? ": AkiraOS Seal refused)\"" : ")\"");
        } else {
            g_cat(j, "{\"decision\":\"");
            g_cat(j, DNAME[d]);
            g_cat(j, "\"");
        }
        g_cat(j, ",\"counter\":");
        g_cat_int(j, signed_ok ? (int)counter : 0);
        g_cat(j, ",\"time\":");
        g_cat_int(j, t);
        g_cat(j, ",\"request_sha256\":\"");
        g_cat(j, rh);
        g_cat(j, "\",\"key\":\"");
        g_cat(j, key_kind == KEY_SE050 ? "se050" : "software");
        g_cat(j, "\",\"sig\":\"");
        if (signed_ok) {
            to_hex(sh, sig, 64);
            g_cat(j, sh);
        }
        g_cat(j, "\"}");
        respond(P.client, 200, j);
    }
    add_history(d);
    flash_decision = d;
    flash_until = g_now() + 1300;
    if (d == D_APPROVED && !signed_ok) scopy(flash_note, "but signing failed: sent as denied", sizeof(flash_note));
    else if (d == D_TIMEOUT) scopy(flash_note, "nobody answered in time", sizeof(flash_note));
    P.active = 0;
}

/* ── Drawing ─────────────────────────────────────────────────────────── */
static uint16_t C_BG, C_PANEL, C_EDGE, C_TEXT, C_DIM, C_OK, C_NO, C_WARN, C_ACC;

static void theme(void)
{
    spr_theme_init(120, 200, 255);
    C_BG = spr_mono ? 0x0000 : SPR_RGB(10, 14, 28);
    C_PANEL = spr_mono ? 0x0000 : SPR_RGB(20, 26, 48);
    C_EDGE = spr_mono ? 0xFFFF : SPR_RGB(70, 90, 140);
    C_TEXT = 0xFFFF;
    C_DIM = spr_mono ? 0xFFFF : SPR_RGB(140, 154, 190);
    C_OK = spr_mono ? 0xFFFF : SPR_RGB(90, 220, 120);
    C_NO = spr_mono ? 0xFFFF : SPR_RGB(255, 90, 90);
    C_WARN = spr_mono ? 0xFFFF : SPR_RGB(255, 190, 70);
    C_ACC = spr_mono ? 0xFFFF : SPR_RGB(120, 200, 255);
}

static uint16_t risk_col(const char *r)
{
    if (spr_mono) return 0xFFFF;
    if (r[0] == 'h') return SPR_RGB(220, 60, 60);
    if (r[0] == 'l') return SPR_RGB(50, 160, 90);
    return SPR_RGB(220, 150, 40);
}

static void shield(int cx, int cy, int s, uint16_t col)
{
    /* flat top with rounded shoulders, sides tapering to a point */
    display_rounded_rect_fill(cx - s, cy - s, 2 * s + 1, s + 4, imax(2, s / 4), col);
    display_triangle_fill(cx - s, cy + 1, cx + s, cy + 1, cx, cy + s + s / 2, col);
}

static void check_mark(int cx, int cy, uint16_t col)
{
    for (int t = -2; t <= 2; t++) {
        display_line(cx - 10, cy + t, cx - 3, cy + 7 + t, col);
        display_line(cx - 3, cy + 7 + t, cx + 12, cy - 8 + t, col);
    }
}

static void cross_mark(int cx, int cy, uint16_t col)
{
    for (int t = -2; t <= 2; t++) {
        display_line(cx - 9 + t, cy - 9, cx + 9 + t, cy + 9, col);
        display_line(cx + 9 + t, cy - 9, cx - 9 + t, cy + 9, col);
    }
}

static void topbar(const char *title)
{
    display_rect(0, 0, SW, 22, C_PANEL);
    display_hline(0, 22, SW, C_EDGE);
    shield(12, 10, 6, C_ACC);
    display_text(24, 6, title, C_TEXT);
    const char *k = key_kind == KEY_SE050 ? "SE050 key" : "SOFTWARE KEY";
    int kw = g_tw(k) + 10;
    uint16_t kc = key_kind == KEY_SE050 ? C_OK : C_WARN;
    display_rounded_rect(SW - kw - 4, 3, kw, 16, 4, kc);
    display_text(SW - kw + 1, 6, k, kc);
}

static void footer(const char *s)
{
    display_rect(0, SH - 18, SW, 18, C_PANEL);
    display_hline(0, SH - 19, SW, C_EDGE);
    display_text(6, SH - 14, s, C_DIM);
}

/* Word-wrap @p s into lines of at most @p cols chars; draws lines
 * [first, first + max) at (x, y) and returns the total line count. */
static int wrap(int x, int y, int cols, int first, int max, const char *s, uint16_t col, int lh)
{
    int line = 0, i = 0, n = strlen(s);
    char buf[64];
    if (cols > 63) cols = 63;
    while (i < n) {
        int len = 0, brk = -1;
        while (i + len < n && s[i + len] != '\n' && len < cols) {
            if (s[i + len] == ' ') brk = len;
            len++;
        }
        int take = len, skip = 0;
        if (i + len < n && s[i + len] == '\n') skip = 1;
        else if (len == cols && i + len < n && s[i + len] != ' ' && brk > 0) {
            take = brk;
            skip = 1;
        } else if (i + len < n && s[i + len] == ' ') skip = 1;
        if (line >= first && line < first + max) {
            for (int k = 0; k < take; k++) buf[k] = s[i + k];
            buf[take] = 0;
            display_text(x, y + (line - first) * lh, buf, col);
        }
        line++;
        i += take + skip;
        if (take == 0 && skip == 0) i++;
    }
    return line;
}

static void draw_idle(int fresh_soft_notice)
{
    display_clear(C_BG);
    topbar("AGENT GUARD");
    int cx = SW / 2;
    char b[48];
    if (!net_up) {
        shield(cx, 70, 22, spr_mono ? 0xFFFF : SPR_RGB(60, 70, 100));
        g_text_large_c(cx, 112, "Waiting for Wi-Fi", C_WARN);
        g_text_c(cx, 134, "Connect in Settings > Wi-Fi,", C_DIM);
        g_text_c(cx, 146, "then come back here.", C_DIM);
    } else {
        uint32_t ph = g_now() / 40;
        int pulse = (int)(ph % 50);
        shield(cx, 66, 22, C_ACC);
        if (!spr_mono) display_circle(cx, 70, 34 + pulse / 5, SPR_RGB(40, 70 + pulse, 120 + pulse));
        check_mark(cx, 70, C_BG);
        g_text_large_c(cx, 112, "Waiting for agents", C_TEXT);
        b[0] = 0;
        g_cat(b, ip_str);
        g_cat(b, ":");
        g_cat_int(b, AG_PORT);
        g_text_c(cx, 138, b, C_ACC);
    }
    b[0] = 0;
    g_cat(b, "approved ");
    g_cat_int(b, (int)n_ok);
    g_cat(b, "   denied ");
    g_cat_int(b, (int)n_no);
    g_text_c(cx, 166, b, C_DIM);
    if (hist_n) {
        char l[60];
        l[0] = 0;
        g_cat(l, hist[0].decision == D_APPROVED ? "last: OK " : "last: NO ");
        g_cat(l, hist[0].action);
        g_cat(l, ": ");
        int room = (SW - 20) / 8 - strlen(l);
        int k = strlen(l), t = 0;
        for (; hist[0].summary[t] && room > 0; t++, room--) l[k++] = hist[0].summary[t];
        if (hist[0].summary[t] && k > 2) l[k - 1] = l[k - 2] = '.';
        l[k] = 0;
        g_text_c(cx, 182, l, hist[0].decision == D_APPROVED ? C_OK : C_NO);
    }
    if (n_badauth) {
        b[0] = 0;
        g_cat_int(b, (int)n_badauth);
        g_cat(b, " unpaired request(s) refused");
        g_text_c(cx, 198, b, C_WARN);
    } else if (key_err && key_kind == KEY_SE050) {
        b[0] = 0;
        g_cat(b, "secure element error ");
        g_cat_int(b, key_err);
        g_text_c(cx, 198, b, C_WARN);
    }
    footer("X pair   Y history   HOME exit");
    if (fresh_soft_notice) {
        spr_panel(16, 34, SW - 32, 170, "NO SECURE ELEMENT");
        g_text_c(cx, 70, "This console signs with a", C_TEXT);
        g_text_c(cx, 82, "SOFTWARE key kept in settings.", C_WARN);
        g_text_c(cx, 100, "Fine for trying Agent Guard,", C_TEXT);
        g_text_c(cx, 112, "not for guarding real money", C_TEXT);
        g_text_c(cx, 124, "or production systems.", C_TEXT);
        g_text_c(cx, 146, "Firmware with the SE050 enabled", C_DIM);
        g_text_c(cx, 158, "keeps the key in hardware.", C_DIM);
        g_text_c(cx, 184, "A: got it", C_ACC);
    }
}

static void draw_pair(void)
{
    display_clear(C_BG);
    topbar("PAIR AN AGENT");
    int cx = SW / 2;
    char b[64];
    g_text_c(cx, 32, "On the computer that runs your agent:", C_DIM);
    b[0] = 0;
    g_cat(b, "akira-guard pair ");
    g_cat(b, net_up ? ip_str : "<console-ip>");
    display_rounded_rect_fill(14, 46, SW - 28, 20, 4, spr_mono ? 0x0000 : SPR_RGB(4, 6, 14));
    if (spr_mono) display_rounded_rect(14, 46, SW - 28, 20, 4, 0xFFFF);
    g_text_c(cx, 51, b, C_OK);
    g_text_c(cx, 76, "and enter this pairing code:", C_DIM);
    char code[10];
    for (int i = 0, n = 0; i < 8; i++) {
        if (i == 4) code[n++] = '-';
        code[n++] = pair_code[i];
        code[n] = 0;
    }
    int w = 9 * 17 - 1;
    display_text_huge(cx - w / 2, 92, code, C_WARN);
    g_text_c(cx, 132, "Check the key fingerprint matches:", C_DIM);
    g_text_large_c(cx, 146, fp_str, C_TEXT);
    g_text_c(cx, 176, key_kind == KEY_SE050 ? "the key never leaves the SE050"
                                            : "software key (no secure element)", key_kind == KEY_SE050 ? C_OK : C_WARN);
    footer("Y new code (unpairs all)   B back");
}

static void draw_history(void)
{
    display_clear(C_BG);
    topbar("HISTORY");
    if (!hist_n) g_text_c(SW / 2, 100, "No requests yet.", C_DIM);
    uint32_t now = g_now() / 1000;
    for (int k = 0; k < hist_n; k++) {
        int y = 30 + k * 24;
        hist_t *h = &hist[k];
        uint16_t c = h->decision == D_APPROVED ? C_OK : h->decision == D_DENIED ? C_NO : C_DIM;
        const char *tag = h->decision == D_APPROVED ? "OK" : h->decision == D_DENIED ? "NO"
                        : h->decision == D_TIMEOUT ? ".." : "--";
        display_rounded_rect_fill(6, y, 22, 18, 3, spr_mono ? 0x0000 : c);
        if (spr_mono) display_rounded_rect(6, y, 22, 18, 3, 0xFFFF);
        display_text(10, y + 4, tag, spr_mono ? 0xFFFF : C_BG);
        char l[64];
        l[0] = 0;
        g_cat(l, h->action);
        g_cat(l, ": ");
        g_cat(l, h->summary);
        int cols = (SW - 90) / 8;
        if (strlen(l) > cols) {
            l[cols - 2] = '.';
            l[cols - 1] = '.';
            l[cols] = 0;
        }
        display_text(34, y + 4, l, C_TEXT);
        char a[16];
        a[0] = 0;
        uint32_t ago = now - h->t;
        if (ago < 60) { g_cat_int(a, (int)ago); g_cat(a, "s"); }
        else if (ago < 3600) { g_cat_int(a, (int)(ago / 60)); g_cat(a, "m"); }
        else { g_cat_int(a, (int)(ago / 3600)); g_cat(a, "h"); }
        g_text_r(SW - 6, y + 4, a, C_DIM);
    }
    footer("B back");
}

static void draw_request(void)
{
    areq_t *r = &P.r;
    uint16_t rc = risk_col(r->risk);
    display_clear(C_BG);
    /* header in the risk colour */
    display_rect(0, 0, SW, 24, spr_mono ? 0x0000 : rc);
    if (spr_mono) display_hline(0, 23, SW, 0xFFFF);
    const char *rk = r->risk[0] == 'h' ? "HIGH RISK" : r->risk[0] == 'l' ? "LOW RISK" : "AGENT REQUEST";
    display_text_large(6, 3, rk, 0xFFFF);
    char code[12];
    code[0] = '#';
    scopy(code + 1, P.code, sizeof(code) - 1);
    display_text(SW - g_tw(code) - 6, 8, code, 0xFFFF);

    char b[64];
    b[0] = 0;
    g_cat(b, r->agent);
    g_cat(b, " wants to:");
    display_text(8, 30, b, C_DIM);
    display_text_large(8, 42, r->action, spr_mono ? 0xFFFF : SPR_RGB(255, 214, 90));

    /* summary + details, scrollable */
    int cols = (SW - 24) / 8, top = 66, lh = 12;
    int max_lines = (SH - 46 - top) / lh;
    static char text[1300];
    text[0] = 0;
    g_cat(text, r->summary);
    if (r->details[0]) {
        g_cat(text, "\n\n");
        int k = strlen(text);
        for (int t = 0; r->details[t] && k < (int)sizeof(text) - 1; t++) text[k++] = r->details[t];
        text[k] = 0;
    }
    display_rect(4, top - 4, SW - 8, max_lines * lh + 6, spr_mono ? 0x0000 : SPR_RGB(16, 20, 38));
    P.lines = wrap(10, top, cols, P.scroll, max_lines, text, C_TEXT, lh);
    if (P.lines > max_lines) {
        int bar_h = max_lines * lh, kh = imax(8, bar_h * max_lines / P.lines);
        int ky = top + (bar_h - kh) * P.scroll / imax(1, P.lines - max_lines);
        display_rect(SW - 8, top, 3, bar_h, C_EDGE);
        display_rect(SW - 8, ky, 3, kh, C_ACC);
    }

    /* hold-to-approve bar */
    int by = SH - 40;
    display_rect(0, by - 4, SW, 44, C_PANEL);
    display_hline(0, by - 4, SW, C_EDGE);
    int bw = SW - 110;
    display_rounded_rect(8, by, bw, 18, 4, C_OK);
    int fill = (bw - 4) * P.hold_ms / HOLD_MS;
    if (fill > 0) display_rounded_rect_fill(10, by + 2, fill, 14, 3, C_OK);
    g_text_c(8 + bw / 2, by + 5, P.hold_ms ? "keep holding..." : "hold A+B: approve",
             P.hold_ms > HOLD_MS / 2 && !spr_mono ? C_BG : (spr_mono && fill > bw / 2 ? 0x0000 : C_TEXT));
    display_rounded_rect(SW - 96, by, 88, 18, 4, C_NO);
    g_text_c(SW - 52, by + 5, "X: deny", C_NO);
    /* time left */
    uint32_t now = g_now();
    int total = (int)(P.deadline - P.t0), left = (int)(P.deadline - now);
    if (left < 0) left = 0;
    int tw = (SW - 16) * left / imax(1, total);
    display_rect(8, by + 24, SW - 16, 4, spr_mono ? 0x0000 : SPR_RGB(40, 46, 70));
    display_rect(8, by + 24, tw, 4, left < 10000 ? C_WARN : C_ACC);
    if (spr_mono) display_rect_outline(8, by + 24, SW - 16, 4, 0xFFFF);
    b[0] = 0;
    g_cat_int(b, (left + 999) / 1000);
    g_cat(b, "s");
    g_text_r(SW - 8, by + 30, b, C_DIM);
}

static void draw_flash(void)
{
    int d = flash_decision;
    uint16_t c = d == D_APPROVED ? C_OK : d == D_DENIED ? C_NO : C_WARN;
    int w = SW - 60, h = 120, x = 30, y = (SH - h) / 2;
    display_rounded_rect_fill(x, y, w, h, 8, spr_mono ? 0x0000 : SPR_RGB(14, 18, 34));
    display_rounded_rect(x, y, w, h, 8, c);
    display_rounded_rect(x + 1, y + 1, w - 2, h - 2, 7, c);
    const char *t = d == D_APPROVED ? "APPROVED" : d == D_DENIED ? "DENIED" : d == D_TIMEOUT ? "TIMED OUT" : "WITHDRAWN";
    if (d == D_APPROVED) check_mark(SW / 2, y + 34, c);
    else cross_mark(SW / 2, y + 34, c);
    g_text_large_c(SW / 2, y + 62, t, c);
    if (flash_note[0]) g_text_c(SW / 2, y + 90, flash_note, C_DIM);
    else if (d == D_APPROVED || d == D_DENIED) g_text_c(SW / 2, y + 90, key_kind == KEY_SE050 ? "signed by the SE050" : "signed (software key)", C_DIM);
}

/* ── Main loop ───────────────────────────────────────────────────────── */
enum { SCR_IDLE, SCR_PAIR, SCR_HISTORY };

int main(void)
{
    printf("AkiraOS Agent Guard");
    g_init();
    SW = G_W;
    SH = G_H;
    spr_init(SW >= 400);
    theme();
    if (spr_mono) {
        g_theme.bg = 0x0000;
        g_theme.border = g_theme.text = g_theme.dim = g_theme.title = 0xFFFF;
        g_theme.sel_bg = 0xFFFF;
        g_theme.sel_text = 0x0000;
    }
    for (int i = 0; i < MAX_CLIENTS; i++) clients[i].h = -1;

    display_clear(C_BG);
    g_text_large_c(SW / 2, SH / 2 - 9, "Preparing key...", C_DIM);
    display_flush();
    int notice = setup_keys();
    if (notice < 0) {
        display_clear(C_BG);
        g_text_c(SW / 2, SH / 2, "Could not create a signing key.", C_NO);
        display_flush();
        g_wait_key();
        app_switch("supervisor");
        return 0;
    }
    if (load_str("ag/code", pair_code, sizeof(pair_code)) != 0 || strlen(pair_code) != 8) new_pair_code();
    else derive_pair_key();
    counter = (uint32_t)g_load_int("ag/ctr", 0);
    n_ok = (uint32_t)g_load_int("ag/nok", 0);
    n_no = (uint32_t)g_load_int("ag/nno", 0);

    int scr = SCR_IDLE;
    uint32_t last = g_now();
    for (;;) {
        uint32_t now = g_now();
        int dt = (int)(now - last);
        last = now;
        if (dt < 0 || dt > 200) dt = 0;

        net_poll();

        uint32_t k;
        if (P.active) {
            while ((k = g_key()) != 0) {
                if (k == K_X) finish(D_DENIED);
                else if (k == K_UP && P.scroll > 0) P.scroll--;
                else if (k == K_DOWN && P.scroll < P.lines - 1) P.scroll++;
                if (!P.active) break;
            }
            if (P.active) {
                uint32_t held = (uint32_t)input_get_buttons();
                if ((held & AKIRA_BTN_A) && (held & AKIRA_BTN_B)) {
                    P.hold_ms += dt;
                    if (P.hold_ms >= HOLD_MS) finish(D_APPROVED);
                } else if (P.hold_ms > 0) {
                    P.hold_ms = imax(0, P.hold_ms - 2 * dt); /* let go: drains */
                }
            }
            if (P.active && P.withdrawn) finish(D_WITHDRAWN);
            if (P.active && (int32_t)(now - P.deadline) >= 0) finish(D_TIMEOUT);
        } else {
            while ((k = g_key()) != 0) {
                if (notice) {
                    if (k == K_A || k == K_B) notice = 0;
                    continue;
                }
                if (scr == SCR_IDLE) {
                    if (k == K_X) scr = SCR_PAIR;
                    else if (k == K_Y) scr = SCR_HISTORY;
                    else if (k == K_HOME || k == K_B) {
                        for (int i = 0; i < MAX_CLIENTS; i++) client_close(i);
                        if (listener >= 0) net_close(listener);
                        app_switch("supervisor");
                        return 0;
                    }
                } else if (scr == SCR_PAIR) {
                    if (k == K_Y) new_pair_code();
                    else if (k == K_B || k == K_HOME || k == K_X) scr = SCR_IDLE;
                } else if (k == K_B || k == K_HOME || k == K_Y) {
                    scr = SCR_IDLE;
                }
            }
        }

        if (P.active) draw_request();
        else if (scr == SCR_PAIR) draw_pair();
        else if (scr == SCR_HISTORY) draw_history();
        else draw_idle(notice);
        if (flash_decision >= 0 && !P.active) {
            if ((int32_t)(now - flash_until) < 0) draw_flash();
            else flash_decision = -1;
        }
        display_flush();
        delay(P.active ? 30000 : 50000);
    }
}
