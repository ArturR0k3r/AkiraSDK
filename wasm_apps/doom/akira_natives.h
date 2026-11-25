/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The AkiraOS natives Doom uses, declared as in include/akira_api.h. That
 * header can't be used here: it defines printf/strlen/strcmp for -nostdlib
 * apps, which clash with the libc Doom links.
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

extern int printf_native(const char *message);
extern int delay(uint32_t microseconds);
extern int rtc_get_uptime_ms(void);
extern int input_get_buttons(void);

extern int display_raw_write(int32_t x, int32_t y, int32_t w, int32_t h,
			     const uint16_t *data, uint32_t data_size);
extern int display_raw_write_async(int32_t x, int32_t y, int32_t w, int32_t h,
				   const uint16_t *data, uint32_t data_size);
extern int display_raw_wait(void);

extern int fs_open(const char *path, int32_t flags);
extern int fs_close(int32_t fd);
extern int fs_read(int32_t fd, void *buf, uint32_t len);
extern int fs_write(int32_t fd, const void *buf, uint32_t len);
extern int fs_seek(int32_t fd, int32_t offset, int32_t whence);
extern int fs_tell(int32_t fd);
extern int fs_unlink(const char *path);
extern int fs_mkdir(const char *path);

#endif /* AKIRA_NATIVES_H */
