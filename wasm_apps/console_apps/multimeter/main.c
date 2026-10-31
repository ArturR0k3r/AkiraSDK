/**
 * @file multimeter.c
 * @brief Multimeter front-end — live ADC voltage read-out for AkiraOS
 *
 * Reads an ADC channel in millivolts, shows a big voltage figure plus the raw
 * sample, tracks min/max/avg, and plots a rolling trace.
 *
 * Controls (AkiraConsole):
 *   LEFT/RIGHT = select ADC channel
 *   A          = HOLD (freeze the reading)
 *   B          = reset min/max/avg + graph
 *   SETTINGS   = exit to launcher
 *
 * @copyright Copyright (c) 2026 PenEngineering S.R.L
 * @license Apache-2.0
 */

#include "akira_api.h"

static int32_t SCR_W = 320;
static int32_t SCR_H = 240;

#define BTN_LEFT 6
#define BTN_RIGHT 7
#define BTN_A 15
#define BTN_B 16
#define BTN_SETTINGS 0

#define COL_BG 0x0000
#define COL_HEADER_BG 0x3800 /* dark amber bar          */
#define COL_TITLE 0xFFE0     /* yellow                  */
#define COL_LABEL 0x8410
#define COL_VALUE 0xFFFF
#define COL_VOLTS 0x07E0     /* green big number        */
#define COL_HOLD 0xF800      /* red HOLD badge          */
#define COL_GRAPH 0x07FF     /* cyan trace              */
#define COL_GRID 0x1082
#define COL_HINT 0x4208
#define COL_ERR 0xF800

#define N_CHANNELS 4 /* channels offered in the UI          */
#define GRAPH_N 240  /* rolling-sample ring capacity        */

static int ch = 0;
static int hold = 0;

static int32_t mv_min, mv_max;
static int64_t mv_sum;
static uint32_t mv_cnt;

static int16_t trace[GRAPH_N];
static int trace_head = 0; /* next write index                     */
static int trace_len = 0;

static void stats_reset(void)
{
    mv_min = 0x7fffffff;
    mv_max = -0x7fffffff;
    mv_sum = 0;
    mv_cnt = 0;
    trace_head = 0;
    trace_len = 0;
}

/* ── mV → "x.xxx V" (sign-aware, no libc/float printing) ────────────────── */
static void mv_to_volts(int32_t mv, char *out)
{
    int p = 0;
    int32_t v = mv;
    if (v < 0) { out[p++] = '-'; v = -v; }
    int32_t whole = v / 1000;
    int32_t frac = v % 1000; /* 3 decimal places */

    char t[12]; int n = 0;
    if (!whole) t[n++] = '0';
    while (whole) { t[n++] = (char)('0' + whole % 10); whole /= 10; }
    while (n) out[p++] = t[--n];

    out[p++] = '.';
    out[p++] = (char)('0' + (frac / 100) % 10);
    out[p++] = (char)('0' + (frac / 10) % 10);
    out[p++] = (char)('0' + frac % 10);
    out[p] = '\0';
}
static void i2s(int32_t v, char *b)
{
    int p = 0;
    if (v < 0) { b[p++] = '-'; v = -v; }
    char t[12]; int n = 0;
    if (!v) t[n++] = '0';
    while (v) { t[n++] = (char)('0' + v % 10); v /= 10; }
    while (n) b[p++] = t[--n];
    b[p] = '\0';
}

static void push_trace(int32_t mv)
{
    int16_t s = (mv > 32767) ? 32767 : (mv < -32768 ? -32768 : (int16_t)mv);
    trace[trace_head] = s;
    trace_head = (trace_head + 1) % GRAPH_N;
    if (trace_len < GRAPH_N) trace_len++;
}

static void draw_graph(int gx, int gy, int gw, int gh)
{
    display_rect(gx, gy, gw, gh, 0x0841);
    display_rect_outline(gx, gy, gw, gh, COL_GRID);
    if (trace_len < 2) return;

    /* Autoscale to the window's own min/max for a readable trace. */
    int32_t lo = 0x7fffffff, hi = -0x7fffffff;
    for (int i = 0; i < trace_len; i++)
    {
        int idx = (trace_head - trace_len + i + GRAPH_N) % GRAPH_N;
        int32_t v = trace[idx];
        if (v < lo) lo = v;
        if (v > hi) hi = v;
    }
    if (hi == lo) { hi = lo + 1; }
    int32_t span = hi - lo;

    int n = trace_len < gw ? trace_len : gw;
    int prev_x = 0, prev_y = 0;
    for (int i = 0; i < n; i++)
    {
        int idx = (trace_head - n + i + GRAPH_N) % GRAPH_N;
        int32_t v = trace[idx];
        int x = gx + (gw - n) + i;
        int y = gy + gh - 1 - (int)((int64_t)(v - lo) * (gh - 2) / span);
        if (i > 0) display_line(prev_x, prev_y, x, y, COL_GRAPH);
        prev_x = x;
        prev_y = y;
    }
}

static void draw(int32_t mv, int err)
{
    display_clear(COL_BG);

    display_rect(0, 0, SCR_W, 20, COL_HEADER_BG);
    display_text(4, 6, "MULTIMETER", COL_TITLE);

    char b[16];
    display_text(SCR_W - 70, 6, "CH", COL_LABEL);
    i2s(ch, b);
    display_text(SCR_W - 48, 6, b, COL_VALUE);

    if (hold)
        display_text(SCR_W / 2 - 16, 6, "HOLD", COL_HOLD);

    /* Big voltage figure */
    if (err)
    {
        display_text_huge(SCR_W / 2 - 60, 54, "n/c", COL_ERR);
        display_text(SCR_W / 2 - 54, 92, "channel error", COL_LABEL);
    }
    else
    {
        char vs[16];
        mv_to_volts(mv, vs);
        display_text_huge(20, 44, vs, COL_VOLTS);
        display_text(SCR_W - 40, 70, "V", COL_LABEL);

        i2s(mv, b);
        display_text(20, 86, "raw mV:", COL_LABEL);
        display_text(90, 86, b, COL_VALUE);
    }

    /* Stats row */
    int sy = 104;
    display_text(6, sy, "min", COL_LABEL);
    i2s(mv_cnt ? mv_min : 0, b); display_text(6, sy + 12, b, COL_VALUE);
    display_text(SCR_W / 3, sy, "avg", COL_LABEL);
    i2s(mv_cnt ? (int32_t)(mv_sum / (int64_t)mv_cnt) : 0, b);
    display_text(SCR_W / 3, sy + 12, b, COL_VALUE);
    display_text(2 * SCR_W / 3, sy, "max", COL_LABEL);
    i2s(mv_cnt ? mv_max : 0, b); display_text(2 * SCR_W / 3, sy + 12, b, COL_VALUE);

    /* Graph */
    draw_graph(6, 134, SCR_W - 12, SCR_H - 134 - 16);

    display_text(4, SCR_H - 12, "L/R:ch  A:hold  B:reset  OK:exit", COL_HINT);
    display_flush();
}

int main(void)
{
    printf("AkiraOS Multimeter");
    display_get_size(&SCR_W, &SCR_H);

    gpio_configure(BTN_LEFT, GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);
    gpio_configure(BTN_RIGHT, GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);
    gpio_configure(BTN_A, GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);
    gpio_configure(BTN_B, GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_SETTINGS, GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);

    stats_reset();

    int ps = 0, pl = 0, pr = 0, pa = 0, pb = 0;
    int32_t last_mv = 0;
    int last_err = 0;

    while (1)
    {
        int s = gpio_read(BTN_SETTINGS);
        int l = gpio_read(BTN_LEFT);
        int r = gpio_read(BTN_RIGHT);
        int a = gpio_read(BTN_A);
        int b = gpio_read(BTN_B);

        if (s && !ps) { app_switch("supervisor"); return 0; }
        if (l && !pl) { if (ch > 0) ch--; stats_reset(); }
        if (r && !pr) { if (ch < N_CHANNELS - 1) ch++; stats_reset(); }
        if (a && !pa) hold = !hold;
        if (b && !pb) stats_reset();
        ps = s; pl = l; pr = r; pa = a; pb = b;

        if (!hold)
        {
            int mv = adc_read_mv(ch);
            if (mv < 0)
            {
                last_err = 1;
            }
            else
            {
                last_err = 0;
                last_mv = mv;
                if (mv < mv_min) mv_min = mv;
                if (mv > mv_max) mv_max = mv;
                mv_sum += mv;
                mv_cnt++;
                push_trace(mv);
            }
        }

        draw(last_mv, last_err);
        delay(60000); /* ~16 S/s — readable, not busy */
    }
}
