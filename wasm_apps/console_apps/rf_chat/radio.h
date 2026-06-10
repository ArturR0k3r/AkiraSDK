#pragma once
#include <stdint.h>

/* Chat packet framing */
#define CHAT_MAGIC_0  0xACu
#define CHAT_MAGIC_1  0xCAu
#define CHAT_MSG_MAX  64

typedef struct __attribute__((packed)) {
    uint8_t magic[2];
    uint8_t len;
    char    text[CHAT_MSG_MAX];
} rf_pkt_t;

/*
 * rf_* WASM import declarations.
 * Declared here so radio.c can use them without including akira_api.h
 * (akira_api.h defines printf as a non-static TU-global, which causes
 *  duplicate-symbol link errors when included in multiple translation units).
 */
extern int rf_set_frequency(uint32_t freq_hz);
extern int rf_set_power(int8_t dbm);
extern int rf_get_rssi(void);
extern int rf_send(uint32_t payload_ptr, uint32_t len);
extern int rf_receive(uint32_t buffer_ptr, uint32_t max_len, uint32_t timeout_ms);

void    radio_init(uint32_t freq_hz);
void    radio_set_freq(uint32_t freq_hz);
int     radio_send(const char *text, uint8_t len);
uint8_t radio_poll(rf_pkt_t *out);
int8_t  radio_rssi(void);
