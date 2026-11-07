/**
 * @file main.c
 * @brief Altimeter / Vario — barometric altitude for hiking (BME280).
 *
 * Altitude is derived from pressure with a near-surface linear model
 * (~8.3 m per hPa), referenced to a datum you calibrate on the trail, so it
 * needs only the BME280 pressure channel (sensor_read returns Pa).  Tracks
 * current altitude, a smoothed vertical-speed vario, min/max, and total
 * ascent/descent with a noise deadband so a jittery sensor doesn't inflate
 * the totals.
 *
 * Controls:
 *   A = calibrate here (this point becomes the datum altitude)
 *   UP/DOWN = set datum altitude +/- 1 m (dial in a known elevation)
 *   Y = reset trip (ascent/descent/min/max)
 *   B = exit
 *
 * @copyright Copyright (c) 2026 PenEngineering S.R.L
 * @license Apache-2.0
 */

#include "akira_api.h"

/* ── Display ─────────────────────────────────────────────────────────── */
static int32_t SCR_W = 320;
static int32_t SCR_H = 240;

/* ── Palette (terminal-green, matches the cyberdeck family) ──────────── */
#define C_BG      0x0000
#define C_HDR     0x0280
#define C_TEXT    0xE7FC
#define C_DIM     0x5AEB
#define C_GREEN   0x07E6
#define C_CYAN    0x07FF
#define C_AMBER   0xFD20
#define C_RED     0xF800
#define C_TILE    0x0841
#define C_TILE_HI 0x1246
#define C_BAR_BG  0x2124

/* ── Helpers ─────────────────────────────────────────────────────────── */
static int clampi(int v,int lo,int hi){ return v<lo?lo:(v>hi?hi:v); }
static int text_px(const char*s){ int n=0; while(s&&s[n])n++; return n*6; }
static uint16_t darken(uint16_t c,int a){ int r=(c>>11)&31,g=(c>>5)&63,b=c&31;
    r-=a;g-=a*2;b-=a; if(r<0)r=0;if(g<0)g=0;if(b<0)b=0; return (uint16_t)((r<<11)|(g<<5)|b); }
static int int_to_str(int v,char*b){ char t[12]; int n=0,p=0;
    if(v<0){b[p++]='-';v=-v;} if(v==0)t[n++]='0';
    while(v>0&&n<11){t[n++]=(char)('0'+v%10);v/=10;} while(n>0)b[p++]=t[--n]; b[p]='\0'; return p; }

/* Signed metres-with-tenths: writes "-12.3" from a value in decimetres. */
static void dm_str(int dm,char*b){
    int p=0; if(dm<0){b[p++]='-';dm=-dm;}
    p+=int_to_str(dm/10,b+p); b[p++]='.'; b[p++]=(char)('0'+dm%10); b[p]='\0';
}

/* ── State (altitude in decimetres = 0.1 m for resolution) ───────────── */
#define K_DM_PER_PA_NUM 83     /* 8.3 m/hPa = 0.83 dm/Pa = 83/100 dm per Pa */
#define K_DM_PER_PA_DEN 100

static int   have_baro;
static int   p_ema;            /* smoothed pressure, Pa */
static int   datum_pa;         /* reference pressure */
static int   datum_alt_dm;     /* altitude assigned to the datum */
static int   cur_dm;           /* current altitude, dm */
static int   min_dm, max_dm;
static int   asc_dm, desc_dm;  /* trip totals */
static int   commit_dm;        /* last committed point for the deadband */
#define DEADBAND_DM 10         /* 1.0 m before ascent/descent accrues */

/* Vario */
static int   vario_cms;        /* smoothed vertical speed, cm/s */
static int   prev_dm;
static uint32_t prev_ms;

/* Altitude history sparkline */
#define HN 100
static int16_t hist[HN]; static int h_head, h_len; static uint32_t h_last_ms;

/* ── Compute altitude from smoothed pressure ─────────────────────────── */
static void recompute(void) {
    cur_dm = datum_alt_dm + (int)(((long)(datum_pa - p_ema) * K_DM_PER_PA_NUM) / K_DM_PER_PA_DEN);
}

static void calibrate_here(void) { datum_pa = p_ema; recompute(); commit_dm = cur_dm; }
static void reset_trip(void) { asc_dm = desc_dm = 0; min_dm = max_dm = commit_dm = cur_dm; h_len=h_head=0; }

/* ── Sampling ────────────────────────────────────────────────────────── */
static void sample(void) {
    int praw = sensor_read(SENSOR_CHAN_PRESS);      /* kPa*1000 == Pa */
    if (praw == AKIRA_SENSOR_ERROR || praw <= 0) { have_baro = 0; return; }
    if (!have_baro) {                               /* first valid reading */
        have_baro = 1; p_ema = praw; datum_pa = praw; datum_alt_dm = 0;
        recompute(); prev_dm = cur_dm; min_dm = max_dm = commit_dm = cur_dm;
        prev_ms = (uint32_t)rtc_get_uptime_ms();
        return;
    }
    p_ema += (praw - p_ema) / 4;                    /* EMA smoothing */
    recompute();

    if (cur_dm > max_dm) max_dm = cur_dm;
    if (cur_dm < min_dm) min_dm = cur_dm;

    /* Ascent/descent with a deadband so noise doesn't inflate totals. */
    int d = cur_dm - commit_dm;
    if (d >= DEADBAND_DM)      { asc_dm  += d; commit_dm = cur_dm; }
    else if (d <= -DEADBAND_DM){ desc_dm += -d; commit_dm = cur_dm; }

    /* Vario: EMA of dAlt/dt. */
    uint32_t now = (uint32_t)rtc_get_uptime_ms();
    uint32_t dt = now - prev_ms;
    if (dt >= 500) {
        int inst = (int)(((long)(cur_dm - prev_dm) * 10000) / (int)dt); /* cm/s */
        vario_cms += (inst - vario_cms) / 2;
        prev_dm = cur_dm; prev_ms = now;
    }

    /* History sample ~1 Hz */
    if (now - h_last_ms >= 1000) {
        h_last_ms = now; hist[h_head]=(int16_t)cur_dm; h_head=(h_head+1)%HN; if(h_len<HN)h_len++;
    }
}

/* ── Chrome ──────────────────────────────────────────────────────────── */
static void tile(int x,int y,int w,int h,const char*t,uint16_t tc){
    display_rect(x,y,w,h,C_TILE);
    display_rect(x,y,w,11,C_TILE_HI);
    display_rect_outline(x,y,w,h,darken(C_GREEN,4));
    display_text(x+5,y+2,t,tc);
}

/* ── Vario gauge: a vertical needle, up=green down=red ───────────────── */
static void draw_vario(int x,int y,int w,int h) {
    tile(x,y,w,h,"VARIO",C_CYAN);
    int cx=x+w/2, midy=y+h/2+4, half=(h-20)/2;
    /* scale ticks */
    for(int i=-3;i<=3;i++){ int ty=midy - i*half/3; display_hline(cx-6,ty,12,darken(C_CYAN,4)); }
    display_hline(cx-8,midy,16,C_DIM);
    /* needle: clamp to +/- 3 m/s */
    int v=clampi(vario_cms,-300,300);
    int len=v*half/300;
    uint16_t col = v>15?C_GREEN : v<-15?C_RED : C_DIM;
    if (len>=0) display_rect(cx-4, midy-len, 8, len, col);
    else        display_rect(cx-4, midy, 8, -len, col);
    /* numeric m/s, built straight from cm/s (e.g. -1.2) */
    char vb[12]; int p=0; int cs=vario_cms; if(cs<0){vb[p++]='-';cs=-cs;}
    p+=int_to_str(cs/100,vb+p); vb[p++]='.'; vb[p++]=(char)('0'+(cs/10)%10);
    vb[p++]=' '; vb[p++]='m'; vb[p++]='/'; vb[p++]='s'; vb[p]='\0';
    display_text(cx-text_px(vb)/2, y+h-11, vb, col);
}

/* ── Altitude sparkline ──────────────────────────────────────────────── */
static void draw_spark(int x,int y,int w,int h) {
    if (h_len<2) return;
    int lo=hist[0], hi=hist[0];
    for(int i=0;i<h_len;i++){ int j=(h_head-h_len+i+HN*2)%HN;
        if(hist[j]<lo)lo=hist[j]; if(hist[j]>hi)hi=hist[j]; }
    int span=hi-lo; if(span<10)span=10;
    int prevx=-1,prevy=0;
    for(int i=0;i<h_len;i++){ int j=(h_head-h_len+i+HN*2)%HN;
        int px=x + i*w/(HN-1);
        int py=y+h-1 - (hist[j]-lo)*(h-2)/span;
        if(prevx>=0) display_line(prevx,prevy,px,py,C_GREEN);
        prevx=px; prevy=py;
    }
}

/* ── Frame ───────────────────────────────────────────────────────────── */
static void draw(void) {
    display_clear(C_BG);
    display_rect(0,0,SCR_W,15,C_HDR);
    display_text(5,3,"ALTIMETER / VARIO",C_TEXT);

    if (!have_baro) {
        display_text(SCR_W/2-70, SCR_H/2-4, "No barometer (BME280)", C_AMBER);
        display_text(SCR_W/2-84, SCR_H/2+12, "needs the pressure sensor channel", C_DIM);
        display_rect(0,SCR_H-13,SCR_W,13,C_HDR);
        display_text(4,SCR_H-11,"B: exit",C_DIM);
        display_flush();
        return;
    }

    /* Big current altitude */
    char ab[16]; dm_str(cur_dm, ab);
    display_text_huge(10, 22, ab, C_GREEN);
    display_text(10+ (int)(text_px(ab)*3) + 6, 40, "m", C_DIM);

    /* Pressure + datum readouts */
    int hpa=p_ema/100;                              /* Pa -> hPa */
    char pb[12]; int_to_str(hpa,pb);
    display_text(10, 66, "P", C_DIM); display_text(28, 66, pb, C_TEXT);
    display_text(28+text_px(pb), 66, "hPa", C_DIM);
    char db[12]; dm_str(datum_alt_dm, db);
    display_text(120, 66, "datum", C_DIM); display_text(160, 66, db, C_TEXT);

    /* Vario gauge on the right */
    int vw=70, vh=54;
    draw_vario(SCR_W-vw-6, 18, vw, vh);

    /* Trip tiles */
    int gy=80, tw=(SCR_W-18)/2, th=30, gx=6, gap=6;
    tile(gx,gy,tw,th,"MAX",C_AMBER);
    { char b[16]; dm_str(max_dm,b); display_text(gx+40,gy+14,b,C_TEXT); display_text(gx+40+text_px(b),gy+14,"m",C_DIM); }
    tile(gx+tw+gap,gy,tw,th,"MIN",C_CYAN);
    { char b[16]; dm_str(min_dm,b); display_text(gx+tw+gap+40,gy+14,b,C_TEXT); display_text(gx+tw+gap+40+text_px(b),gy+14,"m",C_DIM); }

    int gy2=gy+th+gap;
    tile(gx,gy2,tw,th,"ASCENT",C_GREEN);
    { char b[16]; dm_str(asc_dm,b); display_text(gx+52,gy2+14,b,C_GREEN); display_text(gx+52+text_px(b),gy2+14,"m",C_DIM); }
    tile(gx+tw+gap,gy2,tw,th,"DESCENT",C_RED);
    { char b[16]; dm_str(desc_dm,b); display_text(gx+tw+gap+58,gy2+14,b,C_RED); display_text(gx+tw+gap+58+text_px(b),gy2+14,"m",C_DIM); }

    /* Altitude profile sparkline */
    int sy=gy2+th+gap;
    tile(gx,sy,SCR_W-12,SCR_H-sy-15,"ALTITUDE PROFILE",C_DIM);
    draw_spark(gx+4, sy+13, SCR_W-20, SCR_H-sy-15-16);

    display_rect(0,SCR_H-13,SCR_W,13,C_HDR);
    display_text(4,SCR_H-11,"A:cal here  UP/DN:set alt  Y:reset  B:exit",C_DIM);
    display_flush();
}

/* ── Entry point ─────────────────────────────────────────────────────── */
int main(void) {
    printf("AkiraOS Altimeter");
    display_get_size(&SCR_W,&SCR_H);

    int prev=(int)input_get_buttons();
    while (1) {
        sample();

        int held=(int)input_get_buttons();
        int edge=held&~prev; prev=held;

        if (edge & AKIRA_BTN_B) { app_switch("supervisor"); return 0; }
        if (have_baro) {
            if (edge & AKIRA_BTN_A)    calibrate_here();
            if (edge & AKIRA_BTN_Y)    reset_trip();
            if (edge & AKIRA_BTN_UP)   { datum_alt_dm += 10; recompute(); }
            if (edge & AKIRA_BTN_DOWN) { datum_alt_dm -= 10; recompute(); }
        }

        draw();
        delay(250000);      /* 4 Hz */
    }
    return 0;
}
