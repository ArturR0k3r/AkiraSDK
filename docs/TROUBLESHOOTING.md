# 🔧 Akira SDK Troubleshooting Guide

Common issues and solutions to help you debug your Akira applications.

---

## 📑 Table of Contents

- [Display Issues](#-display-issues)
- [Event Handling Problems](#-event-handling-problems)
- [Sensor Reading Errors](#-sensor-reading-errors)
- [RF Communication Issues](#-rf-communication-issues)
- [Storage Problems](#-storage-problems)
- [Network Errors](#-network-errors)
- [Memory Issues](#-memory-issues)
- [Compilation Errors](#-compilation-errors)
- [Performance Problems](#-performance-problems)
- [General Debugging Tips](#-general-debugging-tips)

---a

## 🖥️ Display Issues

### Problem: Display Shows Nothing

**Symptoms:** Screen stays black after drawing

**Possible Causes:**
1. Forgot to call `akira_display_flush()`
2. Drawing outside screen bounds
3. Using color `0x0000` on black background

**Solutions:**

```c
// ✅ Always flush after drawing
akira_display_clear(0x0000);
akira_display_text(10, 10, "Hello", 0xFFFF);
akira_display_flush();  // DON'T FORGET THIS!

// ✅ Check your coordinates
int w, h;
akira_display_get_size(&w, &h);
printf("Display size: %dx%d\n", w, h);

// ✅ Use visible colors
akira_display_text(10, 10, "Text", 0xFFFF);  // White on black
```

---

### Problem: Text is Garbled or Missing

**Symptoms:** Text appears corrupted or doesn't show

**Possible Causes:**
1. NULL or invalid string pointer
2. String not null-terminated
3. Text drawn outside visible area

**Solutions:**

```c
// ✅ Ensure strings are null-terminated
char text[32];
strncpy(text, source, sizeof(text) - 1);
text[sizeof(text) - 1] = '\0';  // Guarantee null termination

// ✅ Validate string before drawing
if (text && text[0] != '\0') {
    akira_display_text(10, 10, text, 0xFFFF);
}

// ✅ Check coordinates are within bounds
if (x >= 0 && y >= 0 && x < width && y < height) {
    akira_display_text(x, y, text, color);
}
```

---

### Problem: Display Flickers

**Symptoms:** Screen flashes or flickers rapidly

**Possible Causes:**
1. Flushing too frequently
2. Clearing entire screen every frame
3. No frame rate limiting

**Solutions:**

```c
// ✅ Limit frame rate
void render_loop() {
    while(1) {
        update_graphics();
        akira_system_sleep(16);  // ~60 FPS
        akira_process_events();
    }
}

// ✅ Only update changed regions
static int last_value = -1;
if (value != last_value) {
    // Clear only the changed area
    akira_display_rect(x, y, w, h, 0x0000);
    draw_new_value(value);
    akira_display_flush();
    last_value = value;
}
```

---

### Problem: Colors Look Wrong

**Symptoms:** Colors don't match what you expected

**Possible Causes:**
1. RGB565 format confusion
2. Byte order issues
3. Wrong color values

**Solutions:**

```c
// ✅ Use RGB565 format correctly
// Format: RRRR RGGG GGGB BBBB

// Red (5 bits): 0xF800
// Green (6 bits): 0x07E0
// Blue (5 bits): 0x001F

// Create custom colors
uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) |  // Red: 5 bits
           ((g & 0xFC) << 3) |  // Green: 6 bits
           (b >> 3);             // Blue: 5 bits
}

// Test your colors
akira_display_rect(0, 0, 10, 10, 0xF800);   // Red square
akira_display_rect(10, 0, 10, 10, 0x07E0);  // Green square
akira_display_rect(20, 0, 10, 10, 0x001F);  // Blue square
akira_display_flush();
```

---

## ⚡ Event Handling Problems

### Problem: Callbacks Never Fire

**Symptoms:** Registered callbacks don't get called

**Possible Causes:**
1. Forgot to call `akira_process_events()`
2. Callback not properly registered
3. Wrong timer/GPIO/topic ID

**Solutions:**

```c
// ✅ MUST call this in your main loop
while(1) {
    akira_process_events();  // This dispatches callbacks!
}

// ✅ Check registration return value
int result = akira_register_timer_callback(0, my_callback);
if (result != 0) {
    akira_log(0, "Failed to register callback! ❌");
}

// ✅ Verify IDs are in valid range
if (timer_id >= 0 && timer_id < AKIRA_MAX_TIMERS) {
    akira_register_timer_callback(timer_id, callback);
}
```

---

### Problem: Callback Gets Called Multiple Times

**Symptoms:** Same callback fires repeatedly for one event

**Possible Causes:**
1. Registered callback multiple times
2. Multiple callbacks registered for same resource
3. Not handling event properly

**Solutions:**

```c
// ✅ Unregister before re-registering
akira_unregister_timer_callback(0);
akira_register_timer_callback(0, new_callback);

// ✅ Check if already registered
static bool is_registered = false;
if (!is_registered) {
    akira_register_timer_callback(0, callback);
    is_registered = true;
}
```

---

### Problem: Events Are Delayed or Slow

**Symptoms:** Callbacks fire later than expected

**Possible Causes:**
1. Blocking in callbacks
2. Not calling `akira_process_events()` frequently
3. Heavy processing in event loop

**Solutions:**

```c
// ❌ BAD - blocks for 5 seconds
void on_button(uint32_t buttons) {
    akira_system_sleep(5000);  // DON'T DO THIS!
}

// ✅ GOOD - returns immediately
static bool action_pending = false;
void on_button(uint32_t buttons) {
    action_pending = true;  // Set flag, process later
}

void on_timer() {
    if (action_pending) {
        perform_long_operation();
        action_pending = false;
    }
}
```

---

## 📊 Sensor Reading Errors

### Problem: Sensor Read Returns Error

**Symptoms:** `akira_sensor_read()` returns non-zero

**Possible Causes:**
1. Sensor not connected
2. Wrong sensor type
3. Sensor needs initialization time
4. Missing capability in manifest

**Solutions:**

```c
// ✅ Add retry logic with delays
int read_sensor_with_retry(float *value) {
    for (int i = 0; i < 3; i++) {
        if (akira_sensor_read(SENSOR_TYPE_TEMP, value) == 0) {
            return 0;  // Success
        }
        akira_log(1, "Retry sensor read... ⏳");
        akira_system_sleep(100);
    }
    akira_log(0, "Sensor read failed after retries ❌");
    return -1;
}

// ✅ Check sensor availability at startup
void test_sensors() {
    float dummy;
    if (akira_sensor_read(SENSOR_TYPE_TEMP, &dummy) == 0) {
        akira_log(2, "Temperature sensor OK ✅");
    } else {
        akira_log(0, "Temperature sensor not available ❌");
    }
}
```

---

### Problem: Sensor Values Seem Wrong

**Symptoms:** Readings are unrealistic or constant

**Possible Causes:**
1. Using uninitialized variable
2. Not checking return value
3. Sensor calibration needed
4. Reading too frequently

**Solutions:**

```c
// ✅ Always initialize and check
float temp = 0.0;  // Initialize
if (akira_sensor_read(SENSOR_TYPE_TEMP, &temp) == 0) {
    // Only use if read was successful
    if (temp >= -40.0 && temp <= 85.0) {  // Sanity check
        process_temperature(temp);
    } else {
        akira_log(1, "Temperature out of range ⚠️");
    }
} else {
    akira_log(0, "Failed to read temperature ❌");
}

// ✅ Don't read too frequently
#define MIN_SENSOR_INTERVAL_MS 1000
static uint64_t last_read = 0;

void read_if_ready() {
    uint64_t now = akira_system_uptime_ms();
    if (now - last_read >= MIN_SENSOR_INTERVAL_MS) {
        read_sensor();
        last_read = now;
    }
}
```

---

## 📡 RF Communication Issues

### Problem: RF Init Fails

**Symptoms:** `akira_rf_init()` returns error

**Possible Causes:**
1. Wrong chip type
2. Hardware not connected
3. Missing capability
4. Already initialized

**Solutions:**

```c
// ✅ Check all possible chips
int init_any_rf() {
    const akira_rf_chip_t chips[] = {
        AKIRA_RF_CHIP_NRF24L01,
        AKIRA_RF_CHIP_CC1101,
        AKIRA_RF_CHIP_SX1276
    };
    
    for (int i = 0; i < sizeof(chips)/sizeof(chips[0]); i++) {
        if (akira_rf_init(chips[i]) == 0) {
            akira_log(2, "RF initialized! ✅");
            return 0;
        }
    }
    
    akira_log(0, "No RF chip found ❌");
    return -1;
}

// ✅ Deinit before reinit
akira_rf_deinit();
if (akira_rf_init(AKIRA_RF_CHIP_NRF24L01) == 0) {
    akira_log(2, "RF reinitialized ✅");
}
```

---

### Problem: Can't Send/Receive RF Data

**Symptoms:** `akira_rf_send()` or `akira_rf_receive()` fails

**Possible Causes:**
1. Incorrect frequency
2. Power level too low
3. Distance too far
4. Packet size too large
5. Timing issues

**Solutions:**

```c
// ✅ Configure RF properly
void setup_rf() {
    akira_rf_init(AKIRA_RF_CHIP_NRF24L01);
    
    // Use common frequency
    akira_rf_set_frequency(2450000000);  // 2.45 GHz
    
    // Use reasonable power
    akira_rf_set_power(0);  // 0 dBm
    
    akira_log(2, "RF configured ⚙️");
}

// ✅ Check packet size
#define MAX_RF_PACKET_SIZE 32

void send_packet(const uint8_t *data, size_t len) {
    if (len > MAX_RF_PACKET_SIZE) {
        akira_log(1, "Packet too large ⚠️");
        return;
    }
    
    if (akira_rf_send(data, len) == 0) {
        akira_log(2, "Packet sent ✅");
    } else {
        akira_log(0, "Send failed ❌");
    }
}

// ✅ Use appropriate timeout
uint8_t buffer[64];
int len = akira_rf_receive(buffer, sizeof(buffer), 5000);  // 5 second timeout
if (len > 0) {
    akira_log(2, "Packet received! 📥");
} else if (len == 0) {
    akira_log(3, "Timeout - no packet 🕐");
} else {
    akira_log(0, "Receive error ❌");
}
```

---

### Problem: Poor RF Range

**Symptoms:** Communication only works at close range

**Possible Causes:**
1. Low transmit power
2. Interference
3. Antenna issues
4. Obstacles

**Solutions:**

```c
// ✅ Increase transmit power
akira_rf_set_power(20);  // Maximum power (check chip specs)

// ✅ Check signal quality
int16_t rssi;
if (akira_rf_get_rssi(&rssi) == 0) {
    akira_log(2, "RSSI: %d dBm 📶", rssi);
    if (rssi < -90) {
        akira_log(1, "Weak signal! ⚠️");
    }
}

// ✅ Try different frequencies
void scan_frequencies() {
    uint32_t freqs[] = {2400000000, 2450000000, 2480000000};
    
    for (int i = 0; i < 3; i++) {
        akira_rf_set_frequency(freqs[i]);
        // Test communication...
    }
}
```

---

## 💾 Storage Problems

### Problem: File Read/Write Fails

**Symptoms:** Storage operations return negative values

**Possible Causes:**
1. File doesn't exist (read)
2. Storage full (write)
3. Invalid path
4. Missing capability

**Solutions:**

```c
// ✅ Check if file exists before reading
int size = akira_storage_size("config.txt");
if (size >= 0) {
    char buffer[256];
    int bytes = akira_storage_read("config.txt", buffer, sizeof(buffer));
    if (bytes > 0) {
        akira_log(2, "Read %d bytes ✅", bytes);
    }
} else {
    akira_log(1, "File not found ⚠️");
}

// ✅ Check write result
int bytes_written = akira_storage_write("data.txt", data, len);
if (bytes_written == len) {
    akira_log(2, "Write successful ✅");
} else if (bytes_written < 0) {
    akira_log(0, "Write failed ❌");
} else {
    akira_log(1, "Partial write ⚠️");
}
```

---

### Problem: Storage Full

**Symptoms:** Writes fail with -ENOMEM or similar

**Possible Causes:**
1. Too much data stored
2. Not deleting old files
3. Large log files

**Solutions:**

```c
// ✅ Implement log rotation
void rotate_log() {
    int size = akira_storage_size("app.log");
    if (size > 10000) {  // 10KB limit
        akira_storage_delete("app.log.old");
        // Could rename app.log to app.log.old
        akira_storage_delete("app.log");
        akira_log(2, "Log rotated 🔄");
    }
}

// ✅ Clean up old files
void cleanup_old_files() {
    akira_storage_delete("temp.dat");
    akira_storage_delete("cache.bin");
    akira_log(2, "Cleanup complete 🧹");
}
```

---

## 🌐 Network Errors

### Problem: HTTP Request Fails

**Symptoms:** `akira_http_get()` returns negative value

**Possible Causes:**
1. No network connection
2. Invalid URL
3. Server unreachable
4. Timeout

**Solutions:**

```c
// ✅ Validate URL format
bool is_valid_url(const char *url) {
    return (strncmp(url, "http://", 7) == 0 || 
            strncmp(url, "https://", 8) == 0);
}

// ✅ Add retry logic
int http_get_with_retry(const char *url, uint8_t *buffer, size_t len) {
    for (int i = 0; i < 3; i++) {
        int result = akira_http_get(url, buffer, len);
        if (result > 0) {
            return result;
        }
        akira_log(1, "HTTP retry %d/3 ⏳", i + 1);
        akira_system_sleep(1000);
    }
    return -1;
}
```

---

### Problem: MQTT Messages Not Received

**Symptoms:** Callback never fires for subscribed topic

**Possible Causes:**
1. Wrong topic pattern
2. Not subscribed correctly
3. Connection lost
4. Callback not registered

**Solutions:**

```c
// ✅ Test subscription
int result = akira_mqtt_subscribe("test/topic", on_mqtt);
if (result == 0) {
    akira_log(2, "Subscribed successfully ✅");
    
    // Publish test message
    const char *msg = "test";
    akira_mqtt_publish("test/topic", msg, strlen(msg));
} else {
    akira_log(0, "Subscribe failed ❌");
}

// ✅ Use wildcard patterns correctly
akira_mqtt_subscribe("sensors/+/temp", on_mqtt);  // Any sensor
akira_mqtt_subscribe("sensors/#", on_mqtt);       // All sensor topics
```

---

## 💾 Memory Issues

### Problem: Out of Memory Errors

**Symptoms:** Operations fail with -ENOMEM

**Possible Causes:**
1. Memory leaks
2. Too many allocations
3. Large stack usage
4. Fragmentation

**Solutions:**

```c
// ✅ Use static allocation
static char buffer[1024];  // Instead of malloc

// ✅ Monitor memory usage
void check_memory() {
    size_t free = akira_system_free_memory();
    akira_log(2, "Free memory: %zu bytes 💾", free);
    
    if (free < 1000) {
        akira_log(1, "Low memory! ⚠️");
    }
}

// ✅ Limit buffer sizes
#define MAX_READINGS 100  // Don't make this too large!
static float readings[MAX_READINGS];
```

---

### Problem: Stack Overflow

**Symptoms:** App crashes or behaves erratically

**Possible Causes:**
1. Large local variables
2. Deep recursion
3. Large format strings

**Solutions:**

```c
// ❌ BAD - large stack allocation
void process() {
    char huge[10000];  // Don't do this!
}

// ✅ GOOD - use static
void process() {
    static char buffer[10000];  // OK
}

// ❌ BAD - recursion
void recursive(int n) {
    recursive(n + 1);  // Stack overflow!
}

// ✅ GOOD - iteration
void iterative(int n) {
    for (int i = 0; i < n; i++) {
        // Process...
    }
}
```

---

## 🔨 Compilation Errors

### Problem: Undefined Reference to akira_*

**Symptoms:** Linker errors for Akira functions

**Solutions:**

```bash
# ✅ Include akira_api.c in compilation
build.sh -o app.wasm main.c

```

---

### Problem: Wrong WASM Output

**Symptoms:** Generated WASM doesn't work

**Solutions:**

```bash
# ✅ Use correct flags for standalone WASM
build.sh -o app.wasm main.c
# ✅ Check WASM with wasm-objdump
wasm-objdump -x app.wasm
```

---

## 🐌 Performance Problems

### Problem: App Runs Slow

**Symptoms:** Laggy UI, slow response

**Possible Causes:**
1. Too many display updates
2. Complex calculations in callbacks
3. No frame rate limiting
4. Sensor polling too frequent

**Solutions:**

```c
// ✅ Limit display updates
static uint64_t last_update = 0;
#define UPDATE_INTERVAL_MS 100

void maybe_update_display() {
    uint64_t now = akira_system_uptime_ms();
    if (now - last_update >= UPDATE_INTERVAL_MS) {
        update_display();
        last_update = now;
    }
}

// ✅ Move heavy work to timers
void on_button(uint32_t buttons) {
    flag_work_needed = true;  // Just set flag
}

void on_timer() {
    if (flag_work_needed) {
        do_heavy_calculation();  // Do work here
        flag_work_needed = false;
    }
}

// ✅ Profile your code
uint64_t start = akira_system_uptime_ms();
expensive_operation();
uint64_t elapsed = akira_system_uptime_ms() - start;
akira_log(3, "Operation took %llu ms", elapsed);
```

---

## 🔍 General Debugging Tips

### Enable Verbose Logging

```c
// Add at start of main()
akira_log(2, "App started 🚀");
akira_log(2, "Version: 1.0.0");

// Log important events
akira_log(2, "Sensor read: %.2f", value);
akira_log(2, "Button pressed: 0x%X", buttons);
```

### Add Debug Display

```c
#ifdef DEBUG
void show_debug_overlay() {
    char dbg[64];
    
    // Memory
    snprintf(dbg, sizeof(dbg), "Mem:%zu", 
             akira_system_free_memory());
    akira_display_text(200, 5, dbg, 0xF800);
    
    // Uptime
    snprintf(dbg, sizeof(dbg), "Up:%llu", 
             akira_system_uptime_ms() / 1000);
    akira_display_text(200, 20, dbg, 0xF800);
}
#endif
```

### Test in Isolation

```c
// Test each component separately
void test_display() {
    akira_display_clear(0xF800);  // Red screen
    akira_display_flush();
    akira_system_sleep(1000);
}

void test_sensors() {
    float temp;
    if (akira_sensor_read(SENSOR_TYPE_TEMP, &temp) == 0) {
        akira_log(2, "Sensor OK: %.2f", temp);
    }
}

AKIRA_APP_MAIN() {
    test_display();
    test_sensors();
    // ...
}
```

### Use Assertions

```c
#define ASSERT(cond) \
    if (!(cond)) { \
        akira_log(0, "Assert failed: " #cond); \
        while(1); \
    }

void process_data(const char *data) {
    ASSERT(data != NULL);
    ASSERT(data[0] != '\0');
    // Process...
}
```

---

## 📞 Getting Help

If you're still stuck:

1. **Check the logs** - Look for error messages
2. **Review the [API Reference](API_REFERENCE.md)** - Verify correct usage
3. **Try the [Examples](EXAMPLES.md)** - Compare with working code
4. **Read [Best Practices](BEST_PRACTICES.md)** - Avoid common pitfalls
5. **Ask the community** - Post on forums with error details

---

## 🎯 Debugging Checklist

Before asking for help, verify:

- [ ] Logs enabled and checked
- [ ] Return values checked
- [ ] Memory usage reasonable
- [ ] Callbacks registered correctly
- [ ] `akira_process_events()` called in loop
- [ ] Display flushed after drawing
- [ ] Pointers validated
- [ ] Bounds checked
- [ ] Error handling present
- [ ] Tested each component separately

---

[⬆ Back to Top](#-akira-sdk-troubleshooting-guide)
