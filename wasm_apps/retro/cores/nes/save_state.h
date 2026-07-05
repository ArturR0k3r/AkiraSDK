/**
 * @file save_state.h
 * @brief Pack/unpack in-progress machine state to/from a flat byte buffer.
 *
 * Only pointer-free fields are persisted (CPU/PPU registers, RAM, mapper
 * bank-select state). Pointer fields (ROM/CHR base pointers, cached
 * prg_page/chr_page tables, the PPU's mapper/fb back-references) are wired
 * up once by nes_init() and are left untouched; nes_state_unpack() must be
 * called on an already-initialised NES (same ROM already loaded), and it
 * calls mapper_resync() to rebuild the cached page tables afterward.
 *
 * This module does no file I/O itself (akira_api.h may only be included
 * from main.c in this app — see nes.c's own note on the same constraint);
 * the caller is responsible for storage_open/read/write/close using the
 * buffer returned by nes_state_buf().
 *
 * @license Apache-2.0
 */
#pragma once
#include "nes.h"

/** Size in bytes of the packed state buffer. */
int nes_state_size(void);

/** Pointer to the shared static packing buffer (valid until next call). */
void *nes_state_buf(void);

/** Fill the buffer (nes_state_buf(), nes_state_size() bytes) from @p nes. */
void nes_state_pack(const NES *nes);

/**
 * Apply the buffer (previously filled via storage_read) onto @p nes.
 * @return 0 on success, -1 on a format/ROM mismatch (e.g. the buffer was
 *         saved by a different ROM or an incompatible build).
 */
int nes_state_unpack(NES *nes);
