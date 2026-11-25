/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Force-included into every Doom source file (-include akira_shim.h).
 *
 * AkiraOS runs apps without WASI, so the parts of libc that would import
 * WASI syscalls (stdio streams, mkdir, system, exit) are redirected here to
 * akira_shim.c, which uses the AkiraOS natives (fs_*, printf_native).
 * Everything else (string, ctype, math, malloc, snprintf, sscanf) comes from
 * wasi-libc, linked statically, and imports nothing.
 */
#ifndef AKIRA_SHIM_H
#define AKIRA_SHIM_H

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/stat.h>

FILE *akira_fopen(const char *path, const char *mode);
int akira_fclose(FILE *f);
size_t akira_fread(void *buf, size_t size, size_t n, FILE *f);
size_t akira_fwrite(const void *buf, size_t size, size_t n, FILE *f);
int akira_fseek(FILE *f, long off, int whence);
long akira_ftell(FILE *f);
int akira_fflush(FILE *f);
int akira_vfprintf(FILE *f, const char *fmt, va_list ap);
int akira_fprintf(FILE *f, const char *fmt, ...);
int akira_printf(const char *fmt, ...);
int akira_puts(const char *s);
int akira_putchar(int c);
int akira_remove(const char *path);
int akira_rename(const char *from, const char *to);
int akira_mkdir(const char *path, mode_t mode);
int akira_system(const char *cmd);
_Noreturn void akira_exit(int code);

#define AKIRA_STDOUT ((FILE *)1)
#define AKIRA_STDERR ((FILE *)2)

#undef stdout
#undef stderr
#define stdout   AKIRA_STDOUT
#define stderr   AKIRA_STDERR
#define fopen    akira_fopen
#define fclose   akira_fclose
#define fread    akira_fread
#define fwrite   akira_fwrite
#define fseek    akira_fseek
#define ftell    akira_ftell
#define fflush   akira_fflush
#define vfprintf akira_vfprintf
#define fprintf  akira_fprintf
#define printf   akira_printf
#define puts     akira_puts
#undef putchar
#define putchar  akira_putchar
#define remove   akira_remove
#define rename   akira_rename
#define mkdir    akira_mkdir
#define system   akira_system
#define exit     akira_exit

#endif /* AKIRA_SHIM_H */
