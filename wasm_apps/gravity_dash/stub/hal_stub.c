/*
 * Copyright (c) 2025 AkiraOS Contributors
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * @file hal_stub.c
 * @brief Desktop test harness for gravity_dash.
 *        Implements all AkiraOS native API imports using SDL2.
 *        Window is 240×135 scaled 4× (960×540).
 *        Arrow keys LEFT/RIGHT simulate IMU tilt (±0.3 rad).
 *        Compile with: make stub
 *
 * Build dependencies: SDL2 (libsdl2-dev on Ubuntu, sdl2 via Homebrew on macOS)
 */

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <time.h>

#include "../include/game.h"

/* ── Display scale factor ───────────────────────────────────────────────── */
#define SCALE 4

/* ── Internal framebuffer (RGB888 — SDL uses 32-bit surface) ────────────── */
static uint32_t s_framebuf[DISP_W * DISP_H];

/* ── SDL handles ────────────────────────────────────────────────────────── */
static SDL_Window *s_window = NULL;
static SDL_Renderer *s_renderer = NULL;
static SDL_Texture *s_texture = NULL;

/* ── Simulated IMU angle (arrow keys) ──────────────────────────────────── */
static float s_sim_angle = 0.0f;

/* ── Simulated button state ─────────────────────────────────────────────── */
static uint8_t s_buttons = 0;

/* ── Monotonic start time ───────────────────────────────────────────────── */
static uint32_t s_start_ms = 0;

/* ── RGB565 → RGB888 conversion ─────────────────────────────────────────── */
static uint32_t rgb565_to_rgb888(uint16_t c)
{
    uint8_t r;
    uint8_t g;
    uint8_t b;

    r = (uint8_t)(((c >> 11) & 0x1F) << 3);
    g = (uint8_t)(((c >> 5) & 0x3F) << 2);
    b = (uint8_t)(((c >> 0) & 0x1F) << 3);

    return (uint32_t)(0xFF000000u | ((uint32_t)r << 16) |
                      ((uint32_t)g << 8) | b);
}

/* ─────────────────────────────────────────────────────────────────────────
 * AkiraOS display API stubs
 * ───────────────────────────────────────────────────────────────────────── */

void akira_display_fill(uint16_t color)
{
    int i;
    uint32_t c32 = rgb565_to_rgb888(color);

    for (i = 0; i < DISP_W * DISP_H; i++)
    {
        s_framebuf[i] = c32;
    }
}

void akira_display_rect(int x, int y, int w, int h, uint16_t color)
{
    int px;
    int py;
    uint32_t c32;

    c32 = rgb565_to_rgb888(color);

    for (py = y; py < y + h; py++)
    {
        if (py < 0 || py >= DISP_H)
        {
            continue;
        }
        for (px = x; px < x + w; px++)
        {
            if (px < 0 || px >= DISP_W)
            {
                continue;
            }
            s_framebuf[py * DISP_W + px] = c32;
        }
    }
}

void akira_display_pixel(int x, int y, uint16_t color)
{
    if (x < 0 || x >= DISP_W || y < 0 || y >= DISP_H)
    {
        return;
    }
    s_framebuf[y * DISP_W + x] = rgb565_to_rgb888(color);
}

void akira_display_flush(void)
{
    SDL_UpdateTexture(s_texture, NULL, s_framebuf, DISP_W * (int)sizeof(uint32_t));
    SDL_RenderClear(s_renderer);
    SDL_RenderCopy(s_renderer, s_texture, NULL, NULL);
    SDL_RenderPresent(s_renderer);
}

/* ─────────────────────────────────────────────────────────────────────────
 * AkiraOS IMU stub
 * ───────────────────────────────────────────────────────────────────────── */

float akira_imu_gravity_angle(void)
{
    return s_sim_angle;
}

/* ─────────────────────────────────────────────────────────────────────────
 * AkiraOS input stub
 * ───────────────────────────────────────────────────────────────────────── */

uint8_t akira_input_get(void)
{
    return s_buttons;
}

/* ─────────────────────────────────────────────────────────────────────────
 * AkiraOS time stub
 * ───────────────────────────────────────────────────────────────────────── */

uint32_t akira_time_ms(void)
{
    return SDL_GetTicks() - s_start_ms;
}

/* ─────────────────────────────────────────────────────────────────────────
 * AkiraOS audio stub — print to stdout
 * ───────────────────────────────────────────────────────────────────────── */

void akira_audio_beep(uint16_t freq_hz, uint16_t duration_ms)
{
    printf("[AUDIO] beep %u Hz for %u ms\n", (unsigned)freq_hz,
           (unsigned)duration_ms);
}

/* ─────────────────────────────────────────────────────────────────────────
 * main — SDL event loop at fixed 30 FPS
 * ───────────────────────────────────────────────────────────────────────── */

/* app_init / app_tick / app_destroy are defined in the other TUs linked in */
extern void app_init(void);
extern void app_tick(void);
extern void app_destroy(void);

int main(int argc, char *argv[])
{
    SDL_Event evt;
    uint32_t frame_start;
    uint32_t frame_elapsed;
    int running;
    const uint8_t *keys;

    (void)argc;
    (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
    {
        fprintf(stderr, "SDL_Init error: %s\n", SDL_GetError());
        return 1;
    }

    s_window = SDL_CreateWindow(
        "gravity_dash — AkiraOS stub",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        DISP_W * SCALE, DISP_H * SCALE, 0);

    if (!s_window)
    {
        fprintf(stderr, "SDL_CreateWindow error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    s_renderer = SDL_CreateRenderer(s_window, -1, SDL_RENDERER_ACCELERATED);
    if (!s_renderer)
    {
        fprintf(stderr, "SDL_CreateRenderer error: %s\n", SDL_GetError());
        SDL_DestroyWindow(s_window);
        SDL_Quit();
        return 1;
    }

    SDL_RenderSetLogicalSize(s_renderer, DISP_W, DISP_H);

    s_texture = SDL_CreateTexture(s_renderer, SDL_PIXELFORMAT_ARGB8888,
                                  SDL_TEXTUREACCESS_STREAMING,
                                  DISP_W, DISP_H);
    if (!s_texture)
    {
        fprintf(stderr, "SDL_CreateTexture error: %s\n", SDL_GetError());
        SDL_DestroyRenderer(s_renderer);
        SDL_DestroyWindow(s_window);
        SDL_Quit();
        return 1;
    }

    s_start_ms = SDL_GetTicks();

    /* Initialise game */
    app_init();

    running = 1;

    while (running)
    {
        frame_start = SDL_GetTicks();

        /* ── Event handling ── */
        while (SDL_PollEvent(&evt))
        {
            if (evt.type == SDL_QUIT)
            {
                running = 0;
            }
        }

        /* ── Key state → simulate IMU and buttons ── */
        keys = SDL_GetKeyboardState(NULL);
        s_buttons = 0;

        if (keys[SDL_SCANCODE_LEFT])
        {
            s_sim_angle -= 0.3f;
            if (s_sim_angle < -1.5707f)
            {
                s_sim_angle = -1.5707f;
            }
        }
        else if (keys[SDL_SCANCODE_RIGHT])
        {
            s_sim_angle += 0.3f;
            if (s_sim_angle > 1.5707f)
            {
                s_sim_angle = 1.5707f;
            }
        }
        else
        {
            /* Drift back to zero when no key held */
            if (s_sim_angle > 0.01f)
            {
                s_sim_angle -= 0.05f;
            }
            else if (s_sim_angle < -0.01f)
            {
                s_sim_angle += 0.05f;
            }
            else
            {
                s_sim_angle = 0.0f;
            }
        }

        /* Map keyboard to AkiraConsole buttons */
        if (keys[SDL_SCANCODE_Z] || keys[SDL_SCANCODE_RETURN])
        {
            s_buttons |= BTN_A;
        }
        if (keys[SDL_SCANCODE_X])
        {
            s_buttons |= BTN_B;
        }
        if (keys[SDL_SCANCODE_UP])
        {
            s_buttons |= BTN_UP;
        }
        if (keys[SDL_SCANCODE_DOWN])
        {
            s_buttons |= BTN_DOWN;
        }

        /* ── Game tick ── */
        app_tick();

        /* ── Cap to 30 FPS ── */
        frame_elapsed = SDL_GetTicks() - frame_start;
        if (frame_elapsed < FRAME_MS)
        {
            SDL_Delay(FRAME_MS - frame_elapsed);
        }
    }

    app_destroy();

    SDL_DestroyTexture(s_texture);
    SDL_DestroyRenderer(s_renderer);
    SDL_DestroyWindow(s_window);
    SDL_Quit();

    return 0;
}
