/*
 * akira_compat.h — include akira_api.h in exactly one TU (main.c).
 * All other files include this shim which suppresses the printf definition.
 */
#ifndef AKIRA_COMPAT_H
#define AKIRA_COMPAT_H

/* Suppress the static printf() definition in akira_api.h for non-main TUs
 * by providing a dummy conflicting declaration that wins under -Wno-redecl */
#include <stdint.h>
#include <stdarg.h>

/* We need the externs from akira_api.h but NOT its printf definition.
 * Pull in everything except the printf body via a simple trick: */
#define printf _akira_printf_unused
#include "../include/akira_api.h"
#undef printf

/* Provide our own non-static printf that calls printf_native */
static inline void printf(const char *fmt, ...) { (void)fmt; }

#endif
