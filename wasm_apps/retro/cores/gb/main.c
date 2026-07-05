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
 *   A (15) → GB A      physical Y (41) → GB B
 *   physical B (16) → GB Select   physical X (17) → GB Start
 *   OK (0, active-low) → pause overlay
 *
 * @license Apache-2.0
 */
#include "akira_api.h"
#include "gb.h"
#include "save_state.h"

#define SAVE_SLOT_COUNT 3
static const char *save_slot_path(int slot)
{
    static const char *PATHS[SAVE_SLOT_COUNT] = { "save1.bin", "save2.bin", "save3.bin" };
    return PATHS[slot];
}

/* ── ROM data injected by rom_to_wasm.py ─────────────────────────────── */
extern const uint8_t  rom_data[];
extern const uint32_t rom_size;

/* ── GPIO pins (akiraconsole — same as the NES core) ─────────────────── */
#define PIN_UP        4
#define PIN_DOWN      5
#define PIN_LEFT      6
#define PIN_RIGHT     7
#define PIN_A        15
#define PIN_B        41   /* physical Y — swapped to logical B */
#define PIN_SETTINGS  0   /* BTN.OK = GPIO0, active-low pull-up */
#define PIN_X        16   /* physical B — swapped to logical X */
#define PIN_Y        17   /* physical X — swapped to logical Y */

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

/* ── Debounced button edge-detect ─────────────────────────────────────────
 * A single-sample "x && !prev" edge check re-fires on mechanical contact
 * bounce: a real press/release can toggle the raw pin several times across
 * consecutive ~16.7ms poll ticks, each toggle looking like a fresh press.
 * deb_edge() requires REL_STABLE consecutive released reads before a button
 * can re-arm, so bounce during release can't be seen as a second press. */
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
#define PM_RESUME     0
#define PM_SAVE_STATE 1
#define PM_LOAD_STATE 2
#define PM_SETTINGS   3
#define PM_RESTART    4
#define PM_EXIT       5
#define PM_COUNT      6
static const char *PM_LABELS[PM_COUNT] = {
    "Resume", "Save State", "Load State", "Settings...", "Restart", "Exit to Menu"
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
/* printf() only supports %d/%s (see akira_api.h) — no %x, so format a
 * 16-bit value as 4 zero-padded hex digits ourselves. */
static void hex4(uint16_t v, char *b)
{
    static const char *digits = "0123456789ABCDEF";
    b[0]=digits[(v>>12)&0xF]; b[1]=digits[(v>>8)&0xF];
    b[2]=digits[(v>>4)&0xF];  b[3]=digits[v&0xF];
    b[4]='\0';
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
            display_text(vx, y + (ROW_H - 8) / 2, val, sel ? C_LGRAY : C_BLACK);
    }
    display_hline(x, y + ROW_H - 1, w, C_LGRAY);
}

/* ── Settings sub-menu ────────────────────────────────────────────────── */
#define SETTINGS_HINT_H 18
static void show_settings_menu(void)
{
    static const char *FS_LABELS[4] = {
        "Off (60fps)", "Half (30fps)", "1/3 (20fps)", "1/4 (15fps)"
    };
    const int OW = (g_disp_w >= 230) ? 220 : g_disp_w - 4;
    const int OH = HDR_H + 2 * ROW_H + SETTINGS_HINT_H;
    const int OX = (g_disp_w - OW) / 2, OY = (g_disp_h - OH) / 2;

    int cur=0, dirty=1;
    debkey_t ku={.held=gpio_read(PIN_UP)},   kd={.held=gpio_read(PIN_DOWN)};
    debkey_t ka={.held=gpio_read(PIN_A)},    kb={.held=gpio_read(PIN_B)};
    debkey_t kl={.held=gpio_read(PIN_LEFT)}, kr={.held=gpio_read(PIN_RIGHT)};

    while (1) {
        if (dirty) {
            display_rect(OX-2, OY-2, OW+4, OH+4, C_DGRAY);
            display_rect(OX,   OY,   OW,   OH,   C_WHITE);
            draw_header(OX, OY, OW, "Settings");
            int ry = OY + HDR_H;
            draw_row(OX, ry, OW, "Frame Skip", FS_LABELS[g_frameskip], cur==0); ry += ROW_H;
            draw_row(OX, ry, OW, "Back", "", cur==1); ry += ROW_H;
            display_hline(OX, ry, OW, C_BLACK);
            display_text(OX + MENU_PAD, ry + (SETTINGS_HINT_H - 8) / 2,
                         "L/R:Change  A:OK  B:Back", C_BLACK);
            display_flush();
            dirty = 0;
        }
        int u=deb_edge(&ku, gpio_read(PIN_UP));
        int d=deb_edge(&kd, gpio_read(PIN_DOWN));
        int a=deb_edge(&ka, gpio_read(PIN_A));
        int b=deb_edge(&kb, gpio_read(PIN_B));
        int l=deb_edge(&kl, gpio_read(PIN_LEFT));
        int r=deb_edge(&kr, gpio_read(PIN_RIGHT));

        if (u) { cur=(cur+1)%2; dirty=1; }
        if (d) { cur=(cur+1)%2; dirty=1; }
        if (l||r) {
            if (cur == 0) {
                g_frameskip = (g_frameskip + (r?1:3)) % 4;
                save_int("gb/frameskip", g_frameskip);
                dirty = 1;
            }
        }
        if (a) {
            if (cur==0) { g_frameskip=(g_frameskip+1)%4; save_int("gb/frameskip",g_frameskip); dirty=1; }
            else break;
        }
        if (b) break;
        delay(16667);
    }
}

/* Briefly shows a standalone status toast, then leaves it to the caller
 * to redraw whatever was on screen (caller must set dirty=1 afterward). */
static void show_pause_status(const char *msg)
{
    const int w = 120, h = ROW_H;
    const int x = (g_disp_w - w) / 2, y = (g_disp_h - h) / 2;
    display_rect(x-2, y-2, w+4, h+4, C_DGRAY);
    display_rect(x,   y,   w,   h,   C_BLACK);
    display_text(x + MENU_PAD, y + (h - 8) / 2, msg, C_WHITE);
    display_flush();
    delay(500000);
}

/* gb_state_{pack,unpack} only marshal to/from a flat header buffer
 * (save_state.c can't include akira_api.h — see its header comment), so
 * the actual storage_* I/O, and the separately-streamed cart_ram bytes,
 * happen here. */
static int do_save_state(int slot)
{
    gb_state_pack(&gb);
    int fd = storage_open(save_slot_path(slot), STORAGE_O_WRITE);
    if (fd < 0) return -1;
    int ok = storage_write(fd, gb_state_header_buf(), gb_state_header_size())
              == gb_state_header_size();
    if (ok && gb.cart_ram_size)
        ok = storage_write(fd, gb.cart_ram, (int)gb.cart_ram_size)
              == (int)gb.cart_ram_size;
    storage_close(fd);
    return ok ? 0 : -1;
}

static int do_load_state(int slot)
{
    int fd = storage_open(save_slot_path(slot), STORAGE_O_READ);
    if (fd < 0) return -1;
    int ok = storage_read(fd, gb_state_header_buf(), gb_state_header_size())
              == gb_state_header_size();
    if (ok) ok = (gb_state_unpack(&gb) == 0);
    if (ok && gb.cart_ram_size)
        ok = storage_read(fd, gb.cart_ram, (int)gb.cart_ram_size)
              == (int)gb.cart_ram_size;
    storage_close(fd);
    return ok ? 0 : -1;
}

static int save_slot_exists(int slot)
{
    int fd = storage_open(save_slot_path(slot), STORAGE_O_READ);
    if (fd < 0) return 0;
    storage_close(fd);
    return 1;
}

/* Dedicated slot picker: shown when the user chooses Save/Load State from
 * the pause menu, so slot selection is an explicit screen rather than an
 * L/R-while-highlighted gesture the user has to discover. */
static void show_slot_menu(int for_save)
{
    const int OW = (g_disp_w >= 175) ? 170 : g_disp_w - 4;
    const int OH = HDR_H + (SAVE_SLOT_COUNT + 1) * ROW_H;
    const int OX = (g_disp_w - OW) / 2, OY = (g_disp_h - OH) / 2;
    const int back_row = SAVE_SLOT_COUNT;

    int cur=0, dirty=1;
    debkey_t ku={.held=gpio_read(PIN_UP)}, kd={.held=gpio_read(PIN_DOWN)};
    debkey_t ka={.held=gpio_read(PIN_A)}, kb={.held=gpio_read(PIN_B)};

    while (1) {
        if (dirty) {
            display_rect(OX-2, OY-2, OW+4, OH+4, C_DGRAY);
            display_rect(OX,   OY,   OW,   OH,   C_WHITE);
            draw_header(OX, OY, OW, for_save ? "Save to Slot" : "Load from Slot");
            int ry = OY + HDR_H;
            char label[8] = "Slot 1";
            for (int i = 0; i < SAVE_SLOT_COUNT; i++) {
                label[5] = (char)('1' + i);
                draw_row(OX, ry, OW, label, save_slot_exists(i) ? "Used" : "Empty", i==cur);
                ry += ROW_H;
            }
            draw_row(OX, ry, OW, "Back", "", cur==back_row);
            display_flush();
            dirty = 0;
        }
        int u=deb_edge(&ku, gpio_read(PIN_UP));
        int d=deb_edge(&kd, gpio_read(PIN_DOWN));
        int a=deb_edge(&ka, gpio_read(PIN_A));
        int b=deb_edge(&kb, gpio_read(PIN_B));

        if (u) { cur=(cur+SAVE_SLOT_COUNT+1-1)%(SAVE_SLOT_COUNT+1); dirty=1; }
        if (d) { cur=(cur+1)%(SAVE_SLOT_COUNT+1);                   dirty=1; }
        if (a) {
            if (cur == back_row) return;
            int rc = for_save ? do_save_state(cur) : do_load_state(cur);
            show_pause_status(rc == 0 ? (for_save ? "Saved!" : "Loaded!")
                                       : (for_save ? "Save failed" : "Load failed"));
            return;
        }
        if (b) return;
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
    debkey_t ku={.held=gpio_read(PIN_UP)}, kd={.held=gpio_read(PIN_DOWN)};
    debkey_t ka={.held=gpio_read(PIN_A)},  kb={.held=gpio_read(PIN_B)};
    debkey_t ks={.held=gpio_read(PIN_SETTINGS)};

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
        int s=deb_edge(&ks, gpio_read(PIN_SETTINGS));
        int u=deb_edge(&ku, gpio_read(PIN_UP));
        int d=deb_edge(&kd, gpio_read(PIN_DOWN));
        int a=deb_edge(&ka, gpio_read(PIN_A));
        int b=deb_edge(&kb, gpio_read(PIN_B));

        if (s) return PM_RESUME;
        if (u) { cur=(cur+PM_COUNT-1)%PM_COUNT; dirty=1; }
        if (d) { cur=(cur+1)%PM_COUNT;           dirty=1; }
        if (a) {
            if (cur==PM_SETTINGS) { show_settings_menu(); dirty=1; continue; }
            if (cur==PM_SAVE_STATE) { show_slot_menu(1); dirty=1; continue; }
            if (cur==PM_LOAD_STATE) { show_slot_menu(0); dirty=1; continue; }
            return cur;
        }
        if (b) return PM_RESUME;
        delay(16667);
    }
}

/* ── Animated boot screen ─────────────────────────────────────────────── */
static void boot_animation(void)
{
    /* CRT warm-up: horizontal line expands from centre outward */
    display_clear(C_BLACK);
    display_flush();

    int cy = g_disp_h / 2;
    for (int i = 1; i <= 10; i++) {
        int half = (g_disp_h / 2) * i / 10;
        display_rect(0, cy - half, g_disp_w, half * 2, C_BLACK);
        display_hline(0, cy - half,     g_disp_w, C_WHITE);
        display_hline(0, cy + half - 1, g_disp_w, C_WHITE);
        display_flush();
        delay(15000);
    }

    /* Boot card */
    display_clear(C_BLACK);
    display_text_large(g_disp_w/2 - 40, g_disp_h/2 - 20, "GB", C_WHITE);
    display_hline(40, g_disp_h/2 + 5, g_disp_w - 80, C_DGRAY);

    /* Loading progress bar */
    const int bx=40, by=g_disp_h/2 + 20, bw=g_disp_w-80, bh=10;
    display_rect_outline(bx-1, by-1, bw+2, bh+2, C_DGRAY);
    display_flush();

    for (int p = 0; p < bw; p += bw / 24 + 1) {
        int pw = p < bw ? p : bw;
        display_rect(bx, by, pw, bh, C_WHITE);
        display_flush();
        delay(10000);
    }
    display_rect(bx, by, bw, bh, C_WHITE);
    display_text(g_disp_w/2 - 44, by + 20, "Loading ROM...", C_DIM);

    /* Scanline wipe overlay */
    display_flush();
    delay(80000);
    for (int y = 0; y < g_disp_h; y += 10) {
        display_hline(0, y, g_disp_w, C_DGRAY);
    }
    display_flush();
    delay(60000);
    display_clear(C_BLACK);
    display_flush();
}

/* ── Main ─────────────────────────────────────────────────────────────── */
int main(void)
{
    init_gpio();
    load_settings();
    init_display_geometry();

    boot_animation();

    gb_init(&gb, rom_data, rom_size);

    draw_border();

    debkey_t k_settings = { .held = gpio_read(PIN_SETTINGS) };
    int frame_count   = 0;
    uint8_t prev_btns = 0;
    printf("[GB] boot OK, rom_size=%u\n", rom_size);

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

        /* ── Per-button diagnostic log (pin -> GB function) ───────────── */
        if (btns != prev_btns) {
            static const struct { uint8_t mask; int pin; const char *name; } BTN_LOG[] = {
                { GB_BTN_RIGHT,  PIN_RIGHT, "RIGHT"  },
                { GB_BTN_LEFT,   PIN_LEFT,  "LEFT"   },
                { GB_BTN_UP,     PIN_UP,    "UP"     },
                { GB_BTN_DOWN,   PIN_DOWN,  "DOWN"   },
                { GB_BTN_A,      PIN_A,     "A"      },
                { GB_BTN_B,      PIN_B,     "B"      },
                { GB_BTN_SELECT, PIN_X,     "SELECT" },
                { GB_BTN_START,  PIN_Y,     "START"  },
            };
            for (int i = 0; i < 8; i++) {
                uint8_t m = BTN_LOG[i].mask;
                if ((btns & m) && !(prev_btns & m))
                    printf("[GB] btn %s down pin=%d\n", BTN_LOG[i].name, BTN_LOG[i].pin);
                else if (!(btns & m) && (prev_btns & m))
                    printf("[GB] btn %s up pin=%d\n", BTN_LOG[i].name, BTN_LOG[i].pin);
            }
            prev_btns = btns;
        }

        /* ── Pause button (debounced rising edge) ─────────────────────── */
        if (deb_edge(&k_settings, gpio_read(PIN_SETTINGS))) {
            int choice = show_pause_menu();
            if (choice == PM_EXIT) {
                display_clear(C_BLACK); display_flush();
                app_switch("akira_shell");
                return 0;
            }
            if (choice == PM_RESTART) {
                gb_init(&gb, rom_data, rom_size);
                frame_count = 0;
                printf("[GB] restarted from pause menu\n");
            }
            draw_border();
            continue;
        }

        /* ── Emulate one GB frame ─────────────────────────────────────── */
        int mod  = FS_MOD[g_frameskip];
        int skip = (frame_count % mod) != 0;
        gb.ppu.skip_render = (uint8_t)skip;
        gb_step_frame(&gb);
        frame_count++;
        { char pcbuf[5]; hex4(gb.cpu.pc, pcbuf);
          printf("[GB] frame=%d pc=0x%s\n", frame_count, pcbuf); }

        if (!skip) {
            render_gb_frame();
        }
    }

    return 0;
}
