/*
 * Copyright (c) 2025 AkiraOS Contributors
 * SPDX-License-Identifier: GPL-3.0-only
 */

/**
 * @file net_echo.c
 * @brief TCP echo client demo for AkiraOS.
 *
 * Connects to an echo server (default: 127.0.0.1:7), sends "Hello AkiraOS!",
 * reads the echoed reply, then closes. Uses shared-memory ring buffers so only
 * net_tx_flush() and net_event_pop() require host calls during data transfer.
 *
 * Build:
 *   cd wasm_sample && ./build_wasm_apps.sh net_echo
 *
 * Quick test server (Linux):
 *   ncat -l -p 7 --keep-open --exec "/bin/cat"
 *
 * Or with socat:
 *   socat TCP-LISTEN:7,fork EXEC:/bin/cat
 */

#include "akira_api.h"

/* ---- tunables ------------------------------------------------------------- */
#define ECHO_HOST    "127.0.0.1"
#define ECHO_PORT    7
#define MSG          "Hello AkiraOS! (echo test)"
#define POLL_TRIES   200   /* × 10 ms = ~2 s max wait             */
#define EVT_BUF_SZ   4     /* [type][handle][extra_lo][extra_hi]  */

/* ---- shared-memory ring buffers live in WASM linear memory --------------- */
#define RING_SIZE    (NET_RING_HDR_SIZE + 512)
static uint8_t tx_ring[RING_SIZE];
static uint8_t rx_ring[RING_SIZE];

static void delay_ms(int ms)
{
    /* Busy-wait — acceptable in a short demo */
    for (volatile int i = 0; i < ms * 2000; i++) {
        ;
    }
}

int main(void)
{
    printf("=== net_echo: TCP client demo ===");

    /* 1. Open a TCP socket */
    int h = net_open(NET_TYPE_TCP);
    if (h < 0) {
        return h;
    }

    /* 2. Bind TX / RX rings */
    if (net_tx_bind(h, tx_ring, sizeof(tx_ring)) < 0 ||
        net_rx_bind(h, rx_ring, sizeof(rx_ring)) < 0) {
        printf("Ring bind failed");
        net_close(h);
        return -1;
    }

    /* 3. Connect (async — DNS + connect on host background thread) */
    printf("Connecting to " );
    printf(ECHO_HOST);
    printf( ": ...");
    printf("ECHO_PORT");
    if (net_connect(h, ECHO_HOST, ECHO_PORT) < 0)
    {
        printf("net_connect failed");
        net_close(h);
        return -1;
    }

    /* 4. Poll for NET_EVT_CONNECTED */
    uint8_t evt[EVT_BUF_SZ];
    int connected = 0;
    for (int i = 0; i < POLL_TRIES; i++) {
        int n = net_event_pop(evt, sizeof(evt));
        if (n >= 2 && evt[1] == (uint8_t)h) {
            if (evt[0] == NET_EVT_CONNECTED) {
                connected = 1;
                break;
            }
            if (evt[0] == NET_EVT_ERROR) {
                printf("Connection error (errno=)");
                net_close(h);
                return -1;
            }
        }
        delay_ms(10);
    }

    if (!connected) {
        printf("Timeout waiting for connection");
        net_close(h);
        return -1;
    }
    printf("Connected!");

    /* 5. Write message into TX ring and flush */
    int ret = net_ring_write(tx_ring, sizeof(tx_ring),
                             (const uint8_t *)MSG, 27);
    if (ret < 0) {
        printf("TX ring write failed (ring full?)");
        net_close(h);
        return -1;
    }
    net_tx_flush(h);
    printf("Sent: ");
    printf(MSG);

    /* 6. Poll for NET_EVT_DATA_READY then read from RX ring */
    for (int i = 0; i < POLL_TRIES; i++) {
        int n = net_event_pop(evt, sizeof(evt));
        if (n >= 2 && evt[1] == (uint8_t)h) {
            if (evt[0] == NET_EVT_DATA_READY) {
                uint8_t reply[256];
                int rlen = net_ring_read(rx_ring, sizeof(rx_ring),
                                         reply, sizeof(reply) - 1);
                if (rlen > 0) {
                    reply[rlen] = '\0';
                    printf("Echoed:");
                }
                break;
            }
            if (evt[0] == NET_EVT_DISCONNECTED || evt[0] == NET_EVT_ERROR) {
                printf("Connection closed before echo");
                break;
            }
        }
        delay_ms(10);
    }

    /* 7. Clean up */
    net_close(h);
    printf("Done.");
    return 0;
}
