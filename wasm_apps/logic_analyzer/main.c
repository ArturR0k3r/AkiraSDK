/**
 * @file main.c
 * @brief Simple Logic Analyzer - samples GPIO pins and draws waveform
 *
 * Demonstrates:
 * - Reading GPIO via `ocre_gpio_pin_get(port, pin)`
 * - Logging changes via `akira_log`
 * - Drawing a rolling waveform on the display using `akira_display_*`
 *
 * Build with `make` in this directory.
 */

#include <stdio.h>
#include <string.h>
#include "akira_api.h"

/* Display colors (RGB565) */
#define COLOR_BG     0x0000
#define COLOR_HIGH   0x07E0 // green
#define COLOR_LOW    0xF800 // red
#define COLOR_TEXT   0xFFFF

/* Sampling configuration */
#define SAMPLE_MS    20   /* sample interval in milliseconds (50 Hz) */
#define MAX_CHANNELS 4

/* Example pins to monitor (port, pin). Adjust for your hardware/simulator */
static const int channels[MAX_CHANNELS][2] = {
    {0, 0}, /* channel 0 -> port 0, pin 0 */
    {0, 1}, /* channel 1 -> port 0, pin 1 */
    {0, 2}, /* channel 2 -> port 0, pin 2 */
    {0, 3}, /* channel 3 -> port 0, pin 3 */
};

AKIRA_APP_MAIN()
{
    int width = 320, height = 240;
    akira_display_get_size(&width, &height);

    /* Layout */
    const int top_margin = 16;
    const int channel_height = (height - top_margin) / MAX_CHANNELS;
    const int amp = channel_height / 4; /* vertical offset for high/low */

    /* Prepare display */
    akira_display_clear(COLOR_BG);
    akira_display_text(5, 2, "Logic Analyzer - press POWER to stop", COLOR_TEXT);
    akira_display_flush();

    int x = 0;
    int prev_state[MAX_CHANNELS];
    memset(prev_state, 0, sizeof(prev_state));

    char logbuf[128];

    while (1) {
        /* Check for exit button (POWER) */
        if (akira_input_button_pressed(AKIRA_BTN_POWER)) {
            akira_log(2, "Logic analyzer: stopping (POWER pressed)");
            break;
        }

        /* Clear current column */
        akira_display_rect(x, top_margin, 1, height - top_margin, COLOR_BG);

        for (int ch = 0; ch < MAX_CHANNELS; ch++) {
            int port = channels[ch][0];
            int pin = channels[ch][1];

            int st = ocre_gpio_pin_get(port, pin);
            if (st < 0) {
                /* Read error - treat as low and log once */
                if (prev_state[ch] != 0) {
                    snprintf(logbuf, sizeof(logbuf), "GPIO read error on ch %d (p%d.%d)", ch, port, pin);
                    akira_log(1, logbuf);
                }
                st = 0;
            }

            /* Draw sample: high -> top, low -> bottom */
            int base_y = top_margin + ch * channel_height + channel_height / 2;
            int y = st ? (base_y - amp) : (base_y + amp);

            /* Draw a pixel for the sample */
            akira_display_pixel(x, y, st ? COLOR_HIGH : COLOR_LOW);

            /* Simple vertical connector to previous state for visibility */
            int prev_y = prev_state[ch] ? (base_y - amp) : (base_y + amp);
            if (prev_y != y) {
                /* draw a vertical line between prev_y and y at x-1 */
                int miny = prev_y < y ? prev_y : y;
                int maxy = prev_y < y ? y : prev_y;
                for (int yy = miny; yy <= maxy; yy++) {
                    int px = x - 1;
                    if (px < 0) px += width;
                    akira_display_pixel(px, yy, st ? COLOR_HIGH : COLOR_LOW);
                }
            }

            /* If state changed, log it */
            if (st != prev_state[ch]) {
                snprintf(logbuf, sizeof(logbuf), "GPIO ch %d (p%d.%d) -> %s", ch, port, pin, st ? "HIGH" : "LOW");
                akira_log(2, logbuf);
            }

            prev_state[ch] = st;
        }

        /* Advance and wrap */
        x = (x + 1) % width;

        /* Flush and sleep */
        akira_display_flush();
        ocre_sleep(SAMPLE_MS);
    }

    /* Clear display on exit */
    akira_display_clear(COLOR_BG);
    akira_display_text(5, 2, "Logic Analyzer stopped", COLOR_TEXT);
    akira_display_flush();

    return 0;
}
