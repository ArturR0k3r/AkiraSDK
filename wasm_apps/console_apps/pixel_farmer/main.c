/**
 * @file main.c
 * @brief Pixel Farmer — farming sim for AkiraOS
 *        Monochrome-optimised for Sharp LS027B7DH01.
 *
 * Controls:
 *   DPAD = Move cursor   A = Action (till/plant/water/harvest)
 *   B/X = Cycle seed fwd   Y = Cycle seed back   SETTINGS = Pause
 *
 * @copyright Copyright (c) 2026 PenEngineering S.R.L
 * @license Apache-2.0
 */

#include "akira_api.h"

/* ── Pins ────────────────────────────────────────────────────────────── */
#define BTN_UP       4
#define BTN_DOWN     5
#define BTN_LEFT     6
#define BTN_RIGHT    7
#define BTN_A        15
#define BTN_B        16
#define BTN_X        17
#define BTN_Y        41
#define BTN_SETTINGS 0

/* ── Screen ──────────────────────────────────────────────────────────── */
#define SCR_W 320
#define SCR_H 240

/* ── Palette ─────────────────────────────────────────────────────────── */
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

/* ── Grid ────────────────────────────────────────────────────────────── */
#define GRID_COLS  10
#define GRID_ROWS   7
#define CELL_W     28
#define CELL_H     26
#define GRID_X     14
#define GRID_Y     24
#define FRAME_US   33333

/* ── Crops ───────────────────────────────────────────────────────────── */
enum { CROP_WHEAT=0, CROP_CARROT, CROP_TOMATO, CROP_PUMPKIN, CROP_COUNT };
static const char *crop_names[] = { "Wheat","Carrot","Tomato","Pumpkin" };
static const int grow_time[]  = { 150, 250, 400, 600 };
static const int sell_value[] = {   5,  12,  25,  50 };
static const int seed_cost[]  = {   0,   5,  10,  25 };

/* ── Cell states ─────────────────────────────────────────────────────── */
enum { ST_GRASS=0, ST_TILLED, ST_PLANTED, ST_WATERED, ST_GROWING, ST_READY };
typedef struct { uint8_t state, crop; int grow_timer; } cell_t;
static cell_t farm[GRID_ROWS][GRID_COLS];

/* ── Game state ──────────────────────────────────────────────────────── */
static int cursor_x, cursor_y, sel_seed;
static int gold, day, day_timer, harvested;
static int game_over, exit_to_supervisor, restart_game;
#define DAY_LEN 900
#define MAX_DAYS 30

/* ── PRNG ────────────────────────────────────────────────────────────── */
static uint32_t rng=54321;
static int rng_next(int mod){rng=rng*1103515245+12345;return(int)((rng>>16)&0x7FFF)%mod;}

/* ── Init ────────────────────────────────────────────────────────────── */
static void init_game(void){
    cursor_x=0;cursor_y=0;sel_seed=CROP_WHEAT;gold=10;day=1;day_timer=0;harvested=0;game_over=0;
    for(int r=0;r<GRID_ROWS;r++)for(int c=0;c<GRID_COLS;c++){farm[r][c].state=ST_GRASS;farm[r][c].crop=0;farm[r][c].grow_timer=0;}
}

/* ── Drawing ─────────────────────────────────────────────────────────── */
static void draw_cell(int col, int row) {
    int sx=GRID_X+col*CELL_W+1, sy=GRID_Y+row*CELL_H+1;
    int cw=CELL_W-2, ch=CELL_H-2;
    int cx=GRID_X+col*CELL_W+CELL_W/2, cy=GRID_Y+row*CELL_H+CELL_H/2;
    cell_t *c=&farm[row][col];

    /* Background — density reflects moisture */
    int dens=1;
    if(c->state>=ST_TILLED&&c->state<ST_WATERED) dens=2;
    if(c->state>=ST_WATERED) dens=3;
    draw_grey(sx, sy, cw, ch, dens);

    /* Crop sprite */
    switch(c->state){
    case ST_PLANTED:
        display_rect(cx-1,cy,3,3,COL_BG);
        break;
    case ST_WATERED:
        display_rect(cx-1,cy,3,3,COL_BG);
        display_pixel(cx+4,cy-3,COL_BG); display_pixel(cx+5,cy-2,COL_BG);
        break;
    case ST_GROWING:
        display_vline(cx,cy+4-8,8,COL_BG);
        display_hline(cx-3,cy-1,3,COL_BG);
        display_hline(cx+1,cy+1,3,COL_BG);
        break;
    case ST_READY:
        switch(c->crop){
        case CROP_WHEAT:
            display_vline(cx,cy-6,10,COL_BG);
            display_hline(cx-3,cy-6,4,COL_BG); display_hline(cx-2,cy-4,3,COL_BG); display_hline(cx+1,cy-5,3,COL_BG);
            break;
        case CROP_CARROT:
            display_triangle_fill(cx-4,cy-6,cx+4,cy-6,cx,cy+4,COL_BG);
            display_vline(cx-2,cy-9,4,COL_BG); display_vline(cx,cy-10,5,COL_BG); display_vline(cx+2,cy-9,4,COL_BG);
            break;
        case CROP_TOMATO:
            display_circle_fill(cx,cy+1,5,COL_BG);
            display_vline(cx,cy-5,3,COL_BG); display_hline(cx-2,cy-6,5,COL_BG);
            break;
        case CROP_PUMPKIN:
            display_rect(cx-6,cy-3,5,8,COL_BG); display_rect(cx-2,cy-5,5,10,COL_BG); display_rect(cx+2,cy-3,5,8,COL_BG);
            display_vline(cx,cy-7,3,COL_BG);
            break;
        }
        break;
    default: break;
    }

    /* Cursor */
    if(col==cursor_x&&row==cursor_y){
        int ox=GRID_X+col*CELL_W, oy=GRID_Y+row*CELL_H;
        display_rect_outline(ox,oy,CELL_W,CELL_H,COL_WHT);
        display_pixel(ox+1,oy+1,COL_WHT); display_pixel(ox+CELL_W-2,oy+1,COL_WHT);
        display_pixel(ox+1,oy+CELL_H-2,COL_WHT); display_pixel(ox+CELL_W-2,oy+CELL_H-2,COL_WHT);
    }
}
static void draw_grid(void){
    for(int r=0;r<GRID_ROWS;r++) for(int c=0;c<GRID_COLS;c++) draw_cell(c,r);
    for(int c=0;c<=GRID_COLS;c++) display_vline(GRID_X+c*CELL_W,GRID_Y,GRID_ROWS*CELL_H,COL_BG);
    for(int r=0;r<=GRID_ROWS;r++) display_hline(GRID_X,GRID_Y+r*CELL_H,GRID_COLS*CELL_W,COL_BG);
}
static void draw_hud(void){
    draw_grey(0,0,SCR_W,22,2);
    display_text(4,4,"GOLD:",COL_BG);  display_number(42,4,gold,COL_BG);
    display_text(96,4,"DAY:",COL_BG);  display_number(128,4,day,COL_BG);
    /* Day progress bar */
    int filled=(day_timer*50)/DAY_LEN;
    display_rect(152,6,50,8,COL_BG);
    for(int i=0;i<filled;i+=2)display_vline(152+i,6,8,COL_WHT);
    display_rect_outline(152,6,50,8,COL_BG);
    display_text(210,4,"Seed:",COL_BG); display_text(250,4,crop_names[sel_seed],COL_BG);

    draw_grey(0,SCR_H-18,SCR_W,18,2);
    int st=farm[cursor_y][cursor_x].state;
    const char *hint="B/X:Cycle seed";
    if(st==ST_GRASS) hint="A:Till  B/X:Seed";
    else if(st==ST_TILLED) hint="A:Plant  B/X:Seed";
    else if(st==ST_PLANTED) hint="A:Water  B/X:Seed";
    else if(st==ST_READY) hint="A:Harvest  B/X:Seed";
    display_text(4,SCR_H-14,hint,COL_BG);
    display_text(196,SCR_H-14,"Harvest:",COL_BG); display_number(262,SCR_H-14,harvested,COL_BG);
}

/* ── Actions ─────────────────────────────────────────────────────────── */
static void do_action(void){
    cell_t *c=&farm[cursor_y][cursor_x];
    switch(c->state){
    case ST_GRASS:   c->state=ST_TILLED; break;
    case ST_TILLED:
        if(gold>=seed_cost[sel_seed]){gold-=seed_cost[sel_seed];c->state=ST_PLANTED;c->crop=sel_seed;c->grow_timer=grow_time[sel_seed];}
        break;
    case ST_PLANTED: c->state=ST_WATERED; break;
    case ST_READY:   gold+=sell_value[c->crop];harvested++;c->state=ST_TILLED;c->crop=0;c->grow_timer=0; break;
    default: break;
    }
}
static void update_crops(void){
    for(int r=0;r<GRID_ROWS;r++)for(int c=0;c<GRID_COLS;c++){
        cell_t *cell=&farm[r][c];
        if(cell->state==ST_WATERED||cell->state==ST_GROWING){
            cell->grow_timer--;
            if(cell->grow_timer<=grow_time[cell->crop]/2&&cell->state==ST_WATERED)cell->state=ST_GROWING;
            if(cell->grow_timer<=0)cell->state=ST_READY;
        }
    }
}

/* ── Pause menu ──────────────────────────────────────────────────────── */
enum{MN_RESUME=0,MN_RESTART,MN_EXIT,MN_N};
static const char*mlbl[]={"Resume","Restart","Exit"};
static int pause_menu(void){
    int cur=0,pu=1,pd=1,pa=1,ps=1;
    while(1){
        display_rect(80,60,160,120,COL_BG);display_rect_outline(80,60,160,120,COL_WHT);
        draw_grey(82,62,156,16,2);display_text(130,66,"PAUSED",COL_BG);
        for(int i=0;i<MN_N;i++){int iy=87+i*18;if(i==cur){display_rect(82,iy-2,156,16,COL_WHT);display_text(120,iy,mlbl[i],COL_BG);}else{display_rect(82,iy-2,156,16,COL_BG);display_text(120,iy,mlbl[i],COL_WHT);}}
        display_flush();
        int u=gpio_read(BTN_UP),d=gpio_read(BTN_DOWN),a=gpio_read(BTN_A),s=gpio_read(BTN_SETTINGS);
        if(s&&!ps)return MN_RESUME;
        if(u&&!pu)cur=(cur>0)?cur-1:MN_N-1;
        if(d&&!pd)cur=(cur<MN_N-1)?cur+1:0;
        if(a&&!pa)return cur;
        pu=u;pd=d;pa=a;ps=s;delay(20000);
    }
}

/* ── Input ───────────────────────────────────────────────────────────── */
static int pv_u,pv_d,pv_l,pv_r,pv_a,pv_b,pv_x,pv_y,pv_s;
static int handle_input(void){
    int u=gpio_read(BTN_UP),d=gpio_read(BTN_DOWN),l=gpio_read(BTN_LEFT),r=gpio_read(BTN_RIGHT);
    int a=gpio_read(BTN_A),b=gpio_read(BTN_B),x=gpio_read(BTN_X),y=gpio_read(BTN_Y);
    int s=gpio_read(BTN_SETTINGS);
    if(u&&!pv_u&&cursor_y>0)          cursor_y--;
    if(d&&!pv_d&&cursor_y<GRID_ROWS-1)cursor_y++;
    if(l&&!pv_l&&cursor_x>0)          cursor_x--;
    if(r&&!pv_r&&cursor_x<GRID_COLS-1)cursor_x++;
    if(a&&!pv_a) do_action();
    if((b&&!pv_b)||(x&&!pv_x)) sel_seed=(sel_seed+1)%CROP_COUNT;
    if(y&&!pv_y) sel_seed=(sel_seed+CROP_COUNT-1)%CROP_COUNT;
    if(s&&!pv_s){pv_s=s;return 1;}
    pv_u=u;pv_d=d;pv_l=l;pv_r=r;pv_a=a;pv_b=b;pv_x=x;pv_y=y;pv_s=s;return 0;
}
static void full_redraw(void){display_clear(COL_BG);draw_hud();draw_grid();display_flush();}
static void game_loop(void){
    full_redraw();
    while(!game_over){
        if(handle_input()){int ch=pause_menu();if(ch==MN_EXIT){exit_to_supervisor=1;return;}if(ch==MN_RESTART){restart_game=1;return;}full_redraw();}
        update_crops();
        day_timer++;if(day_timer>=DAY_LEN){day_timer=0;day++;if(day>MAX_DAYS){game_over=1;break;}}
        draw_hud();draw_grid();display_flush();delay(FRAME_US);
    }
}

/* ── Entry point ─────────────────────────────────────────────────────── */
int main(void){
    printf("AkiraOS Pixel Farmer v2.0");

    gpio_configure(BTN_UP,       GPIO_INPUT|GPIO_PULL_UP|GPIO_ACTIVE_LOW);
    gpio_configure(BTN_DOWN,     GPIO_INPUT|GPIO_PULL_UP|GPIO_ACTIVE_LOW);
    gpio_configure(BTN_LEFT,     GPIO_INPUT|GPIO_PULL_UP|GPIO_ACTIVE_LOW);
    gpio_configure(BTN_RIGHT,    GPIO_INPUT|GPIO_PULL_UP|GPIO_ACTIVE_LOW);
    gpio_configure(BTN_A,        GPIO_INPUT|GPIO_PULL_UP|GPIO_ACTIVE_LOW);
    gpio_configure(BTN_B,        GPIO_INPUT|GPIO_PULL_UP|GPIO_ACTIVE_LOW);
    gpio_configure(BTN_X,        GPIO_INPUT|GPIO_PULL_UP|GPIO_ACTIVE_LOW);
    gpio_configure(BTN_Y,        GPIO_INPUT|GPIO_PULL_UP|GPIO_ACTIVE_LOW);
    gpio_configure(BTN_SETTINGS, GPIO_INPUT|GPIO_PULL_UP|GPIO_ACTIVE_LOW);

    /* Title */
    display_clear(COL_BG);
    draw_grey(0,0,SCR_W,22,2); draw_grey(0,SCR_H-22,SCR_W,22,2);
    display_text_large(52,48,"PIXEL", COL_WHT);
    display_text_large(44,82,"FARMER",COL_WHT);
    /* Crop previews */
    /* Wheat */ display_vline(48,136,10,COL_WHT);display_hline(45,136,4,COL_WHT);display_hline(46,138,3,COL_WHT);display_hline(49,137,3,COL_WHT); display_text(54,142,"Wheat+5g",COL_WHT);
    /* Carrot */ display_triangle_fill(96,130,104,130,100,146,COL_WHT);display_vline(98,126,5,COL_WHT);display_vline(100,125,6,COL_WHT);display_vline(102,126,5,COL_WHT); display_text(108,142,"Carrot+12g",COL_WHT);
    /* Tomato */ display_circle_fill(155,138,6,COL_WHT);display_vline(155,130,4,COL_WHT); display_text(164,142,"Tomato+25g",COL_WHT);
    /* Pumpkin */ display_rect(208,133,5,8,COL_WHT);display_rect(212,131,5,10,COL_WHT);display_rect(216,133,5,8,COL_WHT);display_vline(214,129,3,COL_WHT); display_text(224,142,"Pump+50g",COL_WHT);
    display_hline(20,160,280,COL_WHT);
    display_text(52,168,"A:Till/Plant/Water/Harvest",COL_WHT);
    display_text(68,184,"B/X:Next seed  Y:Prev seed",COL_WHT);
    display_text(88,202,"Press A to start",COL_WHT);
    display_flush();

    uint32_t seed=1;
    while(!gpio_read(BTN_A)&&!gpio_read(BTN_B)){seed++;delay(20000);}
    rng=seed^0xDEADBEEF; rng_next(7);rng_next(7);rng_next(7); delay(100000);

    while(1){
        restart_game=0;exit_to_supervisor=0;init_game();game_loop();
        if(exit_to_supervisor){app_switch("supervisor");return 0;}
        if(restart_game){seed++;rng=seed^0xDEADBEEF;continue;}
        break;
    }

    /* End screen */
    display_clear(COL_BG);
    display_rect_outline(20,20,280,200,COL_WHT);draw_grey(22,22,276,28,2);
    display_text_large(44,26,"SEASON OVER",COL_BG);display_hline(20,54,280,COL_WHT);
    display_text(80,70,"GOLD:",     COL_WHT);display_number(130,70, gold,     COL_WHT);
    display_text(80,90,"HARVESTED:",COL_WHT);display_number(178,90, harvested,COL_WHT);
    display_text(80,110,"DAYS:",    COL_WHT);display_number(125,110,day,      COL_WHT);
    display_hline(20,130,280,COL_WHT);
    if(harvested>=50){draw_grey(22,134,276,16,2);display_text(72,136,"MASTER FARMER! 50+ crops",COL_BG);}
    else if(harvested>=20){draw_grey(22,134,276,16,2);display_text(80,136,"Good harvest! 20+ crops",COL_BG);}
    display_text(76,160,"A:Play Again  SET:Exit",COL_WHT);
    display_flush();
    delay(5000000); printf("Season over!"); return 0;
}
