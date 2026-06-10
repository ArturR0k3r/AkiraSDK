/**
 * @file main.c
 * @brief Gravity Dash — endless runner for AkiraOS
 *        Monochrome-optimised for Sharp LS027B7DH01.
 *
 * Controls:
 *   A/B/X/Y/UP = Flip gravity   SETTINGS = Pause
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

/* ── Play area ───────────────────────────────────────────────────────── */
#define BORDER_H  16
#define PLAY_Y    BORDER_H
#define PLAY_H    (SCR_H - BORDER_H * 2)

/* ── Player ──────────────────────────────────────────────────────────── */
#define P_X  40
#define P_W  12
#define P_H  12

/* ── Physics ─────────────────────────────────────────────────────────── */
#define GRAVITY    1
#define FLIP_BOOST 4
#define MAX_VEL    6
#define FRAME_US   16666

/* ── Obstacles ───────────────────────────────────────────────────────── */
#define MAX_OBS  12
#define MAX_GEMS  6
enum { OBS_SPIKE_BOTTOM=0, OBS_SPIKE_TOP, OBS_WALL_GAP, OBS_TYPES };
typedef struct { int x,y,w,h,type,active; } obs_t;
typedef struct { int x,y,active,collected; } gem_t;
static obs_t obs[MAX_OBS];
static gem_t gems[MAX_GEMS];

/* ── Trail ───────────────────────────────────────────────────────────── */
#define TRAIL_LEN 6
static int trail_y[TRAIL_LEN];
static int trail_idx;
static void trail_push(int y) { trail_y[trail_idx]=y; trail_idx=(trail_idx+1)%TRAIL_LEN; }

/* ── Game state ──────────────────────────────────────────────────────── */
static int py, vy, grav_dir;
static int score, hi_score;
static int scroll_speed, frame_count, spawn_timer, game_over;
static int exit_to_supervisor, restart_game;
static int bg_offset;

/* ── PRNG ────────────────────────────────────────────────────────────── */
static uint32_t rng = 77777;
static int rng_next(int mod) { rng=rng*1103515245+12345; return(int)((rng>>16)&0x7FFF)%mod; }

/* ── High score ──────────────────────────────────────────────────────── */
static void load_hi(void) {
    int fd=storage_open("gd_hi.dat",STORAGE_O_READ);
    if(fd>=0){storage_read(fd,&hi_score,sizeof(hi_score));storage_close(fd);}
}
static void save_hi(void) {
    if(score>hi_score){hi_score=score;int fd=storage_open("gd_hi.dat",STORAGE_O_WRITE);if(fd>=0){storage_write(fd,&hi_score,sizeof(hi_score));storage_close(fd);}}
}

/* ── Init ────────────────────────────────────────────────────────────── */
static void init_game(void) {
    py=PLAY_Y+PLAY_H/2-P_H/2; vy=0; grav_dir=1; score=0;
    scroll_speed=3; frame_count=0; spawn_timer=0; game_over=0; bg_offset=0;
    for(int i=0;i<MAX_OBS;i++) obs[i].active=0;
    for(int i=0;i<MAX_GEMS;i++) gems[i].active=0;
    for(int i=0;i<TRAIL_LEN;i++) trail_y[i]=py; trail_idx=0;
}

/* ── Spawn ───────────────────────────────────────────────────────────── */
static void spawn_obs(void) {
    int slot=-1;
    for(int i=0;i<MAX_OBS;i++) if(!obs[i].active){slot=i;break;}
    if(slot<0) return;
    obs[slot].active=1; obs[slot].x=SCR_W; obs[slot].type=rng_next(OBS_TYPES);
    switch(obs[slot].type){
    case OBS_SPIKE_BOTTOM: obs[slot].w=16;obs[slot].h=20+rng_next(30);obs[slot].y=PLAY_Y+PLAY_H-obs[slot].h; break;
    case OBS_SPIKE_TOP:    obs[slot].w=16;obs[slot].h=20+rng_next(30);obs[slot].y=PLAY_Y; break;
    case OBS_WALL_GAP:     obs[slot].w=12;obs[slot].h=PLAY_H;obs[slot].y=PLAY_Y; break;
    }
    if(rng_next(3)==0) for(int i=0;i<MAX_GEMS;i++) if(!gems[i].active){
        gems[i].x=SCR_W+30+rng_next(40);gems[i].y=PLAY_Y+20+rng_next(PLAY_H-40);gems[i].active=1;gems[i].collected=0;break;
    }
}
static int ovlp(int ax,int ay,int aw,int ah,int bx,int by,int bw,int bh){
    return ax<bx+bw&&ax+aw>bx&&ay<by+bh&&ay+ah>by;
}

/* ── Input ───────────────────────────────────────────────────────────── */
static int pv_a,pv_b,pv_x,pv_y,pv_u,pv_s;
static int handle_input(void) {
    int a=gpio_read(BTN_A),b=gpio_read(BTN_B),x=gpio_read(BTN_X),y=gpio_read(BTN_Y);
    int u=gpio_read(BTN_UP),s=gpio_read(BTN_SETTINGS);
    if((a&&!pv_a)||(b&&!pv_b)||(x&&!pv_x)||(y&&!pv_y)||(u&&!pv_u)){grav_dir=-grav_dir;vy=-grav_dir*FLIP_BOOST;}
    if(s&&!pv_s){pv_s=s;return 1;}
    pv_a=a;pv_b=b;pv_x=x;pv_y=y;pv_u=u;pv_s=s;return 0;
}

/* ── Update ──────────────────────────────────────────────────────────── */
static void update(void) {
    frame_count++;
    if(frame_count%3==0) score++;
    if(frame_count%600==0&&scroll_speed<8) scroll_speed++;
    bg_offset=(bg_offset+1)%40;
    vy+=grav_dir*GRAVITY; if(vy>MAX_VEL)vy=MAX_VEL; if(vy<-MAX_VEL)vy=-MAX_VEL;
    py+=vy;
    if(py<PLAY_Y){py=PLAY_Y;vy=0;} if(py+P_H>PLAY_Y+PLAY_H){py=PLAY_Y+PLAY_H-P_H;vy=0;}
    trail_push(py);
    for(int i=0;i<MAX_OBS;i++){
        if(!obs[i].active) continue;
        obs[i].x-=scroll_speed; if(obs[i].x+obs[i].w<0){obs[i].active=0;continue;}
        if(obs[i].type==OBS_WALL_GAP){
            int gy=PLAY_Y+PLAY_H/2-25,gh=50;
            if(ovlp(P_X,py,P_W,P_H,obs[i].x,PLAY_Y,obs[i].w,gy-PLAY_Y)||
               ovlp(P_X,py,P_W,P_H,obs[i].x,gy+gh,obs[i].w,PLAY_Y+PLAY_H-gy-gh)) game_over=1;
        } else { if(ovlp(P_X,py,P_W,P_H,obs[i].x,obs[i].y,obs[i].w,obs[i].h)) game_over=1; }
    }
    for(int i=0;i<MAX_GEMS;i++){
        if(!gems[i].active) continue;
        gems[i].x-=scroll_speed; if(gems[i].x<-10){gems[i].active=0;continue;}
        if(!gems[i].collected&&ovlp(P_X,py,P_W,P_H,gems[i].x,gems[i].y,8,8)){gems[i].collected=1;gems[i].active=0;score+=25;}
    }
    spawn_timer++; int sp=50-scroll_speed*3; if(sp<20)sp=20; if(spawn_timer>=sp){spawn_timer=0;spawn_obs();}
}

/* ── Drawing ─────────────────────────────────────────────────────────── */
static void draw_bg(void) {
    draw_grey(0, 0,        SCR_W, BORDER_H, 2);
    draw_grey(0, SCR_H-BORDER_H, SCR_W, BORDER_H, 2);
    display_rect(0, PLAY_Y, SCR_W, PLAY_H, COL_BG);
    /* Scrolling dot grid */
    for(int x=(SCR_W-bg_offset)%40;x<SCR_W;x+=40)
        for(int y=PLAY_Y+4;y<PLAY_Y+PLAY_H;y+=4) display_pixel(x,y,COL_WHT);
    display_hline(0, PLAY_Y,           SCR_W, COL_WHT);
    display_hline(0, PLAY_Y+PLAY_H-1,  SCR_W, COL_WHT);
}
static void draw_trail(void) {
    for(int i=0;i<TRAIL_LEN;i++){
        int idx=(trail_idx+i)%TRAIL_LEN, tx=P_X-(TRAIL_LEN-i)*5;
        if(tx<=0) continue;
        if(i>=TRAIL_LEN-2) display_rect(tx,trail_y[idx]+P_H/2-1,3,3,COL_WHT);
        else if(i>=TRAIL_LEN-4) display_pixel(tx+1,trail_y[idx]+P_H/2,COL_WHT);
    }
}
static void draw_player(void) {
    int cx=P_X+P_W/2, cy2=py+P_H/2;
    display_rect(P_X,py,P_W,P_H,COL_WHT);
    display_rect_outline(P_X,py,P_W,P_H,COL_BG);
    /* Arrow pointing in gravity direction */
    if(grav_dir>0) display_triangle_fill(cx-4,py+P_H+1,cx+4,py+P_H+1,cx,py+P_H+7,COL_WHT);
    else            display_triangle_fill(cx-4,py-2,    cx+4,py-2,    cx,py-8,     COL_WHT);
    display_pixel(cx,cy2,COL_BG);
}
static void draw_spike(int x,int y,int w,int h,int from_top){
    if(from_top){draw_grey(x,y,w,h-6,3);display_triangle_fill(x,y+h-6,x+w,y+h-6,x+w/2,y+h,COL_WHT);}
    else        {draw_grey(x,y+6,w,h-6,3);display_triangle_fill(x,y+6,x+w,y+6,x+w/2,y,COL_WHT);}
    display_hline(x+2,from_top?(y+h-8):(y+8),w-4,COL_BG);
}
static void draw_obstacles(void) {
    for(int i=0;i<MAX_OBS;i++){
        if(!obs[i].active) continue;
        if(obs[i].type==OBS_WALL_GAP){
            int gy=PLAY_Y+PLAY_H/2-25,gh=50;
            draw_grey(obs[i].x,PLAY_Y,obs[i].w,gy-PLAY_Y,3);
            draw_grey(obs[i].x,gy+gh,obs[i].w,PLAY_Y+PLAY_H-gy-gh,3);
        } else draw_spike(obs[i].x,obs[i].y,obs[i].w,obs[i].h,obs[i].type==OBS_SPIKE_TOP);
    }
}
static void draw_gems(void) {
    for(int i=0;i<MAX_GEMS;i++){
        if(!gems[i].active||gems[i].collected) continue;
        int gx=gems[i].x,gy=gems[i].y;
        display_line(gx+4,gy,    gx+8,gy+4, COL_WHT);
        display_line(gx+8,gy+4,  gx+4,gy+8, COL_WHT);
        display_line(gx+4,gy+8,  gx,  gy+4, COL_WHT);
        display_line(gx,  gy+4,  gx+4,gy,   COL_WHT);
        display_pixel(gx+4,gy+4,COL_WHT);
    }
}
static void draw_hud(void) {
    draw_grey(0,0,SCR_W,BORDER_H,2);
    display_text(4,2,"SCORE",COL_BG); display_number(46,2,score,COL_BG);
    display_text(SCR_W-84,2,"BEST",COL_BG); display_number(SCR_W-48,2,hi_score,COL_BG);
}

/* ── Pause menu ──────────────────────────────────────────────────────── */
enum {MN_RESUME=0,MN_RESTART,MN_EXIT,MN_N};
static const char *mlbl[]={"Resume","Restart","Exit"};
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

/* ── Entry point ─────────────────────────────────────────────────────── */
int main(void) {
    printf("AkiraOS Gravity Dash v2.0");
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

    /* Title */
    display_clear(COL_BG);
    draw_grey(0,0,SCR_W,18,2); draw_grey(0,SCR_H-18,SCR_W,18,2);
    display_rect(50,100,P_W,P_H,COL_WHT); display_rect_outline(50,100,P_W,P_H,COL_BG);
    display_triangle_fill(50+P_W/2-4,100+P_H+1,50+P_W/2+4,100+P_H+1,50+P_W/2,100+P_H+7,COL_WHT);
    draw_grey(30,50,130,22,2); display_text_large(32,52,"GRAVITY",COL_BG);
    display_text_large(80,80,"DASH",COL_WHT);
    display_text(48,128,"Flip gravity to survive",COL_WHT);
    display_text(68,148,"A/B/X/Y/UP = flip",COL_WHT);
    display_hline(40,163,240,COL_WHT);
    display_text(76,170,"Diamonds = +25 pts",COL_WHT);
    display_text(88,192,"Press A to start",COL_WHT);
    display_flush();

    uint32_t seed=1;
    while(!gpio_read(BTN_A)&&!gpio_read(BTN_B)){seed++;delay(20000);}
    rng=seed^0xBEEFCAFE; rng_next(7);rng_next(7);rng_next(7); delay(100000);

    while(1){
        restart_game=0; exit_to_supervisor=0; init_game();
        while(!game_over){
            if(handle_input()){int ch=pause_menu();if(ch==MN_EXIT){exit_to_supervisor=1;break;}if(ch==MN_RESTART){restart_game=1;break;}}
            update();
            draw_bg();draw_trail();draw_obstacles();draw_gems();draw_player();draw_hud();
            display_flush();delay(FRAME_US);
        }
        save_hi();
        if(exit_to_supervisor){app_switch("supervisor");return 0;}
        if(restart_game){seed++;rng=seed^0xBEEFCAFE;continue;}

        /* Death screen */
        display_clear(COL_BG);
        display_rect_outline(20,20,280,200,COL_WHT);
        draw_grey(22,22,276,28,2); display_text_large(68,26,"GAME OVER",COL_BG);
        display_hline(20,54,280,COL_WHT);
        display_text(80,70,"SCORE:",COL_WHT); display_number(135,70,score,COL_WHT);
        if(score>=hi_score){draw_grey(80,88,160,14,2);display_text(92,90,"NEW HIGH SCORE!",COL_BG);}
        else{display_text(80,90,"BEST:",COL_WHT);display_number(130,90,hi_score,COL_WHT);}
        display_hline(20,110,280,COL_WHT);
        display_text(68,124,"A:Retry   SETTINGS:Exit",COL_WHT);
        display_flush();

        int pa2=1,ps2=1;
        while(1){int a=gpio_read(BTN_A),b=gpio_read(BTN_B),s=gpio_read(BTN_SETTINGS);
            if((a&&!pa2)||(b&&!pa2)){restart_game=1;break;}
            if(s&&!ps2){exit_to_supervisor=1;break;}
            pa2=a;ps2=s;delay(20000);}
        if(exit_to_supervisor){app_switch("supervisor");return 0;}
        seed++;rng=seed^0xBEEFCAFE;delay(100000);
    }
    return 0;
}
