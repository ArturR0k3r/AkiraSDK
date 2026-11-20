/**
 * @file main.c
 * @brief I2C Bus Scanner + Register Poke — a Bus-Pirate-style bring-up tool.
 *
 * SCAN screen : probes every 7-bit address on the selected bus and shows a
 *               live grid of which ones ACK, with friendly names for parts
 *               known to live on the AkiraConsole.
 * POKE screen : hex-dumps a device's registers and lets you edit and write
 *               one, gated behind a confirm prompt so a fat-finger can't
 *               brick a fuel gauge.
 *
 * Controls (SCAN):
 *   DPAD  = move cursor over the address grid
 *   A     = open the highlighted device in the register poker
 *   X     = toggle bus 0 / 1
 *   Y     = rescan
 *   B     = exit to launcher
 *
 * Controls (POKE):
 *   UP/DOWN    = register -/+ 1        LEFT/RIGHT = register -/+ 16 (page)
 *   A          = re-read the window    X = edit the byte to write (UP/DOWN)
 *   Y          = write the byte (asks to confirm)   B = back to scan
 *
 * @copyright Copyright (c) 2026 PenEngineering S.R.L
 * @license Apache-2.0
 */

#include "akira_api.h"

/* ── Display ─────────────────────────────────────────────────────────── */
static int32_t SCR_W = 320;
static int32_t SCR_H = 240;

/* ── Palette (RGB565; reads fine on the mono panel too) ──────────────── */
#define C_BG      0x0000
#define C_HDR     0x001F
#define C_HDR_TXT 0xFFFF
#define C_TEXT    0xFFFF
#define C_DIM     0x8410
#define C_GRID    0x39E7
#define C_PRESENT 0x07E0   /* green — device ACKed        */
#define C_ABSENT  0x2124   /* dark   — no device          */
#define C_CURSOR  0xFFE0   /* yellow cursor ring          */
#define C_KNOWN   0x07FF   /* cyan   — named part         */
#define C_WARN    0xF800   /* red    — write / danger     */
#define C_OK      0x07E0
#define C_FOOT_BG 0x2104

/* ── I2C address space ───────────────────────────────────────────────── */
#define ADDR_LO 0x08     /* 0x00-0x07 and 0x78-0x7F are reserved */
#define ADDR_HI 0x77
#define N_BUSES 2

static uint8_t present[128];   /* 1 = ACKed on the last scan */
static int  g_bus = 0;
static int  g_found = 0;

/* Known AkiraConsole parts — makes the scan self-documenting on this board.
 * Sourced from the KiCad-verified pin/part map. */
typedef struct { uint8_t addr; const char *name; } known_t;
static const known_t KNOWN[] = {
    { 0x21, "TCA6408 IOX" },
    { 0x2D, "LP5817 BL" },
    { 0x34, "LP5817 bcast" },
    { 0x48, "SE050 SE" },
    { 0x6A, "LSM6DS3 IMU" },
    { 0x70, "STC3115 gas" },
};
static const char *known_name(int addr) {
    for (unsigned i = 0; i < sizeof(KNOWN)/sizeof(KNOWN[0]); i++)
        if (KNOWN[i].addr == addr) return KNOWN[i].name;
    return 0;
}

/* ── Screens ─────────────────────────────────────────────────────────── */
enum { SCREEN_SCAN, SCREEN_POKE };
static int screen = SCREEN_SCAN;

/* Grid cursor (SCAN) */
static int cur_col = 0, cur_row = 0;    /* 16 cols x 8 rows over 0x00-0x7F */

/* Poke state */
static int poke_addr = 0x6A;
static int poke_reg  = 0x00;
static int poke_val  = 0x00;   /* byte staged for a write */
static int poke_edit = 0;      /* 1 = editing poke_val */
static uint8_t poke_win[16];   /* last read of reg..reg+15 */
static int poke_win_ok = 0;

/* ── Small hex helpers (no libc) ─────────────────────────────────────── */
static char hexd(int n) { n &= 0xF; return (char)(n < 10 ? '0'+n : 'A'+n-10); }
static void hex2(int v, char *o) { o[0]=hexd(v>>4); o[1]=hexd(v); o[2]='\0'; }

/* ── I2C probe ───────────────────────────────────────────────────────────
 * There is no bare address-ACK primitive, so we probe by reading one byte
 * from register 0x00.  A device that exists but NAKs a read of reg 0 is
 * vanishingly rare, and the read is non-destructive on all known parts. */
static int probe(int bus, int addr) {
    uint8_t b;
    return i2c_read_reg(bus, addr, 0x00, &b, 1) >= 0;
}

static void scan_bus(void) {
    g_found = 0;
    for (int a = 0; a < 128; a++) present[a] = 0;
    for (int a = ADDR_LO; a <= ADDR_HI; a++) {
        if (probe(g_bus, a)) { present[a] = 1; g_found++; }
    }
}

static int read_window(void) {
    int n = i2c_read_reg(g_bus, poke_addr, poke_reg, poke_win,
                         (poke_reg + 16 <= 256) ? 16 : (256 - poke_reg));
    poke_win_ok = (n >= 0);
    if (!poke_win_ok) for (int i = 0; i < 16; i++) poke_win[i] = 0;
    return poke_win_ok;
}

/* ── Shared chrome ───────────────────────────────────────────────────── */
/* The built-in font advances 6px per glyph; compute widths locally rather
 * than depend on an SDK text-metrics call that may not be present. */
static int text_px(const char *s) { int n=0; while (s && s[n]) n++; return n*6; }

static void header(const char *title, const char *right) {
    display_rect(0, 0, SCR_W, 18, C_HDR);
    display_text(6, 5, title, C_HDR_TXT);
    if (right) display_text(SCR_W - 8 - text_px(right), 5, right, C_HDR_TXT);
}

static void footer(const char *hint) {
    display_rect(0, SCR_H - 14, SCR_W, 14, C_FOOT_BG);
    display_text(4, SCR_H - 12, hint, C_DIM);
}

/* ── SCAN screen ─────────────────────────────────────────────────────── */
static void draw_scan(void) {
    display_clear(C_BG);

    char busr[10] = { 'B','U','S',' ', (char)('0'+g_bus), 0 };
    header("I2C SCANNER", busr);

    /* Grid geometry — 16 columns, 8 rows, width-adaptive. */
    int gx = 10, gy = 26;
    int cw = (SCR_W - 2*gx) / 16;
    int ch = cw;
    if (ch > 14) ch = 14;

    /* Column/row hex guides */
    for (int c = 0; c < 16; c++) {
        char h[3]; hex2(c, h);
        display_text(gx + c*cw + (cw-6)/2, gy - 9, h+1, C_DIM);   /* low nibble */
    }

    for (int r = 0; r < 8; r++) {
        char rh[3]; hex2(r << 4, rh);
        display_text(gx - 9, gy + r*ch + (ch-8)/2, rh, C_DIM);    /* high nibble */
        for (int c = 0; c < 16; c++) {
            int addr = (r << 4) | c;
            int x = gx + c*cw, y = gy + r*ch;
            int reserved = (addr < ADDR_LO || addr > ADDR_HI);
            uint16_t fill = reserved ? C_ABSENT
                          : present[addr] ? (known_name(addr) ? C_KNOWN : C_PRESENT)
                          : C_ABSENT;
            display_rect(x+1, y+1, cw-2, ch-2, fill);
            if (present[addr]) {
                /* a small tick so it reads on the mono panel too */
                display_rect(x + cw/2 - 1, y + ch/2 - 1, 2, 2, C_BG);
            }
            if (c == cur_col && r == cur_row)
                display_rect_outline(x, y, cw, ch, C_CURSOR);
        }
    }

    /* Detail line for the highlighted address */
    int sel = (cur_row << 4) | cur_col;
    int info_y = gy + 8*ch + 8;
    char line[40], hb[3]; hex2(sel, hb);
    int p = 0;
    const char *pfx = "0x";
    line[p++]=pfx[0]; line[p++]=pfx[1]; line[p++]=hb[0]; line[p++]=hb[1];
    line[p++]=' '; line[p++]='-'; line[p++]=' '; line[p]='\0';
    display_text(10, info_y, line,
                 (sel<ADDR_LO||sel>ADDR_HI) ? C_DIM : C_TEXT);
    const char *kn = known_name(sel);
    if (sel < ADDR_LO || sel > ADDR_HI)
        display_text(10 + 8*6, info_y, "reserved", C_DIM);
    else if (present[sel])
        display_text(10 + 8*6, info_y, kn ? kn : "device present",
                     kn ? C_KNOWN : C_OK);
    else
        display_text(10 + 8*6, info_y, "no ACK", C_DIM);

    /* Found count */
    char cnt[24] = "FOUND: ";
    display_number(10 + 7*6, info_y + 14, g_found, C_OK);
    display_text(10, info_y + 14, cnt, C_DIM);

    footer("DPAD move  A poke  X bus  Y rescan  B exit");
    display_flush();
}

/* ── POKE screen ─────────────────────────────────────────────────────── */
static void draw_poke(void) {
    display_clear(C_BG);

    char hb[3]; hex2(poke_addr, hb);
    char title[24] = { 'P','O','K','E',' ','0','x', hb[0], hb[1], 0 };
    char busr[10]  = { 'B','U','S',' ', (char)('0'+g_bus), 0 };
    header(title, busr);

    const char *kn = known_name(poke_addr);
    display_text(6, 22, kn ? kn : "(unknown part)", kn ? C_KNOWN : C_DIM);

    /* Register window: base row address on the left, 16 bytes across. */
    int base = poke_reg & 0xF0;
    int y0 = 40;
    display_text(6, y0 - 12, "REG  +0 +1 +2 +3 +4 +5 +6 +7  ...F", C_DIM);

    for (int row = 0; row < 2; row++) {
        int rbase = base + row*16;
        if (rbase > 0xFF) break;
        char rb[3]; hex2(rbase, rb);
        int y = y0 + row*16;
        char pre[6] = { '0','x', rb[0], rb[1], 0 };
        display_text(6, y, pre, C_TEXT);

        for (int i = 0; i < 16 && rbase + i <= 0xFF; i++) {
            int reg = rbase + i;
            int x = 44 + i*16;
            uint8_t val = 0; int have = 0;
            /* poke_win holds poke_reg..poke_reg+15 */
            if (reg >= poke_reg && reg < poke_reg + 16) {
                val = poke_win[reg - poke_reg]; have = poke_win_ok;
            }
            char vb[3]; hex2(val, vb);
            uint16_t col = (reg == poke_reg) ? C_CURSOR : (have ? C_TEXT : C_DIM);
            display_text(x, y, have ? vb : "--", col);
            if (reg == poke_reg)
                display_rect_outline(x-1, y-1, 14, 11, C_CURSOR);
        }
    }

    /* Selected register detail. The window always begins at poke_reg, so its
     * first byte is the selected register's value. */
    int dy = y0 + 44;
    char rb[3]; hex2(poke_reg, rb);
    char vb[3]; hex2(poke_win[0], vb);

    display_text(6,        dy, "REG 0x", C_DIM);
    display_text(6 + 6*6,  dy, rb, C_TEXT);
    display_text(6 + 9*6,  dy, "=", C_DIM);
    display_text(6 + 11*6, dy, poke_win_ok ? vb : "??", poke_win_ok ? C_OK : C_WARN);

    /* Write value editor */
    int wy = dy + 16;
    char wv[3]; hex2(poke_val, wv);
    display_text(6, wy, "WRITE 0x", poke_edit ? C_WARN : C_DIM);
    display_text(6 + 8*6, wy, wv, poke_edit ? C_WARN : C_TEXT);
    if (poke_edit)
        display_rect_outline(6 + 8*6 - 1, wy - 1, 14, 11, C_WARN);
    display_text(6 + 12*6, wy, poke_edit ? "(UP/DOWN, Y=write)" : "(X to edit)",
                 C_DIM);

    footer("UP/DN reg  L/R page  A read  X edit  Y write  B back");
    display_flush();
}

/* ── Inline confirm (write is destructive) ───────────────────────────── */
static int confirm_write(void) {
    int bw = SCR_W*74/100, bh = 92;
    int bx = (SCR_W - bw)/2, by = (SCR_H - bh)/2;
    int sel = 0;   /* 0 = cancel, 1 = confirm */
    int prev = (int)input_get_buttons();

    char ab[3]; hex2(poke_addr, ab);
    char rb[3]; hex2(poke_reg,  rb);
    char vb[3]; hex2(poke_val,  vb);
    char line[40];
    int p=0; const char *w="WRITE 0x";
    for (const char *s=w; *s; s++) line[p++]=*s;
    line[p++]=vb[0]; line[p++]=vb[1];
    line[p++]=' '; line[p++]='-'; line[p++]='>'; line[p++]=' ';
    line[p++]='R'; line[p++]=rb[0]; line[p++]=rb[1];
    line[p]='\0';

    while (1) {
        display_rect(bx, by, bw, bh, C_BG);
        display_rect_outline(bx, by, bw, bh, C_WARN);
        display_rect_outline(bx+1, by+1, bw-2, bh-2, C_WARN);
        display_text(bx + 10, by + 8, "CONFIRM WRITE", C_WARN);
        display_text(bx + 10, by + 26, line, C_TEXT);
        char on[16] = "on 0x"; display_text(bx + 10, by + 40, on, C_DIM);
        display_text(bx + 10 + 5*6, by + 40, ab, C_TEXT);

        /* Cancel / Confirm buttons */
        int bwid = (bw - 30)/2, bhy = by + bh - 24;
        display_rect(bx+10,        bhy, bwid, 16, sel==0 ? C_OK   : C_FOOT_BG);
        display_rect(bx+20+bwid,   bhy, bwid, 16, sel==1 ? C_WARN : C_FOOT_BG);
        display_text(bx+10 + bwid/2 - 18, bhy+4, "CANCEL", sel==0 ? C_BG : C_DIM);
        display_text(bx+20+bwid + bwid/2 - 18, bhy+4, "WRITE", sel==1 ? C_BG : C_DIM);
        display_flush();

        int held = (int)input_get_buttons();
        int edge = held & ~prev; prev = held;
        if (edge & (AKIRA_BTN_LEFT|AKIRA_BTN_RIGHT)) sel ^= 1;
        if (edge & AKIRA_BTN_B) return 0;
        if (edge & AKIRA_BTN_A) return sel;
        delay(20000);
    }
}

/* ── Input: SCAN ─────────────────────────────────────────────────────── */
static int handle_scan(int edge) {
    if (edge & AKIRA_BTN_UP)    cur_row = (cur_row > 0) ? cur_row-1 : 7;
    if (edge & AKIRA_BTN_DOWN)  cur_row = (cur_row < 7) ? cur_row+1 : 0;
    if (edge & AKIRA_BTN_LEFT)  cur_col = (cur_col > 0) ? cur_col-1 : 15;
    if (edge & AKIRA_BTN_RIGHT) cur_col = (cur_col < 15) ? cur_col+1 : 0;

    if (edge & AKIRA_BTN_X) { g_bus = (g_bus+1) % N_BUSES; scan_bus(); }
    if (edge & AKIRA_BTN_Y) { scan_bus(); }

    if (edge & AKIRA_BTN_A) {
        int sel = (cur_row << 4) | cur_col;
        if (sel >= ADDR_LO && sel <= ADDR_HI) {
            poke_addr = sel;
            poke_reg = 0; poke_val = 0; poke_edit = 0;
            read_window();
            screen = SCREEN_POKE;
        }
    }
    if (edge & AKIRA_BTN_B) return 1;   /* exit */
    return 0;
}

/* ── Input: POKE ─────────────────────────────────────────────────────── */
static void handle_poke(int edge) {
    if (poke_edit) {
        if (edge & AKIRA_BTN_UP)   poke_val = (poke_val + 1) & 0xFF;
        if (edge & AKIRA_BTN_DOWN) poke_val = (poke_val - 1) & 0xFF;
        if (edge & AKIRA_BTN_X)    poke_edit = 0;
        if (edge & AKIRA_BTN_Y) {
            if (confirm_write()) {
                uint8_t b = (uint8_t)poke_val;
                int rc = i2c_write_reg(g_bus, poke_addr, poke_reg, &b, 1);
                (void)rc;
                read_window();
            }
            poke_edit = 0;
        }
        if (edge & AKIRA_BTN_B) poke_edit = 0;
        return;
    }

    if (edge & AKIRA_BTN_UP)    { poke_reg = (poke_reg - 1) & 0xFF; read_window(); }
    if (edge & AKIRA_BTN_DOWN)  { poke_reg = (poke_reg + 1) & 0xFF; read_window(); }
    if (edge & AKIRA_BTN_LEFT)  { poke_reg = (poke_reg - 16) & 0xFF; read_window(); }
    if (edge & AKIRA_BTN_RIGHT) { poke_reg = (poke_reg + 16) & 0xFF; read_window(); }
    if (edge & AKIRA_BTN_A)     read_window();
    if (edge & AKIRA_BTN_X)     { poke_val = poke_win_ok ? poke_win[0] : 0; poke_edit = 1; }
    if (edge & AKIRA_BTN_Y)     { poke_edit = 1; }   /* jump straight to write */
    if (edge & AKIRA_BTN_B)     screen = SCREEN_SCAN;
}

/* ── Entry point ─────────────────────────────────────────────────────── */
int main(void) {
    printf("AkiraOS I2C Scanner");
    display_get_size(&SCR_W, &SCR_H);

    scan_bus();

    int prev = (int)input_get_buttons();
    draw_scan();

    while (1) {
        int held = (int)input_get_buttons();
        int edge = held & ~prev;
        prev = held;

        if (edge) {
            if (screen == SCREEN_SCAN) {
                if (handle_scan(edge)) { app_switch("supervisor"); return 0; }
            } else {
                handle_poke(edge);
            }
        }

        if (screen == SCREEN_SCAN) draw_scan();
        else                       draw_poke();

        delay(30000);
    }
    return 0;
}
