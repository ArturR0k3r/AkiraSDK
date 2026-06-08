/*
 * effects.c — Particles, floating hex text, screen flash, glitch displacement.
 */
#include "game.h"
#include "akira_api.h"

/* LCG PRNG (no libc rand) */
static uint32_t rng;
static uint32_t rnd(void) { rng = rng * 1664525u + 1013904223u; return rng; }

static void rng_seed(void) { rng = (uint32_t)g.frame ^ 0xDEADBEEFu; }

/* ─── Particle helpers ─────────────────────────────────────────────── */
static Particle *alloc_particle(void)
{
    for (int i = 0; i < 16; i++)
        if (!g.particles[i].active) return &g.particles[i];
    return 0;
}

static FloatText *alloc_floattext(void)
{
    for (int i = 0; i < 8; i++)
        if (!g.floattexts[i].active) return &g.floattexts[i];
    return 0;
}

/* Build hex string from a 16-bit "address" */
static void make_hex(char *buf, uint16_t v)
{
    static const char hx[] = "0123456789ABCDEF";
    buf[0] = '0'; buf[1] = 'x';
    buf[2] = hx[(v >> 12) & 0xF];
    buf[3] = hx[(v >>  8) & 0xF];
    buf[4] = hx[(v >>  4) & 0xF];
    buf[5] = hx[(v      ) & 0xF];
    buf[6] = '\0';
}

/* ─── Public ───────────────────────────────────────────────────────── */
void effects_emit_collect(int px, int py)
{
    rng_seed();
    /* 6 cyan particles */
    for (int i = 0; i < 6; i++) {
        Particle *p = alloc_particle();
        if (!p) break;
        int sx = (int)(rnd() & 7u) - 3;
        int sy = (int)(rnd() & 7u) - 3;
        p->fpx      = FP(px);
        p->fpy      = FP(py);
        p->vx       = sx * PHYS_FP / 3;
        p->vy       = sy * PHYS_FP / 3;
        p->color    = C_SHARD;
        p->max_life = 12;  /* 400ms ≈ 12 frames */
        p->life     = 12;
        p->active   = 1;
    }
    /* Floating hex text */
    FloatText *ft = alloc_floattext();
    if (ft) {
        uint16_t addr = (uint16_t)(rnd() & 0xFFFFu);
        make_hex(ft->text, addr);
        ft->x       = (int16_t)px;
        ft->fpy     = (int16_t)(py * PHYS_FP);
        ft->color   = C_SHARD;
        ft->max_life = 27;  /* 900ms ≈ 27 frames */
        ft->life    = 27;
        ft->active  = 1;
    }
}

void effects_emit_death(int px, int py)
{
    rng_seed();
    for (int i = 0; i < 8; i++) {
        Particle *p = alloc_particle();
        if (!p) break;
        int sx = (int)(rnd() & 0xFu) - 7;
        int sy = (int)(rnd() & 0xFu) - 7;
        p->fpx      = FP(px);
        p->fpy      = FP(py);
        p->vx       = sx * PHYS_FP / 3;
        p->vy       = sy * PHYS_FP / 3;
        p->color    = 0xFFFF;
        p->max_life = 12;
        p->life     = 12;
        p->active   = 1;
    }
}

void effects_flash(uint16_t color, int frames)
{
    g.flash_color  = color;
    g.flash_frames = (uint8_t)frames;
}

void effects_glitch_fx(void)
{
    rng_seed();
    g.gfx_timer = 4;
    for (int i = 0; i < 3; i++) {
        g.gfx_strips[i][0] = (uint8_t)(rnd() % 128u);   /* y */
        g.gfx_strips[i][1] = (uint8_t)(2u + rnd() % 4u); /* h */
        int off = (int)(rnd() % 13u) + 4;
        g.gfx_strips[i][2] = (uint8_t)((rnd() & 1u) ? off : 256 - off);
    }
}

void effects_update(void)
{
    /* Particles */
    for (int i = 0; i < 16; i++) {
        Particle *p = &g.particles[i];
        if (!p->active) continue;
        p->fpx += p->vx;
        p->fpy += p->vy;
        p->life--;
        if (p->life == 0) p->active = 0;
    }

    /* Floating texts: rise 14px over max_life frames */
    for (int i = 0; i < 8; i++) {
        FloatText *ft = &g.floattexts[i];
        if (!ft->active) continue;
        /* rise step: 14 * PHYS_FP / max_life per frame */
        ft->fpy -= (int16_t)(14 * PHYS_FP / ft->max_life);
        ft->life--;
        if (ft->life == 0) ft->active = 0;
    }

    /* Screen flash */
    if (g.flash_frames > 0) g.flash_frames--;

    /* Glitch FX trigger (avg every 6s = 180 frames) */
    static uint8_t gfx_next = 180;
    if (g.state == STATE_PLAYING) {
        if (gfx_next > 0) gfx_next--;
        else {
            effects_glitch_fx();
            rng_seed();
            gfx_next = (uint8_t)(150u + rnd() % 60u);
        }
    }
    if (g.gfx_timer > 0) g.gfx_timer--;

    /* Glitch ambient sound */
    if (g.in_glitch && (g.frame % 45u) == 0)
        audio_play(SFX_GLITCH_AMB);

    /* Exit pulse sound */
    {
        /* 1.5s ≈ 45 frames */
        if ((g.frame % 45u) == 0 && g.state == STATE_PLAYING) {
            /* Check if exit tile exists */
            for (int r = 0; r < GRID_H; r++)
                for (int c = 0; c < GRID_W; c++)
                    if (g.tilemap[r][c] == T_EXIT)
                        audio_play(SFX_EXIT);
        }
    }
}
