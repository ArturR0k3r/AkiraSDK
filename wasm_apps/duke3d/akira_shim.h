/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Force-included into every Duke3D source file (-include akira_shim.h).
 *
 * AkiraOS runs apps without WASI, so the parts of libc that would import
 * WASI syscalls (stdio streams, POSIX fds, directories, exit) are redirected
 * here to akira_shim.c, which uses the AkiraOS natives (fs_*, printf_native).
 * Everything else (string, ctype, malloc, snprintf) comes from wasi-libc,
 * linked statically, and imports nothing.
 */
#ifndef AKIRA_SHIM_H
#define AKIRA_SHIM_H

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>

FILE *akira_fopen(const char *path, const char *mode);
int akira_fclose(FILE *f);
char *akira_fgets(char *s, int n, FILE *f);
size_t akira_fread(void *buf, size_t size, size_t n, FILE *f);
size_t akira_fwrite(const void *buf, size_t size, size_t n, FILE *f);
int akira_fseek(FILE *f, long off, int whence);
long akira_ftell(FILE *f);
int akira_fflush(FILE *f);
int akira_vfprintf(FILE *f, const char *fmt, va_list ap);
int akira_fprintf(FILE *f, const char *fmt, ...);
int akira_vprintf(const char *fmt, va_list ap);
int akira_printf(const char *fmt, ...);
int akira_puts(const char *s);
int akira_putchar(int c);
int akira_getchar(void);
int akira_remove(const char *path);
int akira_rename(const char *from, const char *to);
int akira_mkdir(const char *path, mode_t mode);
int akira_system(const char *cmd);
_Noreturn void akira_exit(int code);

int akira_open(const char *path, int flags, ...);
int akira_fstat(int fd, struct stat *st);
int akira_unlink(const char *path);
char *akira_getenv(const char *name);
time_t akira_time(time_t *t);
int akira_close(int fd);
ssize_t akira_read(int fd, void *buf, size_t n);
ssize_t akira_write(int fd, const void *buf, size_t n);
off_t akira_lseek(int fd, off_t off, int whence);
int akira_access(const char *path, int mode);
int akira_stat(const char *path, struct stat *st);
DIR *akira_opendir(const char *path);
struct dirent *akira_readdir(DIR *dir);
int akira_closedir(DIR *dir);

#define AKIRA_STDOUT ((FILE *)1)
#define AKIRA_STDERR ((FILE *)2)

#undef stdout
#undef stderr
#define stdout   AKIRA_STDOUT
#define stderr   AKIRA_STDERR
#define fopen    akira_fopen
#define fclose   akira_fclose
#define fread    akira_fread
#define fgets    akira_fgets
#define fwrite   akira_fwrite
#define fseek    akira_fseek
#define ftell    akira_ftell
#define fflush   akira_fflush
#define vfprintf akira_vfprintf
#define fprintf  akira_fprintf
#define vprintf  akira_vprintf
#define printf   akira_printf
#define puts     akira_puts
#undef putchar
#define putchar  akira_putchar
#undef getchar
#define getchar  akira_getchar
#define remove   akira_remove
#define rename   akira_rename
#define system   akira_system
#define exit     akira_exit
#define open     akira_open
#define close    akira_close
#define read     akira_read
#define write    akira_write
#define lseek    akira_lseek
#define access   akira_access
#define fstat    akira_fstat
#define unlink   akira_unlink
#define getenv   akira_getenv
#define time(t)  akira_time(t)
#define stat(p, s) akira_stat(p, s)
#define opendir  akira_opendir
#define readdir  akira_readdir
#define closedir akira_closedir

/* The game calls the DOS form, mkdir(path). */
#define mkdir(p, ...) akira_mkdir(p, 0777)

/* ESP-IDF logging, used once in menues.c. */
#define ESP_LOGV(tag, ...) ((void)0)

#endif /* AKIRA_SHIM_H */
