/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Host stand-ins for the AkiraOS natives, to run the app natively (32-bit,
 * like the target) and check rendering and memory. Not part of the app.
 * Based on the Tomb Raider app's host build.
 *
 * Run from the app directory; the sandbox (the SD app directory) is data/:
 *   DK_FRAMES=600 DK_DUMP=100,599 DK_KEYS="200-260:64" host/out/duke3d_host
 *
 * Time is virtual: each frame sent to the display advances it by 33 ms and
 * each clock read by 1 ms, so runs are reproducible.
 *   DK_FRAMES  exit after this many frames
 *   DK_DUMP    comma-separated frame numbers to save as host/out/frame_N.ppm
 *   DK_KEYS    "first-last:buttonmask;..." buttons held over frame ranges
 */
#include <dirent.h>
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
#define SANDBOX  "data/"

static uint16_t screen[SCREEN_W * SCREEN_H];
static long frames, maxFrames;
static uint32_t vtime;
static size_t heapPeak;
static unsigned long long bytesRead;

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

static const char *in_sandbox(const char *path)
{
	static char buf[512];

	snprintf(buf, sizeof(buf), SANDBOX "%s", path);
	return buf;
}

int printf_native(const char *message)
{
	printf("%s\n", message);
	return 0;
}

/* Each read advances time by 1 ms, so busy-wait loops on the clock end. */
int rtc_get_uptime_ms(void)
{
	return (int)vtime++;
}

int delay(uint32_t us)
{
	vtime += us / 1000;
	return 0;
}

int input_get_buttons(void)
{
	const char *s = getenv("DK_KEYS");
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
	if (listHas(getenv("DK_DUMP"), frames)) {
		dump(frames);
	}
	frames++;
	vtime += 33;
	if (maxFrames && frames >= maxFrames) {
		printf("host: %ld frames, heap peak %zu bytes, %llu bytes read\n",
		       frames, heapPeak, bytesRead);
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

	if (flags & 0x02) {
		f = O_WRONLY | O_APPEND;
	}
	if (flags & 0x08) {
		f |= O_CREAT;
	}
	if (flags & 0x10) {
		f |= O_TRUNC;
	}
	int fd = open(in_sandbox(path), f, 0644);

	trackHeap();
	return fd < 0 ? -2 : fd;
}

int fs_close(int32_t fd) { return close(fd); }
int fs_read(int32_t fd, void *buf, uint32_t len)
{
	int n = (int)read(fd, buf, len);

	if (n > 0) {
		bytesRead += n;
	}
	return n;
}
int fs_write(int32_t fd, const void *buf, uint32_t len) { return (int)write(fd, buf, len); }
int fs_seek(int32_t fd, int32_t offset, int32_t whence)
{
	return lseek(fd, offset, whence) < 0 ? -22 : 0;
}
int fs_tell(int32_t fd) { return (int)lseek(fd, 0, SEEK_CUR); }
int fs_unlink(const char *path) { return unlink(in_sandbox(path)) ? -2 : 0; }
int fs_mkdir(const char *path) { return mkdir(in_sandbox(path), 0755) ? -17 : 0; }

int fs_stat(const char *path, void *out)
{
	struct __attribute__((packed)) {
		uint32_t size;
		uint8_t type, pad[3];
		uint64_t mtime;
	} *as = out;
	struct stat st;

	if (stat(in_sandbox(path), &st)) {
		return -2;
	}
	memset(as, 0, sizeof(*as));
	as->size = (uint32_t)st.st_size;
	as->type = S_ISDIR(st.st_mode);
	return 0;
}

int fs_readdir(const char *path, char *out, uint32_t len)
{
	DIR *d = opendir(in_sandbox(path));
	struct dirent *e;
	uint32_t used = 0;
	int n = 0;

	if (!d) {
		return -2;
	}
	out[0] = '\0';
	while ((e = readdir(d))) {
		size_t l = strlen(e->d_name);

		if (e->d_name[0] == '.' || used + l + 2 > len) {
			continue;
		}
		memcpy(out + used, e->d_name, l);
		used += l;
		out[used++] = '\n';
		out[used] = '\0';
		n++;
	}
	closedir(d);
	return n;
}

__attribute__((constructor)) static void hostInit(void)
{
	const char *s = getenv("DK_FRAMES");

	maxFrames = s ? atol(s) : 0;
	setvbuf(stdout, NULL, _IONBF, 0);
	mkdir("host/out", 0755);
}
