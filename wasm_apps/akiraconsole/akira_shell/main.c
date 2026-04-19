/*
 * Copyright (c) 2026 PenEngineering S.R.L
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file akira_shell/main.c
 * @brief AkiraOS Shell v3 — Playdate-inspired launcher
 *
 * Design language: white background, solid black header/footer, full-width
 * inverted rows for selection, status dots, clean separators, slide-in menu.
 *
 * Screens: HOME · MENU · SD · SETTINGS · NETWORK · ABOUT
 *
 * Navigation:
 *   UP/DOWN — move cursor
 *   A       — launch / confirm / toggle
 *   B       — open context menu (HOME) / back
 *   Y/X     — quick-stop running app (HOME)
 */

#include "../include/akira_api.h"

/* ── GPIO button pins ─────────────────────────────────────────────────────── */
#define PIN_UP    4
#define PIN_DOWN  5
#define PIN_LEFT  6
#define PIN_RIGHT 7
#define PIN_A     15
#define PIN_B     16
#define PIN_X     17
#define PIN_Y     41

/* ── App states (mirrors APP_STATE_* from akira_api.h) ───────────────────── */
#define STATE_INSTALLED 1
#define STATE_RUNNING   2
#define STATE_STOPPED   3
#define STATE_ERROR     4
#define STATE_FAILED    5

/* ── Screen IDs ──────────────────────────────────────────────────────────── */
#define SCR_HOME     0
#define SCR_MENU     1   /* slide-in context menu (B button)  */
#define SCR_SD       2   /* SD card app browser               */
#define SCR_SETTINGS 3   /* shell preferences                 */
#define SCR_NETWORK  4   /* WiFi / IP status                  */
#define SCR_ABOUT    5   /* board / firmware info             */

/* ── Layout (320×240) ────────────────────────────────────────────────────── */
#define SCR_W      320
#define SCR_H      240
#define HDR_H       24   /* solid black header bar    */
#define FTR_H       20   /* solid black footer bar    */
#define FTR_Y      (SCR_H - FTR_H)      /* 220           */
#define CT_Y        HDR_H                /* 24            */
#define CT_H       (FTR_Y - CT_Y)       /* 196           */
#define ROW_H       32   /* pixels per list row       */
#define VIS_ROWS   (CT_H / ROW_H)       /* 6 visible     */
#define ROW_PAD    10   /* left text margin           */

/* ── HOME special rows (appended after app list) ─────────────────────────── */
#define SP_SD        0
#define SP_SETTINGS  1
#define SP_NETWORK   2
#define SP_ABOUT     3
#define SP_COUNT     4
#define HOME_TOTAL(n) ((n) + SP_COUNT)

/* ── Playdate-inspired color palette (white-dominant, black accents) ─────── */
#define C_WHITE  0xFFFFU   /* background, selected text               */
#define C_BLACK  0x0000U   /* header/footer, text, selected bg        */
#define C_LGRAY  0xC618U   /* separators, faint outlines              */
#define C_GRAY   0x8410U   /* dim text, hints, stopped dot            */
#define C_DGRAY  0x4208U   /* darker accent text                      */
#define C_ERR    0xF800U   /* error state (red)                       */
#define C_WARN   0xFD20U   /* warning / stopped (orange)              */

/* ── Data ────────────────────────────────────────────────────────────────── */
#define MAX_APPS   12
#define NAME_LEN   32
#define LIST_BUF   (MAX_APPS * 56)
#define MAX_SD     16
#define SD_BUF_SZ  (MAX_SD * 64)
#define IP_LEN     20

static char g_names[MAX_APPS][NAME_LEN];
static int  g_states[MAX_APPS];
static int  g_count;
static char g_appbuf[LIST_BUF];
static char g_self[NAME_LEN];

static char g_sdnames[MAX_SD][NAME_LEN];
static int  g_sd_inst[MAX_SD];
static int  g_sd_count;
static char g_sdbuf[SD_BUF_SZ];

static char g_ip[IP_LEN];
static int  g_net_ready;

/* ── Navigation ──────────────────────────────────────────────────────────── */
static int g_screen;
static int g_cursor;
static int g_scroll;
static int g_menu_sel;    /* context menu / sub-screen cursor */
static int g_sort_mode;   /* 0 = default, 1 = by status        */

/* ── Launch request ──────────────────────────────────────────────────────── */
static char g_launch[NAME_LEN];

/* ════════════════════════════════════════════════════════════════════════════
 *  Utility
 * ════════════════════════════════════════════════════════════════════════════ */

static int slen(const char *s) { int n=0; while(s[n])n++; return n; }

static int seq(const char *a, const char *b) {
    while(*a && *b && *a==*b){a++;b++;} return *a=='\0' && *b=='\0';
}
static int starts_with(const char *s, const char *p) {
    while(*p){if(*s++!=*p++)return 0;} return 1;
}
static void scopy(char *d, const char *s, int max) {
    int i=0; while(s[i]&&i<max-1){d[i]=s[i];i++;} d[i]='\0';
}
static void itoa5(int n, char *buf) {
    char t[8]; int i=0, j=0;
    if(n==0){buf[0]='0';buf[1]='\0';return;}
    if(n<0){ buf[j++]='-'; n=-n; }
    while(n>0&&i<7){t[i++]='0'+(n%10);n/=10;}
    while(i>0)buf[j++]=t[--i];
    buf[j]='\0';
}

/* ════════════════════════════════════════════════════════════════════════════
 *  Input
 * ════════════════════════════════════════════════════════════════════════════ */

static int g_prev[8];

static void btns_init(void) {
    int f = GPIO_INPUT | GPIO_PULL_DOWN;
    gpio_configure(PIN_UP,    f); gpio_configure(PIN_DOWN,  f);
    gpio_configure(PIN_LEFT,  f); gpio_configure(PIN_RIGHT, f);
    gpio_configure(PIN_A,     f); gpio_configure(PIN_B,     f);
    gpio_configure(PIN_X,     f); gpio_configure(PIN_Y,     f);
}

typedef struct { int up,dn,lt,rt,a,b,x,y; } btns_t;

static btns_t btns_poll(void) {
    int c[8] = {
        gpio_read(PIN_UP)==1,    gpio_read(PIN_DOWN)==1,
        gpio_read(PIN_LEFT)==1,  gpio_read(PIN_RIGHT)==1,
        gpio_read(PIN_A)==1,     gpio_read(PIN_B)==1,
        gpio_read(PIN_X)==1,     gpio_read(PIN_Y)==1,
    };
    btns_t e = {
        c[0]&~g_prev[0], c[1]&~g_prev[1],
        c[2]&~g_prev[2], c[3]&~g_prev[3],
        c[4]&~g_prev[4], c[5]&~g_prev[5],
        c[6]&~g_prev[6], c[7]&~g_prev[7],
    };
    for(int i=0;i<8;i++) g_prev[i]=c[i];
    return e;
}

/* ════════════════════════════════════════════════════════════════════════════
 *  App / SD data management
 * ════════════════════════════════════════════════════════════════════════════ */

static int parse_state(const char *s) {
    if(starts_with(s,"RUNNING")) return STATE_RUNNING;
    if(starts_with(s,"STOPPED")) return STATE_STOPPED;
    if(starts_with(s,"ERROR"))   return STATE_ERROR;
    if(starts_with(s,"FAILED"))  return STATE_FAILED;
    return STATE_INSTALLED;
}

static void refresh_apps(void) {
    int cnt = app_list((uint8_t*)g_appbuf, LIST_BUF);
    if(cnt<0){g_count=0;return;}
    if(cnt>MAX_APPS) cnt=MAX_APPS;
    g_count = 0;
    char *p = g_appbuf;
    for(int i=0;i<cnt;i++){
        char *col=p;
        while(*col && *col!=':' && *col!='\n') col++;
        if(!*col || *col=='\n') break;
        int nl=(int)(col-p); if(nl>=NAME_LEN) nl=NAME_LEN-1;
        for(int j=0;j<nl;j++) g_names[g_count][j]=p[j];
        g_names[g_count][nl]='\0';
        if(seq(g_names[g_count], g_self)){      /* skip self */
            p=col+1; while(*p&&*p!='\n')p++; if(*p)p++; continue;
        }
        char sb[16]; int si=0; char *ss=col+1;
        while(ss[si]&&ss[si]!='\n'&&si<15){sb[si]=ss[si];si++;} sb[si]='\0';
        g_states[g_count++] = parse_state(sb);
        p=ss+si; if(*p=='\n')p++;
    }
}

static int find_app(const char *name) {
    for(int i=0;i<g_count;i++) if(seq(g_names[i],name)) return i;
    return -1;
}

static void drain_lifecycle(void) {
    akira_lifecycle_event_t ev;
    while(msg_try_recv("akira.lifecycle",(uint8_t*)&ev,sizeof(ev))==(int)sizeof(ev)){
        int idx = find_app(ev.name);
        if(idx>=0) g_states[idx]=ev.state; else refresh_apps();
    }
}

static void refresh_sd(void) {
    g_sd_count=0;
    int ret = sd_scan_wasm(g_sdbuf, SD_BUF_SZ);
    if(ret<=0) return;
    char *p=g_sdbuf;
    while(*p && g_sd_count<MAX_SD){
        char *nl=p; while(*nl && *nl!='\n') nl++;
        int len=(int)(nl-p); if(len<=0){p=(*nl?nl+1:nl);continue;}
        int cut=len;
        if(len>5 && p[len-5]=='.') cut=len-5;
        else if(len>4 && p[len-4]=='.') cut=len-4;
        if(cut>=NAME_LEN) cut=NAME_LEN-1;
        for(int j=0;j<cut;j++) g_sdnames[g_sd_count][j]=p[j];
        g_sdnames[g_sd_count][cut]='\0';
        g_sd_inst[g_sd_count] = (find_app(g_sdnames[g_sd_count])>=0) ? 1 : 0;
        g_sd_count++;
        p=(*nl ? nl+1 : nl);
    }
}

/* Insertion sort: RUNNING (2) first, then STOPPED (3), rest after */
static void maybe_sort(void) {
    if(!g_sort_mode) return;
    for(int i=1;i<g_count;i++){
        int ks=g_states[i]; char kn[NAME_LEN]; scopy(kn,g_names[i],NAME_LEN);
        int j=i-1;
        while(j>=0 && g_states[j]>ks){
            g_states[j+1]=g_states[j];
            scopy(g_names[j+1],g_names[j],NAME_LEN);
            j--;
        }
        g_states[j+1]=ks; scopy(g_names[j+1],kn,NAME_LEN);
    }
}

/* ════════════════════════════════════════════════════════════════════════════
 *  Drawing primitives
 * ════════════════════════════════════════════════════════════════════════════ */

/*
 * Status dot: 4-pixel-radius circle drawn at (cx,cy).
 *   RUNNING   → filled black (or white on inverted row)
 *   STOPPED   → outlined gray
 *   ERROR/FAIL→ filled red
 *   default   → faint outlined circle
 */
static void draw_dot(int cx, int cy, int state, int inverted) {
    if(inverted){
        switch(state){
        case STATE_RUNNING:  display_circle_fill(cx,cy,4,C_WHITE); break;
        case STATE_ERROR:
        case STATE_FAILED:   display_circle_fill(cx,cy,4,C_ERR);   break;
        default:             display_circle(cx,cy,4,C_GRAY);        break;
        }
    } else {
        switch(state){
        case STATE_RUNNING:  display_circle_fill(cx,cy,4,C_BLACK);  break;
        case STATE_STOPPED:  display_circle(cx,cy,4,C_GRAY);        break;
        case STATE_ERROR:
        case STATE_FAILED:   display_circle_fill(cx,cy,4,C_ERR);    break;
        default:             display_circle(cx,cy,4,C_LGRAY);       break;
        }
    }
}

/* Solid black header: title left, battery % + bar right */
static void draw_header(const char *title) {
    display_rect(0, 0, SCR_W, HDR_H, C_BLACK);
    display_text_large(8, 4, title, C_WHITE);

    int batt = power_get_battery_level();
    if(batt<0) batt=0;
    char bs[8]; itoa5(batt, bs);
    int bl=slen(bs); bs[bl]='%'; bs[bl+1]='\0';
    display_text(SCR_W-44, 8, bs, (batt<20) ? C_ERR : C_GRAY);

    /* Battery outline (10×6 px) + fill */
    int bx = SCR_W-14;
    display_rect_outline(bx, 9, 10, 6, C_GRAY);
    int fill = batt*8/100; if(fill<1 && batt>0) fill=1;
    uint32_t bc = (batt<20) ? C_ERR : (batt<50) ? C_WARN : C_WHITE;
    display_rect(bx+1, 10, fill, 4, bc);
}

/* Solid black footer: centered hint text in gray */
static void draw_footer(const char *hints) {
    display_rect(0, FTR_Y, SCR_W, FTR_H, C_BLACK);
    int tx = (SCR_W - slen(hints)*7)/2;
    if(tx<8) tx=8;
    display_text(tx, FTR_Y+6, hints, C_GRAY);
}

/*
 * App row: large name left, status dot right.
 * Selected: full black fill, white text and dot.
 */
static void draw_app_row(int y, int sel, const char *name, int state) {
    if(sel){
        display_rect(0, y, SCR_W, ROW_H, C_BLACK);
        display_text_large(ROW_PAD, y+(ROW_H-14)/2, name, C_WHITE);
        draw_dot(SCR_W-16, y+ROW_H/2, state, 1);
    } else {
        display_rect(0, y, SCR_W, ROW_H, C_WHITE);
        display_text_large(ROW_PAD, y+(ROW_H-14)/2, name, C_BLACK);
        draw_dot(SCR_W-16, y+ROW_H/2, state, 0);
    }
    display_hline(0, y+ROW_H-1, SCR_W, C_LGRAY);
}

/*
 * Action row (system entries): name left, optional tag + chevron right.
 */
static void draw_action_row(int y, int sel, const char *lbl, const char *tag) {
    if(sel){
        display_rect(0, y, SCR_W, ROW_H, C_BLACK);
        display_text_large(ROW_PAD, y+(ROW_H-14)/2, lbl, C_WHITE);
        if(tag && tag[0]){
            int tx=SCR_W-slen(tag)*7-20;
            if(tx>ROW_PAD+100) display_text(tx, y+(ROW_H-8)/2, tag, C_GRAY);
        }
        display_text(SCR_W-12, y+(ROW_H-8)/2, ">", C_WHITE);
    } else {
        display_rect(0, y, SCR_W, ROW_H, C_WHITE);
        display_text_large(ROW_PAD, y+(ROW_H-14)/2, lbl, C_BLACK);
        if(tag && tag[0]){
            int tx=SCR_W-slen(tag)*7-20;
            if(tx>ROW_PAD+100) display_text(tx, y+(ROW_H-8)/2, tag, C_DGRAY);
        }
        display_text(SCR_W-12, y+(ROW_H-8)/2, ">", C_GRAY);
    }
    display_hline(0, y+ROW_H-1, SCR_W, C_LGRAY);
}

/* Thin single-pixel scrollbar on right edge */
static void draw_scrollbar(int total, int offset) {
    if(total <= VIS_ROWS) return;
    display_vline(SCR_W-1, CT_Y, CT_H, C_LGRAY);
    int th = (VIS_ROWS * CT_H) / total;
    if(th<10) th=10;
    int ty = CT_Y + (offset * CT_H) / total;
    display_vline(SCR_W-1, ty, th, C_BLACK);
}

/* ════════════════════════════════════════════════════════════════════════════
 *  HOME screen
 * ════════════════════════════════════════════════════════════════════════════ */

/* Draws home content to fb WITHOUT flushing (used by menu animation too). */
static void build_home(void) {
    int total = HOME_TOTAL(g_count);
    if(g_cursor<0) g_cursor=0;
    if(g_cursor>=total) g_cursor=total-1;
    if(g_cursor<g_scroll) g_scroll=g_cursor;
    if(g_cursor>=g_scroll+VIS_ROWS) g_scroll=g_cursor-VIS_ROWS+1;

    draw_header("AkiraOS");
    display_rect(0, CT_Y, SCR_W, CT_H, C_WHITE);

    for(int v=0; v<VIS_ROWS; v++){
        int idx = g_scroll + v;
        if(idx>=total) break;
        int py  = CT_Y + v*ROW_H;
        int sel = (idx == g_cursor);
        if(idx < g_count){
            draw_app_row(py, sel, g_names[idx], g_states[idx]);
        } else {
            int sp = idx - g_count;
            const char *lbl = (sp==SP_SD)       ? "Browse SD..."  :
                              (sp==SP_SETTINGS)  ? "Settings"      :
                              (sp==SP_NETWORK)   ? "Network"       : "About";
            draw_action_row(py, sel, lbl, "");
        }
    }
    draw_scrollbar(total, g_scroll);
    draw_footer("A:Launch  B:Menu  Y:Stop");
}

static void render_home(void) {
    build_home();
    display_flush();
}

/* ════════════════════════════════════════════════════════════════════════════
 *  MENU overlay — slides in from the right over 3 frames
 * ════════════════════════════════════════════════════════════════════════════ */

#define MI_LAUNCH  0
#define MI_STOP    1
#define MI_SD      2
#define MI_ABOUT   3
#define MI_COUNT   4
static const char *MI_LBLS[MI_COUNT] = {"Launch","Stop","Browse SD...","About"};

/* Draw menu card at x position ox. */
static void draw_menu_card(int ox) {
    int mw=180, mh=MI_COUNT*ROW_H+HDR_H, my=(SCR_H-mh)/2;
    /* White card + thick black border */
    display_rect(ox, my, mw, mh, C_WHITE);
    display_rect_outline(ox, my, mw, mh, C_BLACK);
    /* Black title strip */
    display_rect(ox, my, mw, HDR_H, C_BLACK);
    display_text_large(ox+8, my+4, "Menu", C_WHITE);
    display_hline(ox, my+HDR_H, mw, C_BLACK);

    for(int i=0;i<MI_COUNT;i++){
        int ry = my + HDR_H + i*ROW_H;
        int sel = (i == g_menu_sel);
        if(sel){
            display_rect(ox, ry, mw, ROW_H, C_BLACK);
            display_text_large(ox+8, ry+(ROW_H-14)/2, MI_LBLS[i], C_WHITE);
        } else {
            display_rect(ox, ry, mw, ROW_H, C_WHITE);
            display_text_large(ox+8, ry+(ROW_H-14)/2, MI_LBLS[i], C_BLACK);
        }
        display_hline(ox, ry+ROW_H-1, mw, C_LGRAY);
    }
}

/* Full render with 3-frame slide-in animation. */
static void render_menu(void) {
    int mw=180, ox = SCR_W - mw - 10;   /* final resting x */
    /* xs: card starts almost off-screen right, slides to ox */
    int xs[3] = { SCR_W - mw/4, ox+50, ox };
    for(int i=0;i<3;i++){
        int cx = xs[i] < ox ? ox : xs[i];
        build_home();           /* fresh home background each frame */
        draw_menu_card(cx);
        display_flush();
        if(i<2) delay(14000);   /* ~14 ms per animation frame */
    }
}

/* Cursor-change-only redraw: no slide animation. */
static void redraw_menu(void) {
    build_home();
    draw_menu_card(SCR_W - 180 - 10);
    display_flush();
}

/* ════════════════════════════════════════════════════════════════════════════
 *  SD BROWSER
 * ════════════════════════════════════════════════════════════════════════════ */

static void render_sd(void) {
    if(g_cursor<0) g_cursor=0;
    if(g_sd_count>0 && g_cursor>=g_sd_count) g_cursor=g_sd_count-1;
    if(g_cursor<g_scroll) g_scroll=g_cursor;
    if(g_cursor>=g_scroll+VIS_ROWS) g_scroll=g_cursor-VIS_ROWS+1;

    draw_header("SD Card");
    display_rect(0, CT_Y, SCR_W, CT_H, C_WHITE);

    if(g_sd_count==0){
        display_text(ROW_PAD, CT_Y+24, "No .wasm / .aot files found.", C_GRAY);
        display_text(ROW_PAD, CT_Y+44, "Place apps in SD:/apps/",      C_GRAY);
    } else {
        for(int v=0;v<VIS_ROWS;v++){
            int idx = g_scroll+v;
            if(idx>=g_sd_count) break;
            int py  = CT_Y + v*ROW_H;
            int sel = (idx == g_cursor);
            const char *tag = g_sd_inst[idx] ? "installed" : "new";
            if(sel){
                display_rect(0, py, SCR_W, ROW_H, C_BLACK);
                display_text_large(ROW_PAD, py+(ROW_H-14)/2, g_sdnames[idx], C_WHITE);
                display_text(SCR_W-slen(tag)*7-10, py+(ROW_H-8)/2, tag, C_GRAY);
            } else {
                display_rect(0, py, SCR_W, ROW_H, C_WHITE);
                display_text_large(ROW_PAD, py+(ROW_H-14)/2, g_sdnames[idx], C_BLACK);
                uint32_t tc = g_sd_inst[idx] ? C_DGRAY : C_LGRAY;
                display_text(SCR_W-slen(tag)*7-10, py+(ROW_H-8)/2, tag, tc);
            }
            display_hline(0, py+ROW_H-1, SCR_W, C_LGRAY);
        }
    }
    draw_scrollbar(g_sd_count, g_scroll);
    draw_footer("A:Install/Launch  B:Back");
    display_flush();
}

/* ════════════════════════════════════════════════════════════════════════════
 *  SETTINGS screen
 *  Persistent key "shell/sort": "0" = default order, "1" = by status.
 * ════════════════════════════════════════════════════════════════════════════ */

#define SI_SORT  0
#define SI_BACK  1
#define SI_COUNT 2

static void render_settings(void) {
    static const char *SORT_LBLS[2] = {"Default", "By Status"};
    draw_header("Settings");
    display_rect(0, CT_Y, SCR_W, CT_H, C_WHITE);

    /* Section label */
    display_text(ROW_PAD, CT_Y+6, "Shell Preferences", C_GRAY);
    display_hline(0, CT_Y+18, SCR_W, C_LGRAY);

    int y = CT_Y + 20;
    for(int i=0;i<SI_COUNT;i++){
        int sel = (i == g_menu_sel);
        if(i == SI_SORT){
            const char *val = SORT_LBLS[g_sort_mode];
            if(sel){
                display_rect(0, y, SCR_W, ROW_H, C_BLACK);
                display_text_large(ROW_PAD, y+(ROW_H-14)/2, "Sort Apps", C_WHITE);
                int vx = SCR_W - slen(val)*7 - 20;
                display_text(vx, y+(ROW_H-8)/2, val, C_GRAY);
                display_text(SCR_W-12, y+(ROW_H-8)/2, ">", C_WHITE);
            } else {
                display_rect(0, y, SCR_W, ROW_H, C_WHITE);
                display_text_large(ROW_PAD, y+(ROW_H-14)/2, "Sort Apps", C_BLACK);
                int vx = SCR_W - slen(val)*7 - 20;
                display_text(vx, y+(ROW_H-8)/2, val, C_DGRAY);
                display_text(SCR_W-12, y+(ROW_H-8)/2, ">", C_GRAY);
            }
        } else {
            draw_action_row(y, sel, "Back", "");
        }
        display_hline(0, y+ROW_H-1, SCR_W, C_LGRAY);
        y += ROW_H;
    }
    draw_footer("A/LR:Change  B:Back");
    display_flush();
}

/* ════════════════════════════════════════════════════════════════════════════
 *  NETWORK screen
 * ════════════════════════════════════════════════════════════════════════════ */

static void render_network(void) {
    draw_header("Network");
    display_rect(0, CT_Y, SCR_W, CT_H, C_WHITE);

    if(!g_net_ready){
        display_text(ROW_PAD, CT_Y+20, "Querying...", C_GRAY);
        display_flush();
        if(net_get_ip(g_ip, IP_LEN)<0 || g_ip[0]=='\0')
            scopy(g_ip, "No IP", IP_LEN);
        g_net_ready = 1;
        display_rect(0, CT_Y, SCR_W, CT_H, C_WHITE);
    }

    int has_ip = !(g_ip[0]=='N' && g_ip[1]=='o');
    int y = CT_Y + 16;

    /* Status row: dot + text */
    display_circle_fill(ROW_PAD+6, y+8, 6, has_ip ? C_BLACK : C_ERR);
    display_text_large(ROW_PAD+20, y,
                       has_ip ? "Connected" : "Disconnected",
                       has_ip ? C_BLACK : C_ERR);
    y += 26;
    display_hline(0, y, SCR_W, C_LGRAY); y += 12;

    display_text(ROW_PAD, y, "IP Address", C_GRAY);   y += 16;
    display_text_large(ROW_PAD, y, g_ip, C_BLACK);    y += 26;
    display_hline(0, y, SCR_W, C_LGRAY);              y += 10;
    display_text(ROW_PAD, y, "Capability: network.*", C_GRAY);

    draw_footer("A:Refresh  B:Back");
    display_flush();
}

/* ════════════════════════════════════════════════════════════════════════════
 *  ABOUT screen
 * ════════════════════════════════════════════════════════════════════════════ */

static void render_about(void) {
    draw_header("About");
    display_rect(0, CT_Y, SCR_W, CT_H, C_WHITE);

    int y = CT_Y + 14;
    /* Centered title */
    int tx = (SCR_W - slen("AkiraOS")*11) / 2;
    display_text_large(tx, y, "AkiraOS", C_BLACK); y += 26;
    display_hline(24, y, SCR_W-48, C_LGRAY);       y += 12;

    /* Two-column info grid */
    display_text(ROW_PAD, y, "Firmware", C_GRAY);
    display_text(110,      y, "v1.4.8",  C_BLACK); y += 18;
    display_text(ROW_PAD, y, "Shell",    C_GRAY);
    display_text(110,      y, "v3.0.0",  C_BLACK); y += 18;
    display_text(ROW_PAD, y, "Board",    C_GRAY);
    display_text(110,      y, "ESP32-S3",C_BLACK); y += 18;
    display_hline(24, y, SCR_W-48, C_LGRAY);       y += 12;

    int batt = power_get_battery_level(); if(batt<0)batt=0;
    char bs[16];
    bs[0]='B';bs[1]='a';bs[2]='t';bs[3]='t';bs[4]=':';bs[5]=' ';
    itoa5(batt, bs+6); int bl=slen(bs); bs[bl]='%'; bs[bl+1]='\0';
    display_text(ROW_PAD, y, bs, (batt<20)?C_ERR:C_BLACK); y += 18;

    char ac[16];
    ac[0]='A';ac[1]='p';ac[2]='p';ac[3]='s';ac[4]=':';ac[5]=' ';
    itoa5(g_count, ac+6);
    display_text(ROW_PAD, y, ac, C_GRAY);

    /* Decorative corner dots (Playdate-style) */
    display_circle_fill(SCR_W-14, FTR_Y-14, 5, C_LGRAY);
    display_circle_fill(SCR_W-30, FTR_Y-14, 3, C_LGRAY);
    display_circle_fill(SCR_W-42, FTR_Y-14, 2, C_LGRAY);

    draw_footer("B:Back");
    display_flush();
}

/* ════════════════════════════════════════════════════════════════════════════
 *  LAUNCHING splash
 * ════════════════════════════════════════════════════════════════════════════ */

static void show_launching(const char *name) {
    display_clear(C_BLACK);
    int tx = (SCR_W - slen(name)*11) / 2; if(tx<8) tx=8;
    display_text_large(tx, (SCR_H-14)/2, name, C_WHITE);
    display_text((SCR_W-slen("Launching...")*7)/2,
                 SCR_H/2 + 22, "Launching...", C_GRAY);
    display_flush();
    delay(280000);
}

/* ════════════════════════════════════════════════════════════════════════════
 *  Main
 * ════════════════════════════════════════════════════════════════════════════ */

int main(void)
{
    printf("[akira_shell] v3.0 starting\n");

    btns_init();
    app_get_self_name((uint8_t*)g_self, NAME_LEN);
    msg_subscribe("akira.lifecycle");

    /* Load persistent preferences */
    char stbuf[4];
    g_sort_mode = 0;
    if(settings_get("shell/sort", stbuf, sizeof(stbuf))==0 && stbuf[0]=='1')
        g_sort_mode = 1;

    refresh_apps();
    maybe_sort();

    g_screen    = SCR_HOME;
    g_cursor    = 0;
    g_scroll    = 0;
    g_menu_sel  = 0;
    g_net_ready = 0;
    g_launch[0] = '\0';

    render_home();

    while(1){
        drain_lifecycle();
        btns_t e = btns_poll();

        int cur_is_app  = (g_screen==SCR_HOME && g_cursor<g_count);
        const char *cur_name    = cur_is_app ? g_names[g_cursor] : "";
        int cur_running = cur_is_app && (g_states[g_cursor]==STATE_RUNNING);

        /* ════════════════ HOME ════════════════ */
        if(g_screen == SCR_HOME){
            int total = HOME_TOTAL(g_count);
            int dirty = 0;

            if(e.up && g_cursor>0)       { g_cursor--; dirty=1; }
            if(e.dn && g_cursor<total-1) { g_cursor++; dirty=1; }

            if(e.a){
                if(g_cursor < g_count){
                    scopy(g_launch, g_names[g_cursor], NAME_LEN);
                } else {
                    int sp = g_cursor - g_count;
                    if(sp==SP_SD){
                        refresh_sd(); g_screen=SCR_SD; g_cursor=0; g_scroll=0;
                        render_sd(); continue;
                    } else if(sp==SP_SETTINGS){
                        g_screen=SCR_SETTINGS; g_menu_sel=0;
                        render_settings(); continue;
                    } else if(sp==SP_NETWORK){
                        g_net_ready=0; g_screen=SCR_NETWORK;
                        render_network(); continue;
                    } else {
                        g_screen=SCR_ABOUT; render_about(); continue;
                    }
                }
            }

            if(e.b){
                g_screen   = SCR_MENU;
                g_menu_sel = cur_running ? MI_STOP : MI_LAUNCH;
                render_menu();
                delay(22000);
                continue;
            }

            if((e.y||e.x) && cur_is_app && cur_running){
                app_stop(cur_name);
                g_states[g_cursor] = STATE_STOPPED;
                dirty = 1;
            }

            if(dirty) render_home();
        }

        /* ════════════════ MENU ════════════════ */
        else if(g_screen == SCR_MENU){
            if(e.up && g_menu_sel>0)           { g_menu_sel--; redraw_menu(); }
            if(e.dn && g_menu_sel<MI_COUNT-1)  { g_menu_sel++; redraw_menu(); }

            if(e.a){
                g_screen = SCR_HOME;
                switch(g_menu_sel){
                case MI_LAUNCH:
                    if(cur_is_app) scopy(g_launch, cur_name, NAME_LEN);
                    else render_home();
                    break;
                case MI_STOP:
                    if(cur_is_app && cur_running){
                        app_stop(cur_name);
                        g_states[g_cursor] = STATE_STOPPED;
                    }
                    render_home(); break;
                case MI_SD:
                    refresh_sd(); g_screen=SCR_SD; g_cursor=0; g_scroll=0;
                    render_sd(); break;
                case MI_ABOUT:
                    g_screen=SCR_ABOUT; render_about(); break;
                }
            }
            if(e.b){ g_screen=SCR_HOME; render_home(); }
        }

        /* ════════════════ SD ════════════════ */
        else if(g_screen == SCR_SD){
            int dirty = 0;
            if(e.up && g_cursor>0)            { g_cursor--; dirty=1; }
            if(e.dn && g_cursor<g_sd_count-1) { g_cursor++; dirty=1; }

            if(e.a && g_sd_count>0){
                const char *sdn = g_sdnames[g_cursor];
                if(g_sd_inst[g_cursor]){
                    scopy(g_launch, sdn, NAME_LEN);
                } else {
                    /* Install feedback */
                    draw_header("SD Card");
                    display_rect(0, CT_Y, SCR_W, CT_H, C_WHITE);
                    display_circle_fill(ROW_PAD+6, CT_Y+30, 6, C_BLACK);
                    display_text_large(ROW_PAD+20, CT_Y+22, "Installing...", C_BLACK);
                    display_text(ROW_PAD, CT_Y+50, sdn, C_GRAY);
                    display_flush();

                    int ret = app_install_from_sd(sdn);
                    display_rect(0, CT_Y, SCR_W, CT_H, C_WHITE);
                    if(ret>=0){
                        g_sd_inst[g_cursor]=1; refresh_apps();
                        display_circle_fill(ROW_PAD+6, CT_Y+30, 6, C_BLACK);
                        display_text_large(ROW_PAD+20, CT_Y+22, "Installed!", C_BLACK);
                        display_text(ROW_PAD, CT_Y+50, sdn, C_GRAY);
                        display_flush(); delay(700000);
                    } else {
                        display_circle_fill(ROW_PAD+6, CT_Y+30, 6, C_ERR);
                        display_text_large(ROW_PAD+20, CT_Y+22, "Failed!", C_ERR);
                        display_flush(); delay(700000);
                    }
                    dirty=1;
                }
            }
            if(e.b){ g_screen=SCR_HOME; g_cursor=0; g_scroll=0; render_home(); }
            else if(dirty) render_sd();
        }

        /* ════════════════ SETTINGS ════════════════ */
        else if(g_screen == SCR_SETTINGS){
            int dirty=0;
            if(e.up && g_menu_sel>0)          { g_menu_sel--; dirty=1; }
            if(e.dn && g_menu_sel<SI_COUNT-1) { g_menu_sel++; dirty=1; }

            if(e.a || e.rt || e.lt){
                if(g_menu_sel == SI_SORT){
                    g_sort_mode ^= 1;
                    char sv[2] = {'0'+(char)g_sort_mode, '\0'};
                    settings_set("shell/sort", sv);
                    refresh_apps(); maybe_sort();
                    dirty=1;
                } else {                         /* Back row */
                    g_screen=SCR_HOME; render_home(); continue;
                }
            }
            if(e.b){ g_screen=SCR_HOME; render_home(); }
            else if(dirty) render_settings();
        }

        /* ════════════════ NETWORK ════════════════ */
        else if(g_screen == SCR_NETWORK){
            if(e.a){ g_net_ready=0; render_network(); }
            if(e.b){ g_screen=SCR_HOME; render_home(); }
        }

        /* ════════════════ ABOUT ════════════════ */
        else if(g_screen == SCR_ABOUT){
            if(e.b || e.a){ g_screen=SCR_HOME; render_home(); }
        }

        /* ════════════════ LAUNCH ════════════════ */
        if(g_launch[0] != '\0'){
            char ln[NAME_LEN]; scopy(ln, g_launch, NAME_LEN);
            g_launch[0]='\0';
            show_launching(ln);
            if(app_switch(ln)==0) return 0;   /* clean handoff */
            /* Failed — stay in shell */
            draw_footer("Launch failed!");
            display_flush();
            delay(800000);
            render_home();
        }

        delay(33333);   /* ~30 Hz */
    }

    return 0;
}
