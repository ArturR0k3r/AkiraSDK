/**
 * @file main.c
 * @brief Tetris for AkiraOS — monochrome-optimised for Sharp LS027B7DH01
 *
 * Controls:
 *   LEFT/RIGHT = move    DOWN = soft-drop
 *   A/UP/X     = rotate CW    Y = rotate CCW    B = hard-drop
 *   SETTINGS   = pause
 *
 * @copyright Copyright (c) 2026 PenEngineering S.R.L
 * @license Apache-2.0
 */

#include "akira_api.h"

/* ── Pins ────────────────────────────────────────────────────────────── */
#define PIN_UP       4
#define PIN_DOWN     5
#define PIN_LEFT     6
#define PIN_RIGHT    7
#define PIN_A        15
#define PIN_B        16
#define PIN_X        17
#define PIN_Y        41
#define PIN_SETTINGS 0   /* active-LOW, pull-up */

/* ── Layout ──────────────────────────────────────────────────────────── */
#define CELL   10
#define GCOLS  10
#define GROWS  20
static int BX, BY = 20, SX;
static int SCR_W = 320, SCR_H = 240;

/* ── Monochrome palette ──────────────────────────────────────────────── */
#define C_BG    0x0000u
#define C_WHT   0xFFFFu
#define C_DIM   0x0000u  /* black on mono for "dim" text on grey bg */

/* ── Dithered grey helper ────────────────────────────────────────────── */
static void draw_grey(int x, int y, int w, int h, int d) {
    display_rect(x, y, w, h, d >= 3 ? C_WHT : C_BG);
    int step = (d == 2) ? 2 : 4;
    int lc   = (d >= 3) ? C_BG : C_WHT;
    int st   = (d == 3) ? 3 : 0;
    for (int r = st; r < h; r += step) display_hline(x, y + r, w, lc);
}

/* ── Piece shapes — 16-bit 4×4 masks ────────────────────────────────── */
static const uint16_t SHAPES[7][4] = {
    {0x0F00u,0x2222u,0x00F0u,0x4444u}, /* I */
    {0x6600u,0x6600u,0x6600u,0x6600u}, /* O */
    {0x0E40u,0x4C40u,0x4E00u,0x4640u}, /* T */
    {0x06C0u,0x4620u,0x06C0u,0x4620u}, /* S */
    {0x0C60u,0x2640u,0x0C60u,0x2640u}, /* Z */
    {0x44C0u,0x8E00u,0x6440u,0x0E20u}, /* J */
    {0x4460u,0x0E80u,0xC440u,0x2E00u}, /* L */
};
static int sbit(int p, int r, int row, int col) {
    return (SHAPES[p][r] >> (15 - row*4 - col)) & 1;
}

/* ── Game state ──────────────────────────────────────────────────────── */
static uint8_t board[GROWS][GCOLS];
static int cp, cr, cx, cy, np;
static int score, hi_score, lines, level, game_over;
static uint32_t rng;
static int pp, pr, px2, py2, piece_drawn;

static int rng7(void) {
    rng = rng * 1664525u + 1013904223u;
    return (int)((rng >> 16) & 0x7FFFu) % 7;
}
static int hit(int p, int r, int x, int y) {
    for (int row = 0; row < 4; row++) for (int col = 0; col < 4; col++) {
        if (!sbit(p,r,row,col)) continue;
        int bx = x+col, by = y+row;
        if (bx<0||bx>=GCOLS||by>=GROWS) return 1;
        if (by>=0&&board[by][bx]) return 1;
    }
    return 0;
}

/* ── Per-piece inner patterns — drawn in black over white block ──────── */
static void draw_piece_pattern(int px3, int py3, int piece) {
    int cx2 = px3 + CELL/2, cy2 = py3 + CELL/2;
    switch (piece) {
    case 0: /* I: solid — no pattern, just outline */
        break;
    case 1: /* O: 3×3 centre hole */
        display_rect(px3+3, py3+3, CELL-7, CELL-7, C_BG);
        break;
    case 2: /* T: horizontal bar */
        display_hline(px3+1, cy2, CELL-3, C_BG);
        break;
    case 3: /* S: / diagonal */
        display_line(px3+1, py3+CELL-3, px3+CELL-3, py3+1, C_BG);
        break;
    case 4: /* Z: \ diagonal */
        display_line(px3+1, py3+1, px3+CELL-3, py3+CELL-3, C_BG);
        break;
    case 5: /* J: left 2px stripe */
        display_vline(px3+2, py3+1, CELL-3, C_BG);
        break;
    case 6: /* L: right 2px stripe */
        display_vline(px3+CELL-4, py3+1, CELL-3, C_BG);
        break;
    }
    (void)cx2; (void)cy2;
}

/* ── Cell drawing ────────────────────────────────────────────────────── */
static void dcell(int col, int row, int piece_id, int filled) {
    int sx = BX + col*CELL, sy = BY + row*CELL;
    if (filled) {
        display_rect(sx, sy, CELL-1, CELL-1, C_WHT);
        display_rect_outline(sx, sy, CELL-1, CELL-1, C_BG);
        draw_piece_pattern(sx, sy, piece_id);
    } else {
        display_rect(sx, sy, CELL-1, CELL-1, C_BG);
        display_pixel(sx + CELL/2, sy + CELL/2, C_WHT);
    }
}

static void update_piece(void) {
    if (piece_drawn) {
        for (int row=0;row<4;row++) for (int col=0;col<4;col++) {
            if (!sbit(pp,pr,row,col)) continue;
            int by=py2+row; if (by<0||by>=GROWS) continue;
            if (!board[by][px2+col]) dcell(px2+col, by, 0, 0);
        }
    }
    for (int row=0;row<4;row++) for (int col=0;col<4;col++) {
        if (!sbit(cp,cr,row,col)) continue;
        int by=cy+row; if (by<0||by>=GROWS) continue;
        dcell(cx+col, by, cp, 1);
    }
    pp=cp; pr=cr; px2=cx; py2=cy; piece_drawn=1;
}

/* ── Number → string ─────────────────────────────────────────────────── */
static char nbuf[16];
static const char *n2s(int v) {
    int i=0; if(!v){nbuf[0]='0';nbuf[1]='\0';return nbuf;}
    int n=v; while(n){nbuf[i++]='0'+n%10;n/=10;}
    for(int a=0,b=i-1;a<b;a++,b--){char t=nbuf[a];nbuf[a]=nbuf[b];nbuf[b]=t;}
    nbuf[i]='\0'; return nbuf;
}

/* ── Sidebar ─────────────────────────────────────────────────────────── */
static void draw_sidebar(void) {
    int x = SX, sw = SCR_W - x - 2;
    display_rect(x, BY, sw, GROWS*CELL, C_BG);

    /* NEXT box */
    draw_grey(x, BY, sw, 14, 2);
    display_text(x+4, BY+2, "NEXT", C_BG);
    display_rect(x, BY+16, 46, 46, C_BG);
    display_rect_outline(x, BY+16, 46, 46, C_WHT);
    for (int row=0;row<4;row++) for (int col=0;col<4;col++) {
        if (!sbit(np,0,row,col)) continue;
        int sx2=x+col*10+3, sy=BY+18+row*10;
        display_rect(sx2, sy, 9, 9, C_WHT);
        display_rect_outline(sx2, sy, 9, 9, C_BG);
        draw_piece_pattern(sx2, sy, np);
    }

    int sy = BY+70;
    draw_grey(x, sy,      sw, 14, 2); display_text(x+4, sy+2, "SCORE", C_BG);
    display_text(x+4, sy+16, n2s(score), C_WHT);
    draw_grey(x, sy+30,   sw, 14, 2); display_text(x+4, sy+32, "BEST",  C_BG);
    display_text(x+4, sy+46, n2s(hi_score), C_WHT);
    draw_grey(x, sy+60,   sw, 14, 2); display_text(x+4, sy+62, "LINES", C_BG);
    display_text(x+4, sy+76, n2s(lines), C_WHT);
    draw_grey(x, sy+90,   sw, 14, 2); display_text(x+4, sy+92, "LEVEL", C_BG);
    display_text_large(x+4, sy+108, n2s(level), C_WHT);
}

static void redraw_board(void) {
    for (int r=0;r<GROWS;r++) for (int c=0;c<GCOLS;c++)
        dcell(c, r, board[r][c] ? board[r][c]-1 : 0, board[r][c] ? 1 : 0);
    piece_drawn = 0;
}

/* ── Board border ────────────────────────────────────────────────────── */
static void draw_border(void) {
    display_rect(BX-3,         BY-2,          3, GROWS*CELL+4, C_WHT);
    display_rect(BX+GCOLS*CELL,BY-2,          3, GROWS*CELL+4, C_WHT);
    display_rect(BX-3,         BY+GROWS*CELL, GCOLS*CELL+6, 3, C_WHT);
    for (int r=0;r<=GROWS;r+=5) {
        display_rect(BX-5,           BY+r*CELL, 5, 1, C_WHT);
        display_rect(BX+GCOLS*CELL,  BY+r*CELL, 5, 1, C_WHT);
    }
}

/* ── Line-clear flash ────────────────────────────────────────────────── */
static void flash_rows(int *rows, int n) {
    for (int f=0;f<4;f++) {
        for (int i=0;i<n;i++) {
            display_rect(BX, BY+rows[i]*CELL, GCOLS*CELL-1, CELL-1,
                         (f&1) ? C_WHT : C_BG);
        }
        display_flush(); delay(55000);
    }
}

/* ── Lock + clear ────────────────────────────────────────────────────── */
static void lock_piece(void) {
    for (int row=0;row<4;row++) for (int col=0;col<4;col++) {
        if (!sbit(cp,cr,row,col)) continue;
        int by=cy+row; if (by>=0) board[by][cx+col]=(uint8_t)(cp+1);
    }
}
static int clear_lines(void) {
    static const int pts[5]={0,100,300,500,800};
    int full[4], n=0;
    for (int r=GROWS-1;r>=0;r--) {
        int full2=1; for (int c=0;c<GCOLS;c++) if (!board[r][c]){full2=0;break;}
        if (!full2) continue; full[n++]=r;
    }
    if (!n) return 0;
    flash_rows(full, n);
    for (int i=0;i<n;i++) {
        int r=full[i];
        for (int rr=r;rr>0;rr--) for (int c=0;c<GCOLS;c++) board[rr][c]=board[rr-1][c];
        for (int c=0;c<GCOLS;c++) board[0][c]=0;
        for (int j=i+1;j<n;j++) if(full[j]<r) full[j]++;
    }
    score += pts[n]*level; lines += n;
    level = lines/10+1; if (level>15) level=15;
    if (score > hi_score) hi_score = score;
    return n;
}
static void spawn(void) {
    cp=np; np=rng7(); cr=0; cx=GCOLS/2-2; cy=-1;
    if (hit(cp,cr,cx,cy)) game_over=1;
}

/* ── Debounce ────────────────────────────────────────────────────────── */
#define DB 4u
typedef struct { uint8_t raw,cnt,state,prev; } Btn;
static void bp(Btn *b, int lv) {
    uint8_t r=lv?1u:0u; b->prev=b->state;
    if(r!=b->raw){b->raw=r;b->cnt=0u;}
    else if(b->cnt<DB){if(++b->cnt>=DB)b->state=b->raw;}
}
#define ROSE(b) (!(b).prev&&(b).state)
#define HELD(b) ((b).state)
static Btn btn_up,btn_dn,btn_l,btn_r,btn_a,btn_b,btn_x,btn_y,btn_s;

/* ── DAS ─────────────────────────────────────────────────────────────── */
#define DAS_I 20
#define DAS_R  6
static int l_das, r_das, dn_held;

/* ── High score storage ──────────────────────────────────────────────── */
static void load_hi(void) {
    int fd = storage_open("tetris_hi.dat", STORAGE_O_READ);
    if (fd >= 0) { storage_read(fd, &hi_score, sizeof(hi_score)); storage_close(fd); }
}
static void save_hi(void) {
    if (score > hi_score) {
        hi_score = score;
        int fd = storage_open("tetris_hi.dat", STORAGE_O_WRITE);
        if (fd >= 0) { storage_write(fd, &hi_score, sizeof(hi_score)); storage_close(fd); }
    }
}

/* ── Entry point ─────────────────────────────────────────────────────── */
int main(void) {
    display_get_size(&SCR_W, &SCR_H);
    int total_w = GCOLS*CELL + 8 + 56;
    BX = (SCR_W - total_w) / 2; if (BX < 4) BX = 4;
    SX = BX + GCOLS*CELL + 8;

    gpio_configure(PIN_UP,       GPIO_INPUT|GPIO_PULL_UP|GPIO_ACTIVE_LOW);
    gpio_configure(PIN_DOWN,     GPIO_INPUT|GPIO_PULL_UP|GPIO_ACTIVE_LOW);
    gpio_configure(PIN_LEFT,     GPIO_INPUT|GPIO_PULL_UP|GPIO_ACTIVE_LOW);
    gpio_configure(PIN_RIGHT,    GPIO_INPUT|GPIO_PULL_UP|GPIO_ACTIVE_LOW);
    gpio_configure(PIN_A,        GPIO_INPUT|GPIO_PULL_UP|GPIO_ACTIVE_LOW);
    gpio_configure(PIN_B,        GPIO_INPUT|GPIO_PULL_UP|GPIO_ACTIVE_LOW);
    gpio_configure(PIN_X,        GPIO_INPUT|GPIO_PULL_UP|GPIO_ACTIVE_LOW);
    gpio_configure(PIN_Y,        GPIO_INPUT|GPIO_PULL_UP|GPIO_ACTIVE_LOW);
    gpio_configure(PIN_SETTINGS, GPIO_INPUT|GPIO_PULL_UP|GPIO_ACTIVE_LOW);

    load_hi();

    /* Title */
    display_clear(C_BG);
    draw_grey(0, 0, SCR_W, 20, 2);
    draw_grey(0, SCR_H-20, SCR_W, 20, 2);
    display_text_large(SCR_W/2-32, SCR_H/2-30, "TETRIS", C_WHT);
    display_hline(SCR_W/2-36, SCR_H/2-10, 72, C_WHT);
    display_text(SCR_W/2-30, SCR_H/2+4,  "A/UP/X = rotate CW", C_WHT);
    display_text(SCR_W/2-30, SCR_H/2+18, "Y = rotate CCW", C_WHT);
    display_text(SCR_W/2-30, SCR_H/2+32, "B = hard drop", C_WHT);
    display_text(SCR_W/2-24, SCR_H/2+52, "Press A to Start", C_WHT);
    display_flush();

    uint32_t seed=1;
    while(!gpio_read(PIN_A)&&!gpio_read(PIN_B)) { seed++; delay(20000); }
    rng=seed^0xDEADBEEFu; delay(120000);

restart:
    for (int r=0;r<GROWS;r++) for (int c=0;c<GCOLS;c++) board[r][c]=0;
    score=0; lines=0; level=1; game_over=0; piece_drawn=0;
    np=rng7(); spawn();
    btn_up=(Btn){0}; btn_dn=(Btn){0}; btn_l=(Btn){0}; btn_r=(Btn){0};
    btn_a=(Btn){0};  btn_b=(Btn){0};  btn_x=(Btn){0}; btn_y=(Btn){0};
    btn_s=(Btn){0};  l_das=0; r_das=0; dn_held=0;

    display_clear(C_BG);
    draw_border(); redraw_board(); draw_sidebar(); display_flush();

    uint32_t drop_us=600000u, drop_acc=0;
    int paused=0;

    while (!game_over) {
        bp(&btn_up, gpio_read(PIN_UP));   bp(&btn_dn, gpio_read(PIN_DOWN));
        bp(&btn_l,  gpio_read(PIN_LEFT)); bp(&btn_r,  gpio_read(PIN_RIGHT));
        bp(&btn_a,  gpio_read(PIN_A));    bp(&btn_b,  gpio_read(PIN_B));
        bp(&btn_x,  gpio_read(PIN_X));    bp(&btn_y,  gpio_read(PIN_Y));
        bp(&btn_s,  gpio_read(PIN_SETTINGS));

        if (ROSE(btn_s)) {
            paused = !paused;
            if (paused) {
                draw_grey(BX+5, BY+88, GCOLS*CELL-10, 22, 2);
                display_text(BX+12, BY+93, "-- PAUSED --", C_BG);
                display_flush();
            } else { redraw_board(); draw_border(); draw_sidebar(); piece_drawn=0; }
        }
        if (paused) { delay(20000); continue; }

        /* Rotate CW */
        if (ROSE(btn_up)||ROSE(btn_a)||ROSE(btn_x)) {
            int nr=(cr+1)%4;
            if      (!hit(cp,nr,cx,  cy)) cr=nr;
            else if (!hit(cp,nr,cx-1,cy)){cr=nr;cx--;}
            else if (!hit(cp,nr,cx+1,cy)){cr=nr;cx++;}
        }
        /* Rotate CCW */
        if (ROSE(btn_y)) {
            int nr=(cr+3)%4;
            if      (!hit(cp,nr,cx,  cy)) cr=nr;
            else if (!hit(cp,nr,cx-1,cy)){cr=nr;cx--;}
            else if (!hit(cp,nr,cx+1,cy)){cr=nr;cx++;}
        }
        /* Hard drop */
        if (ROSE(btn_b)) {
            while (!hit(cp,cr,cx,cy+1)) { cy++; score++; }
            update_piece(); lock_piece();
            if (clear_lines()) { drop_us=600000u/(uint32_t)level; if(drop_us<80000u)drop_us=80000u; redraw_board(); draw_border(); }
            draw_sidebar(); spawn(); if (game_over) break; drop_acc=0; continue;
        }
        /* Left */
        if (ROSE(btn_l)){if(!hit(cp,cr,cx-1,cy))cx--;l_das=0;}
        else if(HELD(btn_l)){l_das++;if(l_das>=DAS_I&&(l_das-DAS_I)%DAS_R==0)if(!hit(cp,cr,cx-1,cy))cx--;}
        else l_das=0;
        /* Right */
        if (ROSE(btn_r)){if(!hit(cp,cr,cx+1,cy))cx++;r_das=0;}
        else if(HELD(btn_r)){r_das++;if(r_das>=DAS_I&&(r_das-DAS_I)%DAS_R==0)if(!hit(cp,cr,cx+1,cy))cx++;}
        else r_das=0;
        /* Soft drop */
        if (HELD(btn_dn)){if(dn_held<4)dn_held++;}else dn_held=0;

        uint32_t eff=(dn_held>=4)?80000u:drop_us;
        drop_acc+=20000u;
        if (drop_acc>=eff) {
            drop_acc=0;
            if (!hit(cp,cr,cx,cy+1)) { cy++; if(dn_held>=4)score++; }
            else {
                update_piece(); lock_piece();
                if(clear_lines()){drop_us=600000u/(uint32_t)level;if(drop_us<80000u)drop_us=80000u;redraw_board();draw_border();}
                draw_sidebar(); spawn(); if(game_over)break;
            }
        }
        if (!piece_drawn||cp!=pp||cr!=pr||cx!=px2||cy!=py2) update_piece();
        display_flush(); delay(20000);
    }

    save_hi();

    /* Game over overlay */
    draw_grey(BX+2, BY+78, GCOLS*CELL-4, 62, 2);
    display_rect_outline(BX+2, BY+78, GCOLS*CELL-4, 62, C_WHT);
    display_text(BX+8,  BY+86,  "GAME OVER",            C_BG);
    display_text(BX+8,  BY+100, "Score:", C_BG); display_text(BX+52, BY+100, n2s(score), C_BG);
    display_text(BX+8,  BY+114, "Lines:", C_BG); display_text(BX+52, BY+114, n2s(lines), C_BG);
    display_text(BX+8,  BY+128, "A/B: restart",         C_BG);
    display_flush();
    delay(1500000u);
    while(gpio_read(PIN_A)||gpio_read(PIN_B)) delay(20000);
    while(!gpio_read(PIN_A)&&!gpio_read(PIN_B)) delay(20000);
    delay(100000u);
    goto restart;
    return 0;
}
