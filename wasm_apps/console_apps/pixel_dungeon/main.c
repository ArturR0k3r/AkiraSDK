/**
 * @file main.c
 * @brief Pixel Dungeon — a pixel-art roguelike for AkiraOS
 *
 * Twenty floors in four regions (Cellars, Fungal Caverns, Frozen Crypt,
 * Molten Depths), a boss guarding the end of each, and the Akira Crystal at
 * the bottom.  Pick a hero, survive, choose a perk every level.
 *
 * Controls:
 *   D-pad   move / bump to attack (hold to keep walking)
 *   A       act on the tile: stairs, chest, shrine, altar; take a wall
 *           torch next to you; otherwise wait a turn and search for traps
 *   B       drink a health potion
 *   X       bag: potions, flasks, scrolls, bombs, oil, your skill
 *   Y       hero skill (Cleave / Vanish / Arcane Bolt)
 *   HOME    pause: map, hero sheet, guide, restart, exit
 *
 * Rendering: every graphic is 16x16 indexed pixel art expanded at start-up
 * into RGB565 sprite caches and drawn with display_bitmap_transparent(), one
 * native call per tile.  Light falls off in ordered-dither steps; explored
 * but unlit tiles use a dark "memory" copy of each tile.  The 400x240 Sharp
 * Memory LCD is 1-bit (any non-zero pixel is white), so every palette entry
 * also carries a black/white value and the same art works on both panels.
 * display_flush() pushes the whole frame (~8 fps), so the game is turn-based
 * and all game logic runs before a frame starts drawing.
 *
 * @copyright Copyright (c) 2026 PenEngineering S.R.L
 * @license Apache-2.0
 */

#include "akira_api.h"
#include "../../common/akira_game.h"

/* ── Screen ──────────────────────────────────────────────────────────── */
static int32_t SCR_W = 320, SCR_H = 240;
static int g_mono;                 /* 1 = 1-bit Sharp panel */

#define TS        16               /* tile size in pixels */
#define HUD_H     24               /* top bar */
#define BAR_H     24               /* bottom message bar */
#define VIEW_ROWS 12
static int VIEW_COLS = 20;         /* SCR_W / TS */
static int VIEW_Y0 = HUD_H;

#define TKEY 0x0821u               /* transparent colour key for sprites */
#define RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

/* ── Palette ─────────────────────────────────────────────────────────────
 * Sprite art uses one character per pixel.  Letters are fixed colours;
 * '0'-'9', 'x', 'X' are biome slots re-coloured per region.  '.' is clear. */
typedef struct { char ch; uint8_t r, g, b; } pal_t;

static const pal_t PAL[] = {
    {'k', 0, 0, 0},       {'K', 40, 36, 48},    {'d', 84, 80, 96},
    {'g', 140, 136, 150}, {'G', 196, 192, 204}, {'w', 255, 255, 255},
    {'r', 222, 48, 48},   {'R', 128, 22, 34},   {'p', 255, 140, 160},
    {'o', 250, 146, 40},  {'O', 160, 82, 30},   {'y', 255, 222, 70},
    {'Y', 186, 138, 24},  {'n', 146, 94, 52},   {'N', 82, 50, 30},
    {'s', 250, 204, 164}, {'S', 200, 142, 110}, {'l', 160, 234, 90},
    {'e', 62, 170, 72},   {'E', 30, 92, 52},    {'c', 130, 232, 255},
    {'t', 40, 150, 172},  {'b', 74, 116, 232},  {'B', 40, 52, 132},
    {'u', 176, 96, 232},  {'U', 92, 42, 132},   {'m', 240, 86, 200},
    {'v', 240, 232, 210}, {'V', 176, 166, 138}, {'z', 176, 186, 206},
    {'Z', 100, 110, 132}, {'i', 206, 238, 255}, {'P', 255, 0, 255},
    {'Q', 255, 255, 255},
};
#define N_PAL ((int)(sizeof(PAL) / sizeof(PAL[0])))

/* Biome palettes: slots 0-3 wall (dark..light), 4-6 floor (grout..light),
 * 7-8 accent (moss, fungus, frost, embers), 9 glow, x/X flame outer/mid.
 * mono[]: black/white value of each slot on the Sharp panel. */
#define N_BIOME_SLOTS 12
typedef struct {
    const char *name;
    uint8_t rgb[N_BIOME_SLOTS][3];
    uint16_t ui;                   /* HUD accent colour */
} biome_t;

static const biome_t BIOMES[4] = {
    {"The Cellars",
     {{22, 16, 14}, {70, 50, 42}, {112, 80, 62}, {156, 118, 90},
      {36, 32, 32}, {68, 60, 57}, {94, 84, 77},
      {50, 86, 40}, {98, 140, 62}, {255, 214, 120},
      {220, 70, 20}, {255, 160, 40}},
     RGB(214, 150, 80)},
    {"Fungal Caverns",
     {{14, 10, 24}, {50, 38, 72}, {82, 64, 110}, {122, 98, 150},
      {24, 30, 38}, {44, 55, 66}, {64, 78, 88},
      {28, 112, 112}, {96, 226, 200}, {190, 255, 230},
      {20, 140, 130}, {90, 240, 210}},
     RGB(110, 220, 200)},
    {"Frozen Crypt",
     {{10, 16, 28}, {50, 68, 98}, {88, 114, 148}, {156, 186, 218},
      {34, 40, 54}, {58, 66, 86}, {84, 94, 116},
      {118, 148, 188}, {222, 240, 255}, {200, 240, 255},
      {40, 90, 220}, {120, 200, 255}},
     RGB(150, 200, 255)},
    {"Molten Depths",
     {{12, 6, 6}, {48, 22, 22}, {86, 38, 32}, {132, 62, 46},
      {34, 24, 24}, {60, 41, 38}, {84, 57, 51},
      {196, 58, 18}, {255, 150, 40}, {255, 236, 140},
      {210, 40, 10}, {255, 170, 30}},
     RGB(255, 120, 60)},
};
static const uint8_t BIOME_MONO[N_BIOME_SLOTS] = {0, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 1};
static const char BIOME_CH[N_BIOME_SLOTS] = {'0','1','2','3','4','5','6','7','8','9','x','X'};

/* ── Tile art (biome palette) ─────────────────────────────────────────── */
static const char A_FLOOR_A[] =
    "5555555455555554" "5655555456555554" "5555555455555554" "5555555455555554"
    "5555565455555654" "5555555455555554" "4444444444444444" "5554555555545555"
    "5654565555545655" "5554555555545555" "5554555555545555" "5554555556545555"
    "5554555555545555" "4444444444444444" "5555555455555554" "5565555455555554";
static const char A_FLOOR_B[] =
    "5555555455555554" "5555555455565554" "5555555455555554" "5545555455555554"
    "5554555455555554" "5555455455555554" "4444444444444444" "5554555555545555"
    "5554556555545555" "5554555455545555" "5554555545545555" "5654555554545555"
    "5554555555545555" "4444444444444444" "5555555455555554" "5565555455555554";
static const char A_FLOOR_C[] =
    "5555555455555554" "5555555455555554" "5555555455577554" "5555555455788754"
    "5555555455577554" "5555555455555554" "4444444444444444" "5554555555545555"
    "5774555555545555" "7887555555545555" "5774555555545565" "5554555555545555"
    "5554555555545555" "4444444444444444" "5555555455555554" "5555555455555554";
static const char A_WALL_FACE[] =
    "3333333333333333" "3222222132222221" "2222222122222221" "2222222122222221"
    "1111111111111111" "2221322222213222" "2221222222212222" "2221222222212222"
    "1111111111111111" "3222222132222221" "2222222122222221" "2222222122222221"
    "1111111111111111" "2221322222213222" "2221222222212222" "0000000000000000";
static const char A_WALL_FACE_B[] =
    "3333333333333333" "3222222132222221" "2228222122222221" "2287822122222221"
    "1178711111111111" "2227322222213222" "2221222222212222" "2221222212212222"
    "1111111121111111" "3222222132122221" "2222222122212221" "2222222122222221"
    "1111111111111111" "2221322222213222" "2221222222212222" "0000000000000000";
static const char A_WALL_TOP[] =
    "1111111111111111" "1111111111110111" "1101111111111111" "1111111111111111"
    "1111111011111111" "1111111111111111" "1111111111111101" "1011111111111111"
    "1111111111111111" "1111110111111111" "1111111111111111" "1111111111101111"
    "1101111111111111" "1111111111111111" "1111111101111111" "1111111111111111";
static const char A_DOOR[] =
    "3333333333333333" "2222111111112222" "2221nnNnnNnn1222" "221nnnNnnNnnn122"
    "221nnnNnnNnnn122" "221ZZZZZZZZZZ122" "221nnnNnnNnnn122" "221nnnNnnNnyn122"
    "221nnnNnnNnyn122" "221nnnNnnNnnn122" "221ZZZZZZZZZZ122" "221nnnNnnNnnn122"
    "221nnnNnnNnnn122" "221nnnNnnNnnn122" "221NNNNNNNNNN122" "0000000000000000";
static const char A_DOOR_OPEN[] =
    "3333333333333333" "2222111111112222" "2221K000000K1222" "221nK0000000K122"
    "221nK00000000122" "221n000000000122" "221n000000000122" "221n000000000122"
    "221n000000000122" "221n000000000122" "221n000000000122" "221n000000000122"
    "221n400000000122" "221n444444444122" "221N455555554122" "0000000000000000";
static const char A_LOCKED[] =
    "3333333333333333" "2222111111112222" "2221ZZZZZZZZ1222" "221ZzzzzzzzzZ122"
    "221ZzZZZZZZzZ122" "221ZzZzzzzZzZ122" "221ZzZzyyzZzZ122" "221ZzZzykyZzZ122"
    "221ZzZzykyZzZ122" "221ZzZzzkzZzZ122" "221ZzZzzzzZzZ122" "221ZzZZZZZZzZ122"
    "221ZzzzzzzzzZ122" "221ZZZZZZZZZZ122" "221ZZZZZZZZZZ122" "0000000000000000";
/* Doors in a north-south wall seen edge-on (overlay on the floor). */
static const char A_DOOR_SIDE[] =
    "......k33k......" "......kNnk......" "......kNnk......" "......kZZk......"
    "......kNnk......" "......kNnk......" "......kNnk......" "......kNyk......"
    "......kNyk......" "......kNnk......" "......kNnk......" "......kZZk......"
    "......kNnk......" "......kNnk......" "......kNnk......" "......k44k......";
static const char A_DOOR_SIDE_OPEN[] =
    "......k33k......" "......k22k......" "......kkkk......" "................"
    "................" "................" "................" "................"
    "................" "................" "................" "................"
    "................" "......kkkk......" "......k22k......" "......k44k......";
static const char A_LOCKED_SIDE[] =
    "......k33k......" "......kzZk......" "......kzZk......" "......kzZk......"
    "......kzZk......" "......kzZk......" "......kyyk......" "......kkyk......"
    "......kyyk......" "......kzZk......" "......kzZk......" "......kzZk......"
    "......kzZk......" "......kzZk......" "......kzZk......" "......k44k......";
static const char A_STAIRS[] =
    "4444444444444444" "4111111111111114" "4166666666666614" "4155555555555514"
    "4100000000000014" "4106666666666014" "4105555555555014" "4100000000000014"
    "4100666666660014" "4100555555550014" "4100000000000014" "4100066666600014"
    "4100055555500014" "4100000000000014" "4100000000000014" "4444444444444444";
static const char A_CHEST[] =
    "................" "................" "................" "...kkkkkkkkkk..."
    "..knnnnnnnnnnnk." "..knNNNNNNNNNnk." "..kYYYYYYYYYYYk." "..knnnnnyynnnnk."
    "..kNnnnnkynnnNk." "..kNnnnnnnnnnNk." "..kYYYYYYYYYYYk." "..kNNNNNNNNNNNk."
    "...kkkkkkkkkkk.." "...44444444444.." "................" "................";
static const char A_CHEST_OPEN[] =
    "................" "...kkkkkkkkkk..." "..knnnnnnnnnnnk." "..knNNNNNNNNNnk."
    "..kYYYYYYYYYYYk." "..kkkkkkkkkkkkk." "..kKKKKKKKKKKKk." "..kKKKKKKKKKKKk."
    "..kNnnnnkynnnNk." "..kNnnnnnnnnnNk." "..kYYYYYYYYYYYk." "..kNNNNNNNNNNNk."
    "...kkkkkkkkkkk.." "...44444444444.." "................" "................";
static const char A_SHRINE[] =
    "................" ".......kk......." "......kcck......" ".....kcwcck....."
    ".....kcccbk....." "......kcbk......" ".......kk......." "....kkkkkkkk...."
    "....kGGGGGGk...." ".....kggggk....." ".....kgGggk....." ".....kggggk....."
    "....kGGGGGGk...." "...kggggggggk..." "...kkkkkkkkkk..." "....44444444....";
static const char A_SHRINE_USED[] =
    "................" "................" "................" "................"
    "......kddk......" ".....kdKgdk....." "......kddk......" "....kkkkkkkk...."
    "....kggggggk...." ".....kddddk....." ".....kdgddk....." ".....kddddk....."
    "....kggggggk...." "...kddddddddk..." "...kkkkkkkkkk..." "....44444444....";
static const char A_ALTAR[] =
    "................" "................" "......kkkk......" ".....krrrRk....."
    ".....kRrrRk....." "......kRRk......" "..kkkkkkkkkkkk.." ".kddddddddddddk."
    ".kKrKKKrKKKrKKk." ".kKKKKKKKKKKKKk." ".kKKrKKKrKKKrKk." ".kKKKKKKKKKKKKk."
    ".kddddddddddddk." "..kkkkkkkkkkkk.." "..444444444444.." "................";
static const char A_ALTAR_USED[] =
    "................" "................" "................" "................"
    "................" "................" "..kkkkkkkkkkkk.." ".kddddddddddddk."
    ".kKKKKKKKKKKKKk." ".kKKKKKKKKKKKKk." ".kKKKKKKKKKKKKk." ".kKKKKKKKKKKKKk."
    ".kddddddddddddk." "..kkkkkkkkkkkk.." "..444444444444.." "................";
static const char A_GRASS[] =
    ".7..8...7...8..." ".8..7..78..7..8." "778.87.787.87.77" "87.787.87787.87."
    "78778778787787.8" "8787878787878787" "7878787878787878" "8.7..8...7.8..7."
    ".8.78.8.787.8.87" "78787.787878787." "8787878787878787" "7878787878787878"
    ".7..8.7...8..7.." "87.787.87.787.78" "7878787878787878" "8787878787878787";
static const char A_GRASS_CUT[] =
    "................" "................" "...7.......8...." "...8..7....7...."
    "................" "........8......." ".7......7....7.." ".8...........8.."
    "................" "....7..........." "....8.....7....." "..........8....."
    "..8............." "..7......7.8...." ".........8.7...." "................";
static const char A_WATER0[] =
    "BBBBBBBBBBBBBBBB" "BBBBBBbBBBBBBBBB" "BBBBBbcbBBBBBBBB" "BBBBBBBBBBBBBBbB"
    "BBBBBBBBBBBBBbcb" "BbBBBBBBBBBBBBBB" "bcbBBBBBBBBBBBBB" "BBBBBBBBBbBBBBBB"
    "BBBBBBBBbcbBBBBB" "BBBBBBBBBBBBBBBB" "BBBbBBBBBBBBBBBB" "BBbcbBBBBBBbBBBB"
    "BBBBBBBBBBbcbBBB" "BBBBBBBBBBBBBBBB" "BBBBBBbBBBBBBBBB" "BBBBBbcbBBBBBBBB";
static const char A_WATER1[] =
    "BBBBBBBBBBBBBBBB" "BBBBBBBbBBBBBBBB" "BBBBBBbcbBBBBBBB" "BBBBBBBBBBBBBBBb"
    "BBBBBBBBBBBBBBbc" "BBbBBBBBBBBBBBBB" "BbcbBBBBBBBBBBBB" "BBBBBBBBBBbBBBBB"
    "BBBBBBBBBbcbBBBB" "BBBBBBBBBBBBBBBB" "BBBBbBBBBBBBBBBB" "BBBbcbBBBBBBBbBB"
    "BBBBBBBBBBBbcbBB" "BBBBBBBBBBBBBBBB" "BBBBBBBbBBBBBBBB" "BBBBBBbcbBBBBBBB";
static const char A_LAVA0[] =
    "RRRRRRRRRRRRRRRR" "RRRrrRRRRRRRRRRR" "RRrooorRRRRRrRRR" "RRRrorRRRRRrorRR"
    "RRRRRRRRRRRRrRRR" "RRRRRRRRrrRRRRRR" "RRRRRRRroyorRRRR" "RrRRRRRRrorRRRRR"
    "rorRRRRRRRRRRRRR" "RrRRRRRRRRRRRrrR" "RRRRRRRRRRRRroor" "RRRRRrrRRRRRRrrR"
    "RRRRroyrRRRRRRRR" "RRRRRrrRRRRRRRRR" "RRRRRRRRRRRrRRRR" "RRRRRRRRRRrorRRR";
static const char A_LAVA1[] =
    "RRRRRRRRRRRRRRRR" "RRRRrrRRRRRRRRRR" "RRRroorRRRRRRRRR" "RRRRrRRRRRRrrRRR"
    "RRRRRRRRRRRroorR" "RRRRRRRRRrRRrrRR" "RRRRRRRRroorRRRR" "RRRRRRRRRrRRRRRR"
    "RrRRRRRRRRRRRRRR" "rorRRRRRRRRRRrRR" "RrRRRRRRRRRRrorR" "RRRRRRrRRRRRRrRR"
    "RRRRRroyrRRRRRRR" "RRRRRRrrRRRRRRRR" "RRRRRRRRRRRRrRRR" "RRRRRRRRRRRrorRR";
static const char A_TRAP[] =
    "................" "................" "................" "...1111111111..."
    "...1444444441..." "...1455555541..." "...1455445541..." "...1454554541..."
    "...1454554541..." "...1455445541..." "...1455555541..." "...1444444441..."
    "...1111111111..." "................" "................" "................";
static const char A_TORCH0[] =
    "3333333333333333" "3222222132222221" "222222x122222221" "22222xXx22222221"
    "1111xX9Xx1111111" "2221xX9X22213222" "22212xXk22212222" "2221kZZZk2212222"
    "11111kZk11111111" "322222kZk2222221" "2222222k22222221" "2222222122222221"
    "1111111111111111" "2221322222213222" "2221222222212222" "0000000000000000";
static const char A_TORCH1[] =
    "3333333333333333" "3222222x32222221" "2222222x22222221" "222222xXx2222221"
    "11111xX9X1111111" "2221xX99Xx213222" "22212xXXk2212222" "2221kZZZk2212222"
    "11111kZk11111111" "322222kZk2222221" "2222222k22222221" "2222222122222221"
    "1111111111111111" "2221322222213222" "2221222222212222" "0000000000000000";
static const char A_TORCH_OUT[] =
    "3333333333333333" "3222222132222221" "2222222122222221" "2222222122222221"
    "1111111111111111" "2221322222213222" "22212K2K22212222" "2221kZZZk2212222"
    "11111kZk11111111" "322222kZk2222221" "2222222k22222221" "2222222122222221"
    "1111111111111111" "2221322222213222" "2221222222212222" "0000000000000000";

/* ── Decals and effects (fixed palette, transparent) ──────────────────── */
static const char A_BLOOD[] =
    "................" "................" "................" "........R......."
    "...R...........R" "....RR..R......." "...RrrR........." "..RrrrrR...R...."
    "...RrrrrR..RR..." "....RrR......R.." "......R.R......." "..R............."
    "................" ".........R......" "................" "................";
static const char A_BONES[] =
    "................" "................" "................" "................"
    "................" "....kk.........." "...kvvk...kk...." "...kvVvkkkvvk..."
    "....kkvvvvvVk..." "......kkkkVk...." "..kkk.....kk...." ".kvvvk.kk......."
    ".kvKvk.kvk......" ".kvvVk..k......." "..kkk..........." "................";
static const char A_SCORCH[] =
    "................" "................" "......K..K......" "....KK.KK..K...."
    "...K.KKKKK.K...." "..KKKK.KKKKK...." "...KKKKKKK.KK..." "..K.KKKKKKKK...."
    "...KKKKK.KKK.K.." "....KKKKKKK....." "...K..KKK.K....." ".....K....K....."
    "................" "................" "................" "................";
static const char A_WEB[] =
    "w......G.......w" ".G.....G.....G.." "..G....G....G..." "...GGGGwGGGG...."
    "...G..GGG..G...." "...G.G.G.G.G...." "...GG..G..GG...." "GGGwGGGwGGGwGGGG"
    "...GG..G..GG...." "...G.G.G.G.G...." "...G..GGG..G...." "...GGGGwGGGG...."
    "..G....G....G..." ".G.....G.....G.." "G......G.......G" "................";
static const char A_GOO[] =
    "................" "................" "................" "................"
    ".......e........" "....eelle....e.." "...elllle...ele." "...ellle....ee.."
    "....eee........." "..........e....." ".....e...elle..." "....ele..eee...."
    ".....e.........." "................" "................" "................";
static const char A_FIRE0[] =
    "................" "......r........." ".....ro...r....." ".....roo..ro...."
    "....rooo.roor..." "...rooyorooor..." "...royyorroyor.." "..royywyoooyyor."
    "..roywwyyoywyor." "..rowwwyyywwyor." "..royywwywwyyor." "...royyyyyyyor.."
    "....rrooooorr..." "......rrrrr....." "................" "................";
static const char A_FIRE1[] =
    "................" ".........r......" "....r....or....." "...ro...oor....."
    "...roo.rooor...." "...rooroooyor..." "..royorooyyor..." "..royyoooywyor.."
    "..roywyoyywwor.." "..rowwwyywwwyor." "..royywwywwyyor." "...royyyyyyyor.."
    "....rrooooorr..." "......rrrrr....." "................" "................";

/* ── Heroes, monsters, NPC (fixed palette) ────────────────────────────── */
static const char A_WARRIOR[] =
    "................" "......kkkk......" ".....kzGGzk....." "....kzGzzzZk...."
    "....kzzzzzZk..k." "....kkkkkkkk.kwk" "....kskssksk.kwk" "..kk.kSssssk.kwk"
    ".kbbk.kkkkk..kwk" "kbbybkzbbbzkkyyy" "kbyybkzbbbzkksk." "kbbybkbbbbbk.kk."
    ".kbbk.kNyNk....." "..kk.kBBkBBk...." ".....kZZkZZk...." ".....kkk.kkk....";
static const char A_ROGUE[] =
    "................" "......kkkk......" ".....kEeeEk....." "....kEeeeeEk...."
    "....kEKKKKEk...." "....kEKyKyEk...." "....kEKKKKEk...." "...kEEkkkkEEk..."
    "..kEeEEddEEeEk.." "..kEeEddddEEk.k." "..kEEkNyNkEEkGk." "..kEkddddkEkkGk."
    "...kkEEEEEEkksk." "....kEEkkEEk.k.." "....kNNk.kNNk..." "....kkk..kkk....";
static const char A_MAGE[] =
    "........k....kk." ".......kuk..kcck" "......kuuuk.kcwk" "......kuyuk..kk."
    ".....kuuuuuk.kNk" "....kkkkkkkkkkNk" ".....kskssk..kNk" ".....kswwsk.kskk"
    "....kuwwwwuk.kNk" "...kuuwwwwuukkNk" "...kuuuwwuuukkNk" "...kuuuuuuuuk.Nk"
    "...kUuuuuuuUk.kN" "....kUuuuuUk..kN" "....kkUUUUkk...k" ".....kkkkkk.....";
static const char A_RAT[] =
    "................" "................" "................" "................"
    "................" "................" "..........kk...." ".........kpgk..."
    "...kkkkkkkgggkk." "..kgggggggggkwk." ".kgGgggggggggkkk" "kpkgggggggggggpk"
    "k.kddgggggddkkk." "....kkdkkkdk...." ".....kk...kk...." "................";
static const char A_GOBLIN[] =
    "................" "................" "..kk........kk.." ".kelk......klek."
    ".kellkkkkkkllek." "..kellllllllek.." "...kelyleylek..." "...kellllllek..."
    "....kelwwlek...." "...kkkellekkk..." "..kNnkNNNNknNk.." "..kNkNnnnnNkNk.."
    "..klkNnnnnNklk.." "....kNNkkNNk...." "....kEEk.kEEk..." "....kkk..kkk....";
static const char A_SPIDER[] =
    "................" "................" "................" "..k..........k.."
    "...k...kk...k..." "k...k.kKKk.k...k" ".k...kKKKKk...k." "..kkkKKrKrKkkk.."
    "....kKKKKKKk...." "..kkkKKKKKKkkk.." ".k..kKKdKdKk..k." "k..k.kKKKKk.k..k"
    "...k..kKKk..k..." "..k....kk....k.." "................" "................";
static const char A_ARCHER[] =
    "................" "......kkkk..k..." ".....kOOOOk.kk.." "....kOnnnnOk.kk."
    "....kOkllkOk..kk" "....kOlylyOk..kn" "....kOllllOk..kn" "...kkOkkkkOkk.kn"
    "..kOOnnnnnnOkkyn" "..kOnnNNNNnnkyyn" "..kOnnnnnnnOk.kn" "..kkOnnnnnnOk.kn"
    "....kOOOOOOk..kk" "....kNNkkNNk.kk." "....kEEk.kEEk..." "....kkk..kkk....";
static const char A_BAT[] =
    "................" "................" "................" "................"
    "kk............kk" "kUk....kk....kUk" "kUUk..kUUk..kUUk" "kUuUk.kUUk.kUuUk"
    ".kUuUkkUUkkUuUk." ".kUuuUUrrUUuuUk." "..kUuUUUUUUuUk.." "...kkkUwwUkkk..."
    "......kUUk......" ".......kk......." "................" "................";
static const char A_SLIME[] =
    "................" "................" "................" "................"
    "................" "......kkkk......" "....kklllekk...." "...klwlllleek..."
    "..klwwllllleeek." "..kllllllllleek." ".klllkllllkleeek" ".kllllllllllleek"
    ".klllllkkllleeek" ".keeeeeeeeeeeeek" "..kkkkkkkkkkkkk." "................";
static const char A_SLIMELET[] =
    "................" "................" "................" "................"
    "................" "................" "................" "................"
    "......kkkk......" ".....klwlek....." "....klkllkek...." "....klllllek...."
    "....kleeeeek...." ".....kkkkkk....." "................" "................";
static const char A_SHAMAN[] =
    "................" "......kkkk...kk." ".....kttttk.kcck" "....kttwwttk.kck"
    "....kttttttk.kNk" "....kkllllkk.kNk" "....klylylk..kNk" "...kkllllllkkkNk"
    "..kttkllllkttkNk" "..kttttttttttkNk" "..kttwttttwttkNk" "..ktttttttttk.Nk"
    "..kttttttttttk.k" "...kkttttttkk..." "....kkkkkkkk...." "................";
static const char A_SKELETON[] =
    "................" "......kkkk......" ".....kvvvvk....." "....kvkvvkvk...."
    "....kvkvvkvk...." "....kvvvvvvk...." ".....kvkkvk....." "......kvvk......"
    "...kkkvvvvkkk..." "..kvkkvkkvkkvk.." "..kvk.kvvk.kvk.." "..kk..kvvk..kk.."
    "......kvvk......" ".....kvkkvk....." ".....kvk.kvk...." ".....kk...kk....";
static const char A_GHOST[] =
    "................" ".....kkkkkk....." "....kiiiiiik...." "...kiiiiiiiik..."
    "...kiikiiikik..." "...kiikiiikik..." "...kiiiiiiiiik.." "...kiiikkiiiik.."
    "...kiiiiiiiiik.." "...kiiiiiiiiik.." "...kciiiiiiicik." "...kciciiiciccik"
    "...kcckcickcck.." "...kk..kk..kk..." "................" "................";
static const char A_NECRO[] =
    "................" "......kkkk...kk." ".....kUUUUk.kvvk" "....kUUUUUUk.kvk"
    "....kUkkkkUk.kNk" "....kUkrkrUk.kNk" "....kUkkkkUk.kNk" "...kkUUUUUUkkkNk"
    "..kUUkkkkkkUUkNk" "..kUuUUUUUUuUkNk" "..kUuUUvvUUuUkNk" "..kUUUUvvUUUUk.N"
    "..kUUUUUUUUUUk.k" "...kkUUUUUUkk..." "....kkkkkkkk...." "................";
static const char A_IMP[] =
    "................" "...k........k..." "..kok......kok.." "..korkkkkkkrok.."
    "...krrrrrrrrk..." "...kryrrrryrk..." "...krrrrrrrrk..." "....krrwwrrk...."
    "..kk.krrrrk.kk.." ".kOk.krrrrk.kOk." ".kOOkkrrrrkkOOk." "..kOk.kRRk.kOk.."
    "....kRRkkRRk..k." "....kRk..kRk.kok" "....kk....kk..k." "................";
static const char A_GOLEM[] =
    "................" ".....kkkkkk....." "....kggggggk...." "....kgogogGk...."
    "...kkggggggkk..." "..kgdkkkkkkdgk.." ".kgggddddddgggk." ".kgGgdgggggdgGgk"
    ".kgggdgogggdgggk" ".kkkkdgggggdkkkk" "..kgkdddddddkgk." "..kgk.kdddk.kgk."
    "..kkk.kgggk.kkk." "....kggkkggk...." "....kddk.kddk..." "....kkk..kkk....";
static const char A_MIMIC[] =
    "................" "................" "...kkkkkkkkkk..." "..knnnnnnnnnnnk."
    "..knyknnnnkynnk." "..kNNNNNNNNNNNk." "..kwkwkwkwkwkwk." "..kRRRRRRRRRRRk."
    "..kRRRpppppRRRk." "..kwkwkwkwkwkwk." "..kYYYYYYYYYYYk." "..kNnnnnnnnnnNk."
    "..kNNNNNNNNNNNk." "...kkkkkkkkkkk.." "................" "................";
static const char A_MERCHANT[] =
    "................" "......kkkk......" ".....kOOOOk....." "....kOOOOOOk...."
    "....kOsssskk...." "....kskssksk..k." "....ksssssskkyk." "...kkwwwwwwkkok."
    "..kOOkwwwwkOOkk." "..kOnnnnnnnnOk.." "..kOnYnnnnYnOk.." "..kOnnnnnnnnOk.."
    "..kOOnnnnnnOOk.." "...kkOOOOOOkk..." "....kNNkkNNk...." "....kkk..kkk....";

/* Bosses are 24x24, drawn centred on their tile so they loom over it. */
static const char A_BROOD[] =
    "........................" "........................" "..k..................k.." "...k................k..."
    "....k......kk......k...." "k....k...kUUUUk...k....k" ".k....k.kUUUUUUk.k....k." "..k....kUUrUUrUUk....k.."
    "...kk.kUUUUUUUUUUk.kk..." ".....kUUUrUUUUrUUUk....." "kkk.kUUUUUUUUUUUUUUk.kkk" "...kkUUUUUUUUUUUUUUUkk.."
    "....kUUUuUUUUUUuUUUUk..." "..kkkUUUUuuuuuuuUUUUkkk." ".k..kUUUUUuUUuUUUUUUk..k" "k...kUUUUUUUUUUUUUUk...k"
    "...k.kUUUUuUUUUuUUUUk.k." "..k...kUUUUUUUUUUUUk..k." ".k.....kUUUUUUUUUUk...k." "k.......kkUUUUUUkk.....k"
    ".......k..kkkkkk..k....." "......k............k...." "........................" "........................";
static const char A_WARDEN[] =
    "........................" ".........kkkkkk........." "........kzzzzzzk........" ".......kzGGzzzzZk......."
    ".......kzzzzzzzZk......." ".......kkkkkkkkkk......." ".......kZrkZZkrZk......." ".......kZZZZZZZZk......."
    ".....kkkkZZZZZZkkkk....." "....kzzzkkkkkkkkzzzk...." "...kzGzzkRRRRRRkzzGzk..." "...kzzzkkRyyyyRkkzzzk..."
    "...kzzk.kRRRRRRk.kzzk..." "...kzzk.kRRRRRRk.kzzk..." "...kkkk.kzzzzzzk.kkkk..." "...kGGk.kZZZZZZk.kGGk..."
    "...kGGk.kzzkkzzk.kGGk..." "...kkkk.kzzkkzzk.kkkk..." "........kZZkkZZk........" "........kzzkkzzk........"
    ".......kZZZkkZZZk......." ".......kkkkkkkkkk......." "........................" "........................";
static const char A_LICH[] =
    "........................" "..........k..k.........." ".........kyk.kyk........" "........kyykkkyyk......."
    "........kYyyyyyYk..kcck." "........kvvvvvvvk..kcwck" "........kvkkvkkvk...kck." "........kvckvckvk...kNk."
    "........kvvvvvvvk...kNk." ".........kvkvkvk....kNk." "......kkkUkkkkUkkk.kNk.." ".....kUUUUUUUUUUUUkkNk.."
    "....kUUuUUUvvUUUuUUkNk.." "....kUuUUUvvvvUUUuUkNk.." "....kUUUUUUvvUUUUUUkNk.." "....kUuUUUUUUUUUUuUk.kN."
    "....kUuUUUUUUUUUUuUk.kN." "....kUUUUUUUUUUUUUUk..k." "....kUUUuUUUUUUuUUUk...." ".....kUUUUUUUUUUUUk....."
    "......kkUUkkkkUUkk......" "........kk....kk........" "........................" "........................";
static const char A_WYRM[] =
    "........................" "...kk..............kk..." "..kRrk....kkkk....krRk.." "..kRrrk..kRRRRk..krrRk.."
    "...kRrrkkRrrrrRkkrrRk..." "....kRrrRrrrrrrRrrRk...." "....kRRrrryrryrrRRk....." ".....kRrrrrrrrrrRk......"
    "...kkkRrrrwrrwrrRkkk...." "..kRRRkRrrrrrrrRkRRRk..." ".kRrrRRkRRooooRRkRRrrRk." "kRrrrRRRkRoyyoRkRRRrrrRk"
    "kRrrRRRRkRoyyoRkRRRRrrRk" "kRRRk.kRRRooooRRRk.kRRRk" ".kkk..kRrRRRRRRrRk..kkk." "......kRrrrRRrrrRk......"
    ".....kRrrRRkkRRrrRk....." ".....kRRRk....kRRRk....." "....kRrRk......kRrRk...." "....kyykk......kkyyk...."
    "....kkk..........kkk...." "........................" "........................" "........................";

/* ── Items ────────────────────────────────────────────────────────────── */
static const char A_GOLD[] =
    "................" "................" "................" "................"
    "................" "................" ".......kk......." "......kyyk......"
    "....kkyyyykk...." "...kyykyyyyYk..." "..kyyyYkkyyyYk.." "..kyyyyyyyyyyk.."
    "..kYyyyyyyyyYk.." "...kkkkkkkkkk..." "...4444444444..." "................";
static const char A_HPOT[] =
    "................" "................" "......kkkk......" "......kNNk......"
    "......kNNk......" ".......kk......." "......kwGk......" ".....kwrrrk....."
    "....kwrrrrrk...." "....kwrrrrrk...." "....krrrrrRk...." "....krrrrRRk...."
    ".....kRRRRk....." "......kkkk......" ".....444444....." "................";
static const char A_FLASK[] =
    "................" "................" ".......kk......." "......kNNk......"
    "......kGGk......" "......kGGk......" ".....kGwQGk....." "....kGwPPPGk...."
    "...kGwPPPPPGk..." "...kGPPPPPPGk..." "...kGPPPPPPGk..." "...kGPPPPPPGk..."
    "....kGGGGGGk...." ".....kkkkkk....." "....44444444...." "................";
static const char A_SCROLL[] =
    "................" "................" "................" "...kkkkkkkkkk..."
    "..kVvvvvvvvvvVk." "..kVkkkkkkkkVk.." "...kvvvvvvvvk..." "...kvddvdvdvk..."
    "...kvvvvvvvvk..." "...kvdvddvdvk..." "...kvvvvvvvvk..." "..kVkkkkkkkkVk.."
    "..kVvvvvvvvvvVk." "...kkkkkkkkkk..." "...4444444444..." "................";
static const char A_BOMB[] =
    "................" "..........y....." ".........yoy...." "........kNy....."
    "........kN......" "......kkkkk....." ".....kKKKKKk...." "....kKKdKKKKk..."
    "....kKdwKKKKk..." "....kKKKKKKKk..." "....kKKKKKKKk..." "....kKKKKKKKk..."
    ".....kKKKKKk...." "......kkkkk....." ".....4444444...." "................";
static const char A_OIL[] =
    "................" "................" "................" ".......kkk......"
    "......kYyYk....." ".......kYk......" ".....kkkkkkk...." "....kOoooooOk..."
    "....kOkkkkkOk..." "....kOoyyyoOk..." "....kOoyyyoOk..." "....kOkkkkkOk..."
    "....kOooooOOk..." ".....kkkkkkk...." "....444444444..." "................";
static const char A_KEY[] =
    "................" "................" "................" "................"
    "....kkkk........" "...kyyyyk......." "..kyk..kyk......" "..kyk..kykkkkkk."
    "..kyk..kyyyyyyyk" "...kyyyykkykkyk." "....kkkk..k..k.." "................"
    "................" "................" "....44444444...." "................";
static const char A_SWORD[] =
    "................" "............kk.." "...........kwGk." "..........kwGk.."
    ".........kwGk..." "........kwGk...." ".......kwGk....." "..kk..kwGk......"
    "..kYkkwGk......." "...kYwGk........" "....kYYk........" "...kNkYYk......."
    "..kNk.kkYk......" "..kk....kk......" "....44444444...." "................";
static const char A_ARMOR[] =
    "................" "................" "...kkk....kkk..." "..kzzzk..kzzzk.."
    "..kzGzkkkkzGzk.." "..kzzzzzzzzzzk.." "...kzGzzzzzzk..." "...kzGzzzzzZk..."
    "...kzzzZZzzzk..." "...kzzzzzzzZk..." "...kzzzzzzzZk..." "...kkYYYYYYkk..."
    "....kzzzzzzk...." ".....kkkkkk....." "....44444444...." "................";
static const char A_CRYSTAL[] =
    "................" ".......kk......." "......kcck......" ".....kcwcck....."
    "....kcwwccbk...." "....kcwcccbk...." "...kcwccccbbk..." "...kcccccbbbk..."
    "...kbcccccbbk..." "....kbccbbbk...." "....kbbcbbbk...." ".....kbbbbk....."
    "......kbbk......" ".......kk......." ".....444444....." "................";

/* ── 8x8 HUD icons ────────────────────────────────────────────────────── */
static const char I_HEART[]  = ".kk..kk." "krrkkrwk" "krrrrrrk" "krrrrrrk" ".krrrrk." "..krrk.." "...kk..." "........";
static const char I_COIN[]   = "..kkkk.." ".kyyyyk." "kyyYyyyk" "kyYyyyYk" "kyyYyyYk" "kyyyyyYk" ".kYYYYk." "..kkkk..";
static const char I_KEY[]    = ".kkk...." "kyyyk..." "ky.ykkkk" "kyyyyyyk" ".kkkkyky" ".....k.k" "........" "........";
static const char I_STAIRS[] = "kkkkkkkk" "kGGGGGGk" "kkkkkkkk" "kgggggkk" "kkkkkkkk" "kdddkkkk" "kkkkkkkk" "........";
static const char I_FLAME[]  = "...k...." "..kyk..." "..kyok.." ".kyoork." "kyoyorok" "kowyyork" ".koyyok." "..kkkk..";
static const char I_POTION[] = "..kkk..." "..kGk..." ".kwrrk.." "kwrrrrk." "krrrrrk." "krrrRRk." ".kkkkk.." "........";
static const char I_POISON[] = "...k...." "..kek..." ".kelek.." ".kllek.." "kelllek." "kellleek" ".keeeek." "..kkkk..";
static const char I_BURN[]   = "..k..k.." ".krkkrk." ".kroork." "kroyyork" "kroyyork" "krryyrrk" ".krrrrk." "..kkkk..";
static const char I_REGEN[]  = "..kkkk.." "..kllk.." "kkkllkkk" "klllllek" "keeeleek" "kkkllkkk" "..kelk.." "..kkkk..";
static const char I_HASTE[]  = ".....kk." "....kyyk" "...kyyk." "..kyyyyk" ".kkkyyk." "...kyk.." "..kyk..." "..kk....";
static const char I_INVIS[]  = "........" "..kkkk.." ".kddddk." "kdkddkdk" "kddddddk" "kdddddk." "kdkdkdk." "k.k.k.k.";
static const char I_SIGHT[]  = "........" "..kkkk.." ".kuuuuk." "kuuwwuuk" "kuuwkuuk" ".kuuuuk." "..kkkk.." "........";
static const char I_ROOT[]   = "G..G..G." ".G.G.G.." "..GGG..." "GGGwGGG." "..GGG..." ".G.G.G.." "G..G..G." "........";
static const char I_BLESS[]  = "...k...." "..kyk..." "kkkyykkk" "kyyyyyyk" ".kyyyyk." ".kyykyk." "kyk..kyk" "kk....kk";
static const char I_SWORD[]  = "......kk" ".....kwk" "....kwk." "k..kwk.." "kkkwk..." ".kYk...." "kNkYk..." "kk.kk...";
static const char I_SHIELD[] = "kkkkkkkk" "kzzzzzzk" "kzGzzzZk" "kzzyyzZk" "kzzyyzZk" ".kzzzZk." "..kzZk.." "...kk...";
static const char I_SKULL[]  = ".kkkkkk." "kvvvvvvk" "kvkvvkvk" "kvkvvkvk" "kvvvvvvk" ".kvkkvk." ".kvvvvk." "..kkkk..";
static const char I_FROST[]  = "...k...." ".k.c.k.." "..kck..." "kcccccck" "..kck..." ".k.c.k.." "...k...." "........";

/* ── Sprite registry ─────────────────────────────────────────────────── */
enum {
    TL_FLOOR_A, TL_FLOOR_B, TL_FLOOR_C, TL_WALL_FACE, TL_WALL_FACE_B, TL_WALL_TOP,
    TL_DOOR, TL_DOOR_OPEN, TL_LOCKED, TL_DOOR_SIDE, TL_DOOR_SIDE_OPEN, TL_LOCKED_SIDE,
    TL_STAIRS, TL_CHEST, TL_CHEST_OPEN, TL_SHRINE, TL_SHRINE_USED, TL_ALTAR, TL_ALTAR_USED,
    TL_GRASS, TL_GRASS_CUT, TL_TRAP, TL_TORCH_OUT, TL_WATER0, TL_LAVA0, TL_TORCH0,
    TL_N_MEM,                       /* tiles above also get a dark "memory" copy */
    TL_WATER1 = TL_N_MEM, TL_LAVA1, TL_TORCH1,
    TL_COUNT
};
static const char *const TILE_SRC[TL_COUNT] = {
    A_FLOOR_A, A_FLOOR_B, A_FLOOR_C, A_WALL_FACE, A_WALL_FACE_B, A_WALL_TOP,
    A_DOOR, A_DOOR_OPEN, A_LOCKED, A_DOOR_SIDE, A_DOOR_SIDE_OPEN, A_LOCKED_SIDE,
    A_STAIRS, A_CHEST, A_CHEST_OPEN, A_SHRINE, A_SHRINE_USED, A_ALTAR, A_ALTAR_USED,
    A_GRASS, A_GRASS_CUT, A_TRAP, A_TORCH_OUT, A_WATER0, A_LAVA0, A_TORCH0,
    A_WATER1, A_LAVA1, A_TORCH1,
};

enum {
    SP_WARRIOR, SP_ROGUE, SP_MAGE, SP_RAT, SP_GOBLIN, SP_SPIDER, SP_ARCHER, SP_BAT,
    SP_SLIME, SP_SLIMELET, SP_SHAMAN, SP_SKELETON, SP_GHOST, SP_NECRO, SP_IMP, SP_GOLEM,
    SP_MIMIC, SP_MERCHANT,
    SP_GOLD, SP_HPOT, SP_SCROLL, SP_BOMB, SP_OIL, SP_KEY, SP_SWORD, SP_ARMOR, SP_CRYSTAL,
    SP_BLOOD, SP_BONES, SP_SCORCH, SP_WEB, SP_GOO, SP_FIRE0, SP_FIRE1,
    SP_FLASK0, SP_COUNT = SP_FLASK0 + 6
};
static const char *const SPR_SRC[SP_FLASK0] = {
    A_WARRIOR, A_ROGUE, A_MAGE, A_RAT, A_GOBLIN, A_SPIDER, A_ARCHER, A_BAT,
    A_SLIME, A_SLIMELET, A_SHAMAN, A_SKELETON, A_GHOST, A_NECRO, A_IMP, A_GOLEM,
    A_MIMIC, A_MERCHANT,
    A_GOLD, A_HPOT, A_SCROLL, A_BOMB, A_OIL, A_KEY, A_SWORD, A_ARMOR, A_CRYSTAL,
    A_BLOOD, A_BONES, A_SCORCH, A_WEB, A_GOO, A_FIRE0, A_FIRE1,
};
enum { BS_BROOD, BS_WARDEN, BS_LICH, BS_WYRM, BS_COUNT };
static const char *const BOSS_SRC[BS_COUNT] = { A_BROOD, A_WARDEN, A_LICH, A_WYRM };

enum {
    IC_HEART, IC_COIN, IC_KEY, IC_STAIRS, IC_FLAME, IC_POTION, IC_POISON, IC_BURN,
    IC_REGEN, IC_HASTE, IC_INVIS, IC_SIGHT, IC_ROOT, IC_BLESS, IC_SWORD, IC_SHIELD,
    IC_SKULL, IC_FROST, IC_COUNT
};
static const char *const ICON_SRC[IC_COUNT] = {
    I_HEART, I_COIN, I_KEY, I_STAIRS, I_FLAME, I_POTION, I_POISON, I_BURN,
    I_REGEN, I_HASTE, I_INVIS, I_SIGHT, I_ROOT, I_BLESS, I_SWORD, I_SHIELD,
    I_SKULL, I_FROST,
};

static uint16_t tile_px[TL_COUNT][256];
static uint16_t tile_mem[TL_N_MEM][256];
static uint16_t spr_px[SP_COUNT][256];
static uint16_t boss_px[BS_COUNT][576];
static uint16_t icon_px[IC_COUNT][64];
static uint16_t dith_px[5][256];   /* [1..3] darkness steps, [4] memory veil, [0] frost */
static uint16_t scratch[1024];     /* silhouettes and 2x blow-ups */
static uint16_t edge_lit, edge_mem; /* wall-top rim colours */

static uint16_t pal565[128];
static uint8_t palmono[128];

/* Flask liquid colours (shuffled onto flask kinds every run). */
#define N_FLASK 6
static const uint8_t FLASK_RGB[N_FLASK][3] = {
    {255, 170, 40}, {60, 140, 255}, {60, 200, 110}, {176, 90, 240}, {226, 226, 236}, {128, 104, 60},
};
static const char *const FLASK_COLOR[N_FLASK] = { "Amber", "Azure", "Jade", "Violet", "Pearl", "Murky" };

static uint16_t c565(int r, int g, int b) { return RGB(r, g, b); }

static void pal_set(char ch, int r, int g, int b, int mono)
{
    uint16_t c = c565(r, g, b);
    if (c == TKEY) c ^= 0x20;
    pal565[(uint8_t)ch] = c;
    palmono[(uint8_t)ch] = (uint8_t)mono;
}

static void pal_init_fixed(void)
{
    for (int i = 0; i < N_PAL; i++) {
        int lum = (PAL[i].r * 77 + PAL[i].g * 150 + PAL[i].b * 29) >> 8;
        pal_set(PAL[i].ch, PAL[i].r, PAL[i].g, PAL[i].b, lum >= 100);
    }
}

static uint16_t px_of(char ch)
{
    uint8_t u = (uint8_t)ch;
    if (u >= 128) return TKEY;
    if (g_mono) return palmono[u] ? 0xFFFF : 0x0000;
    return pal565[u];
}

/* Expand indexed art into RGB565.  '.' becomes TKEY, or the pixel of
 * @p under when the art is an overlay to be baked onto a floor tile. */
static void build_art(uint16_t *dst, const char *src, int n, const uint16_t *under)
{
    for (int i = 0; i < n; i++) {
        char c = src[i];
        dst[i] = (c == '.') ? (under ? under[i] : TKEY) : px_of(c);
    }
}

/* Dark, slightly blue copy of a lit tile for explored-but-unlit cells. */
static uint16_t mem_px(uint16_t c, int i)
{
    if (g_mono) {
        /* keep a sparse quarter of the white pixels */
        int x = i & 15, y = i >> 4;
        return (c && !(x & 1) && !(y & 1)) ? 0xFFFF : 0x0000;
    }
    int r = ((c >> 11) & 31) << 3, g = ((c >> 5) & 63) << 2, b = (c & 31) << 3;
    int lum = (r * 77 + g * 150 + b * 29) >> 8;
    uint16_t m = c565(lum * 60 / 255 + 4, lum * 72 / 255 + 5, lum * 120 / 255 + 12);
    return m == TKEY ? (uint16_t)(m ^ 0x20) : m;
}

static void build_global_sprites(void)
{
    pal_init_fixed();
    for (int i = 0; i < SP_FLASK0; i++) build_art(spr_px[i], SPR_SRC[i], 256, 0);
    for (int i = 0; i < BS_COUNT; i++) build_art(boss_px[i], BOSS_SRC[i], 576, 0);
    for (int i = 0; i < IC_COUNT; i++) build_art(icon_px[i], ICON_SRC[i], 64, 0);
    for (int f = 0; f < N_FLASK; f++) {
        const uint8_t *c = FLASK_RGB[f];
        pal_set('P', c[0], c[1], c[2], (c[0] * 77 + c[1] * 150 + c[2] * 29) >> 8 >= 100);
        pal_set('Q', (c[0] + 255) / 2, (c[1] + 255) / 2, (c[2] + 255) / 2, 1);
        build_art(spr_px[SP_FLASK0 + f], A_FLASK, 256, 0);
    }
    /* 4x4 Bayer thresholds: darkness steps 75/50/25 %, frost veil, memory veil */
    static const uint8_t bayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    uint16_t frost = g_mono ? 0xFFFF : c565(190, 236, 255);
    for (int i = 0; i < 256; i++) {
        int b = bayer[((i >> 4) & 3) * 4 + (i & 3)];
        dith_px[0][i] = (b < 6) ? frost : TKEY;
        dith_px[1][i] = (b < 12) ? 0x0000 : TKEY;
        dith_px[2][i] = (b < 8) ? 0x0000 : TKEY;
        dith_px[3][i] = (b < 4) ? 0x0000 : TKEY;
        dith_px[4][i] = (b < 10) ? 0x0000 : TKEY;
    }
}

static int is_overlay_tile(int t)
{
    return (t >= TL_DOOR_SIDE && t <= TL_LOCKED_SIDE) || (t >= TL_CHEST && t <= TL_TRAP);
}

static int cur_biome = -1;
static uint16_t c_accent;         /* biome HUD colour */

static void build_biome(int b)
{
    if (b == cur_biome) return;
    cur_biome = b;
    for (int i = 0; i < N_BIOME_SLOTS; i++) {
        const uint8_t *c = BIOMES[b].rgb[i];
        pal_set(BIOME_CH[i], c[0], c[1], c[2], BIOME_MONO[i]);
    }
    build_art(tile_px[TL_FLOOR_A], TILE_SRC[TL_FLOOR_A], 256, 0);
    for (int t = 1; t < TL_COUNT; t++)
        build_art(tile_px[t], TILE_SRC[t], 256, is_overlay_tile(t) ? tile_px[TL_FLOOR_A] : 0);
    for (int t = 0; t < TL_N_MEM; t++)
        for (int i = 0; i < 256; i++) tile_mem[t][i] = mem_px(tile_px[t][i], i);
    edge_lit = tile_px[TL_WALL_FACE][0];
    edge_mem = tile_mem[TL_WALL_FACE][0];
    c_accent = g_mono ? 0xFFFF : BIOMES[b].ui;
}

/* ── UI colours ──────────────────────────────────────────────────────── */
static uint16_t C_BG, C_PANEL, C_PANEL2, C_EDGE, C_TEXT, C_DIM, C_SEL, C_SELTXT;
static uint16_t C_RED, C_GREEN, C_YELLOW, C_CYAN, C_ORANGE, C_VIOLET, C_BARBG, C_GOLD;

static void ui_init(void)
{
    C_BG = 0x0000;
    C_TEXT = 0xFFFF;
    if (g_mono) {
        C_PANEL = C_PANEL2 = C_BARBG = 0x0000;
        C_EDGE = C_DIM = C_SEL = 0xFFFF;
        C_SELTXT = 0x0000;
        C_RED = C_GREEN = C_YELLOW = C_CYAN = C_ORANGE = C_VIOLET = C_GOLD = 0xFFFF;
    } else {
        C_PANEL = c565(18, 16, 30);   C_PANEL2 = c565(30, 26, 46);
        C_EDGE = c565(110, 96, 140);  C_DIM = c565(150, 146, 170);
        C_SEL = c565(64, 52, 110);    C_SELTXT = 0xFFFF;
        C_RED = c565(255, 70, 70);    C_GREEN = c565(110, 230, 100);
        C_YELLOW = c565(255, 222, 80); C_CYAN = c565(110, 220, 255);
        C_ORANGE = c565(255, 150, 50); C_VIOLET = c565(200, 130, 255);
        C_BARBG = c565(40, 34, 52);   C_GOLD = c565(255, 200, 60);
    }
}

/* ── Game tables ─────────────────────────────────────────────────────── */
#define MAP_W 36
#define MAP_H 24
#define CELL_COLS 4
#define CELL_ROWS 3
#define CELL_W (MAP_W / CELL_COLS)
#define CELL_H (MAP_H / CELL_ROWS)
#define N_ROOMS (CELL_COLS * CELL_ROWS)
#define LAST_DEPTH 20

enum {
    T_WALL, T_FLOOR, T_DOOR, T_LOCKED, T_STAIRS, T_CHEST, T_CHEST_OPEN, T_SHRINE,
    T_SHRINE_USED, T_ALTAR, T_ALTAR_USED, T_GRASS, T_GRASS_CUT, T_WATER, T_LAVA,
    T_TORCH, T_TORCH_OUT
};

#define N_WEAPONS 6
static const char *const WEAPON_NAME[N_WEAPONS] = {
    "Fists", "Dagger", "Short Sword", "Battle Axe", "War Hammer", "Rune Blade"
};
static const int8_t WEAPON_ATK[N_WEAPONS] = { 0, 1, 3, 5, 7, 10 };
#define N_ARMORS 6
static const char *const ARMOR_NAME[N_ARMORS] = {
    "Rags", "Leather", "Chain Mail", "Scale Mail", "Plate", "Rune Plate"
};
static const int8_t ARMOR_DEF[N_ARMORS] = { 0, 1, 2, 3, 5, 7 };

enum { CL_WARRIOR, CL_ROGUE, CL_MAGE, N_CLASS };
typedef struct {
    const char *name, *skill, *hud, *line1, *line2;
    uint8_t hp, atk, def, wpn, arm, cd, spr;
} cls_t;
static const cls_t CLASSES[N_CLASS] = {
    {"Warrior", "Cleave", "Cleave", "Tough and well armed.", "Skill: strike all around.", 28, 3, 2, 2, 1, 7, SP_WARRIOR},
    {"Rogue", "Vanish", "Vanish", "Crits, ambushes, traps.", "Skill: turn invisible.", 22, 3, 1, 1, 1, 14, SP_ROGUE},
    {"Mage", "Arcane Bolt", "Bolt", "Frail, but casts bolts.", "Skill: ranged magic bolt.", 18, 2, 0, 1, 0, 3, SP_MAGE},
};

enum {
    M_RAT, M_GOBLIN, M_SPIDER, M_ARCHER, M_BAT, M_SLIME, M_SLIMELET, M_SHAMAN,
    M_SKELETON, M_GHOST, M_NECRO, M_IMP, M_GOLEM, M_MIMIC, M_MERCHANT,
    M_BROOD, M_WARDEN, M_LICH, M_WYRM, N_MOB
};
#define MF_RANGED  0x01
#define MF_FAST    0x02
#define MF_SLOW    0x04
#define MF_PHASE   0x08
#define MF_BOSS    0x10
#define MF_UNDEAD  0x20
#define MF_ERRATIC 0x40
#define MF_PASSIVE 0x80
#define MF_FLY     0x100
typedef struct {
    const char *name;
    uint8_t spr, hp0, hpd, atk0, atkd, def, eva, xp;
    uint16_t flags;
} mobdef_t;
/* hp = hp0 + depth*hpd/4, atk = atk0 + depth*atkd/4.  Bosses: spr is a BS_*. */
static const mobdef_t MOBS[N_MOB] = {
    {"Rat",          SP_RAT,       3,  4,  1, 2, 0, 10,  2, 0},
    {"Goblin",       SP_GOBLIN,    5,  6,  2, 3, 0,  5,  3, 0},
    {"Spider",       SP_SPIDER,    4,  5,  2, 3, 0, 10,  4, 0},
    {"Goblin Archer",SP_ARCHER,    4,  4,  2, 3, 0,  5,  4, MF_RANGED},
    {"Bat",          SP_BAT,       3,  4,  1, 3, 0, 30,  4, MF_FAST | MF_ERRATIC | MF_FLY},
    {"Slime",        SP_SLIME,     8,  8,  2, 3, 1,  0,  5, 0},
    {"Slimelet",     SP_SLIMELET,  3,  3,  1, 2, 0,  5,  1, 0},
    {"Shaman",       SP_SHAMAN,    6,  4,  1, 2, 0,  5,  6, MF_RANGED},
    {"Skeleton",     SP_SKELETON,  8,  6,  3, 3, 2,  0,  6, MF_UNDEAD},
    {"Ghost",        SP_GHOST,     6,  4,  3, 3, 0, 25,  7, MF_PHASE | MF_UNDEAD | MF_FLY},
    {"Necromancer",  SP_NECRO,     8,  4,  2, 2, 1,  5,  8, MF_RANGED},
    {"Imp",          SP_IMP,       6,  4,  3, 3, 0, 15,  7, MF_FAST},
    {"Golem",        SP_GOLEM,    20, 12,  5, 4, 4,  0, 10, MF_SLOW},
    {"Mimic",        SP_MIMIC,    10,  8,  4, 3, 1,  0,  8, 0},
    {"Merchant",     SP_MERCHANT, 99,  0,  0, 0, 9,  0,  0, MF_PASSIVE},
    {"Brood Mother", BS_BROOD,    70,  0,  7, 0, 1,  5, 60, MF_BOSS},
    {"The Warden",   BS_WARDEN,  130,  0, 13, 0, 4,  0, 90, MF_BOSS},
    {"Bone Lich",    BS_LICH,    150,  0, 17, 0, 3, 10, 120, MF_BOSS | MF_UNDEAD | MF_RANGED},
    {"Ember Wyrm",   BS_WYRM,    250,  0, 24, 0, 6,  0, 200, MF_BOSS},
};

enum { IT_NONE, IT_GOLD, IT_HPOT, IT_FLASK, IT_SCROLL, IT_BOMB, IT_OIL, IT_KEY,
       IT_WEAPON, IT_ARMOR, IT_CRYSTAL };

enum { FL_STRENGTH, FL_VIGOR, FL_SIGHT, FL_HASTE, FL_TOXIN, FL_FIRE };
static const char *const FLASK_NAME[N_FLASK] = {
    "Strength", "Vigor", "Insight", "Haste", "Toxin", "Liquid Fire"
};
static const char *const FLASK_DESC[N_FLASK] = {
    "+1 attack, for good.", "+5 max HP and heals.", "Sense every foe here.",
    "Act twice as fast.", "Poison! Do not drink.", "Bursts into flame!"
};

enum { SC_TELEPORT, SC_FIREBALL, SC_FROST, SC_MAPPING, SC_ENCHANT, SC_IDENTIFY, N_SCROLL };
static const char *const SCROLL_NAME[N_SCROLL] = {
    "Teleport", "Fireball", "Frost", "Mapping", "Enchant", "Identify"
};
static const char *const SCROLL_DESC[N_SCROLL] = {
    "Blink to a far spot.", "Blast foes around you.", "Freeze foes in sight.",
    "Reveal this floor.", "+1 to weapon or armour.", "Name every flask."
};

enum { PK_VITAL, PK_MIGHT, PK_GUARD, PK_VAMP, PK_CRIT, PK_DODGE, PK_LANTERN,
       PK_ALCH, PK_GREED, PK_REGEN, PK_FOCUS, PK_THORNS, N_PERK };
typedef struct { const char *name, *d1, *d2; uint8_t icon, max; } perk_t;
static const perk_t PERKS[N_PERK] = {
    {"Vitality",  "+8 max HP",  "and heal 8", IC_HEART,  9},
    {"Might",     "+2 attack",  "",           IC_SWORD,  9},
    {"Guard",     "+1 defence", "",           IC_SHIELD, 9},
    {"Vampirism", "Heal 2 HP",  "per kill",   IC_SKULL,  4},
    {"Precision", "+10% crit",  "chance",     IC_SIGHT,  4},
    {"Evasion",   "+7% dodge",  "chance",     IC_HASTE,  4},
    {"Lantern",   "+1 light",   "& refuel",   IC_FLAME,  3},
    {"Alchemy",   "Potions",    "heal +50%",  IC_POTION, 2},
    {"Greed",     "+50% gold",  "",           IC_COIN,   3},
    {"Regrowth",  "Much faster","regen",      IC_REGEN,  3},
    {"Focus",     "Skill",      "-2 cooldown", IC_BLESS, 2},
    {"Thorns",    "Melee foes", "take 3 dmg", IC_ROOT,   3},
};

/* trap types (low 3 bits), 0x80 = still hidden */
enum { TR_NONE, TR_DART, TR_FIRE, TR_ALARM, TR_TELEPORT };
#define TRAP_HIDDEN 0x80

/* ── Game state ──────────────────────────────────────────────────────── */
static uint8_t map[MAP_H][MAP_W];
static uint8_t seen[MAP_H][MAP_W];
static uint8_t vis[MAP_H][MAP_W];
static uint8_t light[MAP_H][MAP_W];
static uint8_t slight[MAP_H][MAP_W];   /* static light: torches, lava, shrines */
static uint8_t trap[MAP_H][MAP_W];
static uint8_t decal[MAP_H][MAP_W];
static uint8_t fire[MAP_H][MAP_W];
static uint8_t warn[MAP_H][MAP_W];
static uint8_t dmap[MAP_H][MAP_W];
enum { DC_NONE, DC_BLOOD, DC_BONES, DC_SCORCH, DC_WEB, DC_GOO };

typedef struct { int8_t x, y, w, h; uint8_t kind, junction; } room_t;
enum { RK_PLAIN, RK_START, RK_STAIRS, RK_VAULT, RK_SHOP, RK_SHRINE, RK_GARDEN, RK_POOL, RK_DEN };
static room_t rooms[N_ROOMS];

#define MAX_MOBS 28
enum { ST_SLEEP, ST_HUNT, ST_WANDER, ST_PILE, ST_HIDDEN };
typedef struct {
    int8_t x, y, tx, ty;
    uint8_t type, alive, state, cd, cd2, frozen, burn, flash, lunge, revived, summons, prep, enraged;
    int8_t ldx, ldy, pdx, pdy;
    int16_t hp, mhp, atk;
    int8_t def;
} mob_t;
static mob_t mobs[MAX_MOBS];

#define MAX_ITEMS 32
typedef struct { int8_t x, y; uint8_t type, sub; int16_t amt; } item_t;
static item_t items[MAX_ITEMS];

/* player */
static int pcls, px, py, p_hp, p_mhp, p_str, p_def, p_lvl, p_xp, p_xpn;
static int p_gold, p_wpn, p_wench, p_arm, p_aench, p_torch, p_fuel, p_keys;
static int n_hpot, n_bomb, n_oil, n_flask[N_FLASK], n_scroll[N_SCROLL];
static uint8_t flask_col[N_FLASK], flask_known[N_FLASK];
static int perk[N_PERK], pending_perks;
static int skill_cd, haste_turns, haste_flip, invis_turns, root_turns, sight_on;
static int bless_turns, poison_turns, burn_turns, regen_ctr;
static int depth, turns, kills, dead, won, quit_req, restart_req, last_dir;
static const char *killer;
static int stairs_x, stairs_y, boss_idx;
static int hi_score, best_depth, total_wins;

#define TORCH_MAX 200
#define BIOME() ((depth - 1) / 5)

/* animation / fx */
static uint32_t anim_tick;
static int cam_x, cam_y;
static uint8_t p_flash, p_lunge, shake, flash_white, levelup_fx;
static int8_t p_ldx, p_ldy;

#define MAX_PART 56
typedef struct { int16_t x, y, vx, vy; uint16_t col; uint8_t life, grav; } part_t;
static part_t parts[MAX_PART];

#define MAX_PROJ 4
enum { PJ_ARROW, PJ_BOLT, PJ_FROST, PJ_WEB, PJ_BOMB, PJ_DARK, PJ_ZAP };
typedef struct { int16_t x0, y0, x1, y1; uint8_t t, dur, kind; } proj_t;
static proj_t projs[MAX_PROJ];

#define MAX_FLOAT 8
typedef struct { int16_t x, y; uint16_t col; uint8_t t; char txt[8]; } floatx_t;
static floatx_t floats[MAX_FLOAT];

/* message log */
#define LOG_N 2
static char logm[LOG_N][44];
static uint16_t logc[LOG_N];
static uint8_t logrep[LOG_N];
static uint16_t log_turn[LOG_N];

/* ── Small helpers ───────────────────────────────────────────────────── */
static int rn(int n) { return n > 0 ? (int)g_rand_n((uint32_t)n) : 0; }
static int chance(int pct) { return rn(100) < pct; }
static int iabs(int v) { return v < 0 ? -v : v; }
static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }
static int iclamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static int in_map(int x, int y) { return x >= 0 && y >= 0 && x < MAP_W && y < MAP_H; }
static void bclear(void *p, int n) { uint8_t *b = p; for (int i = 0; i < n; i++) b[i] = 0; }
static int streq(const char *a, const char *b) { return strcmp(a, b) == 0; }
static void scopy(char *d, const char *s, int n)
{
    int i = 0;
    for (; s[i] && i < n - 1; i++) d[i] = s[i];
    d[i] = '\0';
}

static const int8_t DX[4] = { 0, 1, 0, -1 };
static const int8_t DY[4] = { -1, 0, 1, 0 };

static void msg(const char *s, uint16_t c)
{
    if (streq(logm[LOG_N - 1], s) && log_turn[LOG_N - 1] + 8 > turns) {
        if (logrep[LOG_N - 1] < 99) logrep[LOG_N - 1]++;
        logc[LOG_N - 1] = c;
        log_turn[LOG_N - 1] = (uint16_t)turns;
        return;
    }
    for (int i = 0; i < LOG_N - 1; i++) {
        scopy(logm[i], logm[i + 1], 44);
        logc[i] = logc[i + 1];
        logrep[i] = logrep[i + 1];
        log_turn[i] = log_turn[i + 1];
    }
    scopy(logm[LOG_N - 1], s, 44);
    logc[LOG_N - 1] = c;
    logrep[LOG_N - 1] = 1;
    log_turn[LOG_N - 1] = (uint16_t)turns;
}

static char mb[48];
static char *mstart(const char *s) { mb[0] = '\0'; return g_cat(mb, s); }

/* ── Effects ─────────────────────────────────────────────────────────── */
static void spawn_part(int x, int y, int vx, int vy, uint16_t col, int life, int grav)
{
    for (int i = 0; i < MAX_PART; i++) {
        if (parts[i].life) continue;
        parts[i].x = (int16_t)(x * 16); parts[i].y = (int16_t)(y * 16);
        parts[i].vx = (int16_t)vx; parts[i].vy = (int16_t)vy;
        parts[i].col = col; parts[i].life = (uint8_t)life; parts[i].grav = (uint8_t)grav;
        return;
    }
}

/* Burst of @p n particles from the centre of tile (tx,ty). */
static void burst(int tx, int ty, int n, uint16_t col, int speed, int grav)
{
    int cx = tx * TS + TS / 2, cy = ty * TS + TS / 2;
    for (int i = 0; i < n; i++)
        spawn_part(cx + rn(5) - 2, cy + rn(5) - 2, rn(speed * 2 + 1) - speed,
                   rn(speed * 2 + 1) - speed - (grav ? speed : 0), col, 4 + rn(5), grav);
}

static void spawn_float(int tx, int ty, const char *s, uint16_t col)
{
    int best = 0;
    for (int i = 0; i < MAX_FLOAT; i++) {
        if (!floats[i].t) { best = i; break; }
        if (floats[i].t < floats[best].t) best = i;
    }
    floatx_t *f = &floats[best];
    f->x = (int16_t)(tx * TS + TS / 2 - g_tw(s) / 2);
    f->y = (int16_t)(ty * TS - 2);
    /* stack numbers that land on the same tile */
    for (int i = 0; i < MAX_FLOAT; i++)
        if (i != best && floats[i].t > 8 && iabs(floats[i].y - f->y) < 6 && iabs(floats[i].x - f->x) < 12)
            f->y -= 8;
    f->col = col; f->t = 12;
    scopy(f->txt, s, 8);
}

static void float_num(int tx, int ty, int v, uint16_t col, const char *prefix)
{
    char b[8];
    b[0] = '\0';
    g_cat(b, prefix);
    g_cat_int(b, v);
    spawn_float(tx, ty, b, col);
}

static void spawn_proj(int x0, int y0, int x1, int y1, int kind)
{
    for (int i = 0; i < MAX_PROJ; i++) {
        if (projs[i].dur) continue;
        int d = imax(iabs(x1 - x0), iabs(y1 - y0));
        projs[i].x0 = (int16_t)(x0 * TS + TS / 2); projs[i].y0 = (int16_t)(y0 * TS + TS / 2);
        projs[i].x1 = (int16_t)(x1 * TS + TS / 2); projs[i].y1 = (int16_t)(y1 * TS + TS / 2);
        projs[i].t = 0;
        projs[i].dur = (uint8_t)iclamp((d + 1) / 2, 1, 5);
        projs[i].kind = (uint8_t)kind;
        return;
    }
}

/* ── Map queries ─────────────────────────────────────────────────────── */
static int is_wall_t(int t) { return t == T_WALL || t == T_TORCH || t == T_TORCH_OUT; }
static int blocks_move(int t) { return is_wall_t(t) || t == T_LAVA || t == T_LOCKED; }

static int mob_at(int x, int y)
{
    for (int i = 0; i < MAX_MOBS; i++)
        if (mobs[i].alive && mobs[i].state != ST_PILE && mobs[i].x == x && mobs[i].y == y)
            return i;
    return -1;
}
static int occupied(int x, int y) { return (x == px && y == py) || mob_at(x, y) >= 0; }

static int blocks_sight(int x, int y)
{
    int t = map[y][x];
    if (is_wall_t(t) || t == T_GRASS) return 1;
    if (t == T_DOOR || t == T_LOCKED) return !occupied(x, y);
    return 0;
}

/* Bresenham line of sight; the end tiles themselves never block. */
static int los_clear(int x0, int y0, int x1, int y1)
{
    int dx = iabs(x1 - x0), dy = iabs(y1 - y0);
    int sx = x1 > x0 ? 1 : -1, sy = y1 > y0 ? 1 : -1;
    int x = x0, y = y0, err = dx - dy;
    while (1) {
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x += sx; }
        if (e2 < dx)  { err += dx; y += sy; }
        if (x == x1 && y == y1) return 1;
        if (blocks_sight(x, y)) return 0;
    }
}

static uint32_t thash(int x, int y)
{
    uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u ^ (uint32_t)depth * 83492791u;
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return h;
}

/* ── Lighting & field of view ────────────────────────────────────────── */
static void add_light(uint8_t (*dst)[MAP_W], int sx, int sy, int r, int top)
{
    int rr = r * r + r;
    for (int y = sy - r; y <= sy + r; y++) {
        if (y < 0 || y >= MAP_H) continue;
        for (int x = sx - r; x <= sx + r; x++) {
            if (x < 0 || x >= MAP_W) continue;
            int d2 = (x - sx) * (x - sx) + (y - sy) * (y - sy);
            if (d2 > rr) continue;
            int lvl = top - (d2 * top) / (rr + 1);
            if (lvl < 1) lvl = 1;
            if (lvl <= dst[y][x]) continue;
            if (!(x == sx && y == sy) && !los_clear(sx, sy, x, y)) continue;
            dst[y][x] = (uint8_t)lvl;
        }
    }
}

static void calc_static_light(void)
{
    bclear(slight, sizeof(slight));
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) {
            int t = map[y][x];
            if (t == T_TORCH && y + 1 < MAP_H) add_light(slight, x, y + 1, 4, 3);
            else if (t == T_LAVA) add_light(slight, x, y, 2, 2);
            else if (t == T_SHRINE || t == T_ALTAR) add_light(slight, x, y, 2, 2);
        }
}

static int eff_torch(void)
{
    int r = p_torch;
    if (p_fuel <= 0) r = 2;
    else if (p_fuel <= 15) r -= 2;
    else if (p_fuel <= 40) r -= 1;
    return imax(1, r);
}

static void update_vision(void)
{
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) {
            light[y][x] = slight[y][x];
            vis[y][x] = 0;
        }
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++)
            if (fire[y][x]) add_light(light, x, y, 2, 3);

    int r = eff_torch(), rr = r * r + r;
    for (int y = py - r; y <= py + r; y++) {
        if (y < 0 || y >= MAP_H) continue;
        for (int x = px - r; x <= px + r; x++) {
            if (x < 0 || x >= MAP_W) continue;
            int d2 = (x - px) * (x - px) + (y - py) * (y - py);
            if (d2 > rr) continue;
            int lvl = d2 * 4 <= r * r ? 4 : d2 * 16 <= r * r * 9 ? 3 : d2 <= r * r ? 2 : 1;
            if (lvl > light[y][x] && los_clear(px, py, x, y)) light[y][x] = (uint8_t)lvl;
        }
    }
    light[py][px] = 4;

    int hx = VIEW_COLS / 2 + 2, hy = VIEW_ROWS / 2 + 2;
    int spot = (pcls == CL_ROGUE) ? 2 : 1;
    for (int y = py - hy; y <= py + hy; y++) {
        if (y < 0 || y >= MAP_H) continue;
        for (int x = px - hx; x <= px + hx; x++) {
            if (x < 0 || x >= MAP_W || !light[y][x]) continue;
            if (!(x == px && y == py) && !los_clear(px, py, x, y)) continue;
            vis[y][x] = 1;
            seen[y][x] = 1;
            if ((trap[y][x] & TRAP_HIDDEN) && iabs(x - px) <= spot && iabs(y - py) <= spot)
                trap[y][x] &= (uint8_t)~TRAP_HIDDEN;
        }
    }
}

/* ── Distance map (BFS from the player) for monster pathing ──────────── */
static int16_t bfs_q[MAP_W * MAP_H];

static void calc_dmap(int sx, int sy)
{
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) dmap[y][x] = 255;
    int h = 0, t = 0;
    dmap[sy][sx] = 0;
    bfs_q[t++] = (int16_t)(sy * MAP_W + sx);
    while (h < t) {
        int c = bfs_q[h++], cx = c % MAP_W, cy = c / MAP_W, d = dmap[cy][cx];
        if (d >= 250) continue;
        for (int k = 0; k < 4; k++) {
            int nx = cx + DX[k], ny = cy + DY[k];
            if (!in_map(nx, ny) || dmap[ny][nx] != 255 || blocks_move(map[ny][nx])) continue;
            dmap[ny][nx] = (uint8_t)(d + 1);
            bfs_q[t++] = (int16_t)(ny * MAP_W + nx);
        }
    }
}

/* ── Dungeon generation ──────────────────────────────────────────────── */
static void carve(int x, int y)
{
    if (x > 0 && y > 0 && x < MAP_W - 1 && y < MAP_H - 1 && map[y][x] == T_WALL) map[y][x] = T_FLOOR;
}
static void carve_h(int x0, int x1, int y) { for (int x = imin(x0, x1); x <= imax(x0, x1); x++) carve(x, y); }
static void carve_v(int y0, int y1, int x) { for (int y = imin(y0, y1); y <= imax(y0, y1); y++) carve(x, y); }

/* Z-shaped corridor through the wall gap between two neighbouring cells. */
static void connect_rooms(int a, int b, int horizontal)
{
    room_t *A = &rooms[a], *B = &rooms[b];
    int ax = A->x + rn(A->w), ay = A->y + rn(A->h);
    int bx = B->x + rn(B->w), by = B->y + rn(B->h);
    if (horizontal) {
        int lo = A->x + A->w, hi = B->x - 1;
        int mx = lo + rn(hi - lo + 1);
        carve_h(ax, mx, ay); carve_v(ay, by, mx); carve_h(mx, bx, by);
    } else {
        int lo = A->y + A->h, hi = B->y - 1;
        int my = lo + rn(hi - lo + 1);
        carve_v(ay, my, ax); carve_h(ax, bx, my); carve_v(my, by, bx);
    }
}

static int in_room(int r, int x, int y)
{
    return x >= rooms[r].x && y >= rooms[r].y && x < rooms[r].x + rooms[r].w && y < rooms[r].y + rooms[r].h;
}
static int room_of(int x, int y)
{
    for (int r = 0; r < N_ROOMS; r++) if (in_room(r, x, y)) return r;
    return -1;
}

static int item_at(int x, int y)
{
    for (int i = 0; i < MAX_ITEMS; i++)
        if (items[i].type && items[i].x == x && items[i].y == y) return i;
    return -1;
}

/* Free floor: walkable, empty, no trap/feature.  r >= 0 limits to a room. */
static int tile_free(int x, int y)
{
    int t = map[y][x];
    if (t != T_FLOOR && t != T_GRASS && t != T_GRASS_CUT) return 0;
    if (trap[y][x] || occupied(x, y) || item_at(x, y) >= 0) return 0;
    return 1;
}
static int find_free_in(int r, int *ox, int *oy)
{
    for (int t = 0; t < 40; t++) {
        int x = rooms[r].x + rn(rooms[r].w), y = rooms[r].y + rn(rooms[r].h);
        if (tile_free(x, y)) { *ox = x; *oy = y; return 1; }
    }
    return 0;
}
static int find_free(int *ox, int *oy, int min_dist, int avoid_room)
{
    for (int t = 0; t < 200; t++) {
        int x = 1 + rn(MAP_W - 2), y = 1 + rn(MAP_H - 2);
        if (!tile_free(x, y)) continue;
        if (iabs(x - px) + iabs(y - py) < min_dist) continue;
        int r = room_of(x, y);
        if (r >= 0 && (r == avoid_room || rooms[r].kind == RK_VAULT || rooms[r].kind == RK_SHOP)) continue;
        *ox = x; *oy = y;
        return 1;
    }
    return 0;
}

static int add_item(int x, int y, int type, int sub, int amt)
{
    for (int i = 0; i < MAX_ITEMS; i++) {
        if (items[i].type) continue;
        items[i].x = (int8_t)x; items[i].y = (int8_t)y;
        items[i].type = (uint8_t)type; items[i].sub = (uint8_t)sub; items[i].amt = (int16_t)amt;
        return i;
    }
    return -1;
}

static int gear_tier(void)
{
    return iclamp(1 + depth / 4 + rn(2), 1, N_WEAPONS - 1);
}

static void random_item(int x, int y)
{
    int r = rn(100);
    if (r < 28)      add_item(x, y, IT_GOLD, 0, 4 + rn(6) + depth * 2);
    else if (r < 46) add_item(x, y, IT_HPOT, 0, 0);
    else if (r < 60) add_item(x, y, IT_FLASK, rn(N_FLASK), 0);
    else if (r < 76) add_item(x, y, IT_SCROLL, rn(N_SCROLL), 0);
    else if (r < 84) add_item(x, y, IT_BOMB, 0, 0);
    else if (r < 92) add_item(x, y, IT_OIL, 0, 0);
    else if (r < 96) add_item(x, y, IT_WEAPON, gear_tier(), 0);
    else             add_item(x, y, IT_ARMOR, gear_tier(), 0);
}

static int pick_mob_type(void)
{
    int r = rn(100), d = depth;
    switch (BIOME()) {
    case 0:
        if (r < 30) return M_RAT;
        if (r < 60) return M_GOBLIN;
        if (r < 78) return d >= 2 ? M_SPIDER : M_RAT;
        if (r < 94) return d >= 2 ? M_ARCHER : M_GOBLIN;
        return d >= 3 ? M_BAT : M_GOBLIN;
    case 1:
        if (r < 14) return M_GOBLIN;
        if (r < 32) return M_BAT;
        if (r < 52) return M_SLIME;
        if (r < 64) return M_SHAMAN;
        if (r < 77) return M_ARCHER;
        if (r < 88) return M_SPIDER;
        return d >= 8 ? M_GOLEM : M_SLIME;
    case 2:
        if (r < 30) return M_SKELETON;
        if (r < 50) return M_GHOST;
        if (r < 62) return M_NECRO;
        if (r < 75) return M_BAT;
        if (r < 85) return M_SLIME;
        return M_ARCHER;
    default:
        if (r < 30) return M_IMP;
        if (r < 50) return M_GOLEM;
        if (r < 65) return M_SKELETON;
        if (r < 77) return M_GHOST;
        if (r < 85) return M_NECRO;
        if (r < 90) return M_SHAMAN;
        return M_ARCHER;
    }
}

static int new_mob(int type, int x, int y, int state)
{
    for (int i = 0; i < MAX_MOBS; i++) {
        mob_t *m = &mobs[i];
        if (m->alive) continue;
        bclear(m, sizeof(*m));
        const mobdef_t *d = &MOBS[type];
        m->type = (uint8_t)type; m->alive = 1; m->state = (uint8_t)state;
        m->x = m->tx = (int8_t)x; m->y = m->ty = (int8_t)y;
        m->mhp = m->hp = (int16_t)(d->hp0 + depth * d->hpd / 4);
        m->atk = (int16_t)(d->atk0 + depth * d->atkd / 4);
        m->def = (int8_t)d->def;
        return i;
    }
    return -1;
}

static void place_mobs(int start_room)
{
    int boss_floor = (depth % 5 == 0);
    int n = 4 + depth * 2 / 3;
    if (n > 16) n = 16;
    if (boss_floor) n /= 2;
    for (int k = 0; k < n; k++) {
        int x, y;
        if (!find_free(&x, &y, 7, start_room)) break;
        int type = pick_mob_type();
        int st = chance(65) ? ST_SLEEP : ST_WANDER;
        new_mob(type, x, y, st);
        if (type == M_RAT || (type == M_BAT && chance(50))) {     /* packs */
            for (int j = 0; j < 4; j++) {
                int nx = x + DX[j], ny = y + DY[j];
                if (in_map(nx, ny) && tile_free(nx, ny) && chance(45)) new_mob(type, nx, ny, st);
            }
        }
    }
    if (depth >= 2 && chance(35)) {
        int x, y;
        if (find_free(&x, &y, 6, start_room)) new_mob(M_MIMIC, x, y, ST_HIDDEN);
    }
}

static void decorate_room(int r)
{
    room_t *R = &rooms[r];
    if (R->junction) return;
    /* a wall torch on the north wall */
    if (chance(55)) {
        int x = R->x + rn(R->w), y = R->y - 1;
        if (map[y][x] == T_WALL) map[y][x] = T_TORCH;
    }
    for (int y = R->y; y < R->y + R->h; y++)
        for (int x = R->x; x < R->x + R->w; x++) {
            if (map[y][x] != T_FLOOR) continue;
            int k = rn(100);
            if (k < 3) decal[y][x] = DC_BLOOD;
            else if (k < (BIOME() == 2 ? 9 : 5)) decal[y][x] = DC_BONES;
        }
    if (BIOME() == 0 && chance(40)) {        /* cobwebs in the cellars */
        int cx = chance(50) ? R->x : R->x + R->w - 1;
        if (map[R->y][cx] == T_FLOOR) decal[R->y][cx] = DC_WEB;
    }
}

static void fill_room_feature(int r)
{
    room_t *R = &rooms[r];
    int cx = R->x + R->w / 2, cy = R->y + R->h / 2;
    switch (R->kind) {
    case RK_GARDEN:
        for (int y = R->y; y < R->y + R->h; y++)
            for (int x = R->x; x < R->x + R->w; x++)
                if (map[y][x] == T_FLOOR && chance(60) && !(x == cx && y == cy)) map[y][x] = T_GRASS;
        break;
    case RK_POOL:
        /* lava only off the room rim, so every entrance stays connected */
        for (int y = R->y + 1; y < R->y + R->h - 1; y++)
            for (int x = R->x + 1; x < R->x + R->w - 1; x++)
                if (map[y][x] == T_FLOOR && chance(70)) map[y][x] = (BIOME() == 3) ? T_LAVA : T_WATER;
        if (BIOME() != 3) {
            for (int y = R->y; y < R->y + R->h; y++)
                for (int x = R->x; x < R->x + R->w; x++)
                    if (map[y][x] == T_FLOOR && chance(25)) map[y][x] = T_WATER;
        } else {
            /* no islands: floor the rim cannot reach becomes lava too */
            uint8_t reach[CELL_H][CELL_W];
            int h = 0, t = 0;
            bclear(reach, sizeof(reach));
            reach[0][0] = 1;
            bfs_q[t++] = (int16_t)(R->y * MAP_W + R->x);
            while (h < t) {
                int c = bfs_q[h++], cx = c % MAP_W, cy = c / MAP_W;
                for (int k = 0; k < 4; k++) {
                    int nx = cx + DX[k], ny = cy + DY[k];
                    if (!in_room(r, nx, ny) || map[ny][nx] == T_LAVA || reach[ny - R->y][nx - R->x]) continue;
                    reach[ny - R->y][nx - R->x] = 1;
                    bfs_q[t++] = (int16_t)(ny * MAP_W + nx);
                }
            }
            for (int y = R->y; y < R->y + R->h; y++)
                for (int x = R->x; x < R->x + R->w; x++)
                    if (!reach[y - R->y][x - R->x] && map[y][x] != T_LAVA) map[y][x] = T_LAVA;
        }
        break;
    case RK_SHRINE:
        map[cy][cx] = (depth >= 3 && chance(35)) ? T_ALTAR : T_SHRINE;
        break;
    case RK_SHOP:
        new_mob(M_MERCHANT, cx, cy, ST_WANDER);
        break;
    case RK_VAULT: {
        int n = 2 + rn(2);
        for (int k = 0; k < n; k++) {
            int x, y;
            if (!find_free_in(r, &x, &y)) break;
            int q = rn(6);
            if (q == 0) add_item(x, y, IT_WEAPON, iclamp(gear_tier() + 1, 1, N_WEAPONS - 1), 0);
            else if (q == 1) add_item(x, y, IT_ARMOR, iclamp(gear_tier() + 1, 1, N_ARMORS - 1), 0);
            else if (q == 2) add_item(x, y, IT_GOLD, 0, 25 + depth * 6);
            else if (q == 3) map[y][x] = T_CHEST;
            else random_item(x, y);
        }
        break;
    }
    case RK_DEN: {
        for (int k = 0; k < 3; k++) {
            int x, y;
            if (find_free_in(r, &x, &y)) new_mob(pick_mob_type(), x, y, ST_SLEEP);
        }
        int x, y;
        if (find_free_in(r, &x, &y)) add_item(x, y, IT_GOLD, 0, 15 + depth * 4);
        break;
    }
    default: break;
    }
}

static void gen_floor(void)
{
    bclear(map, sizeof(map)); bclear(seen, sizeof(seen)); bclear(trap, sizeof(trap));
    bclear(decal, sizeof(decal)); bclear(fire, sizeof(fire)); bclear(warn, sizeof(warn));
    bclear(mobs, sizeof(mobs)); bclear(items, sizeof(items));
    bclear(parts, sizeof(parts)); bclear(projs, sizeof(projs)); bclear(floats, sizeof(floats));
    px = py = -1;

    /* rooms (or 1x1 junctions) in a 4x3 grid of cells */
    int real = 0;
    for (int i = 0; i < N_ROOMS; i++) {
        room_t *R = &rooms[i];
        int x0 = (i % CELL_COLS) * CELL_W, y0 = (i / CELL_COLS) * CELL_H;
        R->kind = RK_PLAIN;
        R->junction = (uint8_t)(chance(20) && real + (N_ROOMS - i) > 7);
        if (R->junction) {
            R->w = R->h = 1;
            R->x = (int8_t)(x0 + 2 + rn(CELL_W - 4));
            R->y = (int8_t)(y0 + 2 + rn(CELL_H - 4));
        } else {
            R->w = (int8_t)(4 + rn(CELL_W - 2 - 4 + 1));
            R->h = (int8_t)(3 + rn(CELL_H - 2 - 3 + 1));
            R->x = (int8_t)(x0 + 1 + rn(CELL_W - 1 - R->w));
            R->y = (int8_t)(y0 + 1 + rn(CELL_H - 1 - R->h));
            real++;
        }
        for (int y = R->y; y < R->y + R->h; y++)
            for (int x = R->x; x < R->x + R->w; x++) map[y][x] = T_FLOOR;
    }

    /* spanning tree over the cell grid + a few loops */
    uint8_t linked[N_ROOMS][4];
    uint8_t visited[N_ROOMS];
    int stack[N_ROOMS], sp = 0;
    bclear(linked, sizeof(linked)); bclear(visited, sizeof(visited));
    int s0 = rn(N_ROOMS);
    stack[sp++] = s0; visited[s0] = 1;
    while (sp) {
        int c = stack[sp - 1], cx = c % CELL_COLS, cy = c / CELL_COLS;
        int opts[4], no = 0;
        for (int k = 0; k < 4; k++) {
            int nx = cx + DX[k], ny = cy + DY[k];
            if (nx < 0 || ny < 0 || nx >= CELL_COLS || ny >= CELL_ROWS) continue;
            if (!visited[ny * CELL_COLS + nx]) opts[no++] = k;
        }
        if (!no) { sp--; continue; }
        int k = opts[rn(no)], n = (cy + DY[k]) * CELL_COLS + cx + DX[k];
        linked[c][k] = 1; linked[n][(k + 2) & 3] = 1;
        visited[n] = 1; stack[sp++] = n;
    }
    for (int e = 2 + rn(3); e > 0; e--) {
        int c = rn(N_ROOMS), k = rn(4), cx = c % CELL_COLS + DX[k], cy = c / CELL_COLS + DY[k];
        if (cx < 0 || cy < 0 || cx >= CELL_COLS || cy >= CELL_ROWS) continue;
        int n = cy * CELL_COLS + cx;
        linked[c][k] = 1; linked[n][(k + 2) & 3] = 1;
    }
    for (int c = 0; c < N_ROOMS; c++) {
        if (linked[c][1]) connect_rooms(c, c + 1, 1);
        if (linked[c][2]) connect_rooms(c, c + CELL_COLS, 0);
    }

    /* doors: corridor mouths flanked by wall; count each room's entrances */
    int entries[N_ROOMS], ex[N_ROOMS], ey[N_ROOMS];
    for (int r = 0; r < N_ROOMS; r++) {
        room_t *R = &rooms[r];
        entries[r] = 0; ex[r] = ey[r] = 0;
        if (R->junction) continue;
        for (int side = 0; side < 4; side++) {
            int len = (side & 1) ? R->h : R->w;
            for (int i = 0; i < len; i++) {
                int x, y;
                if (side == 0)      { x = R->x + i; y = R->y - 1; }
                else if (side == 2) { x = R->x + i; y = R->y + R->h; }
                else if (side == 1) { x = R->x + R->w; y = R->y + i; }
                else                { x = R->x - 1; y = R->y + i; }
                if (map[y][x] != T_FLOOR) continue;
                entries[r]++;
                int flanked = (side & 1) ? (is_wall_t(map[y - 1][x]) && is_wall_t(map[y + 1][x]))
                                         : (is_wall_t(map[y][x - 1]) && is_wall_t(map[y][x + 1]));
                if (flanked) {
                    ex[r] = x; ey[r] = y;
                    if (chance(65)) map[y][x] = T_DOOR;
                } else {
                    ex[r] = -1;
                }
            }
        }
    }

    /* start and stairs rooms */
    int order[N_ROOMS], no = 0;
    for (int r = 0; r < N_ROOMS; r++) if (!rooms[r].junction) order[no++] = r;
    for (int i = no - 1; i > 0; i--) { int j = rn(i + 1), t = order[i]; order[i] = order[j]; order[j] = t; }
    int start = order[0];
    rooms[start].kind = RK_START;
    px = rooms[start].x + rooms[start].w / 2;
    py = rooms[start].y + rooms[start].h / 2;
    calc_dmap(px, py);
    int far = -1, fd = -1;
    for (int i = 1; i < no; i++) {
        int r = order[i], d = dmap[rooms[r].y + rooms[r].h / 2][rooms[r].x + rooms[r].w / 2];
        if (d != 255 && d > fd) { fd = d; far = r; }
    }
    if (far < 0) far = order[1];
    rooms[far].kind = RK_STAIRS;
    stairs_x = rooms[far].x + rooms[far].w / 2;
    stairs_y = rooms[far].y + rooms[far].h / 2;
    int boss_floor = (depth % 5 == 0);
    boss_idx = -1;
    if (!boss_floor) {
        map[stairs_y][stairs_x] = T_STAIRS;
    } else {
        static const uint8_t BOSS_OF[4] = { M_BROOD, M_WARDEN, M_LICH, M_WYRM };
        boss_idx = new_mob(BOSS_OF[BIOME()], stairs_x, stairs_y, ST_SLEEP);
    }

    /* special rooms */
    int merchant = !boss_floor && depth >= 2 && (depth % 5 == 4 || chance(25));
    int vault_room = -1;
    for (int i = 1; i < no; i++) {
        int r = order[i];
        room_t *R = &rooms[r];
        if (R->kind != RK_PLAIN) continue;
        if (vault_room < 0 && entries[r] == 1 && ex[r] > 0 && R->w * R->h >= 9 && chance(70)) {
            R->kind = RK_VAULT; vault_room = r;
            map[ey[r]][ex[r]] = T_LOCKED;
        } else if (merchant) {
            R->kind = RK_SHOP; merchant = 0;
        } else {
            int k = rn(100);
            if (k < 22) R->kind = RK_SHRINE;
            else if (k < 40) R->kind = RK_GARDEN;
            else if (k < 56) R->kind = RK_POOL;
            else if (k < 66 && depth >= 3) R->kind = RK_DEN;
        }
    }
    /* a lock must never cut the floor in two: everything but the vault
     * (rooms, junctions, stairs) has to stay reachable from the start */
    if (vault_room >= 0) {
        calc_dmap(px, py);
        int ok = dmap[stairs_y][stairs_x] != 255;
        for (int r = 0; r < N_ROOMS && ok; r++)
            if (r != vault_room && dmap[rooms[r].y][rooms[r].x] == 255) ok = 0;
        if (!ok) {
            map[ey[vault_room]][ex[vault_room]] = T_DOOR;
            rooms[vault_room].kind = RK_PLAIN;
            vault_room = -1;
        }
    }
    for (int r = 0; r < N_ROOMS; r++) decorate_room(r);
    for (int r = 0; r < N_ROOMS; r++) fill_room_feature(r);
    map[py][px] = T_FLOOR;
    decal[py][px] = DC_NONE;

    /* the vault key lies somewhere else on the floor */
    if (vault_room >= 0) {
        int x, y;
        if (find_free(&x, &y, 4, vault_room)) add_item(x, y, IT_KEY, 0, 0);
        else map[ey[vault_room]][ex[vault_room]] = T_DOOR;
    }

    /* loose items, chests, traps */
    for (int k = 4 + rn(3); k > 0; k--) {
        int x, y;
        if (find_free(&x, &y, 2, -1)) random_item(x, y);
    }
    for (int k = 1 + (chance(35) ? 1 : 0); k > 0; k--) {
        int x, y;
        if (find_free(&x, &y, 3, start)) map[y][x] = T_CHEST;
    }
    for (int k = imin(3 + depth / 3, 9); k > 0; k--) {
        int x, y;
        if (!find_free(&x, &y, 4, start)) continue;
        int r = rn(100), t = r < 40 ? TR_DART : r < 62 ? (depth >= 2 ? TR_FIRE : TR_DART)
                                      : r < 78 ? TR_ALARM : TR_TELEPORT;
        trap[y][x] = (uint8_t)(t | TRAP_HIDDEN);
    }

    place_mobs(start);

    calc_static_light();
    update_vision();
}

/* ── Derived stats ───────────────────────────────────────────────────── */
static int p_atk(void)
{
    return p_str + (p_lvl - 1) + WEAPON_ATK[p_wpn] + p_wench + perk[PK_MIGHT] * 2 + (bless_turns ? 3 : 0);
}
static int p_defence(void)
{
    return p_def + ARMOR_DEF[p_arm] + p_aench + (p_lvl - 1) / 3 + perk[PK_GUARD];
}
static int p_crit(void) { return (pcls == CL_ROGUE ? 15 : 5) + perk[PK_CRIT] * 10; }
static int p_dodge(void) { return (pcls == CL_ROGUE ? 10 : 3) + perk[PK_DODGE] * 7; }
static int skill_max_cd(void) { return imax(2, CLASSES[pcls].cd - perk[PK_FOCUS] * 2); }
static int roll(int a) { return a - rn(a / 4 + 1); }
static int score_now(void)
{
    return p_gold + depth * 100 + kills * 5 + p_lvl * 50 + (won ? 5000 : 0);
}
static int is_hostile(int i)
{
    mob_t *m = &mobs[i];
    return m->alive && m->state != ST_PILE && m->state != ST_HIDDEN && m->type != M_MERCHANT;
}

/* ── Player damage ───────────────────────────────────────────────────── */
static void hurt_player(int dmg, const char *by)
{
    if (dead || dmg <= 0) return;
    p_hp -= dmg;
    p_flash = 3;
    shake = 3;
    float_num(px, py, dmg, C_RED, "-");
    burst(px, py, 3, g_mono ? 0xFFFF : c565(200, 30, 40), 6, 1);
    if (p_hp <= 0) {
        p_hp = 0;
        dead = 1;
        killer = by;
        msg("You die...", C_RED);
    }
}

static void heal_player(int amt)
{
    int before = p_hp;
    p_hp = imin(p_mhp, p_hp + amt);
    if (p_hp > before) {
        float_num(px, py, p_hp - before, C_GREEN, "+");
        burst(px, py, 4, C_GREEN, 3, 0);
    }
}

static void gain_xp(int xp)
{
    p_xp += xp;
    while (p_xp >= p_xpn) {
        p_xp -= p_xpn;
        p_lvl++;
        p_xpn = 20 + p_lvl * 7;
        p_mhp += 4;
        p_hp = imin(p_mhp, p_hp + p_mhp / 3);
        pending_perks++;
        levelup_fx = 10;
        spawn_float(px, py, "LEVEL UP", C_YELLOW);
        msg("Level up! Choose a perk.", C_YELLOW);
    }
}

static void wake_around(int x, int y, int r)
{
    for (int i = 0; i < MAX_MOBS; i++) {
        mob_t *m = &mobs[i];
        if (!m->alive || m->state != ST_SLEEP) continue;
        if (iabs(m->x - x) + iabs(m->y - y) <= r) {
            m->state = ST_HUNT;
            m->tx = (int8_t)px; m->ty = (int8_t)py;
        }
    }
}

/* ── Monster death ───────────────────────────────────────────────────── */
static void place_drop(int x, int y, int type, int sub, int amt)
{
    if (item_at(x, y) < 0) { add_item(x, y, type, sub, amt); return; }
    for (int k = 0; k < 4; k++) {
        int nx = x + DX[k], ny = y + DY[k];
        if (in_map(nx, ny) && !blocks_move(map[ny][nx]) && map[ny][nx] != T_STAIRS && item_at(nx, ny) < 0) {
            add_item(nx, ny, type, sub, amt);
            return;
        }
    }
    add_item(x, y, type, sub, amt);
}

static void spawn_near(int type, int x, int y, int state)
{
    int s = rn(4);
    for (int r = 1; r <= 2; r++)
        for (int k = 0; k < 4; k++) {
            int nx = x + DX[(s + k) & 3] * r, ny = y + DY[(s + k) & 3] * r;
            if (!in_map(nx, ny) || blocks_move(map[ny][nx]) || occupied(nx, ny)) continue;
            int i = new_mob(type, nx, ny, state);
            if (i >= 0) { mobs[i].tx = (int8_t)px; mobs[i].ty = (int8_t)py; burst(nx, ny, 5, C_VIOLET, 4, 0); }
            return;
        }
}

static void kill_mob(int i)
{
    mob_t *m = &mobs[i];
    const mobdef_t *d = &MOBS[m->type];
    int x = m->x, y = m->y;

    if (m->type == M_SKELETON && !m->revived && chance(60)) {
        m->state = ST_PILE;
        m->cd = 4;
        m->hp = (int16_t)(m->mhp / 2);
        msg("The skeleton collapses... for now.", C_DIM);
        burst(x, y, 6, pal565['v'], 4, 1);
        return;
    }
    m->alive = 0;
    kills++;
    gain_xp(d->xp + depth / 2);
    if (perk[PK_VAMP]) heal_player(perk[PK_VAMP] * 2);

    uint16_t bc = (d->flags & MF_UNDEAD) ? pal565['v'] : (m->type == M_SLIME || m->type == M_SLIMELET)
                                                          ? pal565['l'] : c565(200, 30, 40);
    burst(x, y, 10, g_mono ? 0xFFFF : bc, 7, 1);
    if (decal[y][x] == DC_NONE && map[y][x] != T_WATER)
        decal[y][x] = (d->flags & MF_UNDEAD) ? DC_BONES
                    : (m->type == M_SLIME || m->type == M_SLIMELET) ? DC_GOO
                    : (m->type == M_GHOST) ? DC_NONE : DC_BLOOD;

    if (m->type == M_SLIME) {
        mstart("The slime splits!"); msg(mb, C_GREEN);
        spawn_near(M_SLIMELET, x, y, ST_HUNT);
        spawn_near(M_SLIMELET, x, y, ST_HUNT);
        return;
    }
    if (d->flags & MF_BOSS) {
        flash_white = 4; shake = 8;
        burst(x, y, 24, C_YELLOW, 10, 1);
        mstart(d->name); g_cat(mb, " is slain!"); msg(mb, C_YELLOW);
        if (depth >= LAST_DEPTH) {
            add_item(x, y, IT_CRYSTAL, 0, 0);
        } else {
            map[stairs_y][stairs_x] = T_STAIRS;
            place_drop(x, y, chance(50) ? IT_WEAPON : IT_ARMOR, iclamp(gear_tier() + 1, 1, N_WEAPONS - 1), 0);
            place_drop(x, y, IT_GOLD, 0, 40 + depth * 8);
            msg("The way down lies open.", C_CYAN);
        }
        boss_idx = -1;
        bclear(warn, sizeof(warn));
        return;
    }
    if (m->type == M_MIMIC) {
        place_drop(x, y, chance(50) ? IT_WEAPON : IT_ARMOR, gear_tier(), 0);
        place_drop(x, y, IT_GOLD, 0, 15 + depth * 4);
        return;
    }
    int r = rn(100);
    if (r < 12) place_drop(x, y, IT_GOLD, 0, 3 + rn(5) + depth);
    else if (r < 18) place_drop(x, y, IT_HPOT, 0, 0);
    else if (r < 23) place_drop(x, y, IT_SCROLL, rn(N_SCROLL), 0);
    else if (r < 27) place_drop(x, y, IT_FLASK, rn(N_FLASK), 0);
}

/* Deal damage to a monster.  Returns 1 if it died. */
static int damage_mob(int i, int dmg, uint16_t col)
{
    mob_t *m = &mobs[i];
    if (m->type == M_MERCHANT) return 0;
    if (dmg < 1) dmg = 1;
    m->hp -= (int16_t)dmg;
    m->flash = 2;
    if (m->state == ST_SLEEP || m->state == ST_WANDER || m->state == ST_HIDDEN) {
        m->state = ST_HUNT;
        m->tx = (int8_t)px; m->ty = (int8_t)py;
    }
    float_num(m->x, m->y, dmg, col, "");
    if (m->hp <= 0) { kill_mob(i); return 1; }
    return 0;
}

static void player_attack(int i)
{
    mob_t *m = &mobs[i];
    const mobdef_t *d = &MOBS[m->type];
    p_lunge = 2; p_ldx = (int8_t)(m->x - px); p_ldy = (int8_t)(m->y - py);
    int sneak = (m->state == ST_SLEEP || m->frozen);
    if (m->state == ST_HIDDEN) { m->state = ST_HUNT; msg("It was a MIMIC!", C_RED); }
    if (!sneak && !invis_turns && chance(5 + d->eva)) {
        spawn_float(m->x, m->y, "miss", C_DIM);
        m->state = ST_HUNT;
        return;
    }
    int dmg = roll(p_atk()) - m->def;
    int mult = 1;
    if (invis_turns) { mult = 3; invis_turns = 0; msg("You strike from the shadows!", C_VIOLET); }
    else if (sneak) mult = (pcls == CL_ROGUE) ? 3 : 2;
    else if (chance(p_crit())) mult = 2;
    dmg = imax(1, dmg) * mult;
    if (mult > 1) { shake = 2; burst(m->x, m->y, 6, C_YELLOW, 6, 0); }
    wake_around(px, py, 3);
    if (damage_mob(i, dmg, mult > 1 ? C_YELLOW : 0xFFFF)) {
        if (!mobs[i].alive) { mstart("You slay the "); g_cat(mb, d->name); g_cat(mb, "."); msg(mb, C_TEXT); }
    } else if (mult > 1) {
        msg(mult == 3 ? "Ambush! Triple damage." : "Critical hit!", C_YELLOW);
    }
}

/* ── Monster attacks ─────────────────────────────────────────────────── */
static void mob_hits_player(int i, int dmg_mult_pct, int ranged)
{
    mob_t *m = &mobs[i];
    const mobdef_t *d = &MOBS[m->type];
    if (!ranged) { m->lunge = 2; m->ldx = (int8_t)(px - m->x); m->ldy = (int8_t)(py - m->y); }
    if (chance(p_dodge())) {
        spawn_float(px, py, "dodge", C_CYAN);
        return;
    }
    int def = (m->type == M_GHOST) ? 0 : p_defence();
    int dmg = imax(1, roll(m->atk) * dmg_mult_pct / 100 - (def + 1) / 2);
    hurt_player(dmg, d->name);
    if (dead) return;
    if (!ranged && perk[PK_THORNS]) damage_mob(i, perk[PK_THORNS] * 3, C_ORANGE);
    if (!m->alive) return;
    switch (m->type) {
    case M_SPIDER: case M_BROOD:
        if (chance(30)) { root_turns = 2; msg("You are caught in a web!", C_TEXT); }
        break;
    case M_IMP: case M_WYRM:
        if (chance(35)) { burn_turns = 3; msg("You catch fire!", C_ORANGE); }
        break;
    case M_MIMIC: case M_RAT:
        if (chance(m->type == M_RAT ? 10 : 40)) { poison_turns = 5; msg("The bite festers. Poisoned!", C_GREEN); }
        break;
    case M_GHOST:
        if (p_xp > 0 && chance(30)) { p_xp = imax(0, p_xp - 3); msg("The ghost drains your memories.", C_CYAN); }
        break;
    default: break;
    }
}

/* ── Monster movement ────────────────────────────────────────────────── */
static int mob_can_enter(int i, int x, int y)
{
    if (!in_map(x, y) || x == 0 || y == 0 || x == MAP_W - 1 || y == MAP_H - 1) return 0;
    int t = map[y][x];
    if (MOBS[mobs[i].type].flags & MF_PHASE) { if (t == T_LAVA) return 0; }
    else if (blocks_move(t)) return 0;
    if (x == px && y == py) return 0;
    return mob_at(x, y) < 0;
}
static void mob_move(int i, int x, int y)
{
    mobs[i].x = (int8_t)x; mobs[i].y = (int8_t)y;
    if (map[y][x] == T_GRASS) map[y][x] = T_GRASS_CUT;
}

/* Downhill on the player distance map (uphill when fleeing). */
static int mob_step_dmap(int i, int flee)
{
    mob_t *m = &mobs[i];
    int cur = dmap[m->y][m->x], best = -1, bv = flee ? cur : (cur == 255 ? 999 : cur);
    int s = rn(4);
    for (int j = 0; j < 4; j++) {
        int k = (s + j) & 3, nx = m->x + DX[k], ny = m->y + DY[k];
        if (!mob_can_enter(i, nx, ny)) continue;
        int v = dmap[ny][nx];
        if (v == 255) continue;
        if (flee ? v > bv : v < bv) { bv = v; best = k; }
    }
    if (best < 0) return 0;
    mob_move(i, m->x + DX[best], m->y + DY[best]);
    return 1;
}
static int mob_step_greedy(int i, int tx, int ty)
{
    mob_t *m = &mobs[i];
    int dx = tx - m->x, dy = ty - m->y;
    int ax = dx > 0 ? 1 : dx < 0 ? -1 : 0, ay = dy > 0 ? 1 : dy < 0 ? -1 : 0;
    if (iabs(dx) >= iabs(dy)) {
        if (ax && mob_can_enter(i, m->x + ax, m->y)) { mob_move(i, m->x + ax, m->y); return 1; }
        if (ay && mob_can_enter(i, m->x, m->y + ay)) { mob_move(i, m->x, m->y + ay); return 1; }
    } else {
        if (ay && mob_can_enter(i, m->x, m->y + ay)) { mob_move(i, m->x, m->y + ay); return 1; }
        if (ax && mob_can_enter(i, m->x + ax, m->y)) { mob_move(i, m->x + ax, m->y); return 1; }
    }
    return 0;
}
static int mob_step_random(int i)
{
    int k = rn(4);
    mob_t *m = &mobs[i];
    if (mob_can_enter(i, m->x + DX[k], m->y + DY[k])) { mob_move(i, m->x + DX[k], m->y + DY[k]); return 1; }
    return 0;
}
static int mob_approach(int i)
{
    if (MOBS[mobs[i].type].flags & MF_PHASE) return mob_step_greedy(i, px, py);
    return mob_step_dmap(i, 0) || mob_step_greedy(i, px, py);
}

static int mob_sees_player(int i)
{
    mob_t *m = &mobs[i];
    if (invis_turns) return 0;
    int d = iabs(m->x - px) + iabs(m->y - py);
    if (d > 10) return 0;
    return d <= 1 || los_clear(m->x, m->y, px, py);
}

static int ally_to_heal(int i)
{
    int best = -1, worst = 0;
    for (int j = 0; j < MAX_MOBS; j++) {
        if (j == i || !is_hostile(j)) continue;
        if (iabs(mobs[j].x - mobs[i].x) + iabs(mobs[j].y - mobs[i].y) > 4) continue;
        int deficit = mobs[j].mhp - mobs[j].hp;
        if (deficit > worst) { worst = deficit; best = j; }
    }
    return best;
}

static int count_type(int type)
{
    int n = 0;
    for (int j = 0; j < MAX_MOBS; j++) if (mobs[j].alive && mobs[j].type == type) n++;
    return n;
}

/* Mark the telegraph line for a charge/breath from (x,y) along dir k. */
static int mark_line(int x, int y, int k, int len, int cone)
{
    int n = 0;
    for (int s = 1; s <= len; s++) {
        int lx = x + DX[k] * s, ly = y + DY[k] * s;
        if (!in_map(lx, ly) || blocks_move(map[ly][lx])) break;
        warn[ly][lx] = 1; n++;
        if (cone && s >= 3) {
            int sx = DY[k] != 0, sy = DX[k] != 0;
            if (in_map(lx + sx, ly + sy) && !blocks_move(map[ly + sy][lx + sx])) warn[ly + sy][lx + sx] = 1;
            if (in_map(lx - sx, ly - sy) && !blocks_move(map[ly - sy][lx - sx])) warn[ly - sy][lx - sx] = 1;
        }
    }
    return n;
}
static int dir_to_player(int x, int y)
{
    int dx = px - x, dy = py - y;
    if (iabs(dx) >= iabs(dy)) return dx > 0 ? 1 : 3;
    return dy > 0 ? 2 : 0;
}

static void boss_act(int i, int dist, int sees)
{
    mob_t *m = &mobs[i];
    const mobdef_t *d = &MOBS[m->type];
    switch (m->type) {
    case M_BROOD:
        if (!m->cd && count_type(M_SPIDER) < 4) {
            spawn_near(M_SPIDER, m->x, m->y, ST_HUNT);
            m->cd = 5;
            msg("Spiderlings pour out of her!", C_TEXT);
            return;
        }
        if (dist <= 1) { mob_hits_player(i, 100, 0); return; }
        if (sees && dist <= 5 && !m->cd2) {
            spawn_proj(m->x, m->y, px, py, PJ_WEB);
            root_turns = 2; m->cd2 = 5;
            msg("Sticky webbing pins you down!", C_TEXT);
            return;
        }
        mob_approach(i);
        return;

    case M_WARDEN:
        if (m->prep) {
            int k = m->prep - 1, hit = 0;
            m->prep = 0;
            bclear(warn, sizeof(warn));
            for (int s = 0; s < 7; s++) {
                int nx = m->x + DX[k], ny = m->y + DY[k];
                if (nx == px && ny == py) { hit = 1; break; }
                if (!mob_can_enter(i, nx, ny)) break;
                mob_move(i, nx, ny);
                burst(m->x, m->y, 2, C_DIM, 3, 0);
            }
            if (hit) { mob_hits_player(i, 200, 0); msg("The Warden's charge slams into you!", C_RED); }
            else msg("The Warden charges past!", C_DIM);
            shake = 4;
            return;
        }
        if (dist <= 1) { mob_hits_player(i, 100, 0); return; }
        if (sees && !m->cd && (m->x == px || m->y == py) && dist <= 7) {
            int k = dir_to_player(m->x, m->y);
            if (mark_line(m->x, m->y, k, 7, 0) >= dist - 1) {
                m->prep = (uint8_t)(k + 1); m->cd = 4;
                msg("The Warden readies a charge!", C_ORANGE);
                return;
            }
            bclear(warn, sizeof(warn));
        }
        mob_approach(i);
        return;

    case M_LICH:
        if (dist <= 2 && !m->cd2) {
            for (int t = 0; t < 30; t++) {
                int x = m->x + rn(13) - 6, y = m->y + rn(13) - 6;
                if (!in_map(x, y) || !tile_free(x, y)) continue;
                if (iabs(x - px) + iabs(y - py) < 4) continue;
                burst(m->x, m->y, 8, C_VIOLET, 5, 0);
                mob_move(i, x, y);
                burst(x, y, 8, C_VIOLET, 5, 0);
                m->cd2 = 5;
                msg("The Lich blinks away!", C_VIOLET);
                return;
            }
        }
        if (!m->cd && count_type(M_SKELETON) < 4) {
            spawn_near(M_SKELETON, m->x, m->y, ST_HUNT);
            spawn_near(M_SKELETON, m->x, m->y, ST_HUNT);
            m->cd = 7;
            msg("The Lich raises the dead!", C_VIOLET);
            return;
        }
        if (sees && dist <= 7) {
            spawn_proj(m->x, m->y, px, py, PJ_FROST);
            mob_hits_player(i, 80, 1);
            return;
        }
        mob_approach(i);
        return;

    case M_WYRM:
        if (m->prep) {
            int k = m->prep - 1;
            m->prep = 0;
            for (int y = 0; y < MAP_H; y++)
                for (int x = 0; x < MAP_W; x++) {
                    if (!warn[y][x]) continue;
                    warn[y][x] = 0;
                    if (map[y][x] != T_WATER) fire[y][x] = 3;
                    burst(x, y, 2, C_ORANGE, 4, 0);
                    int j = mob_at(x, y);
                    if (j >= 0 && j != i) damage_mob(j, m->atk, C_ORANGE);
                    if (x == px && y == py) {
                        hurt_player(imax(1, roll(m->atk) * 3 / 2 - p_defence() / 2), d->name);
                        burn_turns = 3;
                    }
                }
            (void)k;
            msg("A torrent of flame!", C_ORANGE);
            flash_white = 2; shake = 5;
            return;
        }
        if (!m->enraged && m->hp * 2 < m->mhp) {
            m->enraged = 1;
            spawn_near(M_IMP, m->x, m->y, ST_HUNT);
            spawn_near(M_IMP, m->x, m->y, ST_HUNT);
            msg("The Wyrm roars! Imps answer.", C_ORANGE);
            return;
        }
        if (dist <= 1 && chance(60)) { mob_hits_player(i, 100, 0); return; }
        if (sees && !m->cd && dist <= 7 && (iabs(m->x - px) <= 1 || iabs(m->y - py) <= 1)) {
            int k = dir_to_player(m->x, m->y);
            mark_line(m->x, m->y, k, 7, 1);
            m->prep = (uint8_t)(k + 1); m->cd = 4;
            msg("The Wyrm draws a deep breath...", C_ORANGE);
            return;
        }
        if (dist <= 1) { mob_hits_player(i, 100, 0); return; }
        mob_approach(i);
        return;
    }
}

static void mob_act(int i)
{
    mob_t *m = &mobs[i];
    const mobdef_t *d = &MOBS[m->type];
    int dist = iabs(m->x - px) + iabs(m->y - py);

    if (m->state == ST_PILE) {
        if (m->cd) m->cd--;
        if (!m->cd) {
            if (occupied(m->x, m->y)) { m->cd = 1; return; }
            m->state = ST_HUNT; m->revived = 1;
            if (vis[m->y][m->x]) { msg("The bones knit back together!", C_DIM); burst(m->x, m->y, 6, pal565['v'], 3, 0); }
        }
        return;
    }
    if (m->type == M_MERCHANT) return;
    if (m->frozen) { m->frozen--; return; }
    if (m->burn) {
        m->burn--;
        if (damage_mob(i, 2 + depth / 4, C_ORANGE)) return;
    }
    if (m->cd) m->cd--;
    if (m->cd2) m->cd2--;

    if (m->state == ST_HIDDEN) {
        if (dist == 1 && !invis_turns) {
            m->state = ST_HUNT;
            msg("The chest was a MIMIC!", C_RED);
            mob_hits_player(i, 100, 0);
        }
        return;
    }
    int sees = mob_sees_player(i);
    if (m->state == ST_SLEEP) {
        if (!sees) return;
        int ch = 8 + (10 - dist) * 6;
        if (d->flags & MF_BOSS) ch = dist <= 5 ? 100 : 30;
        if (pcls == CL_ROGUE) ch /= 2;
        if (dist <= 1) ch = 70;
        if (chance(ch)) {
            m->state = ST_HUNT;
            m->tx = (int8_t)px; m->ty = (int8_t)py;
            spawn_float(m->x, m->y, "!", C_RED);
            if (d->flags & MF_BOSS) { mstart(d->name); g_cat(mb, " awakens!"); msg(mb, C_RED); shake = 4; }
        }
        return;
    }
    if (sees) { m->state = ST_HUNT; m->tx = (int8_t)px; m->ty = (int8_t)py; }
    else if (m->state == ST_HUNT) {
        if (m->x == m->tx && m->y == m->ty) m->state = ST_WANDER;
        else if (!mob_step_greedy(i, m->tx, m->ty)) m->state = ST_WANDER;
        return;
    }
    if (m->state == ST_WANDER) {
        if (chance(30)) mob_step_random(i);
        return;
    }
    if (d->flags & MF_SLOW) { if (m->cd2) return; m->cd2 = 1; }
    if (d->flags & MF_BOSS) { boss_act(i, dist, sees); return; }

    switch (m->type) {
    case M_ARCHER:
        if (dist <= 1) { if (!mob_step_dmap(i, 1)) mob_hits_player(i, 70, 0); return; }
        if (dist <= 6 && sees && !m->cd) {
            spawn_proj(m->x, m->y, px, py, PJ_ARROW);
            mob_hits_player(i, 100, 1);
            m->cd = 1;
            return;
        }
        if (dist <= 3) { mob_step_dmap(i, 1); return; }
        mob_approach(i);
        return;
    case M_SHAMAN: {
        int a = m->cd ? -1 : ally_to_heal(i);
        if (a >= 0) {
            int amt = 4 + depth / 2;
            mobs[a].hp = (int16_t)imin(mobs[a].mhp, mobs[a].hp + amt);
            float_num(mobs[a].x, mobs[a].y, amt, C_GREEN, "+");
            burst(mobs[a].x, mobs[a].y, 5, C_GREEN, 3, 0);
            m->cd = 3;
            return;
        }
        if (dist <= 2) { mob_step_dmap(i, 1); return; }
        if (dist <= 5 && !m->cd2) {
            spawn_proj(m->x, m->y, px, py, PJ_ZAP);
            mob_hits_player(i, 80, 1);
            m->cd2 = 2;
            return;
        }
        mob_approach(i);
        return;
    }
    case M_NECRO:
        if (dist <= 2) { if (!mob_step_dmap(i, 1) && dist == 1) mob_hits_player(i, 100, 0); return; }
        if (!m->cd && m->summons < 3 && dist <= 7) {
            spawn_near(M_SKELETON, m->x, m->y, ST_HUNT);
            m->summons++; m->cd = 6;
            msg("The necromancer raises a skeleton!", C_VIOLET);
            return;
        }
        if (dist <= 6 && !m->cd2) {
            spawn_proj(m->x, m->y, px, py, PJ_DARK);
            mob_hits_player(i, 100, 1);
            m->cd2 = 3;
            return;
        }
        mob_approach(i);
        return;
    case M_BAT:
        if (chance(45)) { mob_step_random(i); return; }
        break;
    default: break;
    }
    if (dist <= 1) { mob_hits_player(i, 100, 0); return; }
    mob_approach(i);
    if ((d->flags & MF_FAST) && m->alive) {
        dist = iabs(m->x - px) + iabs(m->y - py);
        if (dist > 1) mob_approach(i);
    }
}

static void mobs_turn(void)
{
    calc_dmap(px, py);
    for (int i = 0; i < MAX_MOBS && !dead; i++)
        if (mobs[i].alive) mob_act(i);
}

/* ── Fire & environment ──────────────────────────────────────────────── */
static int flammable(int t) { return t == T_GRASS || t == T_GRASS_CUT; }

static void ignite(int x, int y, int turns)
{
    if (!in_map(x, y) || is_wall_t(map[y][x]) || map[y][x] == T_WATER || map[y][x] == T_LAVA) return;
    if (fire[y][x] < turns) fire[y][x] = (uint8_t)turns;
}

static void env_turn(void)
{
    static int16_t spread[64];
    int ns = 0;
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) {
            if (!fire[y][x]) continue;
            if (fire[y][x] >= 2 && flammable(map[y][x]))
                for (int k = 0; k < 4 && ns < 64; k++) {
                    int nx = x + DX[k], ny = y + DY[k];
                    if (in_map(nx, ny) && flammable(map[ny][nx]) && !fire[ny][nx] && chance(50))
                        spread[ns++] = (int16_t)(ny * MAP_W + nx);
                }
            if (--fire[y][x] == 0 && flammable(map[y][x])) {
                map[y][x] = T_FLOOR;
                decal[y][x] = DC_SCORCH;
            }
            int j = mob_at(x, y);
            if (j >= 0 && mobs[j].type != M_IMP && mobs[j].type != M_WYRM) mobs[j].burn = 3;
            if (x == px && y == py) burn_turns = imax(burn_turns, 2);
        }
    for (int s = 0; s < ns; s++) ignite(spread[s] % MAP_W, spread[s] / MAP_W, 4);
}

/* Blast centred on (cx,cy): hurts everything within radius r (the player
 * too, unless spare_player), lights grass, scorches the floor. */
static void explode(int cx, int cy, int r, int dmg, int spare_player, const char *by)
{
    for (int y = cy - r; y <= cy + r; y++)
        for (int x = cx - r; x <= cx + r; x++) {
            if (!in_map(x, y) || is_wall_t(map[y][x])) continue;
            if (!(x == cx && y == cy) && !los_clear(cx, cy, x, y)) continue;
            burst(x, y, 3, chance(50) ? C_ORANGE : C_YELLOW, 6, 0);
            if (flammable(map[y][x])) ignite(x, y, 4);
            else if (map[y][x] == T_FLOOR && decal[y][x] == DC_NONE && chance(60)) decal[y][x] = DC_SCORCH;
            int j = mob_at(x, y);
            if (j >= 0) damage_mob(j, dmg, C_ORANGE);
            if (x == px && y == py && !spare_player) hurt_player(dmg / 2, by);
        }
    shake = 6;
    flash_white = 2;
    wake_around(cx, cy, 8);
}

/* ── Player actions ──────────────────────────────────────────────────── */
static void spring_trap(void)
{
    int t = trap[py][px] & 7;
    if (!t) return;
    int hidden = trap[py][px] & TRAP_HIDDEN;
    trap[py][px] = (uint8_t)t;
    switch (t) {
    case TR_DART:
        msg(hidden ? "A hidden dart trap!" : "You trigger the dart trap.", C_RED);
        hurt_player(2 + depth / 2 + rn(3), "a dart trap");
        if (!dead && chance(50)) { poison_turns = 5; msg("The dart was poisoned!", C_GREEN); }
        break;
    case TR_FIRE:
        msg("Flames burst from the floor!", C_ORANGE);
        for (int k = 0; k < 4; k++) ignite(px + DX[k], py + DY[k], 3);
        ignite(px, py, 3);
        burn_turns = 3;
        hurt_player(2 + depth / 3, "a fire trap");
        break;
    case TR_ALARM:
        msg("A shrill alarm wakes the halls!", C_YELLOW);
        for (int i = 0; i < MAX_MOBS; i++)
            if (mobs[i].alive && mobs[i].state == ST_SLEEP) {
                mobs[i].state = ST_HUNT;
                mobs[i].tx = (int8_t)px; mobs[i].ty = (int8_t)py;
            }
        break;
    case TR_TELEPORT: {
        int x, y;
        if (find_free(&x, &y, 8, -1)) { burst(px, py, 8, C_VIOLET, 5, 0); px = x; py = y; }
        msg("A teleport rune flings you away!", C_VIOLET);
        break;
    }
    }
}

static void equip_found(int type, int tier)
{
    if (type == IT_WEAPON) {
        if (WEAPON_ATK[tier] > WEAPON_ATK[p_wpn] + p_wench) {
            p_wpn = tier; p_wench = 0;
            mstart("You wield a "); g_cat(mb, WEAPON_NAME[tier]); g_cat(mb, "!"); msg(mb, C_YELLOW);
        } else {
            p_gold += 6 + depth * 2;
            mstart("Sold a spare "); g_cat(mb, WEAPON_NAME[tier]); g_cat(mb, "."); msg(mb, C_GOLD);
        }
    } else {
        if (ARMOR_DEF[tier] > ARMOR_DEF[p_arm] + p_aench) {
            p_arm = tier; p_aench = 0;
            mstart("You don "); g_cat(mb, ARMOR_NAME[tier]); g_cat(mb, "!"); msg(mb, C_YELLOW);
        } else {
            p_gold += 6 + depth * 2;
            mstart("Sold spare "); g_cat(mb, ARMOR_NAME[tier]); g_cat(mb, "."); msg(mb, C_GOLD);
        }
    }
}

static const char *flask_label(int k)
{
    static char b[24];
    b[0] = '\0';
    if (flask_known[k]) { g_cat(b, "Flask: "); g_cat(b, FLASK_NAME[k]); }
    else { g_cat(b, FLASK_COLOR[flask_col[k]]); g_cat(b, " Flask"); }
    return b;
}

static void pickup(void)
{
    int i;
    while ((i = item_at(px, py)) >= 0) {
        item_t *it = &items[i];
        switch (it->type) {
        case IT_GOLD: {
            int g = it->amt * (2 + perk[PK_GREED]) / 2;
            if (pcls == CL_ROGUE) g += g / 4;
            p_gold += g;
            float_num(px, py, g, C_GOLD, "+");
            mstart("You pick up "); g_cat_int(mb, g); g_cat(mb, " gold."); msg(mb, C_GOLD);
            break;
        }
        case IT_HPOT: n_hpot++; msg("A health potion. (B to drink)", C_RED); break;
        case IT_FLASK:
            n_flask[it->sub]++;
            mstart("You find a "); g_cat(mb, flask_label(it->sub)); g_cat(mb, "."); msg(mb, C_VIOLET);
            break;
        case IT_SCROLL:
            n_scroll[it->sub]++;
            mstart("Scroll of "); g_cat(mb, SCROLL_NAME[it->sub]); g_cat(mb, "."); msg(mb, C_CYAN);
            break;
        case IT_BOMB: n_bomb++; msg("A bomb! Throw it from the bag.", C_TEXT); break;
        case IT_OIL: n_oil++; msg("A flask of lamp oil.", C_ORANGE); break;
        case IT_KEY: p_keys++; msg("A heavy key. A vault lies near.", C_YELLOW); break;
        case IT_WEAPON: case IT_ARMOR: equip_found(it->type, it->sub); break;
        case IT_CRYSTAL:
            won = 1;
            msg("The Akira Crystal is yours!", C_CYAN);
            break;
        }
        burst(px, py, 6, C_YELLOW, 4, 0);
        it->type = IT_NONE;
    }
}

static void open_chest(void)
{
    map[py][px] = T_CHEST_OPEN;
    burst(px, py, 10, C_GOLD, 6, 1);
    int r = rn(100);
    if (r < 30) {
        int g = (20 + rn(20) + depth * 5) * (2 + perk[PK_GREED]) / 2;
        p_gold += g; float_num(px, py, g, C_GOLD, "+");
        mstart("The chest holds "); g_cat_int(mb, g); g_cat(mb, " gold!"); msg(mb, C_GOLD);
    } else if (r < 50) {
        n_hpot += 2; msg("Two health potions inside!", C_RED);
    } else if (r < 65) {
        int k = rn(N_SCROLL); n_scroll[k] += 1; n_bomb++;
        mstart("A bomb and a scroll of "); g_cat(mb, SCROLL_NAME[k]); msg(mb, C_CYAN);
    } else if (r < 83) {
        equip_found(IT_WEAPON, gear_tier());
    } else {
        equip_found(IT_ARMOR, gear_tier());
    }
}

static void use_shrine(void)
{
    map[py][px] = T_SHRINE_USED;
    calc_static_light();
    levelup_fx = 8;
    burst(px, py, 14, C_CYAN, 6, 0);
    switch (rn(6)) {
    case 0: p_hp = p_mhp; poison_turns = burn_turns = 0; msg("Warm light heals and cleanses you.", C_GREEN); break;
    case 1: bless_turns = 40; msg("Blessed! +3 attack for a while.", C_YELLOW); break;
    case 2: p_torch = imin(7, p_torch + 1); p_fuel = TORCH_MAX; msg("Your torch blazes brighter.", C_ORANGE); break;
    case 3: { int k = rn(N_SCROLL); n_scroll[k]++; mstart("A scroll of "); g_cat(mb, SCROLL_NAME[k]); g_cat(mb, " appears."); msg(mb, C_CYAN); break; }
    case 4: for (int k = 0; k < N_FLASK; k++) flask_known[k] = 1; msg("Visions reveal every flask.", C_VIOLET); break;
    default: { int g = 30 + depth * 6; p_gold += g; float_num(px, py, g, C_GOLD, "+"); msg("Coins rain from the shrine!", C_GOLD); break; }
    }
}

static void use_altar(void)
{
    int cost = p_mhp / 4;
    if (p_hp <= cost + 1) { msg("You are too weak to offer blood.", C_DIM); return; }
    p_hp -= cost;
    float_num(px, py, cost, C_RED, "-");
    burst(px, py, 12, c565(200, 20, 30), 6, 1);
    map[py][px] = T_ALTAR_USED;
    calc_static_light();
    pending_perks++;
    msg("The altar drinks. Power flows in!", C_RED);
}

static int fire_line(int k, int range, int *ox, int *oy)
{
    int x = px, y = py;
    for (int s = 0; s < range; s++) {
        int nx = x + DX[k], ny = y + DY[k];
        if (!in_map(nx, ny) || blocks_move(map[ny][nx]) || (map[ny][nx] == T_DOOR && !occupied(nx, ny))) break;
        x = nx; y = ny;
        if (mob_at(x, y) >= 0) break;
    }
    *ox = x; *oy = y;
    return !(x == px && y == py);
}

static void throw_bomb(int k)
{
    int x, y;
    fire_line(k, 5, &x, &y);
    spawn_proj(px, py, x, y, PJ_BOMB);
    n_bomb--;
    msg("BOOM!", C_ORANGE);
    explode(x, y, 1, 10 + depth * 2, 0, "your own bomb");
}

static void arcane_bolt(int k)
{
    int x, y;
    fire_line(k, 8, &x, &y);
    spawn_proj(px, py, x, y, PJ_BOLT);
    int j = mob_at(x, y);
    if (j >= 0 && mobs[j].type == M_MERCHANT) {
        msg("The merchant ducks your bolt!", C_DIM);
    } else if (j >= 0) {
        int dmg = 5 + p_lvl * 2 + perk[PK_MIGHT] - mobs[j].def / 2;
        burst(x, y, 8, C_CYAN, 6, 0);
        mstart("Arcane bolt hits the "); g_cat(mb, MOBS[mobs[j].type].name); g_cat(mb, "!");
        if (!damage_mob(j, dmg, C_CYAN)) msg(mb, C_CYAN);
        else { mstart("Your bolt slays the "); g_cat(mb, MOBS[mobs[j].type].name); g_cat(mb, "."); msg(mb, C_CYAN); }
    } else {
        burst(x, y, 5, C_CYAN, 4, 0);
        msg("The bolt fizzles against stone.", C_DIM);
    }
    wake_around(x, y, 4);
}

static int skill_cleave(void)
{
    int hits = 0;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            if (!dx && !dy) continue;
            int j = mob_at(px + dx, py + dy);
            burst(px + dx, py + dy, 2, C_TEXT, 5, 0);
            if (j < 0 || mobs[j].type == M_MERCHANT) continue;
            if (mobs[j].state == ST_HIDDEN) mobs[j].state = ST_HUNT;
            int dmg = imax(1, roll(p_atk()) - mobs[j].def);
            if (chance(p_crit())) dmg *= 2;
            damage_mob(j, dmg, 0xFFFF);
            hits++;
        }
    if (!hits) { msg("No foes within reach.", C_DIM); return 0; }
    shake = 3;
    msg("You cleave through your foes!", C_YELLOW);
    return 1;
}

static void teleport_player(void)
{
    int x, y;
    if (find_free(&x, &y, 10, -1)) {
        burst(px, py, 10, C_VIOLET, 6, 0);
        px = x; py = y;
        burst(px, py, 10, C_VIOLET, 6, 0);
    }
}

static void drink_flask(int k)
{
    n_flask[k]--;
    int was_known = flask_known[k];
    flask_known[k] = 1;
    switch (k) {
    case FL_STRENGTH: p_str++; msg("Strength! +1 attack for good.", C_YELLOW); levelup_fx = 6; break;
    case FL_VIGOR: p_mhp += 5; heal_player(15); msg("Vigor! +5 max HP.", C_GREEN); break;
    case FL_SIGHT: sight_on = 1; msg("Insight! You sense every foe.", C_VIOLET); break;
    case FL_HASTE: haste_turns = 14; haste_flip = 0; msg("Haste! The world slows down.", C_YELLOW); break;
    case FL_TOXIN: poison_turns = 7; msg("Toxin! You are poisoned.", C_GREEN); break;
    default:
        msg("Liquid fire erupts around you!", C_ORANGE);
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                ignite(px + dx, py + dy, 3);
                int j = mob_at(px + dx, py + dy);
                if (j >= 0) { damage_mob(j, 6 + depth, C_ORANGE); if (mobs[j].alive) mobs[j].burn = 3; }
            }
        burn_turns = 3;
        break;
    }
    if (!was_known) { mstart("(It was a flask of "); g_cat(mb, FLASK_NAME[k]); g_cat(mb, ")"); msg(mb, C_DIM); }
}

static int choose_enchant(void);   /* UI: 0 weapon, 1 armour, -1 cancel */

static int read_scroll(int k)
{
    switch (k) {
    case SC_TELEPORT: teleport_player(); msg("Space folds around you.", C_VIOLET); break;
    case SC_FIREBALL:
        msg("A fireball engulfs the room!", C_ORANGE);
        explode(px, py, 2, (10 + depth * 2) * (pcls == CL_MAGE ? 3 : 2) / 2, 1, "a fireball");
        break;
    case SC_FROST: {
        int n = 0;
        for (int i = 0; i < MAX_MOBS; i++)
            if (is_hostile(i) && vis[mobs[i].y][mobs[i].x]) {
                mobs[i].frozen = 6; mobs[i].prep = 0; n++;
                burst(mobs[i].x, mobs[i].y, 5, C_CYAN, 4, 0);
            }
        bclear(warn, sizeof(warn));
        msg(n ? "Frost locks your foes in ice!" : "Frost crackles harmlessly.", C_CYAN);
        break;
    }
    case SC_MAPPING:
        for (int y = 0; y < MAP_H; y++)
            for (int x = 0; x < MAP_W; x++) {
                if (map[y][x] != T_WALL || (y + 1 < MAP_H && !is_wall_t(map[y + 1][x])) ||
                    (y > 0 && !is_wall_t(map[y - 1][x])) || (x > 0 && !is_wall_t(map[y][x - 1])) ||
                    (x + 1 < MAP_W && !is_wall_t(map[y][x + 1])))
                    seen[y][x] = 1;
                trap[y][x] &= (uint8_t)~TRAP_HIDDEN;
            }
        msg("The floor plan fills your mind.", C_CYAN);
        break;
    case SC_ENCHANT: {
        int c = choose_enchant();
        if (c < 0) return 0;
        if (c == 0) { p_wench++; mstart(WEAPON_NAME[p_wpn]); g_cat(mb, " +"); g_cat_int(mb, p_wench); }
        else { p_aench++; mstart(ARMOR_NAME[p_arm]); g_cat(mb, " +"); g_cat_int(mb, p_aench); }
        g_cat(mb, "! It glows."); msg(mb, C_YELLOW);
        levelup_fx = 6;
        break;
    }
    default:
        for (int i = 0; i < N_FLASK; i++) flask_known[i] = 1;
        msg("Every flask's nature is revealed.", C_VIOLET);
        break;
    }
    n_scroll[k]--;
    return 1;
}

static int quaff_health(void)
{
    if (n_hpot <= 0) { msg("No health potions!", C_DIM); return 0; }
    if (p_hp >= p_mhp && !poison_turns) { msg("You are already at full health.", C_DIM); return 0; }
    n_hpot--;
    int amt = (10 + p_lvl * 3) * (2 + perk[PK_ALCH]) / 2;
    heal_player(amt);
    poison_turns = 0;
    msg("You drink a health potion.", C_GREEN);
    return 1;
}

static int use_oil(void)
{
    if (n_oil <= 0) return 0;
    n_oil--;
    p_fuel = TORCH_MAX;
    msg("You refill your torch. It roars.", C_ORANGE);
    burst(px, py, 6, C_ORANGE, 4, 0);
    return 1;
}

static void descend(void);

static int try_move(int k)
{
    int nx = px + DX[k], ny = py + DY[k];
    last_dir = k;
    if (!in_map(nx, ny)) return 0;
    int j = mob_at(nx, ny);
    if (j >= 0) {
        if (mobs[j].type == M_MERCHANT) return 2;     /* caller opens the shop */
        player_attack(j);
        return 1;
    }
    if (root_turns > 0) {
        msg("You struggle against the web.", C_DIM);
        return 1;
    }
    int t = map[ny][nx];
    if (t == T_LOCKED) {
        if (p_keys > 0) {
            p_keys--;
            map[ny][nx] = T_DOOR;
            msg("The key turns. The vault is open!", C_YELLOW);
            burst(nx, ny, 8, C_YELLOW, 4, 0);
            return 1;
        }
        msg("Locked. Find this floor's key.", C_DIM);
        return 0;
    }
    if (blocks_move(t)) return 0;
    px = nx; py = ny;
    if (t == T_GRASS) map[ny][nx] = T_GRASS_CUT;
    if (t == T_WATER && burn_turns) { burn_turns = 0; msg("The water douses the flames.", C_CYAN); }
    for (int i = 0; i < MAX_MOBS; i++)
        if (mobs[i].alive && mobs[i].state == ST_PILE && mobs[i].x == px && mobs[i].y == py) {
            mobs[i].alive = 0;
            kills++;
            burst(px, py, 8, pal565['v'], 5, 1);
            msg("You grind the bones to dust.", C_TEXT);
        }
    if (decal[py][px] == DC_WEB && BIOME() == 0 && chance(50)) {
        decal[py][px] = DC_NONE;
        root_turns = 1;
        msg("You blunder into a cobweb.", C_DIM);
    }
    spring_trap();
    if (!dead) pickup();
    return 1;
}

static int interact(void)
{
    int t = map[py][px];
    if (t == T_STAIRS) { descend(); return 0; }
    if (t == T_CHEST) { open_chest(); return 1; }
    if (t == T_SHRINE) { use_shrine(); return 1; }
    if (t == T_ALTAR) { use_altar(); return map[py][px] == T_ALTAR_USED; }
    for (int k = 0; k < 4; k++) {
        int nx = px + DX[k], ny = py + DY[k];
        if (in_map(nx, ny) && map[ny][nx] == T_TORCH) {
            map[ny][nx] = T_TORCH_OUT;
            p_fuel = imin(TORCH_MAX, p_fuel + 80);
            calc_static_light();
            burst(nx, ny, 6, C_ORANGE, 4, 0);
            msg("You take the torch. Fuel +80.", C_ORANGE);
            return 1;
        }
    }
    int found = 0;
    for (int y = py - 2; y <= py + 2; y++)
        for (int x = px - 2; x <= px + 2; x++)
            if (in_map(x, y) && (trap[y][x] & TRAP_HIDDEN)) { trap[y][x] &= (uint8_t)~TRAP_HIDDEN; found++; }
    msg(found ? "You search... and find a trap!" : "You wait and listen.", found ? C_YELLOW : C_DIM);
    return 1;
}

/* ── End of the player's turn ────────────────────────────────────────── */
static void player_upkeep(void)
{
    if (bless_turns && --bless_turns == 0) msg("The blessing fades.", C_DIM);
    if (burn_turns) {
        if (map[py][px] == T_WATER) burn_turns = 0;
        else { burn_turns--; hurt_player(1 + depth / 4, "flames"); burst(px, py, 3, C_ORANGE, 3, 0); }
    }
    if (poison_turns && !dead) { poison_turns--; hurt_player(1 + depth / 5, "poison"); }
    int interval = imax(2, 10 - perk[PK_REGEN] * 3 - p_lvl / 5);
    if (++regen_ctr >= interval) { regen_ctr = 0; if (p_hp < p_mhp && !poison_turns) p_hp++; }
    if (skill_cd) skill_cd--;
    if (haste_turns && --haste_turns == 0) msg("You slow back down.", C_DIM);
    if (invis_turns && --invis_turns == 0) msg("You fade back into view.", C_DIM);
    if (root_turns) root_turns--;
    if (p_fuel > 0) {
        p_fuel--;
        if (p_fuel == 40) msg("Your torch burns low.", C_ORANGE);
        if (p_fuel == 15) msg("Your torch is guttering!", C_ORANGE);
        if (p_fuel == 0) msg("Your torch dies! Darkness...", C_RED);
    }
}

static int visible_threats(void)
{
    int n = 0;
    for (int i = 0; i < MAX_MOBS; i++)
        if (is_hostile(i) && mobs[i].state == ST_HUNT && vis[mobs[i].y][mobs[i].x]) n++;
    return n;
}

static void end_turn(void)
{
    int threats = visible_threats(), hp0 = p_hp;
    turns++;
    if (haste_turns && (haste_flip ^= 1)) {
        /* hasted: monsters skip every other player action */
    } else {
        mobs_turn();
        if (!dead) env_turn();
    }
    if (!dead) player_upkeep();
    update_vision();
    /* stop a held D-pad when something new happens */
    if (visible_threats() > threats || p_hp < hp0) g_flush_input();
}

/* ── Rendering ───────────────────────────────────────────────────────── */
static void blit16(int x, int y, const uint16_t *px16)
{
    display_bitmap_transparent(x, y, 16, 16, px16, 512, TKEY);
}
static void blit_icon(int x, int y, int ic)
{
    display_bitmap_transparent(x, y, 8, 8, icon_px[ic], 128, TKEY);
}
/* 2x blow-up through the scratch buffer (16x16 -> 32x32, 8x8 -> 16x16). */
static void blit_2x(int x, int y, const uint16_t *src, int n)
{
    for (int yy = 0; yy < n * 2; yy++)
        for (int xx = 0; xx < n * 2; xx++)
            scratch[yy * n * 2 + xx] = src[(yy / 2) * n + xx / 2];
    display_bitmap_transparent(x, y, n * 2, n * 2, scratch, (uint32_t)(n * n * 8), TKEY);
}
/* White silhouette of a sprite, for hit flashes. */
static void blit_flash(int x, int y, const uint16_t *src, int n)
{
    for (int i = 0; i < n * n; i++) scratch[i] = (src[i] == TKEY) ? TKEY : 0xFFFF;
    display_bitmap_transparent(x, y, n, n, scratch, (uint32_t)(n * n * 2), TKEY);
}

static void text_out(int x, int y, const char *s, uint16_t c)
{
    display_text(x - 1, y, s, 0x0000); display_text(x + 1, y, s, 0x0000);
    display_text(x, y - 1, s, 0x0000); display_text(x, y + 1, s, 0x0000);
    display_text(x, y, s, c);
}

static void camera_update(int snap)
{
    int tx = px * TS + TS / 2 - VIEW_COLS * TS / 2;
    int ty = py * TS + TS / 2 - VIEW_ROWS * TS / 2;
    tx = iclamp(tx, 0, MAP_W * TS - VIEW_COLS * TS);
    ty = iclamp(ty, 0, MAP_H * TS - VIEW_ROWS * TS);
    if (snap) { cam_x = tx; cam_y = ty; return; }
    int dx = tx - cam_x, dy = ty - cam_y;
    cam_x += iabs(dx) <= 2 ? dx : dx / 2;
    cam_y += iabs(dy) <= 2 ? dy : dy / 2;
}
static int sx_of(int tx) { return tx * TS - cam_x; }
static int sy_of(int ty) { return VIEW_Y0 + ty * TS - cam_y; }

static int tile_sprite(int x, int y, int lit)
{
    int t = map[y][x];
    int f = (anim_tick >> 2) & 1;
    switch (t) {
    case T_WALL: {
        int below = (y + 1 < MAP_H) ? map[y + 1][x] : T_WALL;
        if (is_wall_t(below)) return TL_WALL_TOP;
        return (thash(x, y) % 100 < 14) ? TL_WALL_FACE_B : TL_WALL_FACE;
    }
    case T_TORCH: return lit ? (f ? TL_TORCH1 : TL_TORCH0) : TL_TORCH0;
    case T_TORCH_OUT: return TL_TORCH_OUT;
    case T_DOOR: case T_LOCKED: {
        int face = is_wall_t(map[y][x - 1]) && is_wall_t(map[y][x + 1]);
        int open = (t == T_DOOR) && occupied(x, y);
        if (t == T_LOCKED) return face ? TL_LOCKED : TL_LOCKED_SIDE;
        return face ? (open ? TL_DOOR_OPEN : TL_DOOR) : (open ? TL_DOOR_SIDE_OPEN : TL_DOOR_SIDE);
    }
    case T_STAIRS: return TL_STAIRS;
    case T_CHEST: return TL_CHEST;
    case T_CHEST_OPEN: return TL_CHEST_OPEN;
    case T_SHRINE: return TL_SHRINE;
    case T_SHRINE_USED: return TL_SHRINE_USED;
    case T_ALTAR: return TL_ALTAR;
    case T_ALTAR_USED: return TL_ALTAR_USED;
    case T_GRASS: return TL_GRASS;
    case T_GRASS_CUT: return TL_GRASS_CUT;
    case T_WATER: return (lit && ((anim_tick >> 3) & 1)) ? TL_WATER1 : TL_WATER0;
    case T_LAVA: return (lit && ((anim_tick >> 2) & 1)) ? TL_LAVA1 : TL_LAVA0;
    default: {
        int tr = trap[y][x];
        if ((tr & 7) && !(tr & TRAP_HIDDEN)) return TL_TRAP;
        uint32_t h = thash(x, y) % 100;
        return h < 70 ? TL_FLOOR_A : h < 88 ? TL_FLOOR_B : TL_FLOOR_C;
    }
    }
}

static uint16_t trap_col(int t)
{
    switch (t) {
    case TR_DART: return C_DIM;
    case TR_FIRE: return C_ORANGE;
    case TR_ALARM: return C_YELLOW;
    default: return C_VIOLET;
    }
}

static void draw_tile(int x, int y)
{
    int sx = sx_of(x), sy = sy_of(y);
    int lit = vis[y][x];
    int id = tile_sprite(x, y, lit);
    const uint16_t *src;
    if (lit) src = tile_px[id];
    else {
        int mid = id;
        if (id == TL_WATER1) mid = TL_WATER0;
        else if (id == TL_LAVA1) mid = TL_LAVA0;
        else if (id == TL_TORCH1) mid = TL_TORCH0;
        src = tile_mem[mid];
    }
    display_bitmap(sx, sy, 16, 16, src, 512);

    if (id == TL_WALL_TOP) {                     /* rim where the wall meets open floor */
        uint16_t e = lit ? edge_lit : edge_mem;
        if (y > 0 && !is_wall_t(map[y - 1][x])) display_hline(sx, sy, 16, e);
        if (x > 0 && !is_wall_t(map[y][x - 1])) display_vline(sx, sy, 16, e);
        if (x + 1 < MAP_W && !is_wall_t(map[y][x + 1])) display_vline(sx + 15, sy, 16, e);
    }
    if (!lit) return;
    if (id == TL_TRAP) display_rect(sx + 7, sy + 7, 2, 2, trap_col(trap[y][x] & 7));
    int dc = decal[y][x];
    if (dc && !is_wall_t(map[y][x])) {
        static const uint8_t DSP[] = { 0, SP_BLOOD, SP_BONES, SP_SCORCH, SP_WEB, SP_GOO };
        blit16(sx, sy, spr_px[DSP[dc]]);
    }
    int lv = light[y][x];
    if (lv <= 2) display_bitmap_transparent(sx, sy, 16, 16, dith_px[lv + 1], 512, TKEY);
}

static void draw_fx_tiles(int x0, int y0, int x1, int y1)
{
    int f = (anim_tick >> 1) & 1;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            if (!vis[y][x]) continue;
            if (fire[y][x]) blit16(sx_of(x), sy_of(y) - 2, spr_px[f ? SP_FIRE1 : SP_FIRE0]);
            if (warn[y][x] && (anim_tick & 2)) {
                uint16_t c = g_mono ? 0xFFFF : c565(255, 60, 30);
                display_rect_outline(sx_of(x) + 1, sy_of(y) + 1, 14, 14, c);
                display_rect_outline(sx_of(x) + 3, sy_of(y) + 3, 10, 10, c);
            }
        }
}

static void draw_items(void)
{
    for (int i = 0; i < MAX_ITEMS; i++) {
        item_t *it = &items[i];
        if (!it->type || !seen[it->y][it->x]) continue;
        int sx = sx_of(it->x), sy = sy_of(it->y);
        if (sx < -16 || sx >= SCR_W || sy < VIEW_Y0 - 16 || sy >= VIEW_Y0 + VIEW_ROWS * TS) continue;
        int sp;
        switch (it->type) {
        case IT_GOLD: sp = SP_GOLD; break;
        case IT_HPOT: sp = SP_HPOT; break;
        case IT_FLASK: sp = SP_FLASK0 + flask_col[it->sub]; break;
        case IT_SCROLL: sp = SP_SCROLL; break;
        case IT_BOMB: sp = SP_BOMB; break;
        case IT_OIL: sp = SP_OIL; break;
        case IT_KEY: sp = SP_KEY; break;
        case IT_WEAPON: sp = SP_SWORD; break;
        case IT_ARMOR: sp = SP_ARMOR; break;
        default: sp = SP_CRYSTAL; break;
        }
        int bob = (it->type == IT_CRYSTAL || it->type == IT_KEY) ? (((anim_tick >> 2) + i) & 1) : 0;
        blit16(sx, sy - bob, spr_px[sp]);
        if (!vis[it->y][it->x]) {
            display_bitmap_transparent(sx, sy, 16, 16, dith_px[4], 512, TKEY);
        } else if (((anim_tick + i * 5) & 15) == 0) {
            display_pixel(sx + 5 + (i * 3) % 6, sy + 6, 0xFFFF);      /* glint */
        }
    }
}

static int mob_visible(int i)
{
    mob_t *m = &mobs[i];
    if (!m->alive) return 0;
    if (vis[m->y][m->x]) return 1;
    return sight_on && m->state != ST_PILE && m->type != M_MERCHANT;
}

static void draw_mob(int i)
{
    mob_t *m = &mobs[i];
    const mobdef_t *d = &MOBS[m->type];
    int sx = sx_of(m->x), sy = sy_of(m->y);
    if (sx < -24 || sx >= SCR_W + 8 || sy < VIEW_Y0 - 24 || sy >= VIEW_Y0 + VIEW_ROWS * TS + 8) return;
    int lit = vis[m->y][m->x];

    if (m->state == ST_PILE) { blit16(sx, sy, spr_px[SP_BONES]); return; }
    if (m->state == ST_HIDDEN) {
        display_bitmap(sx, sy, 16, 16, tile_px[TL_CHEST], 512);
        int lv = light[m->y][m->x];
        if (lv <= 2) display_bitmap_transparent(sx, sy, 16, 16, dith_px[lv + 1], 512, TKEY);
        return;
    }
    int fly = (d->flags & MF_FLY) != 0;
    int bob = (m->state == ST_SLEEP || m->frozen) ? 0 : (((anim_tick >> 2) + i) & 1) * (fly ? 2 : 1);
    int ox = 0, oy = 0;
    if (m->lunge) { ox = m->ldx * 4; oy = m->ldy * 4; }
    /* contact shadow */
    if (!g_mono || lit) {
        uint16_t sh = 0x0000;
        if (d->flags & MF_BOSS) { display_hline(sx - 1, sy + 14, 18, sh); display_hline(sx + 1, sy + 15, 14, sh); }
        else if (fly) display_hline(sx + 5, sy + 15, 6, sh);
        else { display_hline(sx + 4, sy + 14, 8, sh); display_hline(sx + 3, sy + 15, 10, sh); }
    }
    if (d->flags & MF_BOSS) {
        const uint16_t *src = boss_px[d->spr];
        int bx = sx - 4 + ox, by = sy - 8 + oy - bob;
        if (m->flash) blit_flash(bx, by, src, 24);
        else display_bitmap_transparent(bx, by, 24, 24, src, 1152, TKEY);
        if (m->frozen) display_bitmap_transparent(sx, sy, 16, 16, dith_px[0], 512, TKEY);
        if (m->prep && (anim_tick & 2)) text_out(sx + 5, sy - 18, "!", C_RED);
        return;
    }
    const uint16_t *src = spr_px[d->spr];
    int dy = fly ? -3 : 0;
    if (m->flash) blit_flash(sx + ox, sy + oy - bob + dy, src, 16);
    else blit16(sx + ox, sy + oy - bob + dy, src);
    if (m->frozen) display_bitmap_transparent(sx, sy + dy, 16, 16, dith_px[0], 512, TKEY);
    if (m->burn) blit16(sx, sy - 4, spr_px[(anim_tick & 2) ? SP_FIRE1 : SP_FIRE0]);
    if (!lit) { display_bitmap_transparent(sx, sy + dy, 16, 16, dith_px[2], 512, TKEY); return; }
    int lv = light[m->y][m->x];
    if (lv <= 1) display_bitmap_transparent(sx, sy + dy, 16, 16, dith_px[3], 512, TKEY);
    if (m->state == ST_SLEEP) {
        int z = (anim_tick >> 2) & 3;
        text_out(sx + 11, sy - 6 - z, "z", g_mono ? 0xFFFF : c565(170, 200, 255));
    }
    if (m->type != M_MERCHANT && m->hp < m->mhp) {
        int w = 12 * m->hp / m->mhp;
        display_rect(sx + 2, sy - 3 + dy, 12, 2, C_BARBG);
        display_rect(sx + 2, sy - 3 + dy, imax(1, w), 2, m->hp * 3 <= m->mhp ? C_RED : C_GREEN);
    }
}

static void draw_player(void)
{
    int sx = sx_of(px), sy = sy_of(py);
    int ox = p_lunge ? p_ldx * 4 : 0, oy = p_lunge ? p_ldy * 4 : 0;
    int bob = ((anim_tick >> 2) & 1);
    const uint16_t *src = spr_px[CLASSES[pcls].spr];
    display_hline(sx + 4, sy + 14, 8, 0x0000);
    display_hline(sx + 3, sy + 15, 10, 0x0000);
    if (p_flash) blit_flash(sx + ox, sy + oy - bob, src, 16);
    else blit16(sx + ox, sy + oy - bob, src);
    if (invis_turns) display_bitmap_transparent(sx + ox, sy + oy - bob, 16, 16, dith_px[2], 512, TKEY);
    if (root_turns) blit16(sx, sy, spr_px[SP_WEB]);
    if (burn_turns) blit16(sx, sy - 4, spr_px[(anim_tick & 2) ? SP_FIRE1 : SP_FIRE0]);
    if (bless_turns && (anim_tick & 3) == 0) spawn_part(px * TS + 2 + rn(12), py * TS + 12, 0, -8, C_YELLOW, 6, 0);
    if (haste_turns && (anim_tick & 1)) spawn_part(px * TS + rn(16), py * TS + 4 + rn(10), 0, 0, C_TEXT, 2, 0);
}

static void draw_projs(void)
{
    for (int i = 0; i < MAX_PROJ; i++) {
        proj_t *p = &projs[i];
        if (!p->dur) continue;
        int t1 = p->t + 1;
        int x = p->x0 + (p->x1 - p->x0) * t1 / p->dur - cam_x;
        int y = p->y0 + (p->y1 - p->y0) * t1 / p->dur - cam_y + VIEW_Y0;
        int bx = p->x0 + (p->x1 - p->x0) * p->t / p->dur - cam_x;
        int by = p->y0 + (p->y1 - p->y0) * p->t / p->dur - cam_y + VIEW_Y0;
        switch (p->kind) {
        case PJ_ARROW:
            display_line(bx, by, x, y, g_mono ? 0xFFFF : pal565['n']);
            display_rect(x - 1, y - 1, 3, 3, 0xFFFF);
            break;
        case PJ_BOMB: {
            int arc = (p->dur <= 1) ? 0 : 8 - (2 * t1 - p->dur) * (2 * t1 - p->dur) * 8 / (p->dur * p->dur);
            display_circle_fill(x, y - arc, 3, g_mono ? 0xFFFF : pal565['K']);
            display_pixel(x + 2, y - arc - 4, C_YELLOW);
            break;
        }
        case PJ_WEB: display_line(bx, by, x, y, 0xFFFF); display_circle(x, y, 3, 0xFFFF); break;
        case PJ_FROST: display_circle_fill(x, y, 3, C_CYAN); display_pixel(x, y, 0xFFFF); break;
        case PJ_DARK: display_circle_fill(x, y, 3, C_VIOLET); display_circle(x, y, 4, 0x0000); break;
        case PJ_ZAP: display_line(bx, by, x, y + 2, C_YELLOW); display_line(bx, by + 2, x, y, 0xFFFF); break;
        default:
            display_circle_fill(x, y, 4, C_CYAN);
            display_circle_fill(x, y, 2, 0xFFFF);
            display_line(bx, by, x, y, C_CYAN);
            break;
        }
    }
}

static void draw_particles(void)
{
    for (int i = 0; i < MAX_PART; i++) {
        part_t *p = &parts[i];
        if (!p->life) continue;
        int x = p->x / 16 - cam_x, y = p->y / 16 - cam_y + VIEW_Y0;
        display_rect(x, y, p->life > 3 ? 2 : 1, p->life > 3 ? 2 : 1, p->col);
    }
}

static void draw_floats(void)
{
    for (int i = 0; i < MAX_FLOAT; i++) {
        floatx_t *f = &floats[i];
        if (!f->t) continue;
        int rise = 12 - f->t;
        text_out(f->x - cam_x, f->y - cam_y + VIEW_Y0 - rise - 6, f->txt, f->col);
    }
}

static void tick_fx(void)
{
    anim_tick++;
    for (int i = 0; i < MAX_PART; i++) {
        part_t *p = &parts[i];
        if (!p->life) continue;
        p->x += p->vx * 4; p->y += p->vy * 4;
        if (p->grav) p->vy += 3;
        p->life--;
    }
    for (int i = 0; i < MAX_PROJ; i++)
        if (projs[i].dur && ++projs[i].t >= projs[i].dur) projs[i].dur = 0;
    for (int i = 0; i < MAX_FLOAT; i++) if (floats[i].t) floats[i].t--;
    for (int i = 0; i < MAX_MOBS; i++) {
        if (mobs[i].flash) mobs[i].flash--;
        if (mobs[i].lunge) mobs[i].lunge--;
    }
    if (p_flash) p_flash--;
    if (p_lunge) p_lunge--;
    if (shake) shake--;
    if (flash_white) flash_white--;
    if (levelup_fx) levelup_fx--;
}

/* ── HUD ─────────────────────────────────────────────────────────────── */
static void bar(int x, int y, int w, int h, int v, int max, uint16_t col)
{
    int fw = max > 0 ? w * imax(0, v) / max : 0;
    display_rect(x, y, w, h, C_BARBG);
    if (fw > 0) {
        display_rect(x, y, fw, h, col);
        if (!g_mono && h > 3) display_hline(x, y, fw, 0xFFFF);
    }
    display_rect_outline(x - 1, y - 1, w + 2, h + 2, g_mono ? 0xFFFF : 0x0000);
}

static void draw_hud(void)
{
    char b[24];
    display_rect(0, 0, SCR_W, HUD_H, C_PANEL);
    display_hline(0, HUD_H - 1, SCR_W, c_accent);

    blit_icon(3, 3, IC_HEART);
    uint16_t hc = p_hp * 4 <= p_mhp ? C_RED : p_hp * 2 <= p_mhp ? C_YELLOW : C_GREEN;
    bar(15, 2, 80, 10, p_hp, p_mhp, hc);
    b[0] = '\0'; g_cat_int(b, p_hp); g_cat(b, "/"); g_cat_int(b, p_mhp);
    text_out(55 - g_tw(b) / 2, 2, b, 0xFFFF);
    bar(15, 16, 80, 3, p_xp, p_xpn, C_CYAN);

    blit_icon(101, 3, IC_STAIRS);
    b[0] = '\0'; g_cat(b, "B"); g_cat_int(b, depth);
    display_text(111, 2, b, C_TEXT);
    b[0] = '\0'; g_cat(b, "Lv"); g_cat_int(b, p_lvl);
    display_text(101, 13, b, C_DIM);

    blit_icon(140, 3, IC_COIN);
    b[0] = '\0'; g_cat_int(b, p_gold);
    display_text(150, 2, b, C_GOLD);
    if (p_keys) { blit_icon(184, 3, IC_KEY); b[0] = '\0'; g_cat(b, "x"); g_cat_int(b, p_keys); display_text(194, 2, b, C_YELLOW); }

    /* status icons, row 2 */
    int ix = 140;
    if (poison_turns) { blit_icon(ix, 14, IC_POISON); ix += 10; }
    if (burn_turns)   { blit_icon(ix, 14, IC_BURN);   ix += 10; }
    if (bless_turns)  { blit_icon(ix, 14, IC_BLESS);  ix += 10; }
    if (haste_turns)  { blit_icon(ix, 14, IC_HASTE);  ix += 10; }
    if (invis_turns)  { blit_icon(ix, 14, IC_INVIS);  ix += 10; }
    if (root_turns)   { blit_icon(ix, 14, IC_ROOT);   ix += 10; }
    if (sight_on)     { blit_icon(ix, 14, IC_SIGHT);  ix += 10; }

    /* torch fuel + skill, right-aligned */
    int rx = SCR_W - 70;
    blit_icon(rx, 3, IC_FLAME);
    bar(rx + 11, 4, 56, 5, p_fuel, TORCH_MAX, p_fuel <= 15 ? C_RED : C_ORANGE);
    b[0] = '\0'; g_cat(b, "Y:");
    if (skill_cd) { g_cat_int(b, skill_cd); display_text(rx, 13, b, C_DIM); }
    else { g_cat(b, CLASSES[pcls].hud); display_text(rx, 13, b, C_GREEN); }
}

static void draw_msgbar(void)
{
    int y0 = SCR_H - BAR_H;
    display_rect(0, y0, SCR_W, BAR_H, C_PANEL);
    display_hline(0, y0, SCR_W, c_accent);
    for (int i = 0; i < LOG_N; i++) {
        if (!logm[i][0]) continue;
        char b[48];
        scopy(b, logm[i], 36);
        if (logrep[i] > 1) { g_cat(b, " x"); g_cat_int(b, logrep[i]); }
        int age = turns - log_turn[i];
        uint16_t c = (i == LOG_N - 1 && age < 3) ? logc[i] : C_DIM;
        display_text(4, y0 + 3 + i * 11, b, c);
    }
    /* B: health potions */
    int x = SCR_W - 34;
    display_bitmap_transparent(x, y0 + 4, 16, 16, spr_px[SP_HPOT], 512, TKEY);
    char b[8]; b[0] = '\0'; g_cat(b, "x"); g_cat_int(b, n_hpot);
    display_text(x + 15, y0 + 9, b, n_hpot ? C_TEXT : C_DIM);
}

static void draw_boss_bar(void)
{
    if (boss_idx < 0 || !mobs[boss_idx].alive) return;
    mob_t *m = &mobs[boss_idx];
    if (m->state == ST_SLEEP || !seen[m->y][m->x]) return;
    int w = SCR_W * 3 / 5, x = (SCR_W - w) / 2, y = VIEW_Y0 + 4;
    text_out(SCR_W / 2 - g_tw(MOBS[m->type].name) / 2, y, MOBS[m->type].name, C_RED);
    bar(x, y + 12, w, 4, m->hp, m->mhp, C_RED);
}

static void draw_world(void)
{
    int sx_save = cam_x, sy_save = cam_y;
    if (shake && !g_mono) { cam_x += (anim_tick & 1) ? 2 : -2; cam_y += (anim_tick & 2) ? 1 : -1; }
    display_clear(C_BG);
    int x0 = imax(0, cam_x / TS), y0 = imax(0, cam_y / TS);
    int x1 = imin(MAP_W - 1, (cam_x + VIEW_COLS * TS - 1) / TS + 1);
    int y1 = imin(MAP_H - 1, (cam_y + VIEW_ROWS * TS - 1) / TS + 1);
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            if (seen[y][x]) draw_tile(x, y);
    draw_items();
    /* monsters and hero, back to front */
    for (int y = y0; y <= y1; y++) {
        for (int i = 0; i < MAX_MOBS; i++)
            if (mobs[i].y == y && mob_visible(i)) draw_mob(i);
        if (py == y && !dead) draw_player();
    }
    draw_fx_tiles(x0, y0, x1, y1);
    draw_projs();
    draw_particles();
    draw_floats();
    cam_x = sx_save; cam_y = sy_save;

    if (levelup_fx && (levelup_fx & 2))
        display_rect_outline(0, VIEW_Y0, SCR_W, VIEW_ROWS * TS, C_YELLOW);
    if (flash_white) {
        display_rect_outline(0, VIEW_Y0, SCR_W, VIEW_ROWS * TS, 0xFFFF);
        display_rect_outline(1, VIEW_Y0 + 1, SCR_W - 2, VIEW_ROWS * TS - 2, 0xFFFF);
    }
    if (!dead && p_hp * 4 <= p_mhp && (anim_tick & 4)) {
        display_rect_outline(0, VIEW_Y0, SCR_W, VIEW_ROWS * TS, C_RED);
        display_rect_outline(1, VIEW_Y0 + 1, SCR_W - 2, VIEW_ROWS * TS - 2, C_RED);
    }
    draw_boss_bar();
    draw_hud();
    draw_msgbar();
}

static void frame(void)
{
    camera_update(0);
    draw_world();
    display_flush();
    tick_fx();
}

/* ── UI widgets ──────────────────────────────────────────────────────── */
static void panel(int x, int y, int w, int h, const char *title)
{
    if (!g_mono) display_rounded_rect_fill(x + 3, y + 3, w, h, 6, 0x0000);
    display_rounded_rect_fill(x, y, w, h, 6, C_PANEL);
    display_rounded_rect(x, y, w, h, 6, c_accent);
    if (title) {
        display_text_large(x + w / 2 - g_tw_large(title) / 2, y + 6, title, C_YELLOW);
        display_hline(x + 8, y + 27, w - 16, C_EDGE);
    }
}
static void sel_row(int x, int y, int w, int h)
{
    display_rounded_rect_fill(x, y, w, h, 4, C_SEL);
    if (!g_mono) display_rounded_rect(x, y, w, h, 4, c_accent);
}
/* redraw the game behind a modal so it is never stale */
static void backdrop(void) { draw_world(); }

/* ── Perk choice (level up / blood altar) ────────────────────────────── */
static void choose_perk(void)
{
    int opts[3], n = 0;
    for (int t = 0; t < 60 && n < 3; t++) {
        int k = rn(N_PERK), dup = 0;
        if (perk[k] >= PERKS[k].max) continue;
        for (int j = 0; j < n; j++) if (opts[j] == k) dup = 1;
        if (!dup) opts[n++] = k;
    }
    if (!n) { p_mhp += 5; p_hp += 5; return; }
    int sel = 0;
    g_flush_input();
    for (;;) {
        backdrop();
        int cw = (SCR_W - 24) / 3, ch = 118, y = 52;
        int x0 = (SCR_W - (cw * 3 + 12)) / 2;
        display_rect(0, 26, SCR_W, 22, C_PANEL);
        g_text_large_c(SCR_W / 2, 28, pending_perks > 1 ? "CHOOSE A PERK (+)" : "CHOOSE A PERK", C_YELLOW);
        for (int i = 0; i < n; i++) {
            int x = x0 + i * (cw + 6);
            const perk_t *pk = &PERKS[opts[i]];
            panel(x, y, cw, ch, 0);
            if (i == sel) {
                display_rounded_rect(x - 2, y - 2, cw + 4, ch + 4, 7, C_YELLOW);
                display_rounded_rect(x - 1, y - 1, cw + 2, ch + 2, 7, C_YELLOW);
            }
            blit_2x(x + cw / 2 - 8, y + 10, icon_px[pk->icon], 8);
            g_text_c(x + cw / 2, y + 36, pk->name, i == sel ? C_YELLOW : C_TEXT);
            g_text_c(x + cw / 2, y + 56, pk->d1, C_DIM);
            g_text_c(x + cw / 2, y + 68, pk->d2, C_DIM);
            if (perk[opts[i]]) {
                char b[8]; b[0] = '\0'; g_cat(b, "Rank "); g_cat_int(b, perk[opts[i]] + 1);
                g_text_c(x + cw / 2, y + ch - 18, b, C_CYAN);
            }
        }
        g_text_c(SCR_W / 2, y + ch + 10, "LEFT/RIGHT choose   A take", C_DIM);
        display_flush();
        uint32_t k = g_wait_key();
        if (k == K_LEFT) sel = (sel + n - 1) % n;
        else if (k == K_RIGHT) sel = (sel + 1) % n;
        else if (k == K_A) break;
    }
    int p = opts[sel];
    perk[p]++;
    switch (p) {
    case PK_VITAL: p_mhp += 8; p_hp = imin(p_mhp, p_hp + 8); break;
    case PK_LANTERN: p_torch++; p_fuel = TORCH_MAX; break;
    default: break;
    }
    mstart("Perk gained: "); g_cat(mb, PERKS[p].name); msg(mb, C_YELLOW);
    update_vision();
}

/* ── Direction picker for bombs and bolts ────────────────────────────── */
static int aim(const char *what, int range, int radius)
{
    int k = last_dir;
    /* prefer a monster that is lined up with us */
    for (int d = 0; d < 4; d++) {
        int x, y;
        fire_line(d, range, &x, &y);
        int j = mob_at(x, y);
        if (j >= 0 && is_hostile(j) && vis[y][x]) { k = d; break; }
    }
    g_flush_input();
    for (;;) {
        draw_world();
        int x, y;
        fire_line(k, range, &x, &y);
        for (int s = 1; s <= range; s++) {
            int lx = px + DX[k] * s, ly = py + DY[k] * s;
            if (lx == x && ly == y) break;
            if (!in_map(lx, ly)) break;
            display_rect(sx_of(lx) + 7, sy_of(ly) + 7, 2, 2, C_YELLOW);
        }
        for (int dy = -radius; dy <= radius; dy++)
            for (int dx = -radius; dx <= radius; dx++)
                display_rect_outline(sx_of(x + dx) + 1, sy_of(y + dy) + 1, 14, 14, (anim_tick & 2) ? C_RED : C_ORANGE);
        display_rect(0, VIEW_Y0, SCR_W, 14, C_PANEL);
        char b[48]; b[0] = '\0'; g_cat(b, what); g_cat(b, ": D-pad, A fire, B cancel");
        g_text_c(SCR_W / 2, VIEW_Y0 + 2, b, C_YELLOW);
        display_flush();
        anim_tick++;
        uint32_t key = g_key();
        if (!key) { delay(30000); continue; }
        if (key == K_UP) k = 0; else if (key == K_RIGHT) k = 1;
        else if (key == K_DOWN) k = 2; else if (key == K_LEFT) k = 3;
        else if (key == K_A || key == K_Y || key == K_X) { last_dir = k; return k; }
        else if (key == K_B || key == K_HOME) return -1;
    }
}

static int use_skill(void)
{
    if (skill_cd) {
        char b[40]; mstart(CLASSES[pcls].skill); g_cat(mb, " recharges: "); g_cat_int(mb, skill_cd);
        g_cat(mb, " turns."); scopy(b, mb, 40); msg(b, C_DIM);
        return 0;
    }
    switch (pcls) {
    case CL_WARRIOR:
        if (!skill_cleave()) return 0;
        break;
    case CL_ROGUE:
        invis_turns = 7;
        for (int i = 0; i < MAX_MOBS; i++)
            if (is_hostile(i) && mobs[i].state == ST_HUNT) mobs[i].state = ST_WANDER;
        burst(px, py, 12, C_DIM, 5, 0);
        msg("You melt into the shadows.", C_VIOLET);
        break;
    default: {
        int k = aim("Arcane Bolt", 8, 0);
        if (k < 0) return 0;
        arcane_bolt(k);
        break;
    }
    }
    skill_cd = skill_max_cd();
    return 1;
}

/* ── Bag ─────────────────────────────────────────────────────────────── */
enum { BG_SKILL, BG_HPOT, BG_FLASK, BG_SCROLL, BG_BOMB, BG_OIL };
static int bag_menu(void)
{
    static int sel;
    g_flush_input();
    for (;;) {
        uint8_t kind[20], sub[20];
        int n = 0;
        kind[n] = BG_SKILL; sub[n++] = 0;
        if (n_hpot) { kind[n] = BG_HPOT; sub[n++] = 0; }
        for (int k = 0; k < N_FLASK; k++) if (n_flask[k]) { kind[n] = BG_FLASK; sub[n++] = (uint8_t)k; }
        for (int k = 0; k < N_SCROLL; k++) if (n_scroll[k]) { kind[n] = BG_SCROLL; sub[n++] = (uint8_t)k; }
        if (n_bomb) { kind[n] = BG_BOMB; sub[n++] = 0; }
        if (n_oil) { kind[n] = BG_OIL; sub[n++] = 0; }
        if (sel >= n) sel = n - 1;

        backdrop();
        int rows = imin(n, 7), w = 260, h = 44 + rows * 18 + 22;
        int x = (SCR_W - w) / 2, y = VIEW_Y0 + 4;
        panel(x, y, w, h, "BAG");
        int top = iclamp(sel - 3, 0, imax(0, n - rows));
        const char *desc = "";
        for (int r = 0; r < rows; r++) {
            int i = top + r, iy = y + 32 + r * 18;
            if (i == sel) sel_row(x + 6, iy - 1, w - 12, 18);
            const uint16_t *ic = 0;
            char label[28]; int cnt = 0;
            label[0] = '\0';
            switch (kind[i]) {
            case BG_SKILL:
                ic = spr_px[CLASSES[pcls].spr];
                g_cat(label, "Skill: "); g_cat(label, CLASSES[pcls].skill);
                desc = (i == sel) ? (skill_cd ? "Still recharging." : CLASSES[pcls].line2) : desc;
                break;
            case BG_HPOT:
                ic = spr_px[SP_HPOT]; g_cat(label, "Health Potion"); cnt = n_hpot;
                if (i == sel) desc = "Heals and cures poison. (B)";
                break;
            case BG_FLASK: ic = spr_px[SP_FLASK0 + flask_col[sub[i]]]; g_cat(label, flask_label(sub[i]));
                cnt = n_flask[sub[i]];
                if (i == sel) desc = flask_known[sub[i]] ? FLASK_DESC[sub[i]] : "Unknown. Drink to learn.";
                break;
            case BG_SCROLL:
                ic = spr_px[SP_SCROLL]; g_cat(label, "Scroll: "); g_cat(label, SCROLL_NAME[sub[i]]);
                cnt = n_scroll[sub[i]];
                if (i == sel) desc = SCROLL_DESC[sub[i]];
                break;
            case BG_BOMB:
                ic = spr_px[SP_BOMB]; g_cat(label, "Bomb"); cnt = n_bomb;
                if (i == sel) desc = "Throw it. Mind the blast.";
                break;
            default:
                ic = spr_px[SP_OIL]; g_cat(label, "Lamp Oil"); cnt = n_oil;
                if (i == sel) desc = "Refuels your torch.";
                break;
            }
            display_bitmap_transparent(x + 10, iy, 16, 16, ic, 512, TKEY);
            display_text(x + 32, iy + 4, label, i == sel ? C_SELTXT : C_TEXT);
            if (cnt) {
                char b[8]; b[0] = '\0'; g_cat(b, "x"); g_cat_int(b, cnt);
                g_text_r(x + w - 12, iy + 4, b, i == sel ? C_SELTXT : C_DIM);
            } else if (kind[i] == BG_SKILL && skill_cd) {
                char b[8]; b[0] = '\0'; g_cat_int(b, skill_cd); g_cat(b, "t");
                g_text_r(x + w - 12, iy + 4, b, C_DIM);
            }
        }
        display_hline(x + 8, y + h - 21, w - 16, C_EDGE);
        g_text_c(x + w / 2, y + h - 15, desc, C_CYAN);
        display_flush();

        uint32_t k = g_wait_key();
        if (k == K_UP) sel = (sel + n - 1) % n;
        else if (k == K_DOWN) sel = (sel + 1) % n;
        else if (k == K_B || k == K_X || k == K_HOME) return 0;
        else if (k == K_A) {
            switch (kind[sel]) {
            case BG_SKILL: return use_skill();
            case BG_HPOT: return quaff_health();
            case BG_FLASK: drink_flask(sub[sel]); return 1;
            case BG_SCROLL: return read_scroll(sub[sel]);
            case BG_BOMB: { int d = aim("Bomb", 5, 1); if (d < 0) continue; throw_bomb(d); return 1; }
            default: return use_oil();
            }
        }
    }
}

static int choose_enchant(void)
{
    int sel = 0;
    g_flush_input();
    for (;;) {
        backdrop();
        int w = 240, h = 92, x = (SCR_W - w) / 2, y = 70;
        panel(x, y, w, h, "ENCHANT");
        for (int i = 0; i < 2; i++) {
            int iy = y + 34 + i * 20;
            if (i == sel) sel_row(x + 6, iy - 2, w - 12, 18);
            char b[32]; b[0] = '\0';
            g_cat(b, i ? ARMOR_NAME[p_arm] : WEAPON_NAME[p_wpn]);
            g_cat(b, " +"); g_cat_int(b, (i ? p_aench : p_wench) + 1);
            blit_icon(x + 14, iy + 1, i ? IC_SHIELD : IC_SWORD);
            display_text(x + 30, iy + 2, b, i == sel ? C_SELTXT : C_TEXT);
        }
        display_flush();
        uint32_t k = g_wait_key();
        if (k == K_UP || k == K_DOWN) sel ^= 1;
        else if (k == K_A) return sel;
        else if (k == K_B || k == K_HOME) return -1;
    }
}

/* ── Merchant ────────────────────────────────────────────────────────── */
enum { SH_HPOT, SH_OIL, SH_BOMB, SH_SCROLL, SH_FLASK, SH_SHARPEN, SH_REINFORCE, SH_COUNT };
static int shop_price(int i)
{
    switch (i) {
    case SH_HPOT: return 14 + depth * 2;
    case SH_OIL: return 8 + depth;
    case SH_BOMB: return 16 + depth * 2;
    case SH_SCROLL: return 18 + depth * 2;
    case SH_FLASK: return 14 + depth * 2;
    case SH_SHARPEN: return 30 + depth * 4 + p_wench * 20;
    default: return 30 + depth * 4 + p_aench * 20;
    }
}
static void shop(void)
{
    static const char *const L[SH_COUNT] = {
        "Health Potion", "Lamp Oil", "Bomb", "Mystery Scroll", "Mystery Flask", "Sharpen Weapon", "Reinforce Armour"
    };
    static const char *const D[SH_COUNT] = {
        "Heals and cures poison.", "Refuels your torch.", "Throw it from the bag.",
        "A random scroll.", "A random flask.", "+1 attack on your weapon.", "+1 defence on your armour."
    };
    const uint16_t *IC[SH_COUNT] = {
        spr_px[SP_HPOT], spr_px[SP_OIL], spr_px[SP_BOMB], spr_px[SP_SCROLL],
        spr_px[SP_FLASK0 + 4], spr_px[SP_SWORD], spr_px[SP_ARMOR]
    };
    int sel = 0;
    const char *note = "Welcome, traveller!";
    g_flush_input();
    for (;;) {
        backdrop();
        int w = 280, h = 200, x = (SCR_W - w) / 2, y = 30;
        panel(x, y, w, h, "MERCHANT");
        blit_icon(x + w - 60, y + 9, IC_COIN);
        char b[24]; b[0] = '\0'; g_cat_int(b, p_gold);
        display_text(x + w - 48, y + 8, b, C_GOLD);
        display_bitmap_transparent(x + 10, y + 6, 16, 16, spr_px[SP_MERCHANT], 512, TKEY);
        for (int i = 0; i < SH_COUNT; i++) {
            int iy = y + 32 + i * 18, pr = shop_price(i);
            if (i == sel) sel_row(x + 6, iy - 1, w - 12, 18);
            display_bitmap_transparent(x + 10, iy, 16, 16, IC[i], 512, TKEY);
            display_text(x + 32, iy + 4, L[i], i == sel ? C_SELTXT : C_TEXT);
            b[0] = '\0'; g_cat_int(b, pr); g_cat(b, "g");
            g_text_r(x + w - 12, iy + 4, b, p_gold >= pr ? C_GOLD : C_DIM);
        }
        display_hline(x + 8, y + h - 36, w - 16, C_EDGE);
        g_text_c(x + w / 2, y + h - 31, D[sel], C_CYAN);
        g_text_c(x + w / 2, y + h - 17, note, C_DIM);
        display_flush();

        uint32_t k = g_wait_key();
        if (k == K_UP) sel = (sel + SH_COUNT - 1) % SH_COUNT;
        else if (k == K_DOWN) sel = (sel + 1) % SH_COUNT;
        else if (k == K_B || k == K_HOME) return;
        else if (k == K_A) {
            int pr = shop_price(sel);
            if (p_gold < pr) { note = "You can't afford that."; continue; }
            p_gold -= pr;
            note = "A fine choice!";
            switch (sel) {
            case SH_HPOT: n_hpot++; break;
            case SH_OIL: n_oil++; break;
            case SH_BOMB: n_bomb++; break;
            case SH_SCROLL: n_scroll[rn(N_SCROLL)]++; break;
            case SH_FLASK: n_flask[rn(N_FLASK)]++; break;
            case SH_SHARPEN: p_wench++; break;
            default: p_aench++; break;
            }
        }
    }
}

/* ── Pause, map, hero sheet, guide ───────────────────────────────────── */
static void show_map(void)
{
    int cs = imin((SCR_W - 20) / MAP_W, (SCR_H - 50) / MAP_H);
    int w = MAP_W * cs, h = MAP_H * cs, x0 = (SCR_W - w) / 2, y0 = 34;
    g_flush_input();
    for (;;) {
        display_clear(C_BG);
        char b[32]; b[0] = '\0'; g_cat(b, BIOMES[BIOME()].name); g_cat(b, "  B"); g_cat_int(b, depth);
        g_text_large_c(SCR_W / 2, 8, b, c_accent);
        for (int y = 0; y < MAP_H; y++)
            for (int x = 0; x < MAP_W; x++) {
                if (!seen[y][x]) continue;
                int t = map[y][x];
                uint16_t c;
                if (is_wall_t(t)) c = g_mono ? 0xFFFF : pal565['2'];
                else if (t == T_STAIRS) c = C_CYAN;
                else if (t == T_DOOR || t == T_LOCKED) c = g_mono ? 0xFFFF : pal565['n'];
                else if (t == T_WATER) c = g_mono ? 0 : pal565['b'];
                else if (t == T_LAVA) c = g_mono ? 0xFFFF : pal565['r'];
                else if (t == T_CHEST || t == T_SHRINE || t == T_ALTAR) c = C_GOLD;
                else c = g_mono ? 0 : pal565['5'];
                display_rect(x0 + x * cs, y0 + y * cs, cs, cs, c);
                if (g_mono && !is_wall_t(t)) display_pixel(x0 + x * cs + cs / 2, y0 + y * cs + cs / 2, 0xFFFF);
            }
        for (int i = 0; i < MAX_ITEMS; i++)
            if (items[i].type && seen[items[i].y][items[i].x])
                display_rect(x0 + items[i].x * cs + 1, y0 + items[i].y * cs + 1, cs - 2, cs - 2, C_YELLOW);
        for (int i = 0; i < MAX_MOBS; i++)
            if (is_hostile(i) && mob_visible(i))
                display_rect(x0 + mobs[i].x * cs + 1, y0 + mobs[i].y * cs + 1, cs - 2, cs - 2, C_RED);
        if (anim_tick & 2) display_rect(x0 + px * cs, y0 + py * cs, cs, cs, 0xFFFF);
        display_rect_outline(x0 - 2, y0 - 2, w + 4, h + 4, C_EDGE);
        g_text_c(SCR_W / 2, SCR_H - 13, "any key: back", C_DIM);
        display_flush();
        anim_tick++;
        if (g_sleep_or_key(150)) return;
    }
}

static void show_hero(void)
{
    char b[40];
    g_flush_input();
    backdrop();
    int w = 296, h = 206, x = (SCR_W - w) / 2, y = 28;
    panel(x, y, w, h, CLASSES[pcls].name);
    blit_2x(x + 12, y + 34, spr_px[CLASSES[pcls].spr], 16);
    int lx = x + 54, ly = y + 36;
    b[0] = '\0'; g_cat(b, "Level "); g_cat_int(b, p_lvl); g_cat(b, "   XP "); g_cat_int(b, p_xp); g_cat(b, "/"); g_cat_int(b, p_xpn);
    display_text(lx, ly, b, C_TEXT);
    b[0] = '\0'; g_cat(b, "HP "); g_cat_int(b, p_hp); g_cat(b, "/"); g_cat_int(b, p_mhp);
    g_cat(b, "  ATK "); g_cat_int(b, p_atk()); g_cat(b, "  DEF "); g_cat_int(b, p_defence());
    display_text(lx, ly + 12, b, C_TEXT);
    b[0] = '\0'; g_cat(b, WEAPON_NAME[p_wpn]); if (p_wench) { g_cat(b, " +"); g_cat_int(b, p_wench); }
    blit_icon(lx, ly + 26, IC_SWORD); display_text(lx + 12, ly + 25, b, C_YELLOW);
    b[0] = '\0'; g_cat(b, ARMOR_NAME[p_arm]); if (p_aench) { g_cat(b, " +"); g_cat_int(b, p_aench); }
    blit_icon(lx, ly + 38, IC_SHIELD); display_text(lx + 12, ly + 37, b, C_YELLOW);
    b[0] = '\0'; g_cat(b, "Crit "); g_cat_int(b, p_crit()); g_cat(b, "%  Dodge "); g_cat_int(b, p_dodge());
    g_cat(b, "%  Light "); g_cat_int(b, p_torch);
    display_text(x + 12, ly + 54, b, C_DIM);
    display_hline(x + 8, ly + 68, w - 16, C_EDGE);
    display_text(x + 12, ly + 74, "Perks:", C_DIM);
    int cx = x + 12, cy = ly + 88, any = 0;
    for (int k = 0; k < N_PERK; k++) {
        if (!perk[k]) continue;
        any = 1;
        b[0] = '\0'; g_cat(b, PERKS[k].name);
        if (perk[k] > 1) { g_cat(b, " "); g_cat_int(b, perk[k]); }
        int tw = g_tw(b) + 14;
        if (cx + tw > x + w - 8) { cx = x + 12; cy += 12; }
        blit_icon(cx, cy + 1, PERKS[k].icon);
        display_text(cx + 10, cy, b, C_TEXT);
        cx += tw + 6;
    }
    if (!any) display_text(cx, cy, "none yet - level up!", C_DIM);
    b[0] = '\0'; g_cat(b, "Kills "); g_cat_int(b, kills); g_cat(b, "  Turns "); g_cat_int(b, turns);
    g_cat(b, "  Score "); g_cat_int(b, score_now());
    display_text(x + 12, y + h - 16, b, C_DIM);
    display_flush();
    g_wait_key();
}

static void show_guide(void)
{
    static const char *const G[] = {
        "D-pad  move, bump to attack",
        "       (hold to keep walking)",
        "A      stairs/chest/shrine/altar,",
        "       take a wall torch, or wait",
        "B      drink a health potion",
        "X      bag: flasks, scrolls, bombs",
        "Y      your hero's skill",
        "",
        "Sleeping foes take double damage.",
        "Red squares: MOVE, an attack is",
        "coming. Light is a resource: buy",
        "oil, take torches. Reach B20 and",
        "claim the Akira Crystal!",
    };
    g_flush_input();
    backdrop();
    int w = 296, h = 214, x = (SCR_W - w) / 2, y = 26 - 2;
    panel(x, y, w, h, "GUIDE");
    for (int i = 0; i < (int)(sizeof(G) / sizeof(G[0])); i++)
        display_text(x + 14, y + 33 + i * 13, G[i], i < 7 ? C_TEXT : C_CYAN);
    display_flush();
    g_wait_key();
}

static int pause_menu(void)
{
    static const char *const L[] = { "Resume", "Map", "Hero", "Guide", "New run", "Quit to menu" };
    int n = 6, sel = 0;
    g_flush_input();
    for (;;) {
        backdrop();
        int w = 180, h = 36 + n * 20 + 8, x = (SCR_W - w) / 2, y = (SCR_H - h) / 2;
        panel(x, y, w, h, "PAUSED");
        for (int i = 0; i < n; i++) {
            int iy = y + 34 + i * 20;
            if (i == sel) sel_row(x + 8, iy - 2, w - 16, 18);
            g_text_c(x + w / 2, iy + 2, L[i], i == sel ? C_SELTXT : C_TEXT);
        }
        display_flush();
        uint32_t k = g_wait_key();
        if (k == K_UP) sel = (sel + n - 1) % n;
        else if (k == K_DOWN) sel = (sel + 1) % n;
        else if (k == K_HOME || k == K_B) return 0;
        else if (k == K_A) {
            if (sel == 0) return 0;
            if (sel == 1) show_map();
            else if (sel == 2) show_hero();
            else if (sel == 3) show_guide();
            else return sel == 4 ? 1 : 2;
        }
    }
}

/* ── Floor transitions ───────────────────────────────────────────────── */
static void floor_card(void)
{
    build_biome(BIOME());
    char b[16]; b[0] = '\0'; g_cat(b, "DEPTH "); g_cat_int(b, depth);
    for (int f = 0; f < 6; f++) {
        display_clear(C_BG);
        for (int x = 0; x < SCR_W; x += TS) {
            display_bitmap(x, SCR_H / 2 - 44, 16, 16, tile_px[TL_WALL_FACE], 512);
            display_bitmap(x, SCR_H / 2 + 30, 16, 16, tile_px[TL_WALL_TOP], 512);
        }
        g_text_large_c(SCR_W / 2, SCR_H / 2 - 18, b, C_TEXT);
        g_text_c(SCR_W / 2, SCR_H / 2 + 6, BIOMES[BIOME()].name, c_accent);
        if (depth % 5 == 0 && (f & 1)) g_text_c(SCR_W / 2, SCR_H / 2 + 54, "A great evil stirs here...", C_RED);
        display_flush();
        if (g_sleep_or_key(140)) break;
    }
    g_flush_input();
}

static void enter_floor(void)
{
    floor_card();
    sight_on = 0;
    p_keys = 0;
    gen_floor();
    camera_update(1);
    mstart(BIOMES[BIOME()].name); g_cat(mb, " - depth "); g_cat_int(mb, depth);
    msg(mb, c_accent);
    if (depth % 5 == 0) msg("A guardian seals the stairs.", C_RED);
}

static void descend(void)
{
    if (depth >= LAST_DEPTH) return;
    depth++;
    if (depth > best_depth) { best_depth = depth; g_save_int("pdungeon/deepest", best_depth); }
    enter_floor();
}

/* ── New run ─────────────────────────────────────────────────────────── */
static void new_run(int cls)
{
    const cls_t *c = &CLASSES[cls];
    pcls = cls;
    p_mhp = p_hp = c->hp; p_str = c->atk; p_def = c->def;
    p_lvl = 1; p_xp = 0; p_xpn = 27;
    p_gold = 0; p_wpn = c->wpn; p_wench = 0; p_arm = c->arm; p_aench = 0;
    p_torch = (cls == CL_MAGE) ? 5 : 4; p_fuel = TORCH_MAX; p_keys = 0;
    n_hpot = 2; n_bomb = (cls == CL_WARRIOR) ? 1 : 0; n_oil = 1;
    bclear(n_flask, sizeof(n_flask)); bclear(n_scroll, sizeof(n_scroll));
    bclear(flask_known, sizeof(flask_known)); bclear(perk, sizeof(perk));
    n_scroll[SC_TELEPORT] = 1;
    if (cls == CL_MAGE) { n_scroll[SC_FIREBALL] = 1; n_scroll[SC_FROST] = 1; }
    if (cls == CL_ROGUE) { n_bomb = 1; n_flask[rn(N_FLASK)] = 1; }
    for (int i = 0; i < N_FLASK; i++) flask_col[i] = (uint8_t)i;
    for (int i = N_FLASK - 1; i > 0; i--) { int j = rn(i + 1); uint8_t t = flask_col[i]; flask_col[i] = flask_col[j]; flask_col[j] = t; }
    pending_perks = 0; skill_cd = 0;
    haste_turns = invis_turns = root_turns = sight_on = bless_turns = poison_turns = burn_turns = 0;
    regen_ctr = 0; turns = 0; kills = 0; dead = won = quit_req = restart_req = 0;
    last_dir = 2; killer = "the dark";
    bclear(logm, sizeof(logm));
    depth = 1;
    enter_floor();
    msg("Find the stairs. Good luck!", C_TEXT);
}

/* ── Title & class select ────────────────────────────────────────────── */
/* 5x7 block letters for the logo */
static const char *logo_glyph(char c)
{
    switch (c) {
    case 'P': return "11110100011000111110100001000010000";
    case 'I': return "11111001000010000100001000010011111";
    case 'X': return "10001100010101000100010101000110001";
    case 'E': return "11111100001000011110100001000011111";
    case 'L': return "10000100001000010000100001000011111";
    case 'D': return "11110100011000110001100011000111110";
    case 'U': return "10001100011000110001100011000101110";
    case 'N': return "10001110011010110011100011000110001";
    case 'G': return "01111100001000010011100011000101111";
    case 'O': return "01110100011000110001100011000101110";
    default:  return "00000000000000000000000000000000000";
    }
}
static void draw_logo_word(const char *w, int cy, int s, uint16_t top, uint16_t bot)
{
    int n = strlen(w), tw = n * 6 * s - s;
    int x0 = SCR_W / 2 - tw / 2;
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < n; i++) {
            const char *g = logo_glyph(w[i]);
            for (int r = 0; r < 7; r++)
                for (int c = 0; c < 5; c++) {
                    if (g[r * 5 + c] != '1') continue;
                    int x = x0 + i * 6 * s + c * s, y = cy + r * s;
                    if (pass == 0) { if (!g_mono) display_rect(x + 2, y + 3, s, s, c565(60, 10, 10)); }
                    else {
                        display_rect(x, y, s, s, r < 3 ? top : bot);
                        if (!g_mono) display_hline(x, y, s, r < 3 ? 0xFFFF : top);
                    }
                }
        }
}

static int title_screen(void)
{
    build_biome(0);
    g_flush_input();
    for (;;) {
        display_clear(C_BG);
        for (int x = 0; x < SCR_W; x += TS) {
            display_bitmap(x, 0, 16, 16, tile_px[TL_WALL_TOP], 512);
            display_bitmap(x, 16, 16, 16, tile_px[(x / TS) % 5 == 2 ? ((anim_tick >> 1) & 1 ? TL_TORCH1 : TL_TORCH0)
                                                                    : TL_WALL_FACE], 512);
            display_bitmap(x, SCR_H - 16, 16, 16, tile_px[TL_FLOOR_A], 512);
        }
        draw_logo_word("PIXEL", 48, 5, C_YELLOW, C_ORANGE);
        draw_logo_word("DUNGEON", 92, 5, C_YELLOW, C_ORANGE);
        for (int i = 0; i < 3; i++)
            blit16(SCR_W / 2 - 40 + i * 28, 144 - (((anim_tick >> 2) + i) & 1), spr_px[CLASSES[i].spr]);
        g_text_c(SCR_W / 2, 132, "20 floors. 4 guardians. 1 crystal.", C_DIM);
        char b[40];
        b[0] = '\0'; g_cat(b, "Best "); g_cat_int(b, hi_score); g_cat(b, "   Deepest B"); g_cat_int(b, best_depth);
        if (total_wins) { g_cat(b, "   Wins "); g_cat_int(b, total_wins); }
        g_text_c(SCR_W / 2, 170, b, C_GOLD);
        if ((anim_tick >> 2) & 1) g_text_c(SCR_W / 2, 190, "Press A to begin", C_TEXT);
        g_text_c(SCR_W / 2, 206, "HOME: exit", C_DIM);
        display_flush();
        anim_tick++;
        uint32_t k = g_sleep_or_key(90);
        if (k == K_A) return 0;
        if (k == K_HOME || k == K_B) return -1;
    }
}

static int class_select(void)
{
    static int sel;
    g_flush_input();
    for (;;) {
        display_clear(C_BG);
        g_text_large_c(SCR_W / 2, 8, "CHOOSE YOUR HERO", C_YELLOW);
        int cw = (SCR_W - 24) / 3;
        for (int i = 0; i < N_CLASS; i++) {
            const cls_t *c = &CLASSES[i];
            int x = 8 + i * (cw + 4), y = 34;
            panel(x, y, cw, 128, 0);
            if (i == sel) {
                display_rounded_rect(x - 2, y - 2, cw + 4, 132, 7, C_YELLOW);
                display_rounded_rect(x - 1, y - 1, cw + 2, 130, 7, C_YELLOW);
            }
            display_bitmap(x + cw / 2 - 16, y + 10, 16, 16, tile_px[TL_FLOOR_A], 512);
            display_bitmap(x + cw / 2, y + 10, 16, 16, tile_px[TL_FLOOR_A], 512);
            display_bitmap(x + cw / 2 - 16, y + 26, 16, 16, tile_px[TL_FLOOR_A], 512);
            display_bitmap(x + cw / 2, y + 26, 16, 16, tile_px[TL_FLOOR_A], 512);
            int bob = (i == sel) ? ((anim_tick >> 2) & 1) : 0;
            blit_2x(x + cw / 2 - 16, y + 10 - bob, spr_px[c->spr], 16);
            g_text_c(x + cw / 2, y + 48, c->name, i == sel ? C_YELLOW : C_TEXT);
            char b[24];
            b[0] = '\0'; g_cat(b, "HP "); g_cat_int(b, c->hp);
            g_text_c(x + cw / 2, y + 64, b, C_GREEN);
            b[0] = '\0'; g_cat(b, "ATK "); g_cat_int(b, c->atk + WEAPON_ATK[c->wpn]);
            g_text_c(x + cw / 2, y + 76, b, C_RED);
            b[0] = '\0'; g_cat(b, "DEF "); g_cat_int(b, c->def + ARMOR_DEF[c->arm]);
            g_text_c(x + cw / 2, y + 88, b, C_CYAN);
            g_text_c(x + cw / 2, y + 106, c->skill, C_VIOLET);
        }
        g_text_c(SCR_W / 2, 172, CLASSES[sel].line1, C_TEXT);
        g_text_c(SCR_W / 2, 186, CLASSES[sel].line2, C_DIM);
        g_text_c(SCR_W / 2, 214, "LEFT/RIGHT choose   A start   B back", C_DIM);
        display_flush();
        anim_tick++;
        uint32_t k = g_sleep_or_key(120);
        if (k == K_LEFT) sel = (sel + N_CLASS - 1) % N_CLASS;
        else if (k == K_RIGHT) sel = (sel + 1) % N_CLASS;
        else if (k == K_A) return sel;
        else if (k == K_B || k == K_HOME) return -1;
    }
}

static void end_screen(void)
{
    int score = score_now();
    int best = score > hi_score;
    if (best) { hi_score = score; g_save_int("pdungeon/hiscore", hi_score); }
    if (won) { total_wins++; g_save_int("pdungeon/wins", total_wins); }
    g_flush_input();
    for (int t = 0;; t++) {
        display_clear(C_BG);
        char b[40];
        if (won) {
            g_text_large_c(SCR_W / 2, 14, "VICTORY!", C_YELLOW);
            blit_2x(SCR_W / 2 - 16, 40 - ((t >> 2) & 1), spr_px[SP_CRYSTAL], 16);
            for (int i = 0; i < 6; i++) {
                int a = (t * 3 + i * 40) % 240;
                display_pixel(SCR_W / 2 - 30 + a / 4, 40 + (i * 13 + t) % 40, C_CYAN);
            }
            g_text_c(SCR_W / 2, 78, "The Akira Crystal is yours.", C_CYAN);
        } else {
            g_text_large_c(SCR_W / 2, 14, "YOU DIED", C_RED);
            /* tombstone */
            int tx = SCR_W / 2 - 18;
            display_rounded_rect_fill(tx, 40, 36, 34, 10, g_mono ? 0xFFFF : pal565['g']);
            display_rect(tx, 54, 36, 22, g_mono ? 0xFFFF : pal565['g']);
            display_rect(tx - 6, 74, 48, 4, g_mono ? 0xFFFF : pal565['d']);
            display_text(tx + 8, 50, "RIP", g_mono ? 0x0000 : pal565['K']);
            b[0] = '\0'; g_cat(b, "Slain by "); g_cat(b, killer); g_cat(b, " on B"); g_cat_int(b, depth);
            g_text_c(SCR_W / 2, 84, b, C_TEXT);
        }
        b[0] = '\0'; g_cat(b, CLASSES[pcls].name); g_cat(b, "  Level "); g_cat_int(b, p_lvl);
        g_text_c(SCR_W / 2, 104, b, C_TEXT);
        b[0] = '\0'; g_cat(b, "Kills "); g_cat_int(b, kills); g_cat(b, "   Gold "); g_cat_int(b, p_gold);
        g_cat(b, "   Turns "); g_cat_int(b, turns);
        g_text_c(SCR_W / 2, 120, b, C_DIM);
        b[0] = '\0'; g_cat(b, "Score "); g_cat_int(b, score);
        g_text_large_c(SCR_W / 2, 140, b, C_GOLD);
        if (best) { if ((t >> 2) & 1) g_text_c(SCR_W / 2, 162, "NEW BEST!", C_YELLOW); }
        else { b[0] = '\0'; g_cat(b, "Best "); g_cat_int(b, hi_score); g_text_c(SCR_W / 2, 162, b, C_DIM); }
        g_text_c(SCR_W / 2, 200, "A: continue", (t >> 2) & 1 ? C_TEXT : C_DIM);
        display_flush();
        uint32_t k = g_sleep_or_key(120);
        if (t > 4 && (k == K_A || k == K_B || k == K_HOME)) return;
    }
}

/* ── Main loop ───────────────────────────────────────────────────────── */
static void run(int cls)
{
    new_run(cls);
    for (;;) {
        uint32_t k = g_key();
        int acted = 0;
        if (k & K_DIRS) {
            int d = k == K_UP ? 0 : k == K_RIGHT ? 1 : k == K_DOWN ? 2 : 3;
            int r = try_move(d);
            if (r == 2) { shop(); g_flush_input(); }
            else acted = r;
        } else if (k == K_A) acted = interact();
        else if (k == K_B) acted = quaff_health();
        else if (k == K_X) acted = bag_menu();
        else if (k == K_Y) acted = use_skill();
        else if (k == K_HOME) {
            int r = pause_menu();
            if (r == 1) { restart_req = 1; return; }
            if (r == 2) { quit_req = 1; return; }
        }
        if (acted && !dead && !won) end_turn();
        if (dead || won) {
            for (int f = 0; f < 8; f++) { frame(); delay(40000); }
            end_screen();
            return;
        }
        frame();
        while (pending_perks > 0 && !dead) { choose_perk(); pending_perks--; g_flush_input(); }
        delay(5000);
    }
}

int main(void)
{
    printf("AkiraOS Pixel Dungeon");
    g_init();
    SCR_W = G_W; SCR_H = G_H;
    g_mono = (SCR_W >= 400);
    VIEW_COLS = imin(SCR_W / TS, MAP_W);
    ui_init();
    build_global_sprites();
    hi_score = g_load_int("pdungeon/hiscore", 0);
    best_depth = g_load_int("pdungeon/deepest", 0);
    total_wins = g_load_int("pdungeon/wins", 0);
    g_seed_random();

    for (;;) {
        if (title_screen() < 0) break;
        for (;;) {                     /* after a death, straight back to heroes */
            int cls = class_select();
            if (cls < 0) break;
            do { g_seed_random(); run(cls); } while (restart_req);
            if (quit_req) break;
        }
    }
    app_switch("supervisor");
    return 0;
}
