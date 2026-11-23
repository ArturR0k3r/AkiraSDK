/**
 * @file main.c
 * @brief Pixel Angler — tilt-to-fight fishing for AkiraOS
 *
 * Cast, wait for the bite, then fight the fish with the console itself:
 * hold A to reel while tilting against each run the fish makes.  Too much
 * tension and the line snaps; too little and it throws the hook.
 *
 * Twenty-four species across four waters (Willow Pond, Mountain River,
 * Coral Coast, the Abyss).  Each keeps its own hours by the real clock —
 * dawn, day, dusk or night — and its own depth.  Coins buy rods, reels,
 * longer line, lures and passage to new waters; the Fishdex keeps every
 * species and record size, saved in NVS.
 *
 * Controls:
 *   A (hold, release)  charge and cast        A        strike on the bite
 *   UP / DOWN          lure depth             B        twitch the lure in
 *   tilt               steer the lure / fight the fish (or LEFT/RIGHT)
 *   X                  reel all the way in    HOME     menu
 *
 * The IMU (LSM6DS3) is read with sensor_read(); the neutral pose is taken
 * when the lure lands and when a fish is hooked.  Options picks the axis,
 * inverts it, or switches to the D-pad if the console has no IMU.
 *
 * @copyright Copyright (c) 2026 PenEngineering S.R.L
 * @license Apache-2.0
 */

#include "akira_api.h"
#include "../../common/akira_game.h"
#include "../../common/akira_spr.h"

static int32_t SCR_W = 320, SCR_H = 240;
#define HUD_H 22
#define BAR_H 22
static int WATER_TOP = 100, WATER_BOT = 218;
#define DOCK_END 84

/* ── Art ─────────────────────────────────────────────────────────────────
 * Fish templates face right; digits are per-species colour slots:
 * 1 back, 2 flank, 3 belly, 4 fins, 5 accent (spots, stripes, glow). */
static const char A_SLIM[] =
    "................" "................" "................" "................"
    "......kkk......." ".....k444k......" "k...kk1111kkk..." "kk.k1111111111k."
    "k4k111515151wk1k" "k44k22222222222k" "kk.k3333333332k." "k...kk33333kkk.."
    "......k44k......" ".......kk......." "................" "................";
static const char A_ROUND[] =
    "................" "................" "......kkkk......" ".....k4444k....."
    "....kk11111kk..." "k..k111111111k.." "kk.k1151151151k." "k4k11111111wk11k"
    "k44k2222222222k." "k4k2222222222k.." "kk.k333333333k.." "k...k3333333k..."
    ".....kk444kk...." ".......kkk......" "................" "................";
static const char A_LONG[] =
    "................" "................" "................" "................"
    "................" "...kkk.....kkk.." "..k444kkkkk111k." ".k41151151151wkk"
    "k4k2222222222222" ".k43333333333kk." "..kkkkkkkkkkk..." "................"
    "................" "................" "................" "................";
static const char A_WHISKER[] =
    "................" "................" "................" "................"
    "................" ".....kkkk......." "k...k4444kk....." "kk.k11111111kk.."
    "k4k1111111111wkk" "k44k22222222222k" "kk.k33333333333k" "k...kk3333kkkk4k"
    "........k....k4." ".............k.." "................" "................";
static const char A_SWORD[] =
    "................" "................" "................" "......kk........"
    ".....k44k......." "k...k4444k......" "kk.k111111kk...." "k4k111111111wk.."
    "k44k222222222kGG" "k4k33333333kk..." "kk.k333333k....." "k...kk44kk......"
    "................" "................" "................" "................";
static const char A_SQUID[] =
    "................" "................" "................" "................"
    ".........kkkk..." ".......kk1111kk." "k.k.k.k11111111k" ".k4k4kk11111wk1k"
    "k4k4k4k22222222k" ".k4k4kk33333333k" "k.k.k.k3333333k." ".......kk3333k.."
    ".........kkkk..." "................" "................" "................";
static const char A_ANGLERF[] =
    "................" "...........kk..." "..........k55k.." "..........kkk..."
    ".........k......" "....kkkkk......." "k..k11111kk....." "kk.k1111111kk..."
    "k4k11111111wkk.." "k44k111111111kk." "k4k2222kwkwkwk.." "kk.k22kwkwkwk..."
    "k...k33333kk...." ".....kkkkk......" "................" "................";
static const char A_SHARK[] =
    "................" "................" "................" "......k........."
    ".....k4k........" "k...k444k......." "kk.kk1111kkkk..." "k4k111111111kk.."
    "k44k11111111wkk." "k4k222222222222k" "kk.k3333kwkwkkk." "k....kk3333k...."
    "........k4k....." ".........k......" "................" "................";
static const char A_PUFFER[] =
    "................" "................" "......k.k.k....." ".....kkkkkkk...."
    "...k.k11111k.k.." "..kkk1111111kkk." "k..k115115115k.." "kk.k11111111wk.k"
    "k4k111111111kkk." "k4k222222222k.k." "kk.k22222222kk.." "k...k3333333k..."
    "...k.kk333kk.k.." "......k.k.k....." "................" "................";
static const char A_BOOT[] =
    "................" "................" "................" "....kkkkk......."
    "....kNNNk......." "....knnnk......." "....knNnk......." "....knnnk......."
    "....knnnkkkkk..." "....knnnnnnnnk.." "....kNnnnnnnnNk." "....kNNNNNNNNNk."
    "....kkkkkkkkkkk." "................" "................" "................";
static const char A_FISHER[] =
    "................" "................" ".....kkkkkk....." "....kEEEEEEk...."
    "...kkeeeeeekk..." ".....kssssk....." ".....kskksk....." ".....kssssk....."
    "....kkOOOOkk...." "...ksoOooOosk..." "...ks.OooO.sk..." ".....kbbbbk....."
    ".....kbkkbk....." ".....kbk.kbk...." "....kNNk.kNNk..." "....kkkk.kkkk...";
static const char I_COIN[]  = "..kkkk.." ".kyyyyk." "kyyYyyyk" "kyYyyyYk" "kyyYyyYk" "kyyyyyYk" ".kYYYYk." "..kkkk..";
static const char I_SUN[]   = "...y...." ".y.y.y.." "..yyy..." "yyyyyyy." "..yyy..." ".y.y.y.." "...y...." "........";
static const char I_MOON[]  = "..kkk..." ".kvvk..." "kvvk...." "kvvk...." "kvvk...." ".kvvk..." "..kkk..." "........";
static const char I_FISH[]  = "........" "..kkk..." "kkbbbk.." "kbbbbwk." "kkbbbbk." "..kkk..." "........" "........";
static const char I_HOOK[]  = "....kk.." "....zk.." "....z..." "....z..." "z...z..." "zz..z..." ".zzz...." "........";
static const char I_ROD[]   = ".......z" "......z." ".....z.." "....z..." "...n...." "..n....." ".nn....." "nn......";
static const char I_REEL[]  = "..kkkk.." ".kzzzzk." "kzkzzkzk" "kzzkkzzk" "kzkzzkzk" ".kzzzzk." "..kkkk.." "........";
static const char I_LINE[]  = "..kkkk.." ".kwGGwk." "kwGkkGwk" "kGk..kGk" "kwGkkGwk" ".kwGGwk." "..kkkk.." "........";
static const char I_MAP[]   = "kkkkkkkk" "kvvVvvVk" "kvrVvvVk" "kvVvrvVk" "kvVvvvrk" "kvVvvvVk" "kkkkkkkk" "........";
static const char I_STAR[]  = "...y...." "...y...." "yyyyyyy." ".yyyyy.." "..yyy..." ".yy.yy.." "y.....y." "........";
static const char I_LOCK[]  = "..kkkk.." ".kk..kk." ".k....k." "kkkkkkkk" "kyyyyyyk" "kyykkyyk" "kyyyyyyk" "kkkkkkkk";
static const char I_GEAR[]  = "...kk..." ".kkGGkk." ".kGkkGk." "kGk..kGk" "kGk..kGk" ".kGkkGk." ".kkGGkk." "...kk...";
static const char I_BOOK[]  = "kkkkkkk." "kbbbbbbk" "kbwwwwbk" "kbbbbbbk" "kbwwwbbk" "kbbbbbbk" "kkkkkkkk" "........";
static const char I_EXIT[]  = "kkkk...." "k..k.k.." "k..kkk.." "k..k.kk." "k..kkk.." "k..k.k.." "kkkk...." "........";

/* ── Species & waters ────────────────────────────────────────────────── */
enum { TP_SLIM, TP_ROUND, TP_LONG, TP_WHISKER, TP_SWORD, TP_SQUID, TP_ANGLER, TP_SHARK, TP_PUFFER, TP_BOOT };
static const char *const TMPL[] = {
    A_SLIM, A_ROUND, A_LONG, A_WHISKER, A_SWORD, A_SQUID, A_ANGLERF, A_SHARK, A_PUFFER, A_BOOT,
};
#define TM_DAWN  1
#define TM_DAY   2
#define TM_DUSK  4
#define TM_NIGHT 8
#define TM_ANY   15
#define TM_TWI   (TM_DAWN | TM_DUSK)
#define SF_PRED 1
#define SF_BIG  2
#define SF_GLOW 4
#define SF_JUNK 8

typedef struct {
    const char *name;
    uint8_t tmpl, loc, time, depth, rarity;
    uint16_t cmin, cmax;
    uint8_t str, stam, value, flags;
    uint8_t pal[5][3];
} species_t;

static const species_t SP[] = {
    {"Minnow",      TP_SLIM,    0, TM_ANY,   0, 1,   4,   9,  2,  20,   3, 0,
     {{90, 110, 90}, {170, 180, 160}, {230, 230, 210}, {120, 130, 110}, {140, 150, 130}}},
    {"Perch",       TP_ROUND,   0, TM_DAY,   1, 1,  15,  35,  4,  35,   6, SF_PRED,
     {{70, 100, 40}, {180, 170, 60}, {240, 220, 150}, {230, 110, 40}, {40, 60, 30}}},
    {"Carp",        TP_ROUND,   0, TM_ANY,   2, 2,  30,  80,  6,  60,  12, SF_BIG,
     {{110, 80, 40}, {190, 150, 70}, {240, 210, 140}, {150, 100, 50}, {170, 130, 60}}},
    {"Catfish",     TP_WHISKER, 0, TM_NIGHT, 2, 2,  40, 110,  7,  70,  16, SF_BIG,
     {{60, 55, 50}, {110, 100, 90}, {200, 190, 170}, {80, 70, 60}, {90, 80, 70}}},
    {"Golden Koi",  TP_ROUND,   0, TM_TWI,   1, 3,  30,  60,  5,  50,  45, 0,
     {{230, 110, 30}, {250, 180, 40}, {255, 240, 200}, {255, 250, 240}, {255, 255, 255}}},
    {"Old Boot",    TP_BOOT,    0, TM_ANY,   2, 1,  26,  30,  1,  10,   1, SF_JUNK,
     {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}},
    {"Grayling",    TP_SLIM,    1, TM_ANY,   0, 1,  20,  40,  3,  30,   7, 0,
     {{80, 90, 110}, {160, 170, 190}, {220, 225, 235}, {150, 80, 160}, {60, 70, 90}}},
    {"Trout",       TP_SLIM,    1, TM_DAY,   1, 1,  25,  60,  5,  45,  10, SF_PRED,
     {{70, 100, 80}, {150, 170, 120}, {240, 200, 190}, {120, 130, 100}, {220, 70, 70}}},
    {"Pike",        TP_LONG,    1, TM_ANY,   1, 2,  50, 120,  8,  70,  20, SF_PRED | SF_BIG,
     {{50, 80, 40}, {110, 150, 70}, {220, 220, 170}, {90, 110, 50}, {210, 220, 150}}},
    {"Salmon",      TP_SLIM,    1, TM_TWI,   2, 2,  50, 100,  8,  80,  24, SF_PRED | SF_BIG,
     {{60, 80, 110}, {190, 190, 200}, {250, 170, 150}, {120, 130, 150}, {40, 50, 70}}},
    {"Eel",         TP_LONG,    1, TM_NIGHT, 2, 3,  60, 130,  6,  90,  30, SF_GLOW,
     {{40, 50, 40}, {80, 90, 60}, {170, 170, 120}, {60, 60, 40}, {80, 90, 60}}},
    {"Sturgeon",    TP_WHISKER, 1, TM_NIGHT, 2, 4, 100, 250, 10, 140, 120, SF_BIG,
     {{70, 80, 90}, {140, 150, 160}, {220, 220, 225}, {100, 110, 120}, {230, 230, 235}}},
    {"Sardine",     TP_SLIM,    2, TM_DAY,   0, 1,  10,  22,  2,  20,   5, 0,
     {{40, 80, 140}, {180, 200, 220}, {240, 245, 250}, {120, 150, 180}, {60, 90, 140}}},
    {"Clownfish",   TP_ROUND,   2, TM_DAY,   1, 1,   8,  14,  3,  25,   9, 0,
     {{230, 90, 20}, {255, 140, 40}, {255, 170, 80}, {20, 20, 20}, {255, 255, 255}}},
    {"Pufferfish",  TP_PUFFER,  2, TM_ANY,   1, 2,  15,  40,  4,  40,  18, 0,
     {{150, 130, 60}, {220, 200, 120}, {250, 245, 220}, {180, 160, 90}, {90, 80, 40}}},
    {"Squid",       TP_SQUID,   2, TM_NIGHT, 2, 2,  30,  80,  6,  55,  22, SF_GLOW,
     {{200, 80, 120}, {240, 140, 170}, {255, 220, 230}, {170, 60, 100}, {255, 255, 255}}},
    {"Swordfish",   TP_SWORD,   2, TM_DAY,   2, 3, 120, 300,  9, 130,  90, SF_PRED | SF_BIG,
     {{30, 50, 100}, {90, 120, 180}, {220, 230, 240}, {40, 60, 110}, {200, 210, 230}}},
    {"Moonfish",    TP_ROUND,   2, TM_NIGHT, 0, 3,  40,  90,  6,  70,  60, SF_GLOW | SF_BIG,
     {{150, 160, 190}, {210, 215, 235}, {250, 250, 255}, {180, 190, 220}, {120, 240, 255}}},
    {"Lanternfish", TP_SLIM,    3, TM_ANY,   1, 1,   5,  15,  2,  25,  12, SF_GLOW,
     {{30, 30, 50}, {60, 60, 90}, {100, 100, 140}, {40, 40, 70}, {120, 255, 230}}},
    {"Anglerfish",  TP_ANGLER,  3, TM_NIGHT, 2, 2,  20,  60,  6,  60,  40, SF_GLOW | SF_PRED,
     {{50, 40, 40}, {90, 70, 60}, {140, 110, 90}, {70, 50, 40}, {160, 255, 200}}},
    {"Gulper Eel",  TP_LONG,    3, TM_ANY,   2, 2,  60, 180,  7,  80,  45, SF_BIG,
     {{20, 20, 25}, {50, 40, 50}, {230, 120, 160}, {40, 30, 40}, {60, 50, 60}}},
    {"Ghost Shark", TP_SHARK,   3, TM_NIGHT, 2, 3,  80, 150,  9, 110, 110, SF_BIG | SF_GLOW,
     {{170, 180, 200}, {210, 215, 230}, {245, 245, 250}, {150, 160, 185}, {120, 140, 170}}},
    {"Coelacanth",  TP_ROUND,   3, TM_ANY,   2, 4, 100, 200,  9, 150, 180, SF_BIG,
     {{30, 50, 90}, {60, 90, 140}, {120, 150, 190}, {40, 60, 100}, {220, 230, 255}}},
    {"Crystal Koi", TP_ROUND,   3, TM_NIGHT, 1, 4,  40,  70,  8, 120, 250, SF_GLOW,
     {{40, 80, 180}, {110, 200, 250}, {210, 245, 255}, {255, 255, 255}, {255, 255, 255}}},
};
#define N_SP ((int)(sizeof(SP) / sizeof(SP[0])))
#define N_LOC 4

typedef struct {
    const char *name;
    uint16_t price;
    uint8_t wtop[3], wdeep[3], shore[3], soil[3];
} loc_t;
static const loc_t LOC[N_LOC] = {
    {"Willow Pond",    0,   {70, 130, 100}, {10, 36, 30}, {70, 140, 60},  {110, 80, 50}},
    {"Mountain River", 150, {80, 140, 180}, {12, 36, 60}, {130, 130, 140}, {80, 80, 92}},
    {"Coral Coast",    350, {40, 190, 200}, {6, 48, 90},  {232, 212, 150}, {190, 160, 100}},
    {"The Abyss",      800, {22, 40, 84},   {2, 4, 14},   {44, 44, 64},   {26, 26, 40}},
};
static const char *const RARITY[] = {"", "Common", "Uncommon", "Rare", "Legendary"};

/* ── Caches ──────────────────────────────────────────────────────────── */
static uint16_t fish_r[N_SP][256], fish_l[N_SP][256];
static uint16_t fisher_px[256];
enum { IC_COIN, IC_SUN, IC_MOON, IC_FISH, IC_HOOK, IC_ROD, IC_REEL, IC_LINE, IC_MAP, IC_STAR,
       IC_LOCK, IC_GEAR, IC_BOOK, IC_EXIT, IC_COUNT };
static const char *const ICON_SRC[IC_COUNT] = {
    I_COIN, I_SUN, I_MOON, I_FISH, I_HOOK, I_ROD, I_REEL, I_LINE, I_MAP, I_STAR,
    I_LOCK, I_GEAR, I_BOOK, I_EXIT,
};
static uint16_t icon_px[IC_COUNT][64];
static uint16_t scratch[48 * 48];
static uint16_t veil50[256];

static void build_sprites(void)
{
    spr_init(SCR_W >= 400);
    for (int s = 0; s < N_SP; s++) {
        for (int k = 0; k < 5; k++)
            spr_pal_rgb((char)('1' + k), SP[s].pal[k][0], SP[s].pal[k][1], SP[s].pal[k][2]);
        spr_build(fish_r[s], TMPL[SP[s].tmpl], 256, 0);
        spr_mirror(fish_l[s], fish_r[s], 16, 16);
    }
    spr_build(fisher_px, A_FISHER, 256, 0);
    for (int i = 0; i < IC_COUNT; i++) spr_build(icon_px[i], ICON_SRC[i], 64, 0);
    spr_dither16(veil50, 50, 0x0000);
    spr_theme_init(80, 200, 230);
}
static void blit_icon(int x, int y, int ic) { spr_blit(x, y, 8, 8, icon_px[ic]); }

/* ── Saved progress ──────────────────────────────────────────────────── */
static int coins, unlocked, rod, reel, line_lvl, lures, lure, loc, caught, catches, tz;
static int ctl_buttons, ctl_axis, ctl_invert;
static uint16_t best_cm[N_SP];
static uint8_t catch_n[N_SP];

static void save_progress(void)
{
    g_save_int("angler/coins", coins);
    g_save_int("angler/unlk", unlocked);
    g_save_int("angler/gear", rod | reel << 4 | line_lvl << 8 | lures << 12 | lure << 16 | loc << 20);
    g_save_int("angler/dex", caught);
    g_save_int("angler/n", catches);
}
static void save_options(void)
{
    g_save_int("angler/tz", tz);
    g_save_int("angler/ctl", ctl_buttons | ctl_axis << 1 | ctl_invert << 2);
}
static void save_best(int s)
{
    char k[16] = "angler/b";
    g_cat_int(k, s);
    g_save_int(k, best_cm[s] | catch_n[s] << 16);
}
static void load_progress(void)
{
    coins = g_load_int("angler/coins", 20);
    unlocked = g_load_int("angler/unlk", 1) | 1;
    int g = g_load_int("angler/gear", 0);
    rod = g & 15; reel = g >> 4 & 15; line_lvl = g >> 8 & 15; lures = g >> 12 & 15;
    lure = g >> 16 & 15; loc = g >> 20 & 15;
    if (rod > 3) rod = 3;
    if (reel > 3) reel = 3;
    if (line_lvl > 2) line_lvl = 2;
    if (loc >= N_LOC || !(unlocked & (1 << loc))) loc = 0;
    if (lure > 3 || (lure && !(lures & (1 << lure)))) lure = 0;
    caught = g_load_int("angler/dex", 0);
    catches = g_load_int("angler/n", 0);
    tz = g_load_int("angler/tz", 0);
    int c = g_load_int("angler/ctl", 0);
    ctl_buttons = c & 1; ctl_axis = c >> 1 & 1; ctl_invert = c >> 2 & 1;
    for (int s = 0; s < N_SP; s++) {
        if (!(caught & (1 << s))) continue;
        char k[16] = "angler/b";
        g_cat_int(k, s);
        int v = g_load_int(k, 0);
        best_cm[s] = (uint16_t)(v & 0xFFFF);
        catch_n[s] = (uint8_t)(v >> 16 & 255);
    }
}
static int dex_count(void)
{
    int n = 0;
    for (int s = 0; s < N_SP; s++) n += (caught >> s) & 1;
    return n;
}

/* ── Helpers ─────────────────────────────────────────────────────────── */
static int rn(int n) { return n > 0 ? (int)g_rand_n((uint32_t)n) : 0; }
static int chance(int p) { return rn(100) < p; }
static int iabs(int v) { return v < 0 ? -v : v; }
static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }
static int iclamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static void bclear(void *p, int n) { uint8_t *b = p; for (int i = 0; i < n; i++) b[i] = 0; }
static void scopy(char *d, const char *s, int n) { int i = 0; for (; s[i] && i < n - 1; i++) d[i] = s[i]; d[i] = 0; }
static uint16_t mix(const uint8_t *a, const uint8_t *b, int t, int n)
{
    return spr_col(a[0] + (b[0] - a[0]) * t / n, a[1] + (b[1] - a[1]) * t / n, a[2] + (b[2] - a[2]) * t / n);
}

/* ── Clock ───────────────────────────────────────────────────────────── */
enum { PH_DAWN, PH_DAY, PH_DUSK, PH_NIGHT };
static int clock_real;

static int clock_minutes(void)
{
    int t = rtc_get_unix_time();
    if (t > 1700000000) {
        clock_real = 1;
        int m = (t / 60) % 1440 + tz * 60;
        return ((m % 1440) + 1440) % 1440;
    }
    clock_real = 0;                      /* no RTC: a 24-minute day from 08:00 */
    return (8 * 60 + (int)(g_now() / 1000)) % 1440;
}
static int phase_of(int m)
{
    int h = m / 60;
    if (h >= 5 && h < 7) return PH_DAWN;
    if (h >= 7 && h < 18) return PH_DAY;
    if (h >= 18 && h < 20) return PH_DUSK;
    return PH_NIGHT;
}
static const char *const PHASE_NAME[] = {"Dawn", "Day", "Dusk", "Night"};
static const uint8_t SKY[4][2][3] = {
    {{40, 40, 96}, {244, 156, 124}},
    {{80, 150, 236}, {196, 228, 255}},
    {{56, 36, 100}, {250, 124, 72}},
    {{6, 8, 24}, {26, 32, 70}},
};

/* ── IMU ─────────────────────────────────────────────────────────────── */
static int tilt_zero, imu_ok = -1;

static int tilt_raw(void)
{
    int v = sensor_read(ctl_axis ? SENSOR_CHAN_ACCEL_Y : SENSOR_CHAN_ACCEL_X);
    if (v == AKIRA_SENSOR_ERROR) { imu_ok = 0; return 0; }
    imu_ok = 1;
    return ctl_invert ? -v : v;
}
static void tilt_calibrate(void) { tilt_zero = tilt_raw(); }

/* Tilt in -100..100 (about +-25 degrees for full scale), with a dead zone.
 * Falls back to LEFT/RIGHT held on the D-pad. */
static int tilt_now(void)
{
    if (!ctl_buttons) {
        int r = tilt_raw();
        if (imu_ok) {
            int t = (r - tilt_zero) * 100 / 4200;
            if (iabs(t) < 12) t = 0;
            return iclamp(t, -100, 100);
        }
    }
    uint32_t held = (uint32_t)input_get_buttons();
    if (held & AKIRA_BTN_LEFT) return -100;
    if (held & AKIRA_BTN_RIGHT) return 100;
    return 0;
}
static int a_held(void) { return ((uint32_t)input_get_buttons() & AKIRA_BTN_A) != 0; }

/* ── Fish in the water ───────────────────────────────────────────────── */
#define MAX_FISH 6
enum { FS_SWIM, FS_APPROACH, FS_NIBBLE, FS_BITE, FS_FLEE };
typedef struct {
    int16_t x16, y16, ty16;
    int8_t dir;
    uint8_t alive, sp, state, t, nib, speed, respawn;
} fish_t;
static fish_t fish[MAX_FISH];
static int phase_now, minutes_now;

static int band_top(int d) { return WATER_TOP + (d == 0 ? 10 : d == 1 ? 42 : 80); }
static int band_bot(int d) { return d == 0 ? WATER_TOP + 36 : d == 1 ? WATER_TOP + 76 : WATER_BOT - 14; }
static int max_lure_y(void)
{
    int pct = line_lvl == 0 ? 62 : line_lvl == 1 ? 82 : 100;
    return WATER_TOP + 8 + (WATER_BOT - 14 - WATER_TOP - 8) * pct / 100;
}

static int sp_weight(int s)
{
    const species_t *p = &SP[s];
    if (p->loc != loc || !(p->time & (1 << phase_now))) return 0;
    static const int RW[] = {0, 60, 26, 9, 3};
    int w = RW[p->rarity] * 10;
    if (lure == 1) w = (p->flags & SF_PRED) ? w * 3 / 2 : w * 7 / 10;
    if (lure == 2 && (p->flags & SF_GLOW)) w *= 2;
    if (lure == 3 && p->rarity >= 3) w = w * 9 / 5;
    return w;
}

static void spawn_fish(int i)
{
    int tot = 0;
    for (int s = 0; s < N_SP; s++) tot += sp_weight(s);
    fish_t *f = &fish[i];
    bclear(f, sizeof(*f));
    if (!tot) return;
    int r = rn(tot), s = 0;
    for (; s < N_SP; s++) { int w = sp_weight(s); if (r < w) break; r -= w; }
    if (s >= N_SP) s = 0;
    f->alive = 1; f->sp = (uint8_t)s;
    f->dir = (int8_t)(rn(2) ? 1 : -1);
    f->x16 = (int16_t)((f->dir > 0 ? DOCK_END - 20 : SCR_W + 20) * 16);
    if (rn(3)) f->x16 = (int16_t)((DOCK_END + 20 + rn(SCR_W - DOCK_END - 40)) * 16);
    int d = SP[s].depth;
    f->y16 = f->ty16 = (int16_t)((band_top(d) + rn(band_bot(d) - band_top(d) + 1)) * 16);
    f->speed = (uint8_t)(10 + rn(10) + (SP[s].flags & SF_PRED ? 6 : 0));
    f->state = FS_SWIM;
}

static void reset_fish(void)
{
    for (int i = 0; i < MAX_FISH; i++) {
        spawn_fish(i);
        if (i >= 3 + loc / 2) { fish[i].alive = 0; fish[i].respawn = (uint8_t)(10 + rn(40)); }
    }
}

/* ── Fishing state ───────────────────────────────────────────────────── */
enum { ST_IDLE, ST_CHARGE, ST_CAST, ST_WAIT, ST_FIGHT };
static int st, power, pdir, lx16, ly16, lty, cast_t, cast_x, dip, engaged, twitch;
static int tension, dist, dist0, stam, stam_max, burst_t, burst_dir, rest_t, snap_t, slack_t;
static int hook_sp, hook_cm, hook_frac, fish_y0;
static uint32_t anim_tick;
static char msgb[44];
static uint16_t msg_c;
static int msg_t;
static const char *hint_override;

#define ROD_TX (DOCK_END + 18)
#define ROD_TY (WATER_TOP - 46)
#define CAST_MIN (DOCK_END + 26)

static void msg(const char *s, uint16_t c) { scopy(msgb, s, 44); msg_c = c; msg_t = 30; }

/* particles: bubbles, splashes and rings (life-coded by kind) */
#define MAX_PART 48
enum { PK_DROP, PK_BUBBLE, PK_RING };
typedef struct { int16_t x, y, vx, vy; uint16_t col; uint8_t life, kind; } part_t;
static part_t parts[MAX_PART];
static void spawn_part(int x, int y, int vx, int vy, uint16_t col, int life, int kind)
{
    for (int i = 0; i < MAX_PART; i++) {
        if (parts[i].life) continue;
        parts[i].x = (int16_t)(x * 16); parts[i].y = (int16_t)(y * 16);
        parts[i].vx = (int16_t)vx; parts[i].vy = (int16_t)vy;
        parts[i].col = col; parts[i].life = (uint8_t)life; parts[i].kind = (uint8_t)kind;
        return;
    }
}
static void splash(int x, int y, int n)
{
    uint16_t c = spr_col(220, 240, 255);
    for (int i = 0; i < n; i++) spawn_part(x, y, rn(25) - 12, -10 - rn(16), c, 6 + rn(4), PK_DROP);
    spawn_part(x, y, 0, 0, c, 8, PK_RING);
}

/* ── Scene ───────────────────────────────────────────────────────────── */
static uint32_t h32(uint32_t x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; return x ^ (x >> 16); }

static void draw_sky(void)
{
    int top = HUD_H, hor = WATER_TOP - 14, bands = 8;
    const uint8_t *a = SKY[phase_now][0], *b = SKY[phase_now][1];
    for (int i = 0; i < bands; i++) {
        int y0 = top + (hor - top) * i / bands, y1 = top + (hor - top) * (i + 1) / bands;
        display_rect(0, y0, SCR_W, y1 - y0, mix(a, b, i, bands - 1));
    }
    int m = minutes_now;
    if (phase_now == PH_NIGHT) {
        for (int i = 0; i < 26; i++) {
            uint32_t h = h32((uint32_t)i * 2654435761u);
            int x = (int)(h % (uint32_t)SCR_W), y = top + 2 + (int)((h >> 12) % (uint32_t)(hor - top - 6));
            if (((h >> 20) + anim_tick / 3) % 7) display_pixel(x, y, 0xFFFF);
        }
        int t = (m + 240) % 1440;                     /* 20:00 -> 0 .. 05:00 -> 540 */
        int mx = 20 + (SCR_W - 40) * imin(t, 540) / 540;
        int my = top + 22 - (int)(14 * (270 - iabs(imin(t, 540) - 270)) / 270);
        display_circle_fill(mx, my, 7, spr_col(240, 236, 210));
        display_circle_fill(mx + 3, my - 2, 6, mix(a, b, 1, 7));
    } else {
        int t = iclamp(m - 300, 0, 900);              /* 05:00 .. 20:00 */
        int sx = 20 + (SCR_W - 40) * t / 900;
        int sy = top + 26 - (int)(18 * (450 - iabs(t - 450)) / 450);
        uint16_t sc = phase_now == PH_DAY ? spr_col(255, 236, 120) : spr_col(255, 170, 90);
        if (!spr_mono || phase_now != PH_DAY) display_circle_fill(sx, sy, 8, sc);
        if (phase_now == PH_DAY) {
            uint16_t cc = spr_col(250, 252, 255);
            for (int c = 0; c < 3; c++) {
                int cx = (int)((anim_tick / 2 + (uint32_t)c * 140) % (uint32_t)(SCR_W + 80)) - 40;
                int cy = top + 10 + c * 9;
                display_circle_fill(cx, cy, 6, cc); display_circle_fill(cx + 8, cy - 2, 7, cc);
                display_circle_fill(cx + 16, cy, 5, cc); display_rect(cx, cy, 16, 5, cc);
            }
        }
    }
    /* far shore */
    uint16_t far = mix(b, LOC[loc].soil, 3, 5);
    if (spr_mono) far = 0x0000;
    int fy = hor;
    switch (loc) {
    case 0:
        for (int x = -10; x < SCR_W + 10; x += 22) display_circle_fill(x + (int)(h32((uint32_t)x) % 8), fy, 10 + (int)(h32((uint32_t)x + 7) % 5), far);
        break;
    case 1:
        for (int x = -20; x < SCR_W + 20; x += 46) {
            int hgt = 22 + (int)(h32((uint32_t)x) % 14);
            display_triangle_fill(x, fy + 2, x + 30, fy - hgt, x + 60, fy + 2, far);
            if (!spr_mono) display_triangle_fill(x + 24, fy - hgt + 7, x + 30, fy - hgt, x + 36, fy - hgt + 7, 0xFFFF);
        }
        break;
    case 2:
        display_rect(SCR_W - 70, fy - 6, 60, 6, far);
        display_rect(SCR_W - 52, fy - 26, 3, 20, far);
        display_circle_fill(SCR_W - 50, fy - 26, 7, far);
        break;
    default: break;
    }
    display_rect(0, fy, SCR_W, WATER_TOP - fy, far);
}

static void water_col(int i, int n, uint16_t *out)
{
    if (spr_mono) { *out = 0x0000; return; }   /* black water: white fish read on the Sharp */
    uint8_t a[3], b[3];
    for (int k = 0; k < 3; k++) {
        a[k] = LOC[loc].wtop[k]; b[k] = LOC[loc].wdeep[k];
        if (phase_now == PH_NIGHT) { a[k] = (uint8_t)(a[k] * 45 / 100); b[k] = (uint8_t)(b[k] * 60 / 100); }
        else if (phase_now != PH_DAY) { a[k] = (uint8_t)(a[k] * 75 / 100); }
    }
    *out = mix(a, b, i, n - 1);
}

static void draw_water(void)
{
    int bands = 7;
    for (int i = 0; i < bands; i++) {
        int y0 = WATER_TOP + (WATER_BOT - WATER_TOP) * i / bands, y1 = WATER_TOP + (WATER_BOT - WATER_TOP) * (i + 1) / bands;
        uint16_t c;
        water_col(i, bands, &c);
        display_rect(0, y0, SCR_W, y1 - y0, c);
    }
    uint16_t hi = spr_col(LOC[loc].wtop[0] / 2 + 120, LOC[loc].wtop[1] / 2 + 120, LOC[loc].wtop[2] / 2 + 120);
    if (spr_mono) hi = 0xFFFF;
    display_hline(0, WATER_TOP, SCR_W, hi);
    for (int i = 0; i < 9; i++) {
        uint32_t h = h32((uint32_t)i * 977u);
        int x = (int)((h % (uint32_t)SCR_W + anim_tick * (1 + i % 2)) % (uint32_t)SCR_W);
        display_hline(x, WATER_TOP + 3 + (int)((h >> 8) % 6u), 6 + (int)(h % 6u), hi);
    }
    /* bottom and decor */
    uint16_t dark = spr_mono ? 0x0000 : mix(LOC[loc].soil, LOC[loc].wdeep, 1, 2);
    for (int x = 0; x < SCR_W; x += 8) {
        int hgt = 4 + (int)(h32((uint32_t)x * 31u) % 6u);
        display_rect(x, WATER_BOT - hgt, 8, hgt, spr_mono ? 0xFFFF : dark);
    }
    switch (loc) {
    case 0: {
        uint16_t g = spr_col(60, 140, 60), br = spr_col(120, 80, 40), pad = spr_col(80, 170, 70);
        for (int i = 0; i < 6; i++) {
            int x = DOCK_END + 40 + i * 41 + (int)(h32((uint32_t)i) % 18);
            if (x > SCR_W - 6) break;
            display_vline(x, WATER_BOT - 30, 26, g);
            display_vline(x + 3, WATER_BOT - 24, 20, g);
        }
        for (int i = 0; i < 3; i++) {
            int x = 150 + i * 60 + (int)(h32((uint32_t)i + 99) % 20);
            if (x > SCR_W - 12) break;
            display_circle_fill(x, WATER_TOP + 1, 6, pad);
            display_triangle_fill(x, WATER_TOP + 1, x + 7, WATER_TOP - 2, x + 7, WATER_TOP + 4, hi);
        }
        for (int i = 0; i < 4; i++) {                  /* reeds on the far side */
            int x = SCR_W - 10 - i * 6;
            display_vline(x, WATER_TOP - 18 + i * 2, 20 - i * 2, g);
            display_rect(x - 1, WATER_TOP - 18 + i * 2, 3, 6, br);
        }
        break;
    }
    case 1: {
        uint16_t rk = spr_col(110, 110, 120), rh = spr_col(160, 160, 170);
        for (int i = 0; i < 5; i++) {
            int x = DOCK_END + 30 + i * 52 + (int)(h32((uint32_t)i + 5) % 20);
            display_circle_fill(x, WATER_BOT - 6, 10, rk);
            display_hline(x - 5, WATER_BOT - 13, 7, rh);
        }
        for (int i = 0; i < 6; i++) {                  /* current streaks */
            int y = WATER_TOP + 12 + i * 17;
            int x = (int)((anim_tick * 4 + (uint32_t)i * 53) % (uint32_t)(SCR_W + 40)) - 20;
            display_hline(x, y, 14, hi);
        }
        break;
    }
    case 2: {
        static const uint8_t CC[3][3] = {{250, 110, 140}, {255, 170, 80}, {170, 120, 230}};
        for (int i = 0; i < 6; i++) {
            int x = DOCK_END + 20 + i * 40 + (int)(h32((uint32_t)i + 11) % 16);
            uint16_t c = spr_col(CC[i % 3][0], CC[i % 3][1], CC[i % 3][2]);
            display_vline(x, WATER_BOT - 22, 18, c);
            display_line(x, WATER_BOT - 14, x - 6, WATER_BOT - 22, c);
            display_line(x, WATER_BOT - 10, x + 6, WATER_BOT - 20, c);
        }
        break;
    }
    default:
        for (int i = 0; i < 20; i++) {
            uint32_t h = h32((uint32_t)i * 7919u);
            int x = (int)(h % (uint32_t)SCR_W);
            int y = WATER_TOP + 10 + (int)((h >> 8) % (uint32_t)(WATER_BOT - WATER_TOP - 20));
            y -= (int)((anim_tick + h) % 40u) / 4;
            if ((h >> 16) & 1) display_pixel(x, y, spr_col(120, 255, 230));
        }
        break;
    }
}

static void draw_shore(void)
{
    int dock_y = WATER_TOP - 8;
    uint16_t sh = spr_col(LOC[loc].shore[0], LOC[loc].shore[1], LOC[loc].shore[2]);
    uint16_t so = spr_col(LOC[loc].soil[0], LOC[loc].soil[1], LOC[loc].soil[2]);
    display_rect(0, dock_y - 2, 34, WATER_BOT - dock_y + 2, so);
    display_rect(0, dock_y - 4, 36, 5, sh);
    display_triangle_fill(34, dock_y - 4, 34, WATER_BOT, 46, WATER_BOT, so);
    uint16_t wd = spr_col(150, 100, 60), wdk = spr_col(90, 58, 34);
    display_rect(30, dock_y, DOCK_END - 30, 6, wd);
    for (int x = 30; x < DOCK_END; x += 9) display_vline(x, dock_y, 6, wdk);
    display_hline(30, dock_y + 6, DOCK_END - 30, wdk);
    display_rect(DOCK_END - 6, dock_y + 6, 4, WATER_BOT - dock_y - 12, wdk);
    display_rect(44, dock_y + 6, 4, WATER_BOT - dock_y - 12, wdk);
    spr_blit_scaled(DOCK_END - 40, dock_y - 32, fisher_px, 16, 16, 2, scratch);
}

static int lure_x(void) { return lx16 / 16; }
static int lure_y(void) { return ly16 / 16; }

static void draw_line_and_lure(void)
{
    uint16_t rodc = spr_col(120, 80, 40), linec = spr_mono ? 0xFFFF : spr_col(235, 235, 240);
    int hx = DOCK_END - 14, hy = WATER_TOP - 26;          /* hands */
    int bend = (st == ST_FIGHT) ? tension / 60 : 0;
    display_line(hx, hy, ROD_TX, ROD_TY + bend, rodc);
    display_line(hx, hy + 1, ROD_TX, ROD_TY + bend + 1, rodc);
    if (st == ST_IDLE || st == ST_CHARGE) {
        display_line(ROD_TX, ROD_TY + bend, ROD_TX, ROD_TY + 18, linec);
        display_circle_fill(ROD_TX, ROD_TY + 20, 2, spr_col(230, 60, 50));
        return;
    }
    int bx = lure_x(), by = WATER_TOP;
    if (st == ST_CAST) {
        int t = cast_t, x = ROD_TX + (cast_x - ROD_TX) * t / 5;
        int y = ROD_TY + (WATER_TOP - ROD_TY) * t * t / 25 - 30 * t * (5 - t) / 25;
        display_line(ROD_TX, ROD_TY, x, y, linec);
        display_circle_fill(x, y, 2, spr_col(230, 60, 50));
        return;
    }
    int fx = bx, fy = lure_y();
    if (st == ST_FIGHT && engaged >= 0) { fx = fish[engaged].x16 / 16; fy = fish[engaged].y16 / 16; bx = fx; }
    int mx = (ROD_TX + bx) / 2, my = (ROD_TY + by) / 2 + (st == ST_FIGHT ? 4 - tension / 200 : 10);
    display_line(ROD_TX, ROD_TY + bend, mx, my, linec);
    display_line(mx, my, bx, by, linec);
    if (st == ST_WAIT) {
        for (int y = by + 2; y < fy; y += 3) display_pixel(bx, y, linec);
        int d = dip > 50 ? 99 : dip ? 2 : 0;
        if (d < 99) {
            display_rect(bx - 2, by - 5 + d, 5, 4, spr_col(230, 60, 50));
            display_rect(bx - 2, by - 1 + d, 5, 3, 0xFFFF);
            display_pixel(bx, by - 7 + d, spr_col(230, 60, 50));
        }
        display_pixel(bx, fy, spr_col(200, 200, 210));
        display_pixel(bx + 1, fy + 1, spr_col(200, 200, 210));
    } else {
        display_line(bx, by, fx, fy, linec);
    }
}

static void draw_fish_one(int i)
{
    fish_t *f = &fish[i];
    const species_t *p = &SP[f->sp];
    int x = f->x16 / 16, y = f->y16 / 16;
    const uint16_t *src = f->dir > 0 ? fish_r[f->sp] : fish_l[f->sp];
    uint16_t c;
    if (spr_mono) c = 0xFFFF;
    else if (p->flags & SF_GLOW && (phase_now == PH_NIGHT || loc == 3))
        c = spr_col(p->pal[4][0] / 2 + 20, p->pal[4][1] / 2 + 20, p->pal[4][2] / 2 + 20);
    else {
        uint16_t w;
        water_col(5, 7, &w);
        int r = ((w >> 11) & 31) << 3, g = ((w >> 5) & 63) << 2, b = (w & 31) << 3;
        c = spr_col(r * 45 / 100, g * 45 / 100, b * 45 / 100);
    }
    int big = (p->flags & SF_BIG) != 0;
    if (big) {
        for (int k = 0; k < 256; k++) scratch[1024 + k] = src[k] == SPR_KEY ? SPR_KEY : c;
        spr_blit_scaled(x - 16, y - 16, scratch + 1024, 16, 16, 2, scratch);
    } else {
        spr_blit_tint(x - 8, y - 8, src, 16, 16, c, scratch);
    }
    if (p->rarity >= 3 && ((anim_tick + (uint32_t)i * 5) & 15) == 0)
        display_pixel(x + (f->dir > 0 ? 4 : -4), y - 2, 0xFFFF);
}

static void draw_parts(void)
{
    for (int i = 0; i < MAX_PART; i++) {
        part_t *p = &parts[i];
        if (!p->life) continue;
        int x = p->x / 16, y = p->y / 16;
        if (p->kind == PK_RING) display_circle(x, y, 10 - p->life, p->col);
        else if (p->kind == PK_BUBBLE) display_circle(x, y, 1, p->col);
        else display_rect(x, y, 2, 2, p->col);
    }
}

static void draw_hud(void)
{
    char b[32];
    display_rect(0, 0, SCR_W, HUD_H, SPRT.panel);
    display_hline(0, HUD_H - 1, SCR_W, SPRT.accent);
    blit_icon(4, 7, IC_COIN);
    b[0] = 0; g_cat_int(b, coins); display_text(15, 6, b, SPRT.gold);
    g_text_c(SCR_W / 2, 6, LOC[loc].name, SPRT.accent);
    int m = minutes_now;
    b[0] = 0;
    if (!clock_real) g_cat(b, "~");
    if (m / 60 < 10) g_cat(b, "0");
    g_cat_int(b, m / 60); g_cat(b, m % 60 < 10 ? ":0" : ":"); g_cat_int(b, m % 60);
    g_text_r(SCR_W - 4, 6, b, SPRT.text);
    blit_icon(SCR_W - 4 - g_tw(b) - 12, 7, phase_now == PH_NIGHT ? IC_MOON : IC_SUN);
}

static void draw_bar(void)
{
    int y0 = SCR_H - BAR_H;
    display_rect(0, y0, SCR_W, BAR_H, SPRT.panel);
    display_hline(0, y0, SCR_W, SPRT.accent);
    const char *h = hint_override;
    if (!h) {
        switch (st) {
        case ST_IDLE: h = "Hold A: cast   HOME: menu"; break;
        case ST_CHARGE: h = "Release A to cast!"; break;
        case ST_WAIT: h = "UP/DN depth B twitch X reel in"; break;
        case ST_FIGHT: h = "Hold A: reel  Tilt against pull"; break;
        default: h = ""; break;
        }
    }
    if (msg_t && msgb[0]) {
        display_text(4, y0 + 2, msgb, msg_c);
        display_text(4, y0 + 12, h, SPRT.dim);
    } else {
        display_text(4, y0 + 7, h, SPRT.dim);
    }
}

static void draw_fight_ui(void)
{
    int w = SCR_W * 5 / 8, x = (SCR_W - w) / 2, y = HUD_H + 6;
    int lim = 880 + rod * 40;
    display_rect(x - 4, y - 3, w + 8, 34, SPRT.panel);
    display_rect_outline(x - 4, y - 3, w + 8, 34, SPRT.accent);
    display_text(x, y - 1, "TENSION", SPRT.dim);
    int by = y + 10;
    display_rect(x, by, w, 7, SPRT.barbg);
    int zx = w * 700 / 1000, sx = w * lim / 1000;
    if (!spr_mono) {
        display_rect(x, by, zx, 7, spr_col(40, 110, 60));
        display_rect(x + zx, by, sx - zx, 7, spr_col(150, 120, 30));
        display_rect(x + sx, by, w - sx, 7, spr_col(150, 40, 40));
    }
    int tx = x + w * tension / 1000;
    uint16_t mk = tension >= lim * 9 / 10 && (anim_tick & 1) ? SPRT.red : 0xFFFF;
    display_rect(tx - 1, by - 2, 3, 11, mk);
    display_rect_outline(x - 1, by - 1, w + 2, 9, spr_mono ? 0xFFFF : 0x0000);
    /* tilt gauge and the direction to tilt */
    int gy = by + 12, gw = 60, gx = x + w / 2 - gw / 2, t = tilt_now();
    display_hline(gx, gy + 3, gw, SPRT.dim);
    display_vline(gx + gw / 2, gy, 7, SPRT.dim);
    display_rect(gx + gw / 2 + t * (gw / 2) / 100 - 2, gy, 5, 7, SPRT.cyan);
    if (burst_t && (anim_tick & 2)) {
        const char *s = burst_dir > 0 ? "<< TILT" : "TILT >>";
        if (burst_dir > 0) display_text(gx - 62, gy - 1, s, SPRT.yellow);
        else display_text(gx + gw + 6, gy - 1, s, SPRT.yellow);
    }
    char b[16]; b[0] = 0;
    g_cat_int(b, dist / 25 / 10); g_cat(b, "."); g_cat_int(b, dist / 25 % 10); g_cat(b, "m");
    g_text_r(x + w, y - 1, b, SPRT.text);
    spr_bar(x + w - 92, y + 1, 36, 3, stam, stam_max, SPRT.orange);
}

static void draw_world(void)
{
    display_clear(0x0000);
    draw_sky();
    draw_water();
    for (int i = 0; i < MAX_FISH; i++) if (fish[i].alive) draw_fish_one(i);
    draw_shore();
    draw_line_and_lure();
    draw_parts();
    if (st == ST_CHARGE) {
        int x = DOCK_END - 52, y = WATER_TOP - 70, h = 44;
        display_rect(x, y, 8, h, SPRT.barbg);
        display_rect(x, y + h - h * power / 100, 8, h * power / 100, power > 80 ? SPRT.red : SPRT.yellow);
        display_rect_outline(x - 1, y - 1, 10, h + 2, spr_mono ? 0xFFFF : 0x0000);
    }
    if (st == ST_WAIT && engaged >= 0 && fish[engaged].state == FS_BITE && (anim_tick & 1))
        spr_text_out(lure_x() - 3, WATER_TOP - 22, "!", SPRT.yellow);
    if (st == ST_FIGHT) draw_fight_ui();
    draw_hud();
    draw_bar();
}

static void tick_fx(void)
{
    anim_tick++;
    for (int i = 0; i < MAX_PART; i++) {
        part_t *p = &parts[i];
        if (!p->life) continue;
        p->life--;
        if (p->kind == PK_RING) continue;
        p->x += p->vx; p->y += p->vy;
        if (p->kind == PK_DROP) p->vy += 4;
        if (p->kind == PK_BUBBLE && p->y / 16 <= WATER_TOP) p->life = 0;
    }
    if (msg_t) msg_t--;
    if (dip && dip < 50) dip--;
}

/* ── Fish behaviour ──────────────────────────────────────────────────── */
static void fish_tick(void)
{
    int lx = lure_x(), ly = lure_y(), settled = (st == ST_WAIT && iabs(ly - lty) < 3);
    for (int i = 0; i < MAX_FISH; i++) {
        fish_t *f = &fish[i];
        if (!f->alive) {
            if (f->respawn && --f->respawn == 0) spawn_fish(i);
            else if (!f->respawn) f->respawn = (uint8_t)(20 + rn(40));
            continue;
        }
        if (st == ST_FIGHT && i == engaged) continue;
        int x = f->x16 / 16, y = f->y16 / 16;
        if (chance(3)) spawn_part(x + f->dir * 6, y - 3, 0, -6, spr_col(200, 230, 255), 12, PK_BUBBLE);
        switch (f->state) {
        case FS_SWIM:
            f->x16 += (int16_t)(f->dir * f->speed);
            if (chance(2) || x < DOCK_END - 24 || x > SCR_W + 24) {
                if (x < DOCK_END - 24) f->dir = 1;
                else if (x > SCR_W + 24) f->dir = -1;
                else f->dir = (int8_t)-f->dir;
            }
            if (chance(3)) {
                int d = SP[f->sp].depth;
                f->ty16 = (int16_t)((band_top(d) + rn(band_bot(d) - band_top(d) + 1)) * 16);
            }
            f->y16 += (int16_t)((f->ty16 - f->y16) / 12);
            if (settled && engaged < 0 && iabs(y - ly) <= 20 && iabs(x - lx) <= 70) {
                int ch = 5 + (twitch && (SP[f->sp].flags & SF_PRED) ? 8 : 0);
                if (lure == 2 && (SP[f->sp].flags & SF_GLOW)) ch += 4;
                if (chance(ch)) { f->state = FS_APPROACH; engaged = i; }
            }
            break;
        case FS_APPROACH: {
            int tx = lx - f->dir * 9;
            f->dir = (int8_t)(lx >= x ? 1 : -1);
            int dx = tx - x, dy = ly - y;
            f->x16 += (int16_t)(dx > 0 ? imin(dx * 16, f->speed + 8) : imax(dx * 16, -(f->speed + 8)));
            f->y16 += (int16_t)(dy * 16 / 4);
            if (iabs(dx) < 3 && iabs(dy) < 4) { f->state = FS_NIBBLE; f->nib = (uint8_t)(1 + rn(3)); f->t = (uint8_t)(4 + rn(6)); }
            if (!settled) { f->state = FS_FLEE; engaged = -1; }
            break;
        }
        case FS_NIBBLE:
            if (--f->t == 0) {
                dip = 3;
                splash(lx, WATER_TOP, 1);
                if (--f->nib == 0) {
                    f->state = FS_BITE;
                    f->t = (uint8_t)(6 + (lure == 2 ? 1 : 0));
                    dip = 99;
                    splash(lx, WATER_TOP, 6);
                } else f->t = (uint8_t)(3 + rn(6));
            }
            break;
        case FS_BITE:
            if (--f->t == 0) {
                f->state = FS_FLEE; engaged = -1; dip = 0;
                msg("It got away with the bait...", SPRT.dim);
            }
            break;
        default:
            f->x16 += (int16_t)(f->dir * f->speed * 3);
            if (x < -30 || x > SCR_W + 30) { f->alive = 0; f->respawn = (uint8_t)(20 + rn(40)); }
            break;
        }
    }
    if (twitch) twitch--;
}

/* ── Fight ───────────────────────────────────────────────────────────── */
static void start_fight(void)
{
    fish_t *f = &fish[engaged];
    const species_t *p = &SP[f->sp];
    hook_sp = f->sp;
    int r = rn(1000);
    hook_frac = r * r / 1000;
    if (lure == 3) hook_frac = 1000 - (1000 - hook_frac) * 7 / 10;
    hook_cm = p->cmin + (p->cmax - p->cmin) * hook_frac / 1000;
    stam_max = stam = (p->stam * (60 + hook_frac * 40 / 1000) / 100 + 10) * 10;
    dist0 = dist = 400 + p->str * 60;
    tension = 350; burst_t = 0; rest_t = 4; snap_t = slack_t = 0;
    fish_y0 = f->y16 / 16;
    st = ST_FIGHT;
    tilt_calibrate();
    dip = 0;
    splash(lure_x(), WATER_TOP, 8);
    msg("Fish on! Reel it in!", SPRT.yellow);
    g_flush_input();
}

static void end_fight_lost(const char *why)
{
    fish[engaged].state = FS_FLEE;
    engaged = -1;
    st = ST_IDLE;
    msg(why, SPRT.red);
}

/* One frame of the fight.  Returns 1 if the fish was landed. */
static int fight_tick(void)
{
    const species_t *p = &SP[hook_sp];
    fish_t *f = &fish[engaged];
    int bursting = 0;
    if (rest_t) {
        if (--rest_t == 0) { burst_t = 4 + rn(7); burst_dir = rn(2) ? 1 : -1; }
    } else if (burst_t) {
        bursting = 1;
        if (--burst_t == 0) rest_t = imax(2, 4 + rn(6) - p->str / 3);
    }
    int stamf = 400 + 600 * stam / imax(1, stam_max);
    int pow = p->str * 10 * stamf / 1000 * (90 + hook_frac / 20) / 100;
    int reeling = a_held(), t = tilt_now();
    int opp = (t * burst_dir < 0) ? iabs(t) : 0, match = (t * burst_dir > 0) ? iabs(t) : 0;
    int dT = reeling ? 18 : -45;
    if (bursting) dT += pow * (100 - opp * 85 / 100 + match * 40 / 100) / 100;
    else dT -= 12;
    tension = iclamp(tension + dT, 0, 1000);
    int lim = 880 + rod * 40;
    if (tension >= lim) { if (++snap_t >= 3) { end_fight_lost("SNAP! The line broke."); return 0; } }
    else snap_t = 0;
    if (tension <= 0) { if (++slack_t >= 10) { end_fight_lost("Slack line - it threw the hook."); return 0; } }
    else slack_t = 0;
    if (reeling && tension < lim) dist -= (18 + reel * 6) * (bursting ? 4 : 10) / 10;
    else if (bursting) dist += pow / 4;
    dist = iclamp(dist, 0, dist0 + 100);
    stam = imax(0, stam - (reeling ? 10 : 4));
    /* where the fish is: out along the line, rising as it comes in */
    int fx = DOCK_END + 10 + (SCR_W - 30 - DOCK_END) * imin(dist, 1000) / 1000 + (bursting ? burst_dir * 8 : 0);
    int fy = WATER_TOP + 12 + (fish_y0 - WATER_TOP - 12) * imin(dist, dist0) / imax(1, dist0);
    f->x16 = (int16_t)(f->x16 + (fx * 16 - f->x16) / 3);
    f->y16 = (int16_t)(fy * 16);
    f->dir = (int8_t)(bursting ? burst_dir : -1);
    if (bursting && chance(40)) splash(fx, WATER_TOP, 1);
    if (dist <= 0) return 1;
    return 0;
}

static void land_fish(void)
{
    const species_t *p = &SP[hook_sp];
    int first = !(caught & (1 << hook_sp)), record = hook_cm > best_cm[hook_sp];
    static const int RM[] = {0, 1, 2, 4, 9};
    int value = p->value * (100 + hook_frac / 10) / 100 * RM[p->rarity];
    if (p->flags & SF_JUNK) value = 1;
    coins += value;
    caught |= 1 << hook_sp;
    catches++;
    if (catch_n[hook_sp] < 255) catch_n[hook_sp]++;
    if (record) best_cm[hook_sp] = (uint16_t)hook_cm;
    save_best(hook_sp);
    save_progress();
    splash(DOCK_END + 10, WATER_TOP, 10);
    fish[engaged].alive = 0;
    fish[engaged].respawn = (uint8_t)(30 + rn(40));
    engaged = -1;
    st = ST_IDLE;
    /* the card */
    g_flush_input();
    for (int t = 0;; t++) {
        hint_override = "A: continue";
        draw_world();
        hint_override = 0;
        int w = 250, h = 170, x = (SCR_W - w) / 2, y = 34;
        spr_panel(x, y, w, h, 0);
        g_text_large_c(SCR_W / 2, y + 8, p->flags & SF_JUNK ? "...JUNK" : first ? "NEW SPECIES!" : "CAUGHT!",
                       first ? SPRT.yellow : SPRT.cyan);
        display_rounded_rect_fill(SCR_W / 2 - 34, y + 30, 68, 56, 6, SPRT.barbg);
        spr_blit_scaled(SCR_W / 2 - 24, y + 34 - ((t >> 2) & 1), fish_r[hook_sp], 16, 16, 3, scratch);
        g_text_large_c(SCR_W / 2, y + 92, p->name, SPRT.text);
        char b[40];
        b[0] = 0; g_cat_int(b, hook_cm); g_cat(b, " cm   "); g_cat(b, RARITY[p->rarity]);
        g_text_c(SCR_W / 2, y + 114, b, SPRT.dim);
        for (int s = 0; s < p->rarity; s++) blit_icon(SCR_W / 2 - p->rarity * 5 + s * 10, y + 126, IC_STAR);
        b[0] = 0; g_cat(b, "+"); g_cat_int(b, value); g_cat(b, " coins");
        blit_icon(SCR_W / 2 - g_tw(b) / 2 - 12, y + 140, IC_COIN);
        g_text_c(SCR_W / 2, y + 139, b, SPRT.gold);
        if (record && !first && (t & 2)) g_text_c(SCR_W / 2, y + 152, "NEW RECORD!", SPRT.yellow);
        display_flush();
        tick_fx();
        uint32_t k = g_sleep_or_key(120);
        if (t > 2 && (k == K_A || k == K_B)) break;
    }
    char mb2[44]; mb2[0] = 0; g_cat(mb2, p->name); g_cat(mb2, ", "); g_cat_int(mb2, hook_cm); g_cat(mb2, " cm!");
    msg(mb2, SPRT.green);
    g_flush_input();
}

/* ── Session ─────────────────────────────────────────────────────────── */
static void frame(void)
{
    minutes_now = clock_minutes();
    phase_now = phase_of(minutes_now);
    draw_world();
    display_flush();
    tick_fx();
}

static int session_pause(void)
{
    static const char *const L[] = {"Keep fishing", "Main menu"};
    int sel = 0;
    g_flush_input();
    for (;;) {
        draw_world();
        int w = 180, h = 84, x = (SCR_W - w) / 2, y = (SCR_H - h) / 2;
        spr_panel(x, y, w, h, "PAUSED");
        for (int i = 0; i < 2; i++) {
            int iy = y + 34 + i * 20;
            if (i == sel) spr_sel_row(x + 8, iy - 2, w - 16, 18);
            g_text_c(x + w / 2, iy + 2, L[i], i == sel ? SPRT.seltext : SPRT.text);
        }
        display_flush();
        uint32_t k = g_wait_key();
        if (k == K_UP || k == K_DOWN) sel ^= 1;
        else if (k == K_B || k == K_HOME) return 0;
        else if (k == K_A) return sel;
    }
}

static void fish_session(void)
{
    st = ST_IDLE; engaged = -1; dip = 0; twitch = 0;
    bclear(parts, sizeof(parts));
    minutes_now = clock_minutes();
    phase_now = phase_of(minutes_now);
    reset_fish();
    msgb[0] = 0;
    {
        char b[44]; b[0] = 0; g_cat(b, LOC[loc].name); g_cat(b, " - "); g_cat(b, PHASE_NAME[phase_now]);
        msg(b, SPRT.accent);
    }
    g_flush_input();
    for (;;) {
        uint32_t k = g_key();
        if (k == K_HOME) {
            if (st == ST_FIGHT) end_fight_lost("You cut the line.");
            if (session_pause()) return;
            g_flush_input();
            k = 0;
        }
        switch (st) {
        case ST_IDLE:
            if (k == K_A || (k == 0 && a_held() && anim_tick > 2)) { st = ST_CHARGE; power = 0; pdir = 1; }
            break;
        case ST_CHARGE:
            power += pdir * 9;
            if (power >= 100) { power = 100; pdir = -1; }
            if (power <= 0) { power = 0; pdir = 1; }
            if (!a_held()) {
                st = ST_CAST; cast_t = 0;
                cast_x = CAST_MIN + (SCR_W - 16 - CAST_MIN) * power / 100;
            }
            break;
        case ST_CAST:
            if (++cast_t >= 5) {
                st = ST_WAIT;
                lx16 = cast_x * 16; ly16 = (WATER_TOP + 6) * 16;
                lty = imin(WATER_TOP + 44, max_lure_y());
                splash(cast_x, WATER_TOP, 6);
                tilt_calibrate();
            }
            break;
        case ST_WAIT: {
            int bite = engaged >= 0 && fish[engaged].state == FS_BITE;
            if (k == K_A) {
                if (bite) { start_fight(); break; }
                if (engaged >= 0) {
                    fish[engaged].state = FS_FLEE; engaged = -1; dip = 0;
                    msg("Too soon! It spooked.", SPRT.orange);
                } else msg("Wait for the bobber to dive.", SPRT.dim);
            } else if (k == K_UP) lty = imax(WATER_TOP + 8, lty - 8);
            else if (k == K_DOWN) {
                int m = max_lure_y();
                if (lty >= m) msg("Line too short to go deeper.", SPRT.dim);
                lty = imin(m, lty + 8);
            } else if (k == K_B) {
                lx16 -= 7 * 16; twitch = 8;
                if (engaged >= 0 && fish[engaged].state != FS_APPROACH) { fish[engaged].state = FS_FLEE; engaged = -1; dip = 0; }
            } else if (k == K_X) {
                if (engaged >= 0) { fish[engaged].state = FS_FLEE; engaged = -1; }
                st = ST_IDLE; dip = 0;
                break;
            }
            int t = (engaged < 0) ? tilt_now() : 0;
            lx16 += t * 16 * 2 / 100;
            if (ly16 / 16 < lty) ly16 += 3 * 16;
            else if (ly16 / 16 > lty) ly16 -= 3 * 16;
            if (lx16 / 16 > SCR_W - 10) lx16 = (SCR_W - 10) * 16;
            if (lx16 / 16 < DOCK_END + 12) { st = ST_IDLE; dip = 0; if (engaged >= 0) { fish[engaged].state = FS_FLEE; engaged = -1; } }
            break;
        }
        case ST_FIGHT:
            if (fight_tick()) { land_fish(); continue; }
            break;
        }
        fish_tick();
        frame();
        delay(4000);
    }
}

/* ── Menus ───────────────────────────────────────────────────────────── */
static void scene_backdrop(void)
{
    minutes_now = clock_minutes();
    phase_now = phase_of(minutes_now);
    st = ST_IDLE;
    draw_world();
}

static void travel_menu(void)
{
    int sel = loc;
    const char *note = "";
    g_flush_input();
    hint_override = "A travel/buy   B back";
    for (;;) {
        scene_backdrop();
        int w = 260, h = 40 + N_LOC * 30 + 24, x = (SCR_W - w) / 2, y = 30;
        spr_panel(x, y, w, h, "TRAVEL");
        for (int i = 0; i < N_LOC; i++) {
            int iy = y + 34 + i * 30, have = (unlocked >> i) & 1;
            if (i == sel) spr_sel_row(x + 6, iy - 2, w - 12, 28);
            uint16_t wc = spr_col(LOC[i].wtop[0], LOC[i].wtop[1], LOC[i].wtop[2]);
            display_rounded_rect_fill(x + 12, iy + 2, 20, 20, 4, wc);
            display_rect(x + 12, iy + 2, 20, 6, spr_col(LOC[i].shore[0], LOC[i].shore[1], LOC[i].shore[2]));
            display_text(x + 40, iy + 2, LOC[i].name, i == sel ? SPRT.seltext : SPRT.text);
            char b[24]; b[0] = 0;
            int n = 0, tot = 0;
            for (int s = 0; s < N_SP; s++) if (SP[s].loc == i) { tot++; n += (caught >> s) & 1; }
            g_cat_int(b, n); g_cat(b, "/"); g_cat_int(b, tot); g_cat(b, " species");
            display_text(x + 40, iy + 13, b, SPRT.dim);
            if (i == loc) g_text_r(x + w - 12, iy + 7, "here", SPRT.green);
            else if (!have) {
                blit_icon(x + w - 70, iy + 8, IC_LOCK);
                b[0] = 0; g_cat_int(b, LOC[i].price);
                display_text(x + w - 58, iy + 7, b, coins >= LOC[i].price ? SPRT.gold : SPRT.red);
            }
        }
        g_text_c(SCR_W / 2, y + h - 16, note, SPRT.cyan);
        display_flush();
        uint32_t k = g_wait_key();
        if (k == K_UP) { sel = (sel + N_LOC - 1) % N_LOC; note = ""; }
        else if (k == K_DOWN) { sel = (sel + 1) % N_LOC; note = ""; }
        else if (k == K_B || k == K_HOME) break;
        else if (k == K_A) {
            if (!((unlocked >> sel) & 1)) {
                if (coins < LOC[sel].price) { note = "Not enough coins."; continue; }
                coins -= LOC[sel].price;
                unlocked |= 1 << sel;
            }
            loc = sel;
            save_progress();
            break;
        }
    }
    hint_override = 0;
}

static void shop_menu(void)
{
    static const char *const N[] = {"Rod", "Reel", "Line", "Spinner Lure", "Glow Lure", "Golden Lure"};
    static const char *const D[] = {
        "Takes more tension.", "Reels in faster.", "Reach deeper water.",
        "Predators love it.", "Night & abyss fish bite.", "Rarer, bigger fish.",
    };
    static const int RODP[] = {80, 200, 450}, REELP[] = {70, 180, 400}, LINEP[] = {100, 250}, LUREP[] = {0, 60, 150, 400};
    static const uint8_t IC[] = {IC_ROD, IC_REEL, IC_LINE, IC_HOOK, IC_HOOK, IC_HOOK};
    int sel = 0;
    const char *note = "";
    g_flush_input();
    hint_override = "A buy/equip   B back";
    for (;;) {
        scene_backdrop();
        int w = 280, h = 40 + 6 * 22 + 30, x = (SCR_W - w) / 2, y = 28;
        spr_panel(x, y, w, h, "TACKLE SHOP");
        for (int i = 0; i < 6; i++) {
            int iy = y + 34 + i * 22;
            if (i == sel) spr_sel_row(x + 6, iy - 2, w - 12, 20);
            spr_blit_scaled(x + 12, iy, icon_px[IC[i]], 8, 8, 2, scratch);
            if (i >= 3) {
                static const uint8_t LC[3][3] = {{220, 220, 230}, {120, 255, 220}, {255, 210, 60}};
                display_circle_fill(x + 22, iy + 12, 3, spr_col(LC[i - 3][0], LC[i - 3][1], LC[i - 3][2]));
            }
            display_text(x + 34, iy + 4, N[i], i == sel ? SPRT.seltext : SPRT.text);
            char b[20]; b[0] = 0;
            int lvl = i == 0 ? rod : i == 1 ? reel : i == 2 ? line_lvl : -1, mx = i == 2 ? 2 : 3, price = -1;
            if (lvl >= 0) {
                for (int p = 0; p < mx; p++)
                    display_rect(x + 140 + p * 8, iy + 5, 6, 6, p < lvl ? SPRT.cyan : SPRT.barbg);
                if (lvl < mx) price = i == 0 ? RODP[lvl] : i == 1 ? REELP[lvl] : LINEP[lvl];
                else g_cat(b, "MAX");
            } else {
                int li = i - 2;
                if (!((lures >> li) & 1)) price = LUREP[li];
                else g_cat(b, lure == li ? "equipped" : "owned");
            }
            if (price >= 0) {
                g_cat_int(b, price);
                blit_icon(x + w - 12 - g_tw(b) - 11, iy + 5, IC_COIN);
            }
            g_text_r(x + w - 12, iy + 4, b, price >= 0 ? (coins >= price ? SPRT.gold : SPRT.red) : SPRT.green);
        }
        display_hline(x + 8, y + h - 30, w - 16, SPRT.edge);
        g_text_c(SCR_W / 2, y + h - 25, D[sel], SPRT.dim);
        g_text_c(SCR_W / 2, y + h - 13, note, SPRT.cyan);
        display_flush();
        uint32_t k = g_wait_key();
        if (k == K_UP) { sel = (sel + 5) % 6; note = ""; }
        else if (k == K_DOWN) { sel = (sel + 1) % 6; note = ""; }
        else if (k == K_B || k == K_HOME) break;
        else if (k == K_A) {
            int *lv = sel == 0 ? &rod : sel == 1 ? &reel : sel == 2 ? &line_lvl : 0;
            if (lv) {
                int mx = sel == 2 ? 2 : 3;
                if (*lv >= mx) { note = "Already the best."; continue; }
                int pr = sel == 0 ? RODP[*lv] : sel == 1 ? REELP[*lv] : LINEP[*lv];
                if (coins < pr) { note = "Not enough coins."; continue; }
                coins -= pr; (*lv)++; note = "Upgraded!";
            } else {
                int li = sel - 2;
                if ((lures >> li) & 1) { lure = (lure == li) ? 0 : li; note = lure ? "Lure equipped." : "Back to the worm."; }
                else if (coins < LUREP[li]) { note = "Not enough coins."; continue; }
                else { coins -= LUREP[li]; lures |= 1 << li; lure = li; note = "Bought and equipped!"; }
            }
            save_progress();
        }
    }
    hint_override = 0;
}

static void fishdex(void)
{
    int sel = 0;
    g_flush_input();
    hint_override = "D-pad browse   B back";
    for (;;) {
        display_clear(0x0000);
        draw_hud();
        char b[40];
        b[0] = 0; g_cat(b, "FISHDEX  "); g_cat_int(b, dex_count()); g_cat(b, "/"); g_cat_int(b, N_SP);
        g_text_large_c(SCR_W / 2, HUD_H + 4, b, SPRT.yellow);
        int cw = 46, ch = 28, gx = (SCR_W - 6 * cw) / 2, gy = HUD_H + 26;
        for (int l = 0; l < N_LOC; l++)
            for (int c = 0; c < 6; c++) {
                int s = l * 6 + c, x = gx + c * cw, y = gy + l * ch;
                int have = (caught >> s) & 1;
                display_rounded_rect_fill(x + 2, y + 1, cw - 4, ch - 2, 4, s == sel ? SPRT.sel : SPRT.panel);
                if (s == sel) display_rounded_rect(x + 2, y + 1, cw - 4, ch - 2, 4, SPRT.yellow);
                if (have) spr_blit16(x + cw / 2 - 8, y + 6, fish_r[s]);
                else {
                    spr_blit_tint(x + cw / 2 - 8, y + 6, fish_r[s], 16, 16, spr_mono ? 0xFFFF : SPR_RGB(60, 60, 80), scratch);
                    if (spr_mono) spr_blit16(x + cw / 2 - 8, y + 6, veil50);
                }
            }
        const species_t *p = &SP[sel];
        int have = (caught >> sel) & 1, py = gy + 4 * ch + 4;
        spr_panel(8, py, SCR_W - 16, SCR_H - BAR_H - py - 4, 0);
        display_text(18, py + 6, have ? p->name : "???", have ? SPRT.text : SPRT.dim);
        for (int s = 0; s < p->rarity; s++) blit_icon(SCR_W - 22 - s * 10, py + 7, IC_STAR);
        b[0] = 0; g_cat(b, LOC[p->loc].name); g_cat(b, ", ");
        g_cat(b, p->time == TM_ANY ? "any time" : p->time == TM_NIGHT ? "night" : p->time == TM_DAY ? "day" : "dawn/dusk");
        display_text(18, py + 18, b, SPRT.cyan);
        b[0] = 0; g_cat(b, p->depth == 0 ? "Shallow" : p->depth == 1 ? "Mid water" : "Deep");
        if (have) { g_cat(b, "  best "); g_cat_int(b, best_cm[sel]); g_cat(b, "cm  x"); g_cat_int(b, catch_n[sel]); }
        display_text(18, py + 30, b, SPRT.dim);
        draw_bar();
        display_flush();
        uint32_t k = g_wait_key();
        if (k == K_LEFT) sel = (sel + N_SP - 1) % N_SP;
        else if (k == K_RIGHT) sel = (sel + 1) % N_SP;
        else if (k == K_UP) sel = (sel + N_SP - 6) % N_SP;
        else if (k == K_DOWN) sel = (sel + 6) % N_SP;
        else if (k == K_B || k == K_HOME || k == K_A) break;
    }
    hint_override = 0;
}

static void options_menu(void)
{
    int sel = 0, n = 5;
    g_flush_input();
    hint_override = "LEFT/RIGHT change  A calibrate";
    tilt_calibrate();
    for (;;) {
        scene_backdrop();
        int w = 270, h = 40 + n * 22 + 34, x = (SCR_W - w) / 2, y = 30;
        spr_panel(x, y, w, h, "OPTIONS");
        static const char *const L[] = {"Controls", "Tilt axis", "Invert tilt", "Time zone", "Back"};
        for (int i = 0; i < n; i++) {
            int iy = y + 34 + i * 22;
            if (i == sel) spr_sel_row(x + 6, iy - 2, w - 12, 20);
            display_text(x + 14, iy + 3, L[i], i == sel ? SPRT.seltext : SPRT.text);
            char b[20]; b[0] = 0;
            switch (i) {
            case 0: g_cat(b, ctl_buttons ? "< D-pad >" : "< Tilt >"); break;
            case 1: g_cat(b, ctl_axis ? "< Y >" : "< X >"); break;
            case 2: g_cat(b, ctl_invert ? "< On >" : "< Off >"); break;
            case 3: g_cat(b, "< UTC"); g_cat(b, tz >= 0 ? "+" : ""); g_cat_int(b, tz); g_cat(b, " >"); break;
            default: break;
            }
            g_text_r(x + w - 14, iy + 3, b, SPRT.cyan);
        }
        int gy = y + 34 + n * 22 + 4, gw = 140, gx = x + w / 2 - gw / 2, t = tilt_now();
        display_text(x + 14, gy, imu_ok == 0 && !ctl_buttons ? "No IMU - use D-pad" : "Tilt test:", SPRT.dim);
        display_rect(gx + 30, gy + 3, gw - 30, 3, SPRT.barbg);
        display_vline(gx + 30 + (gw - 30) / 2, gy, 9, SPRT.dim);
        display_rect(gx + 30 + (gw - 30) / 2 + t * (gw - 30) / 200 - 2, gy, 5, 9, SPRT.yellow);
        display_text(gx + gw + 4, gy, t < 0 ? "L" : t > 0 ? "R" : "-", SPRT.text);
        display_flush();
        uint32_t k = g_sleep_or_key(80);
        int d = k == K_LEFT ? -1 : k == K_RIGHT ? 1 : 0;
        if (k == K_UP) sel = (sel + n - 1) % n;
        else if (k == K_DOWN) sel = (sel + 1) % n;
        else if (d) {
            if (sel == 0) ctl_buttons ^= 1;
            else if (sel == 1) { ctl_axis ^= 1; tilt_calibrate(); }
            else if (sel == 2) { ctl_invert ^= 1; tilt_calibrate(); }
            else if (sel == 3) tz = iclamp(tz + d, -12, 14);
        } else if (k == K_A) {
            if (sel == 4) break;
            tilt_calibrate();
        } else if (k == K_B || k == K_HOME) break;
    }
    save_options();
    hint_override = 0;
}

/* ── Title & main menu ───────────────────────────────────────────────── */
static const char *glyph(char c)
{
    switch (c) {
    case 'P': return "11110100011000111110100001000010000";
    case 'I': return "11111001000010000100001000010011111";
    case 'X': return "10001100010101000100010101000110001";
    case 'E': return "11111100001000011110100001000011111";
    case 'L': return "10000100001000010000100001000011111";
    case 'A': return "01110100011000111111100011000110001";
    case 'N': return "10001110011010110011100011000110001";
    case 'G': return "01111100001000010011100011000101111";
    case 'R': return "11110100011000111110101001001010001";
    default:  return "00000000000000000000000000000000000";
    }
}
static void logo_word(const char *w, int y, int s, uint16_t top, uint16_t bot)
{
    int n = strlen(w), x0 = SCR_W / 2 - (n * 6 * s - s) / 2;
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < n; i++) {
            const char *g = glyph(w[i]);
            for (int r = 0; r < 7; r++)
                for (int c = 0; c < 5; c++) {
                    if (g[r * 5 + c] != '1') continue;
                    int x = x0 + i * 6 * s + c * s, yy = y + r * s;
                    if (!pass) { if (!spr_mono) display_rect(x + 2, yy + 3, s, s, SPR_RGB(10, 30, 50)); }
                    else {
                        display_rect(x, yy, s, s, r < 3 ? top : bot);
                        if (!spr_mono) display_hline(x, yy, s, r < 3 ? 0xFFFF : top);
                    }
                }
        }
}

static int title_screen(void)
{
    g_flush_input();
    int jump = 0, jx = 200;
    for (;;) {
        scene_backdrop();
        logo_word("PIXEL", HUD_H + 6, 4, spr_col(255, 240, 160), spr_col(255, 170, 60));
        logo_word("ANGLER", HUD_H + 40, 4, spr_col(170, 240, 255), spr_col(60, 160, 230));
        if (!jump && chance(4)) { jump = 1; jx = DOCK_END + 40 + rn(SCR_W - DOCK_END - 80); }
        if (jump) {
            int t = jump, y = WATER_TOP - (12 * t - t * t);
            spr_blit16(jx + t * 3, y, fish_r[(anim_tick / 64) % 6]);
            if (++jump > 12) { jump = 0; splash(jx + 36, WATER_TOP, 6); }
        }
        char b[40];
        b[0] = 0; g_cat(b, "Fishdex "); g_cat_int(b, dex_count()); g_cat(b, "/"); g_cat_int(b, N_SP);
        g_cat(b, "   Coins "); g_cat_int(b, coins);
        spr_text_out(SCR_W / 2 - g_tw(b) / 2, WATER_TOP + 30, b, SPRT.gold);
        if ((anim_tick >> 2) & 1) spr_text_out(SCR_W / 2 - g_tw("Press A") / 2, WATER_TOP + 50, "Press A", SPRT.text);
        spr_text_out(SCR_W / 2 - g_tw("HOME: exit") / 2, WATER_TOP + 70, "HOME: exit", SPRT.dim);
        display_flush();
        tick_fx();
        uint32_t k = g_sleep_or_key(90);
        if (k == K_A) return 0;
        if (k == K_HOME || k == K_B) return -1;
    }
}

static int main_menu(void)
{
    static int sel;
    g_flush_input();
    msg_t = 0; msgb[0] = 0;
    hint_override = "A select   HOME exit";
    for (;;) {
        scene_backdrop();
        int n = 6, w = 230, h = 36 + n * 22 + 8, x = (SCR_W - w) / 2, y = 34;
        spr_panel(x, y, w, h, "PIXEL ANGLER");
        static const uint8_t IC[] = {IC_HOOK, IC_MAP, IC_ROD, IC_BOOK, IC_GEAR, IC_EXIT};
        for (int i = 0; i < n; i++) {
            int iy = y + 34 + i * 22;
            if (i == sel) spr_sel_row(x + 6, iy - 2, w - 12, 20);
            blit_icon(x + 14, iy + 4, IC[i]);
            char b[40]; b[0] = 0;
            switch (i) {
            case 0: g_cat(b, "Fish: "); g_cat(b, LOC[loc].name); break;
            case 1: g_cat(b, "Travel"); break;
            case 2: g_cat(b, "Tackle shop"); break;
            case 3: g_cat(b, "Fishdex "); g_cat_int(b, dex_count()); g_cat(b, "/"); g_cat_int(b, N_SP); break;
            case 4: g_cat(b, "Options"); break;
            default: g_cat(b, "Quit"); break;
            }
            display_text(x + 30, iy + 3, b, i == sel ? SPRT.seltext : SPRT.text);
        }
        display_flush();
        uint32_t k = g_wait_key();
        if (k == K_UP) sel = (sel + n - 1) % n;
        else if (k == K_DOWN) sel = (sel + 1) % n;
        else if (k == K_HOME || k == K_B) { hint_override = 0; return -1; }
        else if (k == K_A) {
            hint_override = 0;
            switch (sel) {
            case 0: fish_session(); msg_t = 0; msgb[0] = 0; break;
            case 1: travel_menu(); break;
            case 2: shop_menu(); break;
            case 3: fishdex(); break;
            case 4: options_menu(); break;
            default: return -1;
            }
            g_flush_input();
            hint_override = "A select   HOME exit";
        }
    }
}

int main(void)
{
    printf("AkiraOS Pixel Angler");
    g_init();
    SCR_W = G_W; SCR_H = G_H;
    WATER_BOT = SCR_H - BAR_H;
    build_sprites();
    load_progress();
    g_seed_random();
    for (;;) {
        if (title_screen() < 0) break;
        if (main_menu() < 0) break;
    }
    save_progress();
    app_switch("supervisor");
    return 0;
}
