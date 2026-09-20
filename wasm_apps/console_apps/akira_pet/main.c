/**
 * @file main.c
 * @brief Akira Pet — a virtual companion that lives in real time.
 *
 * The pet's stats decay by the wall clock, so it keeps ageing and getting
 * hungry while the console is off.  State is saved to NVS after every action;
 * on launch we read the saved Unix time, measure how long we were away, and
 * fast-forward the pet's needs to catch up.  With no RTC set, decay falls back
 * to session uptime (it only ages while the app is open).
 *
 * Controls:
 *   A = Feed     B = Play     X = Sleep/Wake     Y = Clean
 *   DOWN = Menu (Stats / New Pet / Exit)
 *
 * @copyright Copyright (c) 2026 PenEngineering S.R.L
 * @license Apache-2.0
 */

#include "akira_api.h"

/* ── Display ─────────────────────────────────────────────────────────── */
static int32_t SCR_W = 320;
static int32_t SCR_H = 240;

/* ── Palette (bright on black — reads on colour and 1-bit panels) ─────── */
#define C_BG      0x0000
#define C_HDR     0x18E3   /* dark teal header bar */
#define C_TEXT    0xFFFF
#define C_DIM     0x8410
#define C_PET     0x9E7F   /* soft cyan body */
#define C_PET_SH  0x5ABF   /* darker cyan shadow */
#define C_ACCENT  0xFD20   /* orange accent  */
#define C_GOOD    0x07E6   /* green bars     */
#define C_WARN    0xFFE0   /* yellow         */
#define C_BAD     0xF800   /* red            */
#define C_HEART   0xF81F   /* magenta hearts */
#define C_FOOD    0xFD20
#define C_BUBBLE  0x5DFF
#define C_ZZZ     0xC618
#define C_FLOOR   0x2124
#define C_WALL    0x10A2

/* ── Stats (0..100) ──────────────────────────────────────────────────── */
typedef struct {
    int hunger;    /* 0 = full, 100 = starving */
    int happy;     /* 0 = miserable, 100 = joyful */
    int energy;    /* 0 = exhausted, 100 = rested */
    int hygiene;   /* 0 = filthy, 100 = clean */
    int health;    /* 0 = dead */
    int age_min;   /* total minutes lived */
    int xp, level;
    int stage;     /* evolution stage */
    int alive;
    int sleeping;
    int last_unix; /* wall-clock time of last save (0 if never) */
} pet_t;

static pet_t P;

/* ── Evolution stages ────────────────────────────────────────────────── */
enum { S_EGG = 0, S_BABY, S_CHILD, S_TEEN, S_ADULT, S_COUNT };
static const char *STAGE_NAME[S_COUNT] = {
    "Egg", "Baby", "Child", "Teen", "Adult"
};
/* Age thresholds in minutes: hatch fast, then real-day-scale growth. */
static const int STAGE_AGE[S_COUNT] = { 0, 3, 1440, 4320, 10080 };

static int stage_for_age(int age_min) {
    int s = S_EGG;
    for (int i = 0; i < S_COUNT; i++) if (age_min >= STAGE_AGE[i]) s = i;
    return s;
}

/* ── Small int/string helpers (no libc) ──────────────────────────────── */
static int clampi(int v, int lo, int hi){ return v<lo?lo:(v>hi?hi:v); }

static uint16_t lighten(uint16_t c,int a){ int r=(c>>11)&31,g=(c>>5)&63,b=c&31;
    r+=a;g+=a*2;b+=a; if(r>31)r=31;if(g>63)g=63;if(b>31)b=31; return (uint16_t)((r<<11)|(g<<5)|b); }
static uint16_t darken(uint16_t c,int a){ int r=(c>>11)&31,g=(c>>5)&63,b=c&31;
    r-=a;g-=a*2;b-=a; if(r<0)r=0;if(g<0)g=0;if(b<0)b=0; return (uint16_t)((r<<11)|(g<<5)|b); }
#define C_CHEEK  0xFB56   /* soft pink cheeks */
#define C_BELLY  0xDFFF   /* pale belly patch */

static int str_to_int(const char *s, int *consumed) {
    int v=0, i=0, neg=0;
    if (s[0]=='-'){ neg=1; i=1; }
    for (; s[i]>='0'&&s[i]<='9'; i++) v=v*10+(s[i]-'0');
    if (consumed) *consumed=i;
    return neg?-v:v;
}
static int int_to_str(int v, char *b) {   /* returns length written */
    char t[12]; int n=0, p=0;
    if (v<0){ b[p++]='-'; v=-v; }
    if (v==0) t[n++]='0';
    while (v>0 && n<11){ t[n++]=(char)('0'+v%10); v/=10; }
    while (n>0) b[p++]=t[--n];
    b[p]='\0';
    return p;
}

/* ── Persistence ─────────────────────────────────────────────────────────
 * One NVS key holds a version-tagged, comma-separated record.  Parsing is
 * order-based, so bump PET_VER if the layout ever changes.               */
#define PET_KEY  "pet/save"
#define PET_VER  1

static void save_pet(void) {
    char buf[128];
    int p = 0;
    int fields[13] = {
        PET_VER, P.hunger, P.happy, P.energy, P.hygiene, P.health,
        P.age_min, P.xp, P.level, P.stage, P.alive, P.sleeping, P.last_unix
    };
    for (int i = 0; i < 13; i++) {
        if (i) buf[p++] = ',';
        p += int_to_str(fields[i], buf + p);
    }
    settings_set(PET_KEY, buf);
}

/* Returns 1 if a valid save was loaded, 0 if none (new pet). */
static int load_pet(void) {
    char buf[128];
    if (settings_get(PET_KEY, buf, sizeof(buf)) != 0) return 0;

    int vals[13], n = 0, pos = 0;
    while (n < 13) {
        int used;
        vals[n++] = str_to_int(buf + pos, &used);
        pos += used;
        if (buf[pos] == ',') pos++;
        else break;
    }
    if (n < 13 || vals[0] != PET_VER) return 0;

    P.hunger=vals[1]; P.happy=vals[2]; P.energy=vals[3]; P.hygiene=vals[4];
    P.health=vals[5]; P.age_min=vals[6]; P.xp=vals[7]; P.level=vals[8];
    P.stage=vals[9]; P.alive=vals[10]; P.sleeping=vals[11]; P.last_unix=vals[12];
    return 1;
}

static void new_pet(void) {
    P.hunger=20; P.happy=80; P.energy=90; P.hygiene=90; P.health=100;
    P.age_min=0; P.xp=0; P.level=1; P.stage=S_EGG; P.alive=1; P.sleeping=0;
    P.last_unix = rtc_get_unix_time();
    if (P.last_unix < 0) P.last_unix = 0;
    save_pet();
}

/* ── Time-driven decay ───────────────────────────────────────────────── */
static void apply_decay(int mins) {
    if (mins <= 0 || !P.alive) return;

    P.hunger  = clampi(P.hunger  + mins*12/60, 0, 100);
    P.happy   = clampi(P.happy   - mins*8/60,  0, 100);
    P.hygiene = clampi(P.hygiene - mins*6/60,  0, 100);
    if (P.sleeping) P.energy = clampi(P.energy + mins*15/60, 0, 100);
    else            P.energy = clampi(P.energy - mins*6/60,  0, 100);

    /* Auto-wake once fully rested. */
    if (P.sleeping && P.energy >= 100) P.sleeping = 0;

    /* Health follows care: suffers when needs are unmet, recovers when met. */
    int ail = (P.hunger>=90) + (P.happy<=10) + (P.hygiene<=10) + (P.energy<=5);
    if (ail > 0)      P.health = clampi(P.health - mins*(4*ail)/60, 0, 100);
    else if (P.hunger<50 && P.happy>50 && P.hygiene>50)
                      P.health = clampi(P.health + mins*4/60, 0, 100);

    P.age_min += mins;
    P.stage = stage_for_age(P.age_min);
    if (P.health <= 0) { P.health = 0; P.alive = 0; }
}

/* Catch up to the wall clock since the last save (across power-off). */
static void catch_up_realtime(void) {
    int now = rtc_get_unix_time();
    if (now > 0 && P.last_unix > 0 && now > P.last_unix) {
        int mins = (now - P.last_unix) / 60;
        apply_decay(mins);
    }
    if (now > 0) P.last_unix = now;
    save_pet();
}

/* ── Effects (little floating feedback sprites) ──────────────────────── */
enum { FX_NONE=0, FX_FOOD, FX_HEART, FX_BUBBLE, FX_STAR };
static int fx_type, fx_timer;
static void start_fx(int type){ fx_type=type; fx_timer=24; }

/* ── Actions ─────────────────────────────────────────────────────────── */
static char act_msg[40];
static int  act_msg_timer;
static void say(const char *s){ int i; for(i=0;s[i]&&i<39;i++) act_msg[i]=s[i]; act_msg[i]='\0'; act_msg_timer=60; }

static void gain_xp(int n){ P.xp+=n; while(P.xp >= P.level*50){ P.xp-=P.level*50; P.level++; } }

static void act_feed(void) {
    if (!P.alive) return;
    if (P.sleeping) { say("Zzz... don't wake it to eat."); return; }
    if (P.hunger <= 2) { say("Not hungry right now."); return; }
    P.hunger = clampi(P.hunger - 35, 0, 100);
    P.happy  = clampi(P.happy + 4, 0, 100);
    gain_xp(2); start_fx(FX_FOOD); say("Yum!");
}

static void act_play(void) {
    if (!P.alive) return;
    if (P.sleeping) { say("It's asleep."); return; }
    if (P.energy < 15) { say("Too tired to play."); return; }
    P.happy  = clampi(P.happy + 30, 0, 100);
    P.energy = clampi(P.energy - 15, 0, 100);
    P.hunger = clampi(P.hunger + 5, 0, 100);
    gain_xp(5); start_fx(FX_HEART); say("Wheee!");
}

static void act_sleep(void) {
    if (!P.alive) return;
    P.sleeping = !P.sleeping;
    say(P.sleeping ? "Goodnight..." : "Good morning!");
}

static void act_clean(void) {
    if (!P.alive) return;
    if (P.hygiene >= 98) { say("Already sparkling."); return; }
    P.hygiene = clampi(P.hygiene + 45, 0, 100);
    P.happy   = clampi(P.happy + 3, 0, 100);
    gain_xp(2); start_fx(FX_BUBBLE); say("So fresh!");
}

/* ── Mood, derived from stats ────────────────────────────────────────── */
enum { M_HAPPY=0, M_NEUTRAL, M_SAD, M_SICK, M_SLEEP };
static int pet_mood(void) {
    if (!P.alive)    return M_SICK;
    if (P.sleeping)  return M_SLEEP;
    if (P.health<30) return M_SICK;
    if (P.hunger>75 || P.happy<25 || P.hygiene<20) return M_SAD;
    if (P.happy>65 && P.hunger<40) return M_HAPPY;
    return M_NEUTRAL;
}

/* ── Drawing helpers ─────────────────────────────────────────────────── */
static int text_px(const char *s){ int n=0; while(s&&s[n])n++; return n*6; }

static void draw_bar(int x, int y, int w, int val, uint16_t col, const char *label) {
    display_text(x, y, label, C_DIM);
    int bx = x + 40, bw = w - 40;
    display_rect(bx, y, bw, 8, C_FLOOR);
    int fw = bw * clampi(val,0,100) / 100;
    display_rect(bx, y, fw, 8, col);
    display_rect_outline(bx, y, bw, 8, C_DIM);
}
static uint16_t bar_col(int v, int invert) {
    int good = invert ? (100 - v) : v;   /* invert: low is good (hunger) */
    if (good > 60) return C_GOOD;
    if (good > 30) return C_WARN;
    return C_BAD;
}

/* ── Pet sprite ──────────────────────────────────────────────────────────
 * Body shape grows with the stage; the face is driven by mood.  A gentle
 * vertical bob + occasional blink keeps it feeling alive.                 */
static void draw_face(int cx, int cy, int mood, int blink) {
    uint16_t eye = C_BG;
    switch (mood) {
    case M_SLEEP:
        display_hline(cx-8, cy, 5, C_BG);        /* closed eyes */
        display_hline(cx+4, cy, 5, C_BG);
        display_hline(cx-3, cy+7, 7, C_BG);      /* calm mouth */
        break;
    case M_SICK:
        display_line(cx-8, cy-3, cx-3, cy+2, C_BG);  /* x_x eyes */
        display_line(cx-3, cy-3, cx-8, cy+2, C_BG);
        display_line(cx+3, cy-3, cx+8, cy+2, C_BG);
        display_line(cx+8, cy-3, cx+3, cy+2, C_BG);
        display_hline(cx-3, cy+8, 7, C_BG);
        break;
    case M_SAD:
        if (!blink){ display_rect(cx-8,cy-2,3,3,eye); display_rect(cx+5,cy-2,3,3,eye);}
        else       { display_hline(cx-8,cy,3,eye); display_hline(cx+5,cy,3,eye);}
        display_line(cx-4, cy+9, cx, cy+6, C_BG);    /* frown */
        display_line(cx, cy+6, cx+4, cy+9, C_BG);
        break;
    case M_HAPPY:
        if (!blink){ display_rect(cx-8,cy-3,3,4,eye); display_rect(cx+5,cy-3,3,4,eye);}
        else       { display_hline(cx-8,cy,3,eye); display_hline(cx+5,cy,3,eye);}
        display_line(cx-5, cy+6, cx, cy+10, C_BG);   /* big smile */
        display_line(cx, cy+10, cx+5, cy+6, C_BG);
        break;
    default: /* neutral */
        if (!blink){ display_rect(cx-8,cy-2,3,3,eye); display_rect(cx+5,cy-2,3,3,eye);}
        else       { display_hline(cx-8,cy,3,eye); display_hline(cx+5,cy,3,eye);}
        display_hline(cx-3, cy+7, 7, C_BG);
        break;
    }
}

static void draw_pet(int cx, int cy, int stage, int mood, int blink) {
    uint16_t hi = lighten(C_PET, 6);
    uint16_t lo = darken(C_PET, 5);

    if (stage == S_EGG) {
        /* Shaded egg with a highlight and speckles */
        display_circle_fill(cx, cy+2, 17, lo);
        display_circle_fill(cx, cy+1, 16, C_PET);
        display_circle_fill(cx-4, cy-6, 8, hi);      /* top-left sheen */
        display_circle_fill(cx-6, cy-8, 3, 0xFFFF);  /* glint */
        display_pixel(cx+6, cy+4, lo);
        display_pixel(cx+2, cy+9, lo);
        display_pixel(cx-8, cy+6, lo);
        if (blink) display_hline(cx-5, cy, 10, lo);  /* wobble */
        return;
    }

    int r = (stage==S_BABY)?16 : (stage==S_CHILD)?20 : (stage==S_TEEN)?24 : 28;
    int by = cy + r/2;   /* body centre */

    /* Contact shadow on the floor */
    display_circle_fill(cx, by+r-1, r-2, darken(C_FLOOR,1));

    /* Ears/horns grow in with age (behind the body) */
    if (stage >= S_CHILD) {
        display_triangle_fill(cx-r+4, by-r+8, cx-r+9, by-r-4, cx-r+14, by-r+8, C_PET);
        display_triangle_fill(cx+r-14, by-r+8, cx+r-9, by-r-4, cx+r-4, by-r+8, C_PET);
        display_triangle_fill(cx-r+7, by-r+7, cx-r+9, by-r-1, cx-r+11, by-r+7, C_CHEEK);
        display_triangle_fill(cx+r-11, by-r+7, cx+r-9, by-r-1, cx+r-7, by-r+7, C_CHEEK);
    }
    if (stage == S_ADULT) {
        display_triangle_fill(cx-4, by-r-1, cx, by-r-13, cx+4, by-r-1, C_ACCENT);
    }

    /* Body: base, lower shadow, upper sheen, belly patch, outline */
    display_circle_fill(cx, by, r, C_PET);
    display_circle_fill(cx, by+2, r-1, lo);          /* bottom shade */
    display_circle_fill(cx, by-1, r-1, C_PET);
    display_circle_fill(cx-r/3, by-r/3, r/2, hi);    /* top-left highlight */
    display_circle_fill(cx, by+r/4, r*2/3, C_BELLY); /* belly */
    display_circle(cx, by, r, C_PET_SH);             /* outline */
    display_circle_fill(cx-r/2, by-r/2, 3, 0xFFFF);  /* glint */

    /* Cheeks */
    display_circle_fill(cx-r+6, by+r/8, 3, C_CHEEK);
    display_circle_fill(cx+r-6, by+r/8, 3, C_CHEEK);

    /* Feet */
    display_circle_fill(cx-r/2, by+r-1, 4, C_PET_SH);
    display_circle_fill(cx+r/2, by+r-1, 4, C_PET_SH);

    draw_face(cx, by - r/6, mood, blink);
}

/* ── Floating effect over the pet ────────────────────────────────────── */
static void draw_fx(int cx, int cy) {
    if (fx_timer <= 0) return;
    int rise = (24 - fx_timer);
    int y = cy - 20 - rise;
    switch (fx_type) {
    case FX_FOOD:
        display_circle_fill(cx, y, 3, C_FOOD);
        display_pixel(cx, y-3, C_GOOD);
        break;
    case FX_HEART:
        display_circle_fill(cx-3, y, 3, C_HEART);
        display_circle_fill(cx+3, y, 3, C_HEART);
        display_triangle_fill(cx-6, y+1, cx, y+8, cx+6, y+1, C_HEART);
        break;
    case FX_BUBBLE:
        display_circle(cx-6, y+2, 3, C_BUBBLE);
        display_circle(cx+4, y-2, 4, C_BUBBLE);
        display_circle(cx+8, y+4, 2, C_BUBBLE);
        break;
    case FX_STAR:
        display_line(cx-4, y, cx+4, y, C_WARN);
        display_line(cx, y-4, cx, y+4, C_WARN);
        break;
    }
    fx_timer--;
}

/* ── Sleeping Zzz ────────────────────────────────────────────────────── */
static void draw_zzz(int cx, int cy, int tick) {
    if (!P.sleeping) return;
    int o = (tick>>3) % 3;
    display_text(cx+14, cy-14 - o*6, "z", C_ZZZ);
    display_text(cx+20, cy-20 - o*6, "Z", C_ZZZ);
}

/* ── Screens ─────────────────────────────────────────────────────────── */
static void draw_room(int tick) {
    display_clear(C_BG);

    /* Header */
    display_rect(0, 0, SCR_W, 18, C_HDR);
    display_text(6, 5, "AKIRA PET", C_TEXT);
    display_text(SCR_W-2-text_px(STAGE_NAME[P.stage]), 5, STAGE_NAME[P.stage], C_ACCENT);

    /* ── Room ── back wall with a soft vertical gradient, a window that
     * glows at day / shows stars at night, a skirting board, tiled floor. */
    int floor_y = SCR_H*62/100;
    int wall_h = floor_y - 18;
    for (int i = 0; i < 6; i++) {                 /* gradient wall bands */
        uint16_t band = lighten(C_WALL, i);
        display_rect(0, 18 + i*wall_h/6, SCR_W, wall_h/6 + 1, band);
    }
    /* Window */
    int wx = SCR_W*66/100, wy = 30, ww = SCR_W*26/100, wh = wall_h*46/100;
    int night = P.sleeping;
    display_rect(wx-3, wy-3, ww+6, wh+6, darken(C_WALL,3));      /* frame */
    display_rect(wx, wy, ww, wh, night ? 0x0009 : 0x9EFF);      /* sky */
    if (night) {
        display_pixel(wx+ww/4, wy+wh/3, 0xFFFF);
        display_pixel(wx+ww*3/4, wy+wh/4, 0xFFFF);
        display_pixel(wx+ww/2, wy+wh*2/3, 0xC618);
        display_circle_fill(wx+ww*3/4, wy+wh/3, 4, 0xFFF0);     /* moon */
    } else {
        display_circle_fill(wx+ww/2, wy+wh/2, 6, 0xFFE0);       /* sun */
    }
    display_vline(wx+ww/2, wy, wh, darken(C_WALL,3));           /* mullions */
    display_hline(wx, wy+wh/2, ww, darken(C_WALL,3));

    /* Floor + skirting board + tile seams */
    display_rect(0, floor_y, SCR_W, SCR_H-floor_y, C_FLOOR);
    display_rect(0, floor_y-3, SCR_W, 3, darken(C_FLOOR,2));
    display_hline(0, floor_y, SCR_W, lighten(C_FLOOR,2));
    for (int x = 0; x < SCR_W; x += 24)
        display_vline(x, floor_y+2, SCR_H-floor_y-2, darken(C_FLOOR,1));

    int cx = SCR_W/2;
    int bob = (tick>>3)&1 ? 0 : 2;               /* idle bob */
    if (P.sleeping) bob = 1;
    int cy = floor_y - 34 + bob;
    int blink = ((tick % 90) < 4);               /* occasional blink */
    int mood = pet_mood();

    if (P.alive) {
        draw_pet(cx, cy, P.stage, mood, blink);
        draw_zzz(cx, cy, tick);
        draw_fx(cx, cy);
    } else {
        /* Gravestone */
        display_rect(cx-14, cy-6, 28, 30, C_DIM);
        display_circle_fill(cx, cy-6, 14, C_DIM);
        display_text(cx-9, cy+4, "R.I.P", C_BG);
        display_text(SCR_W/2-58, floor_y+8, "Your pet has passed on.", C_BAD);
    }

    /* Stat bars */
    int by = floor_y + (P.alive ? 6 : 22);
    if (P.alive) {
        int col = 40;
        draw_bar(6,           by,    SCR_W/2-12, 100-P.hunger, bar_col(P.hunger,1), "FOOD");
        draw_bar(SCR_W/2+6,   by,    SCR_W/2-12, P.happy,      bar_col(P.happy,0),  "FUN");
        draw_bar(6,           by+12, SCR_W/2-12, P.energy,     bar_col(P.energy,0), "REST");
        draw_bar(SCR_W/2+6,   by+12, SCR_W/2-12, P.hygiene,    bar_col(P.hygiene,0),"CLEAN");
        draw_bar(6,           by+24, SCR_W-12,   P.health,     bar_col(P.health,0), "LIFE");
        (void)col;
    }

    /* Footer: message or action hints */
    int fy = SCR_H - 14;
    display_rect(0, fy-2, SCR_W, 16, C_HDR);
    if (act_msg_timer > 0) {
        display_text(6, fy+1, act_msg, C_TEXT);
        act_msg_timer--;
    } else if (P.alive) {
        display_text(4, fy+1, "A:Feed B:Play X:Sleep Y:Clean  v:Menu", C_DIM);
    } else {
        display_text(4, fy+1, "Press DOWN: menu to hatch anew", C_DIM);
    }

    display_flush();
}

/* ── Menu ────────────────────────────────────────────────────────────── */
enum { MN_RESUME=0, MN_STATS, MN_RESET, MN_EXIT, MN_COUNT };
static const char *MN_LABEL[MN_COUNT] = { "Resume", "Stats", "New Pet", "Exit" };

static void draw_stats_screen(void) {
    int prev = (int)input_get_buttons();
    while (1) {
        display_clear(C_BG);
        display_rect(0,0,SCR_W,18,C_HDR);
        display_text(6,5,"STATS",C_TEXT);
        int y=28, dh=16;
        display_text(10,y, "Stage",   C_DIM); display_text(120,y, STAGE_NAME[P.stage], C_ACCENT); y+=dh;
        display_text(10,y, "Age",     C_DIM);
        display_number(120,y, P.age_min/1440, C_TEXT); display_text(140,y,"d",C_DIM);
        display_number(156,y, (P.age_min%1440)/60, C_TEXT); display_text(176,y,"h",C_DIM);
        display_number(192,y, P.age_min%60, C_TEXT); display_text(212,y,"m",C_DIM); y+=dh;
        display_text(10,y, "Level",   C_DIM); display_number(120,y, P.level, C_TEXT); y+=dh;
        display_text(10,y, "XP",      C_DIM); display_number(120,y, P.xp, C_TEXT);
        display_text(150,y,"/",C_DIM); display_number(160,y, P.level*50, C_DIM); y+=dh;
        display_text(10,y, "Hunger",  C_DIM); display_number(120,y, P.hunger, C_TEXT); y+=dh;
        display_text(10,y, "Fun",     C_DIM); display_number(120,y, P.happy, C_TEXT); y+=dh;
        display_text(10,y, "Rest",    C_DIM); display_number(120,y, P.energy, C_TEXT); y+=dh;
        display_text(10,y, "Clean",   C_DIM); display_number(120,y, P.hygiene, C_TEXT); y+=dh;
        display_text(10,y, "Health",  C_DIM); display_number(120,y, P.health, C_TEXT); y+=dh;
        display_text(10,y, P.sleeping?"Sleeping":"Awake", P.sleeping?C_ZZZ:C_GOOD);
        display_text(6, SCR_H-14, "A / B: back", C_DIM);
        display_flush();

        int held=(int)input_get_buttons(), edge=held&~prev; prev=held;
        if (edge & (AKIRA_BTN_A|AKIRA_BTN_B)) return;
        delay(25000);
    }
}

/* Returns menu choice (MN_*). */
static int show_menu(void) {
    int cur=0, prev=(int)input_get_buttons();
    int bx=SCR_W*24/100, by=SCR_H*24/100, bw=SCR_W*52/100, bh=SCR_H*52/100;
    while (1) {
        display_rounded_rect_fill(bx,by,bw,bh,6,C_HDR);
        display_rounded_rect(bx,by,bw,bh,6,C_DIM);
        display_text(bx+14,by+8,"MENU",C_TEXT);
        display_hline(bx+8,by+24,bw-16,C_DIM);
        for (int i=0;i<MN_COUNT;i++){
            int iy=by+32+i*20;
            if (i==cur){ display_rect(bx+8,iy-2,bw-16,18,C_ACCENT);
                         display_text(bx+18,iy+2,MN_LABEL[i],C_BG); }
            else         display_text(bx+18,iy+2,MN_LABEL[i],C_TEXT);
        }
        display_flush();

        int held=(int)input_get_buttons(), edge=held&~prev; prev=held;
        if (edge & AKIRA_BTN_UP)   cur=(cur>0)?cur-1:MN_COUNT-1;
        if (edge & AKIRA_BTN_DOWN) cur=(cur<MN_COUNT-1)?cur+1:0;
        if (edge & AKIRA_BTN_B)    return MN_RESUME;   /* B backs out */
        if (edge & AKIRA_BTN_A) {
            if (cur==MN_STATS) { draw_stats_screen(); prev=(int)input_get_buttons(); }
            else return cur;
        }
        delay(25000);
    }
}

/* ── Entry point ─────────────────────────────────────────────────────── */
int main(void) {
    printf("AkiraOS Virtual Pet");
    display_get_size(&SCR_W, &SCR_H);

    if (!load_pet()) new_pet();
    catch_up_realtime();
    if (P.alive)
        say(P.sleeping ? "Shhh, still sleeping." : "I missed you!");

    int prev = (int)input_get_buttons();
    int tick = 0;
    uint32_t last_min_ms = (uint32_t)rtc_get_uptime_ms();

    while (1) {
        int held = (int)input_get_buttons();
        int edge = held & ~prev;
        prev = held;

        if (edge & AKIRA_BTN_A) { act_feed();  save_pet(); }
        if (edge & AKIRA_BTN_B) { act_play();  save_pet(); }
        if (edge & AKIRA_BTN_X) { act_sleep(); save_pet(); }
        if (edge & AKIRA_BTN_Y) { act_clean(); save_pet(); }

        if (edge & AKIRA_BTN_DOWN) {
            int c = show_menu();
            if (c == MN_EXIT)  { save_pet(); app_switch("supervisor"); return 0; }
            if (c == MN_RESET) { new_pet(); say("A new egg appears!"); }
            prev = (int)input_get_buttons();
        }

        /* Live decay while the app is open: apply one minute at a time. */
        uint32_t nowms = (uint32_t)rtc_get_uptime_ms();
        if ((uint32_t)(nowms - last_min_ms) >= 60000u) {
            int mins = (int)((nowms - last_min_ms) / 60000u);
            apply_decay(mins);
            last_min_ms += (uint32_t)mins * 60000u;
            int t = rtc_get_unix_time();
            if (t > 0) P.last_unix = t;
            save_pet();
        }

        draw_room(tick++);
        delay(60000);   /* ~16 fps is plenty for a calm companion */
    }
    return 0;
}
