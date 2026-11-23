/**
 * @file main.c
 * @brief Crystal Lair — guard the Akira Crystal from the heroes (AkiraOS)
 *
 * The other side of Pixel Dungeon: you are the lair.  Between waves, spend
 * souls on monsters and traps in a twisting maze; then the heroes march in
 * from the gate and try to steal shards of the crystal.  Monsters that kill
 * heroes grow veteran ranks; every third wave you pick a dark boon.  Hold
 * out for twenty waves to break the kingdom's will — or keep going.
 *
 * Controls (build phase):
 *   D-pad   move the cursor        A   build here / inspect & sell
 *   X       start the raid         Y   show/hide the heroes' route
 *   HOME    pause (guide, new lair, quit)
 * During a raid:  A  speed x1/x3,  HOME  pause.
 *
 * Heroes: Warrior, Rogue (spots and disarms traps), Mage (ranged), Cleric
 * (heals), Thief (sneaks around monsters), Knight (heavy, web-proof) and a
 * Champion every fifth wave.
 *
 * Rendering follows Pixel Dungeon: 16x16 indexed art expanded once into
 * RGB565 caches (common/akira_spr.h), one display_bitmap_transparent() per
 * sprite, black/white fallback for the 1-bit Sharp panel.  All simulation
 * runs before a frame starts drawing (draw calls arm a 50 ms auto-flush).
 *
 * @copyright Copyright (c) 2026 PenEngineering S.R.L
 * @license Apache-2.0
 */

#include "akira_api.h"
#include "../../common/akira_game.h"
#include "../../common/akira_spr.h"

/* ── Screen ──────────────────────────────────────────────────────────── */
static int32_t SCR_W = 320, SCR_H = 240;
#define TS     16
#define MAP_W  20
#define MAP_H  12
#define HUD_H  24
#define BAR_H  24
static int OX, OY = HUD_H;            /* map origin on screen */

/* ── Art shared with Pixel Dungeon ────────────────────────────────────── */
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
static const char A_CRYSTAL[] =
    "................" ".......kk......." "......kcck......" ".....kcwcck....."
    "....kcwwccbk...." "....kcwcccbk...." "...kcwccccbbk..." "...kcccccbbbk..."
    "...kbcccccbbk..." "....kbccbbbk...." "....kbbcbbbk...." ".....kbbbbk....."
    "......kbbk......" ".......kk......." ".....444444....." "................";
static const char A_WARDEN[] =
    "........................" ".........kkkkkk........." "........kzzzzzzk........" ".......kzGGzzzzZk......."
    ".......kzzzzzzzZk......." ".......kkkkkkkkkk......." ".......kZrkZZkrZk......." ".......kZZZZZZZZk......."
    ".....kkkkZZZZZZkkkk....." "....kzzzkkkkkkkkzzzk...." "...kzGzzkRRRRRRkzzGzk..." "...kzzzkkRyyyyRkkzzzk..."
    "...kzzk.kRRRRRRk.kzzk..." "...kzzk.kRRRRRRk.kzzk..." "...kkkk.kzzzzzzk.kkkk..." "...kGGk.kZZZZZZk.kGGk..."
    "...kGGk.kzzkkzzk.kGGk..." "...kkkk.kzzkkzzk.kkkk..." "........kZZkkZZk........" "........kzzkkzzk........"
    ".......kZZZkkZZZk......." ".......kkkkkkkkkk......." "........................" "........................";
static const char I_HEART[]  = ".kk..kk." "krrkkrwk" "krrrrrrk" "krrrrrrk" ".krrrrk." "..krrk.." "...kk..." "........";
static const char I_SKULL[]  = ".kkkkkk." "kvvvvvvk" "kvkvvkvk" "kvkvvkvk" "kvvvvvvk" ".kvkkvk." ".kvvvvk." "..kkkk..";
static const char I_SWORD[]  = "......kk" ".....kwk" "....kwk." "k..kwk.." "kkkwk..." ".kYk...." "kNkYk..." "kk.kk...";
static const char I_SHIELD[] = "kkkkkkkk" "kzzzzzzk" "kzGzzzZk" "kzzyyzZk" "kzzyyzZk" ".kzzzZk." "..kzZk.." "...kk...";
static const char I_FLAME[]  = "...k...." "..kyk..." "..kyok.." ".kyoork." "kyoyorok" "kowyyork" ".koyyok." "..kkkk..";
static const char I_FROST[]  = "...k...." ".k.c.k.." "..kck..." "kcccccck" "..kck..." ".k.c.k.." "...k...." "........";
static const char I_BLESS[]  = "...k...." "..kyk..." "kkkyykkk" "kyyyyyyk" ".kyyyyk." ".kyykyk." "kyk..kyk" "kk....kk";
static const char I_HASTE[]  = ".....kk." "....kyyk" "...kyyk." "..kyyyyk" ".kkkyyk." "...kyk.." "..kyk..." "..kk....";
static const char I_REGEN[]  = "..kkkk.." "..kllk.." "kkkllkkk" "klllllek" "keeeleek" "kkkllkkk" "..kelk.." "..kkkk..";
static const char I_SIGHT[]  = "........" "..kkkk.." ".kuuuuk." "kuuwwuuk" "kuuwkuuk" ".kuuuuk." "..kkkk.." "........";
static const char I_COIN[]   = "..kkkk.." ".kyyyyk." "kyyYyyyk" "kyYyyyYk" "kyyYyyYk" "kyyyyyYk" ".kYYYYk." "..kkkk..";

/* ── New art for the lair ─────────────────────────────────────────────── */
static const char A_CLERIC[] =
    "................" "......kkkk......" ".....kvvvvk....." "....kvvvvvVk...."
    "....kvsssskk...." "....kskssksk..k." "....kssssssk.kyk" "...kkvvvvvvkkkYk"
    "..kvvvvyyvvvvkNk" "..kvvvyyyyvvvkNk" "..kvvvvyyvvvvkNk" "..kVvvvyyvvvVk.N"
    "...kVvvvvvvVk..N" "...kkVVVVVVkk..k" "....kNNkkNNk...." "....kkk..kkk....";
static const char A_THIEF[] =
    "................" "......kkkk......" ".....kUUUUk....." "....kUUUUUUk...."
    "....kUkkkkUk...." "....kUkwkwUk...." "....kUKKKKUk...." "...kkUUUUUUkk..."
    "..kUUkKKKKkUUk.." "..kUkKKddKKkUkk." "..kkkKKKKKKkkGk." "...kKKKKKKKKkGk."
    "...kKKKkkKKKk.k." "....kKKkkKKk...." "....kNNk.kNNk..." "....kkk..kkk....";
static const char A_KNIGHT[] =
    ".......rr......." "......krRk......" ".....kzzzzk....." "....kzGzzzZk...."
    "....kzkkkkZk...." "....kZkrrkZk...." "....kzzzzzZk...." "..kkkkzzzzkkkk.."
    ".kbbbkzGzzkzzzk." ".kbybkzzzzkzGzk." ".kbyybkzzZZkzzk." ".kbybkzzzzkkkk.."
    ".kbbbkZZZZk....." "..kkkkzzkzzk...." ".....kZZkZZk...." ".....kkk.kkk....";
/* Traps (overlays baked onto the floor tile) */
static const char A_SPIKES[] =
    "................" "................" "................" "................"
    "...k....k....k.." "..kzk..kzk..kzk." "..kzk..kzk..kzk." ".kzGzkkzGzkkzGzk"
    ".kzzzkkzzzkkzzzk" ".kZZZkkZZZkkZZZk" "kkkkkkkkkkkkkkkk" "kZZZZZZZZZZZZZZk"
    "kkkkkkkkkkkkkkkk" "4444444444444444" "................" "................";
static const char A_SPIKES_DOWN[] =
    "................" "................" "................" "................"
    "................" "................" "................" "................"
    "................" "................" "kkkkkkkkkkkkkkkk" "kZZkZZZkZZZZkZZk"
    "kkkkkkkkkkkkkkkk" "4444444444444444" "................" "................";
static const char A_FLAMEVENT[] =
    "................" "................" "................" "................"
    "................" "....kkkkkkkk...." "...kZZZZZZZZk..." "..kZkokokokoZk.."
    "..kZokokokokZk.." "..kZkokokokoZk.." "..kZokokokokZk.." "...kZZZZZZZZk..."
    "....kkkkkkkk...." "....44444444...." "................" "................";
static const char A_FROSTRUNE[] =
    "................" "................" "................" ".....cccccc....."
    "....c......c...." "...c..i..i..c..." "...c...ii...c..." "...c.iiiiii.c..."
    "...c...ii...c..." "...c..i..i..c..." "....c......c...." ".....cccccc....."
    "................" "................" "................" "................";
static const char A_PIT[] =
    "................" "................" "................" "..kkkkkkkkkkkk.."
    "..knnnnnnnnnnk.." "..knkkkkkkkknk.." "..knkKKKKKKknk.." "..knkKkkkkKknk.."
    "..knkKkkkkKknk.." "..knkKKKKKKknk.." "..knkkkkkkkknk.." "..knnnnnnnnnnk.."
    "..kkkkkkkkkkkk.." "................" "................" "................";
static const char A_MINE[] =
    "................" "................" "................" "................"
    "................" "................" "................" "......kkkk......"
    ".....kKrrKk....." "....kKKKKKKk...." "...kKdKKKKKKk..." "...kKKKKKKKKk..."
    "..kkkkkkkkkkkk.." "...4444444444..." "................" "................";
static const char A_GATE[] =
    "....kkkkkkkk...." "..kkyyyyyyyykk.." ".kyywwwwwwwwyyk." ".kywwwwwwwwwwyk."
    ".kywwwwwwwwwwyk." ".kywwwwwwwwwwyk." ".kywwwwwwwwwwyk." ".kywwwwwwwwwwyk."
    ".kywwwwwwwwwwyk." ".kywwwwwwwwwwyk." ".kywwwwwwwwwwyk." ".kywwwwwwwwwwyk."
    ".kywwwwwwwwwwyk." ".kywwwwwwwwwwyk." ".kywwwwwwwwwwyk." ".kyyyyyyyyyyyyk.";
/* the crystal on its pedestal (full tile, baked onto floor) */
static const char A_ALTAR_CRYS[] =
    "......kk........" ".....kcck......." "....kcwcck......" "....kcwccbk....."
    "...kcwcccbbk...." "...kccccbbbk...." "...kbcccbbbk...." "....kbccbbk....."
    ".....kbbbk......" "..kkkkkkkkkkk..." "..kGGGGGGGGGk..." "...kgggggggk...."
    "...kgGgggggk...." "..kGGGGGGGGGk..." "..kkkkkkkkkkk..." "...444444444....";
static const char I_SOUL[] = "...uu..." "..uccu.." ".ucwwcu." ".ucwwcu." "..ucccu." "...uucu." "....uu.." "...u....";
static const char I_CRYS[] = "...kk..." "..kcck.." ".kcwcbk." ".kccbbk." ".kcbbbk." "..kbbk.." "...kk..." "........";
static const char I_LOCK[] = "..kkkk.." ".kk..kk." ".k....k." "kkkkkkkk" "kyyyyyyk" "kyykkyyk" "kyyyyyyk" "kkkkkkkk";
static const char I_STAR[] = "...y...." "...y...." "yyyyyyy." ".yyyyy.." "..yyy..." ".yy.yy.." "y.....y." "........";

/* ── Sprite registry ─────────────────────────────────────────────────── */
enum {
    TL_FLOOR_A, TL_FLOOR_B, TL_FLOOR_C, TL_WALL_FACE, TL_WALL_FACE_B, TL_WALL_TOP,
    TL_TORCH0, TL_TORCH1, TL_GATE, TL_CRYSTAL,
    TL_SPIKES, TL_SPIKES_DOWN, TL_FLAMEVENT, TL_FROSTRUNE, TL_PIT, TL_MINE, TL_COUNT
};
static const char *const TILE_SRC[TL_COUNT] = {
    A_FLOOR_A, A_FLOOR_B, A_FLOOR_C, A_WALL_FACE, A_WALL_FACE_B, A_WALL_TOP,
    A_TORCH0, A_TORCH1, A_GATE, A_ALTAR_CRYS,
    A_SPIKES, A_SPIKES_DOWN, A_FLAMEVENT, A_FROSTRUNE, A_PIT, A_MINE,
};
#define TL_FIRST_OVERLAY TL_GATE

enum {
    SP_WARRIOR, SP_ROGUE, SP_MAGE, SP_CLERIC, SP_THIEF, SP_KNIGHT,
    SP_RAT, SP_GOBLIN, SP_ARCHER, SP_SPIDER, SP_SLIME, SP_SLIMELET, SP_SKELETON,
    SP_SHAMAN, SP_IMP, SP_GOLEM,
    SP_BLOOD, SP_BONES, SP_SCORCH, SP_WEB, SP_FIRE0, SP_FIRE1, SP_CRYSTAL, SP_COUNT
};
static const char *const SPR_SRC[SP_COUNT] = {
    A_WARRIOR, A_ROGUE, A_MAGE, A_CLERIC, A_THIEF, A_KNIGHT,
    A_RAT, A_GOBLIN, A_ARCHER, A_SPIDER, A_SLIME, A_SLIMELET, A_SKELETON,
    A_SHAMAN, A_IMP, A_GOLEM,
    A_BLOOD, A_BONES, A_SCORCH, A_WEB, A_FIRE0, A_FIRE1, A_CRYSTAL,
};
enum {
    IC_SOUL, IC_CRYS, IC_LOCK, IC_STAR, IC_HEART, IC_SKULL, IC_SWORD, IC_SHIELD,
    IC_FLAME, IC_FROST, IC_BLESS, IC_HASTE, IC_REGEN, IC_SIGHT, IC_COIN, IC_COUNT
};
static const char *const ICON_SRC[IC_COUNT] = {
    I_SOUL, I_CRYS, I_LOCK, I_STAR, I_HEART, I_SKULL, I_SWORD, I_SHIELD,
    I_FLAME, I_FROST, I_BLESS, I_HASTE, I_REGEN, I_SIGHT, I_COIN,
};

static uint16_t tile_px[TL_COUNT][256];
static uint16_t spr_c[SP_COUNT][256];
static uint16_t champ_px[576];
static uint16_t icon_px[IC_COUNT][64];
static uint16_t veil_frost[256], veil_dim[256];
static uint16_t scratch[1024];

/* Lair palette: obsidian walls, slate floor, crimson accents. */
static const uint8_t LAIR_RGB[12][3] = {
    {10, 6, 16}, {44, 30, 58}, {76, 54, 96}, {124, 92, 146},
    {28, 24, 34}, {52, 46, 60}, {76, 68, 84},
    {84, 34, 52}, {136, 56, 80}, {255, 206, 130},
    {150, 40, 210}, {220, 130, 255},
};
static const uint8_t LAIR_BW[12] = {0, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 1};
static const char SLOT_CH[12] = {'0','1','2','3','4','5','6','7','8','9','x','X'};
static uint16_t edge_col;

static void build_sprites(void)
{
    spr_init(SCR_W >= 400);
    for (int i = 0; i < 12; i++)
        spr_pal_set(SLOT_CH[i], LAIR_RGB[i][0], LAIR_RGB[i][1], LAIR_RGB[i][2], LAIR_BW[i]);
    spr_build(tile_px[TL_FLOOR_A], TILE_SRC[TL_FLOOR_A], 256, 0);
    for (int t = 1; t < TL_COUNT; t++)
        spr_build(tile_px[t], TILE_SRC[t], 256, t >= TL_FIRST_OVERLAY ? tile_px[TL_FLOOR_A] : 0);
    for (int i = 0; i < SP_COUNT; i++) spr_build(spr_c[i], SPR_SRC[i], 256, 0);
    spr_build(champ_px, A_WARDEN, 576, 0);
    for (int i = 0; i < IC_COUNT; i++) spr_build(icon_px[i], ICON_SRC[i], 64, 0);
    spr_dither16(veil_frost, 38, spr_col(190, 236, 255));
    spr_dither16(veil_dim, 60, 0x0000);
    edge_col = tile_px[TL_WALL_FACE][0];
    spr_theme_init(200, 90, 255);
}

static void blit_icon(int x, int y, int ic) { spr_blit(x, y, 8, 8, icon_px[ic]); }

/* ── Definitions ─────────────────────────────────────────────────────── */
enum { M_RAT, M_GOBLIN, M_ARCHER, M_SPIDER, M_SLIME, M_SKELETON, M_SHAMAN, M_IMP, M_GOLEM,
       M_SLIMELET, N_MON };
typedef struct {
    const char *name, *desc;
    uint8_t spr, cost, hp, atk, def, range, rate, leash, unlock;
} mdef_t;
static const mdef_t MDEF[N_MON] = {
    {"Rat",      "Cheap and quick.",        SP_RAT,      10,  8,  3, 0, 1, 2, 2, 1},
    {"Goblin",   "A solid melee guard.",    SP_GOBLIN,   20, 18,  5, 1, 1, 2, 2, 1},
    {"Archer",   "Shoots heroes in sight.", SP_ARCHER,   30, 10,  4, 0, 4, 3, 0, 1},
    {"Spider",   "Bites tangle heroes.",    SP_SPIDER,   30, 14,  4, 0, 1, 2, 2, 2},
    {"Slime",    "Splits in two on death.", SP_SLIME,    35, 25,  4, 1, 1, 2, 1, 3},
    {"Skeleton", "Rises once more.",        SP_SKELETON, 40, 20,  6, 2, 1, 2, 2, 4},
    {"Shaman",   "Heals nearby monsters.",  SP_SHAMAN,   45, 12,  3, 0, 3, 3, 0, 5},
    {"Imp",      "Fast. Sets heroes alight.", SP_IMP,    50, 16,  6, 0, 1, 1, 3, 6},
    {"Golem",    "Slow, tough, crushing.",  SP_GOLEM,    75, 70, 11, 4, 1, 4, 1, 8},
    {"Slimelet", "",                        SP_SLIMELET,  0,  9,  2, 0, 1, 2, 2, 99},
};

enum { TR_NONE, TR_SPIKES, TR_WEB, TR_FLAME, TR_PIT, TR_FROST, TR_MINE, N_TRAP };
typedef struct { const char *name, *desc; uint8_t tile, cost, cd, unlock; } tdef_t;
static const tdef_t TDEF[N_TRAP] = {
    {"", "", 0, 0, 0, 99},
    {"Spikes",     "Stab each hero who steps.", TL_SPIKES,    15,  3, 1},
    {"Web",        "Roots a hero in place.",    TL_FLOOR_A,   20,  6, 2},
    {"Flame Vent", "A burst of fire, 3x3.",     TL_FLAMEVENT, 40,  8, 3},
    {"Pit",        "Drops a hero at the gate.", TL_PIT,       60, 12, 4},
    {"Frost Rune", "Freezes a hero solid.",     TL_FROSTRUNE, 40,  8, 5},
    {"Mine",       "One big blast, 3x3.",       TL_MINE,      25,  0, 6},
};

typedef struct { uint8_t kind, type; } bentry_t;   /* kind 0 monster, 1 trap */
static const bentry_t BUILD[] = {
    {0, M_RAT}, {0, M_GOBLIN}, {0, M_ARCHER}, {1, TR_SPIKES}, {0, M_SPIDER}, {1, TR_WEB},
    {0, M_SLIME}, {1, TR_FLAME}, {0, M_SKELETON}, {1, TR_PIT}, {0, M_SHAMAN}, {1, TR_FROST},
    {0, M_IMP}, {1, TR_MINE}, {0, M_GOLEM},
};
#define N_BUILD ((int)(sizeof(BUILD) / sizeof(BUILD[0])))

enum { H_WARRIOR, H_ROGUE, H_MAGE, H_CLERIC, H_THIEF, H_KNIGHT, H_CHAMPION, N_HERO };
#define HF_SENSE  1
#define HF_SNEAK  2
#define HF_RANGED 4
#define HF_HEAL   8
#define HF_HEAVY  16
#define HF_BOSS   32
typedef struct {
    const char *name;
    uint8_t spr, hp, atk, def, mv, range, value, steal, unlock, flags;
} hdef_t;
static const hdef_t HDEF[N_HERO] = {
    {"Warrior",  SP_WARRIOR, 16, 4, 1, 2, 1,  8, 1, 1, 0},
    {"Rogue",    SP_ROGUE,   11, 3, 0, 1, 1,  9, 1, 1, HF_SENSE},
    {"Mage",     SP_MAGE,     9, 4, 0, 2, 3, 10, 1, 2, HF_RANGED},
    {"Cleric",   SP_CLERIC,  12, 2, 1, 2, 3, 10, 1, 3, HF_HEAL},
    {"Thief",    SP_THIEF,    8, 2, 0, 1, 1,  8, 1, 4, HF_SNEAK},
    {"Knight",   SP_KNIGHT,  34, 6, 3, 3, 1, 16, 1, 6, HF_HEAVY},
    {"Champion", 0,         110, 9, 4, 3, 1, 50, 3, 5, HF_HEAVY | HF_BOSS},
};

enum { PK_TRAPS, PK_HIDE, PK_BLOOD, PK_HARVEST, PK_WARD, PK_CHEAP, PK_SIGHT, PK_QUICK,
       PK_DRILL, PK_REGEN, N_PERK };
typedef struct { const char *name, *d1, *d2; uint8_t icon, max; } perk_t;
static const perk_t PERKS[N_PERK] = {
    {"Sharp Traps", "Traps deal",  "+50% damage", IC_SWORD,  3},
    {"Thick Hide",  "Monsters",    "+25% HP",     IC_SHIELD, 4},
    {"Bloodlust",   "Monsters",    "+20% attack", IC_SKULL,  4},
    {"Soul Reaper", "+25% souls",  "per kill",    IC_SOUL,   3},
    {"Warding",     "+3 shards",   "max and now", IC_CRYS,   3},
    {"Cheap Labor", "Builds cost", "15% less",    IC_COIN,   2},
    {"Eagle Eye",   "Ranged mons", "+1 range",    IC_SIGHT,  2},
    {"Quick Traps", "Traps reset", "faster",      IC_HASTE,  2},
    {"Drill Hall",  "Recruits",    "are rank 2",  IC_STAR,   1},
    {"Regrowth",    "Monsters",    "regenerate",  IC_REGEN,  2},
};

#define WIN_WAVE 20

/* ── State ───────────────────────────────────────────────────────────── */
enum { T_WALL, T_FLOOR, T_GATE, T_CRYSTAL };
enum { DC_NONE, DC_BLOOD, DC_BONES, DC_SCORCH };
static uint8_t map[MAP_H][MAP_W], trap[MAP_H][MAP_W], tcd[MAP_H][MAP_W], tseen[MAP_H][MAP_W];
static uint8_t fire[MAP_H][MAP_W], decal[MAP_H][MAP_W], torch[MAP_H][MAP_W];
static int8_t occ_m[MAP_H][MAP_W], occ_h[MAP_H][MAP_W];   /* index + 1, 0 = empty */
static uint16_t cost[3][MAP_H][MAP_W];
#define GATE_X 0
#define GATE_Y 5
#define CRYS_X 17
#define CRYS_Y 5

#define MAX_MON 48
typedef struct {
    int8_t x, y, hx, hy, ldx, ldy;
    uint8_t type, alive, lvl, kills, cd, flash, lunge, revived, pile, regen;
    int16_t hp, mhp;
} mon_t;
static mon_t mons[MAX_MON];

#define MAX_HERO 18
typedef struct {
    int8_t x, y, ldx, ldy;
    uint8_t type, alive, cd, root, frozen, burn, flash, lunge, hcd;
    int16_t hp, mhp, atk;
    int8_t def;
} hero_t;
static hero_t heroes[MAX_HERO];
static uint8_t wave_list[MAX_HERO];
static int wave_n;

static int souls, wave, shards, max_shards, kills, cleared, perk[N_PERK];
static int phase, cur_x, cur_y, show_route, fast, spawn_i, spawn_t, won_once;
static int best_wave, best_score, quit_req, restart_req;
static uint32_t tick, anim_tick;
static int shake, flash_crys, banner_t;
enum { PH_BUILD, PH_RAID };

/* fx */
#define MAX_PART 56
typedef struct { int16_t x, y, vx, vy; uint16_t col; uint8_t life, grav; } part_t;
static part_t parts[MAX_PART];
#define MAX_PROJ 6
enum { PJ_ARROW, PJ_ZAP, PJ_MAGIC, PJ_HEAL };
typedef struct { int16_t x0, y0, x1, y1; uint8_t t, dur, kind; } proj_t;
static proj_t projs[MAX_PROJ];
#define MAX_FLOAT 10
typedef struct { int16_t x, y; uint16_t col; uint8_t t; char txt[8]; } fl_t;
static fl_t floats[MAX_FLOAT];
static char msgbuf[44];
static uint16_t msgcol;
static uint8_t msg_t;

/* ── Helpers ─────────────────────────────────────────────────────────── */
static int rn(int n) { return n > 0 ? (int)g_rand_n((uint32_t)n) : 0; }
static int chance(int p) { return rn(100) < p; }
static int iabs(int v) { return v < 0 ? -v : v; }
static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }
static int in_map(int x, int y) { return x >= 0 && y >= 0 && x < MAP_W && y < MAP_H; }
static void bclear(void *p, int n) { uint8_t *b = p; for (int i = 0; i < n; i++) b[i] = 0; }
static void scopy(char *d, const char *s, int n) { int i = 0; for (; s[i] && i < n - 1; i++) d[i] = s[i]; d[i] = 0; }
static const int8_t DX[4] = {0, 1, 0, -1}, DY[4] = {-1, 0, 1, 0};

static void msg(const char *s, uint16_t c) { scopy(msgbuf, s, 44); msgcol = c; msg_t = 40; }
static char mb[48];
static char *mstart(const char *s) { mb[0] = 0; return g_cat(mb, s); }

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
static void burst(int tx, int ty, int n, uint16_t col, int sp, int grav)
{
    int cx = tx * TS + 8, cy = ty * TS + 8;
    for (int i = 0; i < n; i++)
        spawn_part(cx + rn(5) - 2, cy + rn(5) - 2, rn(sp * 2 + 1) - sp, rn(sp * 2 + 1) - sp - (grav ? sp : 0),
                   col, 4 + rn(5), grav);
}
static void spawn_float(int tx, int ty, const char *s, uint16_t col)
{
    int b = 0;
    for (int i = 0; i < MAX_FLOAT; i++) { if (!floats[i].t) { b = i; break; } if (floats[i].t < floats[b].t) b = i; }
    floats[b].x = (int16_t)(tx * TS + 8 - g_tw(s) / 2);
    floats[b].y = (int16_t)(ty * TS - 2);
    for (int i = 0; i < MAX_FLOAT; i++)
        if (i != b && floats[i].t > 8 && iabs(floats[i].y - floats[b].y) < 6 && iabs(floats[i].x - floats[b].x) < 12)
            floats[b].y -= 8;
    floats[b].col = col; floats[b].t = 12;
    scopy(floats[b].txt, s, 8);
}
static void float_num(int tx, int ty, int v, uint16_t col, const char *pre)
{
    char b[8]; b[0] = 0; g_cat(b, pre); g_cat_int(b, v); spawn_float(tx, ty, b, col);
}
static void spawn_proj(int x0, int y0, int x1, int y1, int kind)
{
    for (int i = 0; i < MAX_PROJ; i++) {
        if (projs[i].dur) continue;
        int d = imax(iabs(x1 - x0), iabs(y1 - y0));
        projs[i].x0 = (int16_t)(x0 * TS + 8); projs[i].y0 = (int16_t)(y0 * TS + 8);
        projs[i].x1 = (int16_t)(x1 * TS + 8); projs[i].y1 = (int16_t)(y1 * TS + 8);
        projs[i].t = 0; projs[i].dur = (uint8_t)(d < 2 ? 1 : (d + 1) / 2 > 4 ? 4 : (d + 1) / 2);
        projs[i].kind = (uint8_t)kind;
        return;
    }
}

static uint32_t thash(int x, int y)
{
    uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u ^ 0x9E3779B9u;
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return h;
}

/* ── Map: a braided maze with a gate hall and a crystal chamber ──────── */
static void carve_rect(int x0, int y0, int x1, int y1)
{
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) map[y][x] = T_FLOOR;
}

static void gen_map(void)
{
    bclear(map, sizeof(map)); bclear(trap, sizeof(trap)); bclear(tcd, sizeof(tcd));
    bclear(decal, sizeof(decal)); bclear(fire, sizeof(fire)); bclear(torch, sizeof(torch));
    /* a maze of 7x5 cells on the left (x 1..13), the crystal chamber on the right */
    const int CW = 7, CH = 5;
    uint8_t done[5][7];
    int stack[35], sp = 0;
    bclear(done, sizeof(done));
    stack[sp++] = 2 * CW; done[2][0] = 1;
    map[5][1] = T_FLOOR;
    while (sp) {
        int c = stack[sp - 1], cx = c % CW, cy = c / CW;
        int opts[4], no = 0;
        for (int k = 0; k < 4; k++) {
            int nx = cx + DX[k], ny = cy + DY[k];
            if (nx >= 0 && ny >= 0 && nx < CW && ny < CH && !done[ny][nx]) opts[no++] = k;
        }
        if (!no) { sp--; continue; }
        int k = opts[rn(no)], nx = cx + DX[k], ny = cy + DY[k];
        map[1 + 2 * cy + DY[k]][1 + 2 * cx + DX[k]] = T_FLOOR;
        map[1 + 2 * ny][1 + 2 * nx] = T_FLOOR;
        done[ny][nx] = 1;
        stack[sp++] = ny * CW + nx;
    }
    /* braid a third of the dead ends so routes branch and loop */
    for (int cy = 0; cy < CH; cy++)
        for (int cx = 0; cx < CW; cx++) {
            int x = 1 + 2 * cx, y = 1 + 2 * cy, open = 0;
            for (int k = 0; k < 4; k++) if (map[y + DY[k]][x + DX[k]] == T_FLOOR) open++;
            if (open != 1 || !chance(30)) continue;
            int s = rn(4);
            for (int j = 0; j < 4; j++) {
                int k = (s + j) & 3, nx = cx + DX[k], ny = cy + DY[k];
                if (nx < 0 || ny < 0 || nx >= CW || ny >= CH) continue;
                if (map[y + DY[k]][x + DX[k]] == T_WALL) { map[y + DY[k]][x + DX[k]] = T_FLOOR; break; }
            }
        }
    /* one small hall in the maze */
    {
        int x = 2 * (2 + rn(4)), y = 2 * (1 + rn(4));
        carve_rect(x - 1, y - 1, x + 1, y + 1);
    }
    carve_rect(1, 4, 2, 6);                  /* gate hall */
    carve_rect(15, 4, 17, 6);                /* crystal chamber, walled in */
    map[5][14] = T_FLOOR;                    /* its west door */
    if (rn(2)) {                             /* and a long way round */
        carve_rect(14, 1, 16, 1); carve_rect(16, 2, 16, 3);
    } else {
        carve_rect(14, 9, 16, 9); carve_rect(16, 7, 16, 8);
    }
    map[GATE_Y][GATE_X] = T_GATE;
    map[CRYS_Y][CRYS_X] = T_CRYSTAL;
    for (int y = 0; y < MAP_H - 1; y++)
        for (int x = 0; x < MAP_W; x++)
            if (map[y][x] == T_WALL && map[y + 1][x] == T_FLOOR && chance(14)) torch[y][x] = 1;
}

static int walk_hero(int x, int y)
{
    if (!in_map(x, y)) return 0;
    int t = map[y][x];
    return t == T_FLOOR || t == T_GATE || t == T_CRYSTAL;
}
static int buildable(int x, int y)
{
    return in_map(x, y) && map[y][x] == T_FLOOR && x > 2;
}
static int los_clear(int x0, int y0, int x1, int y1)
{
    int dx = iabs(x1 - x0), dy = iabs(y1 - y0), sx = x1 > x0 ? 1 : -1, sy = y1 > y0 ? 1 : -1;
    int x = x0, y = y0, err = dx - dy;
    while (1) {
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x += sx; }
        if (e2 < dx) { err += dx; y += sy; }
        if (x == x1 && y == y1) return 1;
        if (map[y][x] == T_WALL) return 0;
    }
}

/* ── Derived stats ───────────────────────────────────────────────────── */
static int mon_mhp(const mon_t *m)
{
    return MDEF[m->type].hp * (100 + 35 * (m->lvl - 1) + 25 * perk[PK_HIDE]) / 100;
}
static int mon_atk(const mon_t *m)
{
    return MDEF[m->type].atk * (100 + 35 * (m->lvl - 1) + 20 * perk[PK_BLOOD]) / 100;
}
static int mon_def(const mon_t *m) { return MDEF[m->type].def + (m->lvl - 1); }
static int mon_range(const mon_t *m)
{
    int r = MDEF[m->type].range;
    return r > 1 ? r + perk[PK_SIGHT] : r;
}
static int build_cost(int kind, int type)
{
    int c = kind ? TDEF[type].cost : MDEF[type].cost;
    return c * (100 - 15 * perk[PK_CHEAP]) / 100;
}
static int unlocked(int kind, int type) { return wave >= (kind ? TDEF[type].unlock : MDEF[type].unlock); }
static int roll(int a) { return a - rn(a / 3 + 1); }
static int score_now(void) { return cleared * 100 + kills * 5 + shards * 20; }

/* ── Occupancy and the heroes' cost maps ─────────────────────────────── */
static void rebuild_occ(void)
{
    bclear(occ_m, sizeof(occ_m)); bclear(occ_h, sizeof(occ_h));
    for (int i = 0; i < MAX_MON; i++)
        if (mons[i].alive && !mons[i].pile) occ_m[mons[i].y][mons[i].x] = (int8_t)(i + 1);
    for (int i = 0; i < MAX_HERO; i++)
        if (heroes[i].alive) occ_h[heroes[i].y][heroes[i].x] = (int8_t)(i + 1);
}

/* profile 0: plain heroes, 1: rogue (avoids spotted traps), 2: thief (avoids monsters) */
static int guarded(int x, int y)
{
    for (int k = 0; k < 4; k++) {
        int nx = x + DX[k], ny = y + DY[k];
        if (in_map(nx, ny) && occ_m[ny][nx] && MDEF[mons[occ_m[ny][nx] - 1].type].range <= 1) return 1;
    }
    return 0;
}
static int enter_cost(int p, int x, int y)
{
    int c = 1;
    if (occ_m[y][x]) c += (p == 2) ? 14 : 4;
    else if (p != 2 && guarded(x, y)) c += 2;
    if (p == 1 && tseen[y][x] && trap[y][x]) c += 6;
    return c;
}

static void calc_costs(void)
{
    static uint8_t q[256], inq[MAP_H * MAP_W];
    for (int p = 0; p < 3; p++) {
        uint16_t (*d)[MAP_W] = cost[p];
        for (int y = 0; y < MAP_H; y++) for (int x = 0; x < MAP_W; x++) d[y][x] = 0xFFFF;
        bclear(inq, sizeof(inq));
        int h = 0, t = 0;
        d[CRYS_Y][CRYS_X] = 0;
        q[t++] = (uint8_t)(CRYS_Y * MAP_W + CRYS_X); inq[CRYS_Y * MAP_W + CRYS_X] = 1;
        while (h != t) {
            int c = q[h]; h = (h + 1) & 255;
            int cx = c % MAP_W, cy = c / MAP_W;
            inq[c] = 0;
            int nc = d[cy][cx] + enter_cost(p, cx, cy);
            for (int k = 0; k < 4; k++) {
                int nx = cx + DX[k], ny = cy + DY[k];
                if (!walk_hero(nx, ny) || d[ny][nx] <= nc) continue;
                d[ny][nx] = (uint16_t)nc;
                int ni = ny * MAP_W + nx;
                if (!inq[ni]) { inq[ni] = 1; q[t] = (uint8_t)ni; t = (t + 1) & 255; }
            }
        }
    }
}
static int hero_profile(int type)
{
    if (HDEF[type].flags & HF_SENSE) return 1;
    if (HDEF[type].flags & HF_SNEAK) return 2;
    return 0;
}

/* ── Monsters ────────────────────────────────────────────────────────── */
static int new_mon(int type, int x, int y)
{
    for (int i = 0; i < MAX_MON; i++) {
        if (mons[i].alive) continue;
        mon_t *m = &mons[i];
        bclear(m, sizeof(*m));
        m->type = (uint8_t)type; m->alive = 1;
        m->x = m->hx = (int8_t)x; m->y = m->hy = (int8_t)y;
        m->lvl = (uint8_t)(1 + (perk[PK_DRILL] && type != M_SLIMELET));
        m->kills = (uint8_t)(m->lvl > 1 ? 3 : 0);
        m->hp = m->mhp = (int16_t)mon_mhp(m);
        m->cd = (uint8_t)rn(2);
        occ_m[y][x] = (int8_t)(i + 1);
        return i;
    }
    return -1;
}

static void mon_gain_kill(int i)
{
    mon_t *m = &mons[i];
    if (!m->alive || m->kills >= 250) return;
    m->kills++;
    int lvl = m->kills >= 8 ? 3 : m->kills >= 3 ? 2 : 1;
    if (lvl > m->lvl) {
        int old = m->mhp;
        m->lvl = (uint8_t)lvl;
        m->mhp = (int16_t)mon_mhp(m);
        m->hp += (int16_t)(m->mhp - old);
        spawn_float(m->x, m->y, "RANK UP", SPRT.yellow);
        burst(m->x, m->y, 8, SPRT.yellow, 4, 0);
    }
}

static void spawn_near_mon(int type, int x, int y)
{
    int s = rn(4);
    for (int j = 0; j < 4; j++) {
        int k = (s + j) & 3, nx = x + DX[k], ny = y + DY[k];
        if (!in_map(nx, ny) || map[ny][nx] != T_FLOOR || occ_m[ny][nx] || occ_h[ny][nx]) continue;
        new_mon(type, nx, ny);
        return;
    }
}

static void kill_mon(int i)
{
    mon_t *m = &mons[i];
    int x = m->x, y = m->y;
    occ_m[y][x] = 0;
    burst(x, y, 9, m->type == M_SKELETON ? spr_px('v') : (m->type == M_SLIME || m->type == M_SLIMELET)
                       ? spr_px('l') : spr_col(200, 30, 40), 6, 1);
    if (m->type == M_SKELETON && !m->revived) {
        m->pile = 1; m->cd = 10; m->hp = (int16_t)(m->mhp / 2);
        return;
    }
    m->alive = 0;
    if (!decal[y][x]) decal[y][x] = DC_BONES;
    if (m->type == M_SLIME) {
        spawn_near_mon(M_SLIMELET, x, y);
        spawn_near_mon(M_SLIMELET, x, y);
    }
    mstart("Your "); g_cat(mb, MDEF[m->type].name); g_cat(mb, " falls."); msg(mb, SPRT.dim);
}

/* ── Heroes ──────────────────────────────────────────────────────────── */
static void hurt_mon(int i, int dmg, int by_hero)
{
    mon_t *m = &mons[i];
    if (dmg < 1) dmg = 1;
    m->hp -= (int16_t)dmg;
    m->flash = 2;
    float_num(m->x, m->y, dmg, 0xFFFF, "");
    (void)by_hero;
    if (m->hp <= 0) kill_mon(i);
}

static void kill_hero(int i, int killer_mon)
{
    hero_t *h = &heroes[i];
    const hdef_t *d = &HDEF[h->type];
    h->alive = 0;
    occ_h[h->y][h->x] = 0;
    int s = (d->value + wave / 2) * (4 + perk[PK_HARVEST]) / 4;
    souls += s;
    kills++;
    float_num(h->x, h->y, s, SPRT.violet, "+");
    burst(h->x, h->y, 12, spr_col(200, 30, 40), 7, 1);
    burst(h->x, h->y, 5, SPRT.violet, 3, 0);
    if (!decal[h->y][h->x]) decal[h->y][h->x] = DC_BLOOD;
    if (killer_mon >= 0) mon_gain_kill(killer_mon);
    if (d->flags & HF_BOSS) { shake = 6; msg("The Champion is slain!", SPRT.yellow); }
    else { mstart("The "); g_cat(mb, d->name); g_cat(mb, " dies."); msg(mb, SPRT.text); }
}

static void hurt_hero(int i, int dmg, int killer_mon)
{
    hero_t *h = &heroes[i];
    if (!h->alive) return;
    if (dmg < 1) dmg = 1;
    h->hp -= (int16_t)dmg;
    h->flash = 2;
    float_num(h->x, h->y, dmg, SPRT.red, "-");
    if (h->hp <= 0) kill_hero(i, killer_mon);
}

static void blast(int cx, int cy, int dmg, int fire_t)
{
    for (int y = cy - 1; y <= cy + 1; y++)
        for (int x = cx - 1; x <= cx + 1; x++) {
            if (!in_map(x, y) || map[y][x] == T_WALL) continue;
            burst(x, y, 3, chance(50) ? SPRT.orange : SPRT.yellow, 5, 0);
            if (fire_t) fire[y][x] = (uint8_t)imax(fire[y][x], fire_t);
            else if (!decal[y][x]) decal[y][x] = DC_SCORCH;
            if (occ_h[y][x]) hurt_hero(occ_h[y][x] - 1, dmg, -1);
        }
    shake = imax(shake, 3);
}

static int trap_cd(int t) { return TDEF[t].cd * (100 - 30 * perk[PK_QUICK]) / 100; }
static int trap_dmg(int base) { return base * 5 * (2 + perk[PK_TRAPS]) / 8; }

static void spring_trap(int i)
{
    hero_t *h = &heroes[i];
    int x = h->x, y = h->y, t = trap[y][x];
    if (!t || tcd[y][x]) return;
    const hdef_t *d = &HDEF[h->type];
    tcd[y][x] = (uint8_t)trap_cd(t);
    switch (t) {
    case TR_SPIKES: burst(x, y, 4, spr_px('z'), 4, 1); hurt_hero(i, trap_dmg(6), -1); break;
    case TR_WEB:
        if (d->flags & HF_HEAVY) { spawn_float(x, y, "rip", SPRT.dim); break; }
        h->root = 8; spawn_float(x, y, "stuck", SPRT.text);
        break;
    case TR_FLAME: blast(x, y, 0, 8); hurt_hero(i, trap_dmg(4), -1); break;
    case TR_PIT:
        hurt_hero(i, trap_dmg(4), -1);
        if (h->alive && !occ_h[GATE_Y][GATE_X]) {
            burst(x, y, 6, SPRT.dim, 4, 0);
            occ_h[y][x] = 0; h->x = GATE_X; h->y = GATE_Y; occ_h[GATE_Y][GATE_X] = (int8_t)(i + 1);
            spawn_float(GATE_X, GATE_Y, "fell!", SPRT.yellow);
        }
        break;
    case TR_FROST:
        h->frozen = (uint8_t)((d->flags & HF_HEAVY) ? 4 : 8);
        burst(x, y, 6, SPRT.cyan, 4, 0);
        hurt_hero(i, trap_dmg(3), -1);
        break;
    case TR_MINE:
        trap[y][x] = TR_NONE; tcd[y][x] = 0;
        blast(x, y, trap_dmg(12), 0);
        break;
    }
}

static int new_hero(int type)
{
    for (int i = 0; i < MAX_HERO; i++) {
        if (heroes[i].alive) continue;
        hero_t *h = &heroes[i];
        const hdef_t *d = &HDEF[type];
        bclear(h, sizeof(*h));
        h->type = (uint8_t)type; h->alive = 1;
        h->x = GATE_X; h->y = GATE_Y;
        h->mhp = h->hp = (int16_t)(d->hp * (100 + 10 * (wave - 1)) / 100);
        h->atk = (int16_t)(d->atk * (100 + 6 * (wave - 1)) / 100);
        h->def = (int8_t)(d->def + (wave - 1) / 6);
        h->cd = (uint8_t)d->mv;
        occ_h[GATE_Y][GATE_X] = (int8_t)(i + 1);
        return i;
    }
    return -1;
}

static void hero_attack_mon(int i, int m)
{
    hero_t *h = &heroes[i];
    h->lunge = 2; h->ldx = (int8_t)(mons[m].x - h->x); h->ldy = (int8_t)(mons[m].y - h->y);
    if (HDEF[h->type].flags & HF_BOSS) {          /* the Champion cleaves */
        for (int k = 0; k < 4; k++) {
            int nx = h->x + DX[k], ny = h->y + DY[k];
            if (in_map(nx, ny) && occ_m[ny][nx]) hurt_mon(occ_m[ny][nx] - 1, roll(h->atk) - mon_def(&mons[occ_m[ny][nx] - 1]), 1);
        }
        return;
    }
    hurt_mon(m, roll(h->atk) - mon_def(&mons[m]), 1);
}

static void hero_act(int i)
{
    hero_t *h = &heroes[i];
    const hdef_t *d = &HDEF[h->type];
    int p = hero_profile(h->type);

    if (d->flags & HF_SENSE)
        for (int y = h->y - 2; y <= h->y + 2; y++)
            for (int x = h->x - 2; x <= h->x + 2; x++)
                if (in_map(x, y) && trap[y][x] && !tseen[y][x] && iabs(x - h->x) + iabs(y - h->y) <= 2) {
                    tseen[y][x] = 1;
                    spawn_float(x, y, "!", SPRT.yellow);
                }

    if (d->flags & HF_HEAL && !h->hcd) {
        int best = -1, worst = 0;
        for (int j = 0; j < MAX_HERO; j++) {
            if (!heroes[j].alive || j == i) continue;
            if (iabs(heroes[j].x - h->x) + iabs(heroes[j].y - h->y) > d->range) continue;
            int def = heroes[j].mhp - heroes[j].hp;
            if (def > worst) { worst = def; best = j; }
        }
        if (best >= 0 && worst >= 3) {
            int amt = 4 + wave / 2;
            heroes[best].hp = (int16_t)imin(heroes[best].mhp, heroes[best].hp + amt);
            float_num(heroes[best].x, heroes[best].y, amt, SPRT.green, "+");
            spawn_proj(h->x, h->y, heroes[best].x, heroes[best].y, PJ_HEAL);
            h->hcd = 3;
            return;
        }
    }
    if (d->flags & HF_RANGED) {
        int best = -1, bd = 99;
        for (int j = 0; j < MAX_MON; j++) {
            mon_t *m = &mons[j];
            if (!m->alive || m->pile) continue;
            int dd = iabs(m->x - h->x) + iabs(m->y - h->y);
            if (dd <= d->range && dd < bd && los_clear(h->x, h->y, m->x, m->y)) { bd = dd; best = j; }
        }
        if (best >= 0) {
            spawn_proj(h->x, h->y, mons[best].x, mons[best].y, PJ_MAGIC);
            hurt_mon(best, roll(h->atk) - mon_def(&mons[best]) / 2, 1);
            return;
        }
    }
    /* zone of control: a melee monster beside a hero stops it (thieves slip past) */
    if (!(d->flags & HF_SNEAK)) {
        int tgt = -1, thp = 0x7FFF;
        for (int k = 0; k < 4; k++) {
            int nx = h->x + DX[k], ny = h->y + DY[k];
            if (!in_map(nx, ny) || !occ_m[ny][nx]) continue;
            int m = occ_m[ny][nx] - 1;
            if (MDEF[mons[m].type].range > 1 && !(d->flags & HF_BOSS)) continue;
            if (mons[m].hp < thp) { thp = mons[m].hp; tgt = m; }
        }
        if (tgt >= 0) { hero_attack_mon(i, tgt); return; }
    }
    if (h->root) return;

    /* step down the cost map toward the crystal */
    int best = -1, bv = 0x7FFFFFFF, s = rn(4);
    for (int j = 0; j < 4; j++) {
        int k = (s + j) & 3, nx = h->x + DX[k], ny = h->y + DY[k];
        if (!walk_hero(nx, ny) || cost[p][ny][nx] == 0xFFFF) continue;
        int v = cost[p][ny][nx] + enter_cost(p, nx, ny);
        if (map[ny][nx] == T_CRYSTAL) v = -1;
        if (v < bv) { bv = v; best = k; }
    }
    if (best < 0) return;
    int nx = h->x + DX[best], ny = h->y + DY[best];
    if (map[ny][nx] == T_CRYSTAL) {
        int st = d->steal;
        shards -= st;
        flash_crys = 6; shake = 4;
        float_num(CRYS_X, CRYS_Y, st, SPRT.red, "-");
        burst(CRYS_X, CRYS_Y, 14, SPRT.cyan, 7, 0);
        mstart("The "); g_cat(mb, d->name); g_cat(mb, " steals a shard!"); msg(mb, SPRT.red);
        h->alive = 0; occ_h[h->y][h->x] = 0;
        return;
    }
    if (occ_m[ny][nx]) { hero_attack_mon(i, occ_m[ny][nx] - 1); return; }
    if (occ_h[ny][nx]) return;                       /* queue behind a friend */
    if ((d->flags & HF_SENSE) && trap[ny][nx] && tseen[ny][nx] && trap[ny][nx] != TR_NONE) {
        if (chance(60)) {
            mstart("The Rogue disarms the "); g_cat(mb, TDEF[trap[ny][nx]].name); g_cat(mb, "!");
            msg(mb, SPRT.orange);
            trap[ny][nx] = TR_NONE; tseen[ny][nx] = 0;
            burst(nx, ny, 6, SPRT.dim, 4, 0);
            return;
        }
    }
    occ_h[h->y][h->x] = 0;
    h->x = (int8_t)nx; h->y = (int8_t)ny;
    occ_h[ny][nx] = (int8_t)(i + 1);
    spring_trap(i);
}

/* ── Monster turns ───────────────────────────────────────────────────── */
static int nearest_hero(int x, int y, int maxd, int need_los)
{
    int best = -1, bd = maxd + 1;
    for (int j = 0; j < MAX_HERO; j++) {
        if (!heroes[j].alive) continue;
        int d = iabs(heroes[j].x - x) + iabs(heroes[j].y - y);
        if (d < bd && (!need_los || los_clear(x, y, heroes[j].x, heroes[j].y))) { bd = d; best = j; }
    }
    return best;
}

static void mon_move(int i, int nx, int ny)
{
    mon_t *m = &mons[i];
    occ_m[m->y][m->x] = 0;
    m->x = (int8_t)nx; m->y = (int8_t)ny;
    occ_m[ny][nx] = (int8_t)(i + 1);
}

static int mon_step(int i, int tx, int ty)
{
    mon_t *m = &mons[i];
    int lea = MDEF[m->type].leash, best = -1, bd = iabs(tx - m->x) + iabs(ty - m->y);
    for (int k = 0; k < 4; k++) {
        int nx = m->x + DX[k], ny = m->y + DY[k];
        if (!in_map(nx, ny) || map[ny][nx] != T_FLOOR || occ_m[ny][nx] || occ_h[ny][nx]) continue;
        if (iabs(nx - m->hx) + iabs(ny - m->hy) > lea) continue;
        int d = iabs(tx - nx) + iabs(ty - ny);
        if (d < bd) { bd = d; best = k; }
    }
    if (best < 0) return 0;
    mon_move(i, m->x + DX[best], m->y + DY[best]);
    return 1;
}

static void mon_hits(int i, int j, int ranged)
{
    mon_t *m = &mons[i];
    hero_t *h = &heroes[j];
    if (!ranged) { m->lunge = 2; m->ldx = (int8_t)(h->x - m->x); m->ldy = (int8_t)(h->y - m->y); }
    hurt_hero(j, roll(mon_atk(m)) - h->def, i);
    if (!h->alive) return;
    if (m->type == M_SPIDER && !(HDEF[h->type].flags & HF_HEAVY) && chance(40)) h->root = 5;
    if (m->type == M_IMP && chance(45)) h->burn = 6;
}

static void mon_act(int i)
{
    mon_t *m = &mons[i];
    const mdef_t *d = &MDEF[m->type];
    if (m->pile) {
        if (m->cd) m->cd--;
        if (!m->cd) {
            if (occ_m[m->y][m->x] || occ_h[m->y][m->x]) { m->cd = 2; return; }
            m->pile = 0; m->revived = 1;
            occ_m[m->y][m->x] = (int8_t)(i + 1);
            burst(m->x, m->y, 6, spr_px('v'), 3, 0);
            spawn_float(m->x, m->y, "rises", SPRT.dim);
        }
        return;
    }
    if (perk[PK_REGEN] && (tick % 6) == 0 && m->hp < m->mhp) m->hp = (int16_t)imin(m->mhp, m->hp + perk[PK_REGEN]);
    if (m->cd) { m->cd--; return; }
    m->cd = (uint8_t)(d->rate - 1);

    if (m->type == M_SHAMAN) {
        int best = -1, worst = 0;
        for (int j = 0; j < MAX_MON; j++) {
            mon_t *o = &mons[j];
            if (!o->alive || o->pile || j == i) continue;
            if (iabs(o->x - m->x) + iabs(o->y - m->y) > 3) continue;
            if (o->mhp - o->hp > worst) { worst = o->mhp - o->hp; best = j; }
        }
        if (best >= 0 && worst >= 3) {
            int amt = 5 + 2 * m->lvl;
            mons[best].hp = (int16_t)imin(mons[best].mhp, mons[best].hp + amt);
            float_num(mons[best].x, mons[best].y, amt, SPRT.green, "+");
            spawn_proj(m->x, m->y, mons[best].x, mons[best].y, PJ_HEAL);
            return;
        }
    }
    int r = mon_range(m);
    if (r > 1) {
        int j = nearest_hero(m->x, m->y, r, 1);
        if (j >= 0) {
            spawn_proj(m->x, m->y, heroes[j].x, heroes[j].y, m->type == M_ARCHER ? PJ_ARROW : PJ_ZAP);
            mon_hits(i, j, 1);
        }
        return;
    }
    /* melee: strike an adjacent hero (weakest first) */
    int tgt = -1, thp = 0x7FFF;
    for (int k = 0; k < 4; k++) {
        int nx = m->x + DX[k], ny = m->y + DY[k];
        if (in_map(nx, ny) && occ_h[ny][nx]) {
            int j = occ_h[ny][nx] - 1;
            if (heroes[j].hp < thp) { thp = heroes[j].hp; tgt = j; }
        }
    }
    if (tgt >= 0) { mon_hits(i, tgt, 0); return; }
    int j = nearest_hero(m->hx, m->hy, d->leash + 3, 0);
    if (j >= 0 && mon_step(i, heroes[j].x, heroes[j].y)) {
        if (m->type == M_IMP) {           /* imps strike right after closing in */
            for (int k = 0; k < 4; k++) {
                int nx = m->x + DX[k], ny = m->y + DY[k];
                if (in_map(nx, ny) && occ_h[ny][nx]) { mon_hits(i, occ_h[ny][nx] - 1, 0); break; }
            }
        }
        return;
    }
    if (m->x != m->hx || m->y != m->hy) mon_step(i, m->hx, m->hy);
}

/* ── Waves ───────────────────────────────────────────────────────────── */
static void plan_wave(void)
{
    static const uint8_t W[N_HERO - 1] = {30, 22, 18, 12, 14, 12};
    wave_n = imin(3 + wave * 2 / 3, 14);
    int boss = (wave % 5 == 0);
    for (int i = 0; i < wave_n; i++) {
        int tot = 0;
        for (int t = 0; t < N_HERO - 1; t++) if (wave >= HDEF[t].unlock) tot += W[t];
        int r = rn(tot), t = 0;
        for (; t < N_HERO - 1; t++) {
            if (wave < HDEF[t].unlock) continue;
            if (r < W[t]) break;
            r -= W[t];
        }
        wave_list[i] = (uint8_t)t;
    }
    if (boss) wave_list[wave_n / 2] = H_CHAMPION;
}

static int heroes_alive(void)
{
    int n = 0;
    for (int i = 0; i < MAX_HERO; i++) n += heroes[i].alive;
    return n;
}

static void start_raid(void)
{
    phase = PH_RAID;
    spawn_i = 0; spawn_t = 2; tick = 0;
    bclear(tseen, sizeof(tseen));
    bclear(heroes, sizeof(heroes));
    for (int y = 0; y < MAP_H; y++) for (int x = 0; x < MAP_W; x++) tcd[y][x] = 0;
    rebuild_occ();
    mstart("Wave "); g_cat_int(mb, wave); g_cat(mb, ": the heroes come!"); msg(mb, SPRT.yellow);
}

/* One simulation step.  Returns 1 while the raid continues. */
static int sim_tick(void)
{
    tick++;
    if (spawn_i < wave_n) {
        if (spawn_t) spawn_t--;
        if (!spawn_t && !occ_h[GATE_Y][GATE_X]) {
            int t = wave_list[spawn_i++];
            new_hero(t);
            burst(GATE_X, GATE_Y, 5, SPRT.yellow, 3, 0);
            spawn_t = (t == H_CHAMPION) ? 10 : 6;
            if (t == H_CHAMPION) { msg("A Champion strides in!", SPRT.red); shake = 4; }
        }
    }
    calc_costs();
    for (int i = 0; i < MAX_HERO; i++) {
        hero_t *h = &heroes[i];
        if (!h->alive) continue;
        if (h->burn) {
            h->burn--;
            if ((tick & 1) == 0) { hurt_hero(i, 1 + wave / 5, -1); if (!h->alive) continue; }
        }
        if (fire[h->y][h->x]) h->burn = (uint8_t)imax(h->burn, 6);
        if (h->frozen) { h->frozen--; continue; }
        if (h->root) h->root--;
        if (h->hcd && (tick % HDEF[h->type].mv) == 0) h->hcd--;
        if (h->cd) h->cd--;
        if (h->cd) continue;
        h->cd = HDEF[h->type].mv;
        hero_act(i);
        if (shards <= 0) return 0;
    }
    for (int i = 0; i < MAX_MON; i++)
        if (mons[i].alive) mon_act(i);
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) {
            if (tcd[y][x]) tcd[y][x]--;
            if (fire[y][x] && --fire[y][x] == 0 && !decal[y][x]) decal[y][x] = DC_SCORCH;
        }
    return !(spawn_i >= wave_n && !heroes_alive());
}

static void end_wave(void)
{
    cleared++;
    int bonus = 15 + 4 * wave;
    souls += bonus;
    for (int i = 0; i < MAX_MON; i++) {
        mon_t *m = &mons[i];
        if (!m->alive) continue;
        if (m->pile) { m->alive = 0; continue; }
        m->hp = m->mhp; m->revived = 0;
        if (m->x != m->hx || m->y != m->hy) {
            if (!occ_m[m->hy][m->hx]) mon_move(i, m->hx, m->hy);
            else { m->hx = m->x; m->hy = m->y; }
        }
    }
    bclear(fire, sizeof(fire));
    wave++;
    plan_wave();
    phase = PH_BUILD;
    banner_t = 14;
    mstart("Wave cleared! +"); g_cat_int(mb, bonus); g_cat(mb, " souls"); msg(mb, SPRT.green);
    rebuild_occ();
}

static void new_lair(void)
{
    bclear(mons, sizeof(mons)); bclear(heroes, sizeof(heroes)); bclear(perk, sizeof(perk));
    bclear(parts, sizeof(parts)); bclear(projs, sizeof(projs)); bclear(floats, sizeof(floats));
    gen_map();
    souls = 100; wave = 1; cleared = 0; kills = 0;
    max_shards = shards = 10;
    phase = PH_BUILD; cur_x = 10; cur_y = 5; show_route = 1; fast = 0;
    won_once = quit_req = restart_req = 0;
    shake = flash_crys = banner_t = 0;
    msgbuf[0] = 0;
    plan_wave();
    rebuild_occ();
    msg("Build defences, then press X.", SPRT.text);
}

/* ── Rendering ───────────────────────────────────────────────────────── */
static int sx_of(int x) { return OX + x * TS; }
static int sy_of(int y) { return OY + y * TS; }
static const char *hint_override;

static void draw_tile(int x, int y)
{
    int sx = sx_of(x), sy = sy_of(y), t = map[y][x], id;
    if (t == T_WALL) {
        int below = (y + 1 < MAP_H) ? map[y + 1][x] : T_WALL;
        if (below == T_WALL) {
            display_bitmap(sx, sy, 16, 16, tile_px[TL_WALL_TOP], 512);
            if (y > 0 && map[y - 1][x] != T_WALL) display_hline(sx, sy, 16, edge_col);
            if (x > 0 && map[y][x - 1] != T_WALL) display_vline(sx, sy, 16, edge_col);
            if (x + 1 < MAP_W && map[y][x + 1] != T_WALL) display_vline(sx + 15, sy, 16, edge_col);
            return;
        }
        id = torch[y][x] ? (((anim_tick >> 2) + x) & 1 ? TL_TORCH1 : TL_TORCH0)
                         : (thash(x, y) % 100 < 14 ? TL_WALL_FACE_B : TL_WALL_FACE);
        display_bitmap(sx, sy, 16, 16, tile_px[id], 512);
        return;
    }
    if (t == T_GATE) { display_bitmap(sx, sy, 16, 16, tile_px[TL_GATE], 512); return; }
    if (t == T_CRYSTAL) {
        display_bitmap(sx, sy, 16, 16, tile_px[TL_CRYSTAL], 512);
        if (flash_crys && (flash_crys & 1)) spr_blit_tint(sx, sy, tile_px[TL_CRYSTAL] , 16, 16, SPRT.red, scratch);
        else if (((anim_tick >> 1) & 7) == 0) display_pixel(sx + 6, sy + 3, 0xFFFF);
        return;
    }
    int tr = trap[y][x];
    if (tr && tr != TR_WEB) id = (tr == TR_SPIKES && tcd[y][x]) ? TL_SPIKES_DOWN : TDEF[tr].tile;
    else { uint32_t h = thash(x, y) % 100; id = h < 76 ? TL_FLOOR_A : h < 94 ? TL_FLOOR_B : TL_FLOOR_C; }
    display_bitmap(sx, sy, 16, 16, tile_px[id], 512);
    if (decal[y][x]) {
        static const uint8_t DS[] = {0, SP_BLOOD, SP_BONES, SP_SCORCH};
        spr_blit16(sx, sy, spr_c[DS[decal[y][x]]]);
    }
    if (tr == TR_WEB) spr_blit16(sx, sy, spr_c[SP_WEB]);
    if (tr && tcd[y][x] && tr != TR_SPIKES) spr_blit16(sx, sy, veil_dim);
}

static void draw_mon(int i)
{
    mon_t *m = &mons[i];
    int sx = sx_of(m->x), sy = sy_of(m->y);
    if (m->pile) { spr_blit16(sx, sy, spr_c[SP_BONES]); return; }
    int bob = ((anim_tick >> 2) + i) & 1;
    int ox = m->lunge ? m->ldx * 4 : 0, oy = m->lunge ? m->ldy * 4 : 0;
    display_hline(sx + 4, sy + 14, 8, 0x0000);
    display_hline(sx + 3, sy + 15, 10, 0x0000);
    const uint16_t *src = spr_c[MDEF[m->type].spr];
    if (m->flash) spr_blit_tint(sx + ox, sy + oy - bob, src, 16, 16, 0xFFFF, scratch);
    else spr_blit16(sx + ox, sy + oy - bob, src);
    for (int s = 1; s < m->lvl; s++) blit_icon(sx - 2 + (s - 1) * 7, sy - 3, IC_STAR);
    if (m->hp < m->mhp) {
        display_rect(sx + 2, sy - 3, 12, 2, SPRT.barbg);
        display_rect(sx + 2, sy - 3, imax(1, 12 * m->hp / m->mhp), 2, SPRT.green);
    }
}

static void draw_hero(int i)
{
    hero_t *h = &heroes[i];
    const hdef_t *d = &HDEF[h->type];
    int sx = sx_of(h->x), sy = sy_of(h->y);
    int bob = (h->frozen || h->root) ? 0 : ((anim_tick >> 2) + i) & 1;
    int ox = h->lunge ? h->ldx * 4 : 0, oy = h->lunge ? h->ldy * 4 : 0;
    display_hline(sx + 4, sy + 14, 8, 0x0000);
    display_hline(sx + 3, sy + 15, 10, 0x0000);
    if (d->flags & HF_BOSS) {
        if (h->flash) spr_blit_tint(sx - 4 + ox, sy - 8 + oy - bob, champ_px, 24, 24, 0xFFFF, scratch);
        else spr_blit(sx - 4 + ox, sy - 8 + oy - bob, 24, 24, champ_px);
    } else {
        const uint16_t *src = spr_c[d->spr];
        if (h->flash) spr_blit_tint(sx + ox, sy + oy - bob, src, 16, 16, 0xFFFF, scratch);
        else spr_blit16(sx + ox, sy + oy - bob, src);
    }
    if (h->frozen) spr_blit16(sx, sy, veil_frost);
    if (h->root) spr_blit16(sx, sy, spr_c[SP_WEB]);
    if (h->burn || fire[h->y][h->x]) spr_blit16(sx, sy - 4, spr_c[(anim_tick & 2) ? SP_FIRE1 : SP_FIRE0]);
    int by = (d->flags & HF_BOSS) ? sy - 11 : sy - 3;
    display_rect(sx + 2, by, 12, 2, SPRT.barbg);
    display_rect(sx + 2, by, imax(1, 12 * h->hp / h->mhp), 2, SPRT.red);
}

static void draw_route(void)
{
    calc_costs();
    int x = GATE_X, y = GATE_Y;
    for (int n = 0; n < 120; n++) {
        int best = -1, bv = 0x7FFFFFFF;
        for (int k = 0; k < 4; k++) {
            int nx = x + DX[k], ny = y + DY[k];
            if (!walk_hero(nx, ny) || cost[0][ny][nx] == 0xFFFF) continue;
            int v = cost[0][ny][nx] + enter_cost(0, nx, ny);
            if (map[ny][nx] == T_CRYSTAL) v = -1;
            if (v < bv) { bv = v; best = k; }
        }
        if (best < 0) return;
        x += DX[best]; y += DY[best];
        if (map[y][x] == T_CRYSTAL) return;
        uint16_t c = occ_m[y][x] ? SPRT.red : ((n + (anim_tick >> 1)) % 4 == 0 ? 0xFFFF : SPRT.yellow);
        display_rect(sx_of(x) + 7, sy_of(y) + 7, 2, 2, c);
    }
}

static void draw_fx(void)
{
    for (int i = 0; i < MAX_PROJ; i++) {
        proj_t *p = &projs[i];
        if (!p->dur) continue;
        int t1 = p->t + 1;
        int x = OX + p->x0 + (p->x1 - p->x0) * t1 / p->dur, y = OY + p->y0 + (p->y1 - p->y0) * t1 / p->dur;
        int bx = OX + p->x0 + (p->x1 - p->x0) * p->t / p->dur, by = OY + p->y0 + (p->y1 - p->y0) * p->t / p->dur;
        switch (p->kind) {
        case PJ_ARROW: display_line(bx, by, x, y, spr_px('n')); display_rect(x - 1, y - 1, 3, 3, 0xFFFF); break;
        case PJ_ZAP: display_line(bx, by, x, y + 2, SPRT.yellow); display_line(bx, by + 2, x, y, 0xFFFF); break;
        case PJ_HEAL: display_circle(x, y, 3, SPRT.green); break;
        default: display_circle_fill(x, y, 3, SPRT.violet); display_pixel(x, y, 0xFFFF); break;
        }
    }
    for (int i = 0; i < MAX_PART; i++) {
        part_t *p = &parts[i];
        if (!p->life) continue;
        int s = p->life > 3 ? 2 : 1;
        display_rect(OX + p->x / 16, OY + p->y / 16, s, s, p->col);
    }
    for (int i = 0; i < MAX_FLOAT; i++) {
        fl_t *f = &floats[i];
        if (f->t) spr_text_out(OX + f->x, OY + f->y - (12 - f->t) - 6, f->txt, f->col);
    }
}

static void draw_hud(void)
{
    char b[24];
    display_rect(0, 0, SCR_W, HUD_H, SPRT.panel);
    display_hline(0, HUD_H - 1, SCR_W, SPRT.accent);
    blit_icon(4, 3, IC_SOUL);
    b[0] = 0; g_cat_int(b, souls); display_text(15, 2, b, SPRT.violet);
    blit_icon(4, 14, IC_CRYS);
    spr_bar(15, 15, 40, 4, shards, max_shards, shards * 3 <= max_shards ? SPRT.red : SPRT.cyan);
    b[0] = 0; g_cat(b, "WAVE "); g_cat_int(b, wave);
    display_text(64, 2, b, SPRT.yellow);
    b[0] = 0; g_cat_int(b, shards); g_cat(b, "/"); g_cat_int(b, max_shards);
    display_text(64, 13, b, SPRT.dim);

    if (phase == PH_BUILD) {
        int n = imin(wave_n, 11), x = SCR_W - 4 - n * 13 - 8;
        display_text(x - 34, 7, "NEXT", SPRT.dim);
        for (int i = 0; i < n; i++) {
            int t = wave_list[i];
            if (t == H_CHAMPION) spr_blit(x + i * 13 - 4, 0, 24, 24, champ_px);
            else spr_blit16(x + i * 13, 4, spr_c[HDEF[t].spr]);
        }
        if (wave_n > n) { b[0] = 0; g_cat(b, "+"); g_cat_int(b, wave_n - n); display_text(SCR_W - 18, 7, b, SPRT.dim); }
    } else {
        int left = (wave_n - spawn_i) + heroes_alive();
        b[0] = 0; g_cat(b, "HEROES "); g_cat_int(b, left);
        display_text(SCR_W - 150, 7, b, SPRT.text);
        spr_bar(SCR_W - 82, 9, 56, 5, wave_n - left, wave_n, SPRT.violet);
        if (fast) display_text(SCR_W - 20, 7, ">>", SPRT.yellow);
    }
}

static void draw_bar(void)
{
    int y0 = SCR_H - BAR_H;
    display_rect(0, y0, SCR_W, BAR_H, SPRT.panel);
    display_hline(0, y0, SCR_W, SPRT.accent);
    if (msgbuf[0]) display_text(4, y0 + 3, msgbuf, msg_t ? msgcol : SPRT.dim);
    const char *h = hint_override ? hint_override
                  : phase == PH_BUILD ? "A build  X raid  Y route  HOME menu"
                                      : fast ? "A normal speed   HOME menu" : "A fast forward   HOME menu";
    display_text(4, y0 + 13, h, SPRT.dim);
}

static void draw_world(void)
{
    int ox = OX, oy = OY;
    if (shake && !spr_mono) { OX += (anim_tick & 1) ? 2 : -2; OY += (anim_tick & 2) ? 1 : -1; }
    display_clear(0x0000);
    for (int x = OX - TS; x > -TS; x -= TS)                       /* Sharp side margins */
        for (int y = 0; y < MAP_H; y++) {
            display_bitmap(x, sy_of(y), 16, 16, tile_px[TL_WALL_TOP], 512);
            display_bitmap(SCR_W - (x + TS), sy_of(y), 16, 16, tile_px[TL_WALL_TOP], 512);
        }
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) draw_tile(x, y);
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) {
            if (fire[y][x]) spr_blit16(sx_of(x), sy_of(y) - 2, spr_c[((anim_tick >> 1) + x) & 1 ? SP_FIRE1 : SP_FIRE0]);
            if (phase == PH_RAID && tseen[y][x] && trap[y][x] && (anim_tick & 4))
                spr_text_out(sx_of(x) + 5, sy_of(y) - 4, "!", SPRT.yellow);
        }
    if (phase == PH_BUILD && show_route) draw_route();
    for (int y = 0; y < MAP_H; y++) {
        for (int i = 0; i < MAX_MON; i++) if (mons[i].alive && mons[i].y == y) draw_mon(i);
        for (int i = 0; i < MAX_HERO; i++) if (heroes[i].alive && heroes[i].y == y) draw_hero(i);
    }
    if (phase == PH_BUILD) {
        int sx = sx_of(cur_x), sy = sy_of(cur_y);
        int ok = buildable(cur_x, cur_y) || occ_m[cur_y][cur_x] || trap[cur_y][cur_x];
        uint16_t c = ok ? ((anim_tick & 4) ? 0xFFFF : SPRT.yellow) : SPRT.red;
        display_rect_outline(sx, sy, 16, 16, c);
        display_rect_outline(sx - 1, sy - 1, 18, 18, (anim_tick & 4) ? c : 0x0000);
    }
    draw_fx();
    OX = ox; OY = oy;
    if (banner_t) {
        int w = 200, x = (SCR_W - w) / 2, y = OY + 70;
        spr_panel(x, y, w, 36, 0);
        g_text_large_c(SCR_W / 2, y + 9, "WAVE CLEARED", SPRT.green);
    }
    draw_hud();
    draw_bar();
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
    for (int i = 0; i < MAX_PROJ; i++) if (projs[i].dur && ++projs[i].t >= projs[i].dur) projs[i].dur = 0;
    for (int i = 0; i < MAX_FLOAT; i++) if (floats[i].t) floats[i].t--;
    for (int i = 0; i < MAX_MON; i++) { if (mons[i].flash) mons[i].flash--; if (mons[i].lunge) mons[i].lunge--; }
    for (int i = 0; i < MAX_HERO; i++) { if (heroes[i].flash) heroes[i].flash--; if (heroes[i].lunge) heroes[i].lunge--; }
    if (shake) shake--;
    if (flash_crys) flash_crys--;
    if (banner_t) banner_t--;
    if (msg_t) msg_t--;
}

static void frame(void)
{
    draw_world();
    display_flush();
    tick_fx();
}

/* ── Build & inspect ─────────────────────────────────────────────────── */
static const uint16_t *entry_icon(const bentry_t *e)
{
    if (e->kind == 0) return spr_c[MDEF[e->type].spr];
    if (e->type == TR_WEB) return spr_c[SP_WEB];
    return tile_px[TDEF[e->type].tile];
}

static void build_menu(void)
{
    static int sel = 0;
    g_flush_input();
    hint_override = "LEFT/RIGHT choose  A build  B back";
    for (;;) {
        draw_world();
        int ph = 78, w = SCR_W - 12, x = 6;
        int y = (cur_y >= 6) ? HUD_H + 4 : SCR_H - BAR_H - ph - 4;
        spr_panel(x, y, w, ph, 0);
        const bentry_t *e = &BUILD[sel];
        int lock = !unlocked(e->kind, e->type), c = build_cost(e->kind, e->type);
        const char *nm = e->kind ? TDEF[e->type].name : MDEF[e->type].name;
        const char *ds = e->kind ? TDEF[e->type].desc : MDEF[e->type].desc;
        display_text(x + 10, y + 6, nm, lock ? SPRT.dim : SPRT.yellow);
        char b[40];
        if (lock) {
            b[0] = 0; g_cat(b, "unlocks at wave "); g_cat_int(b, e->kind ? TDEF[e->type].unlock : MDEF[e->type].unlock);
            g_text_r(x + w - 10, y + 6, b, SPRT.dim);
        } else {
            blit_icon(x + w - 46, y + 7, IC_SOUL);
            b[0] = 0; g_cat_int(b, c);
            display_text(x + w - 34, y + 6, b, souls >= c ? SPRT.violet : SPRT.red);
        }
        display_text(x + 10, y + 18, ds, SPRT.dim);
        b[0] = 0;
        if (e->kind == 0) {
            const mdef_t *d = &MDEF[e->type];
            g_cat(b, "HP "); g_cat_int(b, d->hp); g_cat(b, "  ATK "); g_cat_int(b, d->atk);
            if (d->def) { g_cat(b, "  DEF "); g_cat_int(b, d->def); }
            if (d->range > 1) { g_cat(b, "  RANGE "); g_cat_int(b, d->range + perk[PK_SIGHT]); }
        } else if (TDEF[e->type].cd) {
            g_cat(b, "Re-arms in "); g_cat_int(b, trap_cd(e->type)); g_cat(b, " turns");
        } else g_cat(b, "Single use");
        display_text(x + 10, y + 30, b, SPRT.cyan);
        int step = imin(20, (w - 16) / N_BUILD), sx0 = x + (w - step * N_BUILD) / 2;
        for (int i = 0; i < N_BUILD; i++) {
            int ix = sx0 + i * step, iy = y + 50;
            const bentry_t *q = &BUILD[i];
            spr_blit16(ix, iy, entry_icon(q));
            if (!unlocked(q->kind, q->type)) { spr_blit16(ix, iy, veil_dim); blit_icon(ix + 4, iy + 4, IC_LOCK); }
            else if (souls < build_cost(q->kind, q->type)) spr_blit16(ix, iy, veil_dim);
            if (i == sel) {
                display_rect_outline(ix - 2, iy - 2, 20, 20, SPRT.yellow);
                display_rect_outline(ix - 3, iy - 3, 22, 22, SPRT.yellow);
            }
        }
        display_flush();
        anim_tick++;
        uint32_t k = g_wait_key();
        if (k == K_LEFT) sel = (sel + N_BUILD - 1) % N_BUILD;
        else if (k == K_RIGHT) sel = (sel + 1) % N_BUILD;
        else if (k == K_B || k == K_HOME) break;
        else if (k == K_A) {
            if (lock) { msg("Not unlocked yet.", SPRT.dim); continue; }
            if (souls < c) { msg("Not enough souls.", SPRT.red); continue; }
            souls -= c;
            if (e->kind == 0) new_mon(e->type, cur_x, cur_y);
            else trap[cur_y][cur_x] = e->type;
            burst(cur_x, cur_y, 8, SPRT.violet, 4, 0);
            mstart(nm); g_cat(mb, " placed."); msg(mb, SPRT.violet);
            break;
        }
    }
    hint_override = 0;
}

static void inspect_menu(void)
{
    int mi = occ_m[cur_y][cur_x] - 1, tr = trap[cur_y][cur_x];
    int sel_m = (mi >= 0);
    g_flush_input();
    hint_override = sel_m && tr ? "A sell  X sell trap  B back" : "A sell  B back";
    for (;;) {
        draw_world();
        int w = 220, h = 62, x = (SCR_W - w) / 2;
        int y = (cur_y >= 6) ? HUD_H + 4 : SCR_H - BAR_H - h - 4;
        spr_panel(x, y, w, h, 0);
        char b[40];
        if (sel_m) {
            mon_t *m = &mons[mi];
            spr_blit16(x + 8, y + 8, spr_c[MDEF[m->type].spr]);
            display_text(x + 30, y + 6, MDEF[m->type].name, SPRT.yellow);
            for (int s = 1; s < m->lvl; s++) blit_icon(x + 32 + g_tw(MDEF[m->type].name) + s * 9, y + 7, IC_STAR);
            b[0] = 0; g_cat(b, "HP "); g_cat_int(b, m->hp); g_cat(b, "/"); g_cat_int(b, m->mhp);
            g_cat(b, "  ATK "); g_cat_int(b, mon_atk(m)); g_cat(b, "  Kills "); g_cat_int(b, m->kills);
            display_text(x + 30, y + 18, b, SPRT.text);
            b[0] = 0; g_cat(b, "A: sell for "); g_cat_int(b, MDEF[m->type].cost / 2); g_cat(b, " souls");
            display_text(x + 10, y + 40, b, SPRT.dim);
        } else {
            spr_blit16(x + 8, y + 8, entry_icon(&(bentry_t){1, (uint8_t)tr}));
            display_text(x + 30, y + 6, TDEF[tr].name, SPRT.yellow);
            display_text(x + 30, y + 18, TDEF[tr].desc, SPRT.text);
            b[0] = 0; g_cat(b, "A: sell for "); g_cat_int(b, TDEF[tr].cost / 2); g_cat(b, " souls");
            display_text(x + 10, y + 40, b, SPRT.dim);
        }
        display_flush();
        uint32_t k = g_wait_key();
        if (k == K_B || k == K_HOME) break;
        if (k == K_A || (k == K_X && sel_m && tr)) {
            if (sel_m && k == K_A) {
                souls += MDEF[mons[mi].type].cost / 2;
                occ_m[cur_y][cur_x] = 0; mons[mi].alive = 0;
                msg("Monster dismissed.", SPRT.dim);
            } else {
                souls += TDEF[tr].cost / 2;
                trap[cur_y][cur_x] = TR_NONE;
                msg("Trap removed.", SPRT.dim);
            }
            burst(cur_x, cur_y, 6, SPRT.dim, 3, 0);
            break;
        }
    }
    hint_override = 0;
}

/* ── Perks ───────────────────────────────────────────────────────────── */
static void choose_perk(void)
{
    int opts[3], n = 0;
    for (int t = 0; t < 60 && n < 3; t++) {
        int k = rn(N_PERK), dup = 0;
        if (perk[k] >= PERKS[k].max) continue;
        for (int j = 0; j < n; j++) dup |= (opts[j] == k);
        if (!dup) opts[n++] = k;
    }
    if (!n) return;
    int sel = 0;
    g_flush_input();
    hint_override = "LEFT/RIGHT choose   A take";
    for (;;) {
        draw_world();
        int cw = (SCR_W - 24) / 3, ch = 112, y = 56, x0 = (SCR_W - (cw * 3 + 12)) / 2;
        display_rect(0, 28, SCR_W, 22, SPRT.panel);
        g_text_large_c(SCR_W / 2, 30, "CHOOSE A DARK BOON", SPRT.violet);
        for (int i = 0; i < n; i++) {
            int x = x0 + i * (cw + 6);
            const perk_t *p = &PERKS[opts[i]];
            spr_panel(x, y, cw, ch, 0);
            if (i == sel) {
                display_rounded_rect(x - 2, y - 2, cw + 4, ch + 4, 7, SPRT.yellow);
                display_rounded_rect(x - 1, y - 1, cw + 2, ch + 2, 7, SPRT.yellow);
            }
            spr_blit_scaled(x + cw / 2 - 8, y + 10, icon_px[p->icon], 8, 8, 2, scratch);
            g_text_c(x + cw / 2, y + 36, p->name, i == sel ? SPRT.yellow : SPRT.text);
            g_text_c(x + cw / 2, y + 56, p->d1, SPRT.dim);
            g_text_c(x + cw / 2, y + 68, p->d2, SPRT.dim);
            if (perk[opts[i]]) {
                char b[10]; b[0] = 0; g_cat(b, "Rank "); g_cat_int(b, perk[opts[i]] + 1);
                g_text_c(x + cw / 2, y + ch - 18, b, SPRT.cyan);
            }
        }
        display_flush();
        uint32_t k = g_wait_key();
        if (k == K_LEFT) sel = (sel + n - 1) % n;
        else if (k == K_RIGHT) sel = (sel + 1) % n;
        else if (k == K_A) break;
    }
    int p = opts[sel];
    perk[p]++;
    if (p == PK_WARD) { max_shards += 3; shards += 3; }
    if (p == PK_HIDE)
        for (int i = 0; i < MAX_MON; i++) if (mons[i].alive) { mons[i].mhp = (int16_t)mon_mhp(&mons[i]); mons[i].hp = mons[i].mhp; }
    mstart("Boon: "); g_cat(mb, PERKS[p].name); msg(mb, SPRT.violet);
    hint_override = 0;
}

/* ── Menus and screens ───────────────────────────────────────────────── */
static void show_guide(void)
{
    static const char *const G[] = {
        "Build monsters and traps, then",
        "press X to let the heroes in.",
        "",
        "Heroes take the cheapest route.",
        "A monster stops any hero who",
        "walks past it - except thieves.",
        "Y shows the route they'll take.",
        "",
        "Rogues spot and disarm traps.",
        "Thieves sneak around monsters.",
        "Monsters rank up with kills.",
        "Hold out for 20 waves!",
    };
    g_flush_input();
    draw_world();
    int w = 290, h = 206, x = (SCR_W - w) / 2, y = 28;
    spr_panel(x, y, w, h, "GUIDE");
    for (int i = 0; i < (int)(sizeof(G) / sizeof(G[0])); i++)
        display_text(x + 14, y + 34 + i * 13, G[i], i < 3 ? SPRT.text : i < 8 ? SPRT.cyan : SPRT.violet);
    display_flush();
    g_wait_key();
}

static int pause_menu(void)
{
    static const char *const L[] = {"Resume", "Guide", "New lair", "Quit to title"};
    int n = 4, sel = 0;
    g_flush_input();
    for (;;) {
        draw_world();
        int w = 180, h = 36 + n * 20 + 8, x = (SCR_W - w) / 2, y = (SCR_H - h) / 2;
        spr_panel(x, y, w, h, "PAUSED");
        for (int i = 0; i < n; i++) {
            int iy = y + 34 + i * 20;
            if (i == sel) spr_sel_row(x + 8, iy - 2, w - 16, 18);
            g_text_c(x + w / 2, iy + 2, L[i], i == sel ? SPRT.seltext : SPRT.text);
        }
        display_flush();
        uint32_t k = g_wait_key();
        if (k == K_UP) sel = (sel + n - 1) % n;
        else if (k == K_DOWN) sel = (sel + 1) % n;
        else if (k == K_B || k == K_HOME) return 0;
        else if (k == K_A) {
            if (sel == 0) return 0;
            if (sel == 1) show_guide();
            else return sel == 2 ? 1 : 2;
        }
    }
}

static const char *glyph(char c)
{
    switch (c) {
    case 'C': return "01110100011000010000100001000101110";
    case 'R': return "11110100011000111110101001001010001";
    case 'Y': return "10001100010101000100001000010000100";
    case 'S': return "01111100001000001110000010000111110";
    case 'T': return "11111001000010000100001000010000100";
    case 'A': return "01110100011000111111100011000110001";
    case 'L': return "10000100001000010000100001000011111";
    case 'I': return "11111001000010000100001000010011111";
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
                    if (!pass) { if (!spr_mono) display_rect(x + 2, yy + 3, s, s, spr_col(40, 10, 60)); }
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
    for (;;) {
        display_clear(0x0000);
        for (int x = 0; x < SCR_W; x += TS) {
            display_bitmap(x, 0, 16, 16, tile_px[TL_WALL_TOP], 512);
            display_bitmap(x, 16, 16, 16, tile_px[(x / TS) % 5 == 2 ? ((anim_tick >> 1) & 1 ? TL_TORCH1 : TL_TORCH0)
                                                                    : TL_WALL_FACE], 512);
            display_bitmap(x, SCR_H - 16, 16, 16, tile_px[TL_FLOOR_A], 512);
        }
        logo_word("CRYSTAL", 44, 5, spr_col(200, 240, 255), spr_col(110, 170, 255));
        logo_word("LAIR", 88, 5, spr_col(230, 140, 255), spr_col(150, 70, 220));
        int cx = SCR_W / 2;
        spr_blit_scaled(cx - 16, 128 - ((anim_tick >> 2) & 1), spr_c[SP_CRYSTAL], 16, 16, 2, scratch);
        spr_blit16(cx - 64, 142, spr_c[SP_GOBLIN]);
        spr_blit16(cx - 44, 142, spr_c[SP_SKELETON]);
        spr_blit16(cx + 28, 142, spr_c[SP_WARRIOR]);
        spr_blit16(cx + 48, 142, spr_c[SP_MAGE]);
        g_text_c(cx, 166, "Guard the crystal. Break the heroes.", SPRT.dim);
        char b[40];
        b[0] = 0; g_cat(b, "Best wave "); g_cat_int(b, best_wave); g_cat(b, "   Score "); g_cat_int(b, best_score);
        g_text_c(cx, 182, b, SPRT.gold);
        if ((anim_tick >> 2) & 1) g_text_c(cx, 198, "Press A to begin", SPRT.text);
        g_text_c(cx, 212, "HOME: exit", SPRT.dim);
        display_flush();
        anim_tick++;
        uint32_t k = g_sleep_or_key(90);
        if (k == K_A) return 0;
        if (k == K_HOME || k == K_B) return -1;
    }
}

static void end_screen(int victory)
{
    int score = score_now(), best = score > best_score;
    if (best) { best_score = score; g_save_int("lair/score", best_score); }
    if (cleared > best_wave) { best_wave = cleared; g_save_int("lair/best", best_wave); }
    g_flush_input();
    for (int t = 0;; t++) {
        display_clear(0x0000);
        char b[40];
        if (victory) {
            g_text_large_c(SCR_W / 2, 16, "THE KINGDOM YIELDS!", SPRT.yellow);
            spr_blit_scaled(SCR_W / 2 - 16, 42 - ((t >> 2) & 1), spr_c[SP_CRYSTAL], 16, 16, 2, scratch);
            g_text_c(SCR_W / 2, 82, "Twenty waves broken on your walls.", SPRT.cyan);
        } else {
            g_text_large_c(SCR_W / 2, 16, "THE CRYSTAL IS LOST", SPRT.red);
            spr_blit_scaled(SCR_W / 2 - 16, 42, spr_c[SP_WARRIOR], 16, 16, 2, scratch);
            g_text_c(SCR_W / 2, 82, "The heroes carry off the last shard.", SPRT.text);
        }
        b[0] = 0; g_cat(b, "Waves held "); g_cat_int(b, cleared); g_cat(b, "   Heroes slain "); g_cat_int(b, kills);
        g_text_c(SCR_W / 2, 110, b, SPRT.text);
        b[0] = 0; g_cat(b, "Score "); g_cat_int(b, score);
        g_text_large_c(SCR_W / 2, 132, b, SPRT.gold);
        if (best) { if ((t >> 2) & 1) g_text_c(SCR_W / 2, 156, "NEW BEST!", SPRT.yellow); }
        else { b[0] = 0; g_cat(b, "Best "); g_cat_int(b, best_score); g_text_c(SCR_W / 2, 156, b, SPRT.dim); }
        g_text_c(SCR_W / 2, 200, victory ? "A: keep defending" : "A: continue", (t >> 2) & 1 ? SPRT.text : SPRT.dim);
        display_flush();
        uint32_t k = g_sleep_or_key(120);
        if (t > 4 && (k == K_A || k == K_B || k == K_HOME)) return;
    }
}

/* ── Main loop ───────────────────────────────────────────────────────── */
static void run(void)
{
    new_lair();
    for (;;) {
        uint32_t k = g_key();
        if (phase == PH_BUILD) {
            if (k == K_UP && cur_y > 0) cur_y--;
            else if (k == K_DOWN && cur_y < MAP_H - 1) cur_y++;
            else if (k == K_LEFT && cur_x > 0) cur_x--;
            else if (k == K_RIGHT && cur_x < MAP_W - 1) cur_x++;
            else if (k == K_A) {
                if (occ_m[cur_y][cur_x] || trap[cur_y][cur_x]) inspect_menu();
                else if (buildable(cur_x, cur_y)) build_menu();
                else msg(cur_x <= 2 && map[cur_y][cur_x] == T_FLOOR ? "Too close to the gate."
                                                                    : "You can only build on floor.", SPRT.dim);
            } else if (k == K_X) start_raid();
            else if (k == K_Y) show_route ^= 1;
            else if (k == K_HOME) {
                int r = pause_menu();
                if (r == 1) { restart_req = 1; return; }
                if (r == 2) { quit_req = 1; return; }
            }
            frame();
        } else {
            if (k == K_A) fast ^= 1;
            else if (k == K_HOME) {
                int r = pause_menu();
                if (r == 1) { restart_req = 1; return; }
                if (r == 2) { quit_req = 1; return; }
            }
            int going = 1;
            for (int s = 0; s < (fast ? 3 : 1) && going; s++) going = sim_tick();
            frame();
            if (!going) {
                if (shards <= 0) {
                    shards = 0;
                    for (int f = 0; f < 10; f++) { frame(); delay(30000); }
                    end_screen(0);
                    return;
                }
                end_wave();
                for (int f = 0; f < 6; f++) frame();
                if (cleared == WIN_WAVE && !won_once) { won_once = 1; end_screen(1); g_flush_input(); }
                if (cleared % 3 == 0) choose_perk();
                g_flush_input();
            }
        }
        delay(5000);
    }
}

int main(void)
{
    printf("AkiraOS Crystal Lair");
    g_init();
    SCR_W = G_W; SCR_H = G_H;
    OX = (SCR_W - MAP_W * TS) / 2;
    build_sprites();
    best_wave = g_load_int("lair/best", 0);
    best_score = g_load_int("lair/score", 0);
    for (;;) {
        if (title_screen() < 0) break;
        do { g_seed_random(); run(); } while (restart_req);
    }
    app_switch("supervisor");
    return 0;
}
