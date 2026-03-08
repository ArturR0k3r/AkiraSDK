/*
 * Copyright (c) 2026 AkiraOS Contributors
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * storage_test.c — comprehensive test for the AkiraOS sandboxed storage API.
 *
 * Exercises:
 *   STORAGE_O_WRITE  — create + truncate
 *   STORAGE_O_READ   — read-only, must exist
 *   STORAGE_O_APPEND — create if absent, seeks to end
 *   STORAGE_O_RDWR   — create/truncate, read + write
 *   storage_list     — enumerate sandbox root
 *   storage_delete   — remove file
 *   error cases      — missing file → negative errno, path traversal → -EACCES
 *
 * Manifest: storage_test.json
 *   "capabilities": ["storage.read", "storage.write"]
 */

#include "include/akira_api.h"

/* Pass/fail accounting */
static int s_pass = 0;
static int s_fail = 0;

#define EXPECT_GE0(expr, label)                          \
    do {                                                  \
        int _r = (expr);                                  \
        if (_r >= 0) {                                    \
            printf("  PASS  " label " = %d\n", _r);      \
            s_pass++;                                     \
        } else {                                          \
            printf("  FAIL  " label " = %d\n", _r);      \
            s_fail++;                                     \
        }                                                 \
    } while (0)

#define EXPECT_LT0(expr, label)                          \
    do {                                                  \
        int _r = (expr);                                  \
        if (_r < 0) {                                     \
            printf("  PASS  " label " = %d (expected <0)\n", _r); \
            s_pass++;                                     \
        } else {                                          \
            printf("  FAIL  " label " = %d (expected <0)\n", _r); \
            s_fail++;                                     \
        }                                                 \
    } while (0)

#define EXPECT_EQ(a, b, label)                           \
    do {                                                  \
        int _a = (a), _b = (b);                          \
        if (_a == _b) {                                   \
            printf("  PASS  " label " (%d == %d)\n", _a, _b); \
            s_pass++;                                     \
        } else {                                          \
            printf("  FAIL  " label " (%d != %d)\n", _a, _b); \
            s_fail++;                                     \
        }                                                 \
    } while (0)

/* ── helpers ──────────────────────────────────────────────────────────── */

static int write_file(const char *path, const char *data)
{
    int fd = storage_open(path, STORAGE_O_WRITE);
    if (fd < 0) {
        printf("  open(write) %s -> %d\n", path, fd);
        return fd;
    }
    int n = storage_write(fd, data, strlen(data));
    storage_close(fd);
    return n;
}

static int read_file(const char *path, char *buf, int len)
{
    int fd = storage_open(path, STORAGE_O_READ);
    if (fd < 0) {
        return fd;
    }
    int n = storage_read(fd, buf, len - 1);
    storage_close(fd);
    if (n >= 0) {
        buf[n] = '\0';
    }
    return n;
}

/* ── test cases ─────────────────────────────────────────────────────────*/

static void test_write_read(void)
{
    printf("\n[1] Write + read back\n");
    const char *msg = "Hello AkiraOS storage!";
    EXPECT_GE0(write_file("hello.txt", msg), "write hello.txt");

    char buf[64] = {0};
    int n = read_file("hello.txt", buf, sizeof(buf));
    EXPECT_GE0(n, "read hello.txt");
    EXPECT_EQ(n, (int)strlen(msg), "byte count matches");
    EXPECT_EQ(strcmp(buf, msg), 0, "content matches");
}

static void test_append(void)
{
    printf("\n[2] Append mode\n");
    /* Write first line */
    int fd = storage_open("log.txt", STORAGE_O_WRITE);
    EXPECT_GE0(fd, "open log.txt WRITE");
    if (fd >= 0) {
        storage_write(fd, "line1\n", 6);
        storage_close(fd);
    }

    /* Append second line */
    fd = storage_open("log.txt", STORAGE_O_APPEND);
    EXPECT_GE0(fd, "open log.txt APPEND");
    if (fd >= 0) {
        EXPECT_GE0(storage_write(fd, "line2\n", 6), "append write");
        storage_close(fd);
    }

    /* Read back — should be 12 bytes */
    char buf[64] = {0};
    int n = read_file("log.txt", buf, sizeof(buf));
    EXPECT_EQ(n, 12, "append total length");
    EXPECT_EQ(strcmp(buf, "line1\nline2\n"), 0, "append content");
}

static void test_rdwr(void)
{
    printf("\n[3] RDWR mode\n");
    int fd = storage_open("rw.bin", STORAGE_O_RDWR);
    EXPECT_GE0(fd, "open rw.bin RDWR");
    if (fd < 0) {
        return;
    }
    EXPECT_GE0(storage_write(fd, "ABCDE", 5), "write 5 bytes");
    storage_close(fd);

    /* Read back */
    char buf[16] = {0};
    EXPECT_EQ(read_file("rw.bin", buf, sizeof(buf)), 5, "read 5 bytes");
    EXPECT_EQ(strcmp(buf, "ABCDE"), 0, "content ABCDE");
}

static void test_list(void)
{
    printf("\n[4] Directory listing\n");
    char list[512] = {0};
    int n = storage_list("", list, sizeof(list));
    EXPECT_GE0(n, "storage_list");
    if (n > 0) {
        printf("  Sandbox contents:\n%s", list);
    }
}

static void test_delete(void)
{
    printf("\n[5] Delete\n");
    EXPECT_GE0(storage_delete("hello.txt"), "delete hello.txt");
    EXPECT_GE0(storage_delete("log.txt"),   "delete log.txt");
    EXPECT_GE0(storage_delete("rw.bin"),    "delete rw.bin");

    /* Second delete should return -ENOENT (negative = expected) */
    EXPECT_LT0(storage_delete("hello.txt"), "delete missing file");
}

static void test_errors(void)
{
    printf("\n[6] Error cases\n");

    /* Open non-existent file in read-only mode */
    EXPECT_LT0(storage_open("no_such_file.txt", STORAGE_O_READ), "open missing READ");

    /* Path traversal must be rejected */
    EXPECT_LT0(storage_open("../escape.txt", STORAGE_O_WRITE), "path traversal rejected");
    EXPECT_LT0(storage_delete("../../etc/passwd"),               "delete traversal rejected");
}

/* ── main ──────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("=== storage_test begin ===\n");

    test_write_read();
    test_append();
    test_rdwr();
    test_list();
    test_delete();
    test_errors();

    printf("\n=== storage_test done: %d passed, %d failed ===\n",
           s_pass, s_fail);
    return (s_fail == 0) ? 0 : 1;
}
