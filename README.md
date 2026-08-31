# 🐱 Cat Food Machine — 智能猫粮投喂机

English | [中文](./README_zh.md)

An ESP32-S3 based **intelligent cat feeder** with touchscreen UI, WiFi connectivity, scheduled feeding, and stepper motor control.

## 📋 Features

- **Touchscreen UI** — 2.8" 320x240 SPI LCD (ST7789) with LVGL graphical interface
- **Capacitive Touch** — GT911 touch sensor (I2C) for smooth user interaction
- **Multi-page Interface** — Feeding home page (default), feeding control, settings, and WiFi configuration
- **Scheduled Feeding** — Up to 8 schedules with a fixed feeding time (HH:MM) and a repeat interval in days (daily / every other day / every 3 days, ...), with NVS persistence
- **Stepper Motor Dispensing** — A4988 driven stepper motor, 1 slot = 90° rotation
- **OV2640 Camera** — OV2640 DVP 8-bit parallel interface, native 640×480 JPEG captured on demand
- **WiFi Connectivity** — Station mode with one saved credential set, on-screen WiFi configuration and device IP display
- **On-demand LAN Video Streaming** — HTTP-MJPEG stream at `http://<device-ip>/stream`, with a self-reconnecting browser page at `http://<device-ip>/`
- **SNTP Time Sync** — Automatic time synchronization via NTP (Beijing time, UTC+8)
- **Auto Backlight Dimming** — Automatically dims backlight after 5 minutes of inactivity
- **USB Virtual Serial** — Built-in USB-Serial-JTAG debug port (chip ROM), works from bootloader, stays connected across reboots, no USB-TTL adapter needed

## 🧱 Project Structure

```
Cat-Food-Machine/
├── esp32libraries/          # ESP32 library submodules
│   ├── esp-idf/             # ESP-IDF framework (v5.5.5)
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
│   │   │   │                #   - ov2640.h, video_stream.h, ir_light.h
│   │   │   │                #   - wifi_app.h, sntp_time.h
│   │   │   └── src/         # Driver implementations
│   │   ├── driver/          # Application-specific drivers
│   │   │   ├── inc/         #   - feeder_motor.h, feeding_schedule.h
│   │   │   └── src/         # Driver implementations
│   │   └── ui/              # LVGL user interface
│   │       ├── inc/         #   - ui.h, app_page.h, feeding_page.h
│   │       │                #   - setting_page.h, wifi_config_page.h
│   │       └── src/         # UI implementations
├── README.md                # English documentation
├── README_zh.md             # Chinese documentation
└── CHANGELOG.md             # Release history
```

## 🧩 System Architecture

The firmware is one ESP-IDF application component. Initialization is performed
in this order: NVS flash, ST7789/LVGL and GT911, A4988 motor, OV2640 sensor,
home UI, feeding schedule, backlight, and WiFi. After WiFi obtains an IPv4
address, SNTP is started and the HTTP server is created. The main task then
returns and the dedicated LVGL task runs the UI loop at 50 Hz.

- The feeding scheduler checks enabled schedules once per second. It waits for
  a valid synchronized clock before triggering the motor.
- The WiFi scan runs in a FreeRTOS task and only publishes results to LVGL from
  an LVGL timer callback. A scan can temporarily interrupt an automatic
  connection attempt.
- The Web stream uses an asynchronous HTTP task. The first stream client
  acquires the camera and turns on the infrared light; disconnecting the last
  camera user stops the camera, turns the light off, and releases its buffers.
- LVGL page changes are deferred until the current touch event finishes. Page
  objects and their timers are released when the old page is deleted.

## 🚀 Getting Started

### Prerequisites

- **ESP-IDF v5.5.5** (included as a submodule and locked in `dependencies.lock`)
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
git checkout v5.5.5
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

## 🖥️ Hardware Configuration

The currently active hardware profile is the Cat board. ESP32-S3-EYE LCD pins
remain in the source as a commented test profile.

> The module is an N16R8 (16 MB flash + 8 MB octal PSRAM). The current PCB moves
> the LCD to GPIO41/42/3/38, leaving GPIO26~37 available for PSRAM, so the
> firmware enables `CONFIG_SPIRAM=y` and `CONFIG_SPIRAM_MODE_OCT=y`. OV2640 frame
> buffers, the Web JPEG copy, and the LVGL draw buffer prefer PSRAM.

The image header is configured for the physical 16 MB flash
(`CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y`). When upgrading from the old 2 MB setting,
flash the bootloader, partition table, and application together at least once;
otherwise an old bootloader may continue to report a flash-size mismatch.

The selected partition profile is the ESP-IDF single-app-large layout: a 24 KiB
NVS partition, a 4 KiB PHY initialization partition, and one 1,500 KiB factory
application partition. There are no OTA application slots in the current
firmware.

### Persistent Data

The NVS partition stores small device settings rather than filesystem data:

- `wifi_config`: one SSID/password profile; saving another network replaces it.
- `feed_sched`: up to 8 feeding schedules with amount, time, enabled state, and
  repeat interval.
- `lcd_config`: backlight brightness (30–100%).

After factory reset or NVS erase, WiFi credentials and feeding schedules are empty,
and the backlight uses its default brightness.

### LCD (ST7789) — SPI Interface

| Signal | GPIO Pin |
|--------|----------|
| MOSI   | 41       |
| CLK    | 42       |
| CS     | 3        |
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
| Microstep setting | Hardware-fixed 16 microsteps |

> *The firmware controls only EN, STEP, and DIR. The current PCB fixes the A4988 microstep setting in hardware, so MS1/MS2/MS3 are not software GPIOs. EN is active-low and the motor is disabled at initialization and after each feed.*

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

> *SCCB runs on a dedicated I2C_NUM_1 bus (no conflict with GT911 on I2C_NUM_0). Camera pins live in `ov2640.h` (compile-time constants). The application selects the sensor's native 640×480 JPEG format and JPEG quality 10 through the component's public API without modifying `managed_components`.*

Note that `sdkconfig` retains the esp_cam_sensor component's baseline format
`CONFIG_CAMERA_OV2640_DVP_JPEG_320X240_50FPS`; application startup overrides it
through the public API and selects 640×480 JPEG, so the application code defines
the final Web output.

### Infrared Fill Light

| Signal | GPIO pin | Logic |
|--------|----------|-------|
| MOS control | TXD0 / GPIO43 | High on, low off |

The standalone `ir_light.c` module configures the pin as an output and defaults it
low. The fill light is enabled only while OV2640 is actively capturing and is
disabled when the Web stream disconnects or the camera stops. Add an approximately
10 kΩ gate pulldown so the light remains off during reset and boot. GPIO43 is the
ESP32-S3 UART0 TXD pin; it is available here because the project uses the built-in
USB-Serial-JTAG console instead of the application UART. Do not reuse GPIO43 for
UART0 TX and the fill light at the same time.

### On-demand Video Streaming

After WiFi obtains an IP address, the device starts an HTTP server on port 80.
The server itself does not start camera capture. Camera capture begins only
when a client requests the stream, and stops when the client disconnects.

- Browser page: `http://<device-ip>/`
- Raw MJPEG stream: `http://<device-ip>/stream`
- Format: native OV2640 JPEG 640×480, sent at up to 10 FPS
- Scope: LAN only, no authentication in the current version
- Limit: one simultaneous stream client; an additional client receives HTTP 503
- Memory: the firmware uses two 2 MiB DMA frame buffers, preferring PSRAM, plus
  a PSRAM-preferred copy sized to the current compressed JPEG. The copy keeps
  network stalls from corrupting capture; feeding remains available
- Stability: the HTTP server socket send timeout is 2 seconds. A failed send
  ends the current stream and the browser page reconnects; slow sends do not
  trigger catch-up frame bursts that refill TCP buffers
- Recovery: the browser page reconnects about one second after a terminal stream
  failure. It releases the stream while hidden and reconnects when visible.
  Clients that consume `/stream` directly must implement their own reconnect

> The camera feed is viewed through the Web stream; camera and Web buffers use
> PSRAM so the internal RAM remains available for LVGL and the system.

### Memory and Network Resource Profile

| Resource | Current setting | Allocation lifetime |
|---|---:|---|
| LVGL draw buffer | 320×10 RGB565 = 6,400 bytes | At boot, PSRAM preferred, internal DMA fallback |
| LVGL memory pool | 64 KiB | Static DIRAM |
| OV2640 DMA frame buffers | 2×2 MiB | Allocated for a Web stream, PSRAM preferred, freed on disconnect |
| JPEG send copy | Compressed size rounded up to 4 KiB, up to about 2 MiB | Grows on demand, PSRAM preferred with internal fallback, freed on disconnect |
| WiFi RX | 8 static, 24 dynamic maximum, BA window 6 | While WiFi is running |
| WiFi TX | 32 dynamic maximum | While WiFi is running |

This build uses **188,775 / 341,760 bytes of static DIRAM (55.24%)**; the
application image is `0x115880` bytes and leaves `0x61780` bytes (26%) in the
1,500 KiB factory application partition. Static figures exclude camera buffers,
the JPEG copy, and dynamic WiFi buffers allocated at runtime; runtime PSRAM usage
must be measured on the target hardware or estimated from the allocation paths.

From the application buffer limits, one active stream can request at most about
**6,297,856 bytes of application-level PSRAM** (4 MiB of camera frame buffers,
2 MiB of JPEG copy, and 6,400 bytes of LVGL draw buffer). This is a code-derived
upper bound, not total measured PSRAM usage: it excludes WiFi/ESP-IDF/LVGL
component allocations, heap metadata, and any internal-RAM fallback. When no
stream is active, the 4 MiB camera buffers and JPEG copy are released.

The 8 MB PSRAM is used as a preferred heap for the LVGL draw buffer, two camera
DMA buffers, and the per-stream JPEG copy. The LVGL 64 KiB fixed memory pool is
still internal memory (`CONFIG_LV_MEM_CUSTOM` remains disabled). When PSRAM
allocation fails, the LVGL and camera paths fall back to suitable internal DMA
memory where supported.

The firmware uses an IPv4/WPA2 Station-only profile. IPv6, SoftAP/DHCP server,
enterprise WiFi, and WPA3/OWE are disabled. General WiFi IRAM optimization stays
enabled for MJPEG transmit performance, while the optional RX IRAM fast path is
disabled. Re-enable the corresponding Kconfig options when those features are
needed; for frequent packet loss on weak networks, restore the dynamic RX limit
to 32 first.

### Device UI Behavior

- The feeding home page is shown at boot.
- The home page displays the next feeding countdown as `DD:HH:MM` without seconds.
- If no feeding schedule is configured, it displays `Next feed: No schedule`.
- The device IP is shown below the `Feed` button after WiFi obtains an address.
- The WiFi page scans nearby access points and allows the user to connect to one.
- Only one WiFi profile is stored in NVS. Saving another network overwrites the
  previously saved SSID and password; after reboot, the device automatically
  tries the most recently saved network.
- The WiFi scan returns at most 24 access points. Protected networks ask for a
  password; open networks can be connected to directly. The WiFi page does not
  provide SoftAP provisioning or a manual SSID entry field.
- A saved network is retried immediately up to 5 times after a disconnect. If
  it is still unavailable, the firmware retries every 10 minutes. Starting a
  scan pauses an in-progress connection attempt and restores the periodic retry
  behavior afterward when needed.
- Pressing Connect saves the selected SSID/password before starting the
  connection task, so a failed connection remains the profile used at the next
  boot until another network is saved.
- The feeding page stores up to 8 schedules. Each schedule supports 1–10 slots,
  a time in hours and minutes, an enabled flag, and a 1–7 day repeat interval.
  The UI selects minutes in 5-minute steps.
- The settings page shows IDF/LVGL versions, chip revision, touch controller,
  firmware version, WiFi MAC address, and a 30–100% backlight slider.
- The backlight is automatically turned off after 5 minutes without touch input;
  touch activity or an active feed wakes it again.
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

The current firmware stores only one WiFi profile. Calling
`wifi_app_save_config()` with a new network replaces the existing profile; the
maximum of 24 scan results shown on the WiFi page is temporary and does not
represent saved networks.

## 🛠️ Key Dependencies

| Component | Version | Usage |
|-----------|---------|-------|
| ESP-IDF   | v5.5.5  | RTOS, drivers, networking |
| LVGL      | v8.4.0  | Embedded GUI library |
| ST7789    | —       | SPI LCD controller |
| GT911     | —       | Capacitive touch controller |
| USB-Serial-JTAG | ESP32-S3 | Built-in debug serial (ROM, survives reboots) |
| A4988     | —       | Stepper motor driver |
| OV2640    | —       | DVP camera (640×480 JPEG) |
| esp_cam_sensor | ^1.1.0 (locked at 1.7.0) | OV2640 sensor driver |
| esp_http_server | ESP-IDF | HTTP server for LAN video streaming |

## 🤝 Contributing

Contributions are welcome! Please follow these steps:

1. Fork the project
2. Create your feature branch (`git checkout -b feature/AmazingFeature`)
3. Commit your changes (`git commit -m 'Add some AmazingFeature'`)
4. Push to the branch (`git push origin feature/AmazingFeature`)
5. Open a Pull Request

## 📝 License

The repository currently does not contain a root `LICENSE` file. Confirm and add
the intended license before redistributing the project.

## 🙏 Acknowledgments

- Based on example projects from [Espressif Systems](https://github.com/espressif)
- LVGL graphics library — [lvgl/lvgl](https://github.com/lvgl/lvgl)

## 📬 Contact

Project Link: [https://github.com/Garfield-1314/Cat-Food-Machine](https://github.com/Garfield-1314/Cat-Food-Machine)
