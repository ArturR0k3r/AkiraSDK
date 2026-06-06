/*
 * vault_ui.c — AkiraVault Cold Wallet UI
 * SPDX-License-Identifier: Apache-2.0
 */
#include "vault.h"

#define TX(col)  ((col) * GLYPH_W)
#define TY(row)  ((row) * GLYPH_H)

static void tput(int col, int row, const char *s, uint16_t color) {
    display_text(TX(col), TY(row), s, color);
}
static void trow_bg(int row, uint16_t color) {
    display_rect(0, TY(row), GW, GLYPH_H, color);
}

/* ── Status bar ─────────────────────────────────────────────────────────── */
static void draw_status(const char *name, int locked) {
    trow_bg(0, C_HEADER);
    char buf[18]; buf[0]='[';
    int i=1; const char *p=name;
    while(*p&&i<14) buf[i++]=*p++;
    buf[i++]=']'; buf[i]='\0';
    tput(0, 0, buf, C_ACCENT);
    if(locked) tput(COLS-8, 0, "[LOCKED]", C_DANGER);
    display_hline(0, GLYPH_H-1, GW, C_ACCENT);
}

/* ── Action bar ─────────────────────────────────────────────────────────── */
static void draw_action(const char *L, const char *R) {
    int br = ROWS-1;
    trow_bg(br, C_HEADER);
    display_hline(0, TY(br), GW, C_ACCENT);
    char lb[14]="["; sv_ncpy(lb+1,L,10); lb[sv_len(lb)]=']'; lb[sv_len(lb)]='\0';
    char rb[14]="["; sv_ncpy(rb+1,R,10); rb[sv_len(rb)]=']'; rb[sv_len(rb)]='\0';
    tput(0, br, lb, C_FG);
    tput(COLS-sv_len(rb), br, rb, C_ACCENT);
}
static void clear_content(void) {
    display_rect(0, GLYPH_H, GW, GH-GLYPH_H*2, C_BG);
}

/* ── Number to string ───────────────────────────────────────────────────── */
static char _nb[16];
static const char *n2s(uint32_t v) {
    if(!v){_nb[0]='0';_nb[1]='\0';return _nb;}
    int i=0; uint32_t t=v;
    while(t){_nb[i++]='0'+t%10;t/=10;}
    for(int a=0,b=i-1;a<b;a++,b--){char x=_nb[a];_nb[a]=_nb[b];_nb[b]=x;}
    _nb[i]='\0'; return _nb;
}

/* ── Progress bar ───────────────────────────────────────────────────────── */
static void draw_bar(int col, int row, int val, int max, int width,
                     uint16_t fg, uint16_t bg) {
    int filled = (max>0) ? val*width/max : 0;
    char bar[64]; int i=0;
    for(;i<filled&&i<width;i++) bar[i]='\xDB';
    for(;i<width;i++) bar[i]='\xB0';
    bar[i]='\0';
    display_rect(TX(col), TY(row), width*GLYPH_W, GLYPH_H, bg);
    tput(col, row, bar, fg);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Screen-local state
 * ═══════════════════════════════════════════════════════════════════════════ */
static char  pin_digits[PIN_LEN+1];
static int   pin_cur, pin_digit_val, pin_wrong;
static int   list_sel, list_scroll;
static char  seed_words[24][12];
static int   sign_hold_frames;

/* ── BOOT ────────────────────────────────────────────────────────────────── */
static void draw_boot(void) {
    display_clear(C_BG);
    int cy = ROWS/2-3;
    tput(COLS/2-8, cy,   "  ┌──────────────┐", C_ACCENT);
    tput(COLS/2-8, cy+1, "  │  AKIRAVAULT  │", C_ACCENT);
    tput(COLS/2-8, cy+2, "  │  Cold Wallet │", C_FG);
    tput(COLS/2-8, cy+3, "  │   v3.0  🔐  │", C_DIM);
    tput(COLS/2-8, cy+4, "  └──────────────┘", C_ACCENT);
    tput(COLS/2-10, cy+6, "Your keys. Your coins.", C_DIM);
    display_flush();
}

/* ── UNLOCK ──────────────────────────────────────────────────────────────── */
static void draw_unlock(void) {
    display_clear(C_BG);
    draw_status("AKIRAVAULT", 1);
    clear_content();
    int mr = ROWS/2-2;
    tput(COLS/2-6, mr, "   ENTER PIN", C_FG);
    char dotrow[32]; int di=0;
    dotrow[di++]=' '; dotrow[di++]=' '; dotrow[di++]=' '; dotrow[di++]=' ';
    for(int i=0;i<PIN_LEN;i++){
        dotrow[di++]='[';
        dotrow[di++]=(i<pin_cur)?'\xFB':((i==pin_cur)?'0'+pin_digit_val:' ');
        dotrow[di++]=']';
    }
    dotrow[di]='\0';
    tput(1, mr+2, dotrow, C_ACCENT);
    if(g_hdr.wrong_attempts>0){
        tput(2, mr+4, "attempts left: ", C_WARN);
        tput(17, mr+4, n2s(WIPE_AFTER - g_hdr.wrong_attempts), C_WARN);
    }
    if(pin_wrong){ trow_bg(mr+4, C_DANGER); tput(COLS/2-5, mr+4, " WRONG PIN ", C_FG); }
    draw_action("CLR", "ENTER");
    display_flush();
}

/* ── HOME ────────────────────────────────────────────────────────────────── */
static void draw_home(void) {
    display_clear(C_BG);
    draw_status("HOME", 0);
    clear_content();
    static const char *items[] = {
        "  ▶  WALLET    — HD accounts & keys",
        "  ▶  SETTINGS  — PIN, lock, reset",
        "  ▶  ABOUT",
    };
    for(int i=0;i<3;i++){
        int row=3+i*2;
        if(i==list_sel){ trow_bg(row,C_ACCENT); tput(0,row,items[i],C_BG); }
        else            { tput(0,row,items[i],C_FG); }
    }
    draw_action("LOCK","ENTER");
    display_flush();
}

/* ── ACCOUNTS ────────────────────────────────────────────────────────────── */
static void draw_accounts(void) {
    display_clear(C_BG);
    draw_status("WALLET", 0);
    clear_content();
    tput(0,1,"  HD Accounts (BIP44)",C_ACCENT);
    int active=0;
    for(int i=0;i<MAX_ACCOUNTS;i++) if(g_vault.accounts[i].active) active++;
    if(active==0){
        tput(2,3,"  (no accounts — add via companion app)",C_DIM);
    } else {
        int row=2, shown=0;
        for(int i=0;i<MAX_ACCOUNTS&&row<ROWS-2;i++){
            if(!g_vault.accounts[i].active) continue;
            const char *cn = g_vault.accounts[i].coin_type==60?"ETH":
                              g_vault.accounts[i].coin_type==0 ?"BTC":"SOL";
            char buf[44]; buf[0]=' '; buf[1]=' ';
            sv_cpy(buf+2,g_vault.accounts[i].name,20);
            int bl=sv_len(buf); buf[bl]=' '; sv_cpy(buf+bl+1,cn,4);
            if(shown==list_sel){ trow_bg(row,C_ACCENT); tput(0,row,buf,C_BG); }
            else               { tput(0,row,buf,C_FG); }
            row++; shown++;
        }
    }
    tput(0,ROWS-2,"  m/44'/coin'/acct'  — BIP44",C_DIM);
    draw_action("BACK","DETAIL");
    display_flush();
}

/* ── ACCOUNT DETAIL ──────────────────────────────────────────────────────── */
static void draw_account_detail(void) {
    display_clear(C_BG);
    draw_status("WALLET/ACCT", 0);
    clear_content();
    hd_account_t *acc=&g_vault.accounts[list_sel];
    char title[40]="  "; sv_cpy(title+2,acc->name,30);
    tput(0,1,title,C_ACCENT);
    const char *coin = acc->coin_type==60?"ETH":acc->coin_type==0?"BTC":"SOL";
    tput(0,3,"  Coin:  ",C_DIM); tput(9,3,coin,C_FG);
    tput(0,4,"  Index: ",C_DIM); tput(9,4,n2s(acc->index),C_FG);
    tput(0,6,"  Address (xpub-derived):",C_DIM);
    tput(0,7,"  0x742d....3a8F",C_WARN);
    tput(0,9,"  [ENTER] Sign message",C_ACCENT);
    tput(0,10,"  [UP]    Show xpub",C_DIM);
    draw_action("BACK","SIGN");
    display_flush();
}

/* ── SHOW XPUB ───────────────────────────────────────────────────────────── */
static void draw_show_xpub(void) {
    display_clear(C_BG);
    draw_status("WALLET/XPUB", 0);
    clear_content();
    hd_account_t *acc=&g_vault.accounts[list_sel];
    tput(0,1,"  Extended Public Key",C_ACCENT);
    tput(0,2,"  (xpub — watch-only, safe to share)",C_DIM);
    tput(0,4,"  xpub6CUG...h7Qw",C_FG);
    tput(0,5,"  z3tM...8xKL",C_FG);
    tput(0,7,"  Account: ",C_DIM); tput(10,7,acc->name,C_FG);
    tput(0,8,"  Path: m/44'/",C_DIM);
    tput(13,8,n2s(acc->coin_type),C_DIM);
    tput(16,8,"'/",C_DIM); tput(18,8,n2s(acc->index),C_DIM); tput(19,8,"'",C_DIM);
    draw_action("BACK","");
    display_flush();
}

/* ── SIGN MESSAGE ────────────────────────────────────────────────────────── */
static void draw_sign_message(void) {
    display_clear(C_BG);
    trow_bg(0, C_DANGER);
    draw_status("SIGN REQUEST", 0);
    clear_content();
    trow_bg(1, C_DANGER);
    tput(0,1,"  !! SIGNATURE REQUEST !!",C_FG);
    hd_account_t *acc=&g_vault.accounts[list_sel];
    tput(0,3,"  Account:",C_DIM); tput(10,3,acc->name,C_FG);
    tput(0,4,"  Addr: 0x742d....3a8F",C_DIM);
    tput(0,6,"  Hold [ENTER] 1.5s to approve",C_WARN);
    if(sign_hold_frames>0){
        int pct=sign_hold_frames*100/75;
        draw_bar(2,8,pct,100,COLS-4,C_DANGER,C_BG);
    }
    draw_action("REJECT","SIGN");
    display_flush();
}

/* ── SETUP WELCOME ───────────────────────────────────────────────────────── */
static void draw_setup_welcome(void) {
    display_clear(C_BG);
    draw_status("SETUP", 1);
    clear_content();
    tput(COLS/2-8,2,"  WELCOME TO AKIRAVAULT",C_ACCENT);
    tput(1,4,"  No vault found — first run.",C_FG);
    tput(1,6,"  ▶  Generate new seed phrase",list_sel==0?C_ACCENT:C_FG);
    tput(1,7,"  ▶  Restore from mnemonic",   list_sel==1?C_ACCENT:C_FG);
    tput(1,9,"  !! Your keys, your coins !!",C_WARN);
    draw_action("","ENTER");
    display_flush();
}

/* ── SETUP GENERATE ──────────────────────────────────────────────────────── */
static void draw_setup_generate(void) {
    display_clear(C_BG);
    draw_status("SETUP/SEED", 1);
    clear_content();
    tput(0,1,"  24-Word Seed Phrase",C_ACCENT);
    tput(0,2,"  !! WRITE DOWN ALL WORDS !!",C_DANGER);
    int base=list_scroll*6;
    for(int i=0;i<6&&base+i<24;i++){
        int row=3+i; int wi=base+i;
        char buf[20]; buf[0]='0'+(wi+1)/10; buf[1]='0'+(wi+1)%10;
        buf[2]='.'; sv_cpy(buf+3,seed_words[wi],11); buf[14]='\0';
        tput(0,row,buf,C_FG);
        int wi2=wi+12; if(wi2<24){
            char buf2[20]; buf2[0]='0'+(wi2+1)/10; buf2[1]='0'+(wi2+1)%10;
            buf2[2]='.'; sv_cpy(buf2+3,seed_words[wi2],11); buf2[14]='\0';
            tput(COLS/2,row,buf2,C_FG);
        }
    }
    tput(0,ROWS-3,"  Page ",C_DIM); tput(7,ROWS-3,n2s(list_scroll+1),C_DIM);
    tput(9,ROWS-3," / 4",C_DIM);
    if(list_scroll==3) tput(0,ROWS-2,"  Memorised? Press ENTER.",C_WARN);
    else               tput(0,ROWS-2,"  Shown once. Write it down.",C_WARN);
    draw_action("PREV","NEXT");
    display_flush();
}

/* ── SETTINGS ────────────────────────────────────────────────────────────── */
static void draw_settings(void) {
    display_clear(C_BG);
    draw_status("SETTINGS", 0);
    clear_content();
    static const char *sitems[] = {
        "  ▶  Change PIN",
        "  ▶  Auto-lock timeout",
        "  ▶  Factory reset",
    };
    const char *lock_lbl = g_vault.autolock_s==0?"Never":
                            g_vault.autolock_s==30?"30s":
                            g_vault.autolock_s==60?"1min":"5min";
    for(int i=0;i<3;i++){
        int row=2+i*2;
        if(i==list_sel){ trow_bg(row,C_ACCENT); tput(0,row,sitems[i],C_BG); }
        else            { tput(0,row,sitems[i],C_FG); }
        if(i==1) tput(COLS-6,row,lock_lbl,C_DIM);
    }
    draw_action("BACK","ENTER");
    display_flush();
}

/* ── FACTORY RESET ───────────────────────────────────────────────────────── */
static void draw_factory_reset(void) {
    display_clear(C_BG);
    trow_bg(0,C_DANGER);
    draw_status("DANGER!", 0);
    clear_content();
    trow_bg(1,C_DANGER);
    tput(0,1,"  !! FACTORY RESET !!",C_FG);
    tput(0,3,"  ALL DATA WILL BE DELETED.",C_WARN);
    tput(0,4,"  Seeds, accounts — GONE.",C_WARN);
    tput(0,6,"  Hold [ENTER] 3s to confirm.",C_FG);
    if(sign_hold_frames>0){
        int pct=sign_hold_frames*100/150;
        draw_bar(2,8,pct,100,COLS-4,C_DANGER,C_BG);
    }
    draw_action("CANCEL","");
    display_flush();
}

/* ── ABOUT ───────────────────────────────────────────────────────────────── */
static void draw_about(void) {
    display_clear(C_BG);
    draw_status("ABOUT", 0);
    clear_content();
    tput(0,2,"  AkiraVault v3.0",C_ACCENT);
    tput(0,3,"  Hardware Cold Wallet",C_FG);
    tput(0,5,"  Crypto stack:",C_DIM);
    tput(0,6,"    SHA-256, HMAC-SHA256, PBKDF2",C_DIM);
    tput(0,7,"    ChaCha20 stream cipher",C_DIM);
    tput(0,8,"    BIP39 + BIP44 HD wallet",C_DIM);
    tput(0,10,"  PIN rounds: 10,000",C_DIM);
    tput(0,11,"  Wipe after: 3 wrong PINs",C_WARN);
    tput(0,12,"  For 2FA/SSH use AkiraKey app",C_DIM);
    draw_action("BACK","");
    display_flush();
}

/* ═══════════════════════════════════════════════════════════════════════════
 * ui_init / ui_draw / ui_handle_key
 * ═══════════════════════════════════════════════════════════════════════════ */
void ui_init(void) {
    list_sel=0; list_scroll=0;
    pin_cur=0; pin_digit_val=0; pin_wrong=0;
    sign_hold_frames=0;
}

void ui_draw(void) {
    switch(g_screen){
    case SCR_BOOT:           draw_boot();           break;
    case SCR_SETUP_WELCOME:  draw_setup_welcome();  break;
    case SCR_SETUP_GENERATE: draw_setup_generate(); break;
    case SCR_UNLOCK:         draw_unlock();         break;
    case SCR_HOME:           draw_home();           break;
    case SCR_ACCOUNTS:       draw_accounts();       break;
    case SCR_ACCOUNT_DETAIL: draw_account_detail(); break;
    case SCR_SIGN_MESSAGE:   draw_sign_message();   break;
    case SCR_SHOW_XPUB:      draw_show_xpub();      break;
    case SCR_SETTINGS:       draw_settings();       break;
    case SCR_FACTORY_RESET:  draw_factory_reset();  break;
    case SCR_ABOUT:          draw_about();          break;
    default: break;
    }
}

static void go(screen_t s){ g_prev_screen=g_screen; g_screen=s; ui_init(); }
static void back(void){ g_screen=g_prev_screen; ui_init(); }

void ui_handle_key(int key, int long_press) {
    if(long_press && key==KEY_CENTER && g_screen!=SCR_UNLOCK && g_screen!=SCR_BOOT){
        vault_lock(); return;
    }

    switch(g_screen){

    case SCR_UNLOCK:
        pin_wrong=0;
        if(key==KEY_UP)   pin_digit_val=(pin_digit_val+1)%10;
        if(key==KEY_DOWN) pin_digit_val=(pin_digit_val+9)%10;
        if(key==KEY_RIGHT && pin_cur<PIN_LEN){
            pin_digits[pin_cur]='0'+pin_digit_val;
            if(++pin_cur==PIN_LEN){
                pin_digits[PIN_LEN]='\0';
                if(vault_load(pin_digits)==0){ go(SCR_HOME); return; }
                pin_cur=0; pin_digit_val=0; pin_wrong=1;
                if(g_hdr.wrong_attempts>=WIPE_AFTER){
                    storage_delete("vault.bin");
                    g_screen=SCR_SETUP_WELCOME;
                }
            }
        }
        if(key==KEY_LEFT && pin_cur>0){ pin_cur--; pin_digit_val=0; }
        break;

    case SCR_SETUP_WELCOME:
        if(key==KEY_UP||key==KEY_DOWN) list_sel^=1;
        if(key==KEY_RIGHT){
            if(list_sel==0){
                bip39_entropy_to_mnemonic(g_vault.entropy, ENTROPY_BYTES_24, seed_words);
                go(SCR_SETUP_GENERATE);
            } else {
                go(SCR_SETUP_RESTORE);
            }
        }
        break;

    case SCR_SETUP_GENERATE:
        if(key==KEY_RIGHT){ if(++list_scroll>=4){ go(SCR_UNLOCK); } }
        if(key==KEY_LEFT && list_scroll>0) list_scroll--;
        break;

    case SCR_HOME:
        if(key==KEY_UP)   list_sel=(list_sel+2)%3;
        if(key==KEY_DOWN) list_sel=(list_sel+1)%3;
        if(key==KEY_RIGHT){
            if(list_sel==0) go(SCR_ACCOUNTS);
            if(list_sel==1) go(SCR_SETTINGS);
            if(list_sel==2) go(SCR_ABOUT);
        }
        if(key==KEY_LEFT) vault_lock();
        break;

    case SCR_ACCOUNTS:
        if(key==KEY_UP && list_sel>0) list_sel--;
        if(key==KEY_DOWN) list_sel++;
        if(key==KEY_RIGHT) go(SCR_ACCOUNT_DETAIL);
        if(key==KEY_LEFT)  back();
        break;

    case SCR_ACCOUNT_DETAIL:
        if(key==KEY_LEFT)  back();
        if(key==KEY_UP)    go(SCR_SHOW_XPUB);
        if(key==KEY_RIGHT){ sign_hold_frames=0; go(SCR_SIGN_MESSAGE); }
        break;

    case SCR_SHOW_XPUB:
        if(key==KEY_LEFT) back();
        break;

    case SCR_SIGN_MESSAGE:
        if(key==KEY_LEFT){ sign_hold_frames=0; back(); }
        if(key==KEY_CENTER){
            if(++sign_hold_frames>=75){ sign_hold_frames=0; back(); }
        } else sign_hold_frames=0;
        break;

    case SCR_SETTINGS:
        if(key==KEY_UP)   list_sel=(list_sel+2)%3;
        if(key==KEY_DOWN) list_sel=(list_sel+1)%3;
        if(key==KEY_LEFT) back();
        if(key==KEY_RIGHT){
            if(list_sel==0) go(SCR_PIN_CHANGE);
            if(list_sel==1){ g_vault.autolock_s=
                             g_vault.autolock_s==0?30:g_vault.autolock_s==30?60:
                             g_vault.autolock_s==60?300:0; g_vault_dirty=1; }
            if(list_sel==2){ sign_hold_frames=0; go(SCR_FACTORY_RESET); }
        }
        break;

    case SCR_FACTORY_RESET:
        if(key==KEY_LEFT){ sign_hold_frames=0; back(); }
        if(key==KEY_CENTER){
            if(++sign_hold_frames>=150){
                storage_delete("vault.bin");
                sign_hold_frames=0; g_screen=SCR_SETUP_WELCOME; ui_init();
            }
        } else sign_hold_frames=0;
        break;

    case SCR_ABOUT: case SCR_PIN_CHANGE: case SCR_SETUP_RESTORE:
        if(key==KEY_LEFT) back();
        break;

    default: break;
    }
}
