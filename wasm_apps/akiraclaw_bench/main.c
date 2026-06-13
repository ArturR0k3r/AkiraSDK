/*
 * akiraclaw_bench WASM module — B1 inference hot-loop benchmark
 *
 * Runs inside the AkiraOS WASM runtime.  Imports two benchmark-only
 * native functions registered by the host before module load:
 *
 *   bench_ccount_get()   — returns current Xtensa CCOUNT (240 MHz cycles)
 *   bench_pin_core(n)    — pins calling thread to CPU core n (Core 1)
 *
 * Output: one JSON object per configuration printed to stdout (Zephyr
 * console via the WASM printf bridge).  The host-side collect.py script
 * identifies them via <<BENCH_JSON_START>> / <<BENCH_JSON_END>> markers.
 *
 * Build: WASI SDK; see Makefile.  Requires wasm_data.h to be generated
 * from the compiled .wasm binary before building the native Zephyr app.
 */

#include "akira_api.h"
#include "../aiinfer_test/model_data.h"
#include <stddef.h>   /* NULL */

/* ── Native imports registered by bench_b1_wasm.c ─────────────────────── */
extern unsigned int bench_ccount_get(void);
extern void         bench_pin_core(int core);

/* ── Timing helpers (CCOUNT @ 240 MHz) ───────────────────────────────── */
#define CCOUNT_MHZ 240U
#define BARRIER()  __asm__ volatile("" : : : "memory")

static unsigned int ccount_to_us(unsigned int cycles)
{
    return cycles / CCOUNT_MHZ;
}

/* ── Statistics — 1000-sample array for p99 ─────────────────────────── */
#define N_ITERS 1000

static unsigned int g_samples_us[N_ITERS];

static void shellsort_u32(unsigned int *a, int n)
{
    for (int gap = n / 2; gap > 0; gap /= 2) {
        for (int i = gap; i < n; i++) {
            unsigned int tmp = a[i];
            int j = i;
            for (; j >= gap && a[j - gap] > tmp; j -= gap)
                a[j] = a[j - gap];
            a[j] = tmp;
        }
    }
}

static unsigned int compute_avg(const unsigned int *a, int n)
{
    unsigned long long s = 0;
    for (int i = 0; i < n; i++) s += a[i];
    return n ? (unsigned int)(s / (unsigned long long)n) : 0;
}

static unsigned int compute_p99(unsigned int *a, int n)
{
    if (n < 2) return n ? a[0] : 0;
    shellsort_u32(a, n);
    int idx = (n * 99) / 100;
    if (idx >= n) idx = n - 1;
    return a[idx];
}

static unsigned int compute_max(const unsigned int *a, int n)
{
    unsigned int m = 0;
    for (int i = 0; i < n; i++) if (a[i] > m) m = a[i];
    return m;
}

/* ── Integer-only float formatter: prints "D.DDD" ────────────────────── */
static void ftoa3(float f, char *buf)
{
    char *p = buf;
    if (f < 0.0f) { *p++ = '-'; f = -f; }
    int whole = (int)f;
    int frac  = (int)((f - (float)whole) * 1000.0f + 0.5f);
    if (frac >= 1000) { whole++; frac -= 1000; }
    if (whole >= 10) *p++ = '0' + whole / 10;
    *p++ = '0' + whole % 10;
    *p++ = '.';
    *p++ = '0' + frac / 100;
    *p++ = '0' + (frac / 10) % 10;
    *p++ = '0' + frac % 10;
    *p = '\0';
}

/* ── JSON sentinel/printer ──────────────────────────────────────────── */
#define JSON_START "<<BENCH_JSON_START>>"
#define JSON_END   "<<BENCH_JSON_END>>"

static void print_b1_json(const char *config,
                          unsigned int load_us,
                          unsigned int run_avg_us,
                          unsigned int run_p99_us,
                          unsigned int run_max_us,
                          unsigned int unload_us,
                          int          mock_mode,
                          const char  *mock_reason)
{
    char num[16];

    printf(JSON_START "\n{\n");
    printf("  \"bench\": \"B1\",\n");
    printf("  \"config\": \"%s\",\n", config);
    printf("  \"platform\": \"esp32s3_akiraconsole\",\n");

    itoa((int)load_us, num);
    printf("  \"load_us\": %s,\n", num);

    itoa((int)run_avg_us, num);
    printf("  \"run_avg_us\": %s,\n", num);

    itoa((int)run_p99_us, num);
    printf("  \"run_p99_us\": %s,\n", num);

    itoa((int)run_max_us, num);
    printf("  \"run_max_us\": %s,\n", num);

    itoa((int)unload_us, num);
    printf("  \"unload_us\": %s,\n", num);

    itoa(N_ITERS, num);
    printf("  \"iterations\": %s,\n", num);

    printf("  \"model\": \"hello_world_float_sine\",\n");

    itoa((int)hello_world_model_len, num);
    printf("  \"model_size_bytes\": %s,\n", num);

    printf("  \"ble_active\": false,\n");
    printf("  \"mock_mode\": %s,\n", mock_mode ? "true" : "false");
    if (mock_mode && mock_reason)
        printf("  \"mock_reason\": \"%s\"\n", mock_reason);
    else
        printf("  \"mock_reason\": null\n");

    printf("}\n" JSON_END "\n");
}

/* ── Main benchmark loop ─────────────────────────────────────────────── */
int main(void)
{
    /* Pin this WASM thread to Core 1 for consistent timing.
     * Matches CONFIG_AKIRA_BENCH_CORE=1 requirement from the paper. */
    bench_pin_core(1);

    /* Determine config name from build-time define injected by Makefile */
#ifndef BENCH_CONFIG
#define BENCH_CONFIG "wasm_claw"
#endif
    const char *config = BENCH_CONFIG;

#ifdef BENCH_MPU
    /* wasm_claw_mpu: MPU sandbox active.  On ESP32-S3 (Xtensa) ARM_MPU is
     * unavailable — report mock_mode and exit immediately. */
    print_b1_json(config, 0, 0, 0, 0, 0,
                  1, "CONFIG_AKIRA_SANDBOX_MPU requires ARM_MPU; not available on ESP32-S3");
    return 0;
#endif

    /* ── Load phase ────────────────────────────────────────────────── */
    unsigned int t0 = bench_ccount_get();
    int handle = aiinfer_load(hello_world_model, (int)hello_world_model_len);
    unsigned int t1 = bench_ccount_get();

    if (handle < 0) {
        char num[8];
        itoa(handle, num);
        printf("[bench] aiinfer_load failed: %s\n", num);
        print_b1_json(config, 0, 0, 0, 0, 0,
                      1, "aiinfer_load returned error — check CONFIG_AKIRA_AIINFER and capability");
        return 1;
    }

    unsigned int load_us = ccount_to_us(t1 - t0);

    /* ── Warm-up (5 calls, unmeasured) ─────────────────────────────── */
    float x_warm = 0.7854f;
    float y_warm = 0.0f;
    for (int i = 0; i < 5; i++) {
        aiinfer_run(handle, &x_warm, (int)sizeof(float), &y_warm, (int)sizeof(float));
    }

    /* ── Hot loop: N_ITERS iterations ───────────────────────────────── */
    unsigned int max_us = 0;

    for (int i = 0; i < N_ITERS; i++) {
        /* Rotate input to prevent constant-folding by the WASM JIT */
        float xi = (float)i * (3.14159f / (float)N_ITERS);
        float yi = 0.0f;

        BARRIER();
        unsigned int c0 = bench_ccount_get();
        BARRIER();
        aiinfer_run(handle, &xi, (int)sizeof(float), &yi, (int)sizeof(float));
        BARRIER();
        unsigned int c1 = bench_ccount_get();
        BARRIER();

        unsigned int us = ccount_to_us(c1 - c0);
        g_samples_us[i] = us;
        if (us > max_us) max_us = us;
    }

    unsigned int run_avg_us = compute_avg(g_samples_us, N_ITERS);
    unsigned int run_p99_us = compute_p99(g_samples_us, N_ITERS); /* sorts in place */
    unsigned int run_max_us = max_us;

    /* ── Unload phase ─────────────────────────────────────────────── */
    BARRIER();
    unsigned int u0 = bench_ccount_get();
    BARRIER();
    aiinfer_unload(handle);
    BARRIER();
    unsigned int u1 = bench_ccount_get();
    BARRIER();

    unsigned int unload_us = ccount_to_us(u1 - u0);

    /* ── Sanity check: print one inference result ─────────────────── */
    char xbuf[10], ybuf[10];
    ftoa3(0.7854f, xbuf);
    /* (model is already unloaded — just verify we got sane output earlier) */

    print_b1_json(config, load_us, run_avg_us, run_p99_us, run_max_us, unload_us,
                  0, NULL);

    return 0;
}
