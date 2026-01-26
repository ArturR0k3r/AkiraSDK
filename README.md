# 🚀 Akira SDK Documentation

<div align="center">

![Akira SDK](https://img.shields.io/badge/Akira-SDK-blue?style=for-the-badge)
![WASM](https://img.shields.io/badge/WASM-Ready-purple?style=for-the-badge)
![IoT](https://img.shields.io/badge/IoT-Powered-green?style=for-the-badge)

**Build powerful embedded applications with WebAssembly** 🎯

[Getting Started](#-getting-started) • [API Reference](API_REFERENCE.md) • [Troubleshooting](TROUBLESHOOTING.md) • [Best Practices](BEST_PRACTICES.md)

</div>

---

## 🌟 What is Akira SDK?

Akira SDK is a **powerful WASM-based framework** for building embedded applications on AkiraOS. It provides a comprehensive API for interacting with hardware peripherals, sensors, displays, and networks - all from the safety and portability of WebAssembly!

### ✨ Key Features

- 🎮 **Display & Input** - Rich graphics and button handling
- 📡 **RF Communication** - Support for multiple RF chips (nRF24L01, LoRa, CC1101)
- 🔌 **GPIO & Timers** - Hardware control with event-driven callbacks
- 📊 **Sensors** - IMU, environmental, and power monitoring
- 💾 **Storage** - Persistent file storage API
- 🌐 **Networking** - HTTP and MQTT support
- ⚡ **Event-Driven** - Efficient callback-based architecture
- 🔒 **Capability-Based Security** - Fine-grained permission control

---

## 🏗️ Architecture Overview

```
┌─────────────────────────────────────────┐
│         Your WASM Application           │
│      (Built with Akira SDK)             │
└─────────────────┬───────────────────────┘
                  │
         ┌────────▼────────┐
         │   Akira API     │
         │  (akira_api.h)  │
         └────────┬────────┘
                  │
         ┌────────▼────────┐
         │   AkiraOS       │
         │   Runtime       │
         └────────┬────────┘
         ┌────────▼────────┐
         │   AkiraOS       │
         │   HAL           │
         └────────┬────────┘
                  │
    ┌─────────────┼─────────────┐
    │             │             │
┌───▼───┐    ┌───▼───┐    ┌───▼───┐
│ GPIO  │    │Display│    │  RF   │
│Timers │    │ Input │    │Network│
└───────┘    └───────┘    └───────┘
```

---

## 🚀 Getting Started

### Prerequisites

- WASM toolchain (Emscripten or WASI SDK)
- Akira SDK headers
- AkiraOS-compatible hardware

### Quick Start

1️⃣ **Include the SDK header:**
```
#include "akira_api.h"
```

2️⃣ **Write your main function:**
```c
AKIRA_APP_MAIN() {
    // Initialize display
    akira_display_clear(0x0000);  // Black background
    akira_display_text(10, 10, "Hello Akira! 👋", 0xFFFF);
    akira_display_flush();
    
    // Main event loop
    while(1) {
        akira_process_events();
    }
    
    return 0;
}
```

3️⃣ **Compile to WASM:**
```bash
build.sh -o app.wasm main.c
```

---

## 📚 Core Concepts

### 🎯 Event-Driven Architecture

Akira SDK uses an **event-driven model** where your application registers callbacks for various events:

```c
void on_timer() {
    akira_log(2, "Timer fired! ⏰");
}

// Register callback
akira_register_timer_callback(0, on_timer);

// Process events in main loop
while(1) {
    akira_process_events();  // Dispatches events to callbacks
}
```

### 🔐 Capability-Based Security

Each API requires specific capabilities in your app manifest:

| API | Required Capability |
|-----|-------------------|
| Display | `display.write` |
| Input | `input.read` |
| GPIO | `gpio.control` |
| RF | `rf.transceive` |
| Storage | `storage.read`, `storage.write` |
| Network | `network.http`, `network.mqtt` |
| Sensors | `sensor.<type>.read` |

### 🔄 Callback Management

The SDK supports **up to 64 simultaneous callbacks** across:
- ⏱️ **16 Timers**
- 🔌 **256 GPIO pins** (8 ports × 32 pins)
- 📨 **Unlimited message topics** (within callback limit)

---

## 🎨 API Categories

### [📺 Display API](API_REFERENCE.md#display-api)
Create beautiful UIs with RGB565 graphics
- Clear, pixel, rectangle, and text drawing
- Framebuffer management

### [🎮 Input API](API_REFERENCE.md#input-api)
Handle button presses and user input
- 10 button types (D-pad, ABXY, power, settings)
- Event-driven callbacks

### [📡 RF API](API_REFERENCE.md#rf-api)
Wireless communication made easy
- Support for nRF24L01, LoRa, CC1101, and more
- Send/receive packets with RSSI monitoring

### [🔌 GPIO & Timer API](API_REFERENCE.md#gpio-and-timer-api)
Control hardware precisely
- Register/unregister callbacks
- Event-driven state changes

### [📊 Sensor API](API_REFERENCE.md#sensor-api)
Read various sensor types
- IMU (accelerometer + gyroscope)
- Environmental (temp, humidity, pressure)
- Power monitoring

### [💾 Storage API](API_REFERENCE.md#storage-api)
Persistent data storage
- Read/write files
- Directory listings

### [🌐 Network API](API_REFERENCE.md#network-api)
Internet connectivity
- HTTP GET/POST
- MQTT pub/sub

### [⚙️ System API](API_REFERENCE.md#system-api)
System utilities
- Uptime and memory info
- Logging and sleep

---

## 📖 Documentation Structure

- **[API Reference](API_REFERENCE.md)** - Complete API documentation
- **[Examples](EXAMPLES.md)** - Code examples for common tasks
- **[Tutorials](TUTORIALS.md)** - Step-by-step guides
- **[Best Practices](BEST_PRACTICES.md)** - Tips and patterns
- **[Troubleshooting](TROUBLESHOOTING.md)** - Common issues and solutions

---

## 🎯 Example Applications

Check out these examples to get inspired:

- 🎮 **[Retro Game](EXAMPLES.md#retro-game)** - Simple game with display and input
- 🌡️ **[Weather Station](EXAMPLES.md#weather-station)** - Sensor reading and display
- 📡 **[RF Remote](EXAMPLES.md#rf-remote)** - Wireless communication
- 💾 **[Data Logger](EXAMPLES.md#data-logger)** - Storage and sensors
- 🌐 **[IoT Dashboard](EXAMPLES.md#iot-dashboard)** - MQTT and display

---

## 🤝 Contributing

We welcome contributions! Check out our [GitHub repository](https://github.com/drxgoshh/AkiraSDK) to get involved.

---

## 📝 License

Check the repository for licensing information.

---

## 🆘 Support

- 📚 [Documentation](API_REFERENCE.md)
- 🐛 [Issue Tracker](https://github.com/drxgoshh/AkiraSDK/issues)
- 💬 [Community Forum](#)

---

<div align="center">

**Made with ❤️ for the embedded community**

[⬆ Back to Top](#-akira-sdk-documentation)

</div>