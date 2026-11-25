/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Host stand-ins for the AkiraOS natives, to run the app natively (32-bit,
 * like the target) and check rendering and memory. Not part of the app.
 *
 * Run from the app directory (files resolve against it, like the SD sandbox):
 *   TR_FRAMES=300 TR_DUMP=60,299 TR_KEYS="100-200:4" host/out/tr_host
 *
 * Time is virtual: each frame sent to the display advances it by 34 ms, so
 * every frame is exactly one 30 Hz game tick and runs are reproducible.
 *   TR_FRAMES  exit after this many frames
 *   TR_DUMP    comma-separated frame numbers to save as host/out/frame_N.ppm
 *   TR_KEYS    "first-last:buttonmask;..." buttons held over frame ranges
 */
#include <fcntl.h>
#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SCREEN_W 320
#define SCREEN_H 240

static uint16_t screen[SCREEN_W * SCREEN_H];
static long frames, maxFrames;
static uint32_t vtime;
static size_t heapPeak;

static void trackHeap(void)
{
	struct mallinfo2 mi = mallinfo2();
	size_t used = mi.uordblks + mi.hblkhd;

	if (used > heapPeak) {
		heapPeak = used;
	}
}

static int listHas(const char *list, long n)
{
	while (list && *list) {
		if (atol(list) == n) {
			return 1;
		}
		list = strchr(list, ',');
		list = list ? list + 1 : NULL;
	}
	return 0;
}

static void dump(long n)
{
	char path[64];

	snprintf(path, sizeof(path), "host/out/frame_%ld.ppm", n);
	FILE *f = fopen(path, "wb");

	if (!f) {
		return;
	}
	fprintf(f, "P6\n%d %d\n255\n", SCREEN_W, SCREEN_H);
	for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
		uint16_t p = screen[i];
		unsigned char rgb[3] = {
			(unsigned char)((p >> 11) << 3),
			(unsigned char)(((p >> 5) & 0x3F) << 2),
			(unsigned char)((p & 0x1F) << 3),
		};
		fwrite(rgb, 1, 3, f);
	}
	fclose(f);
	printf("host: wrote %s\n", path);
}

int printf_native(const char *message)
{
	printf("%s\n", message);
	return 0;
}

int rtc_get_uptime_ms(void)
{
	return (int)vtime;
}

int input_get_buttons(void)
{
	const char *s = getenv("TR_KEYS");
	int mask = 0;

	while (s && *s) {
		long a, b;
		int m;

		if (sscanf(s, "%ld-%ld:%i", &a, &b, &m) == 3 && frames >= a && frames <= b) {
			mask |= m;
		}
		s = strchr(s, ';');
		s = s ? s + 1 : NULL;
	}
	return mask;
}

int display_raw_write(int32_t x, int32_t y, int32_t w, int32_t h,
		      const uint16_t *data, uint32_t data_size)
{
	if (x < 0 || y < 0 || x + w > SCREEN_W || y + h > SCREEN_H ||
	    data_size < (uint32_t)(w * h * 2)) {
		return -22;
	}
	for (int32_t row = 0; row < h; row++) {
		memcpy(&screen[(y + row) * SCREEN_W + x], &data[row * w], w * 2);
	}
	return 0;
}

int display_raw_write_async(int32_t x, int32_t y, int32_t w, int32_t h,
			    const uint16_t *data, uint32_t data_size)
{
	int ret = display_raw_write(x, y, w, h, data, data_size);

	trackHeap();
	if (listHas(getenv("TR_DUMP"), frames)) {
		dump(frames);
	}
	frames++;
	vtime += 34;
	if (maxFrames && frames >= maxFrames) {
		printf("host: %ld frames, heap peak %zu bytes\n", frames, heapPeak);
		exit(0);
	}
	return ret;
}

int display_raw_wait(void)
{
	return 0;
}

int fs_open(const char *path, int32_t flags)
{
	int f = (flags & 0x01) ? O_WRONLY : (flags & 0x04) ? O_RDWR : O_RDONLY;

	if (flags & 0x08) {
		f |= O_CREAT;
	}
	if (flags & 0x10) {
		f |= O_TRUNC;
	}
	int fd = open(path, f, 0644);

	trackHeap();
	return fd < 0 ? -2 : fd;
}

int fs_close(int32_t fd) { return close(fd); }
int fs_read(int32_t fd, void *buf, uint32_t len) { return (int)read(fd, buf, len); }
int fs_write(int32_t fd, const void *buf, uint32_t len) { return (int)write(fd, buf, len); }
int fs_seek(int32_t fd, int32_t offset, int32_t whence) { return (int)lseek(fd, offset, whence); }
int fs_tell(int32_t fd) { return (int)lseek(fd, 0, SEEK_CUR); }

__attribute__((constructor)) static void hostInit(void)
{
	const char *s = getenv("TR_FRAMES");

	maxFrames = s ? atol(s) : 0;
	mkdir("host/out", 0755);
}
