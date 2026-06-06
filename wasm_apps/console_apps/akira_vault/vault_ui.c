/*
 * vault_ui.c — Terminal-brutalist UI: box-drawing, all screens
 * SPDX-License-Identifier: Apache-2.0
 */
#include "vault.h"

/* ═══════════════════════════════════════════════════════════════════════════
 * Primitive text renderer — 8×13 glyph grid
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Row/col helpers */
#define TX(col)  ((col) * GLYPH_W)
#define TY(row)  ((row) * GLYPH_H)

static void tput(int col, int row, const char *s, uint16_t color) {
    display_text(TX(col), TY(row), s, color);
}
static void tputl(int col, int row, const char *s, uint16_t color) {
    display_text_large(TX(col), TY(row), s, color);
}

/* Fill a text row with background */
static void trow_bg(int row, uint16_t color) {
    display_rect(0, TY(row), GW, GLYPH_H, color);
}

/* ── Box drawing ────────────────────────────────────────────────────────── */
static void hline(int col, int row, int len, uint16_t c) {
    display_hline(TX(col), TY(row) + GLYPH_H/2, len * GLYPH_W, c);
}

/* ── Status bar (always row 0) ──────────────────────────────────────────── */
static void draw_status(const char *app_name, int locked, int ble, int bat) {
    trow_bg(0, C_HEADER);
    /* Left: app name in accent */
    char buf[40]; buf[0]='[';
    int i=1; const char *p=app_name;
    while(*p&&i<14) buf[i++]=*p++;
    buf[i++]=']'; buf[i]='\0';
    tput(0, 0, buf, C_ACCENT);
    /* Right: status icons */
    tput(COLS-18, 0, locked ? "[LOCK]" : "      ", locked?C_DANGER:C_DIM);
    tput(COLS-12, 0, ble    ? "[BLE]"  : "     ", ble   ?C_ACCENT:C_DIM);
    tput(COLS-7,  0, "[",   C_DIM);
    char batbuf[6]; batbuf[0]='0'+bat/100%10;batbuf[1]='0'+bat/10%10;
    batbuf[2]='0'+bat%10; batbuf[3]='%'; batbuf[4]=']'; batbuf[5]='\0';
    tput(COLS-6,  0, batbuf, bat>20?C_FG:C_DANGER);
    /* Separator */
    display_hline(0, GLYPH_H-1, GW, C_ACCENT);
}

/* ── Action bar (always row ROWS-1) ──────────────────────────────────────── */
static void draw_action(const char *left_lbl, const char *right_lbl) {
    int br = ROWS - 1;
    trow_bg(br, C_HEADER);
    display_hline(0, TY(br), GW, C_ACCENT);
    char lb[12]="["; sv_ncpy(lb+1,left_lbl,9); lb[sv_len(lb)]=']'; lb[sv_len(lb)]='\0';
    char rb[12]="["; sv_ncpy(rb+1,right_lbl,9); rb[sv_len(rb)]=']'; rb[sv_len(rb)]='\0';
    tput(0, br, lb, C_FG);
    tput(COLS-sv_len(rb), br, rb, C_ACCENT);
    /* Dashes in middle */
    int ll=sv_len(lb), rl=sv_len(rb), ml=COLS-ll-rl-2;
    char dash[80]; int di=0; dash[di++]=' ';
    for(int i=0;i<ml&&di<78;i++) dash[di++]='-';
    dash[di++]=' '; dash[di]='\0';
    tput(ll, br, dash, C_DIM);
}

/* ── Content area clear ─────────────────────────────────────────────────── */
static void clear_content(void) {
    display_rect(0, GLYPH_H, GW, GH - GLYPH_H*2, C_BG);
}

/* ── Number to string ───────────────────────────────────────────────────── */
static char _nb[16];
static const char *n2str(uint32_t v) {
    if(!v){_nb[0]='0';_nb[1]='\0';return _nb;}
    int i=0; uint32_t t=v;
    while(t){_nb[i++]='0'+t%10;t/=10;}
    for(int a=0,b=i-1;a<b;a++,b--){char x=_nb[a];_nb[a]=_nb[b];_nb[b]=x;}
    _nb[i]='\0'; return _nb;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Screen-local UI state
 * ═══════════════════════════════════════════════════════════════════════════ */

/* PIN entry */
static char  pin_digits[PIN_LEN+1];
static int   pin_cur;          /* current digit position */
static int   pin_digit_val;    /* digit being cycled (0-9) */
static int   pin_wrong;

/* List cursor */
static int   list_sel;
static int   list_scroll;

/* TOTP live state */
static uint32_t totp_code;
static int      totp_remain;

/* Setup phase */
static int   setup_word_idx;
static char  seed_words[24][12];
static char  verify_words[24][12];
static int   verify_idx;

/* Sign request (populated by external caller before switching screen) */
static char  sign_account[32];
static char  sign_hash[12];
static int   sign_len;
static int   sign_hold_frames; /* count frames CENTER held */

/* FIDO2 approve */
static char  fido_rp[64];
static char  fido_user[48];
static int   fido_timeout;     /* frames remaining */
static int   fido_hold_frames;

/* Progress bar helper */
static void draw_bar(int col, int row, int val, int max, int width,
                     uint16_t fg, uint16_t bg) {
    int filled = (max>0) ? val*width/max : 0;
    char bar[64]; int i=0;
    for(;i<filled&&i<width;i++) bar[i]='\xDB'; /* █ */
    for(;i<width;i++) bar[i]='\xB0';           /* ░ */
    bar[i]='\0';
    display_rect(TX(col), TY(row), width*GLYPH_W, GLYPH_H, bg);
    tput(col, row, bar, fg);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * SCREEN DRAW FUNCTIONS
 * ═══════════════════════════════════════════════════════════════════════════ */

static void draw_boot(void) {
    display_clear(C_BG);
    int cy2 = ROWS/2-2;
    tput(COLS/2-6, cy2,   "┌──────────────┐", C_ACCENT);
    tput(COLS/2-6, cy2+1, "│  AKIRAVAULT  │", C_ACCENT);
    tput(COLS/2-6, cy2+2, "│  v2.0  ████  │", C_DIM);
    tput(COLS/2-6, cy2+3, "└──────────────┘", C_ACCENT);
    tput(COLS/2-8, cy2+5, "Hardware Security Module", C_DIM);
    display_flush();
}

/* ── UNLOCK ──────────────────────────────────────────────────────────────── */
static void draw_unlock(void) {
    display_clear(C_BG);
    draw_status("AKIRAVAULT", 1, g_vault.ble_enabled, 100);
    clear_content();

    int mr = ROWS/2-2;
    tput(COLS/2-6, mr, "   ENTER PIN", C_FG);
    /* PIN dots */
    char dotrow[32]; int di=0;
    dotrow[di++]=' '; dotrow[di++]=' '; dotrow[di++]=' '; dotrow[di++]=' ';
    for(int i=0;i<PIN_LEN;i++){
        dotrow[di++]='[';
        dotrow[di++]=(i<pin_cur)?'\xFB':((i==pin_cur)?'0'+pin_digit_val:' ');
        dotrow[di++]=']';
    }
    dotrow[di]='\0';
    tput(1, mr+2, dotrow, C_ACCENT);

    /* Attempts remaining */
    if(g_hdr.wrong_attempts>0){
        tput(2, mr+4, "attempts left: ", C_WARN);
        tput(17, mr+4, n2str(WIPE_AFTER - g_hdr.wrong_attempts), C_WARN);
    }
    if(pin_wrong){
        trow_bg(mr+4, C_DANGER);
        tput(COLS/2-5, mr+4, " WRONG PIN ", C_FG);
    }
    draw_action("CLR", "ENTER");
    display_flush();
}

/* ── HOME ────────────────────────────────────────────────────────────────── */
static void draw_home(void) {
    display_clear(C_BG);
    draw_status("HOME", 0, g_vault.ble_enabled, 100);
    clear_content();
    static const char *items[] = {
        "  ▶  WALLET    — HD accounts & signing",
        "  ▶  KEYS      — TOTP / FIDO2 / SSH",
        "  ▶  SETTINGS  — PIN, lock, BLE, RF",
        "  ▶  ABOUT"
    };
    for(int i=0;i<4;i++){
        int row=2+i*2;
        if(i==list_sel){ trow_bg(row, C_ACCENT); tput(0,row,items[i],C_BG); }
        else            { tput(0,row,items[i],C_FG); }
        tput(0,row+1,"",C_DIM);
    }
    draw_action("LOCK", "ENTER");
    display_flush();
}

/* ── ACCOUNTS LIST ───────────────────────────────────────────────────────── */
static void draw_accounts(void) {
    display_clear(C_BG);
    draw_status("WALLET", 0, g_vault.ble_enabled, 100);
    clear_content();
    tput(0,1,"  HD Accounts",C_ACCENT);
    int active=0;
    for(int i=0;i<MAX_ACCOUNTS;i++) if(g_vault.accounts[i].active) active++;
    if(active==0){
        tput(2,3,"  (no accounts — create one)",C_DIM);
    } else {
        int row=2;
        for(int i=0;i<MAX_ACCOUNTS&&row<ROWS-2;i++){
            if(!g_vault.accounts[i].active) continue;
            char buf[40]; buf[0]=' '; buf[1]=' ';
            const char *cn=g_vault.accounts[i].coin_type==60?"ETH":
                            g_vault.accounts[i].coin_type==0 ?"BTC":"SOL";
            sv_cpy(buf+2,g_vault.accounts[i].name,20); int bl=sv_len(buf);
            buf[bl]=' '; sv_cpy(buf+bl+1,cn,4);
            if(i==list_sel){ trow_bg(row,C_ACCENT); tput(0,row,buf,C_BG); }
            else            { tput(0,row,buf,C_FG); }
            row++;
        }
    }
    tput(0,ROWS-2,"  [+NEW]",C_ACCENT);
    draw_action("BACK","ENTER");
    display_flush();
}

/* ── ACCOUNT DETAIL ──────────────────────────────────────────────────────── */
static void draw_account_detail(void) {
    display_clear(C_BG);
    draw_status("WALLET",0,g_vault.ble_enabled,100);
    clear_content();
    hd_account_t *acc=&g_vault.accounts[list_sel];
    char title[40]="  "; sv_cpy(title+2,acc->name,30);
    tput(0,1,title,C_ACCENT);
    /* Address mock — real derivation needs secp256k1 */
    const char *coin_lbl = acc->coin_type==60?"ETH Addr:":
                            acc->coin_type==0 ?"BTC Addr:":"SOL Addr:";
    tput(0,3,coin_lbl,C_DIM);
    tput(0,4,"  0x742d....3a8F  (watch-only)",C_WARN);
    tput(0,5,"  [derive key not available — xpub only]",C_DIM);
    tput(0,7,"  Index: ",C_DIM); tput(9,7,n2str(acc->index),C_FG);
    tput(0,9,"  [CENTER] Sign message",C_ACCENT);
    tput(0,10,"  [Y]      Show xpub QR",C_DIM);
    draw_action("BACK","SIGN");
    display_flush();
}

/* ── SIGN MESSAGE ────────────────────────────────────────────────────────── */
static void draw_sign_message(void) {
    display_clear(C_BG);
    trow_bg(0,C_DANGER);
    draw_status("WALLET/SIGN",0,0,100);
    clear_content();
    trow_bg(1,C_DANGER);
    tput(0,1,"  !! SIGNATURE REQUEST !!",C_FG);
    tput(0,3,"  Account: ",C_DIM); tput(11,3,sign_account,C_FG);
    tput(0,4,"  0x742d...3a8F",C_DIM);
    tput(0,6,"  Payload:",C_DIM);  tput(10,6,n2str(sign_len),C_FG);
    tput(14,6," bytes",C_DIM);
    tput(0,7,"  Hash:   ",C_DIM);  tput(10,7,sign_hash,C_FG);
    tput(0,9,"  Hold [SELECT] 1.5s to sign",C_WARN);
    /* Hold progress */
    if(sign_hold_frames>0){
        int pct=sign_hold_frames*100/75;
        draw_bar(2,10,pct,100,COLS-4, C_DANGER, C_BG);
    }
    draw_action("REJECT","SIGN ✓");
    display_flush();
}

/* ── TOTP LIST ───────────────────────────────────────────────────────────── */
static void draw_totp_list(void) {
    display_clear(C_BG);
    draw_status("KEYS/TOTP",0,g_vault.ble_enabled,100);
    clear_content();
    tput(0,1,"  OTP Slots",C_ACCENT);
    int row=2, shown=0;
    for(int i=0;i<MAX_OTP_SLOTS;i++){
        if(!g_vault.otp[i].active) continue;
        if(shown<list_scroll){shown++;continue;}
        if(row>=ROWS-2) break;
        char buf[40]; buf[0]=' '; buf[1]=' ';
        sv_cpy(buf+2,g_vault.otp[i].label,30);
        if((shown-list_scroll)==list_sel){trow_bg(row,C_ACCENT);tput(0,row,buf,C_BG);}
        else tput(0,row,buf,C_FG);
        row++; shown++;
    }
    if(shown==0) tput(2,3,"  (no slots — add one)",C_DIM);
    tput(0,ROWS-2,"  [+ADD]",C_ACCENT);
    draw_action("BACK","ENTER");
    display_flush();
}

/* ── TOTP CODE ───────────────────────────────────────────────────────────── */
static void draw_totp_code(void) {
    display_clear(C_BG);
    draw_status("KEYS/TOTP",0,g_vault.ble_enabled,100);
    clear_content();
    otp_slot_t *sl=&g_vault.otp[list_sel];
    /* Label */
    char lbuf[40]; lbuf[0]=' '; sv_cpy(lbuf+1,sl->label,32);
    tput(0,1,lbuf,C_FG);
    /* Code — space-separated */
    uint32_t code=totp_code;
    char cbuf[12]; cbuf[9]='\0';
    for(int i=5;i>=0;i--){ cbuf[i+(i>=3?1:0)]='0'+code%10; code/=10; }
    cbuf[3]=' ';
    tput(COLS/2-5, 3, cbuf, C_ACCENT);
    /* Progress bar */
    int period=sl->period>0?sl->period:30;
    int remain=totp_remain;
    uint16_t bar_col = remain>20?C_ACCENT:(remain>10?C_WARN:C_DANGER);
    draw_bar(1, 5, remain, period, COLS-6, bar_col, C_BG);
    char tbuf[8]; sv_cpy(tbuf,n2str(remain),7); sv_cpy(tbuf+sv_len(tbuf),"s",2);
    tput(COLS-6,5,tbuf,bar_col);
    /* Slot info */
    tput(0,7,"  slot ",C_DIM);
    tput(7,7,n2str(list_sel+1),C_DIM);
    tput(9,7,"/",C_DIM); tput(10,7,n2str(MAX_OTP_SLOTS),C_DIM);
    tput(COLS-12,7,"[type→BLE]",g_vault.ble_enabled?C_ACCENT:C_DIM);
    draw_action("BACK","TYPE↗");
    display_flush();
}

/* ── FIDO2 LIST ──────────────────────────────────────────────────────────── */
static void draw_fido2_list(void) {
    display_clear(C_BG);
    draw_status("KEYS/FIDO2",0,g_vault.ble_enabled,100);
    clear_content();
    tput(0,1,"  FIDO2 / WebAuthn",C_ACCENT);
    int row=2, shown=0;
    for(int i=0;i<MAX_FIDO2_SLOTS;i++){
        if(!g_vault.fido2[i].active) continue;
        if(row>=ROWS-2) break;
        char buf[60]; buf[0]=' '; buf[1]=' ';
        sv_cpy(buf+2,g_vault.fido2[i].rp_id,28);
        int bl=sv_len(buf); buf[bl]=' ';
        sv_cpy(buf+bl+1,g_vault.fido2[i].user_name,16);
        if(shown==list_sel){trow_bg(row,C_ACCENT);tput(0,row,buf,C_BG);}
        else tput(0,row,buf,C_FG);
        row++; shown++;
    }
    if(shown==0) tput(2,3,"  (no credentials stored)",C_DIM);
    draw_action("BACK","DETAIL");
    display_flush();
}

/* ── FIDO2 APPROVE ───────────────────────────────────────────────────────── */
static void draw_fido2_approve(void) {
    display_clear(C_BG);
    trow_bg(0, C_DANGER);
    draw_status("KEYS/FIDO2",0,g_vault.ble_enabled,100);
    clear_content();
    tput(COLS/2-11,2,"  AUTHENTICATION REQUEST  ",C_WARN);
    tput(0,4,"  RP:   ",C_DIM);  tput(8,4,fido_rp,  C_FG);
    tput(0,5,"  User: ",C_DIM);  tput(8,5,fido_user,C_FG);
    /* Countdown bar */
    int total=400; /* 8s @ 50ms ticks */
    uint16_t bar_col=fido_timeout>200?C_ACCENT:(fido_timeout>100?C_WARN:C_DANGER);
    draw_bar(1,7,fido_timeout,total,COLS-6, bar_col, C_BG);
    int secs=fido_timeout*8/total;
    char tb[6]; sv_cpy(tb,n2str(secs),5); sv_cpy(tb+sv_len(tb),"s",2);
    tput(COLS-5,7,tb,bar_col);
    tput(0,9,"  Hold [SELECT] 1s to approve",C_WARN);
    if(fido_hold_frames>0){
        draw_bar(2,10,fido_hold_frames,50,COLS-4,C_ACCENT,C_BG);
    }
    draw_action("DENY","APPROVE");
    display_flush();
}

/* ── SETUP WELCOME ───────────────────────────────────────────────────────── */
static void draw_setup_welcome(void) {
    display_clear(C_BG);
    draw_status("SETUP",1,0,100);
    clear_content();
    tput(COLS/2-7,2,"  WELCOME TO AKIRAVAULT",C_ACCENT);
    tput(1,4,"  No vault found. First run.",C_FG);
    tput(1,6,"  ▶  Generate new seed phrase",list_sel==0?C_ACCENT:C_FG);
    tput(1,7,"  ▶  Restore from mnemonic",  list_sel==1?C_ACCENT:C_FG);
    tput(1,9,"  !! Your keys, your coins !!",C_WARN);
    draw_action("","ENTER");
    display_flush();
}

/* ── SETUP GENERATE SEED ─────────────────────────────────────────────────── */
static void draw_setup_generate(void) {
    display_clear(C_BG);
    draw_status("SETUP/SEED",1,0,100);
    clear_content();
    tput(0,1,"  24-Word Seed Phrase",C_ACCENT);
    tput(0,2,"  !! WRITE DOWN ALL WORDS !!",C_DANGER);
    /* Show 6 words at a time, 2 columns */
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
    tput(0,ROWS-3,"  Page: ",C_DIM); tput(8,ROWS-3,n2str(list_scroll+1),C_DIM);
    tput(10,ROWS-3,"/4",C_DIM);
    tput(0,ROWS-2,"  Shown once. Never re-displayed.",C_WARN);
    draw_action("PREV","NEXT/OK");
    display_flush();
}

/* ── SETTINGS ────────────────────────────────────────────────────────────── */
static void draw_settings(void) {
    display_clear(C_BG);
    draw_status("SETTINGS",0,g_vault.ble_enabled,100);
    clear_content();
    static const char *sitems[] = {
        "  ▶  Change PIN",
        "  ▶  Auto-lock timeout",
        "  ▶  BLE keyboard",
        "  ▶  Factory reset",
    };
    for(int i=0;i<4;i++){
        int row=2+i*2;
        if(i==list_sel){ trow_bg(row,C_ACCENT); tput(0,row,sitems[i],C_BG); }
        else            { tput(0,row,sitems[i],C_FG); }
        /* value on right */
        if(i==1){ const char *lv=g_vault.autolock_s==0?"Never":
                                  g_vault.autolock_s==30?"30s":
                                  g_vault.autolock_s==60?"1min":"5min";
                  tput(COLS-6,row,lv,C_DIM); }
        if(i==2){ tput(COLS-4,row,g_vault.ble_enabled?"ON ":"OFF",
                        g_vault.ble_enabled?C_ACCENT:C_DIM); }
    }
    draw_action("BACK","ENTER");
    display_flush();
}

/* ── ABOUT ───────────────────────────────────────────────────────────────── */
static void draw_about(void) {
    display_clear(C_BG);
    draw_status("ABOUT",0,g_vault.ble_enabled,100);
    clear_content();
    tput(0,2,"  AkiraVault v2.0.0",C_ACCENT);
    tput(0,3,"  AkiraOS Hardware Security Module",C_FG);
    tput(0,5,"  Crypto: SHA-256/HMAC/PBKDF2",C_DIM);
    tput(0,6,"          ChaCha20/TOTP/FIDO2",C_DIM);
    tput(0,7,"          BIP39/BIP32 (ed25519)",C_DIM);
    tput(0,9,"  Vault key: ChaCha20 + PBKDF2",C_DIM);
    tput(0,10," PIN rounds: 10000",C_DIM);
    tput(0,11," Wipe after: 3 wrong PINs",C_WARN);
    draw_action("BACK","");
    display_flush();
}

/* ── FACTORY RESET ───────────────────────────────────────────────────────── */
static void draw_factory_reset(void) {
    display_clear(C_BG);
    trow_bg(0,C_DANGER);
    draw_status("DANGER",0,0,100);
    clear_content();
    trow_bg(1,C_DANGER);
    tput(0,1,"  !! FACTORY RESET !!",C_FG);
    tput(0,3,"  ALL DATA WILL BE DELETED.",C_WARN);
    tput(0,4,"  Seeds, keys, TOTP — GONE.",C_WARN);
    tput(0,6,"  Hold [SELECT] 3s to confirm.",C_FG);
    if(sign_hold_frames>0){
        int pct=sign_hold_frames*100/150;
        draw_bar(2,8,pct,100,COLS-4,C_DANGER,C_BG);
    }
    draw_action("CANCEL","");
    display_flush();
}

/* ── TOTP ADD ────────────────────────────────────────────────────────────── */
static void draw_totp_add(void) {
    display_clear(C_BG);
    draw_status("KEYS/TOTP",0,g_vault.ble_enabled,100);
    clear_content();
    tput(0,1,"  Add OTP Slot",C_ACCENT);
    tput(0,3,"  Scan QR with companion app",C_DIM);
    tput(0,4,"  or enter secret manually:",C_DIM);
    tput(0,6,"  (BLE companion app required)",C_WARN);
    tput(0,7,"  for QR import on this device",C_DIM);
    draw_action("BACK","");
    display_flush();
}

/* ── SSH AGENT ───────────────────────────────────────────────────────────── */
static void draw_ssh_agent(void) {
    display_clear(C_BG);
    draw_status("KEYS/SSH",0,g_vault.ble_enabled,100);
    clear_content();
    tput(0,2,"  SSH Agent (ed25519)",C_ACCENT);
    tput(0,4,"  Waiting for sign request...",C_DIM);
    tput(0,6,"  Connect via BLE and send",C_DIM);
    tput(0,7,"  SSH auth challenge.",C_DIM);
    tput(0,9,"  Public key fingerprint:",C_DIM);
    tput(0,10," SHA256:aBcD...xYzW",C_FG);
    draw_action("BACK","");
    display_flush();
}

/* ── KEYS HOME ───────────────────────────────────────────────────────────── */
static void draw_keys(void) {
    display_clear(C_BG);
    draw_status("KEYS",0,g_vault.ble_enabled,100);
    clear_content();
    static const char *kitems[]={
        "  ▶  TOTP / HOTP      ","  ▶  FIDO2 / WebAuthn ","  ▶  SSH Agent        "
    };
    for(int i=0;i<3;i++){
        int row=2+i*2;
        if(i==list_sel){ trow_bg(row,C_ACCENT); tput(0,row,kitems[i],C_BG); }
        else            { tput(0,row,kitems[i],C_FG); }
    }
    draw_action("BACK","ENTER");
    display_flush();
}

/* ═══════════════════════════════════════════════════════════════════════════
 * ui_init — reset per-screen state
 * ═══════════════════════════════════════════════════════════════════════════ */
void ui_init(void) {
    list_sel=0; list_scroll=0;
    pin_cur=0; pin_digit_val=0; pin_wrong=0;
    sign_hold_frames=0; fido_hold_frames=0;
    fido_timeout=400;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * ui_draw — dispatch to current screen
 * ═══════════════════════════════════════════════════════════════════════════ */
void ui_draw(void) {
    switch(g_screen){
    case SCR_BOOT:           draw_boot();           break;
    case SCR_SETUP_WELCOME:  draw_setup_welcome();  break;
    case SCR_SETUP_GENERATE: draw_setup_generate(); break;
    case SCR_UNLOCK:         draw_unlock();         break;
    case SCR_HOME:           draw_home();           break;
    case SCR_WALLET:         draw_accounts();       break;
    case SCR_ACCOUNTS:       draw_accounts();       break;
    case SCR_ACCOUNT_DETAIL: draw_account_detail(); break;
    case SCR_SIGN_MESSAGE:   draw_sign_message();   break;
    case SCR_KEYS:           draw_keys();           break;
    case SCR_TOTP_LIST:      draw_totp_list();      break;
    case SCR_TOTP_CODE:      draw_totp_code();      break;
    case SCR_TOTP_ADD:       draw_totp_add();       break;
    case SCR_FIDO2_LIST:     draw_fido2_list();     break;
    case SCR_FIDO2_APPROVE:  draw_fido2_approve();  break;
    case SCR_SSH_AGENT:      draw_ssh_agent();      break;
    case SCR_SETTINGS:       draw_settings();       break;
    case SCR_FACTORY_RESET:  draw_factory_reset();  break;
    case SCR_ABOUT:          draw_about();          break;
    default: break;
    }
}

/* ── Navigation helper ─────────────────────────────────────────────────── */
static void go(screen_t s){ g_prev_screen=g_screen; g_screen=s; ui_init(); }
static void back(void){ g_screen=g_prev_screen; ui_init(); }

/* ═══════════════════════════════════════════════════════════════════════════
 * ui_handle_key — per-screen key dispatch
 * ═══════════════════════════════════════════════════════════════════════════ */
void ui_handle_key(int key, int long_press) {
    /* Emergency lock: long CENTER from anywhere */
    if(long_press && key==KEY_CENTER && g_screen!=SCR_UNLOCK && g_screen!=SCR_BOOT){
        vault_lock(); return;
    }

    switch(g_screen){

    /* ── UNLOCK ─────────────────────────────────────────────────────────── */
    case SCR_UNLOCK:
        pin_wrong=0;
        if(key==KEY_UP)   { pin_digit_val=(pin_digit_val+1)%10; }
        if(key==KEY_DOWN) { pin_digit_val=(pin_digit_val+9)%10; }
        if(key==KEY_RIGHT && pin_cur<PIN_LEN) {
            pin_digits[pin_cur]='0'+pin_digit_val;
            pin_cur++; pin_digit_val=0;
            if(pin_cur==PIN_LEN){
                pin_digits[PIN_LEN]='\0';
                int r=vault_load(pin_digits);
                if(r==0){ go(SCR_HOME); return; }
                else { pin_cur=0; pin_digit_val=0; pin_wrong=1;
                       if(g_hdr.wrong_attempts>=WIPE_AFTER){
                           /* Trigger secure wipe */
                           storage_delete("vault.bin");
                           g_screen=SCR_SETUP_WELCOME;
                       }
                }
            }
        }
        if(key==KEY_LEFT) { if(pin_cur>0) pin_cur--; pin_digit_val=0; }
        break;

    /* ── SETUP WELCOME ──────────────────────────────────────────────────── */
    case SCR_SETUP_WELCOME:
        if(key==KEY_UP||key==KEY_DOWN) list_sel^=1;
        if(key==KEY_RIGHT){
            if(list_sel==0){
                /* Generate new seed */
                bip39_entropy_to_mnemonic(
                    g_vault.entropy,  /* filled by vault_create later */
                    ENTROPY_BYTES_24, seed_words);
                go(SCR_SETUP_GENERATE);
            } else {
                go(SCR_SETUP_RESTORE);
            }
        }
        break;

    /* ── SETUP GENERATE ─────────────────────────────────────────────────── */
    case SCR_SETUP_GENERATE:
        if(key==KEY_RIGHT){ list_scroll++; if(list_scroll>=4){
            /* Done viewing — ask for PIN then create vault */
            go(SCR_UNLOCK); /* Reuse unlock for new PIN entry */
        }}
        if(key==KEY_LEFT && list_scroll>0) list_scroll--;
        break;

    /* ── HOME ───────────────────────────────────────────────────────────── */
    case SCR_HOME:
        if(key==KEY_UP)   list_sel=(list_sel>0)?list_sel-1:3;
        if(key==KEY_DOWN) list_sel=(list_sel<3)?list_sel+1:0;
        if(key==KEY_RIGHT){
            if(list_sel==0) go(SCR_ACCOUNTS);
            if(list_sel==1) go(SCR_KEYS);
            if(list_sel==2) go(SCR_SETTINGS);
            if(list_sel==3) go(SCR_ABOUT);
        }
        if(key==KEY_LEFT) vault_lock();
        break;

    /* ── ACCOUNTS ───────────────────────────────────────────────────────── */
    case SCR_ACCOUNTS: case SCR_WALLET:
        if(key==KEY_UP)   list_sel=(list_sel>0)?list_sel-1:0;
        if(key==KEY_DOWN) list_sel++;
        if(key==KEY_RIGHT) go(SCR_ACCOUNT_DETAIL);
        if(key==KEY_LEFT)  back();
        break;

    /* ── ACCOUNT DETAIL ─────────────────────────────────────────────────── */
    case SCR_ACCOUNT_DETAIL:
        if(key==KEY_LEFT) back();
        if(key==KEY_RIGHT){ sv_cpy(sign_account,g_vault.accounts[list_sel].name,32);
                            sv_cpy(sign_hash,"a3f7...9c12",12);
                            sign_len=32; sign_hold_frames=0; go(SCR_SIGN_MESSAGE); }
        break;

    /* ── SIGN MESSAGE ───────────────────────────────────────────────────── */
    case SCR_SIGN_MESSAGE:
        if(key==KEY_LEFT) back();
        if(key==KEY_CENTER){ sign_hold_frames++;
            if(sign_hold_frames>=75){ /* ~1.5s at 20ms ticks */
                /* Signing approved — in production: call secp256k1_sign */
                sign_hold_frames=0; back();
            }
        } else sign_hold_frames=0;
        break;

    /* ── KEYS ───────────────────────────────────────────────────────────── */
    case SCR_KEYS:
        if(key==KEY_UP)   list_sel=(list_sel>0)?list_sel-1:2;
        if(key==KEY_DOWN) list_sel=(list_sel<2)?list_sel+1:0;
        if(key==KEY_RIGHT){
            if(list_sel==0) go(SCR_TOTP_LIST);
            if(list_sel==1) go(SCR_FIDO2_LIST);
            if(list_sel==2) go(SCR_SSH_AGENT);
        }
        if(key==KEY_LEFT) back();
        break;

    /* ── TOTP LIST ──────────────────────────────────────────────────────── */
    case SCR_TOTP_LIST:
        if(key==KEY_UP)   { if(list_sel>0) list_sel--; else if(list_scroll>0) list_scroll--; }
        if(key==KEY_DOWN) list_sel++;
        if(key==KEY_RIGHT) go(SCR_TOTP_CODE);
        if(key==KEY_LEFT)  back();
        break;

    /* ── TOTP CODE ──────────────────────────────────────────────────────── */
    case SCR_TOTP_CODE:
        if(key==KEY_LEFT) back();
        if(key==KEY_CENTER && g_vault.ble_enabled){
            /* Type TOTP via BLE HID */
            char code_str[10]; uint32_zero_pad_6(totp_code, 6, code_str);
            hid_type_string(code_str);
        }
        if(key==KEY_RIGHT && g_vault.ble_enabled){
            char code_str[10]; uint32_zero_pad_6(totp_code, 6, code_str);
            hid_type_string(code_str);
        }
        break;

    /* ── FIDO2 LIST ─────────────────────────────────────────────────────── */
    case SCR_FIDO2_LIST:
        if(key==KEY_UP)   list_sel=(list_sel>0)?list_sel-1:0;
        if(key==KEY_DOWN) list_sel++;
        if(key==KEY_LEFT) back();
        break;

    /* ── FIDO2 APPROVE ──────────────────────────────────────────────────── */
    case SCR_FIDO2_APPROVE:
        if(key==KEY_LEFT){ fido_hold_frames=0; back(); }
        if(key==KEY_CENTER){ fido_hold_frames++;
            if(fido_hold_frames>=50){ /* ~1s */
                /* FIDO2 approved — sign with ed25519 */
                fido_hold_frames=0; back();
            }
        } else fido_hold_frames=0;
        break;

    /* ── SETTINGS ───────────────────────────────────────────────────────── */
    case SCR_SETTINGS:
        if(key==KEY_UP)   list_sel=(list_sel>0)?list_sel-1:3;
        if(key==KEY_DOWN) list_sel=(list_sel<3)?list_sel+1:0;
        if(key==KEY_LEFT) back();
        if(key==KEY_RIGHT){
            if(list_sel==0) go(SCR_PIN_CHANGE);
            if(list_sel==1){ g_vault.autolock_s=
                             g_vault.autolock_s==0?30:g_vault.autolock_s==30?60:
                             g_vault.autolock_s==60?300:0; g_vault_dirty=1; }
            if(list_sel==2){ g_vault.ble_enabled^=1; g_vault_dirty=1; }
            if(list_sel==3){ sign_hold_frames=0; go(SCR_FACTORY_RESET); }
        }
        break;

    /* ── FACTORY RESET ──────────────────────────────────────────────────── */
    case SCR_FACTORY_RESET:
        if(key==KEY_LEFT){ sign_hold_frames=0; back(); }
        if(key==KEY_CENTER){ sign_hold_frames++;
            if(sign_hold_frames>=150){ /* ~3s */
                storage_delete("vault.bin");
                sign_hold_frames=0; g_screen=SCR_SETUP_WELCOME; ui_init();
            }
        } else sign_hold_frames=0;
        break;

    /* ── ABOUT ──────────────────────────────────────────────────────────── */
    case SCR_ABOUT:
        if(key==KEY_LEFT) back();
        break;

    /* ── SSH AGENT / TOTP ADD ───────────────────────────────────────────── */
    case SCR_SSH_AGENT: case SCR_TOTP_ADD:
        if(key==KEY_LEFT) back();
        break;

    default: break;
    }
}

/* ── Tick called from main loop for live updates ─────────────────────────── */
void ui_tick(uint64_t unix_sec) {
    if(g_screen==SCR_TOTP_CODE){
        otp_slot_t *sl=&g_vault.otp[list_sel];
        int period=sl->period>0?sl->period:30;
        totp_remain = (int)(period - unix_sec % period);
        if(sl->active && sl->secret_len>0)
            totp_code=totp_generate(sl->secret,sl->secret_len,unix_sec,period,sl->digits>0?sl->digits:6);
    }
    if(g_screen==SCR_FIDO2_APPROVE){
        if(fido_timeout>0) fido_timeout--;
        else { back(); } /* auto-deny on timeout */
    }
}

/* Helper: zero-pad OTP code to N digits */
void uint32_zero_pad_6(uint32_t v, int digits, char *out){
    out[digits]='\0';
    for(int i=digits-1;i>=0;i--){out[i]='0'+v%10;v/=10;}
}
