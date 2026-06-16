/*
 * key_ui.c — AkiraKey UI: PIN entry, home, TOTP, FIDO2, passwords, SSH, settings
 * SPDX-License-Identifier: Apache-2.0
 *
 * Layout (320×240, 8×13 glyphs → 40 cols × 18 rows):
 *   Row 0        : header bar
 *   Rows 1–16   : content area
 *   Row 17       : action bar
 */
#include "key.h"

/* ── Text helpers ────────────────────────────────────────────────────── */
#define TX(c)  ((c) * GLYPH_W)
#define TY(r)  ((r) * GLYPH_H)

static void tput(int c, int r, const char *s, uint32_t col) {
    display_text(TX(c), TY(r), s, col);
}
static void trow_bg(int r, uint32_t col) {
    display_rect(0, TY(r), GW, GLYPH_H, col);
}
static void clear_content(void) {
    display_rect(0, GLYPH_H, GW, GH - GLYPH_H * 2, C_BG);
}

/* Number → string (fits in static 12-char buffer) */
static char _nb[12];
static const char *n2s(uint32_t v) {
    if (!v) { _nb[0]='0'; _nb[1]='\0'; return _nb; }
    int i = 0; uint32_t t = v;
    while (t) { _nb[i++] = '0' + t%10; t /= 10; }
    for (int a=0, b=i-1; a<b; a++,b--) { char x=_nb[a]; _nb[a]=_nb[b]; _nb[b]=x; }
    _nb[i] = '\0'; return _nb;
}

/* ── Header / footer bars ────────────────────────────────────────────── */
static void draw_header(const char *title, const char *right_tag, uint32_t hcol) {
    trow_bg(0, hcol);
    display_hline(0, GLYPH_H - 1, GW, C_ACCENT);
    tput(1, 0, title, C_ACCENT);
    if (right_tag && right_tag[0])
        tput(COLS - sv_len(right_tag) - 1, 0, right_tag, C_DIM);
}

static void draw_footer(const char *left, const char *right) {
    int br = ROWS - 1;
    trow_bg(br, C_HEADER);
    display_hline(0, TY(br), GW, C_ACCENT);
    if (left && left[0]) tput(1, br, left, C_DIM);
    if (right && right[0]) {
        tput(COLS - sv_len(right) - 1, br, right, C_ACCENT);
    }
}

static void draw_hbar(int col, int row, int val, int max, int w, uint32_t fg) {
    int f = (max > 0) ? val * w / max : 0;
    char bar[64]; int i = 0;
    for (; i < f && i < w; i++) bar[i] = '\xDB';
    for (; i < w; i++) bar[i] = '\xB0';
    bar[i] = '\0';
    display_rect(TX(col), TY(row), w * GLYPH_W, GLYPH_H, C_BG);
    display_text(TX(col), TY(row), bar, fg);
}

/* ── PIN entry state ─────────────────────────────────────────────────── */
static int pin_digits[PIN_DIGITS];  /* current digit values 0-9        */
static int pin_pos;                 /* cursor position 0..PIN_DIGITS-1  */
static int pin_attempts;            /* wrong-PIN counter                */
static int pin_first[PIN_DIGITS];   /* first-pass PIN for setup confirm */
static int pin_error_frames;        /* countdown for error flash        */

static void pin_reset(void) {
    for (int i = 0; i < PIN_DIGITS; i++) pin_digits[i] = 0;
    pin_pos = 0; pin_error_frames = 0;
}

/* Assemble pin_digits[] into a NUL-terminated decimal string */
static void pin_to_str(const int *digits, char *out) {
    for (int i = 0; i < PIN_DIGITS; i++) out[i] = '0' + digits[i];
    out[PIN_DIGITS] = '\0';
}

/* Draw the 4-digit PIN entry row */
static void draw_pin_row(int row, int show_values) {
    /* Centre the boxes: each box is "[ d ]" = 6 chars, 3 spaces between → 6*4+3*3 = 33 */
    int start_col = (COLS - 33) / 2;
    for (int i = 0; i < PIN_DIGITS; i++) {
        int cx = start_col + i * 9;
        uint32_t box_col = (i == pin_pos) ? C_ACCENT : C_DIM;
        char box[8];
        if (i < pin_pos) {
            /* already confirmed — show star */
            box[0]='['; box[1]=' '; box[2]='*'; box[3]=' '; box[4]=']'; box[5]='\0';
        } else if (i == pin_pos) {
            /* current digit — show value */
            char d = show_values ? ('0' + pin_digits[i]) : '_';
            box[0]='['; box[1]=' '; box[2]=d; box[3]=' '; box[4]=']'; box[5]='\0';
        } else {
            /* not yet reached */
            box[0]='['; box[1]=' '; box[2]='_'; box[3]=' '; box[4]=']'; box[5]='\0';
        }
        tput(cx, row, box, box_col);
    }
    /* Cursor indicator under current position */
    if (pin_pos < PIN_DIGITS) {
        int cx = start_col + pin_pos * 9 + 2;
        tput(cx, row + 1, "^", C_ACCENT);
    }
}

/* ── Active-slot index maps (fixes display-index vs vault-index bug) ── */
static int active_totp[MAX_TOTP_SLOTS];  int active_totp_cnt;
static int active_fido[MAX_FIDO2_SLOTS]; int active_fido_cnt;
static int active_pass[MAX_PASS_SLOTS];  int active_pass_cnt;

static void rebuild_totp_map(void) {
    active_totp_cnt = 0;
    for (int i = 0; i < MAX_TOTP_SLOTS; i++)
        if (g_key_vault.totp[i].active) active_totp[active_totp_cnt++] = i;
}
static void rebuild_fido_map(void) {
    active_fido_cnt = 0;
    for (int i = 0; i < MAX_FIDO2_SLOTS; i++)
        if (g_key_vault.fido2[i].active) active_fido[active_fido_cnt++] = i;
}
static void rebuild_pass_map(void) {
    active_pass_cnt = 0;
    for (int i = 0; i < MAX_PASS_SLOTS; i++)
        if (g_key_vault.pass[i].active) active_pass[active_pass_cnt++] = i;
}

/* ── Per-screen state ────────────────────────────────────────────────── */
static int     list_sel, list_scroll;
static uint32_t totp_code;
static int     totp_remain;
static int     hold_ms;             /* ms holding A for confirmation actions */

/* ── Screen draw functions ───────────────────────────────────────────── */

static void draw_boot(void) {
    static int spinner_frame = 0;
    static const char *spin = "-\\|/";

    display_clear(C_BG);
    int cy = ROWS / 2 - 3;
    tput(COLS/2-11, cy,   "+---------------------------+", C_ACCENT);
    tput(COLS/2-11, cy+1, "|      A K I R A K E Y      |", C_ACCENT);
    tput(COLS/2-11, cy+2, "|     Electronic Security    |", C_FG);
    tput(COLS/2-11, cy+3, "|   TOTP  +  FIDO2  +  SSH   |", C_DIM);
    tput(COLS/2-11, cy+4, "+---------------------------+", C_ACCENT);

    if (g_boot_usb_status == 1) {
        tput(COLS/2-9, cy+6, "USB Connected  [OK]", C_ACCENT);
    } else if (g_boot_usb_status == -1) {
        tput(COLS/2-9, cy+6, "No USB host — standalone", C_WARN);
    } else {
        /* Animated spinner while polling */
        char line[24];
        line[0]='W'; line[1]='a'; line[2]='i'; line[3]='t'; line[4]='i';
        line[5]='n'; line[6]='g'; line[7]=' '; line[8]='U'; line[9]='S';
        line[10]='B'; line[11]=' '; line[12]=' ';
        line[13]='['; line[14]=spin[spinner_frame & 3]; line[15]=']';
        line[16]='\0';
        spinner_frame++;
        tput(COLS/2-8, cy+6, line, C_DIM);
    }
    display_flush();
}

static void draw_pin_entry(void) {
    display_clear(C_BG);
    uint32_t hcol = (pin_error_frames > 0) ? C_DANGER : C_HEADER;
    draw_header("AKIRAKEY", "USB", hcol);
    clear_content();

    int mid = ROWS / 2;
    if (pin_error_frames > 0) {
        tput(COLS/2 - 7, mid - 3, "WRONG PIN", C_DANGER);
        char abuf[24]; int ai = 0;
        sv_ncpy(abuf, "Attempts: ", 11); ai = 10;
        abuf[ai++] = '0' + pin_attempts;
        abuf[ai++] = '/';
        abuf[ai++] = '0' + PIN_MAX_ATTEMPTS;
        abuf[ai] = '\0';
        tput(COLS/2 - ai/2, mid - 2, abuf, C_WARN);
    } else {
        tput(COLS/2 - 6, mid - 3, "UNLOCK VAULT", C_FG);
        char abuf[24];
        abuf[0]='A'; abuf[1]='t'; abuf[2]='t'; abuf[3]='e'; abuf[4]='m';
        abuf[5]='p'; abuf[6]='t'; abuf[7]=' ';
        abuf[8]='0'+pin_attempts+1; abuf[9]=' '; abuf[10]='o'; abuf[11]='f'; abuf[12]=' ';
        abuf[13]='0'+PIN_MAX_ATTEMPTS; abuf[14]='\0';
        tput(COLS/2 - 7, mid - 2, abuf, C_DIM);
    }

    draw_pin_row(mid, 1);
    tput(2, mid + 3, "UP/DN change  RIGHT confirm  LEFT back", C_DIM);

    draw_footer("[B] clear", "[A] next >");
    display_flush();
}

static void draw_pin_setup(int confirm_step) {
    display_clear(C_BG);
    draw_header("AKIRAKEY", confirm_step ? "CONFIRM" : "NEW PIN", C_HEADER);
    clear_content();

    int mid = ROWS / 2;
    if (confirm_step) {
        tput(COLS/2 - 7, mid - 3, "CONFIRM PIN", C_ACCENT);
        tput(2, mid - 2, "Re-enter your PIN to confirm.", C_DIM);
    } else {
        tput(COLS/2 - 6, mid - 3, "SET YOUR PIN", C_ACCENT);
        tput(2, mid - 2, "Choose a 4-digit PIN.", C_DIM);
    }

    draw_pin_row(mid, 1);
    tput(2, mid + 3, "UP/DN change  RIGHT confirm  LEFT back", C_DIM);

    draw_footer("", "[A] next >");
    display_flush();
}

static void draw_home(void) {
    display_clear(C_BG);
    draw_header("AKIRAKEY", g_key_vault.ble_enabled ? "BLE" : "USB", C_HEADER);
    clear_content();

    static const char *items[] = {
        "  TOTP / 2FA          RFC 6238",
        "  FIDO2 / WebAuthn    passkey ",
        "  Passwords           BLE type",
        "  SSH Agent           ed25519 ",
        "  Settings                    ",
    };
    for (int i = 0; i < 5; i++) {
        int row = 2 + i * 2 + (i >= 2 ? 1 : 0); /* slight spacing */
        if (row >= ROWS - 1) break;
        if (i == list_sel) {
            trow_bg(row, C_SEL_BG); tput(1, row, items[i], C_SEL_FG);
        } else {
            tput(1, row, items[i], i == list_sel ? C_SEL_FG : C_FG);
        }
    }
    tput(2, ROWS-3, "[SET] lock    [B] exit app", C_DIM);
    draw_footer("[B] exit", "[A] enter >");
    display_flush();
}

static void draw_totp_list(void) {
    rebuild_totp_map();
    display_clear(C_BG);
    draw_header("TOTP / 2FA", n2s(active_totp_cnt), C_HEADER);
    clear_content();

    if (active_totp_cnt == 0) {
        tput(2, ROWS/2, "No TOTP slots active.", C_DIM);
        tput(2, ROWS/2+1, "Add via companion app.", C_DIM);
    } else {
        int row = 2, shown = 0;
        for (int i = 0; i < active_totp_cnt; i++) {
            if (shown < list_scroll) { shown++; continue; }
            if (row >= ROWS - 2) break;
            int vi = active_totp[i];
            char buf[42]; buf[0]=' '; buf[1]=' ';
            sv_cpy(buf+2, g_key_vault.totp[vi].label, 38);
            if ((i - list_scroll) == list_sel) {
                trow_bg(row, C_SEL_BG); tput(0, row, buf, C_SEL_FG);
            } else {
                tput(0, row, buf, C_FG);
            }
            row++; shown++;
        }
    }
    draw_footer("[B] back", "[A] view >");
    display_flush();
}

static void draw_totp_view(void) {
    display_clear(C_BG);
    draw_header("TOTP CODE", "", C_HEADER);
    clear_content();

    int vi = active_totp[list_sel];
    totp_slot_t *sl = &g_key_vault.totp[vi];
    char lbuf[36]; lbuf[0]=' '; sv_cpy(lbuf+1, sl->label, 34);
    tput(1, 1, lbuf, C_FG);

    int digits = sl->digits > 0 ? sl->digits : 6;
    int period = sl->period > 0 ? sl->period : 30;

    /* Big code display — split at midpoint */
    uint32_t code = totp_code;
    char cbuf[10];
    for (int i = digits-1; i >= 0; i--) { cbuf[i] = '0' + code % 10; code /= 10; }
    cbuf[digits] = '\0';
    int half = digits / 2;
    char disp[16]; int di = 0;
    for (int i = 0; i < half; i++) disp[di++] = cbuf[i];
    disp[di++] = ' '; disp[di++] = ' ';
    for (int i = half; i < digits; i++) disp[di++] = cbuf[i];
    disp[di] = '\0';
    display_text_large(TX(COLS/2 - digits/2 - 1), TY(3), disp, C_ACCENT);

    /* Progress bar + countdown */
    int remain = totp_remain > 0 ? totp_remain : period;
    uint32_t bar_col = remain > 20 ? C_ACCENT : (remain > 10 ? C_WARN : C_DANGER);
    draw_hbar(1, 5, remain, period, COLS-6, bar_col);
    char tbuf[8]; sv_cpy(tbuf, n2s(remain), 6); sv_cpy(tbuf+sv_len(tbuf), "s", 2);
    tput(COLS-5, 5, tbuf, bar_col);

    if (g_key_vault.ble_enabled)
        tput(2, 7, "[A] Type code via BLE keyboard", C_DIM);
    else
        tput(2, 7, "Enable BLE in Settings to type", C_WARN);

    draw_footer("[B] back", g_key_vault.ble_enabled ? "[A] type" : "");
    display_flush();
}

static void draw_fido2_list(void) {
    rebuild_fido_map();
    display_clear(C_BG);
    draw_header("FIDO2 / WebAuthn", n2s(active_fido_cnt), C_HEADER);
    clear_content();

    if (active_fido_cnt == 0) {
        tput(2, ROWS/2-1, "No credentials stored.", C_DIM);
        tput(2, ROWS/2,   "Add via WebAuthn enrollment.", C_DIM);
    } else {
        int row = 2;
        for (int i = 0; i < active_fido_cnt; i++) {
            if (row >= ROWS - 2) break;
            int vi = active_fido[i];
            char buf[42]; buf[0]=' '; buf[1]=' ';
            sv_cpy(buf+2, g_key_vault.fido2[vi].rp_id, 28);
            int bl = sv_len(buf); buf[bl]=' '; buf[bl+1]=' ';
            sv_cpy(buf+bl+2, g_key_vault.fido2[vi].user_name, 12);
            if (i == list_sel) {
                trow_bg(row, C_SEL_BG); tput(0, row, buf, C_SEL_FG);
            } else {
                tput(0, row, buf, C_FG);
            }
            row++;
        }
    }
    draw_footer("[B] back", "[A] detail >");
    display_flush();
}

static void draw_fido2_view(void) {
    display_clear(C_BG);
    draw_header("FIDO2 CREDENTIAL", "", C_DANGER);
    clear_content();

    int vi = active_fido[list_sel];
    fido2_cred_t *cr = &g_key_vault.fido2[vi];
    tput(2, 2,  "RP:   ", C_DIM); tput(8,  2, cr->rp_id,    C_FG);
    tput(2, 3,  "User: ", C_DIM); tput(8,  3, cr->user_name, C_FG);
    tput(2, 4,  "Signs:", C_DIM); tput(8,  4, n2s(cr->sign_count), C_FG);
    tput(2, 6,  "Hold [A] to confirm sign request.", C_WARN);

    int pct = hold_ms * 100 / 2000; /* 2s hold to sign */
    if (pct > 100) pct = 100;
    if (hold_ms > 0) draw_hbar(2, 8, pct, 100, COLS-4, C_ACCENT);

    draw_footer("[B] back", "[A] sign");
    display_flush();
}

static void draw_pass_list(void) {
    rebuild_pass_map();
    display_clear(C_BG);
    draw_header("PASSWORDS", n2s(active_pass_cnt), C_HEADER);
    clear_content();

    if (active_pass_cnt == 0) {
        tput(2, ROWS/2, "No passwords stored.", C_DIM);
        tput(2, ROWS/2+1, "Add via companion app.", C_DIM);
    } else {
        int row = 2;
        for (int i = 0; i < active_pass_cnt; i++) {
            if (row >= ROWS - 2) break;
            int vi = active_pass[i];
            char buf[40]; buf[0]=' '; buf[1]=' ';
            sv_cpy(buf+2, g_key_vault.pass[vi].label, 36);
            if (i == list_sel) {
                trow_bg(row, C_SEL_BG); tput(0, row, buf, C_SEL_FG);
            } else {
                tput(0, row, buf, C_FG);
            }
            row++;
        }
    }
    draw_footer("[B] back", "[A] view >");
    display_flush();
}

static void draw_pass_view(void) {
    display_clear(C_BG);
    draw_header("PASSWORD", "", C_HEADER);
    clear_content();

    int vi = active_pass[list_sel];
    pass_slot_t *p = &g_key_vault.pass[vi];
    tput(2, 1, p->label, C_ACCENT);
    display_hline(0, TY(2) + GLYPH_H/2, GW, C_DIM);
    tput(2, 3, "User: ", C_DIM); tput(8, 3, p->username, C_FG);
    tput(2, 4, "Pass: ", C_DIM); tput(8, 4, "* * * * * * * *", C_DIM);
    if (g_key_vault.ble_enabled) {
        tput(2, 6, "[A] Type username via BLE", C_DIM);
        tput(2, 7, "[UP] Type password via BLE", C_DIM);
    } else {
        tput(2, 6, "Enable BLE in Settings to type", C_WARN);
    }
    draw_footer("[B] back", g_key_vault.ble_enabled ? "[A] user" : "");
    display_flush();
}

static void draw_ssh_view(void) {
    display_clear(C_BG);
    draw_header("SSH AGENT", "ed25519", C_HEADER);
    clear_content();
    tput(2, 2, "Public key fingerprint:", C_DIM);
    char hex[36]; int hi = 0;
    static const char hx[] = "0123456789abcdef";
    for (int i = 0; i < 8; i++) {
        hex[hi++] = hx[g_key_vault.ssh_pub[i] >> 4];
        hex[hi++] = hx[g_key_vault.ssh_pub[i] & 0xF];
        if (i == 3) hex[hi++] = ':';
    }
    hex[hi] = '\0';
    tput(2, 3, hex, C_FG);
    tput(2, 5, "Waiting for SSH challenge via BLE.", C_DIM);
    if (g_key_vault.ble_enabled)
        tput(2, 7, "[A] Export pubkey via BLE", C_ACCENT);
    else
        tput(2, 7, "Enable BLE in Settings", C_WARN);
    draw_footer("[B] back", g_key_vault.ble_enabled ? "[A] export" : "");
    display_flush();
}

static void draw_settings(void) {
    display_clear(C_BG);
    draw_header("SETTINGS", "", C_HEADER);
    clear_content();

    struct { const char *label; const char *tag; uint32_t tcol; } items[] = {
        { "  BLE keyboard",  g_key_vault.ble_enabled ? "ON " : "OFF",
          g_key_vault.ble_enabled ? C_ACCENT : C_DIM },
        { "  Factory reset", "", C_DANGER },
        { "  About",         "", C_FG     },
    };
    for (int i = 0; i < 3; i++) {
        int row = 2 + i * 2;
        if (i == list_sel) {
            trow_bg(row, C_SEL_BG);
            tput(1, row, items[i].label, C_SEL_FG);
            tput(COLS-4, row, items[i].tag, C_SEL_FG);
        } else {
            tput(1, row, items[i].label, C_FG);
            tput(COLS-4, row, items[i].tag, items[i].tcol);
        }
    }
    draw_footer("[B] back", "[A] select");
    display_flush();
}

static void draw_factory_reset(void) {
    display_clear(C_BG);
    trow_bg(0, C_DANGER);
    draw_header("!! DANGER !!", "", C_DANGER);
    clear_content();
    trow_bg(2, C_DANGER);
    tput(2, 2, "FACTORY RESET — ALL DATA DELETED", C_FG);
    tput(2, 4, "Hold [A] for 3 seconds to confirm.", C_WARN);
    int pct = hold_ms * 100 / 3000;
    if (pct > 100) pct = 100;
    if (hold_ms > 0) draw_hbar(2, 6, pct, 100, COLS-4, C_DANGER);
    draw_footer("[B] cancel", "");
    display_flush();
}

static void draw_about(void) {
    display_clear(C_BG);
    draw_header("ABOUT AKIRAKEY", "", C_HEADER);
    clear_content();
    tput(2, 2,  "AkiraKey v1.1", C_ACCENT);
    tput(2, 3,  "Electronic Security Key", C_FG);
    tput(2, 5,  "Features:", C_DIM);
    tput(4, 6,  "TOTP/HOTP (RFC 6238/4226)", C_DIM);
    tput(4, 7,  "FIDO2 / WebAuthn (CTAP2)", C_DIM);
    tput(4, 8,  "Password manager + BLE type", C_DIM);
    tput(4, 9,  "SSH Agent (ed25519)", C_DIM);
    tput(2, 11, "Vault encrypted with ChaCha20.", C_DIM);
    tput(2, 12, "Key from PBKDF2-SHA256(PIN).", C_DIM);
    tput(2, 14, "TOTP uses HMAC-SHA1 (RFC 4226).", C_DIM);
    draw_footer("[B] back", "");
    display_flush();
}

/* ── ui_init ─────────────────────────────────────────────────────────── */
void ui_init(void) {
    list_sel   = 0;
    list_scroll = 0;
    hold_ms    = 0;
    totp_remain = 0;
    totp_code   = 0;

    switch (g_screen) {
    case SCR_PIN_ENTRY:
        pin_reset();
        break;
    case SCR_PIN_SETUP_1:
        pin_reset();
        pin_attempts = 0;
        break;
    case SCR_PIN_SETUP_2:
        pin_reset();
        break;
    case SCR_TOTP_LIST:
        rebuild_totp_map();
        break;
    case SCR_FIDO2_LIST:
        rebuild_fido_map();
        break;
    case SCR_PASS_LIST:
        rebuild_pass_map();
        break;
    default:
        break;
    }
}

/* ── ui_draw ─────────────────────────────────────────────────────────── */
void ui_draw(void) {
    switch (g_screen) {
    case SCR_BOOT:          draw_boot();          break;
    case SCR_PIN_ENTRY:     draw_pin_entry();     break;
    case SCR_PIN_SETUP_1:   draw_pin_setup(0);    break;
    case SCR_PIN_SETUP_2:   draw_pin_setup(1);    break;
    case SCR_HOME:          draw_home();          break;
    case SCR_TOTP_LIST:     draw_totp_list();     break;
    case SCR_TOTP_VIEW:     draw_totp_view();     break;
    case SCR_FIDO2_LIST:    draw_fido2_list();    break;
    case SCR_FIDO2_VIEW:    draw_fido2_view();    break;
    case SCR_PASS_LIST:     draw_pass_list();     break;
    case SCR_PASS_VIEW:     draw_pass_view();     break;
    case SCR_SSH_VIEW:      draw_ssh_view();      break;
    case SCR_SETTINGS:      draw_settings();      break;
    case SCR_FACTORY_RESET: draw_factory_reset(); break;
    case SCR_ABOUT:         draw_about();         break;
    default: break;
    }
}

/* ── Navigation helpers ──────────────────────────────────────────────── */
static void go(screen_t s) {
    g_prev_screen = g_screen; g_screen = s; ui_init();
}
static void back(void) {
    g_screen = g_prev_screen; ui_init();
}

/* ── PIN advance logic (shared by entry and setup screens) ───────────── */
static void pin_digit_up(void) {
    pin_digits[pin_pos] = (pin_digits[pin_pos] + 1) % 10;
}
static void pin_digit_down(void) {
    pin_digits[pin_pos] = (pin_digits[pin_pos] + 9) % 10;
}
static void pin_digit_next(void) {
    if (pin_pos < PIN_DIGITS - 1) { pin_pos++; return; }
    /* Last digit confirmed — signal completion by advancing past end */
    pin_pos = PIN_DIGITS;
}
static void pin_digit_prev(void) {
    if (pin_pos > 0) pin_pos--;
}
static int pin_complete(void) { return pin_pos >= PIN_DIGITS; }

/* ── ui_handle_key ───────────────────────────────────────────────────── */
void ui_handle_key(int key, int long_press) {
    (void)long_press;

    /* Global: SET always locks when unlocked */
    if (key == KEY_SET && !g_locked) {
        kstore_lock();
        g_screen = SCR_PIN_ENTRY;
        ui_init();
        return;
    }

    switch (g_screen) {

    /* ── PIN entry ─────────────────────────────────────── */
    case SCR_PIN_ENTRY:
        if (pin_error_frames > 0) { pin_error_frames--; return; }
        if (key == KEY_UP)    { pin_digit_up();   return; }
        if (key == KEY_DOWN)  { pin_digit_down(); return; }
        if (key == KEY_LEFT || key == KEY_B) {
            if (pin_pos == 0) { g_exit = 1; return; } /* exit app at first digit */
            pin_digit_prev(); return;
        }
        if (key == KEY_RIGHT || key == KEY_A) {
            pin_digit_next();
            if (!pin_complete()) return;
            /* Attempt unlock */
            char pstr[PIN_DIGITS + 1];
            pin_to_str(pin_digits, pstr);
            if (kstore_unlock(pstr) == 0) {
                pin_attempts = 0;
                g_screen = SCR_HOME; ui_init();
            } else {
                pin_attempts++;
                pin_reset();
                if (pin_attempts >= PIN_MAX_ATTEMPTS) {
                    /* Too many wrong PINs — wipe vault */
                    kstore_wipe();
                    pin_attempts = 0;
                    g_screen = SCR_PIN_SETUP_1; ui_init();
                } else {
                    pin_error_frames = 8; /* ~8 draw frames of red flash */
                }
            }
        }
        break;

    /* ── First-boot: set PIN ───────────────────────────── */
    case SCR_PIN_SETUP_1:
        if (key == KEY_UP)    { pin_digit_up();   return; }
        if (key == KEY_DOWN)  { pin_digit_down(); return; }
        if (key == KEY_LEFT || key == KEY_B) {
            if (pin_pos == 0) { g_exit = 1; return; } /* exit if already at start */
            pin_digit_prev(); return;
        }
        if (key == KEY_RIGHT || key == KEY_A) {
            pin_digit_next();
            if (!pin_complete()) return;
            /* Save first-pass PIN and move to confirm screen */
            for (int i = 0; i < PIN_DIGITS; i++) pin_first[i] = pin_digits[i];
            go(SCR_PIN_SETUP_2);
        }
        break;

    /* ── First-boot: confirm PIN ───────────────────────── */
    case SCR_PIN_SETUP_2:
        if (key == KEY_UP)    { pin_digit_up();   return; }
        if (key == KEY_DOWN)  { pin_digit_down(); return; }
        if (key == KEY_LEFT || key == KEY_B) {
            if (pin_pos == 0) { go(SCR_PIN_SETUP_1); return; }
            pin_digit_prev(); return;
        }
        if (key == KEY_RIGHT || key == KEY_A) {
            pin_digit_next();
            if (!pin_complete()) return;
            /* Compare */
            int match = 1;
            for (int i = 0; i < PIN_DIGITS; i++)
                if (pin_digits[i] != pin_first[i]) { match = 0; break; }
            if (match) {
                char pstr[PIN_DIGITS + 1];
                pin_to_str(pin_digits, pstr);
                kstore_create(pstr);
                g_screen = SCR_HOME; ui_init();
            } else {
                /* Mismatch — start over */
                go(SCR_PIN_SETUP_1);
            }
        }
        break;

    /* ── Home ──────────────────────────────────────────── */
    case SCR_HOME:
        if (key == KEY_B || key == KEY_LEFT) { kstore_lock(); g_exit = 1; return; }
        if (key == KEY_UP)   list_sel = (list_sel + 4) % 5;
        if (key == KEY_DOWN) list_sel = (list_sel + 1) % 5;
        if (key == KEY_A || key == KEY_RIGHT) {
            if (list_sel == 0) go(SCR_TOTP_LIST);
            if (list_sel == 1) go(SCR_FIDO2_LIST);
            if (list_sel == 2) go(SCR_PASS_LIST);
            if (list_sel == 3) go(SCR_SSH_VIEW);
            if (list_sel == 4) go(SCR_SETTINGS);
        }
        break;

    /* ── TOTP list ─────────────────────────────────────── */
    case SCR_TOTP_LIST:
        rebuild_totp_map();
        if (key == KEY_UP   && list_sel > 0)                  list_sel--;
        if (key == KEY_DOWN && list_sel < active_totp_cnt - 1) list_sel++;
        if (key == KEY_B || key == KEY_LEFT)   back();
        if ((key == KEY_A || key == KEY_RIGHT) && active_totp_cnt > 0) {
            totp_code   = 0;
            totp_remain = 0;
            go(SCR_TOTP_VIEW);
        }
        break;

    /* ── TOTP view ─────────────────────────────────────── */
    case SCR_TOTP_VIEW:
        if (key == KEY_B || key == KEY_LEFT) back();
        if ((key == KEY_A || key == KEY_RIGHT) && g_key_vault.ble_enabled) {
            int vi   = active_totp[list_sel];
            int digs = g_key_vault.totp[vi].digits > 0 ? g_key_vault.totp[vi].digits : 6;
            char cstr[10]; uint32_t c = totp_code; cstr[digs] = '\0';
            for (int i = digs-1; i >= 0; i--) { cstr[i] = '0' + c%10; c /= 10; }
            hid_type_string(cstr);
        }
        break;

    /* ── FIDO2 list ────────────────────────────────────── */
    case SCR_FIDO2_LIST:
        rebuild_fido_map();
        if (key == KEY_UP   && list_sel > 0)                  list_sel--;
        if (key == KEY_DOWN && list_sel < active_fido_cnt - 1) list_sel++;
        if (key == KEY_B || key == KEY_LEFT)  back();
        if ((key == KEY_A || key == KEY_RIGHT) && active_fido_cnt > 0) {
            hold_ms = 0; go(SCR_FIDO2_VIEW);
        }
        break;

    /* ── FIDO2 view ────────────────────────────────────── */
    case SCR_FIDO2_VIEW:
        if (key == KEY_B || key == KEY_LEFT) { hold_ms = 0; back(); }
        if (key == KEY_A) {
            hold_ms += 100; /* each key event counts ~100ms */
            if (hold_ms >= 2000) {
                int vi = active_fido[list_sel];
                g_key_vault.fido2[vi].sign_count++;
                g_dirty = 1; hold_ms = 0; back();
            }
        } else if (key != KEY_A) {
            hold_ms = 0;
        }
        break;

    /* ── Password list ─────────────────────────────────── */
    case SCR_PASS_LIST:
        rebuild_pass_map();
        if (key == KEY_UP   && list_sel > 0)                  list_sel--;
        if (key == KEY_DOWN && list_sel < active_pass_cnt - 1) list_sel++;
        if (key == KEY_B || key == KEY_LEFT)  back();
        if ((key == KEY_A || key == KEY_RIGHT) && active_pass_cnt > 0)
            go(SCR_PASS_VIEW);
        break;

    /* ── Password view ─────────────────────────────────── */
    case SCR_PASS_VIEW:
        if (key == KEY_B || key == KEY_LEFT)  back();
        if (key == KEY_A && g_key_vault.ble_enabled) {
            int vi = active_pass[list_sel];
            hid_type_string(g_key_vault.pass[vi].username);
        }
        if (key == KEY_UP && g_key_vault.ble_enabled) {
            int vi = active_pass[list_sel];
            hid_type_string(g_key_vault.pass[vi].password);
        }
        break;

    /* ── SSH view ──────────────────────────────────────── */
    case SCR_SSH_VIEW:
        if (key == KEY_B || key == KEY_LEFT) back();
        if (key == KEY_A && g_key_vault.ble_enabled) {
            char hexout[66]; int hi = 0;
            static const char hx[] = "0123456789abcdef";
            for (int i = 0; i < 32; i++) {
                hexout[hi++] = hx[g_key_vault.ssh_pub[i] >> 4];
                hexout[hi++] = hx[g_key_vault.ssh_pub[i] & 0xF];
            }
            hexout[hi] = '\0';
            hid_type_string(hexout);
        }
        break;

    /* ── Settings ──────────────────────────────────────── */
    case SCR_SETTINGS:
        if (key == KEY_UP)   list_sel = (list_sel + 2) % 3;
        if (key == KEY_DOWN) list_sel = (list_sel + 1) % 3;
        if (key == KEY_B || key == KEY_LEFT) back();
        if (key == KEY_A || key == KEY_RIGHT) {
            if (list_sel == 0) { g_key_vault.ble_enabled ^= 1; g_dirty = 1; }
            if (list_sel == 1) { hold_ms = 0; go(SCR_FACTORY_RESET); }
            if (list_sel == 2) go(SCR_ABOUT);
        }
        break;

    /* ── Factory reset ─────────────────────────────────── */
    case SCR_FACTORY_RESET:
        if (key == KEY_B || key == KEY_LEFT) { hold_ms = 0; back(); }
        if (key == KEY_A) {
            hold_ms += 100;
            if (hold_ms >= 3000) {
                kstore_wipe();
                hold_ms = 0;
                g_screen = SCR_PIN_SETUP_1; ui_init();
            }
        } else {
            hold_ms = 0;
        }
        break;

    /* ── About ─────────────────────────────────────────── */
    case SCR_ABOUT:
        if (key == KEY_B || key == KEY_LEFT) back();
        break;

    default: break;
    }
}

/* ── ui_tick (called every 500ms) ───────────────────────────────────── */
void ui_tick(uint64_t unix_sec) {
    if (pin_error_frames > 0) pin_error_frames--;

    if (g_screen == SCR_TOTP_VIEW && active_totp_cnt > 0) {
        int vi     = active_totp[list_sel];
        totp_slot_t *sl = &g_key_vault.totp[vi];
        int period = sl->period > 0 ? sl->period : 30;
        totp_remain = (int)(period - (int)(unix_sec % (uint64_t)period));
        if (sl->active && sl->secret_len > 0)
            totp_code = totp_generate(sl->secret, sl->secret_len, unix_sec,
                                      period, sl->digits > 0 ? sl->digits : 6);
    }
}
