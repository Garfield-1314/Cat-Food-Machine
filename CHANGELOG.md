# Changelog

All notable changes to the **Cat Food Machine** project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- English and Chinese README documentation for root project
- CHANGELOG.md (this file)
- **Standalone LVGL example** under `esp32libraries/example/LVGL/`:
  - Extracted core ST7789 LCD driver, GT911 touch driver, and LVGL integration
  - TinyUSB CDC virtual serial for debug logging
  - Interactive demo UI with touch-responsive buttons
  - Self-contained IDF project with sdkconfig.defaults and component dependencies
- Fixed `esp32libraries/README.md` and `README_zh.md` directory paths to match actual example names

## [0.1.0] — 2026-06-22

### Added

#### Core Application
- Project scaffolding with ESP-IDF v5.5.1 build system (CMake)
- Application entry point (`main.c`) with component initialization and event loop
- LVGL event task running at 50 Hz for UI updates
- Global include header aggregating all module dependencies

#### Display & Touch
- **ST7789** LCD driver — 320×240 SPI display with backlight PWM control
  - Pixel/fill/bitmap/image drawing API
  - Brightness get/set with NVS persistence
  - Custom SPI bus configuration (MOSI:36, CLK:35, CS:37, DC:38, RST:47, BL:48)
- **GT911** capacitive touch driver — I2C interface (SDA:2, SCL:1, INT:21, RST:14)
  - Multi-touch support (up to 5 points)
  - Configurable resolution, rotation, and I2C address
  - Touch event reading and coordinate transformation
- **LVGL** (v8.4.0) integration
  - Display and input device binding
  - Custom display flush callback using ST7789
  - Touch read callback using GT911

#### User Interface
- **Main page** (`ui.c`) — Page switching with debounce protection
- **App launcher page** (`app_page.c`) — Grid of application icons
- **Feeding control page** (`feeding_page.c`)
  - Slot count selector with +/- buttons
  - Manual feed trigger
  - Feeding schedule list (up to 8 items)
  - Add/edit/delete schedule items with time picker
- **Settings page** (`setting_page.c`)
  - WiFi configuration entry
  - Brightness slider
  - System info display
- **WiFi config page** (`wifi_config_page.c`)
  - SSID scanning and listing
  - Password input with on-screen keyboard
  - Connection status feedback
- **Feeding popup** — Animated overlay showing feeding progress with motor idle detection

#### Motor & Feeding
- **Stepper motor driver** (A4988) — GPIO-triggered step pulse generation
  - Asynchronous dispensing via GPTimer hardware timer
  - Synchronous (blocking) dispensing for calibration
  - Configurable STEP/DIR/EN pins
  - Idle state polling for completion detection
- **Feeding schedule manager**
  - NVS-backed schedule storage (up to 8 items)
  - CRUD operations for schedule items (add/set/remove/load/save)
  - Background scheduler task (1-second interval)
  - 1-minute cooldown to prevent duplicate triggers

#### Connectivity
- **WiFi Station mode**
  - NVS-persisted credentials (SSID/password)
  - Auto-connect on boot using saved config
  - Connect/disconnect/status API
  - Connected callback registration
  - On-screen WiFi configuration workflow
- **SNTP time synchronization**
  - Alibaba NTP server (ntp.aliyun.com)
  - Beijing timezone (UTC+8)
  - Local time getter with formatted string output
  - Blocking sync with timeout support

#### System Services
- **TinyUSB CDC** virtual serial port — ESP logging output over USB
- **NVS flash** initialization with automatic partition repair
- **Auto backlight dimming** — 5-minute inactivity timeout with touch wake
- **Backlight restoration** on feeding events (manual or scheduled)