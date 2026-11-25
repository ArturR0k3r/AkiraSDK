/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * The AkiraOS natives this app imports (module "env"), as declared in the
 * SDK's include/akira_api.h. Every name here must be in natives.syms.
 */
#ifndef AKIRA_NATIVES_H
#define AKIRA_NATIVES_H

#include <stdint.h>

#define AKIRA_FS_O_READ   0x00
#define AKIRA_FS_O_WRITE  0x01
#define AKIRA_FS_O_APPEND 0x02
#define AKIRA_FS_O_RDWR   0x04
#define AKIRA_FS_O_CREATE 0x08
#define AKIRA_FS_O_TRUNC  0x10

#ifdef __cplusplus
extern "C" {
#endif

int printf_native(const char *message);
int rtc_get_uptime_ms(void);
int input_get_buttons(void);

int display_raw_write(int32_t x, int32_t y, int32_t w, int32_t h,
		      const uint16_t *data, uint32_t data_size);
int display_raw_write_async(int32_t x, int32_t y, int32_t w, int32_t h,
			    const uint16_t *data, uint32_t data_size);
int display_raw_wait(void);

int fs_open(const char *path, int32_t flags);
int fs_close(int32_t fd);
int fs_read(int32_t fd, void *buf, uint32_t len);
int fs_write(int32_t fd, const void *buf, uint32_t len);
int fs_seek(int32_t fd, int32_t offset, int32_t whence);
int fs_tell(int32_t fd);

#ifdef __cplusplus
}
#endif

#endif /* AKIRA_NATIVES_H */
