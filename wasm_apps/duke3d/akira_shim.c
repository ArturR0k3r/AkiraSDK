/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * stdio, POSIX file descriptors, directories and exit for Duke3D on
 * AkiraOS, over the fs_* and printf_native natives. See akira_shim.h.
 * Based on the Doom app's shim. Not force-included here: this file calls
 * the real vsnprintf from wasi-libc.
 */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>

#include "akira_natives.h"
#include "akira_shim.h" /* prototypes; its macros only rename to akira_* */

#undef stat

/* Paths are relative to the app sandbox; the game builds "./x" or "x". */
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

/* ---- POSIX file descriptors: the native fd as is ------------------------ */

int akira_open(const char *path, int flags, ...)
{
	int af;

	switch (flags & O_ACCMODE) {
	case O_RDONLY:
		af = AKIRA_FS_O_READ;
		break;
	case O_WRONLY:
		af = (flags & O_APPEND) ? AKIRA_FS_O_APPEND : AKIRA_FS_O_WRITE;
		break;
	default:
		af = AKIRA_FS_O_RDWR;
		break;
	}
	if (flags & O_CREAT) {
		af |= AKIRA_FS_O_CREATE;
	}
	if (flags & O_TRUNC) {
		af |= AKIRA_FS_O_TRUNC;
	}

	int fd = fs_open(sandbox_path(path), af);

	if (fd < 0) {
		errno = -fd;
		return -1;
	}
	return fd;
}

int akira_close(int fd)
{
	return fs_close(fd) < 0 ? -1 : 0;
}

ssize_t akira_read(int fd, void *buf, size_t n)
{
	size_t got = 0;

	while (got < n) {
		int r = fs_read(fd, (char *)buf + got, n - got);

		if (r < 0) {
			return got ? (ssize_t)got : -1;
		}
		if (r == 0) {
			break;
		}
		got += r;
	}
	return got;
}

ssize_t akira_write(int fd, const void *buf, size_t n)
{
	size_t done = 0;

	while (done < n) {
		int w = fs_write(fd, (const char *)buf + done, n - done);

		if (w <= 0) {
			return done ? (ssize_t)done : -1;
		}
		done += w;
	}
	return done;
}

off_t akira_lseek(int fd, off_t off, int whence)
{
	/* SEEK_SET/CUR/END are 0/1/2, the same as AKIRA_FS_SEEK_*. */
	if (fs_seek(fd, (int32_t)off, whence) < 0) {
		return -1;
	}
	return fs_tell(fd);
}

int akira_stat(const char *path, struct stat *st)
{
	akira_stat_t as;

	if (fs_stat(sandbox_path(path), &as) < 0) {
		errno = ENOENT;
		return -1;
	}
	memset(st, 0, sizeof(*st));
	st->st_size = as.size;
	st->st_mode = as.type == 1 ? S_IFDIR : S_IFREG;
	return 0;
}

/* No fstat native: the size is where a seek to the end lands. */
int akira_fstat(int fd, struct stat *st)
{
	int pos = fs_tell(fd);

	if (pos < 0 || fs_seek(fd, 0, SEEK_END) < 0) {
		errno = EBADF;
		return -1;
	}
	memset(st, 0, sizeof(*st));
	st->st_size = fs_tell(fd);
	st->st_mode = S_IFREG;
	fs_seek(fd, pos, SEEK_SET);
	return 0;
}

int akira_unlink(const char *path)
{
	return fs_unlink(sandbox_path(path)) < 0 ? -1 : 0;
}

char *akira_getenv(const char *name)
{
	(void)name;
	return NULL;
}

/* No wall clock: seconds since boot (used for save-game names and a
 * blinking cursor). */
time_t akira_time(time_t *t)
{
	time_t now = (uint32_t)rtc_get_uptime_ms() / 1000;

	if (t) {
		*t = now;
	}
	return now;
}

int akira_access(const char *path, int mode)
{
	struct stat st;

	(void)mode;
	return akira_stat(path, &st);
}

/* ---- directories: one fs_readdir() listing per opendir() ---------------- */

struct akira_dir {
	char names[2048];
	char *next;
	struct dirent ent; /* wasi-libc: d_name is a flexible array */
	char ent_name[256];
};

DIR *akira_opendir(const char *path)
{
	struct akira_dir *d = calloc(1, sizeof(*d));

	if (!d) {
		return NULL;
	}
	if (fs_readdir(sandbox_path(path), d->names, sizeof(d->names)) < 0) {
		free(d);
		return NULL;
	}
	d->next = d->names;
	return (DIR *)d;
}

struct dirent *akira_readdir(DIR *dir)
{
	struct akira_dir *d = (struct akira_dir *)dir;
	char *end;
	size_t len;

	if (!d || !*d->next) {
		return NULL;
	}
	end = strchr(d->next, '\n');
	len = end ? (size_t)(end - d->next) : strlen(d->next);
	if (len >= sizeof(d->ent_name)) {
		len = sizeof(d->ent_name) - 1;
	}
	memcpy(d->ent.d_name, d->next, len);
	d->ent.d_name[len] = '\0';
	d->next = end ? end + 1 : d->next + strlen(d->next);
	return &d->ent;
}

int akira_closedir(DIR *dir)
{
	free(dir);
	return 0;
}

/* ---- stdio streams over native fds -------------------------------------- */

#define MAX_FILES 4

struct akira_file {
	int fd;
	int used;
};

static struct akira_file files[MAX_FILES];

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
	ssize_t got;

	if (!af || size * n == 0) {
		return 0;
	}
	got = akira_read(af->fd, buf, size * n);
	return got < 0 ? 0 : (size_t)got / size;
}

/* Byte by byte: only used for the small duke3d.cfg. */
char *akira_fgets(char *s, int n, FILE *f)
{
	struct akira_file *af = file_of(f);
	int i = 0;

	if (!af || n <= 0) {
		return NULL;
	}
	while (i < n - 1) {
		char c;

		if (fs_read(af->fd, &c, 1) != 1) {
			break;
		}
		s[i++] = c;
		if (c == '\n') {
			break;
		}
	}
	s[i] = '\0';
	return i ? s : NULL;
}

size_t akira_fwrite(const void *buf, size_t size, size_t n, FILE *f)
{
	size_t want = size * n;

	if (f == AKIRA_STDOUT || f == AKIRA_STDERR) {
		akira_fprintf(f, "%.*s", (int)want, (const char *)buf);
		return n;
	}

	struct akira_file *af = file_of(f);
	ssize_t done;

	if (!af || want == 0) {
		return 0;
	}
	done = akira_write(af->fd, buf, want);
	return done < 0 ? 0 : (size_t)done / size;
}

int akira_fseek(FILE *f, long off, int whence)
{
	struct akira_file *af = file_of(f);

	if (!af) {
		return -1;
	}
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
	size_t len = (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1;

	if (f == AKIRA_STDOUT || f == AKIRA_STDERR) {
		out_chars(buf, len);
		return n;
	}
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

int akira_vprintf(const char *fmt, va_list ap)
{
	return akira_vfprintf(AKIRA_STDOUT, fmt, ap);
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

/* No keyboard: prompts that wait for a key get "Enter". */
int akira_getchar(void)
{
	return '\n';
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

/* config.c waits with a clock() busy loop. */
clock_t clock(void)
{
	return (clock_t)(uint32_t)rtc_get_uptime_ms() * (CLOCKS_PER_SEC / 1000);
}

int akira_system(const char *cmd)
{
	(void)cmd;
	return -1;
}

/* musl's stdio internals (pulled in by vsnprintf) still reference these WASI
 * syscalls. The game never reaches them (its I/O goes through the functions
 * above), but defining them here keeps them from becoming imports AkiraOS
 * cannot link. They fail with EBADF / ENOSYS. */
int32_t __imported_wasi_snapshot_preview1_fd_write(int32_t fd, int32_t iovs,
						   int32_t iovs_len, int32_t nwritten)
{
	(void)fd; (void)iovs; (void)iovs_len; (void)nwritten;
	return 8; /* EBADF */
}

/* localtime() reads TZ: an empty environment. */
int32_t __imported_wasi_snapshot_preview1_environ_sizes_get(int32_t count, int32_t size)
{
	*(uint32_t *)count = 0;
	*(uint32_t *)size = 0;
	return 0;
}

int32_t __imported_wasi_snapshot_preview1_environ_get(int32_t environ, int32_t buf)
{
	(void)environ; (void)buf;
	return 0;
}

void __imported_wasi_snapshot_preview1_proc_exit(int32_t code)
{
	(void)code;
	__builtin_trap();
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

/* An app cannot stop itself and the game quits from deep in its loop, so
 * end the app with a trap once the last line is logged. */
_Noreturn void akira_exit(int code)
{
	akira_printf("Duke3D exited (%d)\n", code);
	if (line_len) {
		out_chars("\n", 1);
	}
	__builtin_trap();
}
