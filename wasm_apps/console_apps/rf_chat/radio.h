#pragma once
#include <stdint.h>

/* Chat packet framing */
#define CHAT_MAGIC_0   0xACu
#define CHAT_MAGIC_1   0xCAu
#define CHAT_MSG_MAX   64
#define CHAT_NAME_MAX  8

/* Radio chip IDs (match rf_select() firmware values) */
#define RF_CHIP_CC1121  2
#define RF_CHIP_LR2021  3

/*
 * Wire format: [0xAC][0xCA][name_len][msg_len][name...][msg...]
 */
typedef struct __attribute__((packed)) {
    uint8_t magic[2];
    uint8_t name_len;
    uint8_t msg_len;
    char    data[CHAT_NAME_MAX + CHAT_MSG_MAX];
} rf_pkt_t;

extern int rf_set_frequency(uint32_t freq_hz);
extern int rf_set_power(int8_t dbm);
extern int rf_get_rssi(void);
extern int rf_send(uint32_t payload_ptr, uint32_t len);
extern int rf_select(int chip);
extern int rf_recv_pop(uint32_t buffer_ptr, uint32_t max_len, uint32_t timeout_ms);

void    radio_select(int chip, uint32_t freq_hz);
void    radio_init(uint32_t freq_hz);
void    radio_set_freq(uint32_t freq_hz);
int     radio_send(const char *text, uint8_t len, const char *name, uint8_t name_len);
uint8_t radio_poll(rf_pkt_t *out);
int8_t  radio_rssi(void);
