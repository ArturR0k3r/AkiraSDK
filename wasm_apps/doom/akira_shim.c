/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * stdio/exit for Doom on AkiraOS, over the fs_* and printf_native natives.
 * See akira_shim.h. Not force-included here: this file calls the real
 * vsnprintf from wasi-libc.
 */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>

#include "akira_natives.h"
#include "akira_shim.h" /* prototypes; its macros only rename to akira_* */

#define MAX_FILES 4

struct akira_file {
	int fd;
	int used;
};

static struct akira_file files[MAX_FILES];

/* Paths are relative to the app sandbox; Doom builds "./x" or "x". */
static const char *sandbox_path(const char *path)
{
	while (path[0] == '.' && path[1] == '/') {
		path += 2;
	}
	while (path[0] == '/') {
		path++;
	}
	return path;
}

static struct akira_file *file_of(FILE *f)
{
	struct akira_file *af = (struct akira_file *)f;

	if (af < files || af >= files + MAX_FILES || !af->used) {
		return NULL;
	}
	return af;
}

FILE *akira_fopen(const char *path, const char *mode)
{
	int flags;

	if (mode[0] == 'r') {
		flags = strchr(mode, '+') ? AKIRA_FS_O_RDWR : AKIRA_FS_O_READ;
	} else if (mode[0] == 'w') {
		flags = AKIRA_FS_O_WRITE | AKIRA_FS_O_CREATE | AKIRA_FS_O_TRUNC;
	} else if (mode[0] == 'a') {
		flags = AKIRA_FS_O_APPEND | AKIRA_FS_O_CREATE;
	} else {
		return NULL;
	}

	for (int i = 0; i < MAX_FILES; i++) {
		if (!files[i].used) {
			int fd = fs_open(sandbox_path(path), flags);

			if (fd < 0) {
				return NULL;
			}
			files[i].fd = fd;
			files[i].used = 1;
			return (FILE *)&files[i];
		}
	}
	return NULL;
}

int akira_fclose(FILE *f)
{
	struct akira_file *af = file_of(f);

	if (!af) {
		return EOF;
	}
	fs_close(af->fd);
	af->used = 0;
	return 0;
}

size_t akira_fread(void *buf, size_t size, size_t n, FILE *f)
{
	struct akira_file *af = file_of(f);
	size_t want = size * n;
	size_t got = 0;

	if (!af || want == 0) {
		return 0;
	}
	while (got < want) {
		int r = fs_read(af->fd, (char *)buf + got, want - got);

		if (r <= 0) {
			break;
		}
		got += r;
	}
	return got / size;
}

size_t akira_fwrite(const void *buf, size_t size, size_t n, FILE *f)
{
	size_t want = size * n;

	if (f == AKIRA_STDOUT || f == AKIRA_STDERR) {
		akira_fprintf(f, "%.*s", (int)want, (const char *)buf);
		return n;
	}

	struct akira_file *af = file_of(f);
	size_t done = 0;

	if (!af || want == 0) {
		return 0;
	}
	while (done < want) {
		int w = fs_write(af->fd, (const char *)buf + done, want - done);

		if (w <= 0) {
			break;
		}
		done += w;
	}
	return done / size;
}

int akira_fseek(FILE *f, long off, int whence)
{
	struct akira_file *af = file_of(f);

	if (!af) {
		return -1;
	}
	/* SEEK_SET/CUR/END are 0/1/2, the same as AKIRA_FS_SEEK_*. */
	return fs_seek(af->fd, (int32_t)off, whence) < 0 ? -1 : 0;
}

long akira_ftell(FILE *f)
{
	struct akira_file *af = file_of(f);

	return af ? fs_tell(af->fd) : -1;
}

int akira_fflush(FILE *f)
{
	(void)f;
	return 0;
}

/* Console output: printf_native() logs whole lines, so collect a line. */
static char line[160];
static size_t line_len;

static void out_chars(const char *s, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		if (s[i] == '\n' || line_len == sizeof(line) - 1) {
			line[line_len] = '\0';
			printf_native(line);
			line_len = 0;
			if (s[i] == '\n') {
				continue;
			}
		}
		line[line_len++] = s[i];
	}
}

int akira_vfprintf(FILE *f, const char *fmt, va_list ap)
{
	char buf[512];
	int n = vsnprintf(buf, sizeof(buf), fmt, ap);

	if (n < 0) {
		return n;
	}
	if (f == AKIRA_STDOUT || f == AKIRA_STDERR) {
		out_chars(buf, (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1);
		return n;
	}
	size_t len = (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1;

	return akira_fwrite(buf, 1, len, f) == len ? n : -1;
}

int akira_fprintf(FILE *f, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	int n = akira_vfprintf(f, fmt, ap);
	va_end(ap);
	return n;
}

int akira_printf(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	int n = akira_vfprintf(AKIRA_STDOUT, fmt, ap);
	va_end(ap);
	return n;
}

int akira_puts(const char *s)
{
	out_chars(s, strlen(s));
	out_chars("\n", 1);
	return 0;
}

int akira_putchar(int c)
{
	char ch = (char)c;

	out_chars(&ch, 1);
	return c;
}

int akira_remove(const char *path)
{
	return fs_unlink(sandbox_path(path)) < 0 ? -1 : 0;
}

/* No rename native: copy, then delete the source. */
int akira_rename(const char *from, const char *to)
{
	static char buf[4096];
	int in = fs_open(sandbox_path(from), AKIRA_FS_O_READ);

	if (in < 0) {
		return -1;
	}
	fs_unlink(sandbox_path(to));
	int out = fs_open(sandbox_path(to),
			  AKIRA_FS_O_WRITE | AKIRA_FS_O_CREATE | AKIRA_FS_O_TRUNC);
	int ret = 0;

	if (out < 0) {
		fs_close(in);
		return -1;
	}
	for (;;) {
		int r = fs_read(in, buf, sizeof(buf));

		if (r <= 0) {
			ret = r < 0 ? -1 : 0;
			break;
		}
		if (fs_write(out, buf, r) != r) {
			ret = -1;
			break;
		}
	}
	fs_close(out);
	fs_close(in);
	if (ret == 0) {
		fs_unlink(sandbox_path(from));
	}
	return ret;
}

int akira_mkdir(const char *path, mode_t mode)
{
	(void)mode;
	return fs_mkdir(sandbox_path(path)) < 0 ? -1 : 0;
}

int akira_system(const char *cmd)
{
	(void)cmd;
	return -1;
}

/* musl's stdio internals (pulled in by vsnprintf) still reference these WASI
 * syscalls. Doom never reaches them (its I/O goes through the functions
 * above), but defining them here keeps them from becoming imports AkiraOS
 * cannot link. They fail with EBADF / ENOSYS. */
int32_t __imported_wasi_snapshot_preview1_fd_write(int32_t fd, int32_t iovs,
						   int32_t iovs_len, int32_t nwritten)
{
	(void)fd; (void)iovs; (void)iovs_len; (void)nwritten;
	return 8; /* EBADF */
}

int32_t __imported_wasi_snapshot_preview1_fd_close(int32_t fd)
{
	(void)fd;
	return 8;
}

int32_t __imported_wasi_snapshot_preview1_fd_seek(int32_t fd, int64_t off,
						  int32_t whence, int32_t newoff)
{
	(void)fd; (void)off; (void)whence; (void)newoff;
	return 8;
}

int32_t __imported_wasi_snapshot_preview1_random_get(int32_t buf, int32_t len)
{
	(void)buf; (void)len;
	return 52; /* ENOSYS */
}

/* An app cannot stop itself and Doom quits from deep in its loop, so end
 * the app with a trap once the last line is logged. */
_Noreturn void akira_exit(int code)
{
	akira_printf("Doom exited (%d)\n", code);
	if (line_len) {
		out_chars("\n", 1);
	}
	__builtin_trap();
}
