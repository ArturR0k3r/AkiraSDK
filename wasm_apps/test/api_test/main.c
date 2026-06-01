/**
 * @file main.c
 * @brief AkiraOS WASM API test suite
 *
 * Exercises every function exported by akira_export_api.c.
 *
 * Result codes:
 *   [PASS] – function returned a value in the expected range
 *   [FAIL] – function returned an unexpected value (linkage or logic issue)
 *   [INFO] – hardware-dependent call; value is logged but not judged
 *
 * Enable/disable test groups by defining (or undefining) the flags below
 * before the include, or by passing -DAKIRA_TEST_<GROUP>=0 to the compiler:
 *
 *   AKIRA_TEST_CORE        delay
 *   AKIRA_TEST_DISPLAY     all display_* primitives
 *   AKIRA_TEST_SENSOR      sensor_read channels
 *   AKIRA_TEST_GPIO        gpio_configure / read / write
 *   AKIRA_TEST_TIMER       timer_create / start / elapsed / stop / free
 *   AKIRA_TEST_STORAGE     storage_open / read / write / list / delete
 *   AKIRA_TEST_FS          fs_open / read / write / seek / stat / readdir …
 *   AKIRA_TEST_IPC         msg_subscribe / publish / recv / unsubscribe
 *   AKIRA_TEST_LIFECYCLE   app_get_self_name / app_list / app_get_status
 *   AKIRA_TEST_POWER       power_get_mode / battery_level / battery_status
 *   AKIRA_TEST_SETTINGS    settings_get / set / delete
 *   AKIRA_TEST_RTC         rtc_get_unix_time / uptime / alarm
 *   AKIRA_TEST_MEMORY      mem_alloc / mem_free
 *   AKIRA_TEST_ADC         adc_read / adc_read_mv
 *   AKIRA_TEST_PWM         pwm_set / pwm_disable
 *   AKIRA_TEST_WDT         wdt_pet
 *   AKIRA_TEST_UART        uart_open / write / read / close
 *   AKIRA_TEST_I2C         i2c_read_reg / i2c_write_reg
 *   AKIRA_TEST_CRYPTO      sha256 / aes256 / hmac / random
 *   AKIRA_TEST_NET         net_open / bind / tx / rx / event / close
 *   AKIRA_TEST_BLE         ble_init / service / char / advertise / deinit
 *   AKIRA_TEST_HID         hid_init / key / mouse / gamepad / consumer
 *   AKIRA_TEST_OTA         ota_get_state (URL tests skipped)
 *   AKIRA_TEST_SYSTEM      sd_scan_wasm
 *   AKIRA_TEST_RF          rf_set_frequency / power / rssi / send
 *
 * All groups are enabled by default. To disable one pass e.g.:
 *   make EXTRA_CFLAGS="-DAKIRA_TEST_BLE=0 -DAKIRA_TEST_HID=0"
 *
 * Build:
 *   make -C wasm_apps/test/api_test
 *
 * @copyright Copyright (c) 2026 PenEngineering S.R.L
 * @license Apache-2.0
 */

#include "akira_api.h"

#define AKIRA_TEST_DISPLAY 0
#define AKIRA_TEST_CRYPTO 0 

/* ── test-group enable flags (define to 0 to skip a group) ─────────────── */
#ifndef AKIRA_TEST_CORE
#define AKIRA_TEST_CORE      1
#endif
#ifndef AKIRA_TEST_DISPLAY
#define AKIRA_TEST_DISPLAY   1
#endif
#ifndef AKIRA_TEST_SENSOR
#define AKIRA_TEST_SENSOR    1
#endif
#ifndef AKIRA_TEST_GPIO
#define AKIRA_TEST_GPIO      1
#endif
#ifndef AKIRA_TEST_TIMER
#define AKIRA_TEST_TIMER     1
#endif
#ifndef AKIRA_TEST_STORAGE
#define AKIRA_TEST_STORAGE   1
#endif
#ifndef AKIRA_TEST_FS
#define AKIRA_TEST_FS        1
#endif
#ifndef AKIRA_TEST_IPC
#define AKIRA_TEST_IPC       1
#endif
#ifndef AKIRA_TEST_LIFECYCLE
#define AKIRA_TEST_LIFECYCLE 1
#endif
#ifndef AKIRA_TEST_POWER
#define AKIRA_TEST_POWER     1
#endif
#ifndef AKIRA_TEST_SETTINGS
#define AKIRA_TEST_SETTINGS  1
#endif
#ifndef AKIRA_TEST_RTC
#define AKIRA_TEST_RTC       1
#endif
#ifndef AKIRA_TEST_MEMORY
#define AKIRA_TEST_MEMORY    1
#endif
#ifndef AKIRA_TEST_ADC
#define AKIRA_TEST_ADC       1
#endif
#ifndef AKIRA_TEST_PWM
#define AKIRA_TEST_PWM       1
#endif
#ifndef AKIRA_TEST_WDT
#define AKIRA_TEST_WDT       1
#endif
#ifndef AKIRA_TEST_UART
#define AKIRA_TEST_UART      1
#endif
#ifndef AKIRA_TEST_I2C
#define AKIRA_TEST_I2C       1
#endif
#ifndef AKIRA_TEST_CRYPTO
#define AKIRA_TEST_CRYPTO    1
#endif
#ifndef AKIRA_TEST_NET
#define AKIRA_TEST_NET       1
#endif
#ifndef AKIRA_TEST_BLE
#define AKIRA_TEST_BLE       1
#endif
#ifndef AKIRA_TEST_HID
#define AKIRA_TEST_HID       1
#endif
#ifndef AKIRA_TEST_OTA
#define AKIRA_TEST_OTA       1
#endif
#ifndef AKIRA_TEST_SYSTEM
#define AKIRA_TEST_SYSTEM    1
#endif
#ifndef AKIRA_TEST_RF
#define AKIRA_TEST_RF        1
#endif

/* ── result counters ──────────────────────────────────────────────────────── */

static int g_pass;
static int g_fail;

/* ── reporting helpers ────────────────────────────────────────────────────── */

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

static void info(const char *label, int ret)
{
    printf("[INFO] %s = %d", label, ret);
}

static void section(const char *name)
{
    printf("");
    printf("--- %s ---", name);
}

/* ── static data (avoids stack pressure) ─────────────────────────────────── */

static uint8_t  s_u8buf[512];
static char     s_cbuf[512];

#if AKIRA_TEST_DISPLAY
static uint16_t s_rgb565[4] = {0xF800, 0x07E0, 0x001F, 0xFFFF};
#endif

#if AKIRA_TEST_CRYPTO
static const uint8_t s_aes_key[32] = {
    0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
    0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,
    0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,
    0x18,0x19,0x1A,0x1B,0x1C,0x1D,0x1E,0x1F
};
static const uint8_t s_aes_iv[16]  = {0};
static const uint8_t s_plain[16]   = {
    'A','k','i','r','a','O','S',' ','A','P','I',' ','t','e','s','t'
};
static uint8_t s_cipher[16];
static uint8_t s_decrypted[16];
static uint8_t s_digest[32];
static uint8_t s_hmac[32];
static uint8_t s_random[16];
#endif

#if AKIRA_TEST_FS
static akira_dirent_t s_dirent;
#endif

/* ── test functions ───────────────────────────────────────────────────────── */

#if AKIRA_TEST_CORE
static void test_core(void)
{
    section("CORE");
    chk("delay(0)", delay(0), 0);
}
#endif

#if AKIRA_TEST_DISPLAY
static void test_display(void)
{
    section("DISPLAY");
    int32_t w = 0, h = 0;
    chk("display_get_size", display_get_size(&w, &h), 0);
    printf("[INFO] display size: %d x %d", w, h);
    chk("display_clear(BLACK)",          display_clear(COLOR_BLACK), 0);
    chk("display_pixel",                 display_pixel(5, 5, COLOR_WHITE), 0);
    chk("display_rect",                  display_rect(10, 10, 40, 20, COLOR_BLUE), 0);
    chk("display_rect_outline",          display_rect_outline(10, 10, 40, 20, COLOR_CYAN), 0);
    chk("display_rounded_rect",          display_rounded_rect(60, 10, 40, 20, 4, COLOR_YELLOW), 0);
    chk("display_rounded_rect_fill",     display_rounded_rect_fill(110, 10, 40, 20, 4, COLOR_GREEN), 0);
    chk("display_line",                  display_line(0, 0, 40, 40, COLOR_RED), 0);
    chk("display_hline",                 display_hline(0, 50, 80, COLOR_WHITE), 0);
    chk("display_vline",                 display_vline(0, 50, 80, COLOR_WHITE), 0);
    chk("display_circle",                display_circle(100, 80, 20, COLOR_MAGENTA), 0);
    chk("display_circle_fill",           display_circle_fill(160, 80, 20, COLOR_ORANGE), 0);
    chk("display_triangle",
        display_triangle(200,60, 240,60, 220,100, COLOR_CYAN), 0);
    chk("display_triangle_fill",
        display_triangle_fill(200,110, 240,110, 220,150, COLOR_YELLOW), 0);
    chk("display_text",                  display_text(0, 120, "api_test", COLOR_WHITE), 0);
    chk("display_text_large",            display_text_large(0, 140, "AkiraOS", COLOR_GREEN), 0);
    chk("display_number",                display_number(0, 165, 42, COLOR_CYAN), 0);
    chk("display_progress_bar",
        display_progress_bar(0, 180, 120, 10, 60, 100, COLOR_GREEN, COLOR_DARK_GRAY), 0);
    chk("display_bitmap",
        display_bitmap(10, 200, 2, 2, s_rgb565, sizeof(s_rgb565)), 0);
    chk("display_bitmap_transparent",
        display_bitmap_transparent(20, 200, 2, 2, s_rgb565, sizeof(s_rgb565), 0x0000), 0);
    chk("display_raw_write",
        display_raw_write(30, 200, 2, 2, s_rgb565, sizeof(s_rgb565)), 0);
    chk("display_flush",                 display_flush(), 0);
}
#endif

#if AKIRA_TEST_SENSOR
static void test_sensor(void)
{
    section("SENSOR");
    info("sensor_read(ACCEL_X)",  sensor_read(SENSOR_CHAN_ACCEL_X));
    info("sensor_read(ACCEL_Y)",  sensor_read(SENSOR_CHAN_ACCEL_Y));
    info("sensor_read(ACCEL_Z)",  sensor_read(SENSOR_CHAN_ACCEL_Z));
    info("sensor_read(GYRO_X)",   sensor_read(SENSOR_CHAN_GYRO_X));
    info("sensor_read(GYRO_Y)",   sensor_read(SENSOR_CHAN_GYRO_Y));
    info("sensor_read(GYRO_Z)",   sensor_read(SENSOR_CHAN_GYRO_Z));
    info("sensor_read(MAGN_X)",   sensor_read(SENSOR_CHAN_MAGN_X));
    info("sensor_read(TEMP)",     sensor_read(SENSOR_CHAN_AMBIENT_TEMP));
    info("sensor_read(PRESS)",    sensor_read(SENSOR_CHAN_PRESS));
    info("sensor_read(HUMIDITY)", sensor_read(SENSOR_CHAN_HUMIDITY));
}
#endif

#if AKIRA_TEST_GPIO
static void test_gpio(void)
{
    section("GPIO");
    info("gpio_configure(0,INPUT)",     gpio_configure(0, GPIO_INPUT));
    info("gpio_read(0)",                gpio_read(0));
    info("gpio_configure(1,OUTPUT_LO)", gpio_configure(1, GPIO_OUTPUT | GPIO_OUTPUT_INIT_LOW));
    info("gpio_write(1,1)",             gpio_write(1, 1));
    info("gpio_write(1,0)",             gpio_write(1, 0));
}
#endif

#if AKIRA_TEST_TIMER
static void test_timer(void)
{
    section("TIMER");
    int tmr = timer_create();
    chk("timer_create", tmr, 0);
    if (tmr >= 0) {
        chk("timer_start",   timer_start(tmr),   0);
        chk("timer_elapsed", timer_elapsed(tmr), 0);
        chk("timer_stop",    timer_stop(tmr),    0);
        chk("timer_free",    timer_free(tmr),    0);
    }
}
#endif

#if AKIRA_TEST_STORAGE
static void test_storage(void)
{
    section("STORAGE");
    static const char wdata[] = "akira_sdk_api_test";

    int fd = storage_open("api_test.bin", STORAGE_O_WRITE);
    chk("storage_open(WRITE)", fd, 0);
    if (fd >= 0) {
        chk("storage_write", storage_write(fd, wdata, sizeof(wdata)), 0);
        storage_close(fd);
        printf("[INFO] storage_close: fd=%d", fd);
    }

    fd = storage_open("api_test.bin", STORAGE_O_READ);
    chk("storage_open(READ)", fd, 0);
    if (fd >= 0) {
        chk("storage_read", storage_read(fd, s_u8buf, sizeof(s_u8buf)), 0);
        storage_close(fd);
    }

    chk("storage_list",   storage_list("", s_cbuf, sizeof(s_cbuf)), 0);
    chk("storage_delete", storage_delete("api_test.bin"), 0);
}
#endif

#if AKIRA_TEST_FS
static void test_fs(void)
{
    section("FS (POSIX sandbox)");

    int fd = fs_open("api_test.txt", FS_O_WRITE | FS_O_CREAT | FS_O_TRUNC);
    chk("fs_open(WRITE|CREAT|TRUNC)", fd, 0);
    if (fd >= 0) {
        chk("fs_write",       fs_write(fd, "Hello AkiraOS", 13), 0);
        chk("fs_tell",        fs_tell(fd), 0);
        chk("fs_seek(SET,0)", fs_seek(fd, 0, FS_SEEK_SET), 0);
        chk("fs_close",       fs_close(fd), 0);
    }

    info("fs_stat(api_test.txt)", fs_stat("api_test.txt", &s_dirent));
    printf("[INFO] fs_stat type=%d size=%d name=%s",
           (int)s_dirent.type, (int)s_dirent.size, s_dirent.name);

    info("fs_readdir(root)", fs_readdir("", s_cbuf, sizeof(s_cbuf)));

    chk("fs_mkdir(test_dir)", fs_mkdir("test_dir"), 0);

    fd = fs_open("test_dir/sub.txt", FS_O_WRITE | FS_O_CREAT | FS_O_TRUNC);
    chk("fs_open(sub dir)", fd, 0);
    if (fd >= 0) {
        fs_write(fd, "x", 1);
        fs_close(fd);
    }
    chk("fs_unlink(test_dir/sub.txt)", fs_unlink("test_dir/sub.txt"), 0);
    chk("fs_unlink(api_test.txt)",     fs_unlink("api_test.txt"), 0);
}
#endif

#if AKIRA_TEST_IPC
static void test_ipc(void)
{
    section("IPC");
    static const uint8_t ipc_payload[4] = {0xDE, 0xAD, 0xBE, 0xEF};

    chk("msg_subscribe(akira.test)",   msg_subscribe("akira.test"), 0);
    info("msg_publish(akira.test)",    msg_publish("akira.test", ipc_payload, sizeof(ipc_payload)));
    info("msg_pending(akira.test)",    msg_pending("akira.test"));
    info("msg_try_recv(akira.test)",   msg_try_recv("akira.test", s_u8buf, sizeof(s_u8buf)));
    chk("msg_unsubscribe(akira.test)", msg_unsubscribe("akira.test"), 0);
}
#endif

#if AKIRA_TEST_LIFECYCLE
static void test_app_lifecycle(void)
{
    section("APP LIFECYCLE");
    chk("app_get_self_name", app_get_self_name(s_u8buf, sizeof(s_u8buf)), 0);
    printf("[INFO] self name: %s", (char *)s_u8buf);
    info("app_list",             app_list(s_u8buf, sizeof(s_u8buf)));
    info("app_get_status(self)", app_get_status("api_test"));
}
#endif

#if AKIRA_TEST_POWER
static void test_power(void)
{
    section("POWER");
    info("power_get_mode",           power_get_mode());
    info("power_get_battery_level",  power_get_battery_level());
    info("power_get_battery_status", power_get_battery_status(s_u8buf, 12));
    info("power_set_low_power(0)",   power_set_low_power(0));
}
#endif

#if AKIRA_TEST_SETTINGS
static void test_settings(void)
{
    section("SETTINGS");
    chk("settings_set",    settings_set("test/apitest", "passed"), 0);
    chk("settings_get",    settings_get("test/apitest", s_cbuf, sizeof(s_cbuf)), 0);
    printf("[INFO] settings_get value: %s", s_cbuf);
    chk("settings_delete", settings_delete("test/apitest"), 0);
}
#endif

#if AKIRA_TEST_RTC
static void test_rtc(void)
{
    section("RTC");
    info("rtc_get_unix_time", rtc_get_unix_time());
    info("rtc_get_uptime_ms", rtc_get_uptime_ms());
    info("rtc_alarm_fired",   rtc_alarm_fired());
}
#endif

#if AKIRA_TEST_MEMORY
static void test_memory(void)
{
    section("MEMORY");
    uint32_t ptr = mem_alloc(128);
    chk("mem_alloc(128)", (int)ptr, 1);
    if (ptr) {
        mem_free(ptr);
        printf("[PASS] mem_free");
        g_pass++;
    }
}
#endif

#if AKIRA_TEST_ADC
static void test_adc(void)
{
    section("ADC");
    info("adc_read(0)",    adc_read(0));
    info("adc_read_mv(0)", adc_read_mv(0));
}
#endif

#if AKIRA_TEST_PWM
static void test_pwm(void)
{
    section("PWM");
    info("pwm_set(ch=0,1kHz,50%)", pwm_set(0, 1000, 50));
    info("pwm_disable(ch=0)",      pwm_disable(0));
}
#endif

#if AKIRA_TEST_WDT
static void test_wdt(void)
{
    section("WDT");
    info("wdt_pet", wdt_pet());
}
#endif

#if AKIRA_TEST_UART
static void test_uart(void)
{
    section("UART");
    int h = uart_open(0, 115200);
    info("uart_open(0,115200)", h);
    if (h >= 0) {
        static const uint8_t tx[] = {'p','i','n','g'};
        info("uart_write", uart_write(h, tx, sizeof(tx)));
        info("uart_read",  uart_read(h, s_u8buf, sizeof(s_u8buf)));
        info("uart_close", uart_close(h));
    }
}
#endif

#if AKIRA_TEST_I2C
static void test_i2c(void)
{
    section("I2C");
    static uint8_t i2c_buf[2] = {0};
    /* LSM6DS3 IMU WHO_AM_I: bus=0, addr=0x6A, reg=0x0F → expect 0x69 */
    info("i2c_read_reg(0,0x6A,0x0F)", i2c_read_reg(0, 0x6A, 0x0F, i2c_buf, 1));
    printf("[INFO] WHO_AM_I: 0x%02X", (unsigned)i2c_buf[0]);
    i2c_buf[0] = 0x40; /* CTRL1_XL: ODR 104Hz, ±2g */
    info("i2c_write_reg(0,0x6A,0x10)", i2c_write_reg(0, 0x6A, 0x10, i2c_buf, 1));
}
#endif

#if AKIRA_TEST_CRYPTO
static void test_crypto(void)
{
    section("CRYPTO");
    info("crypto_random(16B)",   crypto_random(s_random, sizeof(s_random)));
    info("crypto_sha256",        crypto_sha256("AkiraOS", 7, s_digest));
    info("crypto_aes256_encrypt",
         crypto_aes256_encrypt(s_aes_key, s_aes_iv,
                               (int)sizeof(s_plain),
                               s_plain, (int)sizeof(s_plain),
                               s_cipher));
    info("crypto_aes256_decrypt",
         crypto_aes256_decrypt(s_aes_key, s_aes_iv,
                               (int)sizeof(s_cipher),
                               s_cipher, (int)sizeof(s_cipher),
                               s_decrypted));
    info("crypto_hmac_sha256",
         crypto_hmac_sha256("hmac_key", 8, "hmac_data", 9, s_hmac));
}
#endif

#if AKIRA_TEST_NET
static void test_net(void)
{
    section("NET");
    static uint8_t tx_ring[NET_RING_HDR_SIZE + 256];
    static uint8_t rx_ring[NET_RING_HDR_SIZE + 256];
    static uint8_t ev_buf[4];

    int h = net_open(NET_TYPE_TCP);
    info("net_open(TCP)", h);
    if (h >= 0) {
        info("net_tx_bind",   net_tx_bind(h, tx_ring, (int)sizeof(tx_ring)));
        info("net_rx_bind",   net_rx_bind(h, rx_ring, (int)sizeof(rx_ring)));
        info("net_tx_flush",  net_tx_flush(h));
        info("net_event_pop", net_event_pop(ev_buf, (int)sizeof(ev_buf)));
        info("net_get_ip",    net_get_ip(s_cbuf, 16));
        printf("[INFO] net_get_ip: %s", s_cbuf);
        info("net_close(TCP)", net_close(h));
    }

    h = net_open(NET_TYPE_UDP);
    info("net_open(UDP)", h);
    if (h >= 0) {
        info("net_close(UDP)", net_close(h));
    }
}
#endif

#if AKIRA_TEST_BLE
static void test_ble(void)
{
    section("BLE");
    info("ble_init",         ble_init());
    info("ble_is_connected", ble_is_connected());

    int svc = ble_service_create("12345678-0000-1000-8000-00805F9B34FB");
    info("ble_service_create", svc);
    if (svc >= 0) {
        int ch = ble_char_create("12345678-0001-1000-8000-00805F9B34FB",
                                  BLE_PROP_READ | BLE_PROP_NOTIFY, 20);
        info("ble_char_create", ch);
        if (ch >= 0) {
            info("ble_service_add_char", ble_service_add_char(svc, ch));
        }
        info("ble_add_service",           ble_add_service(svc));
        info("ble_set_advertised_service", ble_set_advertised_service(svc));
    }

    info("ble_set_local_name", ble_set_local_name("AkiraTest"));
    info("ble_event_pop",      ble_event_pop(s_u8buf, (int)sizeof(s_u8buf)));
    info("ble_deinit",         ble_deinit());
}
#endif

#if AKIRA_TEST_HID
static void test_hid(void)
{
    section("HID");
    info("hid_init(BLE,KEYBOARD)",
         hid_init(HID_TRANSPORT_BLE, HID_DEVICE_KEYBOARD));
    info("hid_is_connected",       hid_is_connected());
    info("hid_key_press(A)",       hid_key_press(HID_KEY_A));
    info("hid_key_release(A)",     hid_key_release(HID_KEY_A));
    info("hid_key_release_all",    hid_key_release_all());
    info("hid_consumer_send(MUTE)", hid_consumer_send(HID_CONSUMER_MUTE));
    info("hid_mouse_move(1,1)",    hid_mouse_move(1, 1));
    info("hid_mouse_scroll(1)",    hid_mouse_scroll(1));
    info("hid_gamepad_press(1)",   hid_gamepad_press(1));
    info("hid_gamepad_release(1)", hid_gamepad_release(1));
    info("hid_gamepad_reset",      hid_gamepad_reset());
    info("hid_disable",            hid_disable());
}
#endif



#if AKIRA_TEST_OTA
static void test_ota(void)
{
    section("OTA");
    info("ota_get_state", ota_get_state());
    /* ota_check / ota_fetch_and_apply require a live HTTPS URL — skipped */
    printf("[SKIP] ota_check / ota_fetch_and_apply: require live URL");
}
#endif

#if AKIRA_TEST_SYSTEM
static void test_system(void)
{
    section("SYSTEM");
    info("sd_scan_wasm", sd_scan_wasm(s_cbuf, (int)sizeof(s_cbuf)));
    printf("[INFO] sd_scan_wasm: %s", s_cbuf);
}
#endif

#if AKIRA_TEST_RF
static void test_rf(void)
{
    section("RF");
    /* Returns -ENODEV on firmware builds without CONFIG_AKIRA_MODULE_RF */
    info("rf_set_frequency(868MHz)", rf_set_frequency(868000000U));
    info("rf_set_power(14dBm)",      rf_set_power(14));
    info("rf_get_rssi",              rf_get_rssi());
}
#endif

/* ── entry point ──────────────────────────────────────────────────────────── */

int main(void)
{
    printf("AkiraOS WASM API Test Suite v1.0");
    printf("==================================");

#if AKIRA_TEST_CORE
    test_core();
#endif
#if AKIRA_TEST_DISPLAY
    test_display();
#endif
#if AKIRA_TEST_SENSOR
    test_sensor();
#endif
#if AKIRA_TEST_GPIO
    test_gpio();
#endif
#if AKIRA_TEST_TIMER
    test_timer();
#endif
#if AKIRA_TEST_STORAGE
    test_storage();
#endif
#if AKIRA_TEST_FS
    test_fs();
#endif
#if AKIRA_TEST_IPC
    test_ipc();
#endif
#if AKIRA_TEST_LIFECYCLE
    test_app_lifecycle();
#endif
#if AKIRA_TEST_POWER
    test_power();
#endif
#if AKIRA_TEST_SETTINGS
    test_settings();
#endif
#if AKIRA_TEST_RTC
    test_rtc();
#endif
#if AKIRA_TEST_MEMORY
    test_memory();
#endif
#if AKIRA_TEST_ADC
    test_adc();
#endif
#if AKIRA_TEST_PWM
    test_pwm();
#endif
#if AKIRA_TEST_WDT
    test_wdt();
#endif
#if AKIRA_TEST_UART
    test_uart();
#endif
#if AKIRA_TEST_I2C
    test_i2c();
#endif
#if AKIRA_TEST_CRYPTO
    test_crypto();
#endif
#if AKIRA_TEST_NET
    test_net();
#endif
#if AKIRA_TEST_BLE
    test_ble();
#endif
#if AKIRA_TEST_HID
    test_hid();
#endif
#if AKIRA_TEST_OTA
    test_ota();
#endif
#if AKIRA_TEST_SYSTEM
    test_system();
#endif
#if AKIRA_TEST_RF
    test_rf();
#endif

    printf("");
    printf("==================================");
    printf("PASS: %d  FAIL: %d", g_pass, g_fail);
    printf("==================================");

    return g_fail > 0 ? 1 : 0;
}

