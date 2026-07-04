/**
 * @file main.c
 * @brief AkiraOS Game Boy / Game Boy Color emulator — app entry point
 *
 * Mirrors the (working) NES core's input and display approach: gpio_read for
 * buttons and display_raw_write for the framebuffer, with the display size
 * queried at runtime. The GB's 160×144 image is nearest-neighbour upscaled to
 * fill the panel (the one thing NES doesn't need, since NES is already ~screen
 * sized).
 *
 * ROM data injected by rom_to_wasm.py:
 *   const uint8_t  rom_data[];  const uint32_t rom_size;
 *
 * Button mapping (akiraconsole GPIOs — identical to the NES core):
 *   D-pad U/D/L/R (4/5/6/7) → GB d-pad
 *   A (15) → GB A      B (16) → GB B
 *   X (17) → GB Select Y (41) → GB Start
 *   OK (0, active-low) → pause overlay
 *
 * @license Apache-2.0
 */
#include "akira_api.h"
#include "gb.h"

/* ── ROM data injected by rom_to_wasm.py ─────────────────────────────── */
extern const uint8_t  rom_data[];
extern const uint32_t rom_size;

/* ── GPIO pins (akiraconsole — same as the NES core) ─────────────────── */
#define PIN_UP        4
#define PIN_DOWN      5
#define PIN_LEFT      6
#define PIN_RIGHT     7
#define PIN_A        15
#define PIN_B        16
#define PIN_SETTINGS  0   /* BTN.OK = GPIO0, active-low pull-up */
#define PIN_X        17
#define PIN_Y        41

/* ── Colours (RGB565) ────────────────────────────────────────────────── */
#define C_BLACK   0x0000u
#define C_WHITE   0xFFFFu
#define C_LGRAY   0xC618u
#define C_DGRAY   0x4208u
#define C_DIM     0x528Au

/* ── UI layout ────────────────────────────────────────────────────────── */
#define ROW_H     26
#define MENU_PAD  10
#define HDR_H     22

/* ── Display geometry (resolved at runtime) ──────────────────────────── */
static int g_disp_w, g_disp_h;
static int g_dst_x, g_dst_y, g_dst_w, g_dst_h;

/* Full upscaled-frame buffer (max panel 400×240). Lets us blit the whole
 * scaled image in ONE display_raw_write — like the NES core's letterbox path —
 * instead of one call per row (which is ~2fps on the Sharp). */
#define MAX_DST_W  400
#define MAX_DST_H  240
static uint16_t s_frame[MAX_DST_W * MAX_DST_H];

/* ── GB machine (static to avoid stack overflow) ──────────────────────── */
static GB gb;

/* ── Pause menu ──────────────────────────────────────────────────────── */
#define PM_RESUME    0
#define PM_SETTINGS  1
#define PM_RESTART   2
#define PM_EXIT      3
#define PM_COUNT     4
static const char *PM_LABELS[PM_COUNT] = {
    "Resume", "Settings...", "Restart", "Exit to Menu"
};

/* ── Runtime settings ────────────────────────────────────────────────── */
static int g_frameskip = 1;
static const int FS_MOD[4] = {1, 2, 3, 4};

/* ── Tiny helpers ─────────────────────────────────────────────────────── */
static int satoi(const char *s)
{
    int v = 0;
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return v;
}
static void sitoa(int v, char *b, int max)
{
    if (v == 0) { b[0]='0'; b[1]='\0'; return; }
    char t[8]; int ti=0, i=0;
    while (v>0 && ti<7) { t[ti++]='0'+v%10; v/=10; }
    while (ti-->0 && i<max-1) b[i++]=t[ti];
    b[i]='\0';
}
static void load_settings(void)
{
    char buf[8];
    if (settings_get("gb/frameskip", buf, sizeof(buf)) == 0) {
        g_frameskip = satoi(buf);
        if (g_frameskip < 0) g_frameskip = 0;
        if (g_frameskip > 3) g_frameskip = 3;
    }
}
static void save_int(const char *key, int val)
{
    char buf[8];
    sitoa(val, buf, sizeof(buf));
    settings_set(key, buf);
}

/* ── GPIO init (identical to the NES core) ───────────────────────────── */
static void init_gpio(void)
{
    int f = GPIO_INPUT | GPIO_PULL_DOWN;
    gpio_configure(PIN_UP,       f);
    gpio_configure(PIN_DOWN,     f);
    gpio_configure(PIN_LEFT,     f);
    gpio_configure(PIN_RIGHT,    f);
    gpio_configure(PIN_A,        f);
    gpio_configure(PIN_B,        f);
    gpio_configure(PIN_SETTINGS, GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);
    gpio_configure(PIN_X,        f);
    gpio_configure(PIN_Y,        f);
}

/* ── Display geometry: fit 160×144 into the panel, aspect-preserved ──── */
static void init_display_geometry(void)
{
    display_get_size(&g_disp_w, &g_disp_h);
    if (g_disp_w <= 0 || g_disp_h <= 0) { g_disp_w = 320; g_disp_h = 240; }

    if ((long)g_disp_w * GB_H <= (long)g_disp_h * GB_W) {
        g_dst_w = g_disp_w;
        g_dst_h = (int)(((long)g_disp_w * GB_H) / GB_W);
    } else {
        g_dst_h = g_disp_h;
        g_dst_w = (int)(((long)g_disp_h * GB_W) / GB_H);
    }
    if (g_dst_w > MAX_DST_W) g_dst_w = MAX_DST_W;
    if (g_dst_h > MAX_DST_H) g_dst_h = MAX_DST_H;
    if (g_dst_w < 1) g_dst_w = 1;
    if (g_dst_h < 1) g_dst_h = 1;
    g_dst_x = (g_disp_w - g_dst_w) / 2;
    g_dst_y = (g_disp_h - g_dst_h) / 2;
    if (g_dst_x < 0) g_dst_x = 0;
    if (g_dst_y < 0) g_dst_y = 0;
}

/* ── Upscale the 160×144 GB frame to g_dst_w×g_dst_h via display_raw_write.
 *    Mirrors the NES core's per-row display_raw_write render path. ──────── */
static void render_gb_frame(void)
{
    for (int dy = 0; dy < g_dst_h; dy++) {
        int sy = (dy * GB_H) / g_dst_h;
        const uint16_t *srow = gb.fb + sy * GB_W;
        uint16_t *drow = s_frame + (size_t)dy * g_dst_w;
        for (int dx = 0; dx < g_dst_w; dx++)
            drow[dx] = srow[(dx * GB_W) / g_dst_w];
    }
    display_raw_write(g_dst_x, g_dst_y, g_dst_w, g_dst_h,
                      s_frame, (uint32_t)g_dst_w * g_dst_h * 2);
}

/* ── Draw the black letterbox around the scaled image (OS framebuffer) ─── */
static void draw_border(void)
{
    display_clear(C_BLACK);
    display_flush();
}

/* ── Draw helpers ─────────────────────────────────────────────────────── */
static void draw_header(int x, int y, int w, const char *title)
{
    display_rect(x, y, w, HDR_H, C_BLACK);
    display_text(x + MENU_PAD, y + 4, title, C_WHITE);
}
static void draw_row(int x, int y, int w,
                     const char *lbl, const char *val, int sel)
{
    uint16_t bg = sel ? C_BLACK : C_WHITE;
    uint16_t fg = sel ? C_WHITE : C_BLACK;
    display_rect(x, y, w, ROW_H, bg);
    display_text(x + MENU_PAD, y + (ROW_H - 8) / 2, lbl, fg);
    if (val && val[0]) {
        int vx = x + w - 60;
        if (vx > x + 80)
            display_text(vx, y + (ROW_H - 8) / 2, val, sel ? C_LGRAY : C_DIM);
    }
    display_hline(x, y + ROW_H - 1, w, C_LGRAY);
}

/* ── Settings sub-menu ────────────────────────────────────────────────── */
static void show_settings_menu(void)
{
    static const char *FS_LABELS[4] = {
        "Off (60fps)", "Half (30fps)", "1/3 (20fps)", "1/4 (15fps)"
    };
    const int OW = (g_disp_w >= 190) ? 180 : g_disp_w - 4;
    const int OH = HDR_H + 2 * ROW_H;
    const int OX = (g_disp_w - OW) / 2, OY = (g_disp_h - OH) / 2;

    int cur=0, dirty=1;
    int pu=0, pd=0, pa=0, pb=0, pl=0, pr=0;

    while (1) {
        if (dirty) {
            display_rect(OX-2, OY-2, OW+4, OH+4, C_DGRAY);
            display_rect(OX,   OY,   OW,   OH,   C_WHITE);
            draw_header(OX, OY, OW, "Settings");
            int ry = OY + HDR_H;
            draw_row(OX, ry, OW, "Frame Skip", FS_LABELS[g_frameskip], cur==0); ry += ROW_H;
            draw_row(OX, ry, OW, "Back", "", cur==1);
            display_flush();
            dirty = 0;
        }
        int u=gpio_read(PIN_UP), d=gpio_read(PIN_DOWN);
        int a=gpio_read(PIN_A),  b=gpio_read(PIN_B);
        int l=gpio_read(PIN_LEFT),r=gpio_read(PIN_RIGHT);

        if (u&&!pu) { cur=(cur+1)%2; dirty=1; }
        if (d&&!pd) { cur=(cur+1)%2; dirty=1; }
        if ((l&&!pl)||(r&&!pr)) {
            if (cur == 0) {
                g_frameskip = (g_frameskip + ((r&&!pr)?1:3)) % 4;
                save_int("gb/frameskip", g_frameskip);
                dirty = 1;
            }
        }
        if (a&&!pa) {
            if (cur==0) { g_frameskip=(g_frameskip+1)%4; save_int("gb/frameskip",g_frameskip); dirty=1; }
            else break;
        }
        if (b&&!pb) break;
        pu=u; pd=d; pa=a; pb=b; pl=l; pr=r;
        delay(16667);
    }
}

/* ── Pause overlay (identical structure to the NES core) ──────────────── */
static int show_pause_menu(void)
{
    while (gpio_read(PIN_SETTINGS)) delay(10000);
    delay(40000);

    const int OW = (g_disp_w >= 175) ? 170 : g_disp_w - 4;
    const int OH = HDR_H + PM_COUNT * ROW_H;
    const int OX = (g_disp_w - OW) / 2, OY = (g_disp_h - OH) / 2;

    int cur=0, dirty=1;
    int pu=0, pd=0, pa=0, pb=0, ps=0;

    while (1) {
        if (dirty) {
            for (int x = OX - 20; x <= OX; x += 8) {
                int cx = x < OX ? x : OX;
                display_rect(cx-2, OY-2, OW+4, OH+4, C_DGRAY);
                display_rect(cx,   OY,   OW,   OH,   C_WHITE);
                draw_header(cx, OY, OW, "PAUSED");
                int ry = OY + HDR_H;
                for (int i = 0; i < PM_COUNT; i++) {
                    draw_row(cx, ry, OW, PM_LABELS[i], "", i==cur);
                    ry += ROW_H;
                }
                display_flush();
                delay(8000);
            }
            dirty = 0;
        }
        int s=gpio_read(PIN_SETTINGS);
        int u=gpio_read(PIN_UP), d=gpio_read(PIN_DOWN);
        int a=gpio_read(PIN_A),  b=gpio_read(PIN_B);

        if (s&&!ps) return PM_RESUME;
        if (u&&!pu) { cur=(cur+PM_COUNT-1)%PM_COUNT; dirty=1; }
        if (d&&!pd) { cur=(cur+1)%PM_COUNT;           dirty=1; }
        if (a&&!pa) {
            if (cur==PM_SETTINGS) { show_settings_menu(); dirty=1; continue; }
            return cur;
        }
        if (b&&!pb) return PM_RESUME;
        pu=u; pd=d; pa=a; pb=b; ps=s;
        delay(16667);
    }
}

/* ── Main ─────────────────────────────────────────────────────────────── */
int main(void)
{
    init_gpio();
    load_settings();
    init_display_geometry();

    display_clear(C_BLACK);
    display_text_large(g_disp_w/2 - 40, g_disp_h/2 - 20, "GB", C_WHITE);
    display_text(g_disp_w/2 - 44, g_disp_h/2 + 10, "Loading ROM...", C_DIM);
    display_flush();

    gb_init(&gb, rom_data, rom_size);

    draw_border();

    int settings_held = 0;
    int frame_count   = 0;

    while (1) {
        /* ── Buttons (identical mapping to the NES core) ──────────────── */
        uint8_t btns = 0;
        if (gpio_read(PIN_RIGHT))  btns |= GB_BTN_RIGHT;
        if (gpio_read(PIN_LEFT))   btns |= GB_BTN_LEFT;
        if (gpio_read(PIN_DOWN))   btns |= GB_BTN_DOWN;
        if (gpio_read(PIN_UP))     btns |= GB_BTN_UP;
        if (gpio_read(PIN_A))      btns |= GB_BTN_A;
        if (gpio_read(PIN_B))      btns |= GB_BTN_B;
        if (gpio_read(PIN_X))      btns |= GB_BTN_SELECT;
        if (gpio_read(PIN_Y))      btns |= GB_BTN_START;
        gb_set_buttons(&gb, btns);

        /* ── Pause button (rising edge) ───────────────────────────────── */
        int settings_now = gpio_read(PIN_SETTINGS);
        if (settings_now && !settings_held) {
            int choice = show_pause_menu();
            if (choice == PM_EXIT) {
                display_clear(C_BLACK); display_flush();
                app_switch("akira_shell");
                return 0;
            }
            if (choice == PM_RESTART) {
                gb_init(&gb, rom_data, rom_size);
                frame_count = 0;
            }
            draw_border();
            settings_held = 0;
            continue;
        }
        settings_held = settings_now;

        /* ── Emulate one GB frame ─────────────────────────────────────── */
        int mod  = FS_MOD[g_frameskip];
        int skip = (frame_count % mod) != 0;
        gb.ppu.skip_render = (uint8_t)skip;
        gb_step_frame(&gb);
        frame_count++;

        if (!skip) {
            render_gb_frame();
        }
    }

    return 0;
}
