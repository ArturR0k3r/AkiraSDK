/**
 * @file main.c
 * @brief Cyberdeck HUD — a paged, animated status dashboard for AkiraConsole.
 *
 * Three pages (LEFT/RIGHT to switch):
 *   DASH   : big clock + date, battery icon, network link/IP, radio summary.
 *   RADIO  : live BLE "scope" (nearby advertisers with signal bars) and a
 *            Wi-Fi channel-activity graph from the last AP scan.
 *   SYSTEM : power detail + discharge sparkline, IMU bubble level + temp,
 *            power mode, uptime.
 *
 * Everything degrades to "n/a" when a capability is denied or a radio is
 * idle, so the HUD never hard-fails.
 *
 * Controls:
 *   LEFT/RIGHT = page   Y = Wi-Fi scan   X = clear BLE   A = 12/24h   B = exit
 *
 * @copyright Copyright (c) 2026 PenEngineering S.R.L
 * @license Apache-2.0
 */

#include "akira_api.h"

/* ── Display ─────────────────────────────────────────────────────────── */
static int32_t SCR_W = 320;
static int32_t SCR_H = 240;

/* ── Palette (terminal green on black) ───────────────────────────────── */
#define C_BG      0x0000
#define C_HDR     0x0280
#define C_TEXT    0xE7FC
#define C_DIM     0x5AEB
#define C_GREEN   0x07E6
#define C_CYAN    0x07FF
#define C_AMBER   0xFD20
#define C_RED     0xF800
#define C_MAG     0xF81F
#define C_TILE    0x0841
#define C_TILE_HI 0x1246
#define C_BAR_BG  0x2124
#define C_SCAN    0x0140   /* faint scanline tint */

/* ── Helpers ─────────────────────────────────────────────────────────── */
static int clampi(int v,int lo,int hi){ return v<lo?lo:(v>hi?hi:v); }
static int text_px(const char*s){ int n=0; while(s&&s[n])n++; return n*6; }
static uint16_t lighten(uint16_t c,int a){ int r=(c>>11)&31,g=(c>>5)&63,b=c&31;
    r+=a;g+=a*2;b+=a; if(r>31)r=31;if(g>63)g=63;if(b>31)b=31; return (uint16_t)((r<<11)|(g<<5)|b); }
static uint16_t darken(uint16_t c,int a){ int r=(c>>11)&31,g=(c>>5)&63,b=c&31;
    r-=a;g-=a*2;b-=a; if(r<0)r=0;if(g<0)g=0;if(b<0)b=0; return (uint16_t)((r<<11)|(g<<5)|b); }

static int int_to_str(int v,char*b){ char t[12]; int n=0,p=0;
    if(v<0){b[p++]='-';v=-v;} if(v==0)t[n++]='0';
    while(v>0&&n<11){t[n++]=(char)('0'+v%10);v/=10;} while(n>0)b[p++]=t[--n]; b[p]='\0'; return p; }
static void pad2(int v,char*b){ b[0]=(char)('0'+(v/10)%10); b[1]=(char)('0'+v%10); b[2]='\0'; }

/* ── Epoch → UTC calendar ────────────────────────────────────────────── */
static int is_leap(int y){ return (y%4==0 && y%100!=0) || y%400==0; }
static void unix_to_cal(int t,int*Y,int*Mo,int*D,int*h,int*m,int*s) {
    int days=t/86400, rem=t%86400;
    *h=rem/3600; *m=(rem%3600)/60; *s=rem%60;
    int y=1970;
    for(;;){ int dy=is_leap(y)?366:365; if(days>=dy){days-=dy;y++;} else break; }
    static const int md[12]={31,28,31,30,31,30,31,31,30,31,30,31};
    int mo=0;
    for(;;){ int dm=md[mo]+((mo==1&&is_leap(y))?1:0); if(days>=dm){days-=dm;mo++;} else break; }
    *Y=y; *Mo=mo+1; *D=days+1;
}
static const char *MON[12]={"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
static const char *DOW[7]={"Thu","Fri","Sat","Sun","Mon","Tue","Wed"};

/* ── BLE nearby tally ────────────────────────────────────────────────── */
#define BLE_SLOTS 48
#define BLE_WINDOW 16
static uint8_t  ble_addr[BLE_SLOTS][6];
static uint32_t ble_last[BLE_SLOTS];
static int8_t   ble_rssi[BLE_SLOTS];
static char     ble_name[BLE_SLOTS][20];
static int      ble_used, ble_ok;
static uint32_t g_tick;

static int addr_eq(const uint8_t*a,const uint8_t*b){ for(int i=0;i<6;i++) if(a[i]!=b[i])return 0; return 1; }

static void ble_drain(void) {
    akira_ble_scan_report_t rep;
    int guard=64;
    while (guard-->0 && ble_scan_pop(&rep,sizeof(rep))==1) {
        int slot=-1;
        for(int i=0;i<ble_used;i++) if(addr_eq(ble_addr[i],rep.addr)){slot=i;break;}
        if(slot<0){
            if(ble_used<BLE_SLOTS) slot=ble_used++;
            else { slot=0; uint32_t old=ble_last[0];
                   for(int i=1;i<ble_used;i++) if(ble_last[i]<old){old=ble_last[i];slot=i;} }
            for(int i=0;i<6;i++) ble_addr[slot][i]=rep.addr[i];
        }
        ble_last[slot]=g_tick; ble_rssi[slot]=rep.rssi;
        for(int i=0;i<20;i++){ ble_name[slot][i]=rep.name[i]; if(!rep.name[i])break; }
        ble_name[slot][19]='\0';
    }
}
static int ble_nearby(void){ int n=0; for(int i=0;i<ble_used;i++) if(g_tick-ble_last[i]<BLE_WINDOW)n++; return n; }

/* ── Wi-Fi scan ──────────────────────────────────────────────────────── */
static int wifi_count=-1;
static char wifi_top[33]="";
static int8_t wifi_top_rssi=0;
static akira_wifi_ap_t aps[24];
static int wifi_scanning=0;

static void do_wifi_scan(void) {
    int n=wifi_scan_aps(aps,sizeof(aps));
    if(n<0){ wifi_count=-2; return; }
    wifi_count=n; wifi_top[0]='\0'; wifi_top_rssi=-127;
    for(int i=0;i<n;i++) if(aps[i].rssi>wifi_top_rssi){ wifi_top_rssi=aps[i].rssi;
        int j=0; for(;j<32&&aps[i].ssid[j];j++) wifi_top[j]=(char)aps[i].ssid[j]; wifi_top[j]='\0'; }
    if(!wifi_top[0]){ const char*h="(hidden)"; int j=0; for(;h[j];j++)wifi_top[j]=h[j]; wifi_top[j]='\0'; }
}

/* ── Sub-GHz radios (LR2021 LoRa, CC1121) ────────────────────────────────
 * Presence is probed once via rf_select(); the selected chip's live RSSI
 * (noise floor) is read each frame on the RADIO page.  These are external
 * SPI transceivers, independent of the ESP32's own BLE/Wi-Fi radios.     */
static int lr_present, cc_present, rf_sel=-1;
static void rf_probe(void) {
    cc_present = (rf_select(AKIRA_RF_CHIP_CC1121) == 0);
    lr_present = (rf_select(AKIRA_RF_CHIP_LR2021) == 0);
    rf_sel = lr_present ? AKIRA_RF_CHIP_LR2021 : (cc_present ? AKIRA_RF_CHIP_CC1121 : -1);
    if (rf_sel >= 0) rf_select(rf_sel);
}

/* ── BME280 environment (via the sensor channel API) ─────────────────── */
static int env_temp, env_hum, env_press, env_alt;   /* x1000, or ERROR */
static void env_read(void) {
    env_temp  = sensor_read(SENSOR_CHAN_AMBIENT_TEMP);
    env_hum   = sensor_read(SENSOR_CHAN_HUMIDITY);
    env_press = sensor_read(SENSOR_CHAN_PRESS);
    env_alt   = sensor_read(SENSOR_CHAN_ALTITUDE);
}
static int env_ok(void){ return env_temp!=AKIRA_SENSOR_ERROR || env_hum!=AKIRA_SENSOR_ERROR
                              || env_press!=AKIRA_SENSOR_ERROR; }

/* ── Battery history (discharge sparkline) ───────────────────────────── */
#define BH_N 96
static uint8_t bh[BH_N]; static int bh_head, bh_len;
static uint32_t bh_last_tick;
static void bh_sample(int lvl){ if(lvl<0)return;
    if(g_tick-bh_last_tick < 8 && bh_len>0) return;   /* ~every 8 ticks */
    bh_last_tick=g_tick; bh[bh_head]=(uint8_t)lvl; bh_head=(bh_head+1)%BH_N; if(bh_len<BH_N)bh_len++; }

/* ── Icons ───────────────────────────────────────────────────────────── */
static void icon_battery(int x,int y,int w,int h,int pct,int charging){
    display_rect(x,y,w,h,C_BG);
    display_rect_outline(x,y,w,h,C_TEXT);
    display_rect(x+w,y+h/3,2,h/3,C_TEXT);                 /* nub */
    uint16_t bc=(pct<=15)?C_RED:(pct<=40)?C_AMBER:C_GREEN;
    int fw=(w-4)*clampi(pct,0,100)/100;
    display_rect(x+2,y+2,fw,h-4,bc);
    if(charging){                                         /* lightning bolt */
        int mx=x+w/2;
        display_line(mx+1,y+2,mx-2,y+h/2,C_BG);
        display_line(mx-2,y+h/2,mx+1,y+h/2,C_BG);
        display_line(mx+1,y+h/2,mx-2,y+h-2,C_BG);
    }
}
static void icon_signal(int x,int y,int rssi){            /* 4 bars by RSSI */
    int q = rssi>=-55?4 : rssi>=-67?3 : rssi>=-78?2 : rssi>=-90?1 : 0;
    for(int i=0;i<4;i++){ int bh2=3+i*2;
        display_rect(x+i*4, y+8-bh2, 3, bh2, (i<q)?C_GREEN:C_BAR_BG); }
}
static void icon_wifi(int x,int y,uint16_t c){
    display_circle(x+6,y+9,6,c); display_circle(x+6,y+9,3,c);
    display_circle_fill(x+6,y+9,1,c);
}
static void icon_ble(int x,int y,uint16_t c){            /* bluetooth rune */
    display_line(x+4,y+1,x+4,y+13,c);
    display_line(x+4,y+1,x+9,y+5,c); display_line(x+9,y+5,x+1,y+10,c);
    display_line(x+4,y+13,x+9,y+9,c); display_line(x+9,y+9,x+1,y+4,c);
}

/* ── Chrome ──────────────────────────────────────────────────────────── */
#define N_PAGES 4
static const char *PAGE_NAME[N_PAGES]={"DASH","RF","ENV","SYS"};
static int page;

static void draw_header(void){
    display_rect(0,0,SCR_W,15,C_HDR);
    display_text(5,3,"CYBERDECK",C_TEXT);
    /* page tabs, right-aligned */
    int tw=34, tx=SCR_W-N_PAGES*tw;
    for(int i=0;i<N_PAGES;i++){
        int x=tx+i*tw;
        if(i==page){ display_rect(x,1,tw-2,13,C_GREEN); display_text(x+4,3,PAGE_NAME[i],C_BG); }
        else display_text(x+4,3,PAGE_NAME[i],C_DIM);
    }
    /* live scan pip */
    if(ble_ok && (g_tick&2)) display_circle_fill(3+text_px("CYBERDECK")+8,7,2,C_GREEN);
}

static void scanlines(int y0,int y1){
    for(int y=y0;y<y1;y+=3) display_hline(0,y,SCR_W,C_SCAN);
}

static void tile(int x,int y,int w,int h,const char*t,uint16_t tc){
    display_rect(x,y,w,h,C_TILE);
    display_rect(x,y,w,11,C_TILE_HI);
    display_rect_outline(x,y,w,h,darken(C_GREEN,4));
    display_text(x+5,y+2,t,tc);
}
static void hbar(int x,int y,int w,int val,int mx,uint16_t col){
    display_rect(x,y,w,6,C_BAR_BG);
    int fw=mx>0?w*clampi(val,0,mx)/mx:0;
    display_rect(x,y,fw,6,col); display_hline(x,y,fw,lighten(col,6));
    display_rect_outline(x,y,w,6,C_DIM);
}

/* ── Clock block (shared) ────────────────────────────────────────────── */
static int g_24h=1;
static int draw_clock(int y, int big) {   /* returns 1 if RTC valid */
    int now=rtc_get_unix_time();
    if(now<=0){
        display_text_large(SCR_W/2-60,y,"--:--:--",C_DIM);
        display_text(SCR_W/2-40,y+26,"RTC not set",C_AMBER);
        return 0;
    }
    int Y,Mo,D,h,m,s; unix_to_cal(now,&Y,&Mo,&D,&h,&m,&s);
    int disp_h=h; const char*ap=0;
    if(!g_24h){ ap=(h<12)?"AM":"PM"; disp_h=h%12; if(disp_h==0)disp_h=12; }
    char hh[3],mm[3],ss[3],line[16];
    pad2(disp_h,hh); pad2(m,mm); pad2(s,ss);
    int blink=(s&1);
    int p=0; for(char*c=hh;*c;c++)line[p++]=*c; line[p++]=blink?' ':':';
    for(char*c=mm;*c;c++)line[p++]=*c; line[p++]=blink?' ':':';
    for(char*c=ss;*c;c++)line[p++]=*c; line[p]='\0';
    if(big){
        display_text_large(SCR_W/2-text_px(line),y,line,C_GREEN);
        if(ap) display_text(SCR_W/2+text_px(line)+4,y+6,ap,C_DIM);
    } else display_text(SCR_W/2-text_px(line)/2,y,line,C_GREEN);

    /* seconds progress bar */
    if(big) hbar(SCR_W/2-50,y+22,100,s,59,darken(C_GREEN,2));

    /* date */
    int dow=((now/86400)%7+7)%7;
    char db[40]; int q=0;
    for(const char*c=DOW[dow];*c;c++)db[q++]=*c; db[q++]=' ';
    { char t[4]; int_to_str(D,t); for(char*c=t;*c;c++)db[q++]=*c; } db[q++]=' ';
    for(const char*c=MON[Mo-1];*c;c++)db[q++]=*c; db[q++]=' ';
    { char t[6]; int_to_str(Y,t); for(char*c=t;*c;c++)db[q++]=*c; } db[q++]=' ';
    db[q++]='U'; db[q++]='T'; db[q++]='C'; db[q]='\0';
    display_text(SCR_W/2-text_px(db)/2,y+(big?32:12),db,C_DIM);
    return 1;
}

/* ── Battery read (shared) ───────────────────────────────────────────── */
static int batt_read(int*lvl,int*mv,int*ma,int*charging){
    uint8_t bs[12];
    if(power_get_battery_status(bs,sizeof(bs))!=0) return 0;
    *lvl=bs[0]; *charging=bs[1]&BATT_FLAG_CHARGING;
    *mv=bs[4]|(bs[5]<<8)|(bs[6]<<16)|((int)bs[7]<<24);
    *ma=bs[8]|(bs[9]<<8)|(bs[10]<<16)|((int)bs[11]<<24);
    return 1;
}

/* ── Page: DASH ──────────────────────────────────────────────────────── */
static void page_dash(void){
    draw_clock(24,1);

    int gy=78, tw=(SCR_W-18)/2, th=52, gx=6, gap=6;

    /* Battery tile */
    tile(gx,gy,tw,th,"POWER",C_AMBER);
    int lvl,mv,ma,chg;
    if(batt_read(&lvl,&mv,&ma,&chg)){
        bh_sample(lvl);
        icon_battery(gx+6,gy+16,34,16,lvl,chg);
        char v[6]; int_to_str(lvl,v);
        display_text(gx+46,gy+18,v,C_TEXT); display_text(gx+46+text_px(v),gy+18,"%",C_DIM);
        if(chg) display_text(gx+46,gy+30,"CHG",C_GREEN);
        char mvs[10]; int_to_str(mv,mvs);
        display_text(gx+tw-58,gy+16,mvs,C_DIM); display_text(gx+tw-58+text_px(mvs),gy+16,"mV",C_DIM);
        char mas[10]; int_to_str(ma,mas);
        display_text(gx+tw-58,gy+30,mas,C_DIM); display_text(gx+tw-58+text_px(mas),gy+30,"mA",C_DIM);
    } else display_text(gx+6,gy+24,"n/a",C_DIM);

    /* Network tile */
    int nx=gx+tw+gap;
    tile(nx,gy,tw,th,"NETWORK",C_CYAN);
    icon_wifi(nx+6,gy+16,C_CYAN);
    char ip[24];
    int ipok=(net_get_ip(ip,sizeof(ip))==0 && ip[0] && !(ip[0]=='0'&&ip[1]=='.'));
    if(ipok){ display_text(nx+24,gy+18,"LINK UP",C_GREEN); display_text(nx+6,gy+34,ip,C_TEXT); }
    else    { display_text(nx+24,gy+18,"LINK DOWN",C_DIM); display_text(nx+6,gy+34,"no IP",C_DIM); }

    /* Radio summary tile (full width) */
    int ry=gy+th+gap;
    tile(gx,ry,SCR_W-12,th,"RADIOS",C_MAG);
    icon_ble(gx+8,ry+18,C_GREEN);
    display_text(gx+24,ry+16,"BLE",C_DIM);
    char c[6]; int_to_str(ble_ok?ble_nearby():0,c);
    display_text_large(gx+24,ry+26,c,C_TEXT);
    display_text(gx+24+text_px(c)*2,ry+32,"near",C_DIM);

    icon_wifi(gx+SCR_W/2,ry+16,C_CYAN);
    display_text(gx+SCR_W/2+18,ry+16,"WIFI",C_DIM);
    if(wifi_scanning) display_text(gx+SCR_W/2+18,ry+30,"scan...",C_AMBER);
    else if(wifi_count<0) display_text(gx+SCR_W/2+18,ry+30,"press Y",C_DIM);
    else { char w[6]; int_to_str(wifi_count,w);
        display_text_large(gx+SCR_W/2+18,ry+26,w,C_TEXT);
        display_text(gx+SCR_W/2+18+text_px(w)*2,ry+32,"APs",C_DIM); }
}

/* ── Page: RADIO ─────────────────────────────────────────────────────── */
static void draw_rf_chip(int x,int y,int w,const char*name,int present,int rssi,const char*band){
    display_text(x,y,name,present?C_GREEN:C_DIM);
    display_text(x,y+11,present?"ONLINE":"absent",present?C_TEXT:C_DIM);
    if(present){
        display_text(x+w-70,y,band,C_DIM);
        char r[8]; int_to_str(rssi,r);
        display_text(x+w-70,y+11,r,C_TEXT);
        display_text(x+w-70+text_px(r),y+11,"dBm",C_DIM);
        icon_signal(x+w-24,y+3,rssi);
    }
}

static void page_radio(void){
    int gx=6, tw=(SCR_W-18)/2, top=20, th=SCR_H-top-15-34;   /* leave RF strip */

    /* BLE scope (left) */
    tile(gx,top,tw,th,"BLE SCOPE",C_GREEN);
    /* expanding "listening" ring */
    if(ble_ok){ int cx=gx+tw-16, cy=top+7; int rr=(g_tick%4)*2+1;
        display_circle(cx,cy,rr,darken(C_GREEN,3)); }
    /* list up to 7 nearby, strongest first (selection sort over indices) */
    int idx[BLE_SLOTS], m=0;
    for(int i=0;i<ble_used;i++) if(g_tick-ble_last[i]<BLE_WINDOW) idx[m++]=i;
    for(int a=0;a<m && a<7;a++){ int best=a;
        for(int b=a+1;b<m;b++) if(ble_rssi[idx[b]]>ble_rssi[idx[best]]) best=b;
        int t=idx[a]; idx[a]=idx[best]; idx[best]=t;
    }
    if(!ble_ok) display_text(gx+6,top+24,"scan n/a",C_DIM);
    else if(m==0) display_text(gx+6,top+24,"listening...",C_DIM);
    for(int a=0;a<m && a<7;a++){ int i=idx[a]; int y=top+16+a*15;
        icon_signal(gx+6,y,ble_rssi[i]);
        const char*nm=ble_name[i][0]?ble_name[i]:"(anon)";
        display_text(gx+26,y+1,nm,C_TEXT);
        char r[6]; int_to_str(ble_rssi[i],r);
        display_text(gx+tw-24,y+1,r,C_DIM);
    }

    /* Wi-Fi channels (right) */
    int wx=gx+tw+6;
    tile(wx,top,tw,th,"WIFI CH",C_CYAN);
    if(wifi_scanning){ display_text(wx+6,top+24,"scanning...",C_AMBER); }
    else if(wifi_count<0){ display_text(wx+6,top+24,"press Y",C_DIM); }
    else {
        int ch[15]={0}; int8_t chr[15];
        for(int i=0;i<15;i++) chr[i]=-100;
        for(int i=0;i<wifi_count;i++){ int c=aps[i].channel; if(c>=1&&c<=14){ ch[c]++;
            if(aps[i].rssi>chr[c]) chr[c]=aps[i].rssi; } }
        int base=top+th-14, bx=wx+8, bw=(tw-16)/13;
        for(int c=1;c<=13;c++){
            int hh2 = ch[c]? clampi((chr[c]+100)*(th-30)/60,3,th-30) : 0;
            uint16_t col = ch[c]? (chr[c]>=-60?C_GREEN:chr[c]>=-75?C_AMBER:C_RED) : C_BAR_BG;
            display_rect(bx+(c-1)*bw, base-hh2, bw-1, hh2, col);
            if((c&1)||c==11){ char n[4]; int_to_str(c,n);
                display_text(bx+(c-1)*bw, base+2, n, C_DIM); }
        }
        char t[24]="APs: "; int_to_str(wifi_count,t+5);
        display_text(wx+6,top+14,t,C_TEXT);
    }

    /* Sub-GHz RF chips strip (full width, bottom) */
    int ry=top+th+4, rh=30;
    tile(gx,ry,SCR_W-12,rh,"SUB-GHZ RADIOS",C_AMBER);
    int rssi = (rf_sel>=0) ? rf_get_rssi() : 0;
    draw_rf_chip(gx+8,       ry+13, (SCR_W-12)/2-8, "LR2021",
                 lr_present, (rf_sel==AKIRA_RF_CHIP_LR2021)?rssi:0, "LoRa");
    draw_rf_chip(gx+(SCR_W-12)/2, ry+13, (SCR_W-12)/2-8, "CC1121",
                 cc_present, (rf_sel==AKIRA_RF_CHIP_CC1121)?rssi:0, "sub-G");
}

/* ── Page: ENV (BME280 + IMU) ────────────────────────────────────────── */
static void page_env(void){
    int gx=6, tw=(SCR_W-18)/2, gy=20, th=44, gap=6;
    env_read();

    /* Temperature */
    tile(gx,gy,tw,th,"TEMP",C_AMBER);
    if(env_temp!=AKIRA_SENSOR_ERROR){
        int fr=env_temp%1000; if(fr<0)fr=-fr;
        char v[12]; int_to_str(env_temp/1000,v);
        display_text_large(gx+8,gy+16,v,C_TEXT);
        char dec[6]="."; pad2(fr/10,dec+1);
        display_text(gx+8+text_px(v)*2,gy+26,dec,C_DIM);
        display_text(gx+8+text_px(v)*2,gy+16,"C",C_DIM);
    } else display_text(gx+8,gy+22,"n/a",C_DIM);

    /* Humidity */
    int hx=gx+tw+gap;
    tile(hx,gy,tw,th,"HUMIDITY",C_CYAN);
    if(env_hum!=AKIRA_SENSOR_ERROR){
        int h=env_hum/1000;
        char v[8]; int_to_str(h,v);
        display_text_large(hx+8,gy+16,v,C_TEXT);
        display_text(hx+8+text_px(v)*2,gy+20,"%",C_DIM);
        hbar(hx+8,gy+34,tw-16,h,100,C_CYAN);
    } else display_text(hx+8,gy+22,"n/a",C_DIM);

    /* Pressure + altitude */
    int py=gy+th+gap;
    tile(gx,py,SCR_W-12,th,"PRESSURE / ALTITUDE",C_GREEN);
    if(env_press!=AKIRA_SENSOR_ERROR){
        char v[12]; int_to_str(env_press/1000,v);   /* sensor reports x1000 */
        display_text(gx+8,py+16,"P:",C_DIM);
        display_text(gx+28,py+16,v,C_TEXT); display_text(gx+28+text_px(v),py+16,"hPa",C_DIM);
    } else display_text(gx+8,py+16,"P: n/a",C_DIM);
    if(env_alt!=AKIRA_SENSOR_ERROR){
        char v[12]; int_to_str(env_alt/1000,v);
        display_text(gx+SCR_W/2,py+16,"Alt:",C_DIM);
        display_text(gx+SCR_W/2+34,py+16,v,C_TEXT);
        display_text(gx+SCR_W/2+34+text_px(v),py+16,"m",C_DIM);
    }
    if(!env_ok())
        display_text(gx+8,py+30,"BME280 not registered as a sensor",C_DIM);

    /* IMU bubble level */
    int iy=py+th+gap;
    tile(gx,iy,SCR_W-12,SCR_H-iy-15,"IMU LEVEL",C_MAG);
    int ax=sensor_read(SENSOR_CHAN_ACCEL_X);
    int ay=sensor_read(SENSOR_CHAN_ACCEL_Y);
    int az=sensor_read(SENSOR_CHAN_ACCEL_Z);
    int have=(ax!=AKIRA_SENSOR_ERROR && ay!=AKIRA_SENSOR_ERROR);
    int cx=SCR_W/2, cy=iy+34, R=24;
    display_circle(cx,cy,R,C_DIM); display_circle(cx,cy,R/2,darken(C_MAG,3));
    display_hline(cx-R,cy,2*R,darken(C_MAG,4)); display_vline(cx,cy-R,2*R,darken(C_MAG,4));
    if(have){
        int bxp=cx+clampi(-ax*R/9800,-R,R), byp=cy+clampi(ay*R/9800,-R,R);
        display_circle_fill(bxp,byp,4,C_GREEN);
        char a[12];
        int_to_str(ax,a); display_text(gx+8,iy+16,"X",C_DIM); display_text(gx+22,iy+16,a,C_TEXT);
        int_to_str(ay,a); display_text(gx+8,iy+28,"Y",C_DIM); display_text(gx+22,iy+28,a,C_TEXT);
        int_to_str(az,a); display_text(gx+8,iy+40,"Z",C_DIM); display_text(gx+22,iy+40,a,C_TEXT);
    } else display_text(gx+8,iy+20,"no IMU",C_DIM);
}

/* ── Page: SYSTEM ────────────────────────────────────────────────────── */
static const char *PMODE[5]={"ACTIVE","IDLE","LIGHT-SLP","DEEP-SLP","HIBERNATE"};

static void page_system(void){
    int gx=6, tw=(SCR_W-18)/2, gy=20, th=64, gap=6;

    /* Power detail + discharge sparkline */
    tile(gx,gy,tw,th,"POWER",C_AMBER);
    int lvl,mv,ma,chg;
    if(batt_read(&lvl,&mv,&ma,&chg)){
        bh_sample(lvl);
        icon_battery(gx+6,gy+15,30,14,lvl,chg);
        char v[6]; int_to_str(lvl,v); display_text(gx+42,gy+17,v,C_TEXT);
        display_text(gx+42+text_px(v),gy+17,chg?"% CHG":"%",chg?C_GREEN:C_DIM);
        char l[24]="V:"; int_to_str(mv,l+2); display_text(gx+6,gy+32,l,C_DIM);
        char l2[24]="I:"; int_to_str(ma,l2+2); display_text(gx+tw/2,gy+32,l2,C_DIM);
        /* sparkline */
        int spx=gx+6, spy=gy+th-6, spw=tw-12, sph=14;
        display_rect(spx,spy-sph,spw,sph,C_BG);
        for(int i=0;i<bh_len;i++){ int j=(bh_head-bh_len+i+BH_N*2)%BH_N;
            int x=spx + i*spw/(BH_N-1); int hh3=sph*bh[j]/100;
            display_vline(x,spy-hh3,hh3,darken(C_GREEN,1)); }
    } else display_text(gx+6,gy+24,"n/a",C_DIM);

    /* Power mode + uptime */
    int nx=gx+tw+gap;
    tile(nx,gy,tw,th,"SYSTEM",C_CYAN);
    int pm=power_get_mode();
    display_text(nx+6,gy+16,"mode",C_DIM);
    display_text(nx+40,gy+16,(pm>=0&&pm<5)?PMODE[pm]:"n/a",C_TEXT);
    uint32_t up=(uint32_t)rtc_get_uptime_ms()/1000;
    display_text(nx+6,gy+30,"up",C_DIM);
    char ub[32]; int p=0;
    { char t[8]; int_to_str(up/86400,t); for(char*c=t;*c;c++)ub[p++]=*c; } ub[p++]='d';
    { char t[4]; pad2((up/3600)%24,t); ub[p++]=' '; for(char*c=t;*c;c++)ub[p++]=*c; } ub[p++]='h';
    { char t[4]; pad2((up/60)%60,t); ub[p++]=' '; for(char*c=t;*c;c++)ub[p++]=*c; } ub[p++]='m';
    ub[p]='\0';
    display_text(nx+24,gy+30,ub,C_TEXT);

    /* NFC / secure element + storage strip */
    int iy=gy+th+gap;
    tile(gx,iy,SCR_W-12,SCR_H-iy-15,"NFC / SE",C_MAG);
    int fp=nfc_field_present();
    if(fp<0) display_text(gx+8,iy+18,"NFC: n/a",C_DIM);
    else if(fp>0){
        display_text(gx+8,iy+18,"FIELD DETECTED",C_GREEN);
        uint8_t uid[8];
        if(nfc_uid(uid)==0){
            char h[24]; int p2=0;
            for(int i=0;i<7;i++){ int b=uid[i];
                h[p2++]="0123456789ABCDEF"[(b>>4)&0xF];
                h[p2++]="0123456789ABCDEF"[b&0xF]; h[p2++]=':'; }
            h[p2-1]='\0';
            display_text(gx+8,iy+32,"UID",C_DIM); display_text(gx+30,iy+32,h,C_TEXT);
        }
    } else {
        display_text(gx+8,iy+18,"no tag in field",C_DIM);
    }
}

/* ── Full frame ──────────────────────────────────────────────────────── */
static void draw_hud(void){
    display_clear(C_BG);
    scanlines(16, SCR_H-14);          /* CRT lines behind content (dark areas only) */
    draw_header();
    switch(page){
    case 1: page_radio();  break;
    case 2: page_env();    break;
    case 3: page_system(); break;
    default: page_dash();  break;
    }
    display_rect(0,SCR_H-13,SCR_W,13,C_HDR);
    display_text(4,SCR_H-11,"<>page  Y:wifi  X:clr  A:12/24h  B:exit",C_DIM);
    display_flush();
}

/* ── Entry point ─────────────────────────────────────────────────────── */
int main(void){
    printf("AkiraOS Cyberdeck HUD");
    display_get_size(&SCR_W,&SCR_H);

    ble_ok=(ble_scan_start(0)==0);
    rf_probe();                        /* detect LR2021 / CC1121 once */

    int prev=(int)input_get_buttons();
    while(1){
        g_tick++;
        if(ble_ok) ble_drain();

        int held=(int)input_get_buttons();
        int edge=held&~prev; prev=held;

        if(edge&AKIRA_BTN_B){ if(ble_ok)ble_scan_stop(); app_switch("supervisor"); return 0; }
        if(edge&AKIRA_BTN_LEFT)  page=(page>0)?page-1:N_PAGES-1;
        if(edge&AKIRA_BTN_RIGHT) page=(page<N_PAGES-1)?page+1:0;
        if(edge&AKIRA_BTN_X) ble_used=0;
        if(edge&AKIRA_BTN_A) g_24h=!g_24h;
        if(edge&AKIRA_BTN_Y){
            wifi_scanning=1; draw_hud();
            if(ble_ok) ble_scan_stop();
            do_wifi_scan();
            if(ble_ok) ble_ok=(ble_scan_start(0)==0);
            wifi_scanning=0;
        }

        draw_hud();
        delay(250000);
    }
    return 0;
}
