/**
 * @file main.c
 * @brief AkiraOS SNES emulator app entry point
 *
 * ROM is embedded by rom_to_wasm.py as rom_data/rom_size.
 *
 * Button mapping (input_get_buttons()):
 *   D-pad -> D-pad, A/B/X/Y -> SNES A/B/X/Y, Home -> pause menu
 *   (Start and Select are sent from the pause menu; L/R have no button yet.)
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

/* ── Settings (persistent, key "snes/<name>"; same scheme as the NES core) ── */
static int g_frameskip = SNES_FRAMESKIP;   /* frames skipped per drawn frame, 0..3 */
static int g_colorfx;                      /* 1 = colour maths (exact, slower), 0 = skipped */

static void load_settings(void)
{
    char buf[8];
    if (settings_get("snes/frameskip", buf, sizeof(buf)) == 0 && buf[0] >= '0' && buf[0] <= '3')
        g_frameskip = buf[0] - '0';
    if (settings_get("snes/colorfx", buf, sizeof(buf)) == 0)
        g_colorfx = buf[0] == '1';
}
static void save_setting(const char *key, int v)
{
    char buf[2] = { (char)('0' + v), 0 };
    settings_set(key, buf);
}

/* ── Save states: raw struct ranges streamed to storage (no staging buffer) ─
 * [0, sram_mask) is CPU, PPU, DMA, WRAM and SRAM; [wmadd, wr_count) is the scalar
 * machine state. Pointers, page tables and the framebuffer are rebuilt. */
#define SAVE_SLOT_COUNT 3
#define SS_MAGIC 0x31534B41u   /* "AKS1" */
#define SS_A_LEN ((uint32_t)__builtin_offsetof(SNES, sram_mask))
#define SS_B_OFF ((uint32_t)__builtin_offsetof(SNES, wmadd))
#define SS_B_LEN ((uint32_t)__builtin_offsetof(SNES, wr_count) - SS_B_OFF)
#define SS_CHUNK 16384
typedef struct { uint32_t magic, rom_size, a_len, b_len; } SSHead;

static const char *slot_path(int slot)
{
    static const char *PATHS[SAVE_SLOT_COUNT] = { "snes1.sav", "snes2.sav", "snes3.sav" };
    return PATHS[slot];
}
static int io_all(int fd, uint8_t *p, uint32_t len, int write)
{
    while (len) {
        int c = len > SS_CHUNK ? SS_CHUNK : (int)len;
        int n = write ? storage_write(fd, p, c) : storage_read(fd, p, c);
        if (n != c) return -1;
        p += c; len -= c;
    }
    return 0;
}
static int do_save_state(int slot)
{
    SSHead h = { SS_MAGIC, rom_size, SS_A_LEN, SS_B_LEN };
    int fd = storage_open(slot_path(slot), STORAGE_O_WRITE), rc;
    if (fd < 0) return -1;
    rc = io_all(fd, (uint8_t *)&h, sizeof(h), 1) | io_all(fd, (uint8_t *)&g_snes, SS_A_LEN, 1) |
         io_all(fd, (uint8_t *)&g_snes + SS_B_OFF, SS_B_LEN, 1);
    storage_close(fd);
    return rc ? -1 : 0;
}
static int do_load_state(int slot)
{
    SSHead h;
    int rc, fd = storage_open(slot_path(slot), STORAGE_O_READ);
    if (fd < 0) return -1;
    if (io_all(fd, (uint8_t *)&h, sizeof(h), 0) || h.magic != SS_MAGIC || h.rom_size != rom_size ||
        h.a_len != SS_A_LEN || h.b_len != SS_B_LEN) {
        storage_close(fd);
        return -1;
    }
    rc = io_all(fd, (uint8_t *)&g_snes, SS_A_LEN, 0) | io_all(fd, (uint8_t *)&g_snes + SS_B_OFF, SS_B_LEN, 0);
    storage_close(fd);
    if (rc) snes_init(&g_snes, rom_data, rom_size);   /* torn read: restart rather than run on a mix */
    snes_rebuild_pages(&g_snes);
    ppu_invalidate();
    return rc ? -1 : 0;
}
static int save_slot_exists(int slot)
{
    int fd = storage_open(slot_path(slot), STORAGE_O_READ);
    if (fd < 0) return 0;
    storage_close(fd);
    return 1;
}

/* ── Menus (same look as the NES core) ───────────────────────────────── */
#define C_BLACK 0x0000u
#define C_WHITE 0xFFFFu
#define C_LGRAY 0xC618u
#define C_DGRAY 0x4208u
#define C_DIM   0x528Au
#define ROW_H   22
#define MENU_PAD 10
#define HDR_H   22

enum { PM_RESUME, PM_SAVE, PM_LOAD, PM_SETTINGS, PM_START, PM_SELECT, PM_RESTART, PM_EXIT, PM_COUNT };
static const char *PM_LABELS[PM_COUNT] = {
    "Resume", "Save State", "Load State", "Settings...", "Press Start", "Press Select", "Restart", "Exit to Menu"
};

/* A press only re-arms after REL_STABLE released reads, so contact bounce is not a second press. */
#define REL_STABLE 2
typedef struct { int held, rel_run; } debkey_t;
static int deb_edge(debkey_t *k, int raw)
{
    if (raw) {
        k->rel_run = 0;
        if (!k->held) { k->held = 1; return 1; }
        return 0;
    }
    if (k->rel_run < REL_STABLE) k->rel_run++;
    if (k->rel_run >= REL_STABLE) k->held = 0;
    return 0;
}
static int btn_held(uint32_t mask) { return (input_get_buttons() & mask) != 0; }

static int32_t g_dw, g_dh;

static void draw_header(int x, int y, int w, const char *title)
{
    display_rect(x, y, w, HDR_H, C_BLACK);
    display_text(x + MENU_PAD, y + 4, title, C_WHITE);
}
static void draw_row(int x, int y, int w, const char *lbl, const char *val, int sel)
{
    display_rect(x, y, w, ROW_H, sel ? C_BLACK : C_WHITE);
    display_text(x + MENU_PAD, y + (ROW_H - 8) / 2, lbl, sel ? C_WHITE : C_BLACK);
    if (val && val[0] && x + w - 60 > x + 80)
        display_text(x + w - 60, y + (ROW_H - 8) / 2, val, sel ? C_LGRAY : C_DIM);
    display_hline(x, y + ROW_H - 1, w, C_LGRAY);
}
static void show_status(const char *msg)
{
    const int w = 120, h = ROW_H, x = (g_dw - w) / 2, y = (g_dh - h) / 2;
    display_rect(x - 2, y - 2, w + 4, h + 4, C_DGRAY);
    display_rect(x, y, w, h, C_BLACK);
    display_text(x + MENU_PAD, y + (h - 8) / 2, msg, C_WHITE);
    display_flush();
    delay(500000);
}

static void show_settings_menu(void)
{
    static const char *FS[4] = { "All", "1/2", "1/3", "1/4" };
    const int OW = (g_dw >= 190) ? 180 : g_dw - 4, OH = HDR_H + 3 * ROW_H;
    const int OX = (g_dw - OW) / 2, OY = (g_dh - OH) / 2;
    int cur = 0, dirty = 1, i;
    debkey_t ku = { .held = btn_held(AKIRA_BTN_UP) },   kd = { .held = btn_held(AKIRA_BTN_DOWN) };
    debkey_t ka = { .held = btn_held(AKIRA_BTN_A) },    kb = { .held = btn_held(AKIRA_BTN_Y) };
    debkey_t kl = { .held = btn_held(AKIRA_BTN_LEFT) }, kr = { .held = btn_held(AKIRA_BTN_RIGHT) };

    while (1) {
        if (dirty) {
            display_rect(OX - 2, OY - 2, OW + 4, OH + 4, C_DGRAY);
            display_rect(OX, OY, OW, OH, C_WHITE);
            draw_header(OX, OY, OW, "Settings");
            draw_row(OX, OY + HDR_H, OW, "Frame Draw", FS[g_frameskip], cur == 0);
            draw_row(OX, OY + HDR_H + ROW_H, OW, "Colour FX", g_colorfx ? "On" : "Off", cur == 1);
            draw_row(OX, OY + HDR_H + 2 * ROW_H, OW, "Back", "", cur == 2);
            display_flush();
            dirty = 0;
        }
        int u = deb_edge(&ku, btn_held(AKIRA_BTN_UP)), d = deb_edge(&kd, btn_held(AKIRA_BTN_DOWN));
        int a = deb_edge(&ka, btn_held(AKIRA_BTN_A)),  b = deb_edge(&kb, btn_held(AKIRA_BTN_Y));
        int l = deb_edge(&kl, btn_held(AKIRA_BTN_LEFT)), r = deb_edge(&kr, btn_held(AKIRA_BTN_RIGHT));
        if (u) { cur = (cur + 2) % 3; dirty = 1; }
        if (d) { cur = (cur + 1) % 3; dirty = 1; }
        if (l || r || (a && cur < 2)) {
            i = (a || r) ? 1 : -1;
            if (cur == 0) { g_frameskip = (g_frameskip + i + 4) % 4; save_setting("snes/frameskip", g_frameskip); }
            if (cur == 1) { g_colorfx ^= 1; ppu_set_colormath(g_colorfx); save_setting("snes/colorfx", g_colorfx); }
            dirty = 1;
        } else if (a || b) break;
        delay(16667);
    }
}

static void show_slot_menu(int for_save)
{
    const int OW = (g_dw >= 175) ? 170 : g_dw - 4, OH = HDR_H + (SAVE_SLOT_COUNT + 1) * ROW_H;
    const int OX = (g_dw - OW) / 2, OY = (g_dh - OH) / 2;
    int cur = 0, dirty = 1, i;
    debkey_t ku = { .held = btn_held(AKIRA_BTN_UP) }, kd = { .held = btn_held(AKIRA_BTN_DOWN) };
    debkey_t ka = { .held = btn_held(AKIRA_BTN_A) },  kb = { .held = btn_held(AKIRA_BTN_Y) };

    while (1) {
        if (dirty) {
            char label[8] = "Slot 1";
            display_rect(OX - 2, OY - 2, OW + 4, OH + 4, C_DGRAY);
            display_rect(OX, OY, OW, OH, C_WHITE);
            draw_header(OX, OY, OW, for_save ? "Save to Slot" : "Load from Slot");
            for (i = 0; i < SAVE_SLOT_COUNT; i++) {
                label[5] = (char)('1' + i);
                draw_row(OX, OY + HDR_H + i * ROW_H, OW, label, save_slot_exists(i) ? "Used" : "Empty", i == cur);
            }
            draw_row(OX, OY + HDR_H + SAVE_SLOT_COUNT * ROW_H, OW, "Back", "", cur == SAVE_SLOT_COUNT);
            display_flush();
            dirty = 0;
        }
        if (deb_edge(&ku, btn_held(AKIRA_BTN_UP)))   { cur = (cur + SAVE_SLOT_COUNT) % (SAVE_SLOT_COUNT + 1); dirty = 1; }
        if (deb_edge(&kd, btn_held(AKIRA_BTN_DOWN))) { cur = (cur + 1) % (SAVE_SLOT_COUNT + 1);               dirty = 1; }
        if (deb_edge(&ka, btn_held(AKIRA_BTN_A))) {
            int rc;
            if (cur == SAVE_SLOT_COUNT) return;
            rc = for_save ? do_save_state(cur) : do_load_state(cur);
            show_status(rc == 0 ? (for_save ? "Saved!" : "Loaded!") : (for_save ? "Save failed" : "Load failed"));
            return;
        }
        if (deb_edge(&kb, btn_held(AKIRA_BTN_Y))) return;
        delay(16667);
    }
}

/* Returns PM_RESUME, PM_START, PM_SELECT, PM_RESTART or PM_EXIT. */
static int show_pause_menu(void)
{
    const int OW = (g_dw >= 175) ? 170 : g_dw - 4, OH = HDR_H + PM_COUNT * ROW_H;
    const int OX = (g_dw - OW) / 2, OY = (g_dh - OH) / 2;
    int cur = 0, dirty = 1, i;
    debkey_t ks = { .held = 1 }, ku = { .held = btn_held(AKIRA_BTN_UP) }, kd = { .held = btn_held(AKIRA_BTN_DOWN) };
    debkey_t ka = { .held = btn_held(AKIRA_BTN_A) }, kb = { .held = btn_held(AKIRA_BTN_Y) };

    display_raw_wait();
    delay(40000);
    while (1) {
        if (dirty) {
            display_rect(OX - 2, OY - 2, OW + 4, OH + 4, C_DGRAY);
            display_rect(OX, OY, OW, OH, C_WHITE);
            draw_header(OX, OY, OW, "PAUSED");
            for (i = 0; i < PM_COUNT; i++) draw_row(OX, OY + HDR_H + i * ROW_H, OW, PM_LABELS[i], "", i == cur);
            display_flush();
            dirty = 0;
        }
        if (deb_edge(&ks, btn_held(BTN_HOME))) return PM_RESUME;
        if (deb_edge(&ku, btn_held(AKIRA_BTN_UP)))   { cur = (cur + PM_COUNT - 1) % PM_COUNT; dirty = 1; }
        if (deb_edge(&kd, btn_held(AKIRA_BTN_DOWN))) { cur = (cur + 1) % PM_COUNT;            dirty = 1; }
        if (deb_edge(&ka, btn_held(AKIRA_BTN_A))) {
            if (cur == PM_SAVE)     { show_slot_menu(1);     dirty = 1; continue; }
            if (cur == PM_LOAD)     { show_slot_menu(0);     dirty = 1; continue; }
            if (cur == PM_SETTINGS) { show_settings_menu();  dirty = 1; continue; }
            return cur;
        }
        if (deb_edge(&kb, btn_held(AKIRA_BTN_Y))) return PM_RESUME;
        delay(16667);
    }
}

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
    debkey_t k_home = { .held = 1 };
    int pulse = 0, pulse_frames = 0;

    display_get_size(&dw, &dh);
    g_dw = dw; g_dh = dh;
    load_settings();
    ppu_set_colormath(g_colorfx);
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
        int draw = (n % (g_frameskip + 1)) == 0;
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
        if (pulse_frames) { pad |= pulse; pulse_frames--; }
#if SNES_AUTOPLAY
        pad = script_pad(n - 1);
#endif
        snes_set_pad(&g_snes, 0, pad);

        if (deb_edge(&k_home, (b & BTN_HOME) != 0)) {
            int choice = show_pause_menu();
            if (choice == PM_EXIT) {
                display_clear(0);
                display_flush();
                app_switch("akira_shell");
                return 0;
            }
            if (choice == PM_RESTART) snes_init(&g_snes, rom_data, rom_size);
            if (choice == PM_START || choice == PM_SELECT) {
                pulse = choice == PM_START ? SNES_BTN_START : SNES_BTN_SELECT;
                pulse_frames = 6;
            }
            display_clear(0);
            display_flush();
            n = 0;
            continue;
        }

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
