/*
 * game.h — GR4V: Cat-Skull WASM game for AkiraConsole
 * Shared types, constants, tile IDs, module interfaces.
 */
#ifndef GAME_H
#define GAME_H

#include <stdint.h>

/* ─── Display ───────────────────────────────────────────────────────── */
#define SCR_W     240
#define SCR_H     135
#define PLAY_H    128
#define HUD_Y     128
#define HUD_H       7

/* ─── Tile grid ─────────────────────────────────────────────────────── */
#define TILE_W     16
#define TILE_H     16
#define GRID_W     15
#define GRID_H      8
#define TILE_COUNT (GRID_W * GRID_H)

/* ─── Tile IDs ──────────────────────────────────────────────────────── */
#define T_AIR      0
#define T_SOLID    1
#define T_SHARD    2
#define T_EXIT     3
#define T_SPIKE    4
#define T_DISSOLVE 5
#define T_ANCHOR   6
#define T_VOID     7
#define T_GLITCH   8

/* ─── Colors (RGB565) ───────────────────────────────────────────────── */
#define C_BG       0x0801u
#define C_PLAT     0x0813u
#define C_EDGE     0x780Fu
#define C_TRACE    0x0229u
#define C_SHARD    0x07FFu
#define C_SPIKE    0xF940u
#define C_GLITCH   0xF81Fu
#define C_ANCHOR   0x07E8u
#define C_CHAR     0xFFFFu
#define C_HUD_TXT  0xDEDBu
#define C_PALE_VIO 0xC81Fu
#define C_STAR1    0xFFFFu
#define C_STAR2    0x8410u
#define C_HUD_DIM  0x4444u
#define C_HUD_DOT  0x780Fu
#define C_VOID_B   0x0813u
#define C_HUD_BG   0x0000u

/* ─── Physics (24.8 fixed-point: values × 256) ──────────────────────── */
#define PHYS_FP       256
/* G=980 px/s², dt=1/30s → 32.67 px/frame → ×256 = 8363              */
#define PHYS_DV       8363
/* Q14 sin table shift */
#define PHYS_Q14      14
/* Max velocity: 280 px/s → 9.33 px/frame → ×256 = 2390              */
#define PHYS_MAX_V    2390
/* Friction: 0.988/frame */
#define PHYS_FRIC     988
/* Gravity angle deadzone: 0.04 rad ≈ 2 steps in 64-step space */
#define PHYS_DEADZONE  2

/* ─── Player size ───────────────────────────────────────────────────── */
#define PLAYER_W  10
#define PLAYER_H  12

/* ─── Dissolve timing (frames @ 30fps) ─────────────────────────────── */
#define DSV_TOTAL    75   /* 2.5 s total stand */
#define DSV_WARN1    45   /* 1.5 s edge shift  */
#define DSV_WARN2    60   /* 2.0 s flicker     */
#define DSV_FRAG     69   /* 2.3 s fragment    */
#define DSV_RESPAWN 150   /* 5.0 s respawn     */

/* ─── Glitch zone ───────────────────────────────────────────────────── */
#define GLITCH_SURGE_STEPS  4   /* ±4 angle steps ≈ ±22.5° ≈ ±0.39 rad */
#define GLITCH_SURGE_FRAMES 12  /* 0.4 s                                 */

/* ─── Anchor ────────────────────────────────────────────────────────── */
#define ANCHOR_FRAMES 90  /* 3 s */

/* ─── Frame rate ────────────────────────────────────────────────────── */
#define FRAME_US 33333    /* ~30 fps */

/* ─── Levels ────────────────────────────────────────────────────────── */
#define NUM_LEVELS  3
#define SHARD_GOAL  8

/* ─── States ────────────────────────────────────────────────────────── */
typedef enum {
    STATE_TITLE = 0,
    STATE_PLAYING,
    STATE_DEAD,
    STATE_LEVEL_COMPLETE,
    STATE_GAME_COMPLETE,
} GameState;

/* ─── Dissolve tile runtime state ───────────────────────────────────── */
typedef struct {
    uint8_t  stand;    /* frames player has stood on it */
    uint8_t  gone;     /* 1 when collapsed              */
    uint16_t respawn;  /* countdown to respawn          */
} DissolveTile;

/* ─── Particle ──────────────────────────────────────────────────────── */
typedef struct {
    int32_t  fpx, fpy;  /* 24.8 fixed-point position */
    int32_t  vx, vy;    /* fp/frame velocity         */
    uint16_t color;
    uint8_t  life;
    uint8_t  max_life;
    uint8_t  active;
} Particle;

/* ─── Floating text ─────────────────────────────────────────────────── */
typedef struct {
    int16_t  x;
    int16_t  fpy;    /* 24.8 fixed-point y (starts at pixel_y*256) */
    uint8_t  life;
    uint8_t  max_life;
    uint8_t  active;
    uint16_t color;
    char     text[10];
} FloatText;

/* ─── Audio tone entry ──────────────────────────────────────────────── */
typedef struct {
    uint16_t freq;    /* Hz; 0 = silent gap */
    uint8_t  frames;  /* duration in frames */
} AudioTone;

/* ─── Player ────────────────────────────────────────────────────────── */
typedef struct {
    int32_t fpx, fpy;     /* 24.8 position                    */
    int32_t vx, vy;       /* 24.8 fp velocity per frame       */
    uint8_t walk_frame;   /* 0 or 1                           */
    uint8_t walk_timer;   /* counts to 8                      */
    uint8_t facing_right;
    uint8_t spawn_col;
    uint8_t spawn_row;
} Player;

/* ─── Main game context ─────────────────────────────────────────────── */
typedef struct {
    GameState state;

    uint8_t      level;
    uint8_t      shards_collected;
    uint8_t      tilemap[GRID_H][GRID_W];
    uint8_t      dirty[TILE_COUNT];
    DissolveTile dissolve[TILE_COUNT];
    uint8_t      shard_gone[TILE_COUNT];   /* 1 = collected */
    uint8_t      anchor_gone[TILE_COUNT];  /* 1 = collected */

    Player       player;

    int32_t      angle_idx;     /* 0..63 current gravity angle     */
    uint8_t      anchor_active;
    uint16_t     anchor_frames;

    uint8_t      in_glitch;
    int16_t      glitch_timer;
    int16_t      glitch_surge;  /* remaining surge frames          */
    int32_t      glitch_off;    /* angle offset (steps)            */

    Particle     particles[16];
    FloatText    floattexts[8];
    uint8_t      flash_frames;
    uint16_t     flash_color;
    uint8_t      gfx_timer;
    uint8_t      gfx_strips[3][3]; /* y, h, x-offset per strip    */

    AudioTone    aq[16];        /* audio queue ring buffer         */
    uint8_t      aq_head;
    uint8_t      aq_tail;
    uint16_t     audio_timer;   /* frames left for current tone    */

    uint16_t     stars1[12][2];
    uint16_t     stars2[8][2];
    uint32_t     frame;
    uint16_t     state_timer;
    uint16_t     title_frame;
} Game;

/* ─── Q14 sin/cos table (64 entries, 5.625°/step) ───────────────────── */
extern const int16_t sin64[64];
#define SIN64(i) sin64[(unsigned)(i) & 63u]
#define COS64(i) sin64[((unsigned)(i) + 16u) & 63u]

/* ─── Level data ────────────────────────────────────────────────────── */
extern const uint8_t  level_data[NUM_LEVELS][GRID_H][GRID_W];
extern const uint8_t  level_spawn_col[NUM_LEVELS];
extern const uint8_t  level_spawn_row[NUM_LEVELS];
extern const char    *level_name[NUM_LEVELS];

/* ─── Font (36 chars: 0-9, A-Z; 4 wide × 5 tall; 4 low bits/row) ──── */
extern const uint8_t font_data[36][5];

/* ─── Sprite bitmasks (10 wide × 12 tall; bit9 = leftmost) ─────────── */
extern const uint16_t sprite_a[12];
extern const uint16_t sprite_b[12];

/* ─── Global game state ─────────────────────────────────────────────── */
extern Game g;

/* ─── Module interfaces ─────────────────────────────────────────────── */
void physics_init(void);

/* main.c update functions (visible to stub main) */
void update_title(void);
void update_playing(void);
void update_dead(void);
void update_level_complete(void);
void update_game_complete(void);
void physics_update(void);

void renderer_init(void);
void renderer_draw_frame(void);
void renderer_draw_title(void);
void renderer_draw_dead(void);
void renderer_draw_level_complete(void);
void renderer_draw_game_complete(void);
void renderer_mark_all_dirty(void);

void effects_update(void);
void effects_emit_collect(int px, int py);
void effects_emit_death(int px, int py);
void effects_flash(uint16_t color, int frames);
void effects_glitch_fx(void);

void audio_init(void);
void audio_update(void);
void audio_play(int seq_id);

/* SFX IDs */
#define SFX_STEP       0
#define SFX_SHARD      1
#define SFX_DEATH      2
#define SFX_ANCHOR     3
#define SFX_WIN        4
#define SFX_DISSOLVE   5
#define SFX_GLITCH_AMB 6
#define SFX_EXIT       7

/* ─── Utility macros ────────────────────────────────────────────────── */
#define CLAMP(x, lo, hi) ((x) < (lo) ? (lo) : (x) > (hi) ? (hi) : (x))
#define MIN(a, b)        ((a) < (b) ? (a) : (b))
#define MAX(a, b)        ((a) > (b) ? (a) : (b))
#define ABS(x)           ((x) < 0 ? -(x) : (x))
#define TIDX(c, r)       ((r) * GRID_W + (c))

#define FP(px)    ((int32_t)(px) * PHYS_FP)
#define PX(fp)    ((int32_t)(fp) / PHYS_FP)

#endif /* GAME_H */
