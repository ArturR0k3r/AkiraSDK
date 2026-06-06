/*
 * Copyright (c) 2026 PenEngineering S.R.L
 * SPDX-License-Identifier: Apache-2.0
 * Tetris for AkiraConsole — UP/A/B=rotate  LEFT/RIGHT=move  DOWN=drop
 *
 * v1.1 — button debounce + per-axis DAS
 * v1.2 — three rendering/input fixes
 *   · DB_TICKS 3→4 (80 ms): rejects longer GPIO noise; fixes spurious soft-drop
 *   · DAS_INIT 8→12, DAS_RPT 3→4: quick taps no longer trigger auto-repeat
 *   · dn_held guard: soft-drop requires DOWN confirmed held ≥4 ticks (80 ms)
 *     after debounce settles, so noise spikes can't hijack gravity speed
 *   · Dirty check on update_piece(): skips erase+redraw when piece hasn't moved,
 *     eliminating the LCD flicker that caused visible piece trembling
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

/* ── Layout — centred on 320×240 ──────────────────────────────────────── */
/*   Board  10×20 cells × 10px = 100×200px                                */
/*   Sidebar 58px  gap 6px                                                 */
/*   Total width = 100 + 6 + 58 = 164  →  left margin = (320-164)/2 = 78 */
#define CELL     10
#define COLS     10
#define ROWS     20
#define BX       78          /* board left  — board spans x: 78..177      */
#define BY       20          /* board top   — board spans y: 20..220      */
#define SX      (BX + COLS*CELL + 6)   /* sidebar x = 184                 */

/* ── Colours ─────────────────────────────────────────────────────────── */
#define C_BG    0x0000u
#define C_BDR   0x4A69u
#define C_GRID  0x18C3u
#define C_WHT   0xFFFFu
#define C_DIM   0x8410u
#define C_CYAN  0x07FFu

static const uint16_t PC[7] = {
    0x07FFu,0xFFE0u,0xA81Fu,0x07E0u,0xF800u,0x211Fu,0xFD20u
};

/* 16-bit 4×4 shape masks, bit15=top-left bit0=bottom-right */
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
/*
 * Each GPIO is polled once per 20 ms tick.  A transition is accepted only
 * after DB_TICKS consecutive reads of the new level.  This rejects contact
 * bounce spikes shorter than DB_TICKS × 20 ms = 60 ms.
 *
 *  btn_poll(&b, level)  — call every tick; level = logical 1 when pressed
 *  BTN_ROSE(b)          — true for exactly one tick on button press
 *  BTN_HELD(b)          — true every tick while button is held down
 *  BTN_FELL(b)          — true for exactly one tick on button release
 */
#define DB_TICKS 4u          /* 4 × 20 ms = 80 ms settle window           */

typedef struct {
    uint8_t raw;             /* last raw GPIO level seen (0 or 1)          */
    uint8_t cnt;             /* consecutive ticks raw has been stable      */
    uint8_t state;           /* confirmed debounced level                  */
    uint8_t prev;            /* confirmed level on the previous tick       */
} Btn;

static void btn_poll(Btn *b, int level) {
    uint8_t r = level ? 1u : 0u;
    b->prev = b->state;                   /* snapshot before any update    */
    if (r != b->raw) {
        b->raw = r;
        b->cnt = 0u;                      /* different level — reset timer */
    } else if (b->cnt < DB_TICKS) {
        if (++b->cnt >= DB_TICKS)
            b->state = b->raw;            /* stable long enough → confirm  */
    }
}

#define BTN_ROSE(b)  (!(b).prev &&  (b).state)  /* rising  edge            */
#define BTN_HELD(b)  (              (b).state)  /* held down               */
#define BTN_FELL(b)  ( (b).prev  && !(b).state) /* falling edge            */

static Btn btn_up, btn_dn, btn_l, btn_r, btn_a, btn_b, btn_s;

/* ── DAS (Delayed Auto-Shift) ────────────────────────────────────────── */
/*
 * Standard Tetris DAS: on the rising edge the piece moves immediately;
 * after DAS_INIT held ticks auto-repeat fires every DAS_RPT ticks.
 * LEFT and RIGHT each have their own counter so neither blocks the other.
 *
 *   DAS_INIT 12 × 20 ms = 240 ms  initial delay — requires deliberate hold
 *   DAS_RPT   4 × 20 ms =  80 ms  auto-repeat period
 *
 * Was DAS_INIT=8 / DAS_RPT=3: a 220 ms tap could accidentally trigger
 * auto-repeat.  At 240 ms only intentional holds reach the repeat phase.
 */
#define DAS_INIT  12
#define DAS_RPT    4

static int l_das, r_das;
static int dn_held;        /* consecutive HELD ticks of DOWN after debounce */

/* ── Game state ──────────────────────────────────────────────────────── */
static uint8_t board[ROWS][COLS];
static int cp,cr,cx,cy;   /* current piece, rotation, position */
static int np;             /* next piece                        */
static int score,lines,level;
static int game_over;
static uint32_t rng;

/* previous-drawn piece (for partial erase) */
static int pp,pr,px,py,piece_drawn;

static int rng7(void){
    rng=rng*1664525u+1013904223u;
    return(int)((rng>>16)&0x7FFFu)%7;
}

/* ── Collision ───────────────────────────────────────────────────────── */
static int hit(int p,int r,int x,int y){
    for(int row=0;row<4;row++) for(int col=0;col<4;col++){
        if(!sbit(p,r,row,col)) continue;
        int bx=x+col, by=y+row;
        if(bx<0||bx>=COLS||by>=ROWS) return 1;
        if(by>=0&&board[by][bx]) return 1;
    } return 0;
}

/* ── Draw one cell ───────────────────────────────────────────────────── */
static void dcell(int col,int row,uint16_t color){
    int px2=BX+col*CELL, py2=BY+row*CELL;
    if(color){
        display_rect(px2,py2,CELL-1,CELL-1,color);
        display_rect(px2,py2,CELL-2,1,C_WHT);
        display_rect(px2,py2,1,CELL-2,C_WHT);
    } else {
        display_rect(px2,py2,CELL-1,CELL-1,C_BG);
        display_rect(px2,py2,CELL-1,1,C_GRID);
        display_rect(px2,py2,1,CELL-1,C_GRID);
    }
}

/* Erase old piece position, draw at current position */
static void update_piece(void){
    if(piece_drawn){
        for(int row=0;row<4;row++) for(int col=0;col<4;col++){
            if(!sbit(pp,pr,row,col)) continue;
            int by2=py+row; if(by2<0||by2>=ROWS) continue;
            int bx2=px+col;
            if(!board[by2][bx2]) dcell(bx2,by2,0);
        }
    }
    for(int row=0;row<4;row++) for(int col=0;col<4;col++){
        if(!sbit(cp,cr,row,col)) continue;
        int by2=cy+row; if(by2<0||by2>=ROWS) continue;
        dcell(cx+col,by2,PC[cp]);
    }
    pp=cp; pr=cr; px=cx; py=cy; piece_drawn=1;
}

/* ── Sidebar + next piece ────────────────────────────────────────────── */
static char nbuf[12];
static const char *n2s(int v){
    int i=0; if(!v){nbuf[i++]='0';}
    else{int n=v;while(n){nbuf[i++]='0'+n%10;n/=10;}
    for(int a=0,b=i-1;a<b;a++,b--){char t=nbuf[a];nbuf[a]=nbuf[b];nbuf[b]=t;}}
    nbuf[i]='\0'; return nbuf;
}

static void draw_sidebar(void){
    int x=SX;
    display_rect(x,BY,60,200,C_BG);
    display_text(x, BY,    "NEXT", C_DIM);
    display_text(x, BY+80, "SCORE",C_DIM);
    display_text(x, BY+108,"LINES",C_DIM);
    display_text(x, BY+136,"LEVEL",C_DIM);
    display_rect(x,BY+14,44,44,C_BG);
    for(int row=0;row<4;row++) for(int col=0;col<4;col++){
        if(!sbit(np,0,row,col)) continue;
        display_rect(x+col*10+2, BY+16+row*10, 9,9, PC[np]);
    }
    display_text(x, BY+92,  n2s(score), C_WHT);
    display_text(x, BY+120, n2s(lines), C_WHT);
    display_text(x, BY+148, n2s(level), C_WHT);
}

static void redraw_board(void){
    for(int r=0;r<ROWS;r++) for(int c=0;c<COLS;c++)
        dcell(c,r, board[r][c] ? PC[board[r][c]-1] : 0);
    piece_drawn=0;
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
    int n=0;
    for(int r=ROWS-1;r>=0;r--){
        int full=1;
        for(int c=0;c<COLS;c++) if(!board[r][c]){full=0;break;}
        if(!full) continue;
        for(int rr=r;rr>0;rr--) for(int c=0;c<COLS;c++) board[rr][c]=board[rr-1][c];
        for(int c=0;c<COLS;c++) board[0][c]=0;
        n++; r++;
    }
    if(n){ score+=pts[n]*level; lines+=n;
        level=lines/10+1; if(level>15) level=15; }
    return n;
}

static void spawn(void){
    cp=np; np=rng7(); cr=0; cx=COLS/2-2; cy=-1;
    if(hit(cp,cr,cx,cy)) game_over=1;
}

/* ── Entry point ─────────────────────────────────────────────────────── */
int main(void){
    gpio_configure(PIN_UP,       GPIO_INPUT|GPIO_PULL_DOWN);
    gpio_configure(PIN_DOWN,     GPIO_INPUT|GPIO_PULL_DOWN);
    gpio_configure(PIN_LEFT,     GPIO_INPUT|GPIO_PULL_DOWN);
    gpio_configure(PIN_RIGHT,    GPIO_INPUT|GPIO_PULL_DOWN);
    gpio_configure(PIN_A,        GPIO_INPUT|GPIO_PULL_DOWN);
    gpio_configure(PIN_B,        GPIO_INPUT|GPIO_PULL_DOWN);
    gpio_configure(PIN_SETTINGS, GPIO_INPUT|GPIO_PULL_UP|GPIO_ACTIVE_LOW);

    display_clear(C_BG);
    display_text_large(98,90,"TETRIS",C_CYAN);
    display_text(108,140,"Press A",C_WHT);
    display_flush();

    uint32_t seed=1;
    while(!gpio_read(PIN_A)&&!gpio_read(PIN_B)) { seed++; delay(20000); }
    rng=seed^0xDEADBEEFu; delay(120000);

restart:
    for(int r=0;r<ROWS;r++) for(int c=0;c<COLS;c++) board[r][c]=0;
    score=0; lines=0; level=1; game_over=0; piece_drawn=0;
    np=rng7(); spawn();

    /* Reset button state and DAS counters between games so stale edge
     * flags from the "press A to restart" sequence don't leak in.       */
    btn_up=(Btn){0}; btn_dn=(Btn){0};
    btn_l =(Btn){0}; btn_r =(Btn){0};
    btn_a =(Btn){0}; btn_b =(Btn){0};
    btn_s =(Btn){0};
    l_das=0; r_das=0; dn_held=0;

    display_clear(C_BG);
    display_rect(BX-2, BY-2, 2, ROWS*CELL+4, C_BDR);
    display_rect(BX+COLS*CELL, BY-2, 2, ROWS*CELL+4, C_BDR);
    display_rect(BX-2, BY+ROWS*CELL, COLS*CELL+4, 2, C_BDR);
    redraw_board();
    draw_sidebar();
    display_flush();

    uint32_t drop_us=600000u, drop_acc=0;
    int paused=0;

    while(!game_over){

        /* ── Poll all buttons ───────────────────────────────────────── */
        btn_poll(&btn_up, gpio_read(PIN_UP));
        btn_poll(&btn_dn, gpio_read(PIN_DOWN));
        btn_poll(&btn_l,  gpio_read(PIN_LEFT));
        btn_poll(&btn_r,  gpio_read(PIN_RIGHT));
        btn_poll(&btn_a,  gpio_read(PIN_A));
        btn_poll(&btn_b,  gpio_read(PIN_B));
        /* SETTINGS is active-LOW: invert so logical 1 = pressed        */
        btn_poll(&btn_s, !gpio_read(PIN_SETTINGS));

        /* SETTINGS toggle works even while paused                       */
        if (BTN_ROSE(btn_s)) paused = !paused;
        if (paused) { delay(20000); continue; }

        /* ── Rotate — single rising edge (UP or A or B) ─────────────── */
        if (BTN_ROSE(btn_up) || BTN_ROSE(btn_a) || BTN_ROSE(btn_b)) {
            int nr = (cr + 1) % 4;
            if      (!hit(cp, nr, cx,   cy)) cr = nr;
            else if (!hit(cp, nr, cx-1, cy)) { cr = nr; cx--; } /* wall-kick L */
            else if (!hit(cp, nr, cx+1, cy)) { cr = nr; cx++; } /* wall-kick R */
        }

        /* ── LEFT — immediate move on press, then DAS auto-repeat ───── */
        if (BTN_ROSE(btn_l)) {
            if (!hit(cp, cr, cx-1, cy)) cx--;
            l_das = 0;
        } else if (BTN_HELD(btn_l)) {
            l_das++;
            if (l_das >= DAS_INIT && (l_das - DAS_INIT) % DAS_RPT == 0)
                if (!hit(cp, cr, cx-1, cy)) cx--;
        } else {
            l_das = 0;
        }

        /* ── RIGHT — same pattern, independent counter ───────────────── */
        if (BTN_ROSE(btn_r)) {
            if (!hit(cp, cr, cx+1, cy)) cx++;
            r_das = 0;
        } else if (BTN_HELD(btn_r)) {
            r_das++;
            if (r_das >= DAS_INIT && (r_das - DAS_INIT) % DAS_RPT == 0)
                if (!hit(cp, cr, cx+1, cy)) cx++;
        } else {
            r_das = 0;
        }

        /* ── Gravity + soft-drop ────────────────────────────────────── */
        /* dn_held counts consecutive confirmed-HELD ticks of DOWN.
         * Soft-drop only activates after 4 such ticks (80 ms of genuine hold
         * after debounce settles).  A noise spike that just barely clears the
         * 80 ms debounce window resets dn_held before it reaches the threshold
         * and never accelerates the piece.                               */
        if (BTN_HELD(btn_dn)) { if (dn_held < 4) dn_held++; }
        else                    dn_held = 0;

        uint32_t eff = (dn_held >= 4) ? 80000u : drop_us;
        drop_acc += 20000u;
        if (drop_acc >= eff) {
            drop_acc = 0;
            if (!hit(cp, cr, cx, cy+1)) {
                cy++;
                if (dn_held >= 4) score++;
            } else {
                update_piece();
                lock_piece();
                if (clear_lines()) {
                    drop_us = 600000u / (uint32_t)level;
                    if (drop_us < 80000u) drop_us = 80000u;
                    redraw_board();
                }
                draw_sidebar();
                spawn();
                if (game_over) break;
            }
        }

        /* Only redraw the piece when it actually moved, rotated, or spawned.
         * Calling update_piece() unconditionally erased+redrew every 20 ms
         * even on a static piece, causing visible LCD flicker (the "trembling"
         * effect).  The comparison covers all change sources: gravity (cy),
         * DAS (cx), rotation (cr), new piece (cp), and post-redraw_board
         * where piece_drawn is reset to 0.                               */
        if (!piece_drawn || cp != pp || cr != pr || cx != px || cy != py)
            update_piece();
        display_flush();
        delay(20000);
    }

    /* ── Game over ──────────────────────────────────────────────────── */
    display_rect(BX+5, BY+90, COLS*CELL-10, 38, C_BG);
    display_text(BX+10, BY+95,  "GAME OVER", 0xF800u);
    display_text(BX+10, BY+113, n2s(score),  C_WHT);
    display_flush();
    delay(2500000u);
    while( gpio_read(PIN_A) || gpio_read(PIN_B)) delay(20000);
    while(!gpio_read(PIN_A) && !gpio_read(PIN_B)) delay(20000);
    delay(100000u);
    goto restart;
    return 0;
}