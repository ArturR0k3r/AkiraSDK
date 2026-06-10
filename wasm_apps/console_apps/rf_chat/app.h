#pragma once
#include <stdint.h>
#include "radio.h"
#include "akira_console.h"  /* pure-macro header, safe to include in multiple TUs */

/* ── App states ─────────────────────────────────────────────────────────── */
typedef enum {
    STATE_CHAT    = 0,
    STATE_COMPOSE = 1,
    STATE_OPTIONS = 2,
} app_state_t;

/* ── Chat message ───────────────────────────────────────────────────────── */
#define MAX_MSGS    20
#define MAX_MSG_LEN 64

typedef struct {
    char    text[MAX_MSG_LEN + 1];
    uint8_t len;
    uint8_t sent;   /* 1 = sent by me, 0 = received */
    int8_t  rssi;
} chat_msg_t;

/* ── QWERTY keyboard ────────────────────────────────────────────────────── */
#define KB_ROWS      4
#define KB_ROW0_LEN  10
#define KB_ROW1_LEN  9
#define KB_ROW2_LEN  7
#define KB_ROW3_LEN  3   /* SPC=0, DEL=1, SEND=2 */

/* ── Default frequency (868.1 MHz) ─────────────────────────────────────── */
#define FREQ_DEFAULT  868100000U
#define FREQ_STEP_SM  100000U    /* +/- 100 kHz */
#define FREQ_STEP_LG  1000000U   /* +/- 1 MHz   */
#define FREQ_MIN      863000000U
#define FREQ_MAX      870000000U

/* ── Display dimensions (set once via display_get_size) ─────────────────── */
extern int32_t GW, GH;

/* ── Global state ───────────────────────────────────────────────────────── */
extern chat_msg_t  g_msgs[MAX_MSGS];
extern int         g_msg_count;
extern int         g_scroll;       /* 0 = newest at bottom */

extern char        g_compose[MAX_MSG_LEN + 1];
extern int         g_compose_len;
extern int         g_kb_row;
extern int         g_kb_col;

extern uint32_t    g_freq_hz;
extern uint32_t    g_opt_freq;   /* staging freq in options screen */
extern app_state_t g_state;
extern int8_t      g_rssi;
extern int         g_rx_active;  /* 1 = manual RX mode, 0 = idle */

/* ── Shared helpers (defined in main.c) ─────────────────────────────────── */
void msg_add(const char *text, uint8_t len, uint8_t sent, int8_t rssi);
void msg_send(void);

/* ── UI entry points (defined in ui.c) ─────────────────────────────────── */
void ui_draw(void);
