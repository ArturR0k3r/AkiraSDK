/**
 * @file main.c
 * @brief Card Crawl — a Slay-the-Spire-style roguelite deckbuilder.
 *
 * Turn-based card battles over a branching node map.  Play cards from a hand
 * for energy to deal damage / gain block / apply statuses; enemies telegraph
 * their intent so every turn is a small puzzle.  Beat the boss on the last
 * row to win the run.  Best floor reached is saved to NVS.
 *
 * Controls:
 *   MAP     : LEFT/RIGHT pick a node   A enter   B quit to launcher
 *   BATTLE  : LEFT/RIGHT pick card  A play  X change target  B end turn
 *   REWARD  : LEFT/RIGHT pick       A take   B skip
 *
 * @copyright Copyright (c) 2026 PenEngineering S.R.L
 * @license Apache-2.0
 */

#include "akira_api.h"

/* ── Display ─────────────────────────────────────────────────────────── */
static int32_t SCR_W = 320;
static int32_t SCR_H = 240;

/* ── Palette ─────────────────────────────────────────────────────────── */
#define C_BG      0x0000
#define C_HDR     0x2965
#define C_TEXT    0xFFFF
#define C_DIM     0x8410
#define C_ATK     0xF9A0   /* orange-red attack cards */
#define C_SKL     0x4C9F   /* blue skill cards        */
#define C_PWR     0xD69F   /* purple power cards       */
#define C_CARDBG  0x18C3
#define C_SEL     0xFFE0   /* yellow selection        */
#define C_HP      0x07E6
#define C_HP_BG   0x3186
#define C_BLOCK   0x5DFF
#define C_ENERGY  0xFD20
#define C_ENEMY   0xF808
#define C_INTENT  0xFFE0
#define C_VULN    0xF81F
#define C_WEAK    0x9E6F
#define C_POISON  0x2FE6
#define C_STR     0xFC00
#define C_RELIC   0xFEA0
#define C_GOLD    0xFFE0
#define C_WIN     0x07E0
#define C_LOSE    0xF800

/* ── Cards ───────────────────────────────────────────────────────────── */
enum { K_ATK=0, K_SKL, K_PWR };
enum { E_NONE=0, E_VULN, E_WEAK, E_POISON, E_DRAW, E_STR };

typedef struct {
    const char *name;
    uint8_t cost, kind;
    int16_t dmg, blk;
    uint8_t eff, ev;     /* effect + its magnitude */
    uint8_t aoe;
} carddef_t;

enum {
    CARD_STRIKE=0, CARD_DEFEND, CARD_BASH, CARD_IRONWAVE, CARD_POMMEL,
    CARD_CLEAVE, CARD_POISONDART, CARD_SHRUG, CARD_FLEX, CARD_HEAVY,
    CARD_SECWIND, CARD_QUICK, CARD_COUNT
};

static const carddef_t CARDS[CARD_COUNT] = {
    /* name          cost kind   dmg blk  eff        ev aoe */
    { "Strike",      1, K_ATK,   6,  0,  E_NONE,    0, 0 },
    { "Defend",      1, K_SKL,   0,  5,  E_NONE,    0, 0 },
    { "Bash",        2, K_ATK,   8,  0,  E_VULN,    2, 0 },
    { "Iron Wave",   1, K_ATK,   5,  5,  E_NONE,    0, 0 },
    { "Pommel",      1, K_ATK,   9,  0,  E_DRAW,    1, 0 },
    { "Cleave",      1, K_ATK,   8,  0,  E_NONE,    0, 1 },
    { "Poison Dart", 1, K_ATK,   4,  0,  E_POISON,  3, 0 },
    { "Shrug Off",   1, K_SKL,   0,  8,  E_DRAW,    1, 0 },
    { "Flex",        0, K_PWR,   0,  0,  E_STR,     2, 0 },
    { "Heavy Blow",  2, K_ATK,  14,  0,  E_NONE,    0, 0 },
    { "Second Wind", 1, K_SKL,   0, 10,  E_NONE,    0, 0 },
    { "Quick Draw",  0, K_SKL,   0,  0,  E_DRAW,    2, 0 },
};
/* Cards that can appear as rewards (not the basic Strike/Defend). */
static const uint8_t REWARD_POOL[] = {
    CARD_BASH, CARD_IRONWAVE, CARD_POMMEL, CARD_CLEAVE, CARD_POISONDART,
    CARD_SHRUG, CARD_FLEX, CARD_HEAVY, CARD_SECWIND, CARD_QUICK
};
#define REWARD_POOL_N (int)(sizeof(REWARD_POOL)/sizeof(REWARD_POOL[0]))

/* ── Deck / piles ────────────────────────────────────────────────────── */
#define DECK_MAX 64
#define HAND_MAX 10
static uint8_t deck[DECK_MAX];  static int deck_n;    /* master deck */
static uint8_t draw_pile[DECK_MAX]; static int draw_n;
static uint8_t disc_pile[DECK_MAX]; static int disc_n;
static uint8_t hand[HAND_MAX];  static int hand_n;

/* ── Relics ──────────────────────────────────────────────────────────── */
enum { REL_VAJRA=0, REL_SCALES, REL_VIAL, REL_CORE, REL_COUNT };
static const char *RELIC_NAME[REL_COUNT] = {
    "Vajra", "Bronze Scales", "Blood Vial", "Energy Core"
};
static const char *RELIC_DESC[REL_COUNT] = {
    "+1 Strength each combat", "+3 Block each turn",
    "Heal 4 each combat", "+1 Energy each turn"
};
static uint8_t relics[REL_COUNT];
static int has_relic(int r){ return relics[r]; }

/* ── Player ──────────────────────────────────────────────────────────── */
static int p_hp, p_max_hp, p_block, p_energy, p_max_energy;
static int p_str, p_vuln, p_weak, p_poison;
static int gold;

/* ── Enemies ─────────────────────────────────────────────────────────── */
#define MOB_MAX 3
enum { IN_ATTACK=0, IN_BLOCK, IN_BUFF, IN_DEBUFF };
typedef struct {
    const char *name;
    int hp, max_hp, block;
    int str, vuln, weak, poison;
    int intent, intent_val;
    uint8_t kind;    /* mob archetype */
    uint8_t alive;
} mob_t;
enum { MB_RAT=0, MB_GOBLIN, MB_BRUTE, MB_SHAMAN, MB_BOSS };
static mob_t mobs[MOB_MAX];
static int mob_n;

/* ── Map ─────────────────────────────────────────────────────────────── */
#define MAP_ROWS 10
#define MAP_COLS 3
enum { N_NONE=0, N_BATTLE, N_ELITE, N_REST, N_TREASURE, N_BOSS };
static uint8_t node_type[MAP_ROWS][MAP_COLS];
static uint8_t node_link[MAP_ROWS][MAP_COLS];  /* bitmask of reachable next cols */
static int cur_row;     /* row the player has cleared up to (-1 = start) */
static int cur_col;
static int sel_col;     /* highlighted next-row column */

/* ── Run / meta ──────────────────────────────────────────────────────── */
static int floor_reached;
static int best_floor;

/* ── Game state machine ──────────────────────────────────────────────── */
enum { ST_MAP, ST_BATTLE, ST_REWARD, ST_REST, ST_TREASURE, ST_GAMEOVER, ST_WIN };
static int state;

/* ── PRNG ────────────────────────────────────────────────────────────── */
static uint32_t rng_state = 1;
static int rng_next(int mod){ rng_state=rng_state*1103515245u+12345u;
    return (int)((rng_state>>16)&0x7FFF)%(mod>0?mod:1); }
static void reseed(uint32_t mix){
    uint32_t s=0;
    if (!(crypto_random(&s,sizeof(s))==0 && s!=0)) s=(uint32_t)rtc_get_uptime_ms()^(mix*2654435761u);
    rng_state = s ^ 0x5151A5A5u; rng_next(7); rng_next(7); rng_next(7);
}

/* ── Small helpers ───────────────────────────────────────────────────── */
static int clampi(int v,int lo,int hi){ return v<lo?lo:(v>hi?hi:v); }
static uint16_t lighten(uint16_t c,int a){ int r=(c>>11)&31,g=(c>>5)&63,b=c&31;
    r+=a;g+=a*2;b+=a; if(r>31)r=31;if(g>63)g=63;if(b>31)b=31; return (uint16_t)((r<<11)|(g<<5)|b); }
static uint16_t darken(uint16_t c,int a){ int r=(c>>11)&31,g=(c>>5)&63,b=c&31;
    r-=a;g-=a*2;b-=a; if(r<0)r=0;if(g<0)g=0;if(b<0)b=0; return (uint16_t)((r<<11)|(g<<5)|b); }
static int text_px(const char*s){ int n=0; while(s&&s[n])n++; return n*6; }

static int str_to_int(const char*s){ int v=0; for(int i=0;s[i]>='0'&&s[i]<='9';i++) v=v*10+(s[i]-'0'); return v; }
static void int_to_str(int v,char*b){ char t[12]; int n=0,p=0; if(v<0){b[p++]='-';v=-v;}
    if(v==0)t[n++]='0'; while(v>0&&n<11){t[n++]=(char)('0'+v%10);v/=10;} while(n>0)b[p++]=t[--n]; b[p]='\0'; }

static void load_best(void){ char b[16]; best_floor=(settings_get("crawl/best",b,sizeof(b))==0)?str_to_int(b):0; }
static void save_best(void){ if(floor_reached>best_floor){ best_floor=floor_reached;
    char b[16]; int_to_str(best_floor,b); settings_set("crawl/best",b);} }

/* ── Deck helpers ────────────────────────────────────────────────────── */
static void deck_add(int card){ if(deck_n<DECK_MAX) deck[deck_n++]=(uint8_t)card; }

static void init_run(void) {
    deck_n=0;
    for (int i=0;i<5;i++) deck_add(CARD_STRIKE);
    for (int i=0;i<4;i++) deck_add(CARD_DEFEND);
    deck_add(CARD_BASH);

    p_max_hp=60; p_hp=60; p_max_energy=3;
    p_str=p_vuln=p_weak=p_poison=0; gold=0;
    for (int i=0;i<REL_COUNT;i++) relics[i]=0;
    floor_reached=0;
}

/* Fisher-Yates over the draw pile. */
static void shuffle_draw(void) {
    for (int i=draw_n-1;i>0;i--){ int j=rng_next(i+1);
        uint8_t t=draw_pile[i]; draw_pile[i]=draw_pile[j]; draw_pile[j]=t; }
}

static void reshuffle_from_discard(void) {
    for (int i=0;i<disc_n;i++) draw_pile[draw_n++]=disc_pile[i];
    disc_n=0;
    shuffle_draw();
}

static void draw_cards(int n) {
    for (int k=0;k<n;k++) {
        if (hand_n>=HAND_MAX) break;
        if (draw_n==0) reshuffle_from_discard();
        if (draw_n==0) break;             /* nothing left anywhere */
        hand[hand_n++]=draw_pile[--draw_n];
    }
}

static void discard_hand(void) {
    for (int i=0;i<hand_n;i++) disc_pile[disc_n++]=hand[i];
    hand_n=0;
}

/* ── Enemy setup ─────────────────────────────────────────────────────── */
static void set_mob(int i, int kind, int floor) {
    mob_t *m=&mobs[i];
    m->kind=(uint8_t)kind; m->alive=1; m->block=0;
    m->str=m->vuln=m->weak=m->poison=0;
    int f=floor;
    switch (kind) {
    case MB_RAT:    m->name="Rat";    m->max_hp=10+f*2; break;
    case MB_GOBLIN: m->name="Goblin"; m->max_hp=14+f*2; break;
    case MB_BRUTE:  m->name="Brute";  m->max_hp=24+f*3; break;
    case MB_SHAMAN: m->name="Shaman"; m->max_hp=18+f*2; break;
    default:        m->name="OVERLORD"; m->max_hp=70+f*8; break;
    }
    m->hp=m->max_hp;
    m->intent=IN_ATTACK; m->intent_val=0;
}

static void roll_intent(int i, int floor) {
    mob_t *m=&mobs[i];
    m->block=0;                       /* enemy block resets on its turn plan */
    int r=rng_next(10);
    switch (m->kind) {
    case MB_RAT:    m->intent=IN_ATTACK; m->intent_val=4+floor; break;
    case MB_GOBLIN: if (r<7){m->intent=IN_ATTACK;m->intent_val=6+floor;}
                    else {m->intent=IN_DEBUFF;m->intent_val=0;} break;
    case MB_BRUTE:  if (r<7){m->intent=IN_ATTACK;m->intent_val=10+floor+floor/2;}
                    else {m->intent=IN_BLOCK;m->intent_val=8;} break;
    case MB_SHAMAN: if (r<4){m->intent=IN_BUFF;m->intent_val=2;}
                    else if (r<7){m->intent=IN_DEBUFF;m->intent_val=0;}
                    else {m->intent=IN_ATTACK;m->intent_val=6+floor;} break;
    default: /* boss cycles */
                    if (r<5){m->intent=IN_ATTACK;m->intent_val=14+floor;}
                    else if (r<8){m->intent=IN_ATTACK;m->intent_val=8+floor;} /* + vuln */
                    else {m->intent=IN_BUFF;m->intent_val=3;} break;
    }
}

static int mobs_alive(void){ int n=0; for(int i=0;i<mob_n;i++) if(mobs[i].alive) n++; return n; }
static int first_alive(void){ for(int i=0;i<mob_n;i++) if(mobs[i].alive) return i; return 0; }

/* ── Combat: damage application ──────────────────────────────────────── */
static int atk_after_mods(int base, int str, int weak){
    int d=base+str; if(weak>0) d=d*3/4; if(d<0)d=0; return d;
}
static void damage_mob(int i, int raw) {
    mob_t *m=&mobs[i];
    int d=raw; if(m->vuln>0) d=d*3/2;
    int after=d-m->block; m->block-=d; if(m->block<0)m->block=0;
    if(after>0){ m->hp-=after; if(m->hp<=0){m->hp=0;m->alive=0;} }
}
static void damage_player(int raw) {
    int d=raw; if(p_vuln>0) d=d*3/2;
    int after=d-p_block; p_block-=d; if(p_block<0)p_block=0;
    if(after>0){ p_hp-=after; if(p_hp<0)p_hp=0; }
}

/* ── Battle setup ────────────────────────────────────────────────────── */
static int battle_floor;
static int is_elite, is_boss;
static int target;        /* selected enemy index */
static int hand_cur;      /* selected card in hand */
static char log_msg[40]; static int log_timer;
static void blog(const char*s){ int i; for(i=0;s[i]&&i<39;i++)log_msg[i]=s[i]; log_msg[i]='\0'; log_timer=40; }

static void start_player_turn(void);

static void start_battle(int floor, int elite, int boss) {
    battle_floor=floor; is_elite=elite; is_boss=boss;

    /* Compose the encounter. */
    if (boss) { mob_n=1; set_mob(0,MB_BOSS,floor); }
    else if (elite) { mob_n=1; set_mob(0,MB_BRUTE,floor);
                      mobs[0].max_hp += 10; mobs[0].hp=mobs[0].max_hp; }
    else {
        mob_n = 1 + rng_next(2);            /* 1-2 foes */
        for (int i=0;i<mob_n;i++) {
            int k = (floor<3) ? (rng_next(2)?MB_RAT:MB_GOBLIN)
                              : (rng_next(3)==0?MB_SHAMAN:(rng_next(2)?MB_GOBLIN:MB_BRUTE));
            set_mob(i,k,floor);
        }
    }

    /* Fresh combat state + relics that trigger at combat start. */
    p_block=0; p_vuln=0; p_weak=0; p_poison=0; p_str=0;
    if (has_relic(REL_VAJRA)) p_str += 1;
    if (has_relic(REL_VIAL))  { p_hp=clampi(p_hp+4,0,p_max_hp); }

    /* Build draw pile from the whole deck. */
    draw_n=0; disc_n=0; hand_n=0;
    for (int i=0;i<deck_n;i++) draw_pile[draw_n++]=deck[i];
    shuffle_draw();

    for (int i=0;i<mob_n;i++) roll_intent(i,floor);
    target=first_alive();
    hand_cur=0;
    state=ST_BATTLE;
    start_player_turn();
}

static void start_player_turn(void) {
    p_energy = p_max_energy + (has_relic(REL_CORE)?1:0);
    p_block  = has_relic(REL_SCALES) ? 3 : 0;
    /* Poison ticks at the start of your turn. */
    if (p_poison>0){ p_hp-=p_poison; if(p_hp<0)p_hp=0; p_poison--; }
    draw_cards(5);
    hand_cur=0;
}

/* ── Play a card from the hand ───────────────────────────────────────── */
static void apply_card_effect(int eff, int ev, int tgt) {
    switch (eff) {
    case E_VULN:   if(tgt>=0) mobs[tgt].vuln += ev; break;
    case E_WEAK:   if(tgt>=0) mobs[tgt].weak += ev; break;
    case E_POISON: if(tgt>=0) mobs[tgt].poison += ev; break;
    case E_DRAW:   draw_cards(ev); break;
    case E_STR:    p_str += ev; break;
    default: break;
    }
}

static void play_card(int hi) {
    if (hi<0 || hi>=hand_n) return;
    int id=hand[hi];
    const carddef_t *c=&CARDS[id];
    if (p_energy < c->cost) { blog("Not enough energy."); return; }
    p_energy -= c->cost;

    if (c->blk>0) p_block += c->blk;

    if (c->dmg>0) {
        int raw = atk_after_mods(c->dmg, p_str, p_weak);
        if (c->aoe) { for(int i=0;i<mob_n;i++) if(mobs[i].alive) damage_mob(i,raw); }
        else if (mobs[target].alive) damage_mob(target,raw);
        else { int t=first_alive(); if(mobs_alive()) damage_mob(t,raw); }
    }

    /* Effects that target an enemy use the current target. */
    int et = mobs[target].alive ? target : first_alive();
    apply_card_effect(c->eff, c->ev, (c->eff==E_DRAW||c->eff==E_STR)?-1:et);

    /* Move the played card to discard. */
    disc_pile[disc_n++]=(uint8_t)id;
    for (int i=hi;i<hand_n-1;i++) hand[i]=hand[i+1];
    hand_n--;
    if (hand_cur>=hand_n) hand_cur=hand_n>0?hand_n-1:0;

    if (!mobs[target].alive) target=first_alive();
    blog(c->name);
}

/* ── Enemy phase ─────────────────────────────────────────────────────── */
static void enemy_phase(void) {
    for (int i=0;i<mob_n;i++) {
        mob_t *m=&mobs[i];
        if (!m->alive) continue;
        /* Enemy poison ticks as it acts. */
        if (m->poison>0){ m->hp-=m->poison; if(m->hp<=0){m->hp=0;m->alive=0;} m->poison--; }
        if (!m->alive) continue;

        switch (m->intent) {
        case IN_ATTACK: {
            int raw=atk_after_mods(m->intent_val, m->str, m->weak);
            damage_player(raw);
            /* The boss's medium swing also makes you Vulnerable. */
            if (m->kind==MB_BOSS && m->intent_val < 14+battle_floor) p_vuln+=1;
            break;
        }
        case IN_BLOCK:  m->block += m->intent_val; break;
        case IN_BUFF:   for(int j=0;j<mob_n;j++) if(mobs[j].alive) mobs[j].str+=m->intent_val; break;
        case IN_DEBUFF: p_weak+=2; break;
        }
        if (p_hp<=0) return;
    }

    /* End-of-round upkeep: statuses tick down. */
    if (p_vuln>0) p_vuln--;
    if (p_weak>0) p_weak--;
    for (int i=0;i<mob_n;i++){ if(mobs[i].vuln>0)mobs[i].vuln--; if(mobs[i].weak>0)mobs[i].weak--; }
}

static void end_turn(void) {
    discard_hand();
    enemy_phase();
    if (p_hp<=0) { state=ST_GAMEOVER; save_best(); return; }
    if (mobs_alive()==0) return;      /* victory handled by caller */
    for (int i=0;i<mob_n;i++) if(mobs[i].alive) roll_intent(i,battle_floor);
    start_player_turn();
}

/* ── Rewards ─────────────────────────────────────────────────────────── */
static int reward_cards[3];
static int reward_cur;
static void roll_card_reward(void) {
    for (int i=0;i<3;i++) {
        int pick, dup;
        do { pick=REWARD_POOL[rng_next(REWARD_POOL_N)]; dup=0;
             for(int j=0;j<i;j++) if(reward_cards[j]==pick) dup=1;
        } while (dup);
        reward_cards[i]=pick;
    }
    reward_cur=0;
}

static void grant_random_relic(void) {
    int owned=0; for(int i=0;i<REL_COUNT;i++) owned+=relics[i];
    if (owned>=REL_COUNT) { gold+=50; blog("All relics owned! +50g"); return; }
    int r; do { r=rng_next(REL_COUNT); } while (relics[r]);
    relics[r]=1;
    blog(RELIC_NAME[r]);
}

/* ── Map generation ──────────────────────────────────────────────────── */
static void gen_map(void) {
    for (int r=0;r<MAP_ROWS;r++)
        for (int c=0;c<MAP_COLS;c++){ node_type[r][c]=N_NONE; node_link[r][c]=0; }

    /* Row 0: two entry nodes; last row: single boss; middle: 2-3 nodes. */
    for (int r=0;r<MAP_ROWS;r++) {
        if (r==MAP_ROWS-1) { node_type[r][1]=N_BOSS; continue; }
        int cols = 2 + rng_next(2);           /* 2-3 nodes */
        int start = (cols==3)?0:rng_next(2);
        for (int k=0;k<cols;k++) {
            int c=start+k; if(c>=MAP_COLS) c=MAP_COLS-1;
            int t;
            if (r==0) t=N_BATTLE;
            else {
                int roll=rng_next(10);
                if (roll<5) t=N_BATTLE;
                else if (roll<7) t=N_ELITE;
                else if (roll<9) t=N_REST;
                else t=N_TREASURE;
            }
            node_type[r][c]=(uint8_t)t;
        }
    }

    /* Link each node to 1-2 nodes in the next row (same or adjacent column). */
    for (int r=0;r<MAP_ROWS-1;r++)
        for (int c=0;c<MAP_COLS;c++) {
            if (node_type[r][c]==N_NONE) continue;
            for (int dc=-1;dc<=1;dc++){ int nc=c+dc;
                if (nc<0||nc>=MAP_COLS) continue;
                if (node_type[r+1][nc]!=N_NONE) node_link[r][c] |= (1<<nc);
            }
            /* Guarantee at least one link forward. */
            if (node_link[r][c]==0) {
                for (int nc=0;nc<MAP_COLS;nc++) if(node_type[r+1][nc]!=N_NONE){ node_link[r][c]|=(1<<nc); break; }
            }
        }

    cur_row=-1; cur_col=1;
    /* First selectable = leftmost existing node in row 0. */
    sel_col=0; while(sel_col<MAP_COLS && node_type[0][sel_col]==N_NONE) sel_col++;
    if (sel_col>=MAP_COLS) sel_col=0;
}

/* Is (row,col) reachable from the player's current node? */
static int node_reachable(int row, int col) {
    if (node_type[row][col]==N_NONE) return 0;
    if (cur_row<0) return (row==0);
    if (row!=cur_row+1) return 0;
    return (node_link[cur_row][cur_col] & (1<<col)) != 0;
}

/* ── Rendering: shared ───────────────────────────────────────────────── */
static void header(const char*title,const char*right){
    display_rect(0,0,SCR_W,18,C_HDR);
    display_text(6,5,title,C_TEXT);
    if(right) display_text(SCR_W-6-text_px(right),5,right,C_TEXT);
}

/* ── Rendering: map ──────────────────────────────────────────────────── */
static uint16_t node_col(int t){
    switch(t){ case N_BATTLE:return C_ENEMY; case N_ELITE:return C_STR;
        case N_REST:return C_HP; case N_TREASURE:return C_GOLD;
        case N_BOSS:return C_LOSE; default:return C_DIM; }
}
static const char *node_tag(int t){
    switch(t){ case N_BATTLE:return "!"; case N_ELITE:return "E";
        case N_REST:return "+"; case N_TREASURE:return "$"; case N_BOSS:return "B"; default:return " "; }
}

static void draw_map(void) {
    display_clear(C_BG);
    char fr[16]="FLOOR "; int_to_str(floor_reached,fr+6);
    header("CARD CRAWL", fr);

    int top=26, bot=SCR_H-30;
    int rowh=(bot-top)/MAP_ROWS;
    int colw=SCR_W/(MAP_COLS+1);

    /* Links */
    for (int r=0;r<MAP_ROWS-1;r++)
        for (int c=0;c<MAP_COLS;c++){
            if(node_type[r][c]==N_NONE) continue;
            int x0=colw*(c+1), y0=bot-r*rowh;
            for(int nc=0;nc<MAP_COLS;nc++) if(node_link[r][c]&(1<<nc)){
                int x1=colw*(nc+1), y1=bot-(r+1)*rowh;
                display_line(x0,y0,x1,y1,C_DIM);
            }
        }
    /* Nodes */
    for (int r=0;r<MAP_ROWS;r++)
        for (int c=0;c<MAP_COLS;c++){
            if(node_type[r][c]==N_NONE) continue;
            int x=colw*(c+1), y=bot-r*rowh;
            int reach=node_reachable(r,c);
            uint16_t col=node_col(node_type[r][c]);
            display_circle_fill(x,y,7, reach?col:C_CARDBG);
            display_circle(x,y,7, reach?C_TEXT:C_DIM);
            display_text(x-3,y-3, node_tag(node_type[r][c]), reach?C_BG:C_DIM);
            if (r==cur_row+1 && c==sel_col && reach)
                display_circle(x,y,10,C_SEL);
        }
    /* Player marker */
    if (cur_row>=0){ int x=colw*(cur_col+1), y=bot-cur_row*rowh;
        display_circle(x,y,11,C_WIN); }

    display_rect(0,SCR_H-14,SCR_W,14,C_HDR);
    display_text(4,SCR_H-12,"L/R pick  A enter  B quit  (!battle E elite +rest $loot B boss)",C_DIM);
    display_flush();
}

/* ── Rendering: battle ───────────────────────────────────────────────── */
static void draw_status_tags(int x,int y,int vuln,int weak,int poison,int str){
    if(str>0){ display_text(x,y,"S",C_STR); display_number(x+7,y,str,C_STR); x+=22; }
    if(vuln>0){ display_text(x,y,"V",C_VULN); display_number(x+7,y,vuln,C_VULN); x+=22; }
    if(weak>0){ display_text(x,y,"W",C_WEAK); display_number(x+7,y,weak,C_WEAK); x+=22; }
    if(poison>0){ display_text(x,y,"P",C_POISON); display_number(x+7,y,poison,C_POISON); x+=22; }
}

/* ── Enemy sprites (centred on cx,cy; ~a 40px footprint) ─────────────── */
#define C_PINK 0xFB56
static void eye2(int lx,int rx,int y,uint16_t s,uint16_t p){
    display_circle_fill(lx,y,3,s); display_pixel(lx,y,p);
    display_circle_fill(rx,y,3,s); display_pixel(rx,y,p);
}

static void draw_mob_sprite(int kind, int cx, int cy) {
    switch (kind) {
    case MB_RAT: {
        uint16_t b=0xC618, s=darken(b,4), h=lighten(b,3);
        display_line(cx+11,cy+7,cx+22,cy+1,s);           /* tail */
        display_circle_fill(cx-9,cy-8,4,b); display_circle_fill(cx+9,cy-8,4,b);
        display_circle_fill(cx-9,cy-8,2,C_PINK); display_circle_fill(cx+9,cy-8,2,C_PINK);
        display_circle_fill(cx,cy+2,13,s);
        display_circle_fill(cx,cy,12,b);
        display_circle_fill(cx-4,cy-4,5,h);
        display_circle_fill(cx,cy+5,4,h);                /* snout */
        display_pixel(cx,cy+6,0x0000);
        eye2(cx-5,cx+5,cy,0x0000,0xFFFF);
        break;
    }
    case MB_GOBLIN: {
        uint16_t b=0x3E68, s=darken(b,4), h=lighten(b,4);
        display_triangle_fill(cx-15,cy-2,cx-9,cy-14,cx-5,cy-2,b);  /* ears */
        display_triangle_fill(cx+15,cy-2,cx+9,cy-14,cx+5,cy-2,b);
        display_circle_fill(cx,cy+2,14,s);
        display_circle_fill(cx,cy,13,b);
        display_circle_fill(cx-4,cy-4,6,h);
        eye2(cx-5,cx+5,cy-1,0xFFE0,0x0000);
        display_hline(cx-5,cy+7,10,0x0000);              /* grin */
        display_rect(cx-3,cy+7,2,3,0xFFFF);              /* tooth */
        break;
    }
    case MB_BRUTE: {
        uint16_t b=0x9A44, s=darken(b,5), h=lighten(b,4);
        display_triangle_fill(cx-16,cy-8,cx-11,cy-20,cx-7,cy-8,0xEEEE);  /* horns */
        display_triangle_fill(cx+16,cy-8,cx+11,cy-20,cx+7,cy-8,0xEEEE);
        display_circle_fill(cx,cy+3,17,s);
        display_circle_fill(cx,cy,16,b);
        display_circle_fill(cx-5,cy-5,7,h);
        display_rect(cx-10,cy-4,20,3,s);                 /* heavy brow */
        eye2(cx-6,cx+6,cy+1,0xF800,0xFFFF);
        display_hline(cx-6,cy+9,12,0x0000);
        display_rect(cx-14,cy+12,6,6,b); display_rect(cx+8,cy+12,6,6,b); /* fists */
        break;
    }
    case MB_SHAMAN: {
        uint16_t b=0x8010, s=darken(b,4), h=lighten(b,5);
        display_triangle_fill(cx-14,cy+16,cx,cy-14,cx+14,cy+16,b);  /* robe */
        display_triangle_fill(cx-8,cy+16,cx,cy-6,cx+8,cy+16,s);
        display_circle_fill(cx,cy-8,8,darken(b,2));      /* hood */
        display_circle_fill(cx,cy-6,4,0x18C3);           /* shadowed face */
        display_pixel(cx-2,cy-6,0xFFE0); display_pixel(cx+2,cy-6,0xFFE0);
        display_vline(cx+13,cy-14,26,0x9E7F);            /* staff */
        display_circle_fill(cx+13,cy-14,4,h);
        display_circle_fill(cx+13,cy-14,2,0xFFFF);
        break;
    }
    default: { /* MB_BOSS */
        uint16_t b=0xC000, s=darken(b,5), h=lighten(b,6);
        display_triangle_fill(cx-18,cy-6,cx-14,cy-22,cx-9,cy-6,0xF9E0);
        display_triangle_fill(cx-6,cy-8,cx,cy-26,cx+6,cy-8,0xF9E0);
        display_triangle_fill(cx+18,cy-6,cx+14,cy-22,cx+9,cy-6,0xF9E0);
        display_circle_fill(cx,cy+3,19,s);
        display_circle_fill(cx,cy,18,b);
        display_circle_fill(cx-6,cy-6,8,h);
        eye2(cx-7,cx+7,cy,0xFFE0,0xF800);
        display_triangle_fill(cx-8,cy+8,cx,cy+15,cx+8,cy+8,0x0000);   /* maw */
        display_rect(cx-6,cy+8,2,4,0xFFFF); display_rect(cx+4,cy+8,2,4,0xFFFF);
        break;
    }
    }
}

/* Intent shown as an icon + number so it reads at a glance. */
static void draw_intent(int x,int y,mob_t*m){
    switch(m->intent){
    case IN_ATTACK: {                                    /* sword + damage */
        int d=atk_after_mods(m->intent_val,m->str,m->weak);
        display_line(x,y+8,x+8,y,C_INTENT);
        display_line(x+1,y+8,x+9,y,C_INTENT);
        display_line(x-1,y+7,x+2,y+10,C_INTENT);         /* hilt */
        display_number(x+14,y+2,d,C_INTENT);
        break;
    }
    case IN_BLOCK:                                       /* shield */
        display_triangle_fill(x,y,x+10,y,x+5,y+11,C_BLOCK);
        display_rect(x,y,10,5,C_BLOCK);
        display_rect_outline(x,y,10,5,darken(C_BLOCK,4));
        break;
    case IN_BUFF:                                        /* up arrow */
        display_triangle_fill(x,y+8,x+5,y,x+10,y+8,C_STR);
        display_rect(x+3,y+6,4,6,C_STR);
        break;
    case IN_DEBUFF:                                      /* skull */
        display_circle_fill(x+5,y+4,5,C_WEAK);
        display_rect(x+2,y+8,6,3,C_WEAK);
        display_pixel(x+3,y+3,0x0000); display_pixel(x+7,y+3,0x0000);
        break;
    }
}

static void draw_battle(void) {
    display_clear(C_BG);
    char es[16]="ENERGY "; int_to_str(p_energy,es+7);
    header(is_boss?"BOSS":(is_elite?"ELITE":"BATTLE"), es);

    /* Enemies across the top */
    int ey=30;
    for (int i=0;i<mob_n;i++){
        mob_t *m=&mobs[i];
        int ex=20 + i*(SCR_W-40)/ (mob_n>0?mob_n:1);
        if (!m->alive){ display_text(ex,ey+20,"(dead)",C_DIM); continue; }
        /* detailed archetype sprite */
        draw_mob_sprite(m->kind, ex+22, ey+20);
        display_text(ex,ey,m->name, i==target?C_SEL:C_TEXT);
        if (i==target) {
            display_text(ex-8,ey+18,">",C_SEL);
            display_circle(ex+22,ey+20,22,C_SEL);        /* target ring */
        }
        /* intent, shown just under the body */
        draw_intent(ex,ey+40,m);
        /* hp bar */
        int bw=54, bx=ex; int hy=ey+52;
        display_rect(bx,hy,bw,7,C_HP_BG);
        display_rect(bx,hy,bw*clampi(m->hp,0,m->max_hp)/(m->max_hp>0?m->max_hp:1),7,C_ENEMY);
        display_rect_outline(bx,hy,bw,7,C_DIM);
        display_number(bx,hy+9,m->hp,C_TEXT); display_text(bx+22,hy+9,"/",C_DIM);
        display_number(bx+28,hy+9,m->max_hp,C_DIM);
        if (m->block>0){ display_text(bx,hy+18,"[",C_BLOCK); display_number(bx+6,hy+18,m->block,C_BLOCK); }
        draw_status_tags(bx+40,hy+18,m->vuln,m->weak,m->poison,m->str);
    }

    /* Player panel */
    int py0=SCR_H-84;
    display_hline(0,py0-2,SCR_W,C_HDR);
    display_text(6,py0,"YOU",C_TEXT);
    int bw=90;
    display_rect(34,py0,bw,9,C_HP_BG);
    display_rect(34,py0,bw*clampi(p_hp,0,p_max_hp)/(p_max_hp>0?p_max_hp:1),9,C_HP);
    display_rect_outline(34,py0,bw,9,C_DIM);
    display_number(34,py0+11,p_hp,C_TEXT); display_text(56,py0+11,"/",C_DIM);
    display_number(62,py0+11,p_max_hp,C_DIM);
    if (p_block>0){ display_text(132,py0,"BLK",C_BLOCK); display_number(154,py0,p_block,C_BLOCK); }
    /* energy pips */
    display_text(180,py0,"E",C_ENERGY); display_number(190,py0,p_energy,C_ENERGY);
    display_text(204,py0,"/",C_DIM); display_number(212,py0,p_max_energy+(has_relic(REL_CORE)?1:0),C_DIM);
    draw_status_tags(6,py0+11,p_vuln,p_weak,p_poison,p_str);

    /* Hand */
    int hy=SCR_H-58;
    int n=hand_n>0?hand_n:1;
    int cw=(SCR_W-8)/ (n>5?n:5);
    if (cw>60) cw=60;
    for (int i=0;i<hand_n;i++){
        const carddef_t *c=&CARDS[hand[i]];
        int sel = (i==hand_cur);
        int x=4+i*cw, w=cw-3, h=44, y=hy - (sel?5:0);   /* selected lifts up */
        uint16_t edge = sel?C_SEL:C_DIM;
        uint16_t kc = c->kind==K_ATK?C_ATK:(c->kind==K_SKL?C_SKL:C_PWR);

        /* Rounded frame with a gradient type-header */
        display_rounded_rect_fill(x,y,w,h,4,C_CARDBG);
        display_rect(x+2,y+2,w-4,7,darken(kc,4));
        display_rect(x+2,y+2,w-4,4,kc);
        display_rounded_rect(x,y,w,h,4,edge);
        if (sel) display_rounded_rect(x-1,y-1,w+2,h+2,4,C_SEL);

        /* Cost gem */
        display_circle_fill(x+8,y+9,6,C_ENERGY);
        display_circle(x+8,y+9,6,0xFFFF);
        display_number(x+5,y+6,c->cost,C_BG);

        display_text(x+3,y+20,c->name,C_TEXT);
        char v[8];
        if (c->dmg>0){ int_to_str(atk_after_mods(c->dmg,p_str,p_weak),v);
            display_text(x+3,y+31,"DMG",C_DIM); display_text(x+26,y+31,v,C_ATK); }
        else if (c->blk>0){ int_to_str(c->blk,v);
            display_text(x+3,y+31,"BLK",C_DIM); display_text(x+26,y+31,v,C_BLOCK); }
        else display_text(x+3,y+31,"util",C_DIM);
    }

    display_rect(0,SCR_H-12,SCR_W,12,C_HDR);
    if (log_timer>0){ display_text(4,SCR_H-11,log_msg,C_TEXT); log_timer--; }
    else display_text(4,SCR_H-11,"L/R card  A play  X target  B end turn",C_DIM);
    display_flush();
}

/* ── Rendering: reward / rest / treasure / end ───────────────────────── */
static void draw_reward(void) {
    display_clear(C_BG);
    header("CARD REWARD", "A take  B skip");
    display_text(SCR_W/2-60,26,"Choose a card to add:",C_DIM);
    for (int i=0;i<3;i++){
        const carddef_t *c=&CARDS[reward_cards[i]];
        int w=SCR_W*70/100, x=(SCR_W-w)/2, y=44+i*52, h=46;
        uint16_t edge=(i==reward_cur)?C_SEL:C_DIM;
        uint16_t kc=c->kind==K_ATK?C_ATK:(c->kind==K_SKL?C_SKL:C_PWR);
        display_rect(x,y,w,h,C_CARDBG);
        display_rect(x,y,6,h,kc);
        display_rect_outline(x,y,w,h,edge);
        display_circle_fill(x+18,y+14,7,C_ENERGY); display_number(x+15,y+10,c->cost,C_BG);
        display_text(x+34,y+8,c->name,C_TEXT);
        char v[8];
        if(c->dmg>0){int_to_str(atk_after_mods(c->dmg,0,0),v);
            display_text(x+34,y+24,"Deal",C_DIM); display_text(x+66,y+24,v,C_ATK);}
        if(c->blk>0){int_to_str(c->blk,v);
            display_text(x+110,y+24,"Block",C_DIM); display_text(x+150,y+24,v,C_BLOCK);}
        const char *ex="";
        switch(c->eff){case E_VULN:ex="+Vulnerable";break;case E_WEAK:ex="+Weak";break;
            case E_POISON:ex="+Poison";break;case E_DRAW:ex="+Draw";break;case E_STR:ex="+Strength";break;}
        if(c->aoe) display_text(x+34,y+34,"all enemies",C_DIM);
        else if(ex[0]) display_text(x+34,y+34,ex,C_DIM);
    }
    display_flush();
}

static void draw_rest(void) {
    display_clear(C_BG);
    header("CAMPFIRE", 0);
    display_text(SCR_W/2-40,SCR_H/2-30,"You rest.",C_TEXT);
    int heal=p_max_hp*30/100;
    display_text(SCR_W/2-60,SCR_H/2-6,"Recover HP:",C_DIM);
    display_number(SCR_W/2+30,SCR_H/2-6,heal,C_HP);
    display_text(SCR_W/2-60,SCR_H/2+30,"Press A to continue",C_DIM);
    display_flush();
}

static void draw_treasure(void) {
    display_clear(C_BG);
    header("TREASURE", 0);
    display_text(SCR_W/2-50,40,"You found a relic!",C_GOLD);
    /* relic list */
    int y=70;
    for (int i=0;i<REL_COUNT;i++){
        uint16_t col=relics[i]?C_RELIC:C_DIM;
        display_circle_fill(30,y+4,5,col);
        display_text(44,y,RELIC_NAME[i],col);
        display_text(150,y,RELIC_DESC[i],relics[i]?C_TEXT:C_DIM);
        y+=20;
    }
    display_text(SCR_W/2-60,SCR_H-30,"Press A to continue",C_DIM);
    display_flush();
}

static void draw_end(int win) {
    display_clear(C_BG);
    display_rect_outline(8,8,SCR_W-16,SCR_H-16, win?C_WIN:C_LOSE);
    display_text_large(SCR_W/2-48,SCR_H*20/100, win?"VICTORY":"DEFEAT", win?C_WIN:C_LOSE);
    display_text(SCR_W/2-52,SCR_H*44/100,"Floor reached:",C_DIM);
    display_number(SCR_W/2+40,SCR_H*44/100,floor_reached,C_TEXT);
    display_text(SCR_W/2-52,SCR_H*52/100,"Best floor:",C_DIM);
    display_number(SCR_W/2+40,SCR_H*52/100,best_floor,C_GOLD);
    display_text(SCR_W/2-70,SCR_H*70/100,"A: new run   B: quit",C_DIM);
    display_flush();
}

/* ── Node entry ──────────────────────────────────────────────────────── */
static void enter_node(int row, int col) {
    cur_row=row; cur_col=col;
    floor_reached=row+1;
    int t=node_type[row][col];
    /* Prime the next-row selection. */
    sel_col=0; while(sel_col<MAP_COLS && !node_reachable(row+1,sel_col)) sel_col++;
    if (sel_col>=MAP_COLS) sel_col=col;

    switch (t) {
    case N_BATTLE:   start_battle(row+1,0,0); break;
    case N_ELITE:    start_battle(row+1,1,0); break;
    case N_BOSS:     start_battle(row+1,0,1); break;
    case N_REST:     p_hp=clampi(p_hp + p_max_hp*30/100,0,p_max_hp); state=ST_REST; break;
    case N_TREASURE: grant_random_relic(); state=ST_TREASURE; break;
    default:         state=ST_MAP; break;
    }
}

/* After a battle is won. */
static void win_battle(void) {
    gold += 10 + rng_next(10);
    if (is_boss) { floor_reached=MAP_ROWS; save_best(); state=ST_WIN; return; }
    if (is_elite) grant_random_relic();
    roll_card_reward();
    state=ST_REWARD;
}

/* ── Input handlers ──────────────────────────────────────────────────── */
static int move_sel_col(int dir) {
    int c=sel_col;
    for (int k=0;k<MAP_COLS;k++){
        c+=dir; if(c<0)c=MAP_COLS-1; if(c>=MAP_COLS)c=0;
        if (node_reachable(cur_row+1,c)) return c;
    }
    return sel_col;
}

static int handle_map(int edge) {
    if (edge & AKIRA_BTN_LEFT)  sel_col=move_sel_col(-1);
    if (edge & AKIRA_BTN_RIGHT) sel_col=move_sel_col(+1);
    if (edge & AKIRA_BTN_A) {
        if (node_reachable(cur_row+1,sel_col)) enter_node(cur_row+1,sel_col);
    }
    if (edge & AKIRA_BTN_B) return 1;   /* quit */
    return 0;
}

static void handle_battle(int edge) {
    if (edge & AKIRA_BTN_LEFT)  hand_cur=(hand_cur>0)?hand_cur-1:(hand_n>0?hand_n-1:0);
    if (edge & AKIRA_BTN_RIGHT) hand_cur=(hand_n>0)?(hand_cur+1)%hand_n:0;
    if (edge & AKIRA_BTN_X) {
        for (int k=0;k<mob_n;k++){ target=(target+1)%mob_n; if(mobs[target].alive) break; }
    }
    if (edge & AKIRA_BTN_A) {
        play_card(hand_cur);
        if (mobs_alive()==0) { win_battle(); return; }
    }
    if (edge & AKIRA_BTN_B) {
        end_turn();
        if (state==ST_GAMEOVER) return;
        if (mobs_alive()==0) win_battle();
    }
}

/* ── Entry point ─────────────────────────────────────────────────────── */
int main(void) {
    printf("AkiraOS Card Crawl");
    display_get_size(&SCR_W, &SCR_H);
    load_best();

    uint32_t frames=1;
    /* Title */
    display_clear(C_BG);
    display_text_large(SCR_W*24/100, SCR_H*28/100, "CARD", C_ATK);
    display_text_large(SCR_W*22/100, SCR_H*42/100, "CRAWL", C_SKL);
    display_text(SCR_W*20/100, SCR_H*62/100, "A deckbuilding descent", C_DIM);
    if (best_floor>0){ display_text(SCR_W*28/100,SCR_H*72/100,"Best floor:",C_GOLD);
        display_number(SCR_W*66/100,SCR_H*72/100,best_floor,C_GOLD); }
    display_text(SCR_W*30/100, SCR_H*84/100, "Press A", C_TEXT);
    display_flush();

    int prev=(int)input_get_buttons();
    while (!(((int)input_get_buttons()) & (AKIRA_BTN_A|AKIRA_BTN_B))) { frames++; delay(20000); }
    reseed(frames);

    init_run();
    gen_map();
    state=ST_MAP;
    prev=(int)input_get_buttons();

    while (1) {
        int held=(int)input_get_buttons();
        int edge=held&~prev; prev=held;

        switch (state) {
        case ST_MAP:
            if (edge && handle_map(edge)) { app_switch("supervisor"); return 0; }
            draw_map();
            break;

        case ST_BATTLE:
            if (edge) handle_battle(edge);
            if (state==ST_BATTLE) draw_battle();
            break;

        case ST_REWARD:
            if (edge & AKIRA_BTN_LEFT)  reward_cur=(reward_cur>0)?reward_cur-1:2;
            if (edge & AKIRA_BTN_RIGHT) reward_cur=(reward_cur<2)?reward_cur+1:0;
            if (edge & AKIRA_BTN_A) { deck_add(reward_cards[reward_cur]); state=ST_MAP; }
            if (edge & AKIRA_BTN_B) state=ST_MAP;
            if (state==ST_REWARD) draw_reward();
            break;

        case ST_REST:
            if (edge & (AKIRA_BTN_A|AKIRA_BTN_B)) state=ST_MAP;
            if (state==ST_REST) draw_rest();
            break;

        case ST_TREASURE:
            if (edge & (AKIRA_BTN_A|AKIRA_BTN_B)) state=ST_MAP;
            if (state==ST_TREASURE) draw_treasure();
            break;

        case ST_GAMEOVER:
        case ST_WIN:
            if (edge & AKIRA_BTN_A) { init_run(); gen_map(); state=ST_MAP; }
            else if (edge & AKIRA_BTN_B) { app_switch("supervisor"); return 0; }
            else draw_end(state==ST_WIN);
            break;
        }

        delay(30000);
    }
    return 0;
}
