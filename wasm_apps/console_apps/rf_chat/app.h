#pragma once
#include <stdint.h>
#include "radio.h"
#include "akira_console.h"  /* pure-macro header, safe to include in multiple TUs */

/* ── App states ─────────────────────────────────────────────────────────── */
typedef enum {
    STATE_CHAT      = 0,
    STATE_COMPOSE   = 1,
    STATE_OPTIONS   = 2,
    STATE_RADIO_SEL = 3,   /* startup radio-chip selection screen */
    STATE_NAME_EDIT = 4,   /* edit own display name via keyboard */
} app_state_t;

/* ── Chat message ───────────────────────────────────────────────────────── */
#define MAX_MSGS     20
#define MAX_MSG_LEN  64
#define MAX_NAME_LEN  8

typedef struct {
    char    text[MAX_MSG_LEN + 1];
    char    sender[MAX_NAME_LEN + 1]; /* empty for own messages */
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
extern int         g_opt_chip;   /* staging chip in options screen */
extern app_state_t g_state;
extern int8_t      g_rssi;
extern int         g_rf_chip;      /* RF_CHIP_CC1121=2  RF_CHIP_LR2021=3 */
extern int         g_radio_cursor; /* selection cursor: 0=CC1121 1=LR2021 */
extern int         g_opt_cursor;   /* settings row: 0=freq 1=chip 2=name */
extern char        g_my_name[MAX_NAME_LEN + 1];
extern int         g_my_name_len;

/* ── Shared helpers (defined in main.c) ─────────────────────────────────── */
void msg_add(const char *text, uint8_t len, uint8_t sent, int8_t rssi, const char *sender, uint8_t sender_len);
void msg_send(void);

/* ── UI entry points (defined in ui.c) ─────────────────────────────────── */
void ui_draw(void);
