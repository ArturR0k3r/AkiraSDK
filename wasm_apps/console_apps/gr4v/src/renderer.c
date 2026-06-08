/*
 * renderer.c — Tile, sprite, HUD, effects rendering for GR4V.
 * Draws directly via display_* API calls; uses dirty-tile tracking.
 */
#include "game.h"
#include "akira_api.h"

/* ─── Font draw ──────────────────────────────────────────────────────── */
static int font_idx(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'Z') return 10 + (c - 'A');
    if (c >= 'a' && c <= 'z') return 10 + (c - 'a');
    return -1;
}

/* Returns pixel width of rendered string (5 px per char incl. gap) */
static int str_width(const char *s)
{
    int n = 0;
    while (*s++) n++;
    return n * 5;
}

static void draw_char(int x, int y, char c, uint16_t col)
{
    int idx = font_idx(c);
    if (idx < 0) return;
    const uint8_t *rows = font_data[idx];
    for (int row = 0; row < 5; row++) {
        uint8_t bits = rows[row];
        for (int bit = 3; bit >= 0; bit--) {
            if (bits & (1u << (uint8_t)bit))
                display_pixel(x + (3 - bit), y + row, col);
        }
    }
}

static void draw_str(int x, int y, const char *s, uint16_t col)
{
    while (*s) {
        draw_char(x, y, *s++, col);
        x += 5;
    }
}

/* Draw string centered at (cx) */
static void draw_str_center(int cx, int y, const char *s, uint16_t col)
{
    int w = str_width(s);
    draw_str(cx - w / 2, y, s, col);
}


/* ─── Starfield ──────────────────────────────────────────────────────── */
static void init_stars(void)
{
    uint32_t r = 0xCAFEBABEu;
    for (int i = 0; i < 12; i++) {
        r = r * 1664525u + 1013904223u;
        g.stars1[i][0] = (uint16_t)(r % (uint32_t)SCR_W);
        r = r * 1664525u + 1013904223u;
        g.stars1[i][1] = (uint16_t)(r % (uint32_t)PLAY_H);
    }
    for (int i = 0; i < 8; i++) {
        r = r * 1664525u + 1013904223u;
        g.stars2[i][0] = (uint16_t)(r % (uint32_t)SCR_W);
        r = r * 1664525u + 1013904223u;
        g.stars2[i][1] = (uint16_t)(r % (uint32_t)PLAY_H);
    }
}

static void draw_stars(void)
{
    for (int i = 0; i < 12; i++)
        display_pixel(g.stars1[i][0], g.stars1[i][1], C_STAR1);
    for (int i = 0; i < 8; i++)
        display_pixel(g.stars2[i][0], g.stars2[i][1], C_STAR2);
}

/* ─── Tile rendering ─────────────────────────────────────────────────── */
static void draw_tile_solid(int tx, int ty, uint16_t edge_col)
{
    display_rect(tx, ty, TILE_W, TILE_H, C_PLAT);
    /* top edge: neon purple (or alternate color) */
    display_hline(tx, ty, TILE_W, edge_col);
    /* circuit trace: 4px teal lines at y+3, every 8px segment */
    for (int sx = 0; sx + 4 <= TILE_W; sx += 8) {
        display_hline(tx + sx, ty + 3, 4, C_TRACE);
    }
    /* solder pads at y+5, every 16px */
    display_rect(tx + 5, ty + 5, 2, 2, C_TRACE);
}

static void draw_tile_shard(int tx, int ty)
{
    /* Pulsing cyan diamond centered in tile; brightness oscillates at 3Hz */
    uint8_t bright = (uint8_t)((g.frame * 3u / 10u) & 1u);
    uint16_t col = bright ? C_SHARD : 0x03DFu;  /* dim cyan */
    int cx = tx + TILE_W / 2;
    int cy = ty + TILE_H / 2;
    /* 8×8 diamond (4px radius) */
    for (int d = 0; d < 4; d++) {
        display_hline(cx - d, cy - (3 - d), 2 * d + 1, col);
        display_hline(cx - d, cy + (3 - d), 2 * d + 1, col);
    }
    display_hline(cx - 4, cy, 9, col);
}

static void draw_tile_exit(int tx, int ty)
{
    uint8_t pulse = (uint8_t)((g.frame / 15u) & 1u);
    display_rect(tx, ty, TILE_W, TILE_H, C_PLAT);
    display_rect_outline(tx, ty, TILE_W, TILE_H,
                         pulse ? C_GLITCH : 0x7807u);
    /* "EXIT" label, 4×5 font, centered */
    draw_str(tx + 1, ty + 5, "EXIT", C_GLITCH);
}

static void draw_tile_spike(int tx, int ty)
{
    /* Red-orange triangle: base at tile bottom, apex at center-top */
    int x0 = tx + TILE_W / 2;
    int y0 = ty;
    int x1 = tx;
    int y1 = ty + TILE_H - 1;
    int x2 = tx + TILE_W - 1;
    int y2 = ty + TILE_H - 1;
    display_triangle_fill(x0, y0, x1, y1, x2, y2, C_SPIKE);
    display_triangle(x0, y0, x1, y1, x2, y2, 0xF000u);
}

static void draw_tile_dissolve(int tx, int ty, int idx)
{
    DissolveTile *dv = &g.dissolve[idx];
    if (dv->gone) { display_rect(tx, ty, TILE_W, TILE_H, C_BG); return; }

    /* Choose edge color based on dissolve progress */
    uint16_t edge;
    if (dv->stand < DSV_WARN1)
        edge = C_EDGE;
    else if (dv->stand < DSV_WARN2)
        edge = C_PALE_VIO;
    else {
        /* Flicker at 8Hz = ~4 frames per half-cycle */
        if ((g.frame >> 2u) & 1u) {
            display_rect(tx, ty, TILE_W, TILE_H, C_BG);
            return;
        }
        edge = C_PALE_VIO;
    }
    draw_tile_solid(tx, ty, edge);
}

static void draw_tile_anchor(int tx, int ty)
{
    int cx = tx + TILE_W / 2;
    int cy = ty + TILE_H / 2;
    /* Pulsing mint-green diamond outline 8×8 */
    uint8_t p = (uint8_t)((g.frame * 4u / 10u) & 1u);
    uint16_t col = p ? C_ANCHOR : 0x03E4u;
    display_line(cx, cy - 4, cx + 4, cy, col);
    display_line(cx + 4, cy, cx, cy + 4, col);
    display_line(cx, cy + 4, cx - 4, cy, col);
    display_line(cx - 4, cy, cx, cy - 4, col);
}

static void draw_tile_void(int tx, int ty)
{
    /* 1-bit checkerboard: alternating black/dark-indigo */
    for (int dy = 0; dy < TILE_H; dy++) {
        for (int dx = 0; dx < TILE_W; dx++) {
            uint16_t col = ((dx ^ dy) & 1u) ? C_VOID_B : 0x0000u;
            display_pixel(tx + dx, ty + dy, col);
        }
    }
}

static void draw_tile_glitch(int tx, int ty)
{
    /* No tile body — just dashed hot-magenta border every 8 ticks */
    if ((g.frame & 7u) < 4u) {
        display_rect_outline(tx, ty, TILE_W, TILE_H, C_GLITCH);
    }
}

static void draw_tile(int col, int row)
{
    int tx = col * TILE_W;
    int ty = row * TILE_H;
    int idx = TIDX(col, row);
    uint8_t tid = g.tilemap[row][col];

    switch (tid) {
    case T_AIR:
        display_rect(tx, ty, TILE_W, TILE_H, C_BG);
        break;
    case T_SOLID:
        draw_tile_solid(tx, ty, C_EDGE);
        break;
    case T_SHARD:
        display_rect(tx, ty, TILE_W, TILE_H, C_BG);
        draw_tile_shard(tx, ty);
        break;
    case T_EXIT:
        draw_tile_exit(tx, ty);
        break;
    case T_SPIKE:
        display_rect(tx, ty, TILE_W, TILE_H, C_BG);
        draw_tile_spike(tx, ty);
        break;
    case T_DISSOLVE:
        draw_tile_dissolve(tx, ty, idx);
        break;
    case T_ANCHOR:
        display_rect(tx, ty, TILE_W, TILE_H, C_BG);
        draw_tile_anchor(tx, ty);
        break;
    case T_VOID:
        draw_tile_void(tx, ty);
        break;
    case T_GLITCH:
        display_rect(tx, ty, TILE_W, TILE_H, 0x0000u);
        draw_tile_glitch(tx, ty);
        break;
    default:
        display_rect(tx, ty, TILE_W, TILE_H, C_BG);
        break;
    }
}

/* ─── Sprite ─────────────────────────────────────────────────────────── */
static uint16_t mirror_row_10(uint16_t r)
{
    uint16_t m = 0;
    for (int i = 0; i < 10; i++) {
        if (r & (uint16_t)(1u << (9 - i))) m |= (uint16_t)(1u << i);
    }
    return m;
}

static void draw_sprite(int ox, int oy, const uint16_t *rows, int flip)
{
    for (int row = 0; row < PLAYER_H; row++) {
        uint16_t bits = flip ? mirror_row_10(rows[row]) : rows[row];
        for (int col = 0; col < PLAYER_W; col++) {
            if (bits & (uint16_t)(1u << (9 - col)))
                display_pixel(ox + col, oy + row, C_CHAR);
        }
    }
}

/* ─── HUD ────────────────────────────────────────────────────────────── */

/* Draw gravity indicator: 14×7 circle + needle */
static void draw_gravity_indicator(int bx, int by)
{
    int cx = bx + 7;
    int cy = by + 3;
    display_circle(cx, cy, 3, 0x333355u & 0xFFFFu);
    /* Needle direction from angle_idx */
    int sv = SIN64(g.angle_idx);
    int cv = COS64(g.angle_idx);
    /* Scale to 3px radius */
    int ex = cx + (sv * 3) / 16384;
    int ey = cy + (cv * 3) / 16384;
    display_line(cx, cy, ex, ey, C_EDGE);
    /* Anchor arc: if active, draw countdown arc in mint green */
    if (g.anchor_active) {
        uint16_t a_col = C_ANCHOR;
        display_circle(cx, cy, 2, a_col);
    }
    display_pixel(cx, cy, 0xAAAAu);
}

static void draw_hud(void)
{
    /* HUD background */
    display_rect(0, HUD_Y, SCR_W, HUD_H, C_HUD_BG);

    /* Gravity indicator (left) */
    draw_gravity_indicator(1, HUD_Y);

    /* Level name (center) */
    draw_str_center(SCR_W / 2, HUD_Y + 1, level_name[g.level], C_HUD_DIM);

    /* Level dots (right-center) */
    int dx = SCR_W - 50;
    for (int i = 0; i < NUM_LEVELS; i++) {
        uint16_t dc = (i == g.level) ? C_HUD_DOT : 0x111133u & 0xFFFFu;
        display_circle(dx + i * 7, HUD_Y + 3, 2, dc);
    }

    /* Shard counter (right) */
    {
        char buf[10];
        buf[0] = 'M'; buf[1] = 'E'; buf[2] = 'M'; buf[3] = ':';
        buf[4] = '0' + (char)g.shards_collected;
        buf[5] = '/'; buf[6] = '0' + SHARD_GOAL;
        buf[7] = '\0';
        draw_str(SCR_W - 36, HUD_Y + 1, buf, C_SHARD);
    }
}

/* ─── CRT scanlines (every even row darkened) ────────────────────────── */
static void draw_scanlines(void)
{
    /* Drawing 1px wide rectangles for each even row is expensive;
       approximate with every-other-line hlines across play area */
    for (int y = 0; y < PLAY_H; y += 2) {
        /* 20% alpha black overlay — approximate by dimming to ~80%:
           draw a black pixel at every 5th x position on this row */
        for (int x = 0; x < SCR_W; x += 5) {
            display_pixel(x, y, 0x0000u);
        }
    }
}

/* ─── Particles and float texts ──────────────────────────────────────── */
static void draw_particles(void)
{
    for (int i = 0; i < 16; i++) {
        Particle *p = &g.particles[i];
        if (!p->active) continue;
        int px = PX(p->fpx);
        int py = PX(p->fpy);
        if (px >= 0 && px < SCR_W && py >= 0 && py < PLAY_H)
            display_pixel(px, py, p->color);
    }
}

static void draw_floattexts(void)
{
    for (int i = 0; i < 8; i++) {
        FloatText *ft = &g.floattexts[i];
        if (!ft->active) continue;
        int py = ft->fpy / PHYS_FP;
        if (py < 0 || py >= PLAY_H) continue;
        /* fade: alpha proportional to life/max_life — just use color dimming */
        uint16_t col = (ft->life > ft->max_life / 2) ? ft->color : 0x03EFu;
        draw_str(ft->x, py, ft->text, col);
    }
}

/* ─── Screen flash overlay ───────────────────────────────────────────── */
static void draw_flash(void)
{
    if (!g.flash_frames) return;
    /* Approximate: draw every 4th pixel in flash color */
    for (int y = 0; y < SCR_H; y++) {
        for (int x = (y & 1); x < SCR_W; x += 2) {
            display_pixel(x, y, g.flash_color);
        }
    }
}

/* ─── Overlay text helpers ───────────────────────────────────────────── */
static void draw_big_str(int cx, int cy, const char *s, uint16_t col)
{
    /* 2× scaled font */
    int ox = cx - (int)(str_width(s) * 2) / 2;
    while (*s) {
        int fi = font_idx(*s++);
        if (fi < 0) { ox += 10; continue; }
        const uint8_t *rows = font_data[fi];
        for (int row = 0; row < 5; row++) {
            uint8_t bits = rows[row];
            for (int bit = 3; bit >= 0; bit--) {
                if (bits & (1u << (uint8_t)bit)) {
                    int bx = ox + (3 - bit) * 2;
                    int by = cy + row * 2;
                    display_rect(bx, by, 2, 2, col);
                }
            }
        }
        ox += 10;
    }
}

/* ─── Public interface ───────────────────────────────────────────────── */
void renderer_init(void)
{
    init_stars();
    renderer_mark_all_dirty();
}

void renderer_mark_all_dirty(void)
{
    for (int i = 0; i < TILE_COUNT; i++) g.dirty[i] = 1;
}

static void renderer_mark_region_dirty(int col, int row)
{
    if (col >= 0 && col < GRID_W && row >= 0 && row < GRID_H)
        g.dirty[TIDX(col, row)] = 1;
}

void renderer_draw_frame(void)
{
    /* Shard/anchor tiles pulse every few frames — re-dirty them */
    if ((g.frame & 3u) == 0) {
        for (int r = 0; r < GRID_H; r++) {
            for (int c = 0; c < GRID_W; c++) {
                uint8_t t = g.tilemap[r][c];
                if (t == T_SHARD || t == T_ANCHOR || t == T_EXIT || t == T_GLITCH)
                    g.dirty[TIDX(c, r)] = 1;
            }
        }
    }

    /* Redraw dirty tiles + adjacent tiles to erase old sprite overlap */
    int px_tile = PX(g.player.fpx) / TILE_W;
    int py_tile = PX(g.player.fpy) / TILE_H;
    for (int dy = -1; dy <= 2; dy++) {
        for (int dx = -1; dx <= 2; dx++) {
            renderer_mark_region_dirty(px_tile + dx, py_tile + dy);
        }
    }

    for (int r = 0; r < GRID_H; r++) {
        for (int c = 0; c < GRID_W; c++) {
            if (g.dirty[TIDX(c, r)]) {
                draw_tile(c, r);
                g.dirty[TIDX(c, r)] = 0;
            }
        }
    }

    /* Stars (draw over background) */
    draw_stars();

    /* Sprite */
    {
        int ox = PX(g.player.fpx);
        int oy = PX(g.player.fpy);
        const uint16_t *frame_rows = g.player.walk_frame ? sprite_b : sprite_a;
        int flip = !g.player.facing_right;

        /* Movement trail: ghost 3px behind player */
        if (ABS(g.player.vx) > 64) {
            int trail_ox = ox + (flip ? 3 : -3);
            for (int row = 0; row < PLAYER_H; row++) {
                uint16_t bits = flip ? mirror_row_10(frame_rows[row]) : frame_rows[row];
                for (int col = 0; col < PLAYER_W; col++) {
                    if (bits & (uint16_t)(1u << (9 - col))) {
                        int tx2 = trail_ox + col;
                        if (tx2 >= 0 && tx2 < SCR_W && oy + row >= 0 && oy + row < PLAY_H)
                            display_pixel(tx2, oy + row, 0x0208u);  /* dim trail */
                    }
                }
            }
        }

        draw_sprite(ox, oy, frame_rows, flip);
    }

    /* Particles and float texts */
    draw_particles();
    draw_floattexts();

    /* CRT scanlines */
    draw_scanlines();

    /* Screen flash */
    draw_flash();

    /* HUD */
    draw_hud();
}

/* ─── Title screen ───────────────────────────────────────────────────── */
void renderer_draw_title(void)
{
    display_clear(C_BG);
    draw_stars();

    /* "GR4V" in large font, centered */
    draw_big_str(SCR_W / 2, 40, "GR4V", C_EDGE);

    /* Blinking "TILT TO START" at 1Hz */
    if ((g.frame / 15u) & 1u)
        draw_str_center(SCR_W / 2, 72, "TILT TO START", C_HUD_TXT);

    /* Spinning gravity indicator */
    int cx = SCR_W / 2;
    int cy = 20;
    uint32_t spin_idx = (g.frame / 4u) & 63u;
    display_circle(cx, cy, 5, C_EDGE);
    int sv = SIN64(spin_idx);
    int cv = COS64(spin_idx);
    display_line(cx, cy, cx + (sv * 5) / 16384, cy + (cv * 5) / 16384, C_ANCHOR);

    draw_hud();
    display_flush();
}

/* ─── Death overlay ──────────────────────────────────────────────────── */
void renderer_draw_dead(void)
{
    renderer_draw_frame();
    /* "CORRUPTED" blinks 4× over 1.2s */
    if ((g.frame / 4u) & 1u)
        draw_str_center(SCR_W / 2, PLAY_H / 2 - 3, "CORRUPTED", C_SPIKE);
    display_flush();
}

/* ─── Level complete overlay ─────────────────────────────────────────── */
void renderer_draw_level_complete(void)
{
    renderer_draw_frame();
    draw_str_center(SCR_W / 2, PLAY_H / 2 - 3, "SEGMENT RECOVERED", C_SHARD);
    display_flush();
}

/* ─── Game complete screen ───────────────────────────────────────────── */
void renderer_draw_game_complete(void)
{
    display_clear(C_BG);
    draw_stars();
    draw_str_center(SCR_W / 2, 30, "ALL SEGMENTS", C_SHARD);
    draw_str_center(SCR_W / 2, 42, "RECOVERED", C_SHARD);
    {
        char buf[10];
        buf[0] = 'M'; buf[1] = 'E'; buf[2] = 'M'; buf[3] = ':';
        buf[4] = '0' + (char)g.shards_collected;
        buf[5] = '/'; buf[6] = '0' + (char)(SHARD_GOAL * NUM_LEVELS);
        buf[7] = '\0';
        draw_str_center(SCR_W / 2, 60, buf, C_HUD_TXT);
    }
    if ((g.frame / 15u) & 1u)
        draw_str_center(SCR_W / 2, 80, "PRESS A RESTART", C_HUD_TXT);
    display_flush();
}
