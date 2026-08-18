# Changelog / 更新日志

All notable changes to the **Cat Food Machine** project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased] / 未发布

### Changed / 变更

- **Console switched from TinyUSB CDC to the built-in USB-Serial-JTAG / 控制台从 TinyUSB CDC 切换到芯片内置 USB-Serial-JTAG**
  - The chip ROM's USB-Serial-JTAG virtual serial port works from the very first boot stage, so debug logs (ROM bootloader, second-stage bootloader, app) stay visible across any reboot without re-enumeration; no USB-TTL adapter needed
  - 芯片 ROM 级 USB-Serial-JTAG 虚拟串口从上电第一刻即工作，任意重启（panic、看门狗复位、重新上电）都不掉线、不重新枚举，ROM/二级 bootloader 与应用日志全程可见；无需 USB-TTL 转换器
  - Removed `tusb_serial` driver and the `espressif/esp_tinyusb` dependency (`CONFIG_TINYUSB_CDC_ENABLED` off, `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`)
  - 移除 `tusb_serial` 驱动与 `espressif/esp_tinyusb` 依赖（关闭 TinyUSB CDC，启用 `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`）

### Added / 新增

- **OV2640 Camera / OV2640 摄像头**
  - DVP 8-bit parallel interface with SCCB on dedicated I2C_NUM_1 (pins D0-D7: 11,9,8,10,12,18,17,16; VSYNC:6, DE:7, PCLK:13, XCLK:15, SCCB SCL:5, SDA:4)
  - DVP 8-bit 并行接口，SCCB 使用独立 I2C_NUM_1 总线（D0-D7: 11,9,8,10,12,18,17,16；VSYNC:6、DE:7、PCLK:13、XCLK:15、SCCB SCL:5、SDA:4）
  - Native JPEG 320×240 capture with ROM JPEG decoding to RGB565 for the 320×240 ST7789 LCD
  - 使用原生 JPEG 320×240 采集，并通过 ESP32-S3 ROM JPEG 解码器转换为 RGB565 显示到 320×240 ST7789 屏幕
  - New Camera page, opened from the camera icon on the feeding home page
  - 新增“摄像头”页面，可从投喂主页的摄像头图标进入
  - `esp_cam_sensor` component dependency and PSRAM enabled for frame buffers
  - 引入 `esp_cam_sensor` 组件依赖并启用 PSRAM 用于帧缓冲

- **On-demand LAN video streaming / 局域网按需视频推流**
  - Added a modular `esp_http_server` HTTP-MJPEG service with `/` browser page and `/stream` endpoint
  - 新增模块化 `esp_http_server` HTTP-MJPEG 服务，提供 `/` 浏览页面和 `/stream` 推流接口
  - HTTP service starts after WiFi obtains an IP; camera capture and frame buffers start only when needed
  - WiFi 获取 IP 后仅启动 HTTP 服务，摄像头采集和帧缓冲区只在实际使用时启动
  - Native JPEG 320×240 stream at up to 10 FPS; LAN-only access without authentication
  - 原生 JPEG 320×240 推流，最高 10 FPS，仅支持局域网访问且不启用鉴权
  - Only one stream client is allowed at a time; a second client receives HTTP 503
  - 同时只允许一个推流客户端，第二个客户端返回 HTTP 503
  - Shared camera acquire/release lifecycle keeps LCD preview and HTTP streaming from stopping each other
  - 增加摄像头 acquire/release 生命周期管理，避免屏幕预览和 HTTP 推流互相停止摄像头

- **Home page enhancements / 主页功能完善**
  - Restored the feeding home page as the default page at boot
  - 恢复投喂主页为开机默认页面
  - Added a next-feeding countdown displayed in hours and minutes, without seconds
  - 新增下一次投粮倒计时，仅显示小时和分钟，不显示秒
  - Displays `Next feed: No schedule` when no feeding plan is configured
  - 未设置投喂计划时显示 `Next feed: No schedule`
  - Displays the assigned device IP below the `Feed` button after WiFi connection
  - WiFi 连接成功后，在 `Feed` 按钮下方显示设备 IP

### Changed / 变更

- **Board configuration / 板卡配置**
  - Restored the Cat board LCD IO profile (MOSI:36, CLK:35, CS:37, DC:38, RST:47, BL:48), SPI mode 0, and Cat backlight settings
  - 恢复 Cat 板 LCD IO 配置（MOSI:36、CLK:35、CS:37、DC:38、RST:47、BL:48）、SPI mode 0 及 Cat 板背光配置
  - Re-enabled the Cat board GT911 touch input; ESP32-S3-EYE LCD pins remain as a commented test profile
  - 重新启用 Cat 板 GT911 触摸输入，ESP32-S3-EYE LCD 引脚保留为注释测试配置

- **Documentation / 文档更新**
  - Updated English and Chinese README files with current board pins, UI behavior, streaming URLs, parameters, and client limits
  - 更新中英文 README，补充当前板卡引脚、界面行为、推流地址、参数及客户端限制

- **Documentation / 文档完善**
  - English and Chinese README documentation for root project
  - 根项目的中英文 README 说明文档
  - CHANGELOG.md (this file) — 本更新日志文件
  - Fixed `esp32libraries/README.md` and `README_zh.md` directory paths to match actual example names
  - 修正 `esp32libraries/README.md` 与 `README_zh.md` 中的目录路径以匹配实际示例名称

- **Standalone LVGL example** under `esp32libraries/example/LVGL/` / 独立 LVGL 示例工程
  - Extracted core ST7789 LCD driver, GT911 touch driver, and LVGL integration from the main project
  - 从主工程中提取核心 ST7789 LCD 驱动、GT911 触摸驱动及 LVGL 集成代码
  - TinyUSB CDC virtual serial for debug logging — TinyUSB CDC 虚拟串口调试日志
  - Interactive demo UI with touch-responsive buttons — 交互式演示 UI，支持触摸按钮响应
  - Self-contained IDF project with sdkconfig.defaults and component dependencies — 独立的 IDF 工程，包含编译配置和依赖声明

- **Documentation alignment / 文档对齐**
  - Fixed stepper motor pin table in READMEs to match `feeder_motor.c` (EN:45, STEP:39, DIR:40, MS1:41, MS2:42, MS3:3)
  - Fixed project structure trees to show README/CHANGELOG at repository root
  - Corrected the motor dispensing description (FreeRTOS task + GPIO pulses, not GPTimer) in CHANGELOG and `feeder_motor.h`
  - 修正两份 README 中的电机引脚表，使其与 `feeder_motor.c` 一致
  - 修正项目结构树，将 README/CHANGELOG 移到仓库根目录
  - 修正 CHANGELOG 与 `feeder_motor.h` 中关于出粮实现（GPTimer → FreeRTOS 任务 + GPIO 脉冲）的描述

- **48-hour feeding cycle / 48 小时（两天）投喂周期**
- **Day-interval schedules / 按间隔天数的投喂计划**
  - Each schedule item now supports a repeat interval in days: feed at a fixed time (HH:MM) every day, every other day, every 3 days, etc. (up to 7 days)
  - Day rotation is based on local calendar days (Beijing time); legacy 24-hour data and the intermediate per-cycle config are cleaned up automatically
  - 每条计划支持按天间隔重复：在固定时间（时:分）投喂，可设为每天 / 隔 1 天 / 隔 2 天……（最长 7 天）
  - 天数轮换依据本地自然日（北京时间），旧版 24 小时数据与中间版"每周期次数+间隔"配置自动清理

---

## [0.1.0] — 2026-06-22

### Added / 新增

#### Core Application / 核心应用
- Project scaffolding with ESP-IDF v5.5.1 build system (CMake)
- 基于 ESP-IDF v5.5.1 构建系统 (CMake) 的项目框架
- Application entry point (`main.c`) with component initialization and event loop
- 应用入口点 (`main.c`)，包含组件初始化和事件循环
- LVGL event task running at 50 Hz for UI updates
- LVGL 事件任务以 50Hz 频率运行，用于 UI 刷新
- Global include header aggregating all module dependencies
- 全局头文件聚合所有模块依赖

#### Display & Touch / 显示与触摸
- **ST7789** LCD driver — 320×240 SPI display with backlight PWM control
- **ST7789** LCD 驱动 — 320×240 SPI 显示屏，带 PWM 背光控制
  - Pixel/fill/bitmap/image drawing API — 像素/填充/位图/图像绘制 API
  - Brightness get/set with NVS persistence — 亮度读写与 NVS 持久化
  - Custom SPI bus configuration (MOSI:36, CLK:35, CS:37, DC:38, RST:47, BL:48)
  - 自定义 SPI 总线配置
- **GT911** capacitive touch driver — I2C interface (SDA:2, SCL:1, INT:21, RST:14)
- **GT911** 电容触摸驱动 — I2C 接口
  - Multi-touch support (up to 5 points) — 多点触控支持（最多 5 点）
  - Configurable resolution, rotation, and I2C address — 可配置分辨率、旋转方向和 I2C 地址
  - Touch event reading and coordinate transformation — 触摸事件读取与坐标变换
- **LVGL** (v8.4.0) integration — LVGL v8.4.0 集成
  - Display and input device binding — 显示与输入设备绑定
  - Custom display flush callback using ST7789 — 基于 ST7789 的自定义刷新回调
  - Touch read callback using GT911 — 基于 GT911 的触摸读取回调

#### User Interface / 用户界面
- **Main page** (`ui.c`) — Page switching with debounce protection
- **主页面** (`ui.c`) — 页面切换，带消抖保护
- **App launcher page** (`app_page.c`) — Grid of application icons
- **应用启动页面** (`app_page.c`) — 应用图标网格
- **Feeding control page** (`feeding_page.c`) — 投喂控制页面
  - Slot count selector with +/- buttons — 仓格数量选择器 (+/- 按钮)
  - Manual feed trigger — 手动投喂触发
  - Feeding schedule list (up to 8 items) — 投喂计划列表（最多 8 项）
  - Add/edit/delete schedule items with time picker — 添加/编辑/删除计划项，含时间选择器
- **Settings page** (`setting_page.c`) — 设置页面
  - WiFi configuration entry — WiFi 配置入口
  - Brightness slider — 亮度滑块
  - System info display — 系统信息显示
- **WiFi config page** (`wifi_config_page.c`) — WiFi 配置页面
  - SSID scanning and listing — SSID 扫描与列表
  - Password input with on-screen keyboard — 密码输入（屏幕键盘）
  - Connection status feedback — 连接状态反馈
- **Feeding popup** — Overlay showing feeding progress with motor idle detection
- **投喂弹窗** — 显示投喂进度的覆盖层，含电机空闲检测

#### Motor & Feeding / 电机与投喂
- **Stepper motor driver** (A4988) — GPIO-triggered step pulse generation
- **步进电机驱动** (A4988) — GPIO 触发步进脉冲生成
  - Asynchronous dispensing via a dedicated FreeRTOS task with GPIO pulse generation — 通过专用 FreeRTOS 任务 + GPIO 忙等发脉冲异步出粮
  - Synchronous (blocking) dispensing for calibration — 同步（阻塞）出粮用于校准
  - STEP/DIR/EN/MS pin definitions in `feeder_motor.c` — STEP/DIR/EN/MS 引脚定义于 `feeder_motor.c`
  - Idle state polling for completion detection — 空闲状态轮询检测完成
- **Feeding schedule manager** — 投喂计划管理器
  - NVS-backed schedule storage (up to 8 items) — NVS 存储的计划数据（最多 8 项）
  - CRUD operations for schedule items (add/set/remove/load/save) — 计划项的增删改查操作
  - Background scheduler task (1-second interval) — 后台调度任务（1 秒间隔）
  - 1-minute cooldown to prevent duplicate triggers — 1 分钟冷却防止重复触发

#### Connectivity / 网络连接
- **WiFi Station mode** — WiFi Station 模式
  - NVS-persisted credentials (SSID/password) — NVS 持久化凭据
  - Auto-connect on boot using saved config — 开机自动连接保存的配置
  - Connect/disconnect/status API — 连接/断开/状态 API
  - Connected callback registration — 连接成功回调注册
  - On-screen WiFi configuration workflow — 屏幕 WiFi 配置流程
- **SNTP time synchronization** — SNTP 时间同步
  - Alibaba NTP server (ntp.aliyun.com) — 阿里云 NTP 服务器
  - Beijing timezone (UTC+8) — 北京时间 (UTC+8)
  - Local time getter with formatted string output — 本地时间获取与格式化输出
  - Blocking sync with timeout support — 带超时的阻塞同步

#### System Services / 系统服务
- **TinyUSB CDC** virtual serial port — ESP logging output over USB
- **TinyUSB CDC** 虚拟串口 — 通过 USB 输出 ESP 日志
- **NVS flash** initialization with automatic partition repair
- **NVS 闪存**初始化，带自动分区修复
- **Auto backlight dimming** — 5-minute inactivity timeout with touch wake
- **背光自动熄灭** — 5 分钟无操作自动熄灭，触摸唤醒
- **Backlight restoration** on feeding events (manual or scheduled)
- **投喂事件背光唤醒**（手动或定时投喂）
