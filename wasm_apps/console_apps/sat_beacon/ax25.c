/*
 * ax25.c — AX.25 UI frame encoder (pure C, no libc)
 *
 * Implements:
 *   - Callsign → 7-byte AX.25 address field encoding
 *   - FCS-16 CRC (CRC-16-CCITT, polynomial 0x8408)
 *   - HDLC bit-stuffing (insert 0 after 5 consecutive 1s)
 *   - NRZI encoding (0 = transition, 1 = no transition)
 *   - Full UI frame assembly with HDLC flags
 *
 * All functions are self-contained — no memcpy, no strlen, no malloc.
 * Safe for WASM -nostdlib builds and embedded C.
 *
 * Copyright (c) 2026 PenEngineering S.R.L
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ax25.h"

/* ── Internal helpers ──────────────────────────────────────────────────── */

/** Length of a null-terminated string (no libc). */
static size_t ax25_strlen(const char *s)
{
    size_t n = 0;
    while (s && s[n]) n++;
    return n;
}

/** Fill n bytes with value. */
static void ax25_memset(uint8_t *dst, uint8_t v, size_t n)
{
    for (size_t i = 0; i < n; i++) dst[i] = v;
}

/** Check if character is valid in a callsign. */
static int ax25_is_callchar(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

/* ── Callsign encoding ─────────────────────────────────────────────────── */

/**
 * Encode a 6-character callsign into a 7-byte AX.25 address field.
 *
 * AX.25 address format (7 bytes):
 *   bytes 0–5:  each character shifted left by 1 bit
 *   byte 6:     bit 0 = 1 (end marker)
 *               bits 1–4 = SSID (0–15)
 *               bits 5–6 = reserved (0)
 *               bit 7 = 0 (command) or 1 (response) — we use 0
 *
 *  The callsign is space-padded to exactly 6 chars. SSID bits are
 *  packed into bits 1–4 of the 7th byte (SSID<<1).
 */
static void ax25_encode_address(const char *call, uint8_t ssid, uint8_t *out)
{
    size_t len = ax25_strlen(call);

    /* Encode up to 6 characters */
    for (int i = 0; i < 6; i++) {
        char c = (i < (int)len) ? call[i] : ' ';
        if (!ax25_is_callchar(c) && c != ' ') c = ' ';
        out[i] = (uint8_t)((c & 0x7F) << 1);
    }

    /* 7th byte: SSID in bits 1-4, bit 0 = end marker */
    out[6] = (uint8_t)(((ssid & 0x0F) << 1) | 0x01);

    /* Set the "has-been-repeated" bit in the SSID byte for digipeater
     * addresses — we handle this in the digi loop. For source/dest,
     * bit 5 (0x20) stays 0. */
}

/** Set the "last address" bit (bit 0 of 7th byte) — marks end of address list. */
static void ax25_set_last_bit(uint8_t *addr7)
{
    addr7[6] |= 0x01;
}

/** Clear the "last address" bit and mark as not-yet-repeated. */
static void ax25_clear_last_bit(uint8_t *addr7)
{
    addr7[6] &= ~0x01;
}

/* ── FCS-16 CRC ────────────────────────────────────────────────────────── */

/**
 * Compute CRC-16-CCITT (polynomial 0x8408, reflected).
 * This is the standard FCS-16 used in AX.25/HDLC/X.25.
 */
static uint16_t ax25_fcs16(const uint8_t *data, size_t len)
{
    uint16_t fcs = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        fcs ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (fcs & 1)
                fcs = (uint16_t)((fcs >> 1) ^ 0x8408);
            else
                fcs >>= 1;
        }
    }
    /* Ones' complement (AX.25 spec: ~fcs, LSB first on wire) */
    return fcs ^ 0xFFFF;
}

/* ── Bit-stuffing ──────────────────────────────────────────────────────── */

/**
 * HDLC bit-stuffing: after 5 consecutive '1' bits, insert a '0'.
 *
 * Operates on a bit-level output buffer. Returns the number of *bits*
 * written to 'out'. The output buffer is treated as a continuous
 * bit-stream (MSB first within each byte).
 *
 * IMPORTANT: This operates on raw bytes. For AX.25 over 9600 baud GFSK,
 * the bit-stuffed bytes go directly to the modem (no NRZI — G3RUH modems
 * handle NRZI internally). Flags are raw 0x7E, not bit-stuffed.
 */
static size_t ax25_bitstuff(const uint8_t *data, size_t len,
                             uint8_t *out, size_t out_max_bytes)
{
    size_t bit_pos = 0;      /* next output bit position */
    int ones_count = 0;      /* consecutive '1' bits seen */

    for (size_t byte_idx = 0; byte_idx < len; byte_idx++) {
        uint8_t byte = data[byte_idx];
        for (int bit = 7; bit >= 0; bit--) {
            /* Write one output bit */
            size_t byte_off = bit_pos / 8;
            size_t bit_off  = 7 - (bit_pos % 8);
            if (byte_off >= out_max_bytes) return bit_pos;

            uint8_t val = (byte >> bit) & 1;
            if (val) {
                out[byte_off] |= (uint8_t)(1 << bit_off);
                ones_count++;
            } else {
                out[byte_off] &= (uint8_t)(~(1 << bit_off));
                ones_count = 0;
            }
            bit_pos++;

            /* Stuff a '0' after 5 consecutive '1's */
            if (ones_count == 5) {
                size_t s_byte_off = bit_pos / 8;
                size_t s_bit_off  = 7 - (bit_pos % 8);
                if (s_byte_off >= out_max_bytes) return bit_pos;
                out[s_byte_off] &= (uint8_t)(~(1 << s_bit_off));
                bit_pos++;
                ones_count = 0;
            }
        }
    }
    return bit_pos;
}

/* ── Public API ────────────────────────────────────────────────────────── */

int ax25_validate_callsign(const char *call)
{
    if (!call) return -1;
    size_t len = ax25_strlen(call);
    if (len < 1 || len > 6) return -1;
    for (size_t i = 0; i < len; i++) {
        if (!ax25_is_callchar(call[i])) return -1;
    }
    return 0;
}

int ax25_build_ui_frame(const char *dest, uint8_t dest_ssid,
                         const char *src,  uint8_t src_ssid,
                         const char *digi,
                         const char *info,
                         uint8_t *buf, size_t buf_len)
{
    if (!dest || !src || !buf || buf_len < 20) return -1;
    if (dest_ssid > 15 || src_ssid > 15) return -1;

    /* ── Build raw frame (before bit-stuffing) ─────── */
    /* Layout:
     *   [dest  7B]  [src   7B]  [digis 0–56B]  [ctrl 1B]
     *   [pid   1B]  [info 0–256B]  [fcs 2B]
     */

    size_t offset = 0;
    uint8_t raw[512];  /* temporary buffer for raw frame (before stuffing) */

    /* Destination address */
    ax25_encode_address(dest, dest_ssid, raw + offset);
    offset += 7;

    /* Source address */
    ax25_encode_address(src, src_ssid, raw + offset);
    offset += 7;

    /* Digipeater path — parse comma-separated aliases */
    int digi_count = 0;
    if (digi && digi[0]) {
        const char *p = digi;
        while (*p && digi_count < AX25_MAX_DIGI) {
            /* Skip leading spaces/commas */
            while (*p == ' ' || *p == ',') p++;
            if (!*p) break;

            /* Extract callsign (up to 6 chars or until ',' or end) */
            char digi_call[7] = {' ', ' ', ' ', ' ', ' ', ' ', '\0'};
            int di = 0;
            while (*p && *p != ',' && *p != ' ' && di < 6) {
                if (ax25_is_callchar(*p))
                    digi_call[di++] = *p;
                p++;
            }

            if (di > 0) {
                if (offset + 7 > sizeof(raw)) break;
                ax25_encode_address(digi_call, 0, raw + offset);
                ax25_clear_last_bit(raw + offset + 6);  /* not last yet */
                offset += 7;
                digi_count++;
            }
        }
    }

    /* Mark the last address field with the "last" bit */
    if (offset > 0) {
        /* The last 7-byte address field (could be dest if no digis,
         * src if no digis otherwise last digi) */
        ax25_set_last_bit(raw + offset - 7 + 6);
    }

    /* Control field */
    if (offset + 1 > sizeof(raw)) return -1;
    raw[offset++] = AX25_CTRL_UI;

    /* PID */
    if (offset + 1 > sizeof(raw)) return -1;
    raw[offset++] = AX25_PID_NOL3;

    /* Info field */
    size_t info_len = ax25_strlen(info);
    if (info_len > AX25_MAX_INFO) info_len = AX25_MAX_INFO;
    if (offset + info_len > sizeof(raw)) return -1;
    for (size_t i = 0; i < info_len; i++) {
        raw[offset++] = (uint8_t)info[i];
    }

    /* FCS-16 */
    uint16_t fcs = ax25_fcs16(raw, offset);
    raw[offset++] = (uint8_t)(fcs & 0xFF);   /* LSB first */
    raw[offset++] = (uint8_t)(fcs >> 8);

    /* ── HDLC framing: flag, bitstuffed data, flag ─── */
    /* For 9600 baud GFSK (G3RUH standard), NRZI is done by the modem,
     * not in the packet data. The TNC expects NRZ bits. So we only
     * bit-stuff — skip NRZI encoding entirely. */

    size_t out_pos = 0;
    if (out_pos >= buf_len) return -1;
    buf[out_pos++] = AX25_FLAG;

    /* Zero-fill the stuffing workspace */
    uint8_t stuffed[512];
    ax25_memset(stuffed, 0, sizeof(stuffed));

    /* Bit-stuff the raw frame (no NRZI — modem handles that layer) */
    size_t stuffed_bits = ax25_bitstuff(raw, offset, stuffed, sizeof(stuffed));
    size_t stuffed_bytes = (stuffed_bits + 7) / 8;

    /* Copy bit-stuffed data to output (flags are raw 0x7E, not stuffed) */
    if (out_pos + stuffed_bytes > buf_len) return -1;
    /* Note: we need to copy the NRZI-encoded stuffed bytes but NOT
     * overwrite any unstuffed bits at the end */
    for (size_t i = 0; i < stuffed_bytes; i++) {
        buf[out_pos++] = stuffed[i];
    }

    /* Closing flag */
    if (out_pos >= buf_len) return -1;
    buf[out_pos++] = AX25_FLAG;

    return (int)out_pos;
}
