/*
 * akira_stubs.c — Stubs for Emscripten built-ins that WAMR doesn't provide.
 *
 * WAMR issues "failed to link import" warnings for these, then calls them
 * if execution reaches them. Stubbing prevents a trap.
 *
 * Copyright (c) 2025 AkiraOS Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <stddef.h>

/* GC register scan — no-op; GC still works, may miss a few stack-root refs. */
void emscripten_scan_registers(void (*func)(void *, void *)) {
    (void)func;
}

/* Called by Emscripten's abort(). Loop rather than trap so the watchdog fires. */
void _abort_js(void) {
    while (1) {}
}

/* With SUPPORT_LONGJMP=wasm Emscripten compiles setjmp/longjmp to use WASM
 * native exceptions. _emscripten_throw_longjmp is the C entry point that
 * throws the longjmp exception object. Emscripten normally provides this in
 * libsetjmp, but --allow-undefined makes it an import. Stub it: if longjmp
 * is called without a matching setjmp in scope (shouldn't happen in normal
 * MicroPython execution) trap rather than silently return. */
#include <setjmp.h>
__attribute__((noreturn)) void _emscripten_throw_longjmp(void) {
    while (1) {}
}

/* Simple LCG — used by MicroPython's random module when no JS entropy available. */
static uint32_t _rng_state = 0xdeadbeef;
uint32_t mp_js_random_u32(void) {
    _rng_state = _rng_state * 1664525u + 1013904223u;
    return _rng_state;
}

/* Called each interpreter tick when MICROPY_VARIANT_ENABLE_JS_HOOK=1.
 * Patched to 0 in the variant, but stub anyway. */
void mp_js_hook(void) {}

/* mp_js_ticks_ms/mp_js_time_ms — MicroPython JS timing hooks.
 * rtc_get_uptime_ms / rtc_get_unix_time are env imports provided by WAMR. */
extern int rtc_get_uptime_ms(void);
extern int rtc_get_unix_time(void);
double mp_js_ticks_ms(void) { return (double)rtc_get_uptime_ms(); }
double mp_js_time_ms(void)  { return (double)rtc_get_unix_time() * 1000.0; }

/* Memory growth is disabled (-s ALLOW_MEMORY_GROWTH=0) so this should never
 * be called. Trap if it is — means we need a bigger INITIAL_MEMORY. */
int emscripten_resize_heap(size_t requested) {
    (void)requested;
    while (1) {}
}

/* Proxy stubs — JS proxy files removed from build but symbols still referenced. */
void proxy_convert_js_to_mp_obj_cside(void) {}
void proxy_convert_mp_to_js_obj_cside(void) {}
void proxy_convert_mp_to_js_exc_cside(void) {}

/* WASI syscall stubs — WAMR doesn't provide __syscall_* symbols. */
int __syscall_chdir(const char *path) { (void)path; return 0; }
int __syscall_getcwd(char *buf, size_t size) { (void)buf; (void)size; return 0; }
int __syscall_mkdirat(int dirfd, const char *path, uint32_t mode) { (void)dirfd; (void)path; (void)mode; return 0; }
int __syscall_openat(int dirfd, const char *path, int flags, uint32_t mode) { (void)dirfd; (void)path; (void)flags; (void)mode; return 0; }
int __syscall_poll(void *fds, uint32_t nfds, int timeout) { (void)fds; (void)nfds; (void)timeout; return 0; }
int __syscall_getdents64(int fd, void *buf, size_t count) { (void)fd; (void)buf; (void)count; return 0; }
int __syscall_renameat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath) { (void)olddirfd; (void)oldpath; (void)newdirfd; (void)newpath; return 0; }
int __syscall_rmdir(const char *path) { (void)path; return 0; }
int __syscall_fstat64(int fd, void *statbuf) { (void)fd; (void)statbuf; return 0; }
int __syscall_stat64(const char *path, void *statbuf) { (void)path; (void)statbuf; return 0; }
int __syscall_newfstatat(int dirfd, const char *path, void *statbuf, int flags) { (void)dirfd; (void)path; (void)statbuf; (void)flags; return 0; }
int __syscall_lstat64(const char *path, void *statbuf) { (void)path; (void)statbuf; return 0; }
int __syscall_statfs64(const char *path, void *buf) { (void)path; (void)buf; return 0; }
int __syscall_unlinkat(int dirfd, const char *path, int flags) { (void)dirfd; (void)path; (void)flags; return 0; }
