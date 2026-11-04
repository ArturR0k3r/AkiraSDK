#include "akira_api.h"
// Do NOT include <string.h> — akira_api.h provides its own strlen/memset.

#define CAPTURE_MAX_SAMPLES 4096
#define CAPTURE_FILE_PREFIX "cap_"
#define CAPTURE_SAMPLE_RATE 250000u // 250 kHz — good for OOK remotes
#define FREQ_433_920_KHZ 433920
#define FREQ_315_000_KHZ 315000

static uint8_t capture_buf[CAPTURE_MAX_SAMPLES];
static int capture_len;
static int timer;

static void make_capture_path(char *dst, const char *name)
{
    const char *prefix = CAPTURE_FILE_PREFIX;
    while (*prefix)
        *dst++ = *prefix++;
    while (*name)
        *dst++ = *name++;
    *dst++ = '.';
    *dst++ = 'b';
    *dst++ = 'i';
    *dst++ = 'n';
    *dst = '\0';
}

static void save_capture(const char *name)
{
    char path[32];
    make_capture_path(path, name);

    int fd = storage_open(path, STORAGE_O_WRITE);
    if (fd < 0)
    {
        printf("open fail: %d", fd);
        return;
    }
    storage_write(fd, capture_buf, capture_len);
    storage_close(fd);
    printf("Saved %d bytes to %s", capture_len, path);
}

static int load_capture(const char *name)
{
    char path[32];
    make_capture_path(path, name);

    int fd = storage_open(path, STORAGE_O_READ);
    if (fd < 0)
        return -1;
    capture_len = storage_read(fd, capture_buf, CAPTURE_MAX_SAMPLES);
    storage_close(fd);
    return capture_len > 0 ? 0 : -1;
}

static void draw_status(const char *line1, const char *line2)
{
    display_clear(COLOR_BLACK);
    display_text(10, 10, "Sub-GHz Capture", COLOR_CYAN);
    display_text(10, 40, line1, COLOR_WHITE);
    display_text(10, 60, line2, COLOR_WHITE);
    display_flush();
}

int main(void)
{
    rf_select(AKIRA_RF_CHIP_CC1121);
    rf_set_frequency(FREQ_433_920_KHZ);
    rf_set_modulation(RADIO_MOD_OOK);
    rf_set_power(10);

    timer = timer_create();
    timer_start(timer);
    int state = 0;

    while (1)
    {
        if (state == 0)
        {
            draw_status("Press button to capture", "or long-press to replay");

            if (timer_elapsed(timer) > 3000)
            {
                timer_start(timer);
                state = 1;
                draw_status("Capturing...", "OOK 433.92 MHz");

                // (buf, max_bytes, sample_rate_hz, timeout_ms)
                capture_len = rf_raw_capture(capture_buf,
                                             CAPTURE_MAX_SAMPLES,
                                             CAPTURE_SAMPLE_RATE,
                                             500);
                if (capture_len > 0)
                {
                    save_capture("433ook");
                    draw_status("Captured", "Saved to SD");
                }
                else
                {
                    draw_status("Capture failed", "");
                }
                delay(2000000);
                state = 0;
            }
        }
        else if (state == 1)
        {
            if (load_capture("433ook") == 0)
            {
                draw_status("Replaying...", "");

                // (buf, len, sample_rate_hz, repeat)
                rf_raw_replay(capture_buf, capture_len,
                              CAPTURE_SAMPLE_RATE, 3);
                draw_status("Replay done", "");
            }
            delay(1000000);
            state = 0;
        }
        delay(16000);
    }
    return 0;
}