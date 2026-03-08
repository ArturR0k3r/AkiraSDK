/*
 * Copyright (c) 2025 AkiraOS Contributors
 * SPDX-License-Identifier: GPL-3.0-only
 */

/**
 * @file net_server.c
 * @brief TCP echo server demo for AkiraOS.
 *
 * Listens on port 9000, accepts up to (NET_SRV_MAX_CLIENTS) concurrent
 * connections, and echoes every received message back to the sender.  A second
 * ring-buffer pair is allocated per accepted connection.
 *
 * Build:
 *   cd wasm_sample && ./build_wasm_apps.sh net_server
 *
 * Quick test client (Linux):
 *   echo "Hello from client" | ncat 127.0.0.1 9000
 */

#include "include/akira_api.h"

/* ---- tunables ------------------------------------------------------------- */
#define SRV_PORT           9000
#define BACKLOG            4
#define NET_SRV_MAX_CLIENTS  4    /* must be <= CONFIG_AKIRA_NET_MAX_STREAMS - 1 */
#define POLL_INTERVAL_MS   10
#define EVT_BUF_SZ         4     /* [type][handle][extra_lo][extra_hi] */

/* ---- ring buffers --------------------------------------------------------- */
#define RING_SIZE  (NET_RING_HDR_SIZE + 1024)

/* Listener has no data rings (server-side, accept only) */
static uint8_t client_tx[NET_SRV_MAX_CLIENTS][RING_SIZE];
static uint8_t client_rx[NET_SRV_MAX_CLIENTS][RING_SIZE];

/* ---- client slot tracking ------------------------------------------------ */
static int client_handle[NET_SRV_MAX_CLIENTS];
static int client_count;

static void delay_ms(int ms)
{
    delay((uint32_t)ms * 1000u); /* convert ms → µs */
}

static int alloc_slot(void)
{
    for (int i = 0; i < NET_SRV_MAX_CLIENTS; i++) {
        if (client_handle[i] < 0) {
            return i;
        }
    }
    return -1;
}

static void free_slot(int slot)
{
    if (slot >= 0 && slot < NET_SRV_MAX_CLIENTS) {
        client_handle[slot] = -1;
    }
}

static int find_slot_by_handle(int h)
{
    for (int i = 0; i < NET_SRV_MAX_CLIENTS; i++) {
        if (client_handle[i] == h) {
            return i;
        }
    }
    return -1;
}

int main(void)
{
    printf("=== net_server: TCP echo server demo ===");
    printf("Listening on port %d ...", SRV_PORT);

    /* Initialise client slot table */
    for (int i = 0; i < NET_SRV_MAX_CLIENTS; i++) {
        client_handle[i] = -1;
    }

    /* 1. Open listener socket */
    int listener = net_open(NET_TYPE_TCP);
    if (listener < 0) {
        printf("net_open failed: %d", listener);
        return listener;
    }

    /* 2. Bind to local port */
    if (net_bind(listener, SRV_PORT) < 0) {
        printf("net_bind failed");
        net_close(listener);
        return -1;
    }

    /* 3. Start listening */
    if (net_listen(listener, BACKLOG) < 0) {
        printf("net_listen failed");
        net_close(listener);
        return -1;
    }

    printf("Ready. Waiting for connections...");

    /* 4. Main event loop — run until 3 clients have been served */
    int served = 0;
    uint8_t evt[EVT_BUF_SZ];

    while (served < 3) {
        int n = net_event_pop(evt, sizeof(evt));
        if (n < 2) {
            delay_ms(POLL_INTERVAL_MS);
            continue;
        }

        uint8_t type   = evt[0];
        uint8_t handle = evt[1];
        int     extra  = (int)((uint16_t)evt[2] | ((uint16_t)evt[3] << 8));

        if (type == NET_EVT_ACCEPT && handle == (uint8_t)listener) {
            /* extra holds the new client handle */
            int ch = extra;
            int slot = alloc_slot();
            if (slot < 0) {
                printf("No free client slots — closing new connection");
                net_close(ch);
                continue;
            }
            client_handle[slot] = ch;
            client_count++;
            printf("Accepted client handle=%d (slot %d)", ch, slot);

            /* Bind TX/RX rings for this client */
            if (net_tx_bind(ch, client_tx[slot], RING_SIZE) < 0 ||
                net_rx_bind(ch, client_rx[slot], RING_SIZE) < 0) {
                printf("Ring bind failed for client %d", ch);
                net_close(ch);
                free_slot(slot);
                client_count--;
            }

        } else if (type == NET_EVT_DATA_READY) {
            int slot = find_slot_by_handle((int)handle);
            if (slot < 0) {
                continue;
            }
            /* Drain all messages from RX ring and echo them back */
            for (;;) {
                uint8_t msg[512];
                int rlen = net_ring_read(client_rx[slot], RING_SIZE,
                                         msg, sizeof(msg) - 1);
                if (rlen <= 0) {
                    break;
                }
                msg[rlen] = '\0';
                printf("Client %d says: %s", (int)handle, (char *)msg);

                /* Echo back via TX ring */
                net_ring_write(client_tx[slot], RING_SIZE, msg, rlen);
                net_tx_flush((int)handle);
            }

        } else if (type == NET_EVT_DISCONNECTED || type == NET_EVT_ERROR) {
            int h = (int)handle;
            int slot = find_slot_by_handle(h);
            printf("Client %d disconnected (slot %d)", h, slot);
            net_close(h);
            free_slot(slot);
            client_count--;
            served++;
        }
    }

    printf("Served %d clients, shutting down.", served);
    net_close(listener);
    return 0;
}
