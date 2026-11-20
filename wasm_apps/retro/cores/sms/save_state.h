/**
 * @file save_state.h
 * @brief Pack/unpack in-progress machine state to/from a flat byte buffer.
 *
 * Only pointer-free fields are persisted (Z80/VDP registers, RAM, mapper
 * bank-select state). Pointer fields (Mapper.rom/pages, VDP.framebuf) are
 * wired up once by sms_init() and are left untouched; sms_state_unpack()
 * must be called on an already-initialised SMS (same ROM already loaded),
 * and it calls mapper_resync() to rebuild the cached page table afterward.
 *
 * This module does no file I/O itself (akira_api.h may only be included
 * from main.c in this app, which already defines printf non-statically);
 * the caller drives storage_open/read/write/close.
 *
 * @license Apache-2.0
 */
#pragma once
#include "sms.h"

/** Size in bytes of the packed state buffer. */
int sms_state_size(void);

/** Pointer to the shared static packing buffer (valid until next call). */
void *sms_state_buf(void);

/** Fill the buffer (sms_state_buf(), sms_state_size() bytes) from @p sms. */
void sms_state_pack(const SMS *sms);

/**
 * Apply the buffer (previously filled via storage_read) onto @p sms.
 * @return 0 on success, -1 on a format/ROM mismatch.
 */
int sms_state_unpack(SMS *sms);
