/**
 * @file imu_timer_test.c
 * @brief WASM sample: read IMU channels + measure loop time with a timer
 *
 * Uses SENSOR_CHAN_ACCEL_* and SENSOR_CHAN_GYRO_* channel IDs with the generic
 * sensor_read() API. Any Zephyr sensor driver that supports those channels
 * (LSM6DSL, ICM42688P, MPU6050, etc.) is automatically used.
 *
 * Capabilities required: sensor.read, timer
 *
 * Build with wasm_sample/build_wasm_apps.sh imu_timer_test
 *
 * Output (via printf over UART0 shell):
 *   [imu] accel  x=+0.012  y=-0.003  z=+9.812  m/s2
 *   [imu] gyro   x=+0.041  y=-0.012  z=+0.002  deg/s
 *   [imu] loop elapsed: 42 ms  (10 samples in 420 ms)
 */

#include "include/akira_api.h"

/* Number of IMU samples per reporting period */
#define SAMPLES_PER_REPORT 10

/* Delay between samples in microseconds (≈10 ms) */
#define SAMPLE_DELAY_US 10000

/* -------------------------------------------------------------------------
 * Minimal string helpers — no stdlib, only akira_api.h
 * -------------------------------------------------------------------------*/

static char *write_str(char *p, const char *s)
{
    while (*s) *p++ = *s++;
    return p;
}

static char *write_uint(char *p, unsigned int v)
{
    char tmp[12];
    int n = 0;

    if (v == 0) { *p++ = '0'; return p; }
    while (v) { tmp[n++] = '0' + (v % 10); v /= 10; }
    while (n > 0) *p++ = tmp[--n];
    return p;
}

static char *write_int(char *p, int v)
{
    if (v < 0) { *p++ = '-'; return write_uint(p, (unsigned int)(-v)); }
    return write_uint(p, (unsigned int)v);
}

/* Write a fixed-point sensor raw value (raw = real * 1000) as "+X.XXX". */
static char *write_axis(char *p, int raw)
{
    int whole = raw / 1000;
    int frac  = raw % 1000;
    if (frac < 0) frac = -frac;

    /* Always show explicit sign */
    if (raw >= 0) {
        *p++ = '+';
    } else {
        *p++ = '-';
        if (whole < 0) whole = -whole;
    }
    p = write_uint(p, (unsigned int)whole);
    *p++ = '.';
    /* Zero-pad fractional part to three digits */
    if (frac < 100) *p++ = '0';
    if (frac < 10)  *p++ = '0';
    p = write_uint(p, (unsigned int)frac);
    return p;
}

int main(void)
{
    printf("[imu_timer_test] starting — reading IMU via generic sensor API");

    int th = timer_create();
    if (th < 0) {
        char buf[48];
        char *p = write_str(buf, "[imu_timer_test] timer_create failed: ");
        p = write_int(p, th);
        *p = '\0';
        printf(buf);
        return -1;
    }

    timer_start(th);

    for (int i = 0; i < SAMPLES_PER_REPORT; i++) {
        int ax = sensor_read(SENSOR_CHAN_ACCEL_X);
        int ay = sensor_read(SENSOR_CHAN_ACCEL_Y);
        int az = sensor_read(SENSOR_CHAN_ACCEL_Z);
        int gx = sensor_read(SENSOR_CHAN_GYRO_X);
        int gy = sensor_read(SENSOR_CHAN_GYRO_Y);
        int gz = sensor_read(SENSOR_CHAN_GYRO_Z);

        if (ax == AKIRA_SENSOR_ERROR || ay == AKIRA_SENSOR_ERROR || az == AKIRA_SENSOR_ERROR) {
            printf("[imu] ERROR: sensor not available (device not ready)");
        } else if (gx == AKIRA_SENSOR_ERROR || gy == AKIRA_SENSOR_ERROR || gz == AKIRA_SENSOR_ERROR) {
            printf("[imu] ERROR: gyro not available (device not ready)");
        } else if (ax < -10000 || ay < -10000 || az < -10000) {
            char buf[72];
            char *p = write_str(buf, "[imu] ERROR reading accel: ax=");
            p = write_int(p, ax);
            p = write_str(p, " ay=");
            p = write_int(p, ay);
            p = write_str(p, " az=");
            p = write_int(p, az);
            *p = '\0';
            printf(buf);
        } else {
            char buf[80];
            char *p = write_str(buf, "[imu] accel  x=");
            p = write_axis(p, ax);
            p = write_str(p, "  y=");
            p = write_axis(p, ay);
            p = write_str(p, "  z=");
            p = write_axis(p, az);
            p = write_str(p, "  m/s2");
            *p = '\0';
            printf(buf);

            char buf2[80];
            char *p2 = write_str(buf2, "[imu] gyro   x=");
            p2 = write_axis(p2, gx);
            p2 = write_str(p2, "  y=");
            p2 = write_axis(p2, gy);
            p2 = write_str(p2, "  z=");
            p2 = write_axis(p2, gz);
            p2 = write_str(p2, "  deg/s");
            *p2 = '\0';
            printf(buf2);
        }

        delay(SAMPLE_DELAY_US);
    }

    timer_stop(th);
    int elapsed = timer_elapsed(th);
    timer_free(th);

    {
        char buf[80];
        char *p = write_str(buf, "[imu] loop elapsed: ");
        p = write_int(p, elapsed);
        p = write_str(p, " ms  (");
        p = write_uint(p, SAMPLES_PER_REPORT);
        p = write_str(p, " samples in ");
        p = write_int(p, elapsed);
        p = write_str(p, " ms)");
        *p = '\0';
        printf(buf);
    }

    printf("[imu_timer_test] done");

    return 0;
}
