# 🐱 Cat Food Machine — 智能猫粮投喂机

English | [中文版](./README_zh.md)

An ESP32-S3 based **intelligent cat feeder** with touchscreen UI, WiFi connectivity, scheduled feeding, and stepper motor control.

## 📋 Features

- **Touchscreen UI** — 2.8" 320x240 SPI LCD (ST7789) with LVGL graphical interface
- **Capacitive Touch** — GT911 touch sensor (I2C) for smooth user interaction
- **Multi-page Interface** — Home page, app launcher, feeding control, settings, WiFi configuration
- **Scheduled Feeding** — Up to 8 schedules with a fixed feeding time (HH:MM) and a repeat interval in days (daily / every other day / every 3 days, ...), with NVS persistence
- **Stepper Motor Dispensing** — A4988 driven stepper motor, 1 slot = 90° rotation
- **WiFi Connectivity** — Station mode with saved credentials, on-screen WiFi configuration
- **SNTP Time Sync** — Automatic time synchronization via NTP (Beijing time, UTC+8)
- **Auto Backlight Dimming** — Automatically dims backlight after 5 minutes of inactivity
- **USB Virtual Serial** — TinyUSB CDC for debug logging and communication

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
│   │   │   │                #   - wifi_app.h, sntp_time.h
│   │   │   └── src/         # Driver implementations
│   │   ├── driver/          # Application-specific drivers
│   │   │   ├── inc/         #   - feeder_motor.h, feeding_schedule.h
│   │   │   │                #   - tusb_serial.h
│   │   │   └── src/         # Driver implementations
│   │   └── ui/              # LVGL user interface
│   │       ├── inc/         #   - ui.h, app_page.h, feeding_page.h
│   │       │                #   - setting_page.h, wifi_config_page.h
│   │       └── src/         # UI implementations
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

## 🖥️ Hardware Configuration

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
| TinyUSB   | —       | USB CDC virtual serial |
| A4988     | —       | Stepper motor driver |

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
