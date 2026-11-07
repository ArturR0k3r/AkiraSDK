/*
 * ax25.h — AX.25 UI frame encoder for AkiraOS WASM apps
 *
 * Self-contained, no standard library dependency — safe for -nostdlib WASM builds.
 * Encodes amateur radio AX.25 Unnumbered Information (UI) frames with
 * bit-stuffing and NRZI encoding for direct transmission via GFSK modem.
 *
 * Copyright (c) 2026 PenEngineering S.R.L
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AX25_H
#define AX25_H

#include <stdint.h>
#include <stddef.h>

/* ── Constants ─────────────────────────────────────────────────────────── */

#define AX25_FLAG       0x7E   /* HDLC frame delimiter */
#define AX25_CTRL_UI    0x03   /* Unnumbered Information frame */
#define AX25_PID_NOL3   0xF0   /* No layer 3 protocol */
#define AX25_MAX_INFO   256    /* Max info field length */
#define AX25_MAX_DIGI   8      /* Max digipeater addresses */

/* ── Public API ────────────────────────────────────────────────────────── */

/** @brief Build an AX.25 UI frame with HDLC framing.
 *
 *  Produces a complete frame: flag, dest/src/digi addresses, control,
 *  PID, info payload, FCS-16 CRC, closing flag.  Bit-stuffing and
 *  NRZI encoding are applied before the closing flag.
 *
 *  @param dest        Destination callsign (e.g., "NA1SS").
 *                     Padded/truncated to 6 chars.
 *  @param dest_ssid   Destination SSID (0–15).
 *  @param src         Source callsign (e.g., "MYCALL").
 *  @param src_ssid    Source SSID (0–15).
 *  @param digi        Comma-separated digipeater aliases
 *                     (e.g., "ARISS,WIDE1-1"), or NULL.
 *  @param info        Payload string (null-terminated).
 *  @param buf         Output buffer.
 *  @param buf_len     Output buffer size (≥ 340 recommended).
 *  @return            Frame length in bytes, or -1 on error.
 */
int ax25_build_ui_frame(const char *dest, uint8_t dest_ssid,
                         const char *src,  uint8_t src_ssid,
                         const char *digi,
                         const char *info,
                         uint8_t *buf, size_t buf_len);

/** @brief Validate an AX.25 callsign string.
 *  @return 0 if valid (1–6 alphanumeric chars), -1 otherwise. */
int ax25_validate_callsign(const char *call);

#endif /* AX25_H */
