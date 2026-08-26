# 🐱 Cat Food Machine — 智能猫粮投喂机

English | [中文](./README_zh.md)

An ESP32-S3 based **intelligent cat feeder** with touchscreen UI, WiFi connectivity, scheduled feeding, and stepper motor control.

## 📋 Features

- **Touchscreen UI** — 2.8" 320x240 SPI LCD (ST7789) with LVGL graphical interface
- **Capacitive Touch** — GT911 touch sensor (I2C) for smooth user interaction
- **Multi-page Interface** — Feeding home page (default), feeding control, settings, and WiFi configuration
- **Scheduled Feeding** — Up to 8 schedules with a fixed feeding time (HH:MM) and a repeat interval in days (daily / every other day / every 3 days, ...), with NVS persistence
- **Stepper Motor Dispensing** — A4988 driven stepper motor, 1 slot = 90° rotation
- **OV2640 Camera** — DVP 8-bit parallel interface, square JPEG 240×240, captured on demand
- **WiFi Connectivity** — Station mode with saved credentials, on-screen WiFi configuration and device IP display
- **On-demand LAN Video Streaming** — HTTP-MJPEG stream at `http://<device-ip>/stream`, with a self-reconnecting browser page at `http://<device-ip>/`
- **SNTP Time Sync** — Automatic time synchronization via NTP (Beijing time, UTC+8)
- **Auto Backlight Dimming** — Automatically dims backlight after 5 minutes of inactivity
- **USB Virtual Serial** — Built-in USB-Serial-JTAG debug port (chip ROM), works from bootloader, stays connected across reboots, no USB-TTL adapter needed

## 🧱 Project Structure

```
Cat-Food-Machine/
├── esp32libraries/          # ESP32 library submodules
│   ├── esp-idf/             # ESP-IDF framework (v5.5.1)
│   └── example/             # Example projects (LVGL, DVP camera)
├── src/                     # Main application firmware
│   ├── CMakeLists.txt       # IDF project configuration
│   ├── dependencies.lock    # Component dependency lock
│   ├── sdkconfig*           # Build configuration
│   ├── main/
│   │   ├── main.c           # Application entry & feeding popup management
│   │   ├── include.h        # Global includes
│   │   ├── device/          # Hardware device drivers
│   │   │   ├── inc/         #   - st7789.h, gt911.h, user_lvgl.h
│   │   │   │                #   - ov2640.h, video_stream.h
│   │   │   │                #   - wifi_app.h, sntp_time.h
│   │   │   └── src/         # Driver implementations
│   │   ├── driver/          # Application-specific drivers
│   │   │   ├── inc/         #   - feeder_motor.h, feeding_schedule.h
│   │   │   └── src/         # Driver implementations
│   │   └── ui/              # LVGL user interface
│   │       ├── inc/         #   - ui.h, app_page.h, feeding_page.h
│   │       │                #   - setting_page.h, wifi_config_page.h
│   │       └── src/         # UI implementations
├── sim/                     # Offline UI simulator (debug UI on PC, no flashing)
│   ├── main.c               #   SDL2 interactive window build
│   ├── include/             #   Stub headers (replace hardware deps)
│   ├── src/                 #   Stub implementations
│   └── README.md            #   Simulator documentation
├── README.md                # English documentation
├── README_zh.md             # Chinese documentation
└── CHANGELOG.md             # Release history
```

## 🚀 Getting Started

### Prerequisites

- **ESP-IDF v5.5.1** (included as a submodule)
- ESP32-S3 development board
- Required hardware components (see [Hardware Configuration](#hardware-configuration))

### 1. Clone & Initialize Submodules

```bash
git clone --recursive https://github.com/Garfield-1314/Cat-Food-Machine.git
cd Cat-Food-Machine

# If already cloned without --recursive:
git submodule init
git submodule update --progress

cd esp32libraries/esp-idf
git checkout v5.5.1
git submodule update --init --recursive --progress
./install.sh
```

### 2. Build & Flash

```bash
cd src
source ../esp32libraries/esp-idf/export.sh
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

## 🖥️ Offline UI Simulator (debug UI without flashing)

The firmware's LVGL UI can run directly on a **PC** for debugging, without
flashing to the ESP32-S3 every time.

The simulator **reuses** the exact same source under `src/main/ui/src/*.c`,
only replacing hardware dependencies with stubs (WiFi / SNTP / feeding schedule / NVS / motor).

### Prerequisites

- CMake ≥ 3.12, a C compiler
- **SDL2**

### Build & Run

```bash
# Install SDL2 (Ubuntu/Debian)
sudo apt install -y libsdl2-dev

cd sim
cmake -S . -B build
cmake --build build -j
./build/cat_food_sim       # Opens a 640×480 window; mouse = touch; interactive debugging
```

### How firmware changes sync

| Firmware change | Simulator sync |
|---|---|
| Edit an existing page | Just `cmake --build build` |
| Add / remove a page | Just `cmake --build build` (auto-detected); remember to update `switch_page_cb` in `ui.c` |
| New page uses new hardware functions | Add stubs in `sim/include/` + `sim/src/*_stub.c` |

> See [`sim/README.md`](./sim/README.md) for details.

## 🖥️ Hardware Configuration

The currently active hardware profile is the Cat board. ESP32-S3-EYE LCD pins
remain in the source as a commented test profile.

> **⚠️ PSRAM is unusable on this board (hardware constraint)**: the module is an
> N16R8 (16 MB flash + 8 MB octal PSRAM), but its data lines D2/D3/D4 are fixed on
> GPIO35/36/37 — exactly the pins the ST7789 LCD below uses for CLK/MOSI/CS.
> **Enabling PSRAM causes MSPI bus contention: the device hangs during boot and
> is reset in an infinite loop by the watchdog.** Therefore `CONFIG_SPIRAM` stays
> disabled in the firmware. Re-enable it only on a board with free PSRAM pins
> (see the comment in `sdkconfig.defaults`).

The image header is configured for the physical 16 MB flash
(`CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y`). When upgrading from the old 2 MB setting,
flash the bootloader, partition table, and application together at least once;
otherwise an old bootloader may continue to report a flash-size mismatch.

### LCD (ST7789) — SPI Interface

| Signal | GPIO Pin |
|--------|----------|
| MOSI   | 36       |
| CLK    | 35       |
| CS     | 37       |
| DC     | 38       |
| RST    | 47       |
| BL     | 48       |

**Resolution:** 320 × 240

### Touch (GT911) — I2C Interface

| Signal | GPIO Pin |
|--------|----------|
| SDA    | 2        |
| SCL    | 1        |
| INT    | 21       |
| RST    | 14       |

### Stepper Motor (A4988)

| Signal | GPIO Pin |
|--------|----------|
| EN     | 45       |
| STEP   | 39       |
| DIR    | 40       |
| MS1    | 41       |
| MS2    | 42       |
| MS3    | 3        |

> *Pin definitions live in `feeder_motor.c` (compile-time constants); MS1/MS2/MS3 are driven high by default to enable 16-microstep mode. Adjust as needed for your custom PCB.*

### OV2640 Camera — DVP parallel interface + SCCB (I2C)

| Signal  | GPIO Pin |
|---------|----------|
| D0 ~ D7 | 11, 9, 8, 10, 12, 18, 17, 16 |
| VSYNC   | 6        |
| DE (HREF) | 7      |
| PCLK    | 13       |
| XCLK    | 15       |
| SCCB SCL | 5       |
| SCCB SDA | 4       |

> *SCCB runs on a dedicated I2C_NUM_1 bus (no conflict with GT911 on I2C_NUM_0). Camera pins live in `ov2640.h` (compile-time constants). Application code configures a 240×240 square output from the component's public 320×240 JPEG base mode without modifying `managed_components`.*

### On-demand Video Streaming

After WiFi obtains an IP address, the device starts an HTTP server on port 80.
The server itself does not start camera capture. Camera capture begins only
when a client requests the stream, and stops when the client disconnects.

- Browser page: `http://<device-ip>/`
- Raw MJPEG stream: `http://<device-ip>/stream`
- Format: square OV2640 JPEG 240×240, sent at up to 10 FPS
- Scope: LAN only, no authentication in the current version
- Limit: one simultaneous stream client; an additional client receives HTTP 503
- Memory: without usable PSRAM the firmware uses two 57,600-byte DMA frame
  buffers plus a reusable copy sized to the current compressed JPEG. The copy
  keeps network stalls from corrupting capture; feeding remains available
- Stability: each socket send waits for 2 seconds and retries `EAGAIN` up to
  twice. Slow sends do not trigger catch-up frame bursts that refill TCP buffers
- Recovery: the browser page reconnects about one second after a terminal stream
  failure. It releases the stream while hidden and reconnects when visible.
  Clients that consume `/stream` directly must implement their own reconnect

> This board has no usable PSRAM, and the local LCD preview has been removed
> (internal SRAM cannot host both the frame buffer and the RGB565 decode output);
> the camera feed is viewed through the web stream instead.

### Memory and Network Resource Profile

| Resource | Current setting | Allocation lifetime |
|---|---:|---|
| LVGL draw buffer | 320×10 RGB565 = 6,400 bytes | At boot, internal DMA SRAM |
| LVGL memory pool | 20 KiB | Static DIRAM |
| OV2640 DMA frame buffers | 2×57,600 bytes | Allocated for a Web stream, freed on disconnect |
| JPEG send copy | Compressed size rounded up to 4 KiB | Grows on demand during a Web stream |
| WiFi RX | 8 static, 24 dynamic maximum, BA window 6 | While WiFi is running |
| WiFi TX | 32 dynamic maximum | While WiFi is running |

For the current build, `idf.py size` reports **134,935 bytes of static DIRAM
(39.48%)**. This excludes camera buffers, the JPEG copy, and dynamic WiFi
buffers allocated at runtime.

The firmware uses an IPv4/WPA2 Station-only profile. IPv6, SoftAP/DHCP server,
enterprise WiFi, and WPA3/OWE are disabled. General WiFi IRAM optimization stays
enabled for MJPEG transmit performance, while the optional RX IRAM fast path is
disabled. Re-enable the corresponding Kconfig options when those features are
needed; for frequent packet loss on weak networks, restore the dynamic RX limit
to 32 first.

### Device UI Behavior

- The feeding home page is shown at boot.
- The home page displays the next feeding countdown in `HH:MM` format without seconds.
- If no feeding schedule is configured, it displays `Next feed: No schedule`.
- The device IP is shown below the `Feed` button after WiFi obtains an address.
- Tapping the camera icon on the home page shows the web stream URL
  (`http://<device-ip>/`) — open it in a browser to watch the live feed.

## 📖 API Overview

### Manual Feeding

```c
#include "include.h"

// Dispense a specific number of slots (1 ~ 10)
manual_feeding_start(2);  // Dispense 2 slots
```

### Feeding Schedule

```c
#include "driver/inc/feeding_schedule.h"

// Add a schedule: feed 2 slots every day at 08:00
feed_schedule_item_t item = {
    .hour = 8, .minute = 0, .amount = 2, .enabled = true,
    .every_days = 1,   /* 1 = daily, 2 = every other day, 3 = every 3 days, ... */
};
feed_schedule_add_item(&item);
feed_schedule_save();
```

> `every_days` is the repeat interval in days (up to 7): 1 = daily, 2 = every other day, 3 = every 3 days, ... The specific days follow local calendar-day rotation (Beijing time).

### WiFi Configuration

```c
#include "device/inc/wifi_app.h"

// Connect to a WiFi network
wifi_app_connect("MyWiFi", "password123");

// Check connection status
bool connected = wifi_app_is_connected();
```

## 🛠️ Key Dependencies

| Component | Version | Usage |
|-----------|---------|-------|
| ESP-IDF   | v5.5.1  | RTOS, drivers, networking |
| LVGL      | v8.4.0  | Embedded GUI library |
| ST7789    | —       | SPI LCD controller |
| GT911     | —       | Capacitive touch controller |
| USB-Serial-JTAG | ESP32-S3 | Built-in debug serial (ROM, survives reboots) |
| A4988     | —       | Stepper motor driver |
| OV2640    | —       | DVP camera (square JPEG 240×240) |
| esp_cam_sensor | ^1.1.0 | OV2640 sensor driver |
| esp_http_server | ESP-IDF | HTTP server for LAN video streaming |

## 🤝 Contributing

Contributions are welcome! Please follow these steps:

1. Fork the project
2. Create your feature branch (`git checkout -b feature/AmazingFeature`)
3. Commit your changes (`git commit -m 'Add some AmazingFeature'`)
4. Push to the branch (`git push origin feature/AmazingFeature`)
5. Open a Pull Request

## 📝 License

Distributed under the MIT License. See `LICENSE` for more information.

## 🙏 Acknowledgments

- Based on example projects from [Espressif Systems](https://github.com/espressif)
- LVGL graphics library — [lvgl/lvgl](https://github.com/lvgl/lvgl)

## 📬 Contact

Project Link: [https://github.com/Garfield-1314/Cat-Food-Machine](https://github.com/Garfield-1314/Cat-Food-Machine)
