/**
 * @file main.c
 * @brief Space Invaders for AkiraOS — monochrome Sharp LS027B7DH01
 *
 * Controls:
 *   LEFT/RIGHT = Move   A/B/X/Y = Shoot   SETTINGS = Pause
 *
 * @copyright Copyright (c) 2026 PenEngineering S.R.L
 * @license Apache-2.0
 */

#include "akira_api.h"

/* ── Pins ────────────────────────────────────────────────────────────── */
#define BTN_LEFT     6
#define BTN_RIGHT    7
#define BTN_A        15
#define BTN_B        16
#define BTN_X        17
#define BTN_Y        41
#define BTN_UP       4
#define BTN_DOWN     5
#define BTN_SETTINGS 0

/* ── Screen ──────────────────────────────────────────────────────────── */
static int32_t SCR_W = 320, SCR_H = 240;

/* ── Monochrome palette ──────────────────────────────────────────────── */
#define COL_BG  0x0000u
#define COL_WHT 0xFFFFu

/* ── Dithered grey helper ────────────────────────────────────────────── */
static void draw_grey(int x, int y, int w, int h, int d) {
    display_rect(x, y, w, h, d >= 3 ? COL_WHT : COL_BG);
    int step = (d == 2) ? 2 : 4;
    int lc   = (d >= 3) ? COL_BG : COL_WHT;
    int st   = (d == 3) ? 3 : 0;
    for (int r = st; r < h; r += step) display_hline(x, y + r, w, lc);
}

/* ── Tuning ──────────────────────────────────────────────────────────── */
#define FRAME_US      20000
#define SHIP_Y        222
#define SHIP_W        16
#define SHIP_H         8
#define SHIP_SPEED     3
#define BULLET_SPEED   4
#define BULLET_W       2
#define BULLET_H       6
#define MAX_BULLETS    4
#define ALIEN_COLS    11
#define ALIEN_ROWS     5
#define ALIEN_W       12
#define ALIEN_H        8
#define ALIEN_PAD_X    4
#define ALIEN_PAD_Y    4
#define ALIEN_TOTAL   (ALIEN_COLS * ALIEN_ROWS)
#define ALIEN_AREA_W  (ALIEN_COLS * (ALIEN_W + ALIEN_PAD_X))
#define BOMB_SPEED     2
#define BOMB_W         2
#define BOMB_H         6
#define MAX_BOMBS      4
#define BOMB_CHANCE   60
#define SHIELD_COUNT   4
#define SHIELD_W      24
#define SHIELD_H      12
#define SHIELD_Y      200
#define SH_COLS       (SHIELD_W / 4)
#define SH_ROWS       (SHIELD_H / 4)
#define MYSTERY_W     20
#define MYSTERY_H      6
#define MYSTERY_Y     16
#define MYSTERY_SPEED  1
#define MYSTERY_CHANCE 800
#define MAX_EXPL       8

/* ── Alien bitmap sprites — 12px wide rows ───────────────────────────── */
/* Row bit 11=leftmost. Two animation frames per type. */
/* Type A (small invader) */
static const uint16_t ALN_A0[8] = {0x060,0x0F0,0x1F8,0x36C,0x1F8,0x090,0x1B0,0x240};
static const uint16_t ALN_A1[8] = {0x060,0x0F0,0x1F8,0x36C,0x1F8,0x1B0,0x090,0x420};
/* Type B (crab) */
static const uint16_t ALN_B0[8] = {0x180,0x3FC,0x7FE,0xDB6,0xFFE,0x3CC,0x6C6,0x183};
static const uint16_t ALN_B1[8] = {0x180,0x3FC,0x7FE,0xDB6,0xFFE,0x318,0xCC6,0x300};
/* Type C (octopus) */
static const uint16_t ALN_C0[8] = {0x0F0,0x3FC,0x7FE,0xDB6,0x7FE,0x3CC,0x0C0,0x660};
static const uint16_t ALN_C1[8] = {0x0F0,0x3FC,0x7FE,0xDB6,0x7FE,0x3CC,0x180,0x3C0};

static void draw_alien_sprite(int sx, int sy, const uint16_t *rows, int col) {
    for (int row = 0; row < 8; row++) {
        uint16_t mask = rows[row];
        int run_start = -1;
        for (int bit = 11; bit >= 0; bit--) {
            int set = (mask >> bit) & 1;
            if (set && run_start < 0) run_start = 11 - bit;
            else if (!set && run_start >= 0) {
                display_hline(sx + run_start, sy + row,
                              11 - bit - run_start, col ? COL_WHT : COL_BG);
                run_start = -1;
            }
        }
        if (run_start >= 0)
            display_hline(sx + run_start, sy + row, 12 - run_start, col ? COL_WHT : COL_BG);
    }
}

/* ── State ───────────────────────────────────────────────────────────── */
typedef struct { int x, y, active; } bullet_t;
typedef struct { int x, y, active; } bomb_t;
typedef struct { int x, y, timer;  } expl_t;

static bullet_t bullets[MAX_BULLETS];
static bomb_t   bombs[MAX_BOMBS];
static expl_t   expls[MAX_EXPL];

static int alien_alive[ALIEN_ROWS][ALIEN_COLS];
static int alien_base_x, alien_base_y, alien_dir;
static int alien_move_timer, alien_move_delay, alien_step_x, aliens_remaining;
static int alien_frame;   /* 0 or 1 for animation */

static int mystery_x, mystery_active, mystery_dir;
static uint8_t shield_hp[SHIELD_COUNT][SH_ROWS][SH_COLS];
static int ship_x, score, hi_score, lives, wave, game_over;
static int exit_to_supervisor, restart_game;

static uint32_t rng = 12345;
static int rng_next(int mod) {
    rng = rng * 1103515245 + 12345;
    return (int)((rng >> 16) & 0x7FFF) % mod;
}

/* ── High score ──────────────────────────────────────────────────────── */
static void load_hi(void) {
    int fd = storage_open("si_hi.dat", STORAGE_O_READ);
    if (fd >= 0) { storage_read(fd, &hi_score, sizeof(hi_score)); storage_close(fd); }
}
static void save_hi(void) {
    if (score > hi_score) {
        hi_score = score;
        int fd = storage_open("si_hi.dat", STORAGE_O_WRITE);
        if (fd >= 0) { storage_write(fd, &hi_score, sizeof(hi_score)); storage_close(fd); }
    }
}

/* ── Shield helpers ──────────────────────────────────────────────────── */
static int shield_bx(int idx) { return (SCR_W / (SHIELD_COUNT+1)) * (idx+1) - SHIELD_W/2; }
static void init_shields(void) {
    for (int s=0;s<SHIELD_COUNT;s++) for (int r=0;r<SH_ROWS;r++) for (int c=0;c<SH_COLS;c++) shield_hp[s][r][c]=3;
}
static void draw_shields(void) {
    for (int s=0;s<SHIELD_COUNT;s++) {
        int bx=shield_bx(s);
        for (int r=0;r<SH_ROWS;r++) for (int c=0;c<SH_COLS;c++) {
            int hp=shield_hp[s][r][c];
            if (hp == 0) display_rect(bx+c*4, SHIELD_Y+r*4, 4, 4, COL_BG);
            else draw_grey(bx+c*4, SHIELD_Y+r*4, 4, 4, hp);
        }
    }
}
static int damage_shield(int px, int py) {
    for (int s=0;s<SHIELD_COUNT;s++) {
        int bx=shield_bx(s);
        if (px>=bx&&px<bx+SHIELD_W&&py>=SHIELD_Y&&py<SHIELD_Y+SHIELD_H) {
            int c=(px-bx)/4, r=(py-SHIELD_Y)/4;
            if (c>=0&&c<SH_COLS&&r>=0&&r<SH_ROWS&&shield_hp[s][r][c]>0) {
                shield_hp[s][r][c]--; return 1;
            }
        }
    }
    return 0;
}

/* ── Alien helpers ───────────────────────────────────────────────────── */
static int alien_pts(int row) { return row==0?30:row<=2?20:10; }
static void get_alien_pos(int row, int col, int *ax, int *ay) {
    *ax = alien_base_x + col*(ALIEN_W+ALIEN_PAD_X);
    *ay = alien_base_y + row*(ALIEN_H+ALIEN_PAD_Y);
}
static void alien_bounds(int *lc, int *rc) {
    *lc=ALIEN_COLS; *rc=-1;
    for (int c=0;c<ALIEN_COLS;c++) for (int r=0;r<ALIEN_ROWS;r++) if (alien_alive[r][c]) {
        if (c<*lc)*lc=c; if (c>*rc)*rc=c;
    }
}

/* ── Explosion ───────────────────────────────────────────────────────── */
static void add_expl(int x, int y) {
    for (int i=0;i<MAX_EXPL;i++) if (expls[i].timer<=0) { expls[i].x=x; expls[i].y=y; expls[i].timer=8; return; }
}
static void draw_expl(expl_t *e) {
    if (e->timer<=0) return;
    int r=8-e->timer;
    display_line(e->x, e->y, e->x+r,   e->y,     COL_WHT);
    display_line(e->x, e->y, e->x-r,   e->y,     COL_WHT);
    display_line(e->x, e->y, e->x,     e->y+r,   COL_WHT);
    display_line(e->x, e->y, e->x,     e->y-r,   COL_WHT);
    display_line(e->x, e->y, e->x+r/2, e->y+r/2, COL_WHT);
    display_line(e->x, e->y, e->x-r/2, e->y+r/2, COL_WHT);
    display_line(e->x, e->y, e->x+r/2, e->y-r/2, COL_WHT);
    display_line(e->x, e->y, e->x-r/2, e->y-r/2, COL_WHT);
}

/* ── Init ────────────────────────────────────────────────────────────── */
static void init_game(void) {
    ship_x=SCR_W/2-SHIP_W/2; score=0; lives=3; wave=1; game_over=0;
    for (int i=0;i<MAX_BULLETS;i++) bullets[i].active=0;
    for (int i=0;i<MAX_BOMBS;i++) bombs[i].active=0;
    for (int i=0;i<MAX_EXPL;i++) expls[i].timer=0;
    mystery_active=0; aliens_remaining=ALIEN_TOTAL;
    alien_base_x=(SCR_W-ALIEN_AREA_W)/2; alien_base_y=30;
    alien_dir=1; alien_move_timer=0; alien_move_delay=30; alien_step_x=2; alien_frame=0;
    for (int r=0;r<ALIEN_ROWS;r++) for (int c=0;c<ALIEN_COLS;c++) alien_alive[r][c]=1;
    init_shields();
}
static void next_wave(void) {
    wave++; aliens_remaining=ALIEN_TOTAL; mystery_active=0;
    alien_base_x=(SCR_W-ALIEN_AREA_W)/2; alien_base_y=30; alien_dir=1;
    alien_move_timer=0; alien_move_delay=30-wave*3; if(alien_move_delay<6)alien_move_delay=6;
    alien_step_x=2; alien_frame=0;
    for (int r=0;r<ALIEN_ROWS;r++) for (int c=0;c<ALIEN_COLS;c++) alien_alive[r][c]=1;
    for (int i=0;i<MAX_BOMBS;i++) bombs[i].active=0;
    init_shields();
}

/* ── Input ───────────────────────────────────────────────────────────── */
static int prev_a, prev_s;
static int handle_input(void) {
    int l=gpio_read(BTN_LEFT), r=gpio_read(BTN_RIGHT);
    int a=gpio_read(BTN_A)|gpio_read(BTN_B)|gpio_read(BTN_X)|gpio_read(BTN_Y);
    int s=gpio_read(BTN_SETTINGS);
    if (l&&ship_x>0) ship_x-=SHIP_SPEED;
    if (r&&ship_x<SCR_W-SHIP_W) ship_x+=SHIP_SPEED;
    if (a&&!prev_a) {
        for (int i=0;i<MAX_BULLETS;i++) if (!bullets[i].active) {
            bullets[i].x=ship_x+SHIP_W/2-BULLET_W/2;
            bullets[i].y=SHIP_Y-BULLET_H; bullets[i].active=1; break;
        }
    }
    if (s&&!prev_s){prev_s=s;return 1;}
    prev_a=a; prev_s=s; return 0;
}

/* ── Update ──────────────────────────────────────────────────────────── */
static void update_bullets(void) {
    for (int i=0;i<MAX_BULLETS;i++) {
        if (!bullets[i].active) continue;
        display_rect(bullets[i].x, bullets[i].y, BULLET_W, BULLET_H, COL_BG);
        bullets[i].y-=BULLET_SPEED;
        if (bullets[i].y<0){bullets[i].active=0;continue;}
        if (damage_shield(bullets[i].x+BULLET_W/2, bullets[i].y)){bullets[i].active=0;continue;}
        if (mystery_active&&bullets[i].x+BULLET_W>mystery_x&&bullets[i].x<mystery_x+MYSTERY_W&&
            bullets[i].y<MYSTERY_Y+MYSTERY_H&&bullets[i].y+BULLET_H>MYSTERY_Y) {
            score+=100+rng_next(150); add_expl(mystery_x+MYSTERY_W/2,MYSTERY_Y+MYSTERY_H/2);
            display_rect(mystery_x,MYSTERY_Y,MYSTERY_W,MYSTERY_H,COL_BG);
            mystery_active=0; bullets[i].active=0; continue;
        }
        int hit=0;
        for (int r=0;r<ALIEN_ROWS&&!hit;r++) for (int c=0;c<ALIEN_COLS&&!hit;c++) {
            if (!alien_alive[r][c]) continue;
            int ax,ay; get_alien_pos(r,c,&ax,&ay);
            if (bullets[i].x+BULLET_W>ax&&bullets[i].x<ax+ALIEN_W&&
                bullets[i].y<ay+ALIEN_H&&bullets[i].y+BULLET_H>ay) {
                alien_alive[r][c]=0; aliens_remaining--; score+=alien_pts(r);
                if(score>hi_score)hi_score=score;
                add_expl(ax+ALIEN_W/2,ay+ALIEN_H/2);
                display_rect(ax,ay,ALIEN_W,ALIEN_H,COL_BG);
                bullets[i].active=0; hit=1;
            }
        }
    }
}
static void update_bombs(void) {
    for (int i=0;i<MAX_BOMBS;i++) {
        if (!bombs[i].active) continue;
        display_rect(bombs[i].x, bombs[i].y, BOMB_W, BOMB_H, COL_BG);
        bombs[i].y+=BOMB_SPEED;
        if (bombs[i].y>SCR_H){bombs[i].active=0;continue;}
        if (damage_shield(bombs[i].x+BOMB_W/2, bombs[i].y+BOMB_H)){bombs[i].active=0;continue;}
        if (bombs[i].x+BOMB_W>ship_x&&bombs[i].x<ship_x+SHIP_W&&
            bombs[i].y+BOMB_H>SHIP_Y&&bombs[i].y<SHIP_Y+SHIP_H) {
            bombs[i].active=0; lives--;
            add_expl(ship_x+SHIP_W/2, SHIP_Y+SHIP_H/2);
            if (lives<=0) game_over=1;
        }
    }
}
static void update_aliens(void) {
    alien_move_timer++;
    if (alien_move_timer<alien_move_delay) return;
    alien_move_timer=0; alien_frame^=1;
    for (int r=0;r<ALIEN_ROWS;r++) for (int c=0;c<ALIEN_COLS;c++)
        if (alien_alive[r][c]) { int ax,ay; get_alien_pos(r,c,&ax,&ay); display_rect(ax,ay,ALIEN_W,ALIEN_H,COL_BG); }
    int lc,rc; alien_bounds(&lc,&rc);
    if (lc>rc) return;
    int left_px=alien_base_x+lc*(ALIEN_W+ALIEN_PAD_X);
    int right_px=alien_base_x+rc*(ALIEN_W+ALIEN_PAD_X)+ALIEN_W;
    int need_drop=(alien_dir>0&&right_px+alien_step_x>=SCR_W-4)||(alien_dir<0&&left_px-alien_step_x<=4);
    if (need_drop){alien_base_y+=ALIEN_H;alien_dir=-alien_dir;if(alien_move_delay>6)alien_move_delay--;}
    else alien_base_x+=alien_dir*alien_step_x;
    for (int r2=ALIEN_ROWS-1;r2>=0;r2--) for (int c2=0;c2<ALIEN_COLS;c2++) if (alien_alive[r2][c2]) {
        int ax,ay; get_alien_pos(r2,c2,&ax,&ay); if (ay+ALIEN_H>=SHIP_Y) game_over=1;
    }
    for (int c2=0;c2<ALIEN_COLS;c2++) for (int r2=ALIEN_ROWS-1;r2>=0;r2--) if (alien_alive[r2][c2]) {
        if (rng_next(BOMB_CHANCE)==0) for (int b=0;b<MAX_BOMBS;b++) if (!bombs[b].active) {
            int ax,ay; get_alien_pos(r2,c2,&ax,&ay);
            bombs[b].x=ax+ALIEN_W/2; bombs[b].y=ay+ALIEN_H; bombs[b].active=1; break;
        }
        break;
    }
    if (aliens_remaining<=5) alien_move_delay=2;
    else if (aliens_remaining<=15) alien_move_delay=4;
}
static void update_mystery(void) {
    if (!mystery_active) { if (rng_next(MYSTERY_CHANCE)==0){mystery_active=1;mystery_dir=rng_next(2)?1:-1;mystery_x=mystery_dir>0?-MYSTERY_W:SCR_W;} return; }
    display_rect(mystery_x,MYSTERY_Y,MYSTERY_W,MYSTERY_H,COL_BG);
    mystery_x+=mystery_dir*MYSTERY_SPEED;
    if (mystery_x<-MYSTERY_W||mystery_x>SCR_W) mystery_active=0;
}
static void update_expls(void) {
    for (int i=0;i<MAX_EXPL;i++) if (expls[i].timer>0) {
        expls[i].timer--;
        if (expls[i].timer==0) { int r2=8; display_rect(expls[i].x-r2,expls[i].y-r2,r2*2,r2*2,COL_BG); }
    }
}

/* ── Drawing ─────────────────────────────────────────────────────────── */
static void draw_hud(void) {
    draw_grey(0, 0, SCR_W, 14, 2);
    display_text(4,   2, "SCORE", COL_BG); display_number(50,  2, score,    COL_BG);
    display_text(140, 2, "W",     COL_BG); display_number(152, 2, wave,     COL_BG);
    display_text(185, 2, "BEST",  COL_BG); display_number(220, 2, hi_score, COL_BG);
    display_text(270, 2, "LIVES", COL_BG);
    for (int i=0;i<lives;i++) { int lx=SCR_W-24+i*8; display_triangle_fill(lx,13,lx+6,13,lx+3,8,COL_BG); }
}
static void draw_ship(void) {
    int cx=ship_x+SHIP_W/2;
    display_triangle_fill(ship_x, SHIP_Y+SHIP_H, ship_x+SHIP_W, SHIP_Y+SHIP_H, cx, SHIP_Y+2, COL_WHT);
    display_rect(cx-1, SHIP_Y, 2, 4, COL_WHT);
    display_rect_outline(ship_x, SHIP_Y+SHIP_H-4, SHIP_W, 4, COL_BG);
}
static void draw_aliens(void) {
    for (int r=0;r<ALIEN_ROWS;r++) for (int c=0;c<ALIEN_COLS;c++) {
        if (!alien_alive[r][c]) continue;
        int ax,ay; get_alien_pos(r,c,&ax,&ay);
        const uint16_t *sprite;
        if      (r==0) sprite=(alien_frame?ALN_A1:ALN_A0);
        else if (r<=2) sprite=(alien_frame?ALN_B1:ALN_B0);
        else           sprite=(alien_frame?ALN_C1:ALN_C0);
        draw_alien_sprite(ax, ay, sprite, 1);
    }
}
static void draw_bullets(void) {
    for (int i=0;i<MAX_BULLETS;i++) if (bullets[i].active)
        display_rect(bullets[i].x, bullets[i].y, BULLET_W, BULLET_H, COL_WHT);
}
static void draw_bombs(void) {
    for (int i=0;i<MAX_BOMBS;i++) if (bombs[i].active) {
        display_vline(bombs[i].x,   bombs[i].y, BOMB_H, COL_WHT);
        display_vline(bombs[i].x+1, bombs[i].y+2, BOMB_H-2, COL_WHT);
    }
}
static void draw_mystery(void) {
    if (!mystery_active) return;
    int mx=mystery_x, my=MYSTERY_Y;
    display_rect(mx+2, my+2, MYSTERY_W-4, MYSTERY_H-2, COL_WHT);
    display_triangle_fill(mx+4, my+2, mx+MYSTERY_W-4, my+2, mx+MYSTERY_W/2, my, COL_WHT);
    for (int i=0;i<4;i++) display_pixel(mx+4+i*4, my+3, COL_BG);
}
static void draw_expls(void) {
    for (int i=0;i<MAX_EXPL;i++) draw_expl(&expls[i]);
}
static void draw_ground(void) { display_hline(0, SHIP_Y+SHIP_H+2, SCR_W, COL_WHT); }

/* ── Pause ───────────────────────────────────────────────────────────── */
enum { MN_RESUME=0,MN_RESTART,MN_EXIT,MN_N };
static const char *mlbl[]={"Resume","Restart","Exit"};
static int pause_menu(void) {
    int cur=0,pu=1,pd=1,pa=1,ps=1;
    while (1) {
        display_rect(80,60,160,120,COL_BG); display_rect_outline(80,60,160,120,COL_WHT);
        draw_grey(82,62,156,16,2); display_text(130,66,"PAUSED",COL_BG);
        for (int i=0;i<MN_N;i++){int iy=87+i*18;if(i==cur){display_rect(82,iy-2,156,16,COL_WHT);display_text(120,iy,mlbl[i],COL_BG);}else{display_rect(82,iy-2,156,16,COL_BG);display_text(120,iy,mlbl[i],COL_WHT);}}
        display_flush();
        int u=gpio_read(BTN_UP),d=gpio_read(BTN_DOWN),a=gpio_read(BTN_A),s=gpio_read(BTN_SETTINGS);
        if(s&&!ps)return MN_RESUME;
        if(u&&!pu)cur=(cur>0)?cur-1:MN_N-1;
        if(d&&!pd)cur=(cur<MN_N-1)?cur+1:0;
        if(a&&!pa)return cur;
        pu=u;pd=d;pa=a;ps=s;delay(20000);
    }
}

/* ── Full redraw ─────────────────────────────────────────────────────── */
static void full_redraw(void) {
    display_clear(COL_BG); draw_hud(); draw_ground(); draw_shields();
    draw_aliens(); draw_ship(); draw_bullets(); draw_bombs(); draw_mystery(); draw_expls();
    display_flush();
}

/* ── Game loop ───────────────────────────────────────────────────────── */
static void game_loop(void) {
    int prev_sx=ship_x; full_redraw();
    while (!game_over) {
        if (handle_input()){int ch=pause_menu();if(ch==MN_EXIT){exit_to_supervisor=1;return;}if(ch==MN_RESTART){restart_game=1;return;}full_redraw();}
        if (prev_sx!=ship_x) display_rect(prev_sx,SHIP_Y,SHIP_W,SHIP_H+4,COL_BG);
        update_bullets(); update_bombs(); update_aliens(); update_mystery(); update_expls();
        if (aliens_remaining<=0){next_wave();full_redraw();delay(500000);prev_sx=ship_x;continue;}
        draw_hud(); draw_ship(); draw_bullets(); draw_bombs(); draw_aliens();
        draw_mystery(); draw_expls(); draw_shields();
        display_flush(); prev_sx=ship_x; delay(FRAME_US);
    }
}

/* ── Entry point ─────────────────────────────────────────────────────── */
int main(void) {
    printf("AkiraOS Space Invaders v2.0");
    display_get_size(&SCR_W, &SCR_H);

    gpio_configure(BTN_UP,       GPIO_INPUT|GPIO_PULL_UP|GPIO_ACTIVE_LOW);
    gpio_configure(BTN_DOWN,     GPIO_INPUT|GPIO_PULL_UP|GPIO_ACTIVE_LOW);
    gpio_configure(BTN_LEFT,     GPIO_INPUT|GPIO_PULL_UP|GPIO_ACTIVE_LOW);
    gpio_configure(BTN_RIGHT,    GPIO_INPUT|GPIO_PULL_UP|GPIO_ACTIVE_LOW);
    gpio_configure(BTN_A,        GPIO_INPUT|GPIO_PULL_UP|GPIO_ACTIVE_LOW);
    gpio_configure(BTN_B,        GPIO_INPUT|GPIO_PULL_UP|GPIO_ACTIVE_LOW);
    gpio_configure(BTN_X,        GPIO_INPUT|GPIO_PULL_UP|GPIO_ACTIVE_LOW);
    gpio_configure(BTN_Y,        GPIO_INPUT|GPIO_PULL_UP|GPIO_ACTIVE_LOW);
    gpio_configure(BTN_SETTINGS, GPIO_INPUT|GPIO_PULL_UP|GPIO_ACTIVE_LOW);
    load_hi();

    /* Title screen */
    display_clear(COL_BG);
    draw_grey(0,0,SCR_W,18,2); draw_grey(0,SCR_H-18,SCR_W,18,2);
    /* Sample alien sprites */
    draw_alien_sprite(30, 48, ALN_A0, 1);  display_text(50, 50, "= 30 pts", COL_WHT);
    draw_alien_sprite(30, 70, ALN_B0, 1);  display_text(50, 72, "= 20 pts", COL_WHT);
    draw_alien_sprite(30, 92, ALN_C0, 1);  display_text(50, 94, "= 10 pts", COL_WHT);
    display_text_large(60,130,"SPACE",   COL_WHT);
    display_text_large(36,160,"INVADERS",COL_WHT);
    display_text(76,196,"A/B/X/Y = shoot", COL_WHT);
    display_text(84,210,"Press A to start",COL_WHT);
    display_flush();

    uint32_t seed=1;
    while(!gpio_read(BTN_A)&&!gpio_read(BTN_B)){seed++;delay(20000);}
    rng=seed^0xDEADBEEF; rng_next(7);rng_next(7);rng_next(7); delay(100000);

    while (1) {
        restart_game=0; exit_to_supervisor=0;
        init_game(); game_loop();
        save_hi();
        if (exit_to_supervisor){app_switch("supervisor");return 0;}
        if (restart_game){seed++;rng=seed^0xDEADBEEF;rng_next(7);rng_next(7);continue;}
        break;
    }

    /* Game over */
    display_clear(COL_BG);
    display_rect_outline(20,20,280,200,COL_WHT); draw_grey(22,22,276,28,2);
    display_text_large(68,26,"GAME OVER",COL_BG);
    display_hline(20,54,280,COL_WHT);
    display_text(80,70,"SCORE:",COL_WHT); display_number(135,70,score,COL_WHT);
    display_text(80,90,"BEST:", COL_WHT); display_number(130,90,hi_score,COL_WHT);
    display_text(80,110,"WAVE:", COL_WHT); display_number(130,110,wave,COL_WHT);
    display_text(68,140,"A:Retry  SETTINGS:Exit",COL_WHT);
    display_flush(); delay(5000000);
    printf("Game over!"); return 0;
}
