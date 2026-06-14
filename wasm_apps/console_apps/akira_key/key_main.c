/*
 * key_main.c — AkiraKey entry point + event loop
 * SPDX-License-Identifier: Apache-2.0
 */
#include "key.h"
#include "../include/akira_api.h"

static int g_timer = -1;

/* ── Auto-lock ─────────────────────────────────────────────────────────── */
static int g_last_activity_ms = 0;
static void activity(void){ if(g_timer>=0) g_last_activity_ms=timer_elapsed(g_timer); }
static int autolock_expired(void){
    if(g_locked||g_key_vault.autolock_s==0||g_timer<0) return 0;
    return (timer_elapsed(g_timer)-g_last_activity_ms)>(int)(g_key_vault.autolock_s*1000);
}

/* ── Unix time (from settings seed) ────────────────────────────────────── */
static uint64_t g_unix_base=0;
static int      g_ms_base=0;
static uint64_t get_unix(void){
    if(g_timer<0) return 0;
    int e=timer_elapsed(g_timer)-g_ms_base;
    return g_unix_base+(uint64_t)(e>0?e:0)/1000;
}

/* ── Button debounce ────────────────────────────────────────────────────── */
#define DB 4
static int cnt[7], st[7], prev[7], hold[7];
static const int PINS[7]={PIN_UP,PIN_DOWN,PIN_LEFT,PIN_RIGHT,PIN_A,PIN_B,PIN_SET};

static void btns_init(void){
    for(int i=0;i<7;i++) gpio_configure(PINS[i],GPIO_INPUT|GPIO_PULL_UP|GPIO_ACTIVE_LOW);
}
static void btns_poll(void){
    for(int i=0;i<7;i++){
        int raw=gpio_read(PINS[i]);
        prev[i]=st[i];
        if(raw) cnt[i]++; else cnt[i]=0;
        st[i]=(cnt[i]>=DB)?1:0;
        if(st[i]) hold[i]++; else hold[i]=0;
    }
}
static int rose(int i){ return st[i]&&!prev[i]; }
static int held_long(int i){ return hold[i]==125; } /* ~2.5s at 20ms */

/* ── BLE HID (optional, toggled from settings) ──────────────────────────── */
static int g_ble_ready=0;
static void ble_init_if_needed(void){
    if(!g_key_vault.ble_enabled||g_ble_ready||g_locked) return;
    if(hid_init(HID_TRANSPORT_BLE,HID_DEVICE_KEYBOARD)==0) g_ble_ready=1;
}

/* ── FIDO2 channel polling ──────────────────────────────────────────────── */
/* Poll once per loop tick for incoming FIDO/CTAP2 packets on Report ID 4.
 * A real CTAP2 implementation would process the packet here; for now we
 * just acknowledge so the host knows we're alive. */
static uint8_t g_fido_buf[64];
static void fido_poll(void){
    int n = hid_fido_recv(g_fido_buf, 64);
    if(n <= 0) return;
    /* Echo an error response (CTAP2_ERR_OPERATION_DENIED = 0x27) so the
     * browser sees a proper FIDO device rather than a silent timeout. */
    uint8_t resp[64];
    int i; for(i=0;i<64;i++) resp[i]=0;
    /* CTAP HID initialisation channel packet: preserve CID + CMD, set LEN=1, payload=error */
    resp[0]=g_fido_buf[0]; resp[1]=g_fido_buf[1]; resp[2]=g_fido_buf[2]; resp[3]=g_fido_buf[3];
    resp[4]=g_fido_buf[4]|0x80; /* CMD with INIT bit */
    resp[5]=0x00; resp[6]=0x01; /* LEN = 1 */
    resp[7]=0x27;               /* CTAP2_ERR_OPERATION_DENIED */
    hid_fido_send(resp, 64);
}

/* ── Main ───────────────────────────────────────────────────────────────── */
int main(void) {
    display_get_size(&GW,&GH);
    btns_init();

    g_timer=timer_create(); if(g_timer>=0) timer_start(g_timer);

    /* RTC seed */
    char ts[24];
    if(settings_get("key/unix",ts,sizeof(ts))==0){
        uint64_t v=0;
        for(int i=0;ts[i]>='0'&&ts[i]<='9';i++) v=v*10+(ts[i]-'0');
        if(v>1700000000ULL){ g_unix_base=v; g_ms_base=(g_timer>=0)?timer_elapsed(g_timer):0; }
    }

    /* Switch to USB HID on startup — exposes Report ID 3 (raw) and
     * Report ID 4 (FIDO/U2F) so the host enumerates us as a security key. */
    hid_init(HID_TRANSPORT_USB, HID_DEVICE_KEYBOARD);

    /* Boot splash */
    g_screen=SCR_BOOT; ui_draw(); delay(1200000);

    if(kstore_exists()) g_screen=SCR_UNLOCK;
    else{ /* First run: auto-create with PIN 000000 — user must change */
        kstore_create("000000"); g_screen=SCR_HOME;
    }
    ui_init();

    int dirty=1, last_draw_ms=0, last_tick_ms=0, last_save_ms=0;

    while(1){
        int now_ms=(g_timer>=0)?timer_elapsed(g_timer):0;

        if(autolock_expired()){ kstore_lock(); dirty=1; }
        ble_init_if_needed();

        /* Poll FIDO2 channel regardless of lock state */
        fido_poll();

        /* TOTP tick every 500ms */
        if(now_ms-last_tick_ms>=500){
            ui_tick(get_unix()); last_tick_ms=now_ms;
            if(g_screen==SCR_TOTP_VIEW) dirty=1;
        }

        /* Save unix time hourly */
        if(now_ms-last_save_ms>=60000&&!g_locked){
            char ts2[24]; uint64_t ut=get_unix();
            int idx=0; if(!ut){ts2[idx++]='0';}
            else{uint64_t n=ut;char tmp[20];int ti=0;
                 while(n){tmp[ti++]='0'+n%10;n/=10;}
                 while(ti-->0) ts2[idx++]=tmp[ti];}
            ts2[idx]='\0'; settings_set("key/unix",ts2);
            last_save_ms=now_ms;
        }

        btns_poll();

        /* Map debounced buttons to key events */
        static const int MAP[]={KEY_UP,KEY_DOWN,KEY_LEFT,KEY_RIGHT,KEY_A,KEY_B,KEY_LEFT};
        for(int i=0;i<7;i++){
            if(rose(i)){
                activity(); ui_handle_key(MAP[i],0); dirty=1;
            }
        }
        /* Long-press B = global lock */
        if(held_long(5)){ kstore_lock(); dirty=1; }

        if(dirty&&(now_ms-last_draw_ms>=50)){
            ui_draw(); last_draw_ms=now_ms; dirty=0;
        }

        if(g_dirty&&!g_locked) kstore_save();

        delay(20000);
    }

    /* Restore default HID transport when app exits */
    hid_disable();
    return 0;
}
