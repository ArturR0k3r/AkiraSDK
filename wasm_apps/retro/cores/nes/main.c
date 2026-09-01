/**
 * @file main.c
 * @brief AkiraOS NES emulator — app entry point (v3)
 *
 * Playdate-inspired UI: animated boot screen, Playdate-style slide-in
 * pause overlay, settings sub-menu with persistent storage via NVS.
 *
 * Persistent settings (key "nes/<name>", requires "settings.*" capability):
 *   nes/frameskip   "0"…"3"  (0=every frame, 1=every other =30fps,
 *                             2=1-in-3 ~20fps, 3=1-in-4 ~15fps).  Default 1.
 *   nes/overscan    "0"/"1"  (1=crop top/bottom 8 lines =224 px).  Default 1.
 *
 * Button mapping (via input_get_buttons(), kernel gpio-keys):
 *   D-pad Up/Down/Left/Right → NES d-pad
 *   A → NES A      physical Y → NES B
 *   physical X → NES Start  physical B → NES Select
 *   Home/OK → Pause overlay (not sent to NES)
 *
 * @license Apache-2.0
 */
#include "akira_api.h"
#include "nes.h"
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

/* ── Overscan ────────────────────────────────────────────────────────── */
#define NES_OVERSCAN   8
#define NES_CROP_H     (NES_H - 2 * NES_OVERSCAN)   /* 224 */

/* ── Render modes (set at startup by init_display_geometry) ──────────── */
#define RENDER_LETTERBOX  0   /* disp_w >= NES_W: centre + black bars     */
#define RENDER_CROP       1   /* 128-255px wide: show centre N pixels      */
#define RENDER_SCALE2     2   /* <128px wide: 2:1 nearest-neighbour scale  */

static int g_disp_w, g_disp_h;
static int g_render_mode;
static int g_nes_dst_x;   /* X on display where NES image starts           */
static int g_nes_dst_y;   /* Y on display where NES image starts           */
static int g_nes_src_x;   /* X in NES fb to start reading (crop only)      */
static int g_nes_draw_w;  /* Pixels written per row                        */

/* ── Buttons (kernel gpio-keys via input_get_buttons()) ───────────────── */
#define BTN_HOME (1u << 1)   /* zephyr,code=1 — no AKIRA_BTN_* for Home/OK */
static int btn_held(uint32_t mask) { return (input_get_buttons() & mask) != 0; }

/* ── Colours (RGB565, byte-swapped for ST7789V SPI) ──────────────────── */
#define C_BLACK   0x0000u
#define C_WHITE   0xFFFFu
#define C_LGRAY   0xC618u
#define C_DGRAY   0x4208u
#define C_DIM     0x528Au   /* mid-tone for secondary text */

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

/* ── Pause-menu items ─────────────────────────────────────────────────── */
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

/* ── Settings-menu items ──────────────────────────────────────────────── */
#define SM_FRAMESKIP  0
#define SM_OVERSCAN   1
#define SM_BACK       2
#define SM_COUNT      3

/* ── Runtime settings ────────────────────────────────────────────────── */
static int g_frameskip = 0;   /* 0 = every frame (max fps) */
static int g_overscan  = 1;   /* 1 = crop 8 top/bottom lines            */

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

/* ── Settings persistence ─────────────────────────────────────────────── */
static void load_settings(void)
{
    char buf[8];
    if (settings_get("nes/frameskip", buf, sizeof(buf)) == 0) {
        g_frameskip = satoi(buf);
        if (g_frameskip < 0) g_frameskip = 0;
        if (g_frameskip > 3) g_frameskip = 3;
    }
    if (settings_get("nes/overscan", buf, sizeof(buf)) == 0)
        g_overscan = satoi(buf) ? 1 : 0;
}
static void save_int(const char *key, int val)
{
    char buf[8];
    sitoa(val, buf, sizeof(buf));
    settings_set(key, buf);
}

/* ── Display geometry init ────────────────────────────────────────────── */
static void init_display_geometry(void)
{
    display_get_size(&g_disp_w, &g_disp_h);

    if (g_disp_w >= NES_W) {
        g_render_mode = RENDER_LETTERBOX;
        g_nes_dst_x   = (g_disp_w - NES_W) / 2;
        g_nes_src_x   = 0;
        g_nes_draw_w  = NES_W;
    } else if (g_disp_w >= NES_W / 2) {
        g_render_mode = RENDER_CROP;
        g_nes_dst_x   = 0;
        g_nes_src_x   = (NES_W - g_disp_w) / 2;
        g_nes_draw_w  = g_disp_w;
    } else {
        g_render_mode = RENDER_SCALE2;
        g_nes_draw_w  = NES_W / 2;
        g_nes_dst_x   = (g_disp_w - g_nes_draw_w) / 2;
        g_nes_src_x   = 0;
    }

    int out_h   = (g_render_mode == RENDER_SCALE2) ? NES_H / 2 : NES_H;
    /* Negative dst_y means the display is shorter than the NES output height:
     * render_nes_frame will use it to skip NES rows from the top and centre
     * the visible window.  Positive dst_y centres NES inside a taller display. */
    g_nes_dst_y = (g_disp_h - out_h) / 2;
}

/* ── Frame render helper ──────────────────────────────────────────────── */
/* fb      : NES RGB565 framebuffer (NES_W pixels wide)
 * src_y   : first NES row to output (e.g. NES_OVERSCAN when overscan on)
 * rows    : number of NES rows to output                                 */
static void render_nes_frame(const uint16_t *fb, int src_y, int rows)
{
    if (g_render_mode == RENDER_LETTERBOX) {
        /* Vertical clip: when display is shorter than NES output, g_nes_dst_y is
         * negative.  Advance the NES source row and shrink the row count so we
         * only write pixels that fit on screen (centred window). */
        int dst_y = g_nes_dst_y + src_y;
        if (dst_y < 0) { src_y += -dst_y; rows -= -dst_y; dst_y = 0; }
        if (dst_y + rows > g_disp_h) rows = g_disp_h - dst_y;
        if (rows <= 0) return;
        display_raw_write(g_nes_dst_x, dst_y,
                          g_nes_draw_w, rows,
                          fb + src_y * NES_W,
                          rows * NES_W * 2);
    } else if (g_render_mode == RENDER_CROP) {
        int dst_y = g_nes_dst_y + src_y;
        if (dst_y < 0) { int skip = -dst_y; src_y += skip; rows -= skip; dst_y = 0; }
        if (dst_y + rows > g_disp_h) rows = g_disp_h - dst_y;
        if (rows <= 0) return;
        static uint16_t s_line[NES_W];
        for (int r = 0; r < rows; r++) {
            const uint16_t *src = fb + (src_y + r) * NES_W + g_nes_src_x;
            __builtin_memcpy(s_line, src, g_nes_draw_w * 2);
            display_raw_write(g_nes_dst_x, dst_y + r,
                              g_nes_draw_w, 1,
                              s_line, g_nes_draw_w * 2);
        }
    } else { /* RENDER_SCALE2: src_y and rows are in NES-pixel space (2× scaled down) */
        int out_y    = src_y / 2;
        int dst_y    = g_nes_dst_y + out_y;
        int out_rows = rows / 2;
        if (dst_y < 0) { int skip = -dst_y; src_y += skip * 2; out_rows -= skip; dst_y = 0; }
        if (dst_y + out_rows > g_disp_h) out_rows = g_disp_h - dst_y;
        if (out_rows <= 0) return;
        static uint16_t s_line[NES_W / 2];
        for (int r = 0; r < out_rows; r++) {
            const uint16_t *src = fb + (src_y + r * 2) * NES_W;
            for (int c = 0; c < g_nes_draw_w; c++)
                s_line[c] = src[c * 2];
            display_raw_write(g_nes_dst_x, dst_y + r,
                              g_nes_draw_w, 1,
                              s_line, g_nes_draw_w * 2);
        }
    }
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
    display_text_large(g_disp_w/2 - 40, g_disp_h/4,      "RETRO", C_WHITE);
    display_text_large(g_disp_w/2 - 24, g_disp_h/4 + 30, "NES",   C_WHITE);
    display_hline(40, g_disp_h/2 - 15, g_disp_w - 80, C_DGRAY);

    /* Loading progress bar */
    const int bx=40, by=g_disp_h/2, bw=g_disp_w-80, bh=10;
    display_rect_outline(bx-1, by-1, bw+2, bh+2, C_DGRAY);
    display_flush();

    for (int p = 0; p < bw; p += bw / 24 + 1) {
        int pw = p < bw ? p : bw;
        display_rect(bx, by, pw, bh, C_WHITE);
        display_flush();
        delay(10000);
    }
    display_rect(bx, by, bw, bh, C_WHITE);
    display_text(MENU_PAD, g_disp_h/2 + 20, "Loading ROM...", C_DIM);

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

/* ── Error screen ─────────────────────────────────────────────────────── */
static void show_error(const char *hdr, const char *msg, const char *hint)
{
    display_clear(C_BLACK);
    display_rect(0, 0, g_disp_w, HDR_H, 0xF800u);  /* red header */
    display_text(MENU_PAD, 4, hdr, C_WHITE);
    int y = HDR_H + 14;
    display_text(MENU_PAD, y, msg, C_WHITE);  y += 20;
    if (hint) display_text(MENU_PAD, y, hint, C_DIM);
    display_hline(0, g_disp_h - ROW_H, g_disp_w, C_DGRAY);
    display_text(MENU_PAD, g_disp_h - ROW_H + (ROW_H-8)/2,
                 "A/B: Return to menu", C_LGRAY);
    display_flush();
    delay(400000);
    while (!btn_held(AKIRA_BTN_A) && !btn_held(AKIRA_BTN_Y) && !btn_held(BTN_HOME))
        delay(16667);
}

/* ── Settings sub-menu ────────────────────────────────────────────────── */
static void show_settings_menu(void)
{
    static const char *FS_LABELS[4] = {
        "Off (60fps)", "Half (30fps)", "1/3 (20fps)", "1/4 (15fps)"
    };
    const int OW = (g_disp_w >= 190) ? 180 : g_disp_w - 4;
    const int OH = HDR_H + SM_COUNT * ROW_H;
    const int OX = (g_disp_w - OW) / 2, OY = (g_disp_h - OH) / 2;

    int cur=0, dirty=1;
    debkey_t ku={.held=btn_held(AKIRA_BTN_UP)},   kd={.held=btn_held(AKIRA_BTN_DOWN)};
    debkey_t ka={.held=btn_held(AKIRA_BTN_A)},    kb={.held=btn_held(AKIRA_BTN_Y)};
    debkey_t kl={.held=btn_held(AKIRA_BTN_LEFT)}, kr={.held=btn_held(AKIRA_BTN_RIGHT)};

    while (1) {
        if (dirty) {
            display_rect(OX-2, OY-2, OW+4, OH+4, C_DGRAY);
            display_rect(OX,   OY,   OW,   OH,   C_WHITE);
            draw_header(OX, OY, OW, "Settings");
            int ry = OY + HDR_H;
            for (int i = 0; i < SM_COUNT; i++) {
                const char *lbl = "", *val = "";
                if      (i == SM_FRAMESKIP) { lbl="Frame Skip"; val=FS_LABELS[g_frameskip]; }
                else if (i == SM_OVERSCAN)  { lbl="Overscan";   val=g_overscan?"On":"Off"; }
                else                        { lbl="Back";       val=""; }
                draw_row(OX, ry, OW, lbl, val, i==cur);
                ry += ROW_H;
            }
            display_flush();
            dirty = 0;
        }
        int u=deb_edge(&ku, btn_held(AKIRA_BTN_UP));
        int d=deb_edge(&kd, btn_held(AKIRA_BTN_DOWN));
        int a=deb_edge(&ka, btn_held(AKIRA_BTN_A));
        int b=deb_edge(&kb, btn_held(AKIRA_BTN_Y));
        int l=deb_edge(&kl, btn_held(AKIRA_BTN_LEFT));
        int r=deb_edge(&kr, btn_held(AKIRA_BTN_RIGHT));

        if (u) { cur=(cur+SM_COUNT-1)%SM_COUNT; dirty=1; }
        if (d) { cur=(cur+1)%SM_COUNT;           dirty=1; }
        if (l||r) {
            int delta = r?1:-1;
            if (cur==SM_FRAMESKIP) { g_frameskip=(g_frameskip+delta+4)%4; save_int("nes/frameskip",g_frameskip); dirty=1; }
            if (cur==SM_OVERSCAN)  { g_overscan^=1; save_int("nes/overscan",g_overscan); dirty=1; }
        }
        if (a) {
            if (cur==SM_FRAMESKIP) { g_frameskip=(g_frameskip+1)%4; save_int("nes/frameskip",g_frameskip); dirty=1; }
            else if (cur==SM_OVERSCAN) { g_overscan^=1; save_int("nes/overscan",g_overscan); dirty=1; }
            else break;
        }
        if (b) break;
        delay(16667);
    }
}

/* ── NES machine (static to avoid stack overflow) ─────────────────────── */
static NES g_nes;

/* ── Pause overlay ────────────────────────────────────────────────────── */
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

/* nes_state_{pack,unpack} only marshal to/from a flat buffer (save_state.c
 * can't include akira_api.h — see its header comment), so the actual
 * storage_* I/O happens here. */
static int do_save_state(int slot)
{
    nes_state_pack(&g_nes);
    int fd = storage_open(save_slot_path(slot), STORAGE_O_WRITE);
    if (fd < 0) return -1;
    int n = storage_write(fd, nes_state_buf(), nes_state_size());
    storage_close(fd);
    return (n == nes_state_size()) ? 0 : -1;
}

static int do_load_state(int slot)
{
    int fd = storage_open(save_slot_path(slot), STORAGE_O_READ);
    if (fd < 0) return -1;
    int n = storage_read(fd, nes_state_buf(), nes_state_size());
    storage_close(fd);
    if (n != nes_state_size()) return -1;
    return nes_state_unpack(&g_nes);
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
    debkey_t ku={.held=btn_held(AKIRA_BTN_UP)}, kd={.held=btn_held(AKIRA_BTN_DOWN)};
    debkey_t ka={.held=btn_held(AKIRA_BTN_A)}, kb={.held=btn_held(AKIRA_BTN_Y)};

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
        int u=deb_edge(&ku, btn_held(AKIRA_BTN_UP));
        int d=deb_edge(&kd, btn_held(AKIRA_BTN_DOWN));
        int a=deb_edge(&ka, btn_held(AKIRA_BTN_A));
        int b=deb_edge(&kb, btn_held(AKIRA_BTN_Y));

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

/* Returns PM_RESUME / PM_RESTART / PM_EXIT */
static int show_pause_menu(void)
{
    while (btn_held(BTN_HOME)) delay(10000);
    delay(40000);

    const int OW = (g_disp_w >= 175) ? 170 : g_disp_w - 4;
    const int OH = HDR_H + PM_COUNT * ROW_H;
    const int OX = (g_disp_w - OW) / 2, OY = (g_disp_h - OH) / 2;

    int cur=0, dirty=1;
    debkey_t ku={.held=btn_held(AKIRA_BTN_UP)}, kd={.held=btn_held(AKIRA_BTN_DOWN)};
    debkey_t ka={.held=btn_held(AKIRA_BTN_A)},  kb={.held=btn_held(AKIRA_BTN_Y)};
    debkey_t ks={.held=btn_held(BTN_HOME)};

    while (1) {
        if (dirty) {
            /* Slide-in effect: draw overlay from left edge */
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
        int s=deb_edge(&ks, btn_held(BTN_HOME));
        int u=deb_edge(&ku, btn_held(AKIRA_BTN_UP));
        int d=deb_edge(&kd, btn_held(AKIRA_BTN_DOWN));
        int a=deb_edge(&ka, btn_held(AKIRA_BTN_A));
        int b=deb_edge(&kb, btn_held(AKIRA_BTN_Y));

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

/* ── Frameskip modulus per setting [0..3] ─────────────────────────────── */
static const int FS_MOD[4] = {1, 2, 3, 4};

/* ── Main ─────────────────────────────────────────────────────────────── */
int main(void)
{
    load_settings();
    init_display_geometry();
    boot_animation();

    if (nes_init(&g_nes, rom_data, rom_size) != 0) {
        show_error("Unsupported Mapper",
                   "Supported: 0,1,2,3,4,7,9,66,206",
                   "Contribute at github.com/AkiraOS");
        app_switch("akira_shell");
        return 1;
    }

    /* Black side-bars (letterbox only) and overscan bars.
     * g_nes_dst_y may be negative when the display is shorter than the NES
     * output (vertical crop mode), so guard every y-coordinate before drawing. */
    if (g_render_mode == RENDER_LETTERBOX && g_nes_dst_x > 0) {
        display_rect(0,                     0, g_nes_dst_x, g_disp_h, C_BLACK);
        display_rect(g_nes_dst_x + NES_W,   0, g_nes_dst_x, g_disp_h, C_BLACK);
    }
    if (g_overscan) {
        int ov_top = g_nes_dst_y;
        int ov_bot = g_nes_dst_y + NES_H - NES_OVERSCAN;
        if (ov_top >= 0 && ov_top < g_disp_h)
            display_rect(0, ov_top, g_disp_w, NES_OVERSCAN, C_BLACK);
        if (ov_bot >= 0 && ov_bot < g_disp_h)
            display_rect(0, ov_bot, g_disp_w, NES_OVERSCAN, C_BLACK);
    }
    if (g_nes_dst_y > 0)
        display_rect(0, 0, g_disp_w, g_nes_dst_y, C_BLACK);
    display_flush();

    debkey_t k_settings = { .held = btn_held(BTN_HOME) };
    int frame_count   = 0;

    while (1) {
        /* ── Buttons ──────────────────────────────────────────────── */
        uint8_t btns = 0;
        if (btn_held(AKIRA_BTN_RIGHT))  btns |= NES_BTN_RIGHT;
        if (btn_held(AKIRA_BTN_LEFT))   btns |= NES_BTN_LEFT;
        if (btn_held(AKIRA_BTN_DOWN))   btns |= NES_BTN_DOWN;
        if (btn_held(AKIRA_BTN_UP))     btns |= NES_BTN_UP;
        if (btn_held(AKIRA_BTN_A))      btns |= NES_BTN_A;
        if (btn_held(AKIRA_BTN_Y))      btns |= NES_BTN_B;
        if (btn_held(AKIRA_BTN_B))      btns |= NES_BTN_SELECT;
        if (btn_held(AKIRA_BTN_X))      btns |= NES_BTN_START;
        nes_set_controller(&g_nes, 0, btns);

        /* ── Settings button (debounced rising edge) ──────────────── */
        if (deb_edge(&k_settings, btn_held(BTN_HOME))) {
            /* Snapshot the current NES frame into the OS framebuffer so the
             * pause overlay has a correct freeze-frame background.         */
            /* Re-use render_nes_frame so the snapshot respects display geometry
             * (letterbox centering, crop, scale2, and vertical clip on small
             * displays) instead of blitting the full NES framebuffer blindly. */
            if (g_overscan)
                render_nes_frame((const uint16_t *)g_nes.fb, NES_OVERSCAN, NES_CROP_H);
            else
                render_nes_frame((const uint16_t *)g_nes.fb, 0, NES_H);
            display_flush();
            int choice = show_pause_menu();
            if (choice == PM_EXIT) {
                /* Fade to black */
                for (int i = 0; i < 4; i++) { display_clear(C_BLACK); display_flush(); delay(25000); }
                app_switch("akira_shell");
                return 0;
            }
            if (choice == PM_RESTART) {
                nes_init(&g_nes, rom_data, rom_size);
                if (g_render_mode == RENDER_LETTERBOX && g_nes_dst_x > 0) {
                    display_rect(0,                   0, g_nes_dst_x, g_disp_h, C_BLACK);
                    display_rect(g_nes_dst_x + NES_W, 0, g_nes_dst_x, g_disp_h, C_BLACK);
                }
                if (g_overscan) {
                    int ov_top = g_nes_dst_y;
                    int ov_bot = g_nes_dst_y + NES_H - NES_OVERSCAN;
                    if (ov_top >= 0 && ov_top < g_disp_h)
                        display_rect(0, ov_top, g_disp_w, NES_OVERSCAN, C_BLACK);
                    if (ov_bot >= 0 && ov_bot < g_disp_h)
                        display_rect(0, ov_bot, g_disp_w, NES_OVERSCAN, C_BLACK);
                }
                display_flush();
                frame_count = 0;
            }
            continue;
        }

        /* ── Emulate one NES frame ────────────────────────────────── */
        int mod  = FS_MOD[g_frameskip];
        int skip = (frame_count % mod) != 0;
        g_nes.ppu.skip_render = (uint8_t)skip;
        nes_step_frame(&g_nes);
        frame_count++;

        /* ── Push rendered frame to display ──────────────────────── */
        if (!skip) {
            if (g_overscan)
                render_nes_frame((const uint16_t *)g_nes.fb, NES_OVERSCAN, NES_CROP_H);
            else
                render_nes_frame((const uint16_t *)g_nes.fb, 0, NES_H);
        }
    }

    return 0;
}
