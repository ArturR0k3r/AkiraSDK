/*
 * Copyright (c) 2026 PenEngineering S.R.L
 * SPDX-License-Identifier: Apache-2.0
 * Tetris for AkiraConsole
 *
 * v2.0 — Sharp-mono support, ghost piece, line-clear flash,
 *         dynamic layout, board borders with tick marks
 *
 * Controls:
 *   UP / A / B   — rotate CW
 *   LEFT / RIGHT — move
 *   DOWN         — soft-drop
 *   SETTINGS     — pause / resume
 */

#include "akira_api.h"

/* ── Pins ──────────────────────────────────────────────────────────────── */
#define PIN_UP       4
#define PIN_DOWN     5
#define PIN_LEFT     7
#define PIN_RIGHT    6
#define PIN_A        15
#define PIN_B        16
#define PIN_SETTINGS 0   /* active-LOW pull-up */

/* ── Layout — computed in main() from screen size ──────────────────────── */
#define CELL     10
#define GCOLS    10
#define GROWS    20
static int BX, BY=20, SX;   /* board origin, sidebar x */
static int SCR_W=320;
static int g_mono=0;

/* ── Colours ─────────────────────────────────────────────────────────── */
/* All usable on both LCD and Sharp mono (with INVERT_COLORS=y) */
#define C_BG    0x0000u
#define C_BDR   0xFFFFu   /* white border (black on Sharp) */
#define C_GRID  0x2104u   /* very dark — invisible on mono, dim on LCD */
#define C_WHT   0xFFFFu
#define C_DIM   0x8410u
#define C_CYAN  0x07FFu
#define C_GHOST 0x4228u   /* dark gray — outline-only on mono */

/* Piece colours (LCD) — map to white on mono (> luma threshold) */
static const uint16_t PC[7] = {
    0x07FFu,  /* I cyan   */
    0xFFE0u,  /* O yellow */
    0xA81Fu,  /* T purple */
    0x07E0u,  /* S green  */
    0xF800u,  /* Z red    */
    0x211Fu,  /* J blue   */
    0xFD20u   /* L orange */
};

/* 16-bit 4×4 shape masks */
static const uint16_t SHAPES[7][4] = {
    {0x0F00u,0x2222u,0x00F0u,0x4444u}, /* I */
    {0x6600u,0x6600u,0x6600u,0x6600u}, /* O */
    {0x0E40u,0x4C40u,0x4E00u,0x4640u}, /* T */
    {0x06C0u,0x4620u,0x06C0u,0x4620u}, /* S */
    {0x0C60u,0x2640u,0x0C60u,0x2640u}, /* Z */
    {0x44C0u,0x8E00u,0x6440u,0x0E20u}, /* J */
    {0x4460u,0x0E80u,0xC440u,0x2E00u}, /* L */
};

static int sbit(int p,int r,int row,int col){
    return (SHAPES[p][r]>>(15-row*4-col))&1;
}

/* ── Button debounce ─────────────────────────────────────────────────── */
#define DB_TICKS 4u

typedef struct { uint8_t raw,cnt,state,prev; } Btn;

static void btn_poll(Btn *b, int level) {
    uint8_t r = level ? 1u : 0u;
    b->prev = b->state;
    if (r != b->raw) { b->raw = r; b->cnt = 0u; }
    else if (b->cnt < DB_TICKS) { if (++b->cnt >= DB_TICKS) b->state = b->raw; }
}

#define BTN_ROSE(b)  (!(b).prev && (b).state)
#define BTN_HELD(b)  ((b).state)

static Btn btn_up, btn_dn, btn_l, btn_r, btn_a, btn_b, btn_s;

/* ── DAS ─────────────────────────────────────────────────────────────── */
#define DAS_INIT 12
#define DAS_RPT   4
static int l_das, r_das, dn_held;

/* ── Game state ──────────────────────────────────────────────────────── */
static uint8_t board[GROWS][GCOLS];
static int cp,cr,cx,cy, np;
static int score, lines, level, game_over;
static uint32_t rng;
static int pp,pr,px,py,piece_drawn;

static int rng7(void){
    rng=rng*1664525u+1013904223u;
    return(int)((rng>>16)&0x7FFFu)%7;
}

static int hit(int p,int r,int x,int y){
    for(int row=0;row<4;row++) for(int col=0;col<4;col++){
        if(!sbit(p,r,row,col)) continue;
        int bx=x+col, by=y+row;
        if(bx<0||bx>=GCOLS||by>=GROWS) return 1;
        if(by>=0&&board[by][bx]) return 1;
    } return 0;
}

/* ── Ghost piece: drop simulation ────────────────────────────────────── */
static int ghost_y(void){
    int gy=cy;
    while(!hit(cp,cr,cx,gy+1)) gy++;
    return gy;
}

/* ── Cell rendering ──────────────────────────────────────────────────── */
static void dcell(int col, int row, uint16_t color, int ghost) {
    int px2=BX+col*CELL, py2=BY+row*CELL;
    if(color && !ghost) {
        /* Filled piece cell with highlight edge */
        display_rect(px2,   py2,   CELL-1, CELL-1, color);
        display_rect(px2,   py2,   CELL-2, 1,       C_WHT);  /* top highlight */
        display_rect(px2,   py2,   1,       CELL-2, C_WHT);  /* left highlight */
    } else if(ghost) {
        /* Ghost: outline only */
        display_rect(px2, py2, CELL-1, CELL-1, C_BG);        /* clear */
        display_rect(px2, py2, CELL-1, 1,      C_WHT);       /* top */
        display_rect(px2, py2+CELL-2, CELL-1, 1, C_WHT);     /* bottom */
        display_rect(px2, py2, 1,      CELL-1, C_WHT);       /* left */
        display_rect(px2+CELL-2, py2, 1, CELL-1, C_WHT);     /* right */
    } else {
        /* Empty: background + subtle grid tick marks on corners */
        display_rect(px2, py2, CELL-1, CELL-1, C_BG);
        /* 1×1 corner dots for grid visibility (works on mono too) */
        display_rect(px2, py2, 1, 1, C_DIM);
    }
}

static void draw_ghost(void) {
    int gy=ghost_y();
    if(gy==cy) return; /* ghost at same pos = no useful indicator */
    for(int row=0;row<4;row++) for(int col=0;col<4;col++){
        if(!sbit(cp,cr,row,col)) continue;
        int by2=gy+row; if(by2<0||by2>=GROWS) continue;
        /* Don't draw ghost over live piece */
        if(by2==cy+row) continue;
        dcell(cx+col, by2, PC[cp], 1);
    }
}

static void update_piece(void){
    if(piece_drawn){
        for(int row=0;row<4;row++) for(int col=0;col<4;col++){
            if(!sbit(pp,pr,row,col)) continue;
            int by2=py+row; if(by2<0||by2>=GROWS) continue;
            int bx2=px+col;
            if(!board[by2][bx2]) dcell(bx2,by2,0,0);
        }
        /* Re-clear ghost area of previous position */
        int old_gy=ghost_y(); /* approximate — just clear near old pos */
        (void)old_gy;
    }
    draw_ghost();
    for(int row=0;row<4;row++) for(int col=0;col<4;col++){
        if(!sbit(cp,cr,row,col)) continue;
        int by2=cy+row; if(by2<0||by2>=GROWS) continue;
        dcell(cx+col,by2,PC[cp],0);
    }
    pp=cp; pr=cr; px=cx; py=cy; piece_drawn=1;
}

/* ── Number → string ────────────────────────────────────────────────── */
static char nbuf[16];
static const char *n2s(int v){
    int i=0; if(!v){nbuf[0]='0';nbuf[1]='\0';return nbuf;}
    int n=v;
    while(n){nbuf[i++]='0'+n%10;n/=10;}
    for(int a=0,b=i-1;a<b;a++,b--){char t=nbuf[a];nbuf[a]=nbuf[b];nbuf[b]=t;}
    nbuf[i]='\0'; return nbuf;
}

/* ── Sidebar ─────────────────────────────────────────────────────────── */
static void draw_sidebar(void){
    int x=SX, sw=SCR_W-x-2;
    display_rect(x, BY, sw, GROWS*CELL, C_BG);

    /* ── NEXT box ── */
    display_text(x+4, BY,    "NEXT", C_DIM);
    display_rect(x, BY+14, 46, 46, C_BG);
    display_rect(x, BY+14, 46, 1, C_WHT);   /* box top */
    display_rect(x, BY+59, 46, 1, C_WHT);   /* box bottom */
    display_rect(x, BY+14, 1, 46, C_WHT);   /* box left */
    display_rect(x+45, BY+14, 1, 46, C_WHT);/* box right */
    for(int row=0;row<4;row++) for(int col=0;col<4;col++){
        if(!sbit(np,0,row,col)) continue;
        display_rect(x+col*10+3, BY+16+row*10, 9, 9, g_mono?C_WHT:PC[np]);
    }

    /* ── Score box ── */
    int sy=BY+68;
    display_text(x+4, sy, "SCORE", C_DIM);
    display_rect(x, sy+14, sw, 16, C_BG);
    display_rect(x, sy+14, sw, 1, C_WHT);
    display_text(x+4, sy+16, n2s(score), C_WHT);

    /* ── Lines ── */
    display_text(x+4, sy+34, "LINES", C_DIM);
    display_rect(x, sy+48, sw, 1, C_WHT);
    display_text(x+4, sy+50, n2s(lines), C_WHT);

    /* ── Level ── */
    display_text(x+4, sy+68, "LEVEL", C_DIM);
    display_rect(x, sy+82, sw, 1, C_WHT);
    display_text_large(x+4, sy+84, n2s(level), C_WHT);

    /* ── Controls hint ── */
    if(sy+110 < BY+GROWS*CELL-20){
        display_text(x+2, BY+GROWS*CELL-18, "A=rot", C_DIM);
        display_text(x+2, BY+GROWS*CELL-8,  "SET=pause", C_DIM);
    }
}

static void redraw_board(void){
    for(int r=0;r<GROWS;r++) for(int c=0;c<GCOLS;c++)
        dcell(c,r, board[r][c]?PC[board[r][c]-1]:0, 0);
    piece_drawn=0;
}

/* ── Line-clear flash animation ─────────────────────────────────────── */
static void flash_rows(int *rows, int n) {
    for(int f=0;f<3;f++){
        for(int i=0;i<n;i++){
            int r=rows[i];
            display_rect(BX, BY+r*CELL, GCOLS*CELL-1, CELL-1, (f&1)?C_WHT:C_BG);
        }
        display_flush();
        delay(60000);
    }
}

/* ── Lock + clear ────────────────────────────────────────────────────── */
static void lock_piece(void){
    for(int row=0;row<4;row++) for(int col=0;col<4;col++){
        if(!sbit(cp,cr,row,col)) continue;
        int by2=cy+row; if(by2>=0) board[by2][cx+col]=(uint8_t)(cp+1);
    }
}

static int clear_lines(void){
    static const int pts[5]={0,100,300,500,800};
    int full_rows[4], n=0;
    for(int r=GROWS-1;r>=0;r--){
        int full=1;
        for(int c=0;c<GCOLS;c++) if(!board[r][c]){full=0;break;}
        if(!full) continue;
        full_rows[n++]=r;
    }
    if(!n) return 0;
    flash_rows(full_rows, n);
    /* Now remove them */
    for(int i=0;i<n;i++){
        int r=full_rows[i];
        for(int rr=r;rr>0;rr--) for(int c=0;c<GCOLS;c++) board[rr][c]=board[rr-1][c];
        for(int c=0;c<GCOLS;c++) board[0][c]=0;
        /* Adjust remaining full row indices */
        for(int j=i+1;j<n;j++) if(full_rows[j]<r) full_rows[j]++;
    }
    score+=pts[n]*level; lines+=n;
    level=lines/10+1; if(level>15) level=15;
    return n;
}

static void spawn(void){
    cp=np; np=rng7(); cr=0; cx=GCOLS/2-2; cy=-1;
    if(hit(cp,cr,cx,cy)) game_over=1;
}

/* ── Board borders with tick marks ─────────────────────────────────── */
static void draw_borders(void){
    /* Left border */
    display_rect(BX-3, BY-2, 3, GROWS*CELL+4, C_BDR);
    /* Right border */
    display_rect(BX+GCOLS*CELL, BY-2, 3, GROWS*CELL+4, C_BDR);
    /* Bottom border */
    display_rect(BX-3, BY+GROWS*CELL, GCOLS*CELL+6, 3, C_BDR);
    /* Top tick marks — every 5 rows */
    for(int r=0;r<=GROWS;r+=5){
        display_rect(BX-5, BY+r*CELL, 5, 1, C_BDR);
        display_rect(BX+GCOLS*CELL, BY+r*CELL, 5, 1, C_BDR);
    }
}

/* ── Entry point ─────────────────────────────────────────────────────── */
int main(void){
    int32_t w=320, h=240;
    display_get_size(&w, &h);
    SCR_W = w;
    g_mono = (w >= 400);

    /* Centre board + sidebar on actual screen width */
    int total_w = GCOLS*CELL + 8 + 56;   /* board + gap + sidebar */
    BX = (w - total_w) / 2;
    if(BX < 4) BX = 4;
    SX = BX + GCOLS*CELL + 8;

    gpio_configure(PIN_UP,       GPIO_INPUT|GPIO_PULL_DOWN);
    gpio_configure(PIN_DOWN,     GPIO_INPUT|GPIO_PULL_DOWN);
    gpio_configure(PIN_LEFT,     GPIO_INPUT|GPIO_PULL_DOWN);
    gpio_configure(PIN_RIGHT,    GPIO_INPUT|GPIO_PULL_DOWN);
    gpio_configure(PIN_A,        GPIO_INPUT|GPIO_PULL_DOWN);
    gpio_configure(PIN_B,        GPIO_INPUT|GPIO_PULL_DOWN);
    gpio_configure(PIN_SETTINGS, GPIO_INPUT|GPIO_PULL_UP|GPIO_ACTIVE_LOW);

    /* ── Title screen ── */
    display_clear(C_BG);
    display_text_large(w/2-32, h/2-28, "TETRIS", C_CYAN);
    display_hline(w/2-36, h/2-10, 72, C_WHT);
    display_text(w/2-28, h/2+2,  "Press A to Start", C_WHT);
    display_text(w/2-28, h/2+18, "A/UP=rotate  v=drop", C_DIM);
    display_flush();

    uint32_t seed=1;
    while(!gpio_read(PIN_A)&&!gpio_read(PIN_B)) { seed++; delay(20000); }
    rng=seed^0xDEADBEEFu; delay(120000);

restart:
    for(int r=0;r<GROWS;r++) for(int c=0;c<GCOLS;c++) board[r][c]=0;
    score=0; lines=0; level=1; game_over=0; piece_drawn=0;
    np=rng7(); spawn();

    btn_up=(Btn){0}; btn_dn=(Btn){0};
    btn_l =(Btn){0}; btn_r =(Btn){0};
    btn_a =(Btn){0}; btn_b =(Btn){0};
    btn_s =(Btn){0};
    l_das=0; r_das=0; dn_held=0;

    display_clear(C_BG);
    draw_borders();
    redraw_board();
    draw_sidebar();
    display_flush();

    uint32_t drop_us=600000u, drop_acc=0;
    int paused=0;

    while(!game_over){

        btn_poll(&btn_up, gpio_read(PIN_UP));
        btn_poll(&btn_dn, gpio_read(PIN_DOWN));
        btn_poll(&btn_l,  gpio_read(PIN_LEFT));
        btn_poll(&btn_r,  gpio_read(PIN_RIGHT));
        btn_poll(&btn_a,  gpio_read(PIN_A));
        btn_poll(&btn_b,  gpio_read(PIN_B));
        btn_poll(&btn_s, !gpio_read(PIN_SETTINGS));

        if(BTN_ROSE(btn_s)){
            paused=!paused;
            if(paused){
                display_rect(BX+5, BY+88, GCOLS*CELL-10, 22, C_BG);
                display_text(BX+12, BY+95, "-- PAUSED --", C_WHT);
                display_flush();
            } else {
                redraw_board(); draw_borders(); draw_sidebar();
                piece_drawn=0;
            }
        }
        if(paused){ delay(20000); continue; }

        /* Rotate */
        if(BTN_ROSE(btn_up)||BTN_ROSE(btn_a)||BTN_ROSE(btn_b)){
            int nr=(cr+1)%4;
            if     (!hit(cp,nr,cx,  cy)) cr=nr;
            else if(!hit(cp,nr,cx-1,cy)){ cr=nr; cx--; }
            else if(!hit(cp,nr,cx+1,cy)){ cr=nr; cx++; }
        }

        /* Left */
        if(BTN_ROSE(btn_l)){ if(!hit(cp,cr,cx-1,cy)) cx--; l_das=0; }
        else if(BTN_HELD(btn_l)){
            l_das++;
            if(l_das>=DAS_INIT&&(l_das-DAS_INIT)%DAS_RPT==0)
                if(!hit(cp,cr,cx-1,cy)) cx--;
        } else l_das=0;

        /* Right */
        if(BTN_ROSE(btn_r)){ if(!hit(cp,cr,cx+1,cy)) cx++; r_das=0; }
        else if(BTN_HELD(btn_r)){
            r_das++;
            if(r_das>=DAS_INIT&&(r_das-DAS_INIT)%DAS_RPT==0)
                if(!hit(cp,cr,cx+1,cy)) cx++;
        } else r_das=0;

        /* Gravity + soft-drop */
        if(BTN_HELD(btn_dn)){ if(dn_held<4) dn_held++; }
        else dn_held=0;

        uint32_t eff=(dn_held>=4)?80000u:drop_us;
        drop_acc+=20000u;
        if(drop_acc>=eff){
            drop_acc=0;
            if(!hit(cp,cr,cx,cy+1)){
                cy++;
                if(dn_held>=4) score++;
            } else {
                update_piece();
                lock_piece();
                if(clear_lines()){
                    drop_us=600000u/(uint32_t)level;
                    if(drop_us<80000u) drop_us=80000u;
                    redraw_board();
                    draw_borders();
                }
                draw_sidebar();
                spawn();
                if(game_over) break;
            }
        }

        if(!piece_drawn||cp!=pp||cr!=pr||cx!=px||cy!=py)
            update_piece();
        display_flush();
        delay(20000);
    }

    /* ── Game over overlay ── */
    display_rect(BX+2, BY+80, GCOLS*CELL-4, 58, C_BG);
    display_rect(BX+2, BY+80, GCOLS*CELL-4, 1,  C_WHT);
    display_rect(BX+2, BY+137,GCOLS*CELL-4, 1,  C_WHT);
    display_rect(BX+2, BY+80, 1, 58, C_WHT);
    display_rect(BX+GCOLS*CELL-5,BY+80,1,58,C_WHT);
    display_text(BX+8, BY+88,  "GAME  OVER", 0xF800u);
    display_text(BX+8, BY+104, "Score:", C_DIM);
    display_text(BX+52, BY+104, n2s(score), C_WHT);
    display_text(BX+8, BY+120, "Lines:", C_DIM);
    display_text(BX+52, BY+120, n2s(lines), C_WHT);
    display_text(BX+8, BY+130, "Press A/B to restart", C_DIM);
    display_flush();
    delay(2000000u);
    while(gpio_read(PIN_A)||gpio_read(PIN_B)) delay(20000);
    while(!gpio_read(PIN_A)&&!gpio_read(PIN_B)) delay(20000);
    delay(100000u);
    goto restart;
    return 0;
}
