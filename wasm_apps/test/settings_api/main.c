/**
 * @file main.c
 * @brief AkiraOS settings API test — system/x namespace
 *
 * Tests that an app with "settings.*" capability can set, get, and delete
 * keys in the "system/" namespace.
 *
 * Result codes:
 *   [PASS] – returned expected value
 *   [FAIL] – unexpected return value
 *   [INFO] – informational, not judged
 *
 * Build:
 *   make -C wasm_apps/test/settings_api
 *
 * @copyright Copyright (c) 2026 PenEngineering S.R.L
 * @license Apache-2.0
 */

#include "akira_api.h"

static int str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

/* ── result counters ──────────────────────────────────────────────────────── */

static int g_pass;
static int g_fail;

/* ── helpers ──────────────────────────────────────────────────────────────── */

static void chk(const char *label, int ret, int min_ok)
{
    if (ret >= min_ok) {
        printf("[PASS] %s = %d", label, ret);
        g_pass++;
    } else {
        printf("[FAIL] %s = %d", label, ret);
        g_fail++;
    }
}

static void section(const char *name)
{
    printf("");
    printf("--- %s ---", name);
}

static char s_buf[256];

/*
 * set → get → compare → delete for one key.
 * Returns 1 if the full round-trip matched, 0 otherwise.
 */
static int roundtrip(const char *key, const char *value)
{
    int rc;

    rc = settings_set(key, value);
    if (rc < 0) {
        printf("[FAIL] settings_set(\"%s\") = %d", key, rc);
        g_fail++;
        return 0;
    }
    printf("[PASS] settings_set(\"%s\") = %d", key, rc);
    g_pass++;

    s_buf[0] = '\0';
    rc = settings_get(key, s_buf, sizeof(s_buf));
    if (rc < 0) {
        printf("[FAIL] settings_get(\"%s\") = %d", key, rc);
        g_fail++;
        settings_delete(key);
        return 0;
    }
    printf("[PASS] settings_get(\"%s\") = %d  value=\"%s\"", key, rc, s_buf);
    g_pass++;

    if (!str_eq(s_buf, value)) {
        printf("[FAIL] value mismatch for \"%s\": expected \"%s\" got \"%s\"",
               key, value, s_buf);
        g_fail++;
        settings_delete(key);
        return 0;
    }
    printf("[PASS] value match for \"%s\"", key);
    g_pass++;

    rc = settings_delete(key);
    if (rc < 0) {
        printf("[FAIL] settings_delete(\"%s\") = %d", key, rc);
        g_fail++;
        return 0;
    }
    printf("[PASS] settings_delete(\"%s\") = %d", key, rc);
    g_pass++;

    return 1;
}

/* ── test: baseline non-system key ───────────────────────────────────────── */

static void test_baseline(void)
{
    section("SETTINGS BASELINE (non-system namespace)");
    roundtrip("test/settings_api", "baseline_ok");
}

/* ── test: system/* keys ─────────────────────────────────────────────────── */

static void test_system_settings(void)
{
    section("SETTINGS system/wifi/ssid");
    roundtrip("system/wifi/ssid", "MyNetwork");

    section("SETTINGS system/wifi/psk");
    roundtrip("system/wifi/psk", "s3cr3tpassword");

    section("SETTINGS system/time_base");
    /* "ntp" | "rtc" | "manual" — store as string */
    roundtrip("system/time_base", "ntp");

    section("SETTINGS system/hostname");
    roundtrip("system/hostname", "akira-device");

    section("SETTINGS system/timezone");
    roundtrip("system/timezone", "UTC+2");

    section("SETTINGS system/ota/channel");
    roundtrip("system/ota/channel", "stable");

    section("SETTINGS system/locale");
    roundtrip("system/locale", "en_US");
}

/* ── test: overwrite existing system key ─────────────────────────────────── */

static void test_system_overwrite(void)
{
    section("SETTINGS system/* overwrite");

    /* set initial value */
    chk("settings_set(system/wifi/ssid, v1)",
        settings_set("system/wifi/ssid", "net_v1"), 0);

    /* overwrite */
    chk("settings_set(system/wifi/ssid, v2)",
        settings_set("system/wifi/ssid", "net_v2"), 0);

    s_buf[0] = '\0';
    chk("settings_get after overwrite",
        settings_get("system/wifi/ssid", s_buf, sizeof(s_buf)), 0);
    printf("[INFO] value after overwrite: \"%s\"", s_buf);

    if (str_eq(s_buf, "net_v2")) {
        printf("[PASS] overwrite value correct");
        g_pass++;
    } else {
        printf("[FAIL] overwrite value wrong: got \"%s\"", s_buf);
        g_fail++;
    }

    settings_delete("system/wifi/ssid");
}

/* ── test: get nonexistent system key → -ENOENT ──────────────────────────── */

static void test_system_missing(void)
{
    section("SETTINGS system/* missing key");

    s_buf[0] = '\0';
    int rc = settings_get("system/wifi/ssid_nonexistent", s_buf, sizeof(s_buf));
    if (rc == -2 /* -ENOENT */ || rc < 0) {
        printf("[PASS] settings_get(missing) = %d (negative = not found)", rc);
        g_pass++;
    } else {
        printf("[FAIL] settings_get(missing) = %d (expected negative errno)", rc);
        g_fail++;
    }
}

/* ── entry point ──────────────────────────────────────────────────────────── */

__attribute__((export_name("main")))
int main(void)
{
    printf("=== settings_api test: system/* namespace ===");

    test_baseline();
    test_system_settings();
    test_system_overwrite();
    test_system_missing();

    printf("");
    printf("=== RESULT: %d passed, %d failed ===", g_pass, g_fail);

    return g_fail == 0 ? 0 : 1;
}
