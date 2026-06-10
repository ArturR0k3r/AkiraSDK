/**
 * @file main.c
 * @brief Pixel Dungeon — roguelike for AkiraOS
 *        Monochrome-optimised for Sharp LS027B7DH01.
 *
 * Controls:
 *   DPAD = Move/attack   A = Use stairs   B/Y = Use potion
 *   X = Wait (skip turn)   SETTINGS = Pause
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
#define SCR_W  320
#define SCR_H  240

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

/* ── Map ─────────────────────────────────────────────────────────────── */
#define MAP_W   20
#define MAP_H   14
#define TILE_W  16
#define TILE_H  15
#define MAP_OX   0
#define MAP_OY  22
#define FRAME_US 33333

enum { T_WALL=0, T_FLOOR, T_STAIRS };
static uint8_t map[MAP_H][MAP_W];

/* ── Entities ────────────────────────────────────────────────────────── */
#define MAX_ENEMIES 12
#define MAX_ITEMS    8
typedef struct { int x,y,hp,max_hp,atk,alive,tier; } enemy_t;
enum { ITEM_GOLD=1, ITEM_POTION };
typedef struct { int x,y,type,active; } item_t;
static enemy_t enemies[MAX_ENEMIES];
static item_t  items[MAX_ITEMS];

/* ── Player ──────────────────────────────────────────────────────────── */
static int px,py,p_hp,p_max_hp,p_atk,p_def,p_xp,p_xp_next,p_level,p_gold,p_potions,p_floor;
static int game_over,exit_to_supervisor,restart_game;

/* ── Messages ────────────────────────────────────────────────────────── */
static char msg_buf[40];
static int msg_timer;
static void set_msg(const char *s){int i;for(i=0;s[i]&&i<39;i++)msg_buf[i]=s[i];msg_buf[i]='\0';msg_timer=60;}

/* ── PRNG ────────────────────────────────────────────────────────────── */
static uint32_t rng=99999;
static int rng_next(int mod){rng=rng*1103515245+12345;return(int)((rng>>16)&0x7FFF)%mod;}

/* ── Dungeon gen ─────────────────────────────────────────────────────── */
static void gen_room(int rx,int ry,int rw,int rh){for(int y=ry;y<ry+rh&&y<MAP_H;y++)for(int x=rx;x<rx+rw&&x<MAP_W;x++)map[y][x]=T_FLOOR;}
static void gen_ch(int x1,int x2,int y){int a=x1<x2?x1:x2,b=x1<x2?x2:x1;for(int x=a;x<=b&&x<MAP_W;x++)if(y>=0&&y<MAP_H)map[y][x]=T_FLOOR;}
static void gen_cv(int y1,int y2,int x){int a=y1<y2?y1:y2,b=y1<y2?y2:y1;for(int y=a;y<=b&&y<MAP_H;y++)if(x>=0&&x<MAP_W)map[y][x]=T_FLOOR;}

static void generate_map(void) {
    for(int y=0;y<MAP_H;y++) for(int x=0;x<MAP_W;x++) map[y][x]=T_WALL;
    int num=4+rng_next(3);
    int rcx[7],rcy[7];
    for(int i=0;i<num;i++){
        int rw=3+rng_next(4),rh=3+rng_next(3);
        int rx=1+rng_next(MAP_W-rw-2),ry=1+rng_next(MAP_H-rh-2);
        gen_room(rx,ry,rw,rh); rcx[i]=rx+rw/2; rcy[i]=ry+rh/2;
        if(i>0){if(rng_next(2)){gen_ch(rcx[i-1],rcx[i],rcy[i-1]);gen_cv(rcy[i-1],rcy[i],rcx[i]);}else{gen_cv(rcy[i-1],rcy[i],rcx[i-1]);gen_ch(rcx[i-1],rcx[i],rcy[i]);}}
    }
    px=rcx[0]; py=rcy[0];
    map[rcy[num-1]][rcx[num-1]]=T_STAIRS;
    int ne=3+p_floor; if(ne>MAX_ENEMIES)ne=MAX_ENEMIES;
    for(int i=0;i<MAX_ENEMIES;i++) enemies[i].alive=0;
    for(int i=0;i<ne;i++){int att=50;while(att-->0){int ex=1+rng_next(MAP_W-2),ey=1+rng_next(MAP_H-2);if(map[ey][ex]==T_FLOOR&&!(ex==px&&ey==py)){enemies[i].x=ex;enemies[i].y=ey;enemies[i].max_hp=2+p_floor+rng_next(p_floor+1);enemies[i].hp=enemies[i].max_hp;enemies[i].atk=1+p_floor/2+rng_next(2);enemies[i].alive=1;enemies[i].tier=(p_floor<=2)?0:(p_floor<=5)?1:2;break;}}}
    int ni=2+rng_next(3); if(ni>MAX_ITEMS)ni=MAX_ITEMS;
    for(int i=0;i<MAX_ITEMS;i++) items[i].active=0;
    for(int i=0;i<ni;i++){int att=50;while(att-->0){int ix=1+rng_next(MAP_W-2),iy=1+rng_next(MAP_H-2);if(map[iy][ix]==T_FLOOR&&!(ix==px&&iy==py)){items[i].x=ix;items[i].y=iy;items[i].type=(rng_next(3)==0)?ITEM_POTION:ITEM_GOLD;items[i].active=1;break;}}}
}
static void init_game(void){p_hp=20;p_max_hp=20;p_atk=3;p_def=1;p_xp=0;p_xp_next=10;p_level=1;p_gold=0;p_potions=1;p_floor=1;game_over=0;msg_buf[0]='\0';msg_timer=0;generate_map();}

/* ── Combat ──────────────────────────────────────────────────────────── */
static void check_lvlup(void){while(p_xp>=p_xp_next){p_xp-=p_xp_next;p_level++;p_max_hp+=5;p_hp=p_max_hp;p_atk++;p_def++;p_xp_next+=5+p_level*2;set_msg("LEVEL UP!");}}
static void attack_enemy(int i){enemy_t*e=&enemies[i];int d=p_atk-rng_next(2);if(d<1)d=1;e->hp-=d;if(e->hp<=0){e->alive=0;p_xp+=e->max_hp+e->atk;set_msg("Enemy slain!");check_lvlup();}else set_msg("Hit!");}
static void enemy_atk(int i){enemy_t*e=&enemies[i];int d=e->atk-p_def+rng_next(2);if(d<1)d=1;p_hp-=d;if(p_hp<=0){p_hp=0;game_over=1;set_msg("You died!");}}
static void move_enemies(void){
    for(int i=0;i<MAX_ENEMIES;i++){if(!enemies[i].alive)continue;int ex=enemies[i].x,ey=enemies[i].y;int dx=px-ex,dy=py-ey;int dist=(dx<0?-dx:dx)+(dy<0?-dy:dy);
    if(dist<=1){enemy_atk(i);continue;}if(dist>6)continue;
    int nx=ex,ny=ey;if((dx<0?-dx:dx)>=(dy<0?-dy:dy))nx+=(dx>0)?1:-1;else ny+=(dy>0)?1:-1;
    if(nx<0||nx>=MAP_W||ny<0||ny>=MAP_H||map[ny][nx]==T_WALL)continue;
    if(nx==px&&ny==py){enemy_atk(i);continue;}
    int bl=0;for(int j=0;j<MAX_ENEMIES;j++)if(j!=i&&enemies[j].alive&&enemies[j].x==nx&&enemies[j].y==ny){bl=1;break;}
    if(!bl){enemies[i].x=nx;enemies[i].y=ny;}}
}
static void try_pickup(void){for(int i=0;i<MAX_ITEMS;i++){if(!items[i].active||items[i].x!=px||items[i].y!=py)continue;if(items[i].type==ITEM_GOLD){p_gold+=3+rng_next(5)+p_floor;set_msg("Gold!");}else{p_potions++;set_msg("Potion!");}items[i].active=0;}}

/* ── Drawing ─────────────────────────────────────────────────────────── */
static void draw_tile(int x, int y) {
    int sx=MAP_OX+x*TILE_W, sy=MAP_OY+y*TILE_H;
    switch(map[y][x]){
    case T_WALL:
        draw_grey(sx,sy,TILE_W,TILE_H,3);
        display_hline(sx,sy+TILE_H/2,TILE_W,COL_BG);
        if((y&1)==0) display_vline(sx+TILE_W/2,sy,TILE_H/2,COL_BG);
        else         display_vline(sx+TILE_W/4,sy+TILE_H/2,TILE_H/2,COL_BG);
        break;
    case T_FLOOR:
        draw_grey(sx,sy,TILE_W,TILE_H,1);
        display_pixel(sx+2,sy+2,COL_WHT);
        display_pixel(sx+TILE_W-3,sy+TILE_H-3,COL_WHT);
        break;
    case T_STAIRS:
        draw_grey(sx,sy,TILE_W,TILE_H,2);
        display_hline(sx+2,sy+3,TILE_W-4,COL_WHT);
        display_hline(sx+4,sy+7,TILE_W-8,COL_WHT);
        display_hline(sx+6,sy+11,TILE_W-12,COL_WHT);
        break;
    }
}
static void draw_map(void){for(int y=0;y<MAP_H;y++)for(int x=0;x<MAP_W;x++)draw_tile(x,y);}

static void draw_items(void){
    for(int i=0;i<MAX_ITEMS;i++){if(!items[i].active)continue;
    int sx=MAP_OX+items[i].x*TILE_W,sy=MAP_OY+items[i].y*TILE_H;
    int cx=sx+TILE_W/2,cy=sy+TILE_H/2;
    if(items[i].type==ITEM_GOLD){display_line(cx,cy-4,cx+4,cy,COL_WHT);display_line(cx+4,cy,cx,cy+4,COL_WHT);display_line(cx,cy+4,cx-4,cy,COL_WHT);display_line(cx-4,cy,cx,cy-4,COL_WHT);display_pixel(cx,cy,COL_WHT);}
    else{display_hline(cx-4,cy,9,COL_WHT);display_vline(cx,cy-4,9,COL_WHT);}}
}
static void draw_enemy(int i){
    int sx=MAP_OX+enemies[i].x*TILE_W+2,sy=MAP_OY+enemies[i].y*TILE_H+2;
    int ew=TILE_W-4,eh=TILE_H-4;
    display_rect(sx,sy,ew,eh,COL_WHT);
    if(enemies[i].tier==0){display_pixel(sx+2,sy+2,COL_BG);display_pixel(sx+3,sy+2,COL_BG);display_pixel(sx+ew-4,sy+2,COL_BG);display_pixel(sx+ew-3,sy+2,COL_BG);display_pixel(sx+2,sy+eh-3,COL_BG);display_pixel(sx+ew/2,sy+eh-2,COL_BG);display_pixel(sx+ew-3,sy+eh-3,COL_BG);}
    else if(enemies[i].tier==1){display_line(sx+2,sy+2,sx+4,sy+4,COL_BG);display_line(sx+4,sy+2,sx+2,sy+4,COL_BG);display_line(sx+ew-5,sy+2,sx+ew-3,sy+4,COL_BG);display_line(sx+ew-3,sy+2,sx+ew-5,sy+4,COL_BG);display_hline(sx+2,sy+eh-3,ew-4,COL_BG);}
    else{display_hline(sx,sy+eh/2,ew,COL_BG);display_vline(sx+ew/2,sy,eh,COL_BG);}
    int bw=ew*enemies[i].hp/enemies[i].max_hp;
    display_rect(sx,sy+eh,ew,2,COL_BG);if(bw>0)display_hline(sx,sy+eh,bw,COL_WHT);
}
static void draw_player_ent(void){
    int sx=MAP_OX+px*TILE_W,sy=MAP_OY+py*TILE_H;
    int cx=sx+TILE_W/2,cy=sy+TILE_H/2,r=5;
    display_triangle_fill(cx-r,cy,cx,cy-r,cx+r,cy,COL_WHT);
    display_triangle_fill(cx-r,cy,cx,cy+r,cx+r,cy,COL_WHT);
    display_vline(cx,cy-r,r*2+1,COL_BG);
}
static void draw_entities(void){draw_items();for(int i=0;i<MAX_ENEMIES;i++)if(enemies[i].alive)draw_enemy(i);draw_player_ent();}

static void draw_hud(void){
    draw_grey(0,0,SCR_W,MAP_OY,2);
    display_text(2,5,"HP",COL_BG);
    int hw=(p_hp*60)/p_max_hp;
    display_rect(18,5,60,10,COL_BG);
    for(int i=0;i<60;i+=4){if(i<hw)display_vline(18+i,5,10,COL_WHT);}
    display_rect_outline(18,5,60,10,COL_BG);
    display_text(84,5,"XP",COL_BG);
    int xw=(p_xp*40)/p_xp_next;
    display_rect(100,5,40,10,COL_BG);
    for(int i=0;i<40;i+=2){if(i<xw)display_vline(100+i,5,10,COL_WHT);}
    display_rect_outline(100,5,40,10,COL_BG);
    display_text(148,5,"Lv",COL_BG);display_number(163,5,p_level,COL_BG);
    display_text(188,5,"F", COL_BG);display_number(198,5,p_floor,COL_BG);
    display_text(218,5,"G", COL_BG);display_number(228,5,p_gold, COL_BG);
    display_text(265,5,"P", COL_BG);display_number(275,5,p_potions,COL_BG);
    if(msg_timer>0){draw_grey(0,SCR_H-16,SCR_W,16,2);display_text(4,SCR_H-13,msg_buf,COL_BG);}
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
static int turn_taken;
static int handle_input(void){
    int u=gpio_read(BTN_UP),d=gpio_read(BTN_DOWN),l=gpio_read(BTN_LEFT),r=gpio_read(BTN_RIGHT);
    int a=gpio_read(BTN_A),b=gpio_read(BTN_B),x=gpio_read(BTN_X),y=gpio_read(BTN_Y);
    int s=gpio_read(BTN_SETTINGS);
    turn_taken=0; int nx=px,ny=py;
    if(u&&!pv_u){ny--;turn_taken=1;}else if(d&&!pv_d){ny++;turn_taken=1;}
    else if(l&&!pv_l){nx--;turn_taken=1;}else if(r&&!pv_r){nx++;turn_taken=1;}
    if(turn_taken){
        if(nx>=0&&nx<MAP_W&&ny>=0&&ny<MAP_H&&map[ny][nx]!=T_WALL){
            int hit=-1;for(int i=0;i<MAX_ENEMIES;i++)if(enemies[i].alive&&enemies[i].x==nx&&enemies[i].y==ny){hit=i;break;}
            if(hit>=0)attack_enemy(hit);else{px=nx;py=ny;try_pickup();}
        }
    }
    if(a&&!pv_a&&map[py][px]==T_STAIRS){p_floor++;generate_map();set_msg("Descending...");turn_taken=1;}
    if((b&&!pv_b)||(y&&!pv_y)){if(p_potions>0&&p_hp<p_max_hp){p_potions--;p_hp+=8+p_level*2;if(p_hp>p_max_hp)p_hp=p_max_hp;set_msg("Healed!");}}
    if((x&&!pv_x)&&!turn_taken){turn_taken=1;set_msg("Wait...");}  /* skip turn */
    if(s&&!pv_s){pv_s=s;return 1;}
    pv_u=u;pv_d=d;pv_l=l;pv_r=r;pv_a=a;pv_b=b;pv_x=x;pv_y=y;pv_s=s;return 0;
}

static void full_redraw(void){display_clear(COL_BG);draw_map();draw_entities();draw_hud();display_flush();}
static void game_loop(void){
    full_redraw();
    while(!game_over){
        if(handle_input()){int ch=pause_menu();if(ch==MN_EXIT){exit_to_supervisor=1;return;}if(ch==MN_RESTART){restart_game=1;return;}full_redraw();}
        if(turn_taken)move_enemies();
        if(msg_timer>0)msg_timer--;
        draw_map();draw_entities();draw_hud();display_flush();delay(FRAME_US);
    }
}

/* ── Entry point ─────────────────────────────────────────────────────── */
int main(void){
    printf("AkiraOS Pixel Dungeon v2.0");

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
    draw_grey(0,0,SCR_W,20,2); draw_grey(0,SCR_H-20,SCR_W,20,2);
    display_text_large(44,48,"PIXEL",  COL_WHT);
    display_text_large(28,82,"DUNGEON",COL_WHT);
    /* Sample goblin */
    display_rect(28,126,24,20,COL_WHT);display_pixel(31,129,COL_BG);display_pixel(32,129,COL_BG);display_pixel(39,129,COL_BG);display_pixel(40,129,COL_BG);display_pixel(31,143,COL_BG);display_pixel(36,144,COL_BG);display_pixel(41,143,COL_BG);
    /* Sample skeleton */
    display_rect(60,126,24,20,COL_WHT);display_line(63,129,65,131,COL_BG);display_line(65,129,63,131,COL_BG);display_line(72,129,74,131,COL_BG);display_line(74,129,72,131,COL_BG);display_hline(63,143,18,COL_BG);
    /* Sample demon */
    display_rect(96,126,24,20,COL_WHT);display_hline(96,136,24,COL_BG);display_vline(108,126,20,COL_BG);
    display_text(56,158,"Explore  Fight  Loot",COL_WHT);
    display_text(64,174,"Dpad:Move  A:Stairs",COL_WHT);
    display_text(64,190,"B/Y:Potion  X:Wait",COL_WHT);
    display_text(84,208,"Press A to start",COL_WHT);
    display_flush();

    uint32_t seed=1;
    while(!gpio_read(BTN_A)&&!gpio_read(BTN_B)){seed++;delay(20000);}
    rng=seed^0xCAFEBEEF; rng_next(7);rng_next(7);rng_next(7); delay(100000);

    while(1){
        restart_game=0;exit_to_supervisor=0;init_game();game_loop();
        if(exit_to_supervisor){app_switch("supervisor");return 0;}
        if(restart_game){seed++;rng=seed^0xCAFEBEEF;continue;}
        break;
    }

    display_clear(COL_BG);
    display_rect_outline(20,20,280,200,COL_WHT);draw_grey(22,22,276,28,2);
    display_text_large(68,26,"YOU DIED",COL_BG);display_hline(20,54,280,COL_WHT);
    display_text(80,70,"FLOOR:",COL_WHT);display_number(130,70,p_floor,COL_WHT);
    display_text(80,90,"LEVEL:",COL_WHT);display_number(130,90,p_level,COL_WHT);
    display_text(80,110,"GOLD:", COL_WHT);display_number(125,110,p_gold,COL_WHT);
    display_hline(20,130,280,COL_WHT);
    display_text(80,145,"A:Retry  SETTINGS:Exit",COL_WHT);
    display_flush();
    delay(3000000); printf("Game over!"); return 0;
}
