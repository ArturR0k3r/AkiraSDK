/*
 * key_ui.c — AkiraKey electronic key UI
 * Screens: Unlock, Home, TOTP, FIDO2, Passwords, SSH, Settings
 * SPDX-License-Identifier: Apache-2.0
 */
#include "key.h"

#define TX(c) ((c)*GLYPH_W)
#define TY(r) ((r)*GLYPH_H)

static void tput(int c,int r,const char *s,uint32_t col){ display_text(TX(c),TY(r),s,col); }
static void trow_bg(int r,uint32_t col){ display_rect(0,TY(r),GW,GLYPH_H,col); }

/* ── Status bar ─────────────────────────────────────────────────────────── */
static void draw_status(const char *name, int locked, int ble) {
    trow_bg(0, C_HEADER);
    char buf[18]; buf[0]='[';
    int i=1; const char *p=name;
    while(*p&&i<14) buf[i++]=*p++;
    buf[i++]=']'; buf[i]='\0';
    tput(0, 0, buf, C_ACCENT);
    if(locked) tput(COLS-8, 0, "[LOCKED]", C_DANGER);
    if(ble)    tput(COLS-5, 0, "[BLE]",  C_ACCENT);
    display_hline(0, GLYPH_H-1, GW, C_ACCENT);
}

static void draw_action(const char *L, const char *R) {
    int br=ROWS-1;
    trow_bg(br, C_HEADER);
    display_hline(0, TY(br), GW, C_ACCENT);
    if(L[0]) {
        char lb[14]="["; sv_ncpy(lb+1,L,10); lb[sv_len(lb)]=']'; lb[sv_len(lb)]='\0';
        tput(0, br, lb, C_DIM);
    }
    if(R[0]) {
        char rb[14]="["; sv_ncpy(rb+1,R,10); rb[sv_len(rb)]=']'; rb[sv_len(rb)]='\0';
        tput(COLS-sv_len(rb), br, rb, C_ACCENT);
    }
}
static void clear_content(void) { display_rect(0,GLYPH_H,GW,GH-GLYPH_H*2,C_BG); }

static char _nb[16];
static const char *n2s(uint32_t v){
    if(!v){_nb[0]='0';_nb[1]='\0';return _nb;}
    int i=0;uint32_t t=v;
    while(t){_nb[i++]='0'+t%10;t/=10;}
    for(int a=0,b=i-1;a<b;a++,b--){char x=_nb[a];_nb[a]=_nb[b];_nb[b]=x;}
    _nb[i]='\0';return _nb;
}

static void draw_bar(int col,int row,int val,int max,int w,uint32_t fg,uint32_t bg){
    int f=(max>0)?val*w/max:0; char bar[64]; int i=0;
    for(;i<f&&i<w;i++) bar[i]='\xDB';
    for(;i<w;i++) bar[i]='\xB0';
    bar[i]='\0';
    display_rect(TX(col),TY(row),w*GLYPH_W,GLYPH_H,bg);
    tput(col,row,bar,fg);
}

/* ── Screen-local state ─────────────────────────────────────────────────── */
static char    pin_digits[PIN_LEN+1];
static int     pin_cur, pin_digit_val, pin_wrong;
static int     list_sel, list_scroll;
static uint32_t totp_code;
static int     totp_remain;
static int     hold_frames = 0;

/* ── Change-PIN state (0=verify old, 1=enter new, 2=confirm new) ─────────── */
static int  chpin_phase;
static char chpin_new[PIN_LEN+1];
static int  chpin_err;

/* ── BOOT ────────────────────────────────────────────────────────────────── */
static void draw_boot(void) {
    display_clear(C_BG);
    int cy=ROWS/2-3;
    tput(COLS/2-8,cy,   "  ┌─────────────────┐",C_ACCENT);
    tput(COLS/2-8,cy+1, "  │   AKIRAKEY  🔑  │",C_ACCENT);
    tput(COLS/2-8,cy+2, "  │  Electronic Key  │",C_FG);
    tput(COLS/2-8,cy+3, "  │  TOTP/FIDO2/SSH  │",C_DIM);
    tput(COLS/2-8,cy+4, "  └─────────────────┘",C_ACCENT);
    tput(COLS/2-9,cy+6, "2FA • PassKey • SSH Agent",C_DIM);
    display_flush();
}

/* ── UNLOCK ──────────────────────────────────────────────────────────────── */
static void draw_unlock(void) {
    display_clear(C_BG);
    draw_status("AKIRAKEY", 1, 0);
    clear_content();
    int mr=ROWS/2-2;
    tput((COLS-9)/2,mr,"ENTER PIN",C_FG);
    int pin_col=(COLS-PIN_LEN*3)/2;
    char dotrow[32]; int di=0;
    for(int i=0;i<PIN_LEN;i++){
        dotrow[di++]='[';
        dotrow[di++]=(i<pin_cur)?'*':((i==pin_cur)?'0'+pin_digit_val:' ');
        dotrow[di++]=']';
    }
    dotrow[di]='\0';
    tput(pin_col,mr+2,dotrow,C_ACCENT);
    if(pin_wrong){
        trow_bg(mr+4,C_DANGER);
        tput(COLS/2-5,mr+4," WRONG PIN ",C_FG);
    } else if(g_key_hdr.wrong_attempts>0){
        tput(2,mr+4,"attempts left: ",C_WARN);
        tput(17,mr+4,n2s(WIPE_AFTER-g_key_hdr.wrong_attempts),C_WARN);
    }
    tput(1,ROWS-3,"hold B = reset vault",C_DIM);
    draw_action("CLR","ENTER");
    display_flush();
}

/* ── HOME ────────────────────────────────────────────────────────────────── */
static void draw_home(void) {
    display_clear(C_BG);
    draw_status("HOME", 0, g_key_vault.ble_enabled);
    clear_content();
    static const char *items[]={
        "  \x10  TOTP / HOTP     2FA codes",
        "  \x10  FIDO2 / PassKey  WebAuthn",
        "  \x10  Passwords        BLE type",
        "  \x10  SSH Agent        ed25519",
        "  \x10  SETTINGS         PIN, BLE",
    };
    for(int i=0;i<5;i++){
        int row=2+i;
        if(i==list_sel){ trow_bg(row,C_ACCENT); tput(0,row,items[i],C_BG); }
        else            { tput(0,row,items[i],C_FG); }
    }
    draw_action("LOCK","ENTER");
    display_flush();
}

/* ── TOTP LIST ───────────────────────────────────────────────────────────── */
static void draw_totp_list(void) {
    display_clear(C_BG);
    draw_status("TOTP/HOTP",0,g_key_vault.ble_enabled);
    clear_content();
    tput(0,1,"  One-Time Passwords",C_ACCENT);
    int row=2,shown=0;
    for(int i=0;i<MAX_TOTP_SLOTS;i++){
        if(!g_key_vault.totp[i].active) continue;
        if(shown<list_scroll){shown++;continue;}
        if(row>=ROWS-2) break;
        char buf[40]; buf[0]=' '; buf[1]=' ';
        sv_cpy(buf+2,g_key_vault.totp[i].label,32);
        if((shown-list_scroll)==list_sel){trow_bg(row,C_ACCENT);tput(0,row,buf,C_BG);}
        else tput(0,row,buf,C_FG);
        row++;shown++;
    }
    if(shown==0) tput(2,3,"  (no slots — add via companion app)",C_DIM);
    tput(0,ROWS-2,"  [ENTER] view code  [A] add stub",C_DIM);
    draw_action("BACK","ENTER");
    display_flush();
}

/* ── TOTP VIEW ───────────────────────────────────────────────────────────── */
static void draw_totp_view(void) {
    display_clear(C_BG);
    draw_status("TOTP",0,g_key_vault.ble_enabled);
    clear_content();
    totp_slot_t *sl=&g_key_vault.totp[list_sel];
    char lbuf[36]; lbuf[0]=' '; sv_cpy(lbuf+1,sl->label,34);
    tput(0,1,lbuf,C_FG);

    /* Large code display — split 3+3 */
    int digits=sl->digits>0?sl->digits:6;
    uint32_t code=totp_code;
    char cbuf[10]; cbuf[0]='\0';
    for(int i=digits-1;i>=0;i--){ cbuf[i]='0'+code%10; code/=10; }
    cbuf[digits]='\0';
    /* Put a space in the middle */
    char disp[12];
    int half=digits/2;
    for(int i=0;i<half;i++) disp[i]=cbuf[i];
    disp[half]=' '; disp[half+1]=' ';
    for(int i=half;i<digits;i++) disp[i+2]=cbuf[i];
    disp[digits+2]='\0';
    display_text_large(TX(COLS/2-digits/2-1), TY(3), disp, C_ACCENT);

    /* Countdown bar */
    int period=sl->period>0?sl->period:30;
    int remain=totp_remain;
    uint32_t bar_col=remain>20?C_ACCENT:(remain>10?C_WARN:C_DANGER);
    draw_bar(1,5,remain,period,COLS-6,bar_col,C_BG);
    char tbuf[8]; sv_cpy(tbuf,n2s(remain),7); sv_cpy(tbuf+sv_len(tbuf),"s",2);
    tput(COLS-5,5,tbuf,bar_col);

    if(g_key_vault.ble_enabled)
        tput(0,7,"  [A] Type via BLE keyboard",C_DIM);
    else
        tput(0,7,"  Enable BLE in settings",C_WARN);

    draw_action("BACK","TYPE");
    display_flush();
}

/* ── FIDO2 LIST ──────────────────────────────────────────────────────────── */
static void draw_fido2_list(void) {
    display_clear(C_BG);
    draw_status("FIDO2",0,g_key_vault.ble_enabled);
    clear_content();
    tput(0,1,"  FIDO2 / WebAuthn / PassKey",C_ACCENT);
    int row=2,shown=0;
    for(int i=0;i<MAX_FIDO2_SLOTS;i++){
        if(!g_key_vault.fido2[i].active) continue;
        if(row>=ROWS-2) break;
        char buf[60]; buf[0]=' '; buf[1]=' ';
        sv_cpy(buf+2,g_key_vault.fido2[i].rp_id,28);
        int bl=sv_len(buf); buf[bl]=' ';
        sv_cpy(buf+bl+1,g_key_vault.fido2[i].user_name,16);
        if(shown==list_sel){trow_bg(row,C_ACCENT);tput(0,row,buf,C_BG);}
        else tput(0,row,buf,C_FG);
        row++;shown++;
    }
    if(shown==0) tput(2,3,"  (no credentials stored)",C_DIM);
    tput(0,ROWS-2,"  resident keys: ed25519",C_DIM);
    draw_action("BACK","DETAIL");
    display_flush();
}

/* ── FIDO2 DETAIL/APPROVE ────────────────────────────────────────────────── */
static void draw_fido2_view(void) {
    display_clear(C_BG);
    trow_bg(0,C_DANGER);
    draw_status("FIDO2/AUTH",0,g_key_vault.ble_enabled);
    clear_content();
    fido2_cred_t *cr=&g_key_vault.fido2[list_sel];
    tput(0,2,"  FIDO2 Credential",C_ACCENT);
    tput(0,4,"  RP:   ",C_DIM); tput(8,4,cr->rp_id,C_FG);
    tput(0,5,"  User: ",C_DIM); tput(8,5,cr->user_name,C_FG);
    tput(0,6,"  Signs:",C_DIM); tput(8,6,n2s(cr->sign_count),C_FG);
    tput(0,8,"  Hold [A] 2s to sign challenge",C_WARN);
    draw_bar(2,9,hold_frames,100,COLS-4,C_ACCENT,C_BG);
    draw_action("BACK","SIGN");
    display_flush();
}

/* ── PASSWORDS LIST ──────────────────────────────────────────────────────── */
static void draw_pass_list(void) {
    display_clear(C_BG);
    draw_status("PASSWORDS",0,g_key_vault.ble_enabled);
    clear_content();
    tput(0,1,"  Stored Passwords",C_ACCENT);
    int row=2,shown=0;
    for(int i=0;i<MAX_PASS_SLOTS;i++){
        if(!g_key_vault.pass[i].active) continue;
        if(row>=ROWS-2) break;
        char buf[40]; buf[0]=' '; buf[1]=' ';
        sv_cpy(buf+2,g_key_vault.pass[i].label,32);
        if(shown==list_sel){trow_bg(row,C_ACCENT);tput(0,row,buf,C_BG);}
        else tput(0,row,buf,C_FG);
        row++;shown++;
    }
    if(shown==0) tput(2,3,"  (no entries — add via companion app)",C_DIM);
    draw_action("BACK","TYPE");
    display_flush();
}

/* ── PASSWORD VIEW ───────────────────────────────────────────────────────── */
static void draw_pass_view(void) {
    display_clear(C_BG);
    draw_status("PASSWORDS",0,g_key_vault.ble_enabled);
    clear_content();
    pass_slot_t *p=&g_key_vault.pass[list_sel];
    tput(0,1,"  ",C_ACCENT); tput(2,1,p->label,C_ACCENT);
    tput(0,3,"  User: ",C_DIM);     tput(8,3,p->username,C_FG);
    tput(0,4,"  Pass: ",C_DIM);     tput(8,4,"••••••••",C_DIM);
    tput(0,6,"  [A] Type username via BLE",C_DIM);
    tput(0,7,"  [B] Type password via BLE",C_DIM);
    if(!g_key_vault.ble_enabled) tput(0,9,"  ! BLE disabled",C_WARN);
    draw_action("BACK","TYPE");
    display_flush();
}

/* ── SSH VIEW ────────────────────────────────────────────────────────────── */
static void draw_ssh_view(void) {
    display_clear(C_BG);
    draw_status("SSH AGENT",0,g_key_vault.ble_enabled);
    clear_content();
    tput(0,1,"  SSH Agent — ed25519",C_ACCENT);
    tput(0,3,"  Public key fingerprint:",C_DIM);
    /* Show first 16 bytes of pub key as hex */
    char hex[36]; int hi=0;
    static const char hx[]="0123456789abcdef";
    for(int i=0;i<8;i++){
        hex[hi++]=hx[g_key_vault.ssh_pub[i]>>4];
        hex[hi++]=hx[g_key_vault.ssh_pub[i]&0xF];
        if(i==3) hex[hi++]=':';
    }
    hex[hi]='\0';
    tput(2,4,hex,C_FG);
    tput(0,6,"  BLE: waiting for SSH challenge",C_DIM);
    tput(0,7,"  Connect via ssh-agent socket",C_DIM);
    tput(0,9,"  [A] Export pubkey via BLE",g_key_vault.ble_enabled?C_ACCENT:C_DIM);
    draw_action("BACK","EXPORT");
    display_flush();
}

/* ── SET PIN (first run) ─────────────────────────────────────────────────── */
static int  setpin_phase; /* 0=enter, 1=confirm */
static char setpin_buf[PIN_LEN+1];

static void draw_set_pin(void) {
    display_clear(C_BG);
    trow_bg(0, C_HEADER);
    tput((COLS-11)/2, 0, "[AKIRAKEY]", C_ACCENT);
    display_hline(0, GLYPH_H-1, GW, C_ACCENT);
    int mr=3;
    if(setpin_phase==0){
        tput((COLS-16)/2, mr,   "  Set your PIN  ", C_FG);
        tput((COLS-22)/2, mr+1, "  (6 digits, remember it!)  ", C_DIM);
    } else {
        tput((COLS-14)/2, mr,   "  Confirm PIN  ", C_ACCENT);
    }
    char dotrow[PIN_LEN*3+2]; int di=0;
    for(int i=0;i<PIN_LEN;i++){
        if(i>0){dotrow[di++]=' ';}
        dotrow[di++]=(i<pin_cur)?'*':((i==pin_cur)?'0'+pin_digit_val:'_');
    }
    dotrow[di]='\0';
    int pin_col=(COLS-PIN_LEN*2)/2;
    tput(pin_col, mr+3, dotrow, C_ACCENT);
    if(chpin_err==2) tput(COLS/2-6, mr+5, " NO MATCH ", C_DANGER);
    tput(0, mr+6, "  UP/DOWN: digit   RIGHT: next", C_DIM);
    draw_action("", "NEXT");
    display_flush();
}

/* ── CHANGE PIN ──────────────────────────────────────────────────────────── */
static void draw_change_pin(void) {
    display_clear(C_BG);
    draw_status("CHANGE PIN",0,0);
    clear_content();
    static const char *phases[]={"Step 1/3  Current PIN","Step 2/3  New PIN    ","Step 3/3  Confirm PIN"};
    int ph=chpin_phase<3?chpin_phase:0;
    tput((COLS-20)/2,2,phases[ph],C_ACCENT);
    int mr=4;
    char dotrow[PIN_LEN*3+2]; int di=0;
    for(int i=0;i<PIN_LEN;i++){
        if(i>0){dotrow[di++]=' ';}
        dotrow[di++]=(i<pin_cur)?'*':((i==pin_cur)?'0'+pin_digit_val:'_');
    }
    dotrow[di]='\0';
    int pin_col=(COLS-PIN_LEN*2)/2;
    tput(pin_col,mr+1,dotrow,C_ACCENT);
    if(chpin_err==1) tput(COLS/2-6,mr+3," WRONG PIN ",C_DANGER);
    if(chpin_err==2) tput(COLS/2-7,mr+3," NO MATCH  ",C_DANGER);
    tput(0,mr+5,"  UP/DOWN: digit   RIGHT: next",C_DIM);
    draw_action("CANCEL","CONFIRM");
    display_flush();
}

/* ── SETTINGS ────────────────────────────────────────────────────────────── */
static void draw_settings(void) {
    display_clear(C_BG);
    draw_status("SETTINGS",0,g_key_vault.ble_enabled);
    clear_content();
    static const char *sitems[]={
        "  ▶  Change PIN",
        "  ▶  BLE keyboard",
        "  ▶  Auto-lock timeout",
        "  ▶  Factory reset",
    };
    const char *lock_lbl=g_key_vault.autolock_s==0?"Never":
                          g_key_vault.autolock_s==30?"30s":
                          g_key_vault.autolock_s==60?"1min":"5min";
    for(int i=0;i<4;i++){
        int row=2+i*2;
        if(i==list_sel){trow_bg(row,C_ACCENT);tput(0,row,sitems[i],C_BG);}
        else tput(0,row,sitems[i],C_FG);
        if(i==1) tput(COLS-4,row,g_key_vault.ble_enabled?"ON ":"OFF",
                      (i==list_sel)?C_BG:(g_key_vault.ble_enabled?C_ACCENT:C_DIM));
        if(i==2) tput(COLS-6,row,lock_lbl,(i==list_sel)?C_BG:C_DIM);
    }
    draw_action("BACK","ENTER");
    display_flush();
}

/* ── FACTORY RESET ───────────────────────────────────────────────────────── */
static void draw_factory_reset(void) {
    display_clear(C_BG);
    trow_bg(0,C_DANGER);
    draw_status("DANGER!",0,0);
    clear_content();
    trow_bg(1,C_DANGER);
    tput(0,1,"  !! FACTORY RESET !!",C_FG);
    tput(0,3,"  ALL DATA WILL BE DELETED.",C_WARN);
    tput(0,5,"  Hold [A] 3s to confirm.",C_FG);
    if(hold_frames>0){ int pct=hold_frames*100/150; draw_bar(2,7,pct,100,COLS-4,C_DANGER,C_BG); }
    draw_action("CANCEL","");
    display_flush();
}

/* ── ABOUT ───────────────────────────────────────────────────────────────── */
static void draw_about(void) {
    display_clear(C_BG);
    draw_status("ABOUT",0,0);
    clear_content();
    tput(0,2,"  AkiraKey v1.0",C_ACCENT);
    tput(0,3,"  Electronic Security Key",C_FG);
    tput(0,5,"  Features:",C_DIM);
    tput(0,6,"    TOTP/HOTP (RFC 6238/4226)",C_DIM);
    tput(0,7,"    FIDO2 / WebAuthn",C_DIM);
    tput(0,8,"    SSH Agent (ed25519)",C_DIM);
    tput(0,9,"    BLE HID keyboard",C_DIM);
    tput(0,11," For cold wallet use AkiraVault",C_DIM);
    draw_action("BACK","");
    display_flush();
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Public interface
 * ═══════════════════════════════════════════════════════════════════════════ */
void ui_init(void) {
    list_sel=0; list_scroll=0;
    pin_cur=0; pin_digit_val=0; pin_wrong=0;
    hold_frames=0;
    if(g_screen==SCR_CHANGE_PIN){ chpin_phase=0; chpin_err=0; }
    if(g_screen==SCR_SET_PIN){ setpin_phase=0; chpin_err=0; }
}

void ui_draw(void) {
    switch(g_screen){
    case SCR_BOOT:         draw_boot();          break;
    case SCR_UNLOCK:       draw_unlock();        break;
    case SCR_HOME:         draw_home();          break;
    case SCR_TOTP_LIST:    draw_totp_list();     break;
    case SCR_TOTP_VIEW:    draw_totp_view();     break;
    case SCR_FIDO2_LIST:   draw_fido2_list();    break;
    case SCR_FIDO2_VIEW:   draw_fido2_view();    break;
    case SCR_PASS_LIST:    draw_pass_list();     break;
    case SCR_PASS_VIEW:    draw_pass_view();     break;
    case SCR_SSH_VIEW:     draw_ssh_view();      break;
    case SCR_SET_PIN:      draw_set_pin();       break;
    case SCR_SETTINGS:     draw_settings();      break;
    case SCR_CHANGE_PIN:   draw_change_pin();    break;
    case SCR_FACTORY_RESET:draw_factory_reset(); break;
    case SCR_ABOUT:        draw_about();         break;
    default: break;
    }
}

static void go(screen_t s){ g_prev_screen=g_screen; g_screen=s; ui_init(); }
static void back(void){ g_screen=g_prev_screen; ui_init(); }

void ui_handle_key(int key, int long_press) {
    if(long_press && key==KEY_B && g_screen==SCR_UNLOCK){
        storage_delete("key.bin"); g_screen=SCR_SET_PIN; ui_init(); return;
    }
    if(long_press && key==KEY_B && g_screen!=SCR_BOOT){
        kstore_lock(); return;
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
                if(kstore_load(pin_digits)==0){ go(SCR_HOME); return; }
                pin_cur=0; pin_digit_val=0; pin_wrong=1;
                if(g_key_hdr.wrong_attempts>=WIPE_AFTER){
                    storage_delete("key.bin"); g_screen=SCR_SET_PIN; ui_init();
                }
            }
        }
        if(key==KEY_LEFT && pin_cur>0){ pin_cur--; pin_digit_val=0; }
        break;

    case SCR_HOME:
        if(key==KEY_UP)   list_sel=(list_sel+4)%5;
        if(key==KEY_DOWN) list_sel=(list_sel+1)%5;
        if(key==KEY_LEFT) kstore_lock();
        if(key==KEY_RIGHT){
            if(list_sel==0) go(SCR_TOTP_LIST);
            if(list_sel==1) go(SCR_FIDO2_LIST);
            if(list_sel==2) go(SCR_PASS_LIST);
            if(list_sel==3) go(SCR_SSH_VIEW);
            if(list_sel==4) go(SCR_SETTINGS);
        }
        break;

    case SCR_TOTP_LIST:
        if(key==KEY_UP && list_sel>0) list_sel--;
        if(key==KEY_DOWN) list_sel++;
        if(key==KEY_RIGHT) go(SCR_TOTP_VIEW);
        if(key==KEY_LEFT)  back();
        break;

    case SCR_TOTP_VIEW:
        if(key==KEY_LEFT) back();
        if((key==KEY_A||key==KEY_RIGHT) && g_key_vault.ble_enabled){
            char code_str[10];
            int digs=g_key_vault.totp[list_sel].digits>0?g_key_vault.totp[list_sel].digits:6;
            uint32_t c=totp_code;
            code_str[digs]='\0';
            for(int i=digs-1;i>=0;i--){ code_str[i]='0'+c%10; c/=10; }
            hid_type_string(code_str);
        }
        break;

    case SCR_FIDO2_LIST:
        if(key==KEY_UP && list_sel>0) list_sel--;
        if(key==KEY_DOWN) list_sel++;
        if(key==KEY_RIGHT) go(SCR_FIDO2_VIEW);
        if(key==KEY_LEFT)  back();
        break;

    case SCR_FIDO2_VIEW:
        if(key==KEY_LEFT){ hold_frames=0; back(); }
        if(key==KEY_A){
            if(++hold_frames>=100){ /* ~2s */
                g_key_vault.fido2[list_sel].sign_count++;
                g_dirty=1; hold_frames=0; back();
            }
        } else if(key!=KEY_A) hold_frames=0;
        break;

    case SCR_PASS_LIST:
        if(key==KEY_UP && list_sel>0) list_sel--;
        if(key==KEY_DOWN) list_sel++;
        if(key==KEY_RIGHT) go(SCR_PASS_VIEW);
        if(key==KEY_LEFT)  back();
        break;

    case SCR_PASS_VIEW:
        if(key==KEY_LEFT) back();
        if(key==KEY_A && g_key_vault.ble_enabled)
            hid_type_string(g_key_vault.pass[list_sel].username);
        if(key==KEY_B && g_key_vault.ble_enabled)
            hid_type_string(g_key_vault.pass[list_sel].password);
        break;

    case SCR_SSH_VIEW:
        if(key==KEY_LEFT) back();
        if(key==KEY_A && g_key_vault.ble_enabled){
            /* Export pubkey fingerprint via BLE — type as hex string */
            char hexout[66]; int hi=0;
            static const char hx[]="0123456789abcdef";
            for(int i=0;i<32;i++){
                hexout[hi++]=hx[g_key_vault.ssh_pub[i]>>4];
                hexout[hi++]=hx[g_key_vault.ssh_pub[i]&0xF];
            }
            hexout[hi]='\0';
            hid_type_string(hexout);
        }
        break;

    case SCR_SET_PIN:
        chpin_err=0;
        if(key==KEY_UP)   pin_digit_val=(pin_digit_val+1)%10;
        if(key==KEY_DOWN) pin_digit_val=(pin_digit_val+9)%10;
        if(key==KEY_LEFT && pin_cur>0){ pin_cur--; pin_digit_val=0; }
        if(key==KEY_RIGHT){
            pin_digits[pin_cur]='0'+pin_digit_val;
            if(++pin_cur==PIN_LEN){
                pin_digits[PIN_LEN]='\0';
                if(setpin_phase==0){
                    sv_cpy(setpin_buf, pin_digits, PIN_LEN+1);
                    setpin_phase=1; pin_cur=0; pin_digit_val=0;
                } else {
                    if(sv_cmp(pin_digits, setpin_buf)!=0){
                        chpin_err=2; setpin_phase=0; pin_cur=0; pin_digit_val=0;
                    } else {
                        kstore_create(setpin_buf);
                        g_screen=SCR_HOME; ui_init();
                    }
                }
            }
        }
        break;

    case SCR_SETTINGS:
        if(key==KEY_UP)   list_sel=(list_sel+3)%4;
        if(key==KEY_DOWN) list_sel=(list_sel+1)%4;
        if(key==KEY_LEFT) back();
        if(key==KEY_RIGHT){
            if(list_sel==0){ go(SCR_CHANGE_PIN); }
            if(list_sel==1){ g_key_vault.ble_enabled^=1; g_dirty=1; }
            if(list_sel==2){ g_key_vault.autolock_s=
                             g_key_vault.autolock_s==0?30:g_key_vault.autolock_s==30?60:
                             g_key_vault.autolock_s==60?300:0; g_dirty=1; }
            if(list_sel==3){ hold_frames=0; go(SCR_FACTORY_RESET); }
        }
        break;

    case SCR_CHANGE_PIN:
        chpin_err=0;
        if(key==KEY_UP)   pin_digit_val=(pin_digit_val+1)%10;
        if(key==KEY_DOWN) pin_digit_val=(pin_digit_val+9)%10;
        if(key==KEY_LEFT){ if(pin_cur>0){ pin_cur--; pin_digit_val=0; } else back(); }
        if(key==KEY_RIGHT){
            pin_digits[pin_cur]='0'+pin_digit_val;
            if(++pin_cur==PIN_LEN){
                pin_digits[PIN_LEN]='\0';
                if(chpin_phase==0){
                    /* verify current PIN */
                    if(kstore_load(pin_digits)!=0){ chpin_err=1; pin_cur=0; pin_digit_val=0; }
                    else{ chpin_phase=1; pin_cur=0; pin_digit_val=0; }
                } else if(chpin_phase==1){
                    /* store new PIN candidate */
                    sv_cpy(chpin_new,pin_digits,PIN_LEN+1); chpin_phase=2; pin_cur=0; pin_digit_val=0;
                } else {
                    /* confirm new PIN */
                    if(sv_cmp(pin_digits,chpin_new)!=0){ chpin_err=2; chpin_phase=1; pin_cur=0; pin_digit_val=0; }
                    else{
                        kstore_create(chpin_new); /* re-encrypt vault with new PIN */
                        back();
                    }
                }
            }
        }
        break;

    case SCR_FACTORY_RESET:
        if(key==KEY_LEFT){ hold_frames=0; back(); }
        if(key==KEY_A){
            if(++hold_frames>=150){
                storage_delete("key.bin"); hold_frames=0;
                g_screen=SCR_UNLOCK; ui_init();
            }
        } else hold_frames=0;
        break;

    case SCR_ABOUT:
        if(key==KEY_LEFT) back();
        break;

    default: break;
    }
}

void ui_tick(uint64_t unix_sec) {
    if(g_screen==SCR_TOTP_VIEW){
        totp_slot_t *sl=&g_key_vault.totp[list_sel];
        int period=sl->period>0?sl->period:30;
        totp_remain=(int)(period - unix_sec%period);
        if(sl->active && sl->secret_len>0)
            totp_code=totp_generate(sl->secret,sl->secret_len,unix_sec,period,
                                    sl->digits>0?sl->digits:6);
    }
}
