/**
 * @file save_state.h
 * @brief Pack/unpack in-progress machine state to/from a flat byte buffer.
 *
 * Only pointer-free fields are persisted; GB's only pointer field (`rom`)
 * is wired up once by gb_init() and left untouched — unlike the NES core,
 * GB has no derived pointer caches to rebuild (bank state is a plain index,
 * resolved at access time in gb_read/gb_write), so there is no resync step.
 *
 * `cart_ram` can be up to 128KB but is usually much smaller (cart_ram_size);
 * to avoid reserving a duplicate 128KB buffer it is streamed to/from storage
 * directly by the caller (main.c) rather than folded into the header here —
 * see gb_state_header_size()/gb_state_header_buf() for the fixed part.
 *
 * This module does no file I/O itself (akira_api.h may only be included
 * from main.c in this app, which already defines printf non-statically);
 * the caller drives storage_open/read/write/close.
 *
 * @license Apache-2.0
 */
#pragma once
#include "gb.h"

/** Size in bytes of the packed (non-cart_ram) header buffer. */
int gb_state_header_size(void);

/** Pointer to the shared static header buffer (valid until next call). */
void *gb_state_header_buf(void);

/** Fill the header buffer from @p gb (does not touch cart_ram). */
void gb_state_pack(const GB *gb);

/**
 * Apply the header buffer (previously filled via storage_read) onto @p gb.
 * Does not touch cart_ram — after this returns 0, gb->cart_ram_size tells
 * the caller how many cart_ram bytes to additionally read from storage
 * directly into gb->cart_ram.
 * @return 0 on success, -1 on a format/ROM mismatch.
 */
int gb_state_unpack(GB *gb);
