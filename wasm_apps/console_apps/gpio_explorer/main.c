/**
 * @file gpio_explorer.c
 * @brief GPIO toggler / pin explorer — a bench tool for AkiraOS
 *
 * Scroll the ESP32-S3 GPIO map, watch input levels live, and drive outputs.
 *
 * Controls (AkiraConsole):
 *   UP/DOWN   = scroll the pin list
 *   A         = cycle the selected pin's mode (IN / IN-PU / IN-PD / OUT)
 *   B         = toggle output level (only when the pin is an output)
 *   SETTINGS  = exit to launcher
 *
 * Safety: pins that would brick the device or lose input control are LOCKED
 * and never (re)configured — SPI flash / octal-PSRAM / USB / UART-console and
 * the console's own button GPIOs. The tool only drives genuinely free pins.
 *
 * @copyright Copyright (c) 2026 PenEngineering S.R.L
 * @license Apache-2.0
 */

#include "akira_api.h"

static int32_t SCR_W = 320;
static int32_t SCR_H = 240;

/* ── Buttons (read-only here; never reconfigured) ───────────────────────── */
#define BTN_UP 4
#define BTN_DOWN 5
#define BTN_LEFT 6
#define BTN_RIGHT 7
#define BTN_A 15
#define BTN_B 16
#define BTN_SETTINGS 0

/* ── Palette ────────────────────────────────────────────────────────────── */
#define COL_BG 0x0000
#define COL_HEADER_BG 0x018A /* teal bar              */
#define COL_TITLE 0x07FF
#define COL_LABEL 0x8410
#define COL_VALUE 0xFFFF
#define COL_SEL_BG 0x001F   /* blue selection        */
#define COL_HI 0x07E0       /* green = logic high     */
#define COL_LO 0x4208       /* dim grey = logic low   */
#define COL_OUT 0xFD20      /* orange = output pin    */
#define COL_LOCK 0xF800     /* red = locked           */
#define COL_HINT 0x4208

#define N_PINS 49 /* GPIO 0..48 */

/* Per-pin mode */
#define M_IN 0
#define M_IN_PU 1
#define M_IN_PD 2
#define M_OUT 3
static const char *MODE_NAME[4] = {"IN   ", "IN-PU", "IN-PD", "OUT  "};

static uint8_t pin_mode[N_PINS];
static uint8_t pin_out[N_PINS];    /* last driven level for outputs           */
static uint8_t configured[N_PINS]; /* 1 once the user has touched this pin     */

/* Pins that must never be reconfigured. */
static int is_locked(int p)
{
    switch (p)
    {
    /* ESP32-S3 SPI0 flash */
    case 26: case 27: case 28: case 29: case 30: case 31: case 32:
    /* Octal PSRAM (WROOM-N16R8) */
    case 33: case 34: case 35: case 36: case 37:
    /* USB D-/D+ */
    case 19: case 20:
    /* UART0 console */
    case 43: case 44:
    /* These GPIO numbers do not exist on the S3 */
    case 22: case 23: case 24: case 25:
    /* Console button GPIOs — locking these keeps our own controls alive */
    case BTN_SETTINGS: case BTN_UP: case BTN_DOWN: case BTN_LEFT:
    case BTN_RIGHT: case BTN_A: case BTN_B: case 17: case 41:
        return 1;
    default:
        return 0;
    }
}

/* ── no-libc int→str ────────────────────────────────────────────────────── */
static void u2s(uint32_t v, char *b)
{
    char t[12]; int n = 0, p = 0;
    if (!v) t[n++] = '0';
    while (v) { t[n++] = (char)('0' + v % 10); v /= 10; }
    while (n) b[p++] = t[--n];
    b[p] = '\0';
}

/* ── Apply a pin's current mode to hardware ─────────────────────────────── */
static void apply_pin(int p)
{
    if (is_locked(p)) return;
    switch (pin_mode[p])
    {
    case M_IN:    gpio_configure(p, GPIO_INPUT); break;
    case M_IN_PU: gpio_configure(p, GPIO_INPUT | GPIO_PULL_UP); break;
    case M_IN_PD: gpio_configure(p, GPIO_INPUT | GPIO_PULL_DOWN); break;
    case M_OUT:
        gpio_configure(p, GPIO_OUTPUT);
        gpio_write(p, pin_out[p]);
        break;
    }
}

/* ── Live level for display (-1 locked, -2 not yet configured) ──────────── */
static int pin_level(int p)
{
    if (is_locked(p)) return -1;
    if (!configured[p]) return -2; /* untouched — don't poke the pin */
    if (pin_mode[p] == M_OUT) return pin_out[p];
    return gpio_read(p) ? 1 : 0;
}

#define ROW_H 12
#define LIST_Y 24

static int visible_rows(void) { return (SCR_H - LIST_Y - 16) / ROW_H; }

static void draw(int sel, int top)
{
    display_clear(COL_BG);

    /* Header */
    display_rect(0, 0, SCR_W, 20, COL_HEADER_BG);
    display_text(4, 6, "GPIO EXPLORER", COL_TITLE);

    int rows = visible_rows();
    char b[8];
    for (int i = 0; i < rows; i++)
    {
        int p = top + i;
        if (p >= N_PINS) break;
        int y = LIST_Y + i * ROW_H;

        if (p == sel)
            display_rect(0, y - 1, SCR_W, ROW_H, COL_SEL_BG);

        /* "GPIOnn" */
        display_text(6, y, "GPIO", COL_LABEL);
        u2s((uint32_t)p, b);
        display_text(40, y, b, COL_VALUE);

        if (is_locked(p))
        {
            display_text(90, y, "LOCKED", COL_LOCK);
            continue;
        }

        int lv = pin_level(p);

        if (lv == -2)
        {
            /* Untouched: no mode applied, pin not read. */
            display_text(90, y, "----", COL_HINT);
            display_text(160, y, "A to probe", COL_HINT);
            continue;
        }

        /* Mode */
        uint32_t mc = (pin_mode[p] == M_OUT) ? COL_OUT : COL_LABEL;
        display_text(90, y, MODE_NAME[pin_mode[p]], mc);

        /* Level */
        display_text(160, y, lv ? "HIGH" : "LOW", lv ? COL_HI : COL_LO);

        /* Level bar */
        display_rect(210, y + 1, 90, 7, 0x1082);
        if (lv) display_rect(210, y + 1, 90, 7, COL_HI);
    }

    /* Footer hint */
    display_text(4, SCR_H - 12,
                 "A:mode  B:toggle  OK:exit", COL_HINT);
    display_flush();
}

int main(void)
{
    printf("AkiraOS GPIO Explorer");
    display_get_size(&SCR_W, &SCR_H);

    /* Buttons as inputs (sibling-app polarity). */
    gpio_configure(BTN_UP, GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);
    gpio_configure(BTN_DOWN, GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);
    gpio_configure(BTN_LEFT, GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);
    gpio_configure(BTN_RIGHT, GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);
    gpio_configure(BTN_A, GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);
    gpio_configure(BTN_B, GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_SETTINGS, GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);

    /* Do NOT configure anything up front — a pin may currently be muxed to a
     * peripheral (display SPI, backlight I2C, ...). We only touch a pin once
     * the user deliberately probes it with A. */
    for (int p = 0; p < N_PINS; p++)
    {
        pin_mode[p] = M_IN;
        pin_out[p] = 0;
        configured[p] = 0;
    }

    int sel = 1, top = 0;
    int ps = 0, pu = 0, pd = 0, pa = 0, pb = 0;

    while (1)
    {
        int s = gpio_read(BTN_SETTINGS);
        int u = gpio_read(BTN_UP);
        int d = gpio_read(BTN_DOWN);
        int a = gpio_read(BTN_A);
        int b = gpio_read(BTN_B);

        if (s && !ps)
        {
            app_switch("supervisor");
            return 0;
        }
        if (u && !pu) { if (sel > 0) sel--; }
        if (d && !pd) { if (sel < N_PINS - 1) sel++; }

        /* A: first press probes the pin as an input; later presses cycle the
         * mode (IN / IN-PU / IN-PD / OUT). */
        if (a && !pa && !is_locked(sel))
        {
            if (!configured[sel])
            {
                configured[sel] = 1;
                pin_mode[sel] = M_IN;
            }
            else
            {
                pin_mode[sel] = (uint8_t)((pin_mode[sel] + 1) & 3);
            }
            apply_pin(sel);
        }
        /* B: toggle output level */
        if (b && !pb && !is_locked(sel) && configured[sel] && pin_mode[sel] == M_OUT)
        {
            pin_out[sel] ^= 1;
            gpio_write(sel, pin_out[sel]);
        }

        ps = s; pu = u; pd = d; pa = a; pb = b;

        /* Keep the selection on screen */
        int rows = visible_rows();
        if (sel < top) top = sel;
        if (sel >= top + rows) top = sel - rows + 1;

        draw(sel, top);
        delay(40000);
    }
}
