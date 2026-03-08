/**
 * @file akira_api.h
 * @brief AkiraOS WASM SDK - Native API Declarations
 * 
 * This header provides function declarations and constants for WASM applications
 * running on the AkiraOS runtime. Include this header in your WASM apps to access
 * native functionality for display, GPIO, sensors, and more.
 * 
 * All native functions are declared as extern and automatically become imports
 * when compiled with WASI SDK and -nostdlib flag.
 * 
 * @copyright Copyright (c) 2026 AkiraOS Contributors
 * @license Apache-2.0
 */

#ifndef AKIRA_API_H
#define AKIRA_API_H

#include <stdint.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * =============================================================================
 * COLOR CONSTANTS (RGB565 Format)
 * =============================================================================
 */

/** @brief Black color (RGB565: 0x0000) */
#define COLOR_BLACK       0x0000
/** @brief White color (RGB565: 0xFFFF) */
#define COLOR_WHITE       0xFFFF
/** @brief Red color (RGB565: 0xF800) */
#define COLOR_RED         0xF800
/** @brief Green color (RGB565: 0x07E0) */
#define COLOR_GREEN       0x07E0
/** @brief Blue color (RGB565: 0x001F) */
#define COLOR_BLUE        0x001F
/** @brief Yellow color (RGB565: 0xFFE0) */
#define COLOR_YELLOW      0xFFE0
/** @brief Cyan color (RGB565: 0x07FF) */
#define COLOR_CYAN        0x07FF
/** @brief Magenta color (RGB565: 0xF81F) */
#define COLOR_MAGENTA     0xF81F
/** @brief Gray color (RGB565: 0x7BEF) */
#define COLOR_GRAY        0x7BEF
/** @brief Dark gray color (RGB565: 0x39E7) */
#define COLOR_DARK_GRAY   0x39E7
/** @brief Light gray color (RGB565: 0xC618) */
#define COLOR_LIGHT_GRAY  0xC618
/** @brief Orange color (RGB565: 0xFD20) */
#define COLOR_ORANGE      0xFD20
/** @brief Purple color (RGB565: 0x801F) */
#define COLOR_PURPLE      0x801F

/*
 * =============================================================================
 * GPIO CONFIGURATION FLAGS
 * =============================================================================
 */

/** @brief Configure GPIO pin as input */
#define GPIO_INPUT            (1U << 0)
/** @brief Configure GPIO pin as output */
#define GPIO_OUTPUT           (1U << 1)
/** @brief Initialize output pin to low state */
#define GPIO_OUTPUT_INIT_LOW  (1U << 2)
/** @brief Initialize output pin to high state */
#define GPIO_OUTPUT_INIT_HIGH (1U << 3)
/** @brief Enable internal pull-up resistor */
#define GPIO_PULL_UP          (1U << 4)
/** @brief Enable internal pull-down resistor */
#define GPIO_PULL_DOWN        (1U << 5)
/** @brief Configure pin as active-low */
#define GPIO_ACTIVE_LOW       (1U << 6)
/** @brief Configure pin as active-high */
#define GPIO_ACTIVE_HIGH      (1U << 7)

/*
 * =============================================================================
 * SENSOR CHANNEL IDs
 * =============================================================================
 * These values match Zephyr's enum sensor_channel exactly.
 * Pass them directly to sensor_read().
 */

/** @brief Accelerometer X-axis, m/s² */
#define SENSOR_CHAN_ACCEL_X        0
/** @brief Accelerometer Y-axis, m/s² */
#define SENSOR_CHAN_ACCEL_Y        1
/** @brief Accelerometer Z-axis, m/s² */
#define SENSOR_CHAN_ACCEL_Z        2
/** @brief Gyroscope X-axis, rad/s */
#define SENSOR_CHAN_GYRO_X         4
/** @brief Gyroscope Y-axis, rad/s */
#define SENSOR_CHAN_GYRO_Y         5
/** @brief Gyroscope Z-axis, rad/s */
#define SENSOR_CHAN_GYRO_Z         6
/** @brief Magnetometer X-axis, Gauss */
#define SENSOR_CHAN_MAGN_X         8
/** @brief Magnetometer Y-axis, Gauss */
#define SENSOR_CHAN_MAGN_Y         9
/** @brief Magnetometer Z-axis, Gauss */
#define SENSOR_CHAN_MAGN_Z         10
/** @brief Ambient temperature, °C */
#define SENSOR_CHAN_AMBIENT_TEMP   13
/** @brief Pressure, kPa */
#define SENSOR_CHAN_PRESS          14
/** @brief Relative humidity, % */
#define SENSOR_CHAN_HUMIDITY       16
/** @brief Altitude, m */
#define SENSOR_CHAN_ALTITUDE       23
/** @brief Voltage, V */
#define SENSOR_CHAN_VOLTAGE        33
/** @brief Current, A */
#define SENSOR_CHAN_CURRENT        35
/** @brief Power, W */
#define SENSOR_CHAN_POWER          36

/*
 * =============================================================================
 * LOG LEVELS
 * =============================================================================
 */

/** @brief Error log level */
#define LOG_LEVEL_ERR     1
/** @brief Warning log level */
#define LOG_LEVEL_WRN     2
/** @brief Info log level */
#define LOG_LEVEL_INF     3

/*
 * =============================================================================
 * LOGGING API
 * =============================================================================
 * Required capability: none
 */

/**
 * @brief Log a message to the AkiraOS console
 * 
 * @param level Log level (LOG_LEVEL_ERR, LOG_LEVEL_WRN, LOG_LEVEL_INF, LOG_LEVEL_DBG)
 * @param message Null-terminated string message to log
 * @return 0 on success, negative error code on failure
 */
extern int printf_native(const char *message);

/* String helpers — no stdlib required */
static inline int strlen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static inline int strcmp(const char *a, const char *b) {
    while (*a && (*a == *b)) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

// A simple integer-to-string helper since we have no libc
static void itoa(int value, char *ptr) {
    char temp[12];
    int i = 0;
    if (value == 0) { *ptr++ = '0'; *ptr = '\0'; return; }
    if (value < 0) { *ptr++ = '-'; value = -value; }
    while (value > 0) { temp[i++] = (value % 10) + '0'; value /= 10; }
    while (i > 0) { *ptr++ = temp[--i]; }
    *ptr = '\0';
}

// Your wrapper function inside WASM
void printf(const char *fmt, ...) {
    char buffer[256]; // The "baked" string buffer
    char *p = buffer;
    va_list args;
    va_start(args, fmt);

    for (const char *f = fmt; *f != '\0'; f++) {
        if (*f == '\n' || *f == '\r') {
            /* Skip newlines — the host logger adds its own line endings */
            continue;
        }
        if (*f != '%') {
            *p++ = *f;
            continue;
        }
        f++; // Skip '%'
        switch (*f) {
            case 'd': {
                itoa(va_arg(args, int), p);
                while (*p) p++; // Move pointer to end of number
                break;
            }
            case 's': {
                char *s = va_arg(args, char *);
                while (*s) *p++ = *s++;
                break;
            }
            default: *p++ = *f; break;
        }
    }
    *p = '\0';
    va_end(args);

    // Send the final, formatted string to the host
    printf_native(buffer);
}



/**
 * @brief Delay execution for a specified number of microseconds
 */
extern int delay(uint32_t microseconds);


/*
 * =============================================================================
 * DISPLAY API
 * =============================================================================
 * Required capability: display.write
 * 
 * All display functions use RGB565 color format (16-bit color).
 * Coordinates start at (0,0) in the top-left corner.
 */

/**
 * @brief Clear the entire display with a solid color
 * 
 * @param color RGB565 color value
 * @return 0 on success, negative error code on failure
 */
extern int display_clear(uint32_t color);

/**
 * @brief Set a single pixel on the display
 * 
 * @param x X coordinate (horizontal position)
 * @param y Y coordinate (vertical position)
 * @param color RGB565 color value
 * @return 0 on success, negative error code on failure
 */
extern int display_pixel(int32_t x, int32_t y, uint32_t color);

/**
 * @brief Draw a filled rectangle on the display
 * 
 * @param x X coordinate of top-left corner
 * @param y Y coordinate of top-left corner
 * @param w Width in pixels
 * @param h Height in pixels
 * @param color RGB565 color value
 * @return 0 on success, negative error code on failure
 */
extern int display_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);

/**
 * @brief Display text using the small font (7x10 pixels per character)
 * 
 * @param x X coordinate of text starting position
 * @param y Y coordinate of text starting position
 * @param text Null-terminated string to display
 * @param color RGB565 color value
 * @return 0 on success, negative error code on failure
 */
extern int display_text(int32_t x, int32_t y, const char *text, uint32_t color);

/**
 * @brief Display text using the large font (11x18 pixels per character)
 * 
 * @param x X coordinate of text starting position
 * @param y Y coordinate of text starting position
 * @param text Null-terminated string to display
 * @param color RGB565 color value
 * @return 0 on success, negative error code on failure
 */
extern int display_text_large(int32_t x, int32_t y, const char *text, uint32_t color);

/**
 * @brief Flush the framebuffer to the display hardware.
 *
 * Draw calls (rect, pixel, text, …) write into a back-buffer.  Call
 * display_flush() once you have finished composing a frame to push the
 * result to the screen.  An automatic flush fires 50 ms after the last draw
 * call, but calling this explicitly gives smoother animation.
 *
 * @return 0 on success, negative error code on failure
 */
extern int display_flush(void);

/**
 * @brief Get the display resolution.
 *
 * @param w_out  Pointer receives the display width  in pixels
 * @param h_out  Pointer receives the display height in pixels
 * @return 0 on success, negative error code on failure
 */
extern int display_get_size(int32_t *w_out, int32_t *h_out);

/**
 * @brief Draw a straight line between two points (Bresenham — no FPU).
 *
 * @param x0 Start X  @param y0 Start Y
 * @param x1 End X    @param y1 End Y
 * @param color RGB565 color
 * @return 0 on success
 */
extern int display_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color);

/**
 * @brief Draw a circle outline (midpoint algorithm — no FPU).
 *
 * @param cx Centre X  @param cy Centre Y  @param r Radius (pixels)
 * @param color RGB565 color
 * @return 0 on success
 */
extern int display_circle(int32_t cx, int32_t cy, int32_t r, uint32_t color);

/**
 * @brief Draw a filled circle.
 *
 * @param cx Centre X  @param cy Centre Y  @param r Radius (pixels)
 * @param color RGB565 fill color
 * @return 0 on success
 */
extern int display_circle_fill(int32_t cx, int32_t cy, int32_t r, uint32_t color);

/**
 * @brief Draw a triangle outline (three lines).
 *
 * @param x0,y0  @param x1,y1  @param x2,y2  Vertices
 * @param color RGB565 color
 * @return 0 on success
 */
extern int display_triangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                             int32_t x2, int32_t y2, uint32_t color);

/**
 * @brief Draw a filled triangle (scanline rasteriser).
 *
 * @param x0,y0  @param x1,y1  @param x2,y2  Vertices (any order)
 * @param color RGB565 fill color
 * @return 0 on success
 */
extern int display_triangle_fill(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                                  int32_t x2, int32_t y2, uint32_t color);

/**
 * @brief Draw a rectangle outline (four lines).
 *
 * @param x,y   Top-left corner  @param w Width  @param h Height
 * @param color RGB565 color
 * @return 0 on success
 */
extern int display_rect_outline(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);

/**
 * @brief Blit an RGB565 bitmap to the display.
 *
 * @param x,y        Top-left destination corner
 * @param w,h        Width and height in pixels
 * @param data       Pointer to RGB565 pixel data (row-major, w*h*2 bytes)
 * @param data_size  Size of @p data in bytes (must be >= w*h*2)
 * @return 0 on success, -EINVAL if data_size is too small
 */
extern int display_bitmap(int32_t x, int32_t y, int32_t w, int32_t h,
                           const uint16_t *data, uint32_t data_size);

/**
 * @brief Blit an RGB565 bitmap with a transparent colour key.
 *
 * Pixels whose value equals @p key are not written to the framebuffer.
 *
 * @param x,y        Top-left destination corner
 * @param w,h        Width and height in pixels
 * @param data       Pointer to RGB565 pixel data (row-major, w*h*2 bytes)
 * @param data_size  Size of @p data in bytes (must be >= w*h*2)
 * @param key        RGB565 transparent colour value
 * @return 0 on success
 */
extern int display_bitmap_transparent(int32_t x, int32_t y, int32_t w, int32_t h,
                                       const uint16_t *data, uint32_t data_size,
                                       uint32_t key);

/**
 * @brief Draw a horizontal line (optimised run).
 * @param x,y  Start coordinate  @param len  Length in pixels
 * @param color  RGB565 color
 * @return 0 on success
 */
extern int display_hline(int32_t x, int32_t y, int32_t len, uint32_t color);

/**
 * @brief Draw a vertical line (optimised run).
 * @param x,y  Start coordinate  @param len  Length in pixels
 * @param color  RGB565 color
 * @return 0 on success
 */
extern int display_vline(int32_t x, int32_t y, int32_t len, uint32_t color);

/**
 * @brief Render an integer as decimal text using the small font.
 *
 * No stdlib required — the conversion is done natively.
 *
 * @param x,y    Top-left of the first digit
 * @param value  Value to display (positive or negative)
 * @param color  RGB565 color
 * @return 0 on success
 */
extern int display_number(int32_t x, int32_t y, int32_t value, uint32_t color);

/**
 * @brief Draw a horizontal progress bar.
 *
 * Draws a @p bg filled rectangle, then a @p fg rectangle proportional to
 * @p value / @p max_val, then an outline in @p fg.
 *
 * @param x,y      Top-left corner
 * @param w,h      Width and height in pixels
 * @param value    Current fill value (clamped to [0, max_val])
 * @param max_val  Maximum value (bar is full when value == max_val)
 * @param fg       Foreground / fill RGB565 color
 * @param bg       Background RGB565 color
 * @return 0 on success
 */
extern int display_progress_bar(int32_t x, int32_t y, int32_t w, int32_t h,
                                 int32_t value, int32_t max_val,
                                 uint32_t fg, uint32_t bg);

/**
 * @brief Draw a rounded rectangle outline.
 *
 * @param x,y    Top-left corner
 * @param w,h    Width and height in pixels
 * @param radius Corner arc radius in pixels (clamped to min(w,h)/2)
 * @param color  RGB565 color
 * @return 0 on success
 */
extern int display_rounded_rect(int32_t x, int32_t y, int32_t w, int32_t h,
                                 int32_t radius, uint32_t color);

/**
 * @brief Draw a filled rounded rectangle.
 *
 * @param x,y    Top-left corner
 * @param w,h    Width and height in pixels
 * @param radius Corner arc radius in pixels (clamped to min(w,h)/2)
 * @param color  RGB565 fill color
 * @return 0 on success
 */
extern int display_rounded_rect_fill(int32_t x, int32_t y, int32_t w, int32_t h,
                                      int32_t radius, uint32_t color);

/*
 * =============================================================================
 * GPIO API
 * =============================================================================
 * Required capabilities: gpio.read and/or gpio.write
 * 
 * GPIO operations allow control of digital input/output pins.
 * Pin numbers are logical and mapped by the runtime to physical pins.
 */

/**
 * @brief Configure a GPIO pin
 * 
 * @param pin GPIO pin number
 * @param flags Configuration flags (GPIO_INPUT, GPIO_OUTPUT, etc.)
 * @return 0 on success, negative error code on failure
 */
extern int gpio_configure(uint32_t pin, uint32_t flags);

/**
 * @brief Read the state of a GPIO input pin
 * 
 * @param pin GPIO pin number
 * @return Pin state (0 = low, 1 = high), negative error code on failure
 */
extern int gpio_read(uint32_t pin);

/**
 * @brief Write a value to a GPIO output pin
 * 
 * @param pin GPIO pin number
 * @param value Value to write (0 = low, 1 = high)
 * @return 0 on success, negative error code on failure
 */
extern int gpio_write(uint32_t pin, uint32_t value);

/*
 * =============================================================================
 * SENSOR API
 * =============================================================================
 * Required capability: sensor.read
 * 
 * Sensor readings return scaled integer values. Refer to documentation for
 * scaling factors specific to each sensor type.
 */

/**
 * @brief Error sentinel returned by sensor_read() when the sensor is
 * unavailable or an I/O error occurred.
 *
 * Compare the return value against this constant rather than testing for
 * arbitrary negative numbers, because valid near-zero readings (e.g.
 * -0.001 m/s² = -1) are also small negative integers.
 *
 * Example:
 *   int ax = sensor_read(SENSOR_CHAN_ACCEL_X);
 *   if (ax == AKIRA_SENSOR_ERROR) { ... handle error ... }
 */
#define AKIRA_SENSOR_ERROR  (-2147483647 - 1)   /* INT32_MIN */

/**
 * @brief Read a sensor channel value.
 *
 * Iterates all DT-enabled sensor devices and returns the first that answers
 * the requested channel. On success, returns the reading scaled by 1000
 * (divide by 1000.0 to recover the physical value). On failure, returns
 * AKIRA_SENSOR_ERROR.
 *
 * @param channel  Sensor channel ID (SENSOR_CHAN_* constant).
 * @return Reading x1000 on success, AKIRA_SENSOR_ERROR on failure.
 */
extern int sensor_read(int32_t channel);

/*
 * =============================================================================
 * MEMORY API
 * =============================================================================
 * 
 * Dynamic memory allocation within WASM module's memory quota.
 * Memory is automatically freed when the WASM app terminates.
 */

/**
 * @brief Allocate memory from the WASM heap
 * 
 * @param size Number of bytes to allocate
 * @return WASM address of allocated memory, 0 on failure
 */
extern uint32_t mem_alloc(uint32_t size);

/**
 * @brief Free previously allocated memory
 * 
 * @param ptr WASM address returned by mem_alloc()
 */
extern void mem_free(uint32_t ptr);

/*
 * =============================================================================
 * BLE APP API
 * =============================================================================
 * Required capability: "ble"
 *
 * Arduino-style BLE API for creating custom GATT services.
 *
 * Typical usage:
 *
 *   int svc = ble_service_create("19B10000-E8F2-537E-4F6C-D104768A1214");
 *   int ch  = ble_char_create("19B10001-E8F2-537E-4F6C-D104768A1214",
 *                              BLE_PROP_READ | BLE_PROP_WRITE, 1);
 *   ble_service_add_char(svc, ch);
 *   ble_add_service(svc);
 *   ble_set_local_name("AkiraOS_LED");
 *   ble_set_advertised_service(svc);
 *   ble_init();
 *   ble_advertise();
 *
 *   while (1) {
 *       uint8_t buf[64];
 *       int evt = ble_event_pop(buf, sizeof(buf));
 *       if (evt == BLE_EVT_CHAR_WRITTEN) {
 *           int char_h = buf[1];
 *           // buf[2..3] = data_len LE, buf[4..] = data
 *       }
 *   }
 */

/* BLE characteristic property flags */
#define BLE_PROP_READ        0x02
#define BLE_PROP_WRITE_WO_RSP 0x04
#define BLE_PROP_WRITE       0x08
#define BLE_PROP_NOTIFY      0x10
#define BLE_PROP_INDICATE    0x20

/* BLE event types returned by ble_event_pop() */
#define BLE_EVT_NONE         0
#define BLE_EVT_CONNECTED    1
#define BLE_EVT_DISCONNECTED 2
#define BLE_EVT_CHAR_WRITTEN 3

/**
 * @brief Initialise BLE in app mode (lazy BT stack start).
 * Must be called before advertising. Fails with -EBUSY if HID mode active.
 * @return 0 on success, negative error code on failure.
 */
extern int ble_init(void);

/**
 * @brief Deinitialise BLE, unregister all services, release BLE lock.
 * @return 0 on success.
 */
extern int ble_deinit(void);

/**
 * @brief Set the BLE device name visible to scanning peers.
 * @param name  Null-terminated string (max 29 bytes).
 * @return 0 on success, negative error code on failure.
 */
extern int ble_set_local_name(const char *name);

/**
 * @brief Create a GATT service with a 128-bit UUID.
 * @param uuid128_str  UUID string "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx".
 * @return Service handle (>=0) on success, negative error code on failure.
 */
extern int ble_service_create(const char *uuid128_str);

/**
 * @brief Create a GATT characteristic.
 * @param uuid128_str  128-bit UUID string.
 * @param props        OR of BLE_PROP_* flags.
 * @param max_len      Maximum value length in bytes.
 * @return Characteristic handle (>=0) on success, negative error code on failure.
 */
extern int ble_char_create(const char *uuid128_str, int32_t props,
			   int32_t max_len);

/**
 * @brief Add a characteristic to a service (call before ble_add_service).
 * @param svc_h   Service handle from ble_service_create().
 * @param char_h  Characteristic handle from ble_char_create().
 * @return 0 on success, negative error code on failure.
 */
extern int ble_service_add_char(int32_t svc_h, int32_t char_h);

/**
 * @brief Finalise and register a service with the GATT server.
 * Call once after all characteristics are added.
 * @param svc_h  Service handle.
 * @return 0 on success, negative error code on failure.
 */
extern int ble_add_service(int32_t svc_h);

/**
 * @brief Choose which service UUID appears in the advertisement payload.
 * @param svc_h  Service handle.
 * @return 0 on success, negative error code on failure.
 */
extern int ble_set_advertised_service(int32_t svc_h);

/**
 * @brief Start BLE advertising.
 * Call after ble_add_service() and ble_set_advertised_service().
 * @return 0 on success, negative error code on failure.
 */
extern int ble_advertise(void);

/**
 * @brief Stop BLE advertising.
 * @return 0 on success.
 */
extern int ble_stop_advertise(void);

/**
 * @brief Check if a BLE peer is connected.
 * @return 1 if connected, 0 otherwise.
 */
extern int ble_is_connected(void);

/**
 * @brief Write (and optionally notify) a characteristic value.
 * @param char_h   Characteristic handle.
 * @param data     Pointer to data buffer.
 * @param len      Number of bytes to write.
 * @return 0 on success, negative error code on failure.
 */
extern int ble_char_write(int32_t char_h, const uint8_t *data, uint32_t len);

/**
 * @brief Read the current value of a characteristic (last write).
 * @param char_h   Characteristic handle.
 * @param buf      Destination buffer.
 * @param len      Buffer capacity in bytes.
 * @return Bytes copied on success, negative error code on failure.
 */
extern int ble_char_read(int32_t char_h, uint8_t *buf, uint32_t len);

/**
 * @brief Pop the next BLE event from the internal queue (non-blocking).
 *
 * Event serialisation in @p buf:
 *   Byte 0        : event type (BLE_EVT_*)
 *   Byte 1        : char_handle (only valid for BLE_EVT_CHAR_WRITTEN)
 *   Bytes 2-3 LE  : data_len
 *   Bytes 4+      : data payload
 *
 * @param buf  Destination buffer (at least 4 + expected data bytes).
 * @param len  Buffer capacity.
 * @return Event type (>0 = BLE_EVT_*) if available, 0 if queue empty.
 */
extern int ble_event_pop(uint8_t *buf, uint32_t len);

/*
 * =============================================================================
 * HID API
 * =============================================================================
 * Required capability: hid
 *
 * HID keyboard modifier constants (combinable with |)
 */
#define HID_MOD_NONE        0x00
#define HID_MOD_LEFT_CTRL   0x01
#define HID_MOD_LEFT_SHIFT  0x02
#define HID_MOD_LEFT_ALT    0x04
#define HID_MOD_LEFT_GUI    0x08  /**< Windows / Cmd key */
#define HID_MOD_RIGHT_CTRL  0x10
#define HID_MOD_RIGHT_SHIFT 0x20
#define HID_MOD_RIGHT_ALT   0x40
#define HID_MOD_RIGHT_GUI   0x80

/* HID mouse button masks */
#define HID_MOUSE_BTN_LEFT   0x01
#define HID_MOUSE_BTN_RIGHT  0x02
#define HID_MOUSE_BTN_MIDDLE 0x04

/* HID consumer / media usage codes */
#define HID_CONSUMER_PLAY_PAUSE    0x00CD
#define HID_CONSUMER_STOP          0x00B7
#define HID_CONSUMER_NEXT_TRACK    0x00B5
#define HID_CONSUMER_PREV_TRACK    0x00B6
#define HID_CONSUMER_VOL_UP        0x00E9
#define HID_CONSUMER_VOL_DOWN      0x00EA
#define HID_CONSUMER_MUTE          0x00E2
#define HID_CONSUMER_BRIGHTNESS_UP 0x006F
#define HID_CONSUMER_BRIGHTNESS_DN 0x0070

/* USB HID keyboard keycodes (HUT 1.4 table 10) */
#define HID_KEY_A     0x04
#define HID_KEY_B     0x05
#define HID_KEY_C     0x06
#define HID_KEY_S     0x16
#define HID_KEY_GRAVE  0x35  /**< Backtick / grave; with SHIFT → tilde */
#define HID_KEY_MINUS  0x2D  /**< - / _ */
#define HID_KEY_PRTSCN 0x46  /**< Print Screen / SysRq */
#define HID_KEY_ENTER  0x28
#define HID_KEY_ESC   0x29
#define HID_KEY_SPACE 0x2C

/** @brief Enable HID subsystem. Must be called before other HID functions. */
extern int hid_enable(void);

/** @brief Disable HID subsystem. */
extern int hid_disable(void);

/**
 * @brief Check whether a HID host is connected and subscribed.
 * @return 1 connected, 0 not connected
 */
extern int hid_is_connected(void);

/** @brief Press a keyboard key (USB HID keycode). */
extern int hid_key_press(int32_t keycode);

/** @brief Release a keyboard key. */
extern int hid_key_release(int32_t keycode);

/** @brief Release all keyboard keys at once. */
extern int hid_key_release_all(void);

/**
 * @brief Type a null-terminated ASCII string as individual key events.
 * Handles uppercase via Shift automatically.
 */
extern int hid_type_string(const char *str);

/** @brief Press one or more gamepad buttons (bitmask). */
extern int hid_gamepad_press(int32_t btn_mask);

/** @brief Release gamepad buttons. */
extern int hid_gamepad_release(int32_t btn_mask);

/** @brief Set a gamepad analogue axis (-32768..32767). */
extern int hid_gamepad_set_axis(int32_t axis, int32_t value);

/** @brief Set gamepad D-pad direction (0=centre, 1-8=N/NE/E/SE/S/SW/W/NW). */
extern int hid_gamepad_set_dpad(int32_t direction);

/** @brief Zero all gamepad state. */
extern int hid_gamepad_reset(void);

/** @brief Move mouse by relative dx/dy (-127..127). */
extern int hid_mouse_move(int32_t dx, int32_t dy);

/** @brief Press a mouse button (HID_MOUSE_BTN_*). */
extern int hid_mouse_btn_press(int32_t button);

/** @brief Release a mouse button. */
extern int hid_mouse_btn_release(int32_t button);

/** @brief Scroll mouse wheel by delta (-127..127). */
extern int hid_mouse_scroll(int32_t delta);

/**
 * @brief Send a consumer (media) key event (single shot).
 * @param usage_code  HID_CONSUMER_* constant
 */
extern int hid_consumer_send(int32_t usage_code);

/**
 * @brief Send a raw HID report.
 * @param report_id  HID report identifier
 * @param data_ptr   Pointer to report payload
 * @param len        Payload length (≤ 64 bytes)
 */
extern int hid_send_raw_report(int32_t report_id,
                               const uint8_t *data_ptr, uint32_t len);

/**
 * @brief Register a named keyboard shortcut.
 * @param name      Short identifier, e.g. "copy" (max 15 chars)
 * @param modifier  Modifier bitmask (HID_MOD_* constants)
 * @param keycode   USB HID keycode of the main key
 */
extern int hid_action_register(const char *name,
                               int32_t modifier, int32_t keycode);

/**
 * @brief Trigger a previously registered named shortcut.
 * @param name  Shortcut name passed to hid_action_register()
 */
extern int hid_action_trigger(const char *name);

/**
 * @brief Select HID transport.
 * @param transport  0=none, 1=BLE, 2=USB
 * Required capability: hid
 */
extern int hid_set_transport(int32_t transport);

/** HID transport identifiers (pass to hid_set_transport()) */
#define HID_TRANSPORT_NONE 0
#define HID_TRANSPORT_BLE  1
#define HID_TRANSPORT_USB  2

/**
 * @brief Set HID device type bitmask before calling hid_enable().
 * @param types  Bitmask of HID_DEVICE_* flags.
 *               Returns -EBUSY if called after hid_enable().
 * Required capability: hid
 */
extern int hid_set_device_types(int32_t types);

/** HID device type bitmask flags (pass to hid_set_device_types() or hid_init()) */
#define HID_DEVICE_KEYBOARD 0x01  /**< Standard keyboard + media keys */
#define HID_DEVICE_GAMEPAD  0x02  /**< Gamepad / joystick */
#define HID_DEVICE_MOUSE    0x04  /**< Mouse with relative movement */
#define HID_DEVICE_COMBO    0x07  /**< All device types combined (keyboard + gamepad + mouse) */

/**
 * @brief One-shot HID setup: select transport, set device types, and enable.
 *
 * Replaces the three-call sequence hid_set_transport + hid_set_device_types + hid_enable.
 * Use HID_DEVICE_COMBO (0x07) to enable all report types.
 *
 * @param transport    HID_TRANSPORT_BLE or HID_TRANSPORT_USB
 * @param device_types Bitmask of HID_DEVICE_* flags, e.g. HID_DEVICE_COMBO
 * @return 0 on success, negative errno on failure
 * Required capability: hid
 */
extern int hid_init(int transport, int device_types);

/*
 * =============================================================================
 * APP LIFECYCLE API
 * =============================================================================
 * Required capability: app.control  (elevated — must not be granted to
 *                                    untrusted apps)
 *
 * App state codes returned by app_get_status():
 */
#define APP_STATE_NEW       0
#define APP_STATE_INSTALLED 1
#define APP_STATE_RUNNING   2
#define APP_STATE_STOPPED   3
#define APP_STATE_ERROR     4
#define APP_STATE_FAILED    5

/**
 * @brief Get the current state of an installed app.
 * @return APP_STATE_* constant, or negative error code
 */
extern int app_get_status(const char *name);

/**
 * @brief List all installed apps into a buffer.
 *
 * Writes newline-separated "name:STATE" entries, e.g.:
 * "bt_echo:INSTALLED\nmacro_pad:RUNNING\n"
 *
 * @param buf      Destination buffer
 * @param buf_len  Buffer capacity
 * @return Number of apps written, negative on error
 */
extern int app_list(uint8_t *buf, uint32_t buf_len);

/**
 * @brief Write this app's own name into @p buf.
 * @param buf      Destination buffer (at least 32 bytes)
 * @param buf_len  Buffer capacity
 * @return Length of the name, negative on error
 */
extern int app_get_self_name(uint8_t *buf, uint32_t buf_len);

/**
 * @brief Start an installed app by name.
 *
 * Requires capability: "app.control"
 *
 * @param name  Null-terminated app name
 * @return 0 on success, negative errno on failure
 *   -ENOENT  app not installed
 *   -EBUSY   max concurrent apps already running
 *   -EPERM   capability not granted
 */
extern int app_start(const char *name);

/**
 * @brief Stop a running app by name.
 *
 * Requires capability: "app.control"
 * An app cannot stop itself; use return 0 from main() for self-exit.
 *
 * @param name  Null-terminated app name
 * @return 0 on success, negative errno on failure
 */
extern int app_stop(const char *name);

/**
 * @brief Start another app and signal this app to exit (lightweight handoff).
 *
 * Requires capability: "app.switch" OR "app.control"
 *
 * Starts (or resumes) the target app.  The calling app must then
 * return 0 from its own main() to complete the handoff.  The supervisor
 * detects both events via the "akira.lifecycle" IPC topic.
 *
 * Typical usage:
 * @code
 *   app_switch("supervisor");
 *   return 0;   // trigger clean exit -> lifecycle event -> supervisor redraws
 * @endcode
 *
 * @param name  Target app name
 * @return 0 on success (caller must return from main), negative on error
 */
extern int app_switch(const char *name);

/**
 * @brief Lifecycle event payload published on "akira.lifecycle" IPC topic.
 *
 * Subscribe to "akira.lifecycle" with msg_subscribe() and receive events
 * with msg_recv() / msg_try_recv() into a buffer of this size.
 *
 * state values match APP_STATE_* constants defined above.
 */
typedef struct {
    char name[32]; /**< App name (null-terminated) */
    int  state;    /**< New state: APP_STATE_* constant */
} akira_lifecycle_event_t;

/*
 * =============================================================================
 * IPC PUB/SUB API
 * =============================================================================
 * Required capability: ipc
 */

/** @brief Subscribe to a named topic. */
extern int msg_subscribe(const char *topic);

/** @brief Unsubscribe from a named topic. */
extern int msg_unsubscribe(const char *topic);

/**
 * @brief Publish a message to all subscribers.
 * @param topic     Topic name
 * @param data_ptr  Pointer to payload buffer
 * @param len       Payload length (≤ CONFIG_AKIRA_IPC_MSG_MAX_SIZE = 256)
 * @return Number of subscribers that received the message
 */
extern int msg_publish(const char *topic,
                       const uint8_t *data_ptr, uint32_t len);

/**
 * @brief Receive the next message from a subscribed topic.
 * @param topic       Topic name
 * @param buf_ptr     Destination buffer
 * @param buf_len     Buffer capacity
 * @param timeout_ms  0 = non-blocking, -1 = wait forever, else ms limit
 * @return Bytes received, -EAGAIN on timeout, negative on error
 */
extern int msg_recv(const char *topic,
                    uint8_t *buf_ptr, uint32_t buf_len,
                    int32_t timeout_ms);

/**
 * @brief Non-blocking receive (equivalent to msg_recv(..., 0)).
 * @return Bytes received, -EAGAIN if nothing pending
 */
extern int msg_try_recv(const char *topic,
                        uint8_t *buf_ptr, uint32_t buf_len);

/**
 * @brief Return the number of pending messages in a subscription queue.
 * @return Count ≥ 0, or -ENOENT if not subscribed
 */
extern int msg_pending(const char *topic);

/*
 * =============================================================================
 * RF TRANSCEIVER API
 * =============================================================================
 * Required capability: rf.transceive
 * 
 * Functions for controlling radio frequency transceivers (e.g., LoRa, LR1121).
 */

/**
 * @brief Set RF transceiver frequency
 * 
 * @param freq_hz Frequency in Hertz
 * @return 0 on success, negative error code on failure
 */
extern int rf_set_frequency(uint32_t freq_hz);

/**
 * @brief Set RF transmission power
 * 
 * @param dbm Power in dBm
 * @return 0 on success, negative error code on failure
 */
extern int rf_set_power(int8_t dbm);

/**
 * @brief Get received signal strength indicator (RSSI)
 * 
 * @param rssi Pointer to store RSSI value
 * @return 0 on success, negative error code on failure
 */
extern int rf_get_rssi(int16_t *rssi);

/**
 * @brief Send data over RF transceiver
 * 
 * @param payload_ptr Pointer to payload data
 * @param len Length of payload in bytes
 * @return 0 on success, negative error code on failure
 */
extern int rf_send(uint32_t payload_ptr, uint32_t len);

/*
 * =============================================================================
 * TIMER API
 * =============================================================================
 * Required capability: timer
 *
 * Polling-only timers backed by the OS uptime counter.
 * Values are in milliseconds. Use delay() to yield between polls.
 */

/**
 * @brief Allocate a new timer handle.
 * @return Handle index ≥0 on success, -ENOMEM if pool is full.
 */
extern int timer_create(void);

/**
 * @brief Start (or restart) a timer, resetting elapsed time to 0.
 * @param handle Handle returned by timer_create().
 * @return 0 on success, negative error code on failure.
 */
extern int timer_start(int32_t handle);

/**
 * @brief Stop a running timer, preserving the elapsed time.
 * @param handle Handle returned by timer_create().
 * @return 0 on success, negative error code on failure.
 */
extern int timer_stop(int32_t handle);

/**
 * @brief Return elapsed milliseconds.
 * Running timer: time since last timer_start().
 * Stopped timer: time between last timer_start() and timer_stop().
 * @param handle Handle returned by timer_create().
 * @return Elapsed milliseconds (int32), negative error code on failure.
 */
extern int timer_elapsed(int32_t handle);

/**
 * @brief Release a timer handle back to the pool.
 * @param handle Handle returned by timer_create().
 * @return 0 on success, negative error code on failure.
 */
extern int timer_free(int32_t handle);

/*
 * =============================================================================
 * UART API
 * =============================================================================
 * Required capability: uart
 *
 * Full-duplex UART access. UART0 is reserved for the system shell.
 * port_id 0 = first secondary UART (UART1).
 * uart_read() is non-blocking — returns 0 if no data is available.
 */

/**
 * @brief Open a secondary UART port.
 * @param port_id  0-indexed secondary UART (0 = UART1).
 * @param baud_rate Desired baud rate (e.g. 115200).
 * @return Handle index ≥0 on success, negative error code on failure.
 */
extern int uart_open(int32_t port_id, int32_t baud_rate);

/**
 * @brief Write bytes to an open UART.
 * @param handle    Handle from uart_open().
 * @param buf       Pointer to data buffer.
 * @param len       Number of bytes to write.
 * @return Bytes written, or negative error code.
 */
extern int uart_write(int32_t handle, const uint8_t *buf, uint32_t len);

/**
 * @brief Non-blocking read from UART RX ring buffer.
 * Returns 0 immediately if no data is available; use delay() to poll.
 * @param handle    Handle from uart_open().
 * @param buf       Destination buffer.
 * @param max_len   Maximum bytes to read.
 * @return Bytes read (0 = no data), or negative error code.
 */
extern int uart_read(int32_t handle, uint8_t *buf, uint32_t max_len);

/**
 * @brief Close a UART handle and release its resources.
 * @param handle Handle from uart_open().
 * @return 0 on success, negative error code on failure.
 */
extern int uart_close(int32_t handle);

/*
 * =============================================================================
 * I2C API
 * =============================================================================
 * Required capability: i2c
 *
 * Stateless raw register access. The LSM6DS3 IMU is on bus 0 at address 0x6A.
 * All lengths are capped at 256 bytes; addresses must be 7-bit (≤ 0x7F).
 */

/**
 * @brief Write bytes to an I2C device register.
 * @param bus_id   I2C bus index (0 = i2c0, 1 = i2c1).
 * @param dev_addr 7-bit I2C device address.
 * @param reg_addr Register address to write to.
 * @param buf      Pointer to data buffer.
 * @param len      Number of bytes to write (max 256).
 * @return 0 on success, negative error code on failure.
 */
extern int i2c_write_reg(int32_t bus_id, int32_t dev_addr, int32_t reg_addr,
                          const uint8_t *buf, uint32_t len);

/**
 * @brief Read bytes from an I2C device register.
 * @param bus_id   I2C bus index (0 = i2c0, 1 = i2c1).
 * @param dev_addr 7-bit I2C device address.
 * @param reg_addr Register address to read from.
 * @param buf      Destination buffer.
 * @param len      Number of bytes to read (max 256).
 * @return Bytes read on success, negative error code on failure.
 */
extern int i2c_read_reg(int32_t bus_id, int32_t dev_addr, int32_t reg_addr,
                         uint8_t *buf, uint32_t len);

/*
 * =============================================================================
 * PWM API
 * =============================================================================
 * Required capability: pwm
 *
 * Channel-indexed PWM control. Channel 0 is the first available PWM output.
 */

/**
 * @brief Set a PWM channel's frequency and duty cycle.
 * @param channel  Logical PWM channel index (0-based).
 * @param freq_hz  Frequency in Hertz (1–10,000,000).
 * @param duty_pct Duty cycle percentage (0–100).
 * @return 0 on success, negative error code on failure.
 */
extern int pwm_set(int32_t channel, int32_t freq_hz, int32_t duty_pct);

/**
 * @brief Disable a PWM channel (output held low).
 * @param channel Logical PWM channel index.
 * @return 0 on success, negative error code on failure.
 */
extern int pwm_disable(int32_t channel);

/*
 * =============================================================================
 * HELPER MACROS
 * =============================================================================
 */

/** 
 * Note: Export functions via linker flags (-Wl,--export=main), not source attributes.
 * This avoids duplicate export errors with the build system.
 */

/*
 * =============================================================================
 * ERROR CODES
 * =============================================================================
 */

#define EPERM           1   /**< Operation not permitted */
#define ENOENT          2   /**< No such file or directory */
#define EIO             5   /**< I/O error */
#define EBADF           9   /**< Bad file descriptor */
#define ENOMEM          12  /**< Out of memory */
#define EACCES          13  /**< Permission denied */
#define EFAULT          14  /**< Bad address */
#define EBUSY           16  /**< Device or resource busy */
#define EINVAL          22  /**< Invalid argument */
#define ENOSPC          28  /**< No space left on device */
#define ETIMEDOUT       110 /**< Connection timed out */

/*
 * =============================================================================
 * STORAGE API
 * =============================================================================
 *
 * Sandboxed file I/O.  Each app is confined to its private directory:
 *   <best_mount>/apps/<app_name>/
 * Paths are relative.  ".." traversal is rejected with -EACCES.
 *
 * Required capabilities in the app manifest:
 *   "storage.read"  — storage_open(O_READ), storage_read, storage_list
 *   "storage.write" — storage_open(O_WRITE/O_APPEND), storage_write,
 *                     storage_delete
 */

/** Open flags for storage_open() */
#define STORAGE_O_READ    0   /**< Read-only; file must exist */
#define STORAGE_O_WRITE   1   /**< Write; create/truncate */
#define STORAGE_O_APPEND  2   /**< Append; create if absent */
#define STORAGE_O_RDWR    3   /**< Read + write; create/truncate */

/**
 * @brief Open a file in the app's private sandbox.
 * @param path  Relative path (e.g. "log.txt", "sub/data.bin").
 * @param flags STORAGE_O_READ / STORAGE_O_WRITE / STORAGE_O_APPEND / STORAGE_O_RDWR
 * @return Non-negative fd on success; negative errno on error.
 */
extern int storage_open(const char *path, int flags);

/**
 * @brief Read from an open storage file.
 * @param fd   Descriptor from storage_open().
 * @param buf  Destination buffer.
 * @param len  Max bytes to read.
 * @return Bytes read (0 = EOF); negative errno on error.
 */
extern int storage_read(int fd, void *buf, int len);

/**
 * @brief Write to an open storage file.
 * @param fd   Descriptor from storage_open().
 * @param buf  Source data.
 * @param len  Number of bytes to write.
 * @return Bytes written; negative errno on error.
 */
extern int storage_write(int fd, const void *buf, int len);

/**
 * @brief Close an open storage file descriptor.
 * @param fd  Descriptor to close.
 */
extern void storage_close(int fd);

/**
 * @brief Delete a file from the app's sandbox.
 * @param path  Relative path.
 * @return 0 on success; negative errno on error (-ENOENT if not found).
 */
extern int storage_delete(const char *path);

/**
 * @brief List files in a sandbox directory.
 *
 * Returns a newline-separated list of names, NUL-terminated.
 * Directories appear with a trailing '/'.
 *
 * @param path  Relative subdirectory, or "" for sandbox root.
 * @param buf   Output buffer.
 * @param len   Buffer size in bytes.
 * @return Total bytes written including NUL; negative errno on error.
 */
extern int storage_list(const char *path, char *buf, int len);

/*
 * =============================================================================
 * NETWORK API
 * =============================================================================
 * Required capability: network
 *
 * Data flows through shared-memory ring buffers that live in WASM linear memory.
 * Only net_tx_flush() / net_event_pop() require a host call; bulk data never does.
 *
 * Ring buffer layout (TX and RX buffers use the same format):
 *
 *   Byte  0-3  : write_idx  (uint32 LE) — producer increments after writing
 *   Byte  4-7  : read_idx   (uint32 LE) — consumer increments after reading
 *   Byte  8-11 : capacity   (uint32 LE) — data area size; set by host at bind
 *   Byte 12-15 : flags      (uint32 LE) — reserved, must be 0
 *   Byte 16+   : data area (capacity bytes)
 *
 * Each message in the data area: [len_lo][len_hi][payload bytes…]
 * Indices are monotonically increasing; actual byte = data[(idx % capacity)].
 *
 * Event buffer layout returned by net_event_pop():
 *   Byte 0   : event type (NET_EVT_*)
 *   Byte 1   : stream handle
 *   Byte 2-3 : extra (uint16 LE) — new handle for NET_EVT_ACCEPT, errno for NET_EVT_ERROR
 */

/** @brief Socket type: TCP stream */
#define NET_TYPE_TCP  0
/** @brief Socket type: UDP datagram */
#define NET_TYPE_UDP  1

/** @brief Event: no events in queue */
#define NET_EVT_NONE         0
/** @brief Event: TCP handshake complete (or UDP default peer set) */
#define NET_EVT_CONNECTED    1
/** @brief Event: peer closed the connection or a socket error occurred */
#define NET_EVT_DISCONNECTED 2
/** @brief Event: host wrote data into the RX ring — start reading */
#define NET_EVT_DATA_READY   3
/** @brief Event: new inbound connection accepted; extra = new stream handle */
#define NET_EVT_ACCEPT       4
/** @brief Event: socket error; extra = errno */
#define NET_EVT_ERROR        5

/** @brief Ring buffer header size in bytes (must match host-side NET_RING_HDR_SIZE) */
#define NET_RING_HDR_SIZE  16

/*
 * Helper: write one framed message into a TX ring buffer.
 * Returns data_len on success, -1 if the ring is full.
 *
 * Usage example:
 *   static uint8_t tx_buf[2048];
 *   net_tx_bind(h, tx_buf, sizeof(tx_buf));
 *   net_ring_write(tx_buf, sizeof(tx_buf), "ping", 4);
 *   net_tx_flush(h);
 */
static inline int net_ring_write(uint8_t *ring_buf, int buf_size,
                                  const uint8_t *data, int data_len)
{
    if (!ring_buf || data_len <= 0 || data_len > 0xFFFF) {
        return -1;
    }
    volatile uint32_t *wi_ptr  = (volatile uint32_t *)(ring_buf + 0);
    volatile uint32_t *ri_ptr  = (volatile uint32_t *)(ring_buf + 4);
    volatile uint32_t *cap_ptr = (volatile uint32_t *)(ring_buf + 8);
    uint32_t cap  = *cap_ptr;
    uint32_t wi   = *wi_ptr;
    uint32_t ri   = *ri_ptr;
    uint32_t used = wi - ri;
    uint32_t need = (uint32_t)(2 + data_len);
    if (cap == 0 || (cap - used) < need) {
        return -1;  /* ring full */
    }
    uint8_t *dat = ring_buf + NET_RING_HDR_SIZE;
    uint32_t wi_mod = wi % cap;
    dat[wi_mod]             = (uint8_t)(data_len & 0xFF);
    dat[(wi_mod + 1) % cap] = (uint8_t)((data_len >> 8) & 0xFF);
    uint32_t data_wi = (wi_mod + 2) % cap;
    for (int i = 0; i < data_len; i++) {
        dat[(data_wi + i) % cap] = data[i];
    }
    /* Atomic store: ensure payload is visible before write_idx update */
    __atomic_store_n(wi_ptr, wi + need, __ATOMIC_RELEASE);
    return data_len;
}

/*
 * Helper: read one framed message from an RX ring buffer.
 * Returns bytes read (0 = no data yet, -1 = output buffer too small).
 *
 * Usage example:
 *   static uint8_t rx_buf[2048];
 *   net_rx_bind(h, rx_buf, sizeof(rx_buf));
 *   // after NET_EVT_DATA_READY:
 *   uint8_t msg[256];
 *   int n = net_ring_read(rx_buf, sizeof(rx_buf), msg, sizeof(msg));
 */
static inline int net_ring_read(uint8_t *ring_buf, int buf_size,
                                 uint8_t *out, int out_len)
{
    if (!ring_buf || !out) {
        return -1;
    }
    volatile uint32_t *wi_ptr  = (volatile uint32_t *)(ring_buf + 0);
    volatile uint32_t *ri_ptr  = (volatile uint32_t *)(ring_buf + 4);
    volatile uint32_t *cap_ptr = (volatile uint32_t *)(ring_buf + 8);
    uint32_t cap  = *cap_ptr;
    uint32_t wi   = __atomic_load_n(wi_ptr, __ATOMIC_ACQUIRE);
    uint32_t ri   = *ri_ptr;
    uint32_t used = wi - ri;
    if (cap == 0 || used < 2) {
        return 0;  /* no data */
    }
    uint8_t *dat = ring_buf + NET_RING_HDR_SIZE;
    uint32_t ri_mod = ri % cap;
    uint16_t plen   = (uint16_t)dat[ri_mod] |
                      ((uint16_t)dat[(ri_mod + 1) % cap] << 8);
    if (used < (uint32_t)(2 + plen)) {
        return 0;  /* incomplete message */
    }
    if ((int)plen > out_len) {
        return -1;  /* output buffer too small */
    }
    uint32_t data_ri = (ri_mod + 2) % cap;
    for (uint16_t i = 0; i < plen; i++) {
        out[i] = dat[(data_ri + i) % cap];
    }
    /* Atomic store: signal that we consumed the message */
    __atomic_store_n(ri_ptr, ri + 2 + plen, __ATOMIC_RELEASE);
    return (int)plen;
}

/**
 * @brief Open a new TCP or UDP socket.
 * @param type  NET_TYPE_TCP or NET_TYPE_UDP.
 * @return Stream handle (>=0) on success; negative errno on failure.
 */
extern int net_open(int32_t type);

/**
 * @brief Initiate an async connection to host:port.
 *
 * DNS lookup and connect() happen on a background thread.
 * Poll net_event_pop() for NET_EVT_CONNECTED or NET_EVT_ERROR.
 * For UDP, sets the default peer (no handshake occurs).
 *
 * @param handle  Stream handle from net_open().
 * @param host    Null-terminated hostname or IPv4 dotted-decimal string.
 * @param port    Remote port (1–65535).
 * @return 0 if queued; negative errno on failure.
 */
extern int net_connect(int32_t handle, const char *host, int32_t port);

/**
 * @brief Bind a socket to a local port (required for servers).
 * @param handle  Stream handle.
 * @param port    Local port (0 = OS-assigned).
 * @return 0 on success; negative errno on failure.
 */
extern int net_bind(int32_t handle, int32_t port);

/**
 * @brief Mark a TCP socket as listening for inbound connections.
 *
 * On each accepted connection, net_event_pop() returns NET_EVT_ACCEPT
 * with the new stream handle in bytes 2–3 (extra field).
 *
 * @param handle   Stream handle.
 * @param backlog  Accept queue depth.
 * @return 0 on success; negative errno on failure.
 */
extern int net_listen(int32_t handle, int32_t backlog);

/**
 * @brief Close a socket and release its slot.
 * @param handle  Stream handle.
 * @return 0 on success; negative errno on failure.
 */
extern int net_close(int32_t handle);

/**
 * @brief Bind a WASM buffer as the TX ring for a stream.
 *
 * Call once after net_open(). Write messages into the buffer using
 * net_ring_write(), then call net_tx_flush() to send them.
 *
 * @param handle      Stream handle.
 * @param tx_buf      Pointer to the WASM buffer (>= NET_RING_HDR_SIZE + 1 byte).
 * @param total_size  Total buffer size in bytes.
 * @return 0 on success; negative errno on failure.
 */
extern int net_tx_bind(int32_t handle, void *tx_buf, int32_t total_size);

/**
 * @brief Bind a WASM buffer as the RX ring for a stream.
 *
 * Call once after net_open(). Read messages from the buffer using
 * net_ring_read() after NET_EVT_DATA_READY.
 *
 * @param handle      Stream handle.
 * @param rx_buf      Pointer to the WASM buffer (>= NET_RING_HDR_SIZE + 1 byte).
 * @param total_size  Total buffer size in bytes.
 * @return 0 on success; negative errno on failure.
 */
extern int net_rx_bind(int32_t handle, void *rx_buf, int32_t total_size);

/**
 * @brief Flush the TX ring immediately without waiting for the next poll tick.
 *
 * The host poll thread drains the TX ring automatically every ~10 ms.
 * Call this for lower-latency sends.
 *
 * @param handle  Stream handle.
 * @return Bytes sent on success; negative errno on failure.
 */
extern int net_tx_flush(int32_t handle);

/**
 * @brief Pop the next network event (non-blocking).
 *
 * Event buffer layout:
 *   buf[0] : event type (NET_EVT_*)
 *   buf[1] : stream handle
 *   buf[2] : extra low byte  (little-endian uint16)
 *   buf[3] : extra high byte — new stream handle (ACCEPT), errno (ERROR)
 *
 * @param buf  Destination buffer (must be >= 4 bytes).
 * @param len  Buffer capacity in bytes.
 * @return Event type (NET_EVT_*; >0) if available, 0 if queue empty.
 */
extern int net_event_pop(void *buf, int32_t len);

/*
 * =============================================================================
 * POWER MANAGEMENT API
 * =============================================================================
 *
 * Required manifest capabilities:
 *   "power.read"   — power_get_mode, power_get_battery_level,
 *                    power_get_battery_status
 *   "power.control"— power_set_mode, power_wake_on_gpio,
 *                    power_wake_on_timer, power_set_low_power
 *                    (elevated — only works when host has
 *                     CONFIG_AKIRA_WASM_POWER_CONTROL=y)
 */

/** @brief System power modes */
#define POWER_MODE_ACTIVE       0
#define POWER_MODE_IDLE         1
#define POWER_MODE_LIGHT_SLEEP  2
#define POWER_MODE_DEEP_SLEEP   3
#define POWER_MODE_HIBERNATE    4

/** @brief Battery status buffer flags (buf[1] from power_get_battery_status) */
#define BATT_FLAG_CHARGING      (1 << 0)
#define BATT_FLAG_LOW_BATTERY   (1 << 1)

/**
 * @brief Unpack a battery status buffer filled by power_get_battery_status().
 *
 * Example:
 *   uint8_t buf[12];
 *   power_get_battery_status(buf, sizeof(buf));
 *   int pct = buf[0];
 *   bool charging = buf[1] & BATT_FLAG_CHARGING;
 *   int32_t mv; memcpy(&mv, buf + 4, 4);
 */

/**
 * @brief Return the current power mode (POWER_MODE_* constant).
 * @return Power mode, or -EACCES if permission denied.
 */
extern int power_get_mode(void);

/**
 * @brief Read battery state of charge.
 * @return 0-100 %, -ENODEV if no battery present, -EACCES if denied.
 */
extern int power_get_battery_level(void);

/**
 * @brief Read full battery status into @p buf (12 bytes required).
 *
 * Buffer layout:
 *   [0]     uint8  level_percent (0-100)
 *   [1]     uint8  flags (BATT_FLAG_*)
 *   [2-3]   pad
 *   [4-7]   int32  voltage_mv (little-endian)
 *   [8-11]  int32  current_ma (little-endian, positive=charge, negative=discharge)
 *
 * @return 0 on success, negative errno on error.
 */
extern int power_get_battery_status(void *buf, int len);

/**
 * @brief Request a power mode transition.
 *
 * Deep sleep and hibernate require the host to have
 * CONFIG_AKIRA_POWER_DEEP_SLEEP=y; otherwise returns -ENOTSUP.
 *
 * @param mode POWER_MODE_* constant.
 * @return 0 on success.
 */
extern int power_set_mode(int mode);

/**
 * @brief Register a GPIO pin as a wake source before deep sleep.
 * @param pin  GPIO pin number.
 * @param edge 0=low level, 1=high level, 2=any edge.
 * @return 0 on success.
 */
extern int power_wake_on_gpio(int pin, int edge);

/**
 * @brief Register a timer wake source before deep sleep.
 * @param ms Wake delay in milliseconds (must be > 0).
 * @return 0 on success, -EINVAL if ms == 0.
 */
extern int power_wake_on_timer(int ms);

/**
 * @brief Enable or disable automatic low-power idle.
 * @param enable 1 to enable, 0 to disable.
 * @return 0 always.
 */
extern int power_set_low_power(int enable);


#ifdef __cplusplus
}
#endif

#endif /* AKIRA_API_H */
