/*
 * main.c — AkiraFocus: Pomodoro focus timer for AkiraConsole
 *
 * Controls:
 *   A         — start / pause timer
 *   B         — reset current phase
 *   UP/DOWN   — adjust custom time (±1 min) when stopped
 *   RIGHT     — skip to next phase
 *   SETTINGS  — exit app (long-press = force exit)
 *
 * Phases:  WORK (25 min)  →  SHORT REST (5 min)  →  repeat 4×  →  LONG REST (15 min)
 * SPDX-License-Identifier: Apache-2.0
 */

#include "../include/akira_api.h"

/* ── Display & colours ──────────────────────────────────────────────────── */
static int32_t SCR_W=320, SCR_H=240;
static int g_mono=0;

/* All colours are Sharp-safe (inversion-aware) */
#define C_BG       0x0000u   /* black → white on Sharp */
#define C_FG       0xFFFFu   /* white → black on Sharp */
#define C_DIM      0xFFFFu
#define C_WORK     0xFFFFu   /* work phase indicator */
#define C_REST     0xFFFFu
#define C_WARN     0xFFFFu

/* ── Timer phases ───────────────────────────────────────────────────────── */
#define PHASE_WORK       0
#define PHASE_SHORT_REST 1
#define PHASE_LONG_REST  2

static const int PHASE_DEFAULT_S[3] = { 25*60, 5*60, 15*60 };
static const char *PHASE_NAMES[3]   = { "FOCUS", "BREAK", "LONG BREAK" };

/* ── App state ──────────────────────────────────────────────────────────── */
static int g_timer = -1;         /* AkiraOS timer handle */
static int g_last_ms = 0;        /* ms reading when phase started */
static int g_elapsed_s = 0;      /* elapsed seconds this phase */
static int g_phase = PHASE_WORK;
static int g_phase_secs = 25*60;
static int g_running = 0;
static int g_pomodoros = 0;      /* completed work sessions */
static int g_cycle = 0;          /* 0-3 within Pomodoro cycle */
static int g_flash = 0;          /* frame counter for end flash */
static int g_done = 0;           /* phase just completed flag */

/* ── Button debounce ────────────────────────────────────────────────────── */
#define PIN_UP    4
#define PIN_DOWN  5
#define PIN_LEFT  7
#define PIN_RIGHT 6
#define PIN_A     15
#define PIN_B     16
#define PIN_SET   0   /* active-low */

#define DB 4
typedef struct { int cnt, st, prev, hold; } Btn;
static Btn bup,bdn,blt,brt,ba,bb,bs;

static void btn_poll(Btn *b, int raw) {
    b->prev=b->st;
    if(raw) b->cnt++; else b->cnt=0;
    b->st=(b->cnt>=DB)?1:0;
    if(b->st) b->hold++; else b->hold=0;
}
static void btns_poll(void) {
    btn_poll(&bup, gpio_read(PIN_UP));
    btn_poll(&bdn, gpio_read(PIN_DOWN));
    btn_poll(&blt, gpio_read(PIN_LEFT));
    btn_poll(&brt, gpio_read(PIN_RIGHT));
    btn_poll(&ba,  gpio_read(PIN_A));
    btn_poll(&bb,  gpio_read(PIN_B));
    btn_poll(&bs,  !gpio_read(PIN_SET));
}
#define ROSE(b)  ((b).st&&!(b).prev)
#define LONG(b)  ((b).hold==75)  /* 75*20ms = 1.5s */

/* ── Number rendering ───────────────────────────────────────────────────── */
static void render_digit_large(int x, int y, char c, uint32_t fg, uint32_t bg) {
    /* 28×48 digit using 2×3 block of text_large glyphs */
    /* Each display_text_large glyph is ~8×13; we scale with big rects */
    char s[2]={c,'\0'};
    /* Draw a 28×48 block for the digit */
    display_rect(x, y, 28, 48, bg);
    /* 4 rows of text_large to fill the block */
    display_text_large(x+2, y+4,  s, fg);
    display_text_large(x+2, y+18, s, fg);
    display_text_large(x+2, y+32, s, fg);
}

/* Draw MM:SS in large digits */
static void draw_time_large(int x, int y, int total_s, uint32_t fg, uint32_t bg) {
    int mm = total_s / 60;
    int ss = total_s % 60;
    char buf[8];
    buf[0]='0'+mm/10; buf[1]='0'+mm%10;
    buf[2]=':';
    buf[3]='0'+ss/10; buf[4]='0'+ss%10;
    buf[5]='\0';

    int cx = x;
    for(int i=0;i<5;i++) {
        if(buf[i]==':') {
            /* Colon: two dots */
            display_rect(cx+6, y+14, 6, 6, fg);
            display_rect(cx+6, y+28, 6, 6, fg);
            cx += 18;
        } else {
            render_digit_large(cx, y, buf[i], fg, bg);
            cx += 32;
        }
    }
}

/* ── Progress arc / bar ─────────────────────────────────────────────────── */
static void draw_progress(int elapsed, int total) {
    /* Horizontal progress bar */
    int bx=20, by=SCR_H-40, bw=SCR_W-40, bh=10;
    display_rect(bx-1, by-1, bw+2, bh+2, C_FG);  /* border */
    display_rect(bx, by, bw, bh, C_BG);           /* inner clear */
    int fill = (total>0) ? elapsed*bw/total : 0;
    display_rect(bx, by, fill, bh, C_FG);
}

/* ── Phase indicator dots ───────────────────────────────────────────────── */
static void draw_dots(void) {
    /* 4 dots = 4 Pomodoros in a cycle; filled = completed */
    int dx = SCR_W/2 - 36, dy = SCR_H-22;
    for(int i=0;i<4;i++){
        int filled=(i < (g_cycle % 4)) || (g_phase==PHASE_WORK && i==(g_cycle%4) && g_running);
        display_rect(dx+i*18, dy, 12, 8, C_BG);
        if(filled) display_rect(dx+i*18+1, dy+1, 10, 6, C_FG);
        else        display_rect(dx+i*18+1, dy+1, 10, 6, C_BG);
        display_rect(dx+i*18, dy, 12, 1, C_FG);
        display_rect(dx+i*18, dy+7, 12, 1, C_FG);
        display_rect(dx+i*18, dy, 1, 8, C_FG);
        display_rect(dx+i*18+11, dy, 1, 8, C_FG);
    }
}

/* ── Main draw ──────────────────────────────────────────────────────────── */
static void draw(void) {
    int remaining = g_phase_secs - g_elapsed_s;
    if(remaining < 0) remaining = 0;

    /* Flash display when phase done */
    int flash_invert = g_done && (g_flash & 4);
    uint32_t bg = flash_invert ? C_FG : C_BG;
    uint32_t fg = flash_invert ? C_BG : C_FG;

    display_clear(bg);

    /* ── Header: phase name + pomodoro count ── */
    const char *phase_name = PHASE_NAMES[g_phase];
    display_text_large(20, 8, phase_name, fg);

    /* Pomodoro tally: "🍅 x3" style */
    char tally[12]; tally[0]='#'; tally[1]='0'+g_pomodoros/10; tally[2]='0'+g_pomodoros%10;
    tally[3]='\0';
    display_text_large(SCR_W-50, 8, tally, fg);

    /* Separator */
    display_hline(0, 28, SCR_W, fg);

    /* ── Large countdown ── */
    int tx = SCR_W/2 - 82;   /* centre: 5 digits = 2×32 + 18 + 2×32 = 162px, so -81 */
    int ty = 50;
    draw_time_large(tx, ty, remaining, fg, bg);

    /* Running indicator */
    if(g_running && !g_done) {
        display_text(SCR_W/2-48, ty+58, "Running...", fg);
    } else if(!g_running && !g_done) {
        display_text(SCR_W/2-24, ty+58, g_elapsed_s>0 ? "Paused" : "Ready", fg);
    } else if(g_done) {
        display_text(SCR_W/2-28, ty+58, "DONE! Press A", fg);
    }

    /* ── Progress bar ── */
    draw_progress(g_elapsed_s, g_phase_secs);

    /* ── Phase dots ── */
    draw_dots();

    /* ── Help strip ── */
    display_hline(0, SCR_H-52, SCR_W, fg);
    if(!g_running && g_elapsed_s==0) {
        display_text(6, SCR_H-48, "[A] Start  [UP/DN] +/-1min", fg);
    } else if(g_running) {
        display_text(6, SCR_H-48, "[A] Pause  [B] Reset  [>] Skip", fg);
    } else {
        display_text(6, SCR_H-48, "[A] Resume [B] Reset  [>] Skip", fg);
    }

    display_flush();
}

/* ── Phase transition ───────────────────────────────────────────────────── */
static void next_phase(void) {
    if(g_phase == PHASE_WORK) {
        g_pomodoros++;
        g_cycle++;
        /* Every 4 pomodoros → long break */
        if((g_cycle % 4) == 0) {
            g_phase = PHASE_LONG_REST;
            g_phase_secs = PHASE_DEFAULT_S[PHASE_LONG_REST];
        } else {
            g_phase = PHASE_SHORT_REST;
            g_phase_secs = PHASE_DEFAULT_S[PHASE_SHORT_REST];
        }
    } else {
        g_phase = PHASE_WORK;
        g_phase_secs = PHASE_DEFAULT_S[PHASE_WORK];
    }
    g_elapsed_s = 0;
    g_running = 0;
    g_done = 0;
    g_last_ms = (g_timer>=0) ? timer_elapsed(g_timer) : 0;
}

/* ── Main ───────────────────────────────────────────────────────────────── */
int main(void)
{
    display_get_size(&SCR_W, &SCR_H);
    g_mono = (SCR_W >= 400);

    gpio_configure(PIN_UP,    GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(PIN_DOWN,  GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(PIN_LEFT,  GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(PIN_RIGHT, GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(PIN_A,     GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(PIN_B,     GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(PIN_SET,   GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);

    g_timer = timer_create();
    if(g_timer >= 0) timer_start(g_timer);

    g_phase = PHASE_WORK;
    g_phase_secs = PHASE_DEFAULT_S[PHASE_WORK];
    g_last_ms = (g_timer>=0) ? timer_elapsed(g_timer) : 0;

    /* Boot splash */
    display_clear(C_BG);
    display_text_large(SCR_W/2-50, SCR_H/2-10, "AkiraFocus", C_FG);
    display_text(SCR_W/2-44, SCR_H/2+14, "Pomodoro Timer", C_FG);
    display_flush();
    delay(800000);

    int last_draw_ms=0, dirty=1;

    while(1) {
        int now_ms = (g_timer>=0) ? timer_elapsed(g_timer) : 0;

        btns_poll();

        /* ── Update elapsed time ── */
        if(g_running && !g_done) {
            int delta_ms = now_ms - g_last_ms;
            g_elapsed_s = delta_ms / 1000;
            if(g_elapsed_s >= g_phase_secs) {
                g_elapsed_s = g_phase_secs;
                g_running = 0;
                g_done = 1;
                dirty = 1;
            }
        }

        /* ── Flash animation when done ── */
        if(g_done) {
            g_flash++;
            if(g_flash & 1) dirty = 1;
        }

        /* ── Button handling ── */
        if(ROSE(ba)) {
            if(g_done) {
                /* Confirm done → next phase */
                next_phase();
                dirty = 1;
            } else if(g_running) {
                /* Pause — save elapsed */
                int delta_ms = now_ms - g_last_ms;
                g_elapsed_s = delta_ms / 1000;
                if(g_elapsed_s > g_phase_secs) g_elapsed_s = g_phase_secs;
                g_running = 0;
                dirty = 1;
            } else {
                /* Start / resume */
                g_last_ms = now_ms - g_elapsed_s * 1000;
                g_running = 1;
                dirty = 1;
            }
        }

        if(ROSE(bb)) {
            /* Reset current phase */
            g_elapsed_s = 0;
            g_running = 0;
            g_done = 0;
            g_flash = 0;
            g_last_ms = now_ms;
            dirty = 1;
        }

        if(ROSE(brt)) {
            /* Skip to next phase */
            next_phase();
            dirty = 1;
        }

        /* Adjust timer duration when stopped */
        if(!g_running && !g_done) {
            if(ROSE(bup)) {
                g_phase_secs += 60;
                if(g_phase_secs > 60*60) g_phase_secs = 60*60;
                g_elapsed_s = 0;
                dirty = 1;
            }
            if(ROSE(bdn)) {
                g_phase_secs -= 60;
                if(g_phase_secs < 60) g_phase_secs = 60;
                g_elapsed_s = 0;
                dirty = 1;
            }
        }

        /* SETTINGS = exit */
        if(ROSE(bs)) break;
        if(LONG(bs)) break;

        /* Draw at ~20fps when dirty */
        if(dirty && (now_ms - last_draw_ms >= 50)) {
            draw();
            last_draw_ms = now_ms;
            dirty = 0;
        }

        delay(20000);  /* 20ms = 50Hz */
    }

    /* Exit splash */
    display_clear(C_BG);
    display_text_large(SCR_W/2-40, SCR_H/2-8, "Bye!", C_FG);
    display_flush();
    delay(400000);
    return 0;
}
