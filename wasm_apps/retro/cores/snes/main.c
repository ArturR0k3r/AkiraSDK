/**
 * @file main.c
 * @brief AkiraOS SNES emulator app entry point
 *
 * ROM is embedded by rom_to_wasm.py as rom_data/rom_size.
 *
 * Button mapping (input_get_buttons()):
 *   D-pad -> D-pad, A/B/X/Y -> SNES A/B/X/Y, Home -> Start
 *   (Select/L/R have no physical button yet.)
 *
 * Output: the frame is pushed with display_raw_write() only. display_flush()
 * is deliberately not called per frame: it blocks on the OS compositor and
 * copies the 192 KB OS framebuffer, ~38 ms on this board, and the raw write
 * already reaches the panel.
 *
 * @license Apache-2.0
 */
#include "akira_api.h"
#include "snes.h"

#define BTN_HOME (1u << 1)   /* zephyr,code=1 — no AKIRA_BTN_* for Home */

/* Emulate FRAMESKIP frames without drawing for every drawn frame. */
#ifndef SNES_FRAMESKIP
#define SNES_FRAMESKIP 2
#endif

/* Print "snes fps*10=.. emu_ms*10=.." every REPORT_FRAMES emulated frames. */
#define REPORT_FRAMES 120

/* Deterministic benchmark: scripted ALttP play (name entry, cutscene, walk out of the
 * house onto the scrolling overworld). Frames before BENCH_START are fast-forwarded
 * without drawing; the average fps over [BENCH_START, BENCH_END) is printed once. */
#ifndef SNES_AUTOPLAY
#define SNES_AUTOPLAY 0
#endif
#if SNES_AUTOPLAY
#define BENCH_START 3500
#define BENCH_END   4800
static const uint16_t bs_f[] = { 0, 900, 915, 1300, 1308, 1360, 1368, 1450, 1465, 1600, 1608, 1660, 1668, 1720, 1728, 1780, 1788, 1840, 1848, 1900, 1908, 1960, 1968, 2020, 2028, 2080, 2088, 2140, 2148, 2200, 2208, 2260, 2268, 2320, 2328, 2380, 2388, 2440, 2448, 2500, 2508, 2560, 2568, 2620, 2628, 2680, 2688, 2740, 2748, 2800, 2808, 2860, 2868, 2920, 2928, 2980, 2988, 3040, 3048, 3100, 3108, 3160, 3168, 3220, 3228, 3280, 3288, 3340, 3348, 3400, 3408, 3460, 3468, 3500, 3540, 3640, 3650, 3690, 3700, 3900, 3950, 4300, 4320, 4600, 4620, 4900, 4920, 5200, 5220, 5500, 5520, 5800 };
static const uint16_t bs_p[] = { 0, SNES_BTN_START, 0, SNES_BTN_A, 0, SNES_BTN_A, 0, SNES_BTN_START, 0, SNES_BTN_A, 0, SNES_BTN_A, 0, SNES_BTN_A, 0, SNES_BTN_A, 0, SNES_BTN_A, 0, SNES_BTN_A, 0, SNES_BTN_A, 0, SNES_BTN_A, 0, SNES_BTN_A, 0, SNES_BTN_A, 0, SNES_BTN_A, 0, SNES_BTN_A, 0, SNES_BTN_A, 0, SNES_BTN_A, 0, SNES_BTN_A, 0, SNES_BTN_A, 0, SNES_BTN_A, 0, SNES_BTN_A, 0, SNES_BTN_A, 0, SNES_BTN_A, 0, SNES_BTN_A, 0, SNES_BTN_A, 0, SNES_BTN_A, 0, SNES_BTN_A, 0, SNES_BTN_A, 0, SNES_BTN_A, 0, SNES_BTN_A, 0, SNES_BTN_A, 0, SNES_BTN_A, 0, SNES_BTN_A, 0, SNES_BTN_A, 0, SNES_BTN_A, 0, 0, SNES_BTN_DOWN, 0, SNES_BTN_RIGHT, 0, SNES_BTN_DOWN, 0, SNES_BTN_DOWN, 0, SNES_BTN_RIGHT, 0, SNES_BTN_DOWN, 0, SNES_BTN_RIGHT, 0, SNES_BTN_DOWN, 0, SNES_BTN_RIGHT, 0 };
static uint16_t script_pad(uint32_t frame)
{
    int i, n = sizeof(bs_f) / sizeof(bs_f[0]), cur = 0;
    for (i = 0; i < n && bs_f[i] <= frame; i++) cur = bs_p[i];
    return (uint16_t)cur;
}
#endif

extern const uint8_t  rom_data[];
extern const uint32_t rom_size;

static SNES g_snes;

static char *put_str(char *p, const char *k) { while (*k) *p++ = *k++; return p; }
static char *put_int(char *p, int v) { itoa(v, p); while (*p) p++; return p; }

int main(void)
{
    int32_t dw = 0, dh = 0;
    int dst_x, dst_y, frames = 0, t0, ta, tb;
    int skip_ms = 0, draw_ms = 0, disp_ms = 0, n_skip = 0, n_draw = 0;
#if SNES_AUTOPLAY
    int bench_t0 = 0;
#endif
    uint32_t n = 0;
    char msg[64];

    display_get_size(&dw, &dh);
    dst_x = (dw - SNES_W) / 2;
    dst_y = (dh - SNES_H) / 2;

    if (snes_init(&g_snes, rom_data, rom_size) != 0) {
        printf_native("snes: bad ROM");
        app_switch("akira_shell");
        return 1;
    }

    display_clear(0);
    display_flush();
    t0 = rtc_get_uptime_ms();

    while (1) {
        uint32_t b = (uint32_t)input_get_buttons();
        uint16_t pad = 0;
        int draw = (n % (SNES_FRAMESKIP + 1)) == 0;
#if SNES_AUTOPLAY
        if (n < BENCH_START) draw = 0;
        b = 0;
#endif
        n++;

        if (b & AKIRA_BTN_UP)    pad |= SNES_BTN_UP;
        if (b & AKIRA_BTN_DOWN)  pad |= SNES_BTN_DOWN;
        if (b & AKIRA_BTN_LEFT)  pad |= SNES_BTN_LEFT;
        if (b & AKIRA_BTN_RIGHT) pad |= SNES_BTN_RIGHT;
        if (b & AKIRA_BTN_A)     pad |= SNES_BTN_A;
        if (b & AKIRA_BTN_B)     pad |= SNES_BTN_B;
        if (b & AKIRA_BTN_X)     pad |= SNES_BTN_X;
        if (b & AKIRA_BTN_Y)     pad |= SNES_BTN_Y;
        if (b & BTN_HOME)        pad |= SNES_BTN_START;
#if SNES_AUTOPLAY
        pad = script_pad(n - 1);
#endif
        snes_set_pad(&g_snes, 0, pad);

        g_snes.skip_render = !draw;
        /* the previous frame's SPI transfer reads fb: it must be done before the PPU rewrites it */
        if (draw) display_raw_wait();
        ta = rtc_get_uptime_ms();
        snes_run_frame(&g_snes);
        tb = rtc_get_uptime_ms();
        if (draw) {
            /* queued on a system work queue: overlaps with the next (skipped) frame */
            display_raw_write_async(dst_x, dst_y, SNES_W, SNES_H, g_snes.fb, SNES_W * SNES_H * 2);
            disp_ms += rtc_get_uptime_ms() - tb;
            draw_ms += tb - ta; n_draw++;
        } else {
            skip_ms += tb - ta; n_skip++;
        }

#if SNES_AUTOPLAY
        if (n == BENCH_START) bench_t0 = rtc_get_uptime_ms();
        if (n == BENCH_END) {
            int ms = rtc_get_uptime_ms() - bench_t0;
            char *q = put_str(msg, "snes BENCH window fps*100=");
            put_int(q, ms > 0 ? (BENCH_END - BENCH_START) * 100000 / ms : 0);
            printf_native(msg);
        }
        if (n < BENCH_START) { frames = 0; skip_ms = draw_ms = disp_ms = n_skip = n_draw = 0; t0 = rtc_get_uptime_ms(); continue; }
#endif
        if (++frames == REPORT_FRAMES) {
            int ms = rtc_get_uptime_ms() - t0;
            /* x10: fps; ms/frame of a frame with PPU skipped (CPU only), with PPU drawn, and the display push */
            char *p = put_str(msg, "snes fps*10=");
            p = put_int(p, ms > 0 ? (frames * 10000) / ms : 0);
            p = put_str(p, " cpu*10=");
            p = put_int(p, n_skip ? skip_ms * 10 / n_skip : 0);
            p = put_str(p, " cpu+ppu*10=");
            p = put_int(p, n_draw ? draw_ms * 10 / n_draw : 0);
            p = put_str(p, " disp*10=");
            put_int(p, n_draw ? disp_ms * 10 / n_draw : 0);
            printf_native(msg);
            frames = 0; skip_ms = draw_ms = disp_ms = n_skip = n_draw = 0;
            t0 = rtc_get_uptime_ms();
        }
    }
    return 0;
}
