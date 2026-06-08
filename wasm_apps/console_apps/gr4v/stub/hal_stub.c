/*
 * hal_stub.c — SDL2 desktop test harness for GR4V (4× scale, 960×540).
 * Implements the AkiraOS display/input/sensor/PWM API stubs.
 *
 * Build: see Makefile target 'desktop'
 *
 * Controls:
 *   Left/Right arrows: simulate ±30° tilt
 *   Up/Down arrows: tilt ±90°
 *   Z key: BTN_A (start/confirm)
 *   X key: BTN_B
 */
#ifdef GAME_STUB

#include <SDL2/SDL.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
/* Define button bitmasks before game.h so they're available without akira_api.h */
#define AKIRA_BTN_A  (1U << 6)
#define AKIRA_BTN_B  (1U << 7)
#define AKIRA_BTN_UP (1U << 2)
/* Stub out akira_api.h — we implement the API ourselves below */
#define AKIRA_API_H
#define AKIRA_CONSOLE_H
#include "game.h"

#define SCALE 4
#define WIN_W (SCR_W * SCALE)
#define WIN_H (SCR_H * SCALE)

/* Framebuffer (RGB565) */
static uint16_t fb[SCR_H][SCR_W];

/* Tilt simulation state */
static int32_t sim_tilt_x = 0;   /* simulated accel X × 1000 */
static int32_t sim_tilt_y = 9800;
static uint32_t sim_buttons = 0;

/* SDL state */
static SDL_Window   *win;
static SDL_Renderer *ren;
static SDL_Texture  *tex;

/* ─── Display API stubs ──────────────────────────────────────────────── */
int display_pixel(int32_t x, int32_t y, uint32_t color)
{
    if (x < 0 || x >= SCR_W || y < 0 || y >= SCR_H) return 0;
    fb[y][x] = (uint16_t)color;
    return 0;
}

int display_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color)
{
    for (int32_t dy = 0; dy < h; dy++)
        for (int32_t dx = 0; dx < w; dx++)
            display_pixel(x + dx, y + dy, color);
    return 0;
}

int display_rect_outline(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color)
{
    for (int32_t dx = 0; dx < w; dx++) {
        display_pixel(x + dx, y, color);
        display_pixel(x + dx, y + h - 1, color);
    }
    for (int32_t dy = 1; dy < h - 1; dy++) {
        display_pixel(x, y + dy, color);
        display_pixel(x + w - 1, y + dy, color);
    }
    return 0;
}

int display_hline(int32_t x, int32_t y, int32_t len, uint32_t color)
{
    for (int32_t i = 0; i < len; i++) display_pixel(x + i, y, color);
    return 0;
}

int display_vline(int32_t x, int32_t y, int32_t len, uint32_t color)
{
    for (int32_t i = 0; i < len; i++) display_pixel(x, y + i, color);
    return 0;
}

int display_clear(uint32_t color)
{
    for (int y = 0; y < SCR_H; y++)
        for (int x = 0; x < SCR_W; x++)
            fb[y][x] = (uint16_t)color;
    return 0;
}

/* Bresenham line */
int display_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color)
{
    int32_t dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int32_t dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int32_t err = dx + dy;
    while (1) {
        display_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int32_t e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
    return 0;
}

int display_circle(int32_t cx, int32_t cy, int32_t r, uint32_t color)
{
    int32_t x = r, y = 0, err = 0;
    while (x >= y) {
        display_pixel(cx+x, cy+y, color); display_pixel(cx+y, cy+x, color);
        display_pixel(cx-y, cy+x, color); display_pixel(cx-x, cy+y, color);
        display_pixel(cx-x, cy-y, color); display_pixel(cx-y, cy-x, color);
        display_pixel(cx+y, cy-x, color); display_pixel(cx+x, cy-y, color);
        y++; err += 1 + 2*y;
        if (2*(err-x) + 1 > 0) { x--; err += 1 - 2*x; }
    }
    return 0;
}

int display_circle_fill(int32_t cx, int32_t cy, int32_t r, uint32_t color)
{
    for (int32_t dy = -r; dy <= r; dy++)
        for (int32_t dx = -r; dx <= r; dx++)
            if (dx*dx + dy*dy <= r*r)
                display_pixel(cx+dx, cy+dy, color);
    return 0;
}

/* Filled triangle (simple scanline) */
int display_triangle_fill(int32_t x0,int32_t y0, int32_t x1,int32_t y1,
                          int32_t x2,int32_t y2, uint32_t color)
{
    /* Sort by y */
    if (y0 > y1) { int32_t t; t=y0;y0=y1;y1=t; t=x0;x0=x1;x1=t; }
    if (y0 > y2) { int32_t t; t=y0;y0=y2;y2=t; t=x0;x0=x2;x2=t; }
    if (y1 > y2) { int32_t t; t=y1;y1=y2;y2=t; t=x1;x1=x2;x2=t; }
    for (int32_t y = y0; y <= y2; y++) {
        int32_t xa, xb;
        if (y < y1) {
            xa = (y1==y0) ? x0 : x0 + (x1-x0)*(y-y0)/(y1-y0);
            xb = (y2==y0) ? x0 : x0 + (x2-x0)*(y-y0)/(y2-y0);
        } else {
            xa = (y2==y1) ? x1 : x1 + (x2-x1)*(y-y1)/(y2-y1);
            xb = (y2==y0) ? x0 : x0 + (x2-x0)*(y-y0)/(y2-y0);
        }
        if (xa > xb) { int32_t t=xa; xa=xb; xb=t; }
        display_hline(xa, y, xb-xa+1, color);
    }
    return 0;
}

int display_triangle(int32_t x0,int32_t y0, int32_t x1,int32_t y1,
                     int32_t x2,int32_t y2, uint32_t color)
{
    display_line(x0,y0,x1,y1,color);
    display_line(x1,y1,x2,y2,color);
    display_line(x2,y2,x0,y0,color);
    return 0;
}

int display_flush(void)
{
    /* Convert RGB565 framebuffer to ARGB8888 texture */
    uint32_t pixels[SCR_H * SCR_W];
    for (int y = 0; y < SCR_H; y++) {
        for (int x = 0; x < SCR_W; x++) {
            uint16_t c = fb[y][x];
            uint8_t r5 = (c >> 11) & 0x1F;
            uint8_t g6 = (c >> 5) & 0x3F;
            uint8_t b5 = c & 0x1F;
            uint8_t r = (uint8_t)((r5 * 255) / 31);
            uint8_t gv = (uint8_t)((g6 * 255) / 63);
            uint8_t b = (uint8_t)((b5 * 255) / 31);
            pixels[y * SCR_W + x] = (0xFFu << 24) | ((uint32_t)r << 16) |
                                    ((uint32_t)gv << 8) | b;
        }
    }
    SDL_UpdateTexture(tex, NULL, pixels, SCR_W * (int)sizeof(uint32_t));
    SDL_RenderCopy(ren, tex, NULL, NULL);
    SDL_RenderPresent(ren);
    return 0;
}

int display_get_size(int32_t *w, int32_t *h) { *w = SCR_W; *h = SCR_H; return 0; }
int display_get_width(void)  { return SCR_W; }
int display_get_height(void) { return SCR_H; }

/* ─── GPIO API stubs ─────────────────────────────────────────────────── */
static int sim_gpio15 = 0;  /* A button state (1 = pressed) */

int gpio_configure(int32_t pin, uint32_t flags)
{
    (void)pin; (void)flags;
    return 0;
}

int gpio_read(int32_t pin)
{
    if (pin == 15) return sim_gpio15;
    return 0;
}

/* ─── Input API stubs ────────────────────────────────────────────────── */
int input_get_buttons(void)
{
    SDL_Event ev;
    sim_buttons = 0;
    sim_gpio15 = 0;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) exit(0);
        if (ev.type == SDL_KEYDOWN) {
            switch (ev.key.keysym.sym) {
            case SDLK_z: sim_buttons |= AKIRA_BTN_A; sim_gpio15 = 1; break;
            case SDLK_x: sim_buttons |= AKIRA_BTN_B; break;
            case SDLK_LEFT:  sim_tilt_x = -2900; break;
            case SDLK_RIGHT: sim_tilt_x =  2900; break;
            case SDLK_UP:    sim_tilt_y = -9800; break;
            case SDLK_DOWN:  sim_tilt_y =  9800; break;
            case SDLK_ESCAPE: exit(0); break;
            default: break;
            }
        }
        if (ev.type == SDL_KEYUP) {
            switch (ev.key.keysym.sym) {
            case SDLK_LEFT: case SDLK_RIGHT: sim_tilt_x = 0; break;
            case SDLK_UP:   case SDLK_DOWN:  sim_tilt_y = 9800; break;
            default: break;
            }
        }
    }
    return (int)sim_buttons;
}

int input_poll_event(uint8_t *buf, uint32_t len)
{
    (void)buf; (void)len;
    return 0;
}

/* ─── Sensor API stubs ───────────────────────────────────────────────── */
int32_t sensor_read(int32_t channel)
{
    switch (channel) {
    case 0: return sim_tilt_x;   /* ACCEL_X */
    case 1: return sim_tilt_y;   /* ACCEL_Y */
    case 2: return 0;            /* ACCEL_Z */
    default: return 0;
    }
}

/* ─── PWM API stubs ──────────────────────────────────────────────────── */
int pwm_set(int32_t ch, int32_t freq, int32_t duty)
{
    (void)ch; (void)freq; (void)duty;
    /* SDL_mixer would go here for audio */
    return 0;
}
int pwm_disable(int32_t ch) { (void)ch; return 0; }

/* ─── Misc API stubs ─────────────────────────────────────────────────── */
void delay(int32_t us)
{
    SDL_Delay((uint32_t)(us / 1000));
}

int printf_native(const char *fmt, ...)
{
    (void)fmt;
    return 0;
}

/* ─── SDL2 main ──────────────────────────────────────────────────────── */
int main(void)
{
    SDL_Init(SDL_INIT_VIDEO);
    win = SDL_CreateWindow("GR4V — AkiraConsole Desktop",
                           SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           WIN_W, WIN_H, 0);
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                            SDL_TEXTUREACCESS_STREAMING, SCR_W, SCR_H);
    SDL_RenderSetLogicalSize(ren, WIN_W, WIN_H);

    memset(&g, 0, sizeof(g));
    audio_init();
    renderer_init();
    display_clear(C_BG);
    g.state = STATE_TITLE;

    while (1) {
        g.frame++;
        switch (g.state) {
        case STATE_TITLE:          update_title();         renderer_draw_title();          break;
        case STATE_PLAYING:        update_playing();       renderer_draw_frame(); display_flush(); break;
        case STATE_DEAD:           update_dead();          renderer_draw_dead();           break;
        case STATE_LEVEL_COMPLETE: update_level_complete();renderer_draw_level_complete(); break;
        case STATE_GAME_COMPLETE:  update_game_complete(); renderer_draw_game_complete();  break;
        }
        SDL_Delay(33);
    }
    return 0;
}
#endif /* GAME_STUB */
