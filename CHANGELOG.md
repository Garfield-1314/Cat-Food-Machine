# Changelog

All notable changes to the **Cat Food Machine** project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [0.2.2] - 2026-09-06

### Added

- **IR fill light PWM dimming and automatic brightness control**
  - The IR fill light (GPIO43/TXD0) is now driven by a dedicated LEDC PWM channel (11-bit, 20 kHz) instead of a plain GPIO toggle, and a `ir_light_set_brightness()` API allows software dimming of the light
  - A new auto-brightness module starts with the camera stream and reads the OV2640 exposure/gain/average-Y registers on a control loop; when the scene is still dark at maximum exposure it raises the PWM duty, and when the scene is bright enough it lowers it, keeping night-vision images correctly exposed without manual intervention
  - The auto controller never exceeds a configurable brightness ceiling persisted in NVS (`ir_config`/`max`, default 60 %); the ceiling API and the loop thresholds (deadband, step, saturation) are exposed as tunables pending on-device calibration in a dark room

## [0.2.1] - 2026-09-04

### Fixed

- **Backlight brightness restoration after automatic screen-off**
  - Fixed an issue where waking the display after lowering the brightness restored the backlight to 100% instead of the user-selected level
  - The brightness selected before dimming is now kept separately from the temporary 0% PWM output and is restored for both touch wake and feeding-triggered wake

## [0.2.0] - 2026-09-03

### Fixed

- **HTTP-MJPEG stalls, tearing, and manual-refresh recovery**
  - Replaced the unsafe capture-buffer handoff with two 57,600-byte internal-DMA buffers and an independent, reusable JPEG send copy. Capture never overwrites a frame while HTTP is sending it
  - Added low-level partial-send-safe retries for socket `EAGAIN` (2-second wait, two retries). Frame pacing now caps at 10 FPS without catch-up bursts after a slow send
  - The browser page automatically reconnects after a terminal stream error and releases the stream while the page is hidden; a page refresh is no longer required for recovery

- **False PSRAM allocation errors**
  - Camera and LVGL allocation paths now check `CONFIG_SPIRAM`; on the Cat board they request internal DMA SRAM directly instead of deliberately failing a PSRAM request first

- **Flash-size header mismatch**
  - Changed the image header from 2 MB to the N16 module's detected 16 MB, eliminating `Detected size(16384k) larger than ... header(2048k)` at boot

- **Infinite reboot loop (PSRAM pin conflict)**
  - **Symptom**: Device could not boot — it kept resetting in a loop (`rst:0x8 (TG1WDT_SYS_RST)`), with no crash backtrace; the log stopped right after LCD init.
  - **Root cause**: The N16R8 module's octal PSRAM data lines D2/D3/D4 are fixed on GPIO35/36/37, but the Cat board routes those same three pins to the ST7789 LCD (CLK=35 / MOSI=36 / CS=37). PSRAM detection and the memory test pass, but once the LCD driver starts driving those pins, the MSPI (PSRAM) bus and SPI2 (LCD) contend for the same pins — any PSRAM access (heap allocation, LVGL buffer read/write) hangs the MSPI bus forever → CPU stalls → the task watchdog (TG1WDT) resets the system → infinite loop.
  - **Diagnosis**: With the USB-Serial-JTAG console visible, the log showed PSRAM init OK + memory test OK, yet a hard WDT reset ~1.2 s after LCD initialization with no abort backtrace (i.e., a hang, not a crash). Toggling `CONFIG_SPIRAM` reproduced / eliminated the loop.
  - **Fix**: Keep `CONFIG_SPIRAM` disabled on the Cat board (restored in `sdkconfig.defaults` with an explanatory comment). The 8 MB PSRAM chip itself is fine — it is a board-level pin conflict that makes PSRAM unusable on this design. Only re-enable it on a board with free PSRAM pins (e.g., ESP32-S3-EYE).

### Changed

- **Feeder hopper changed from 4 bins to 6 bins**
  - Per-slot output rotation reduced from 90° to 60° (360° / 6); `STEPS_PER_SLOT` recalculated from 3,600 to `3200 × (60° × 4.5 / 360°) = 2400` microsteps
  - Maximum feeding amount per manual/scheduled feed reduced from 10 to 6 slots (UI slider, schedule sanitize, and motor-driver validation); previously stored schedules with amount 7–10 are clamped to 6 on load

- **Single saved WiFi profile**
  - Documented that the current Station-mode firmware stores one SSID/password pair in NVS; saving another network overwrites the previous profile and the latest profile is used for automatic connection after reboot
  - Clarified that the WiFi page's 24-item scan list is temporary and is not a multi-network credential list

- **ESP32-S3-EYE OV2640 high-quality web stream**
  - Enabled the ESP32-S3-EYE LCD/pin profile and Octal PSRAM configuration for the EYE hardware mode; the Cat-board profile remains separate
  - Changed OV2640 output from the 240×240 square stream to native 640×480 hardware JPEG, with JPEG quality 10 configurable in the project header
  - Added two PSRAM-preferred 2 MiB DMA capture buffers and JPEG end-marker detection for the ESP32-S3 DVP path, including buffer-limit and invalid-frame diagnostics
  - Kept the Web stream rate at an adjustable default of 10 FPS (`VIDEO_STREAM_FPS`); JPEG send copies also prefer PSRAM and the latest-frame-only low-latency policy is retained
  - Moved streaming work to an asynchronous HTTP task and restored ESP-IDF's standard chunked MJPEG response API; browser reconnects now use backoff instead of retrying every second
  - Leaving the video page closes the browser's stream socket, so `errno=104` and camera stop/release are treated as the expected on-demand lifecycle

- **Internal-memory and WiFi resource profile**
  - LVGL draw buffer reduced to one 320×10 RGB565 buffer (6,400 bytes); LVGL pool reduced to 20 KiB; `app_main()` now returns after creating the LVGL task so ESP-IDF can release the main-task stack
  - WiFi RX uses 8 static buffers, a dynamic limit of 24, and BA window 6; dynamic TX remains 32. General WiFi IRAM optimization remains enabled, while RX IRAM optimization is disabled
  - Disabled unused IPv6, SoftAP/DHCP server, enterprise WiFi, WPA3/OWE, SAE-PK/H2E, and GMAC options for the IPv4/WPA2 Station-only product profile
  - Current `idf.py size`: 134,935 bytes static DIRAM (39.48%); runtime camera/JPEG/WiFi dynamic allocations are not included in this figure

- **Console switched from TinyUSB CDC to the built-in USB-Serial-JTAG**
  - The chip ROM's USB-Serial-JTAG virtual serial port works from the very first boot stage, so debug logs (ROM bootloader, second-stage bootloader, app) stay visible across any reboot without re-enumeration; no USB-TTL adapter needed
  - Removed `tusb_serial` driver and the `espressif/esp_tinyusb` dependency (`CONFIG_TINYUSB_CDC_ENABLED` off, `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`)

- **Camera switched to web-only preview**
  - Removed the local LCD camera page (`camera_page`) and the JPEG preview decoder (`jpeg_preview`); the home-page camera icon now shows the web stream URL (`http://<device-ip>/`) in a popup
  - Reason: with no usable PSRAM, internal SRAM (~250 KB free heap) cannot simultaneously host the camera frame buffer, the RGB565 decode output, WiFi, and LVGL; the web stream only needs the JPEG frame buffer
  - Final memory layout: two 57,600-byte camera DMA buffers are allocated only while streaming, with a compressed-size send copy rounded to 4 KiB; LVGL uses one 320×10 draw buffer and a 20 KiB pool

- **SNTP Kconfig cleanup**
  - Replaced deprecated `CONFIG_LWIP_SNTP` / `CONFIG_SNTP_*` symbols with the IDF 5.5 names (`CONFIG_LWIP_SNTP_MAX_SERVERS=3`), removing confgen "unknown symbol" warnings; smooth sync is configured via API (`sntp_set_sync_mode`) so behavior is unchanged

### Added

- **OV2640 Camera**
  - DVP 8-bit parallel interface with SCCB on dedicated I2C_NUM_1 (pins D0-D7: 11,9,8,10,12,18,17,16; VSYNC:6, DE:7, PCLK:13, XCLK:15, SCCB SCL:5, SDA:4)
  - Project-level 240×240 square JPEG output derived through the component's public format API; `managed_components` remains unmodified
  - Added the `esp_cam_sensor` dependency; frame buffers use internal DMA SRAM because PSRAM is intentionally disabled on this board

- **On-demand LAN video streaming**
  - Added a modular `esp_http_server` HTTP-MJPEG service with `/` browser page and `/stream` endpoint
  - HTTP service starts after WiFi obtains an IP; camera capture and frame buffers start only when needed
  - Square JPEG 240×240 stream at up to 10 FPS; LAN-only access without authentication
  - Only one stream client is allowed at a time; a second client receives HTTP 503
  - Reference-counted acquire/release lifecycle starts capture and allocates frame buffers only while a stream client is active

- **Home page enhancements**
  - Restored the feeding home page as the default page at boot
  - Added a next-feeding countdown displayed in hours and minutes, without seconds
  - Displays `Next feed: No schedule` when no feeding plan is configured
  - Displays the assigned device IP below the `Feed` button after WiFi connection

### Changed

- **Board configuration**
  - Restored the Cat board LCD IO profile (MOSI:36, CLK:35, CS:37, DC:38, RST:47, BL:48), SPI mode 0, and Cat backlight settings
  - Re-enabled the Cat board GT911 touch input; ESP32-S3-EYE LCD pins remain as a commented test profile

- **Documentation**
  - Updated English and Chinese README files with current board pins, UI behavior, streaming URLs, parameters, and client limits

- **Documentation completeness**
  - English and Chinese README documentation for root project
  - `CHANGELOG.md` (this file)
  - Fixed `esp32libraries/README.md` and `README_zh.md` directory paths to match actual example names

- **Standalone LVGL example** under `esp32libraries/example/LVGL/`
  - Extracted core ST7789 LCD driver, GT911 touch driver, and LVGL integration from the main project
  - TinyUSB CDC virtual serial for debug logging
  - Interactive demo UI with touch-responsive buttons
  - Self-contained IDF project with sdkconfig.defaults and component dependencies

- **Documentation alignment**
  - Fixed stepper motor pin table in READMEs to match `feeder_motor.c` (EN:45, STEP:39, DIR:40, MS1:41, MS2:42, MS3:3)
  - Fixed project structure trees to show README/CHANGELOG at repository root
  - Corrected the motor dispensing description (FreeRTOS task + GPIO pulses, not GPTimer) in CHANGELOG and `feeder_motor.h`

- **48-hour feeding cycle**
- **Day-interval schedules**
  - Each schedule item now supports a repeat interval in days: feed at a fixed time (HH:MM) every day, every other day, every 3 days, etc. (up to 7 days)
  - Day rotation is based on local calendar days (Beijing time); legacy 24-hour data and the intermediate per-cycle config are cleaned up automatically

## [0.1.0] - 2026-06-22

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
- **Feeding popup** — Overlay showing feeding progress with motor idle detection

#### Motor & Feeding

- **Stepper motor driver** (A4988) — GPIO-triggered step pulse generation
  - Asynchronous dispensing via a dedicated FreeRTOS task with GPIO pulse generation
  - Synchronous (blocking) dispensing for calibration
  - STEP/DIR/EN/MS pin definitions in `feeder_motor.c`
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

---

# 更新日志

所有 **Cat Food Machine** 项目的重要变更都会记录在此文件中。

格式基于 [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)，
项目版本遵循 [语义化版本规范](https://semver.org/spec/v2.0.0.html)。

---

## [0.2.1] - 2026-09-04

### 修复

- **自动熄屏后背光亮度恢复**
  - 修复调低屏幕亮度后自动熄屏，触摸唤醒时背光恢复为 100% 而不是用户设置亮度的问题
  - 现在将熄屏前的用户亮度与临时关闭背光的 0% PWM 输出分开保存，触摸唤醒和投喂唤醒都会恢复原亮度

## [0.2.0] - 2026-09-03

### 修复

- **HTTP-MJPEG 卡死、撕裂与手动刷新恢复**
  - 将不安全的采集缓冲直接传递改为两块 57,600 字节内部 DMA 缓冲 + 独立复用的 JPEG 发送副本，HTTP 发送期间采集不会覆盖正在使用的帧
  - 对 socket `EAGAIN` 增加了保持已发送偏移的底层重试（单次等待 2 秒，最多重试 2 次）；帧率调度在慢发送后不再追赶补帧
  - 流最终断开后浏览页会自动重连，页面转入后台时主动释放流，恢复时不再需要手动刷新

- **虚假的 PSRAM 分配错误**
  - 摄像头与 LVGL 分配路径现在会检查 `CONFIG_SPIRAM`；Cat 板直接申请内部 DMA SRAM，不再每次先触发一次必然失败的 PSRAM 申请

- **Flash 镜像头大小不匹配**
  - 将镜像头从 2MB 改为 N16 模块实际的 16MB，消除启动时的 `16384k` / `2048k` 不匹配警告

- **PSRAM 与 LCD 引脚冲突导致无限重启**
  - **现象**：设备无法启动，反复重启（`rst:0x8 (TG1WDT_SYS_RST)`），没有崩溃回溯，日志在 LCD 初始化后停止
  - **根因**：模块 N16R8 的八线 PSRAM 数据线 D2/D3/D4 固定在 GPIO35/36/37，而 Cat 板把这 3 个引脚接给了 ST7789 LCD（CLK=35/MOSI=36/CS=37）。PSRAM 检测与内存测试可通过，但 LCD 驱动开始驱动这些引脚后，MSPI（PSRAM）总线与 SPI2（LCD）争用同一组引脚；任何 PSRAM 访问（堆分配、LVGL 缓冲读写）都会使 MSPI 总线永久挂起，CPU 卡死后由任务看门狗（TG1WDT）复位，形成无限重启
  - **定位过程**：通过 USB-Serial-JTAG 串口日志可看到 PSRAM 初始化与内存测试均通过，却在 LCD 初始化后约 1.2 秒被硬复位且无 abort 回溯（卡死而非崩溃）；切换 `CONFIG_SPIRAM` 开关可复现/消除该问题
  - **解决**：Cat 板上保持 PSRAM 关闭（`sdkconfig.defaults` 已注明原因）。8MB PSRAM 芯片本身完好，属于板级引脚冲突导致不可用；只有在 PSRAM 引脚未被占用的板卡（例如 ESP32-S3-EYE）上才重新启用

### 变更

- **食仓由 4 仓改为 6 仓**
  - 每仓位输出轴旋转量从 90° 调整为 60°（360° / 6），`STEPS_PER_SLOT` 按公式重新计算：`3200 × (60° × 4.5 / 360°) = 2400` 微步
  - 每次手动/定时投喂的仓位上限由 10 调整为 6（UI 滑块、计划校验与电机驱动校验）；此前保存的 7–10 仓位计划在加载时会被夹取为 6

- **单个已保存 WiFi 配置**
  - 明确当前 Station 模式固件仅在 NVS 中保存一组 SSID/密码；保存新网络会覆盖旧配置，设备重启后自动连接最后保存的网络
  - 说明 WiFi 页面显示的最多 24 个扫描结果只是临时扫描列表，不是多网络凭据列表

- **ESP32-S3-EYE OV2640 高画质 Web 推流**
  - 启用 ESP32-S3-EYE 的 LCD/引脚配置与八线 PSRAM 配置用于 EYE 硬件模式；Cat 板配置保持独立
  - OV2640 输出从 240×240 方形 JPEG 改为原生 640×480 硬件 JPEG，JPEG quality 默认值为 10，可在项目头文件中调整
  - 增加两块优先使用 PSRAM 的 2 MiB DMA 采集缓冲，并针对 ESP32-S3 DVP 路径增加 JPEG 结束标志检测、缓冲区溢出和无效帧诊断
  - Web 推流默认保持可调的 10 FPS（`VIDEO_STREAM_FPS`）；JPEG 发送副本也优先使用 PSRAM，并保留只发送最新帧的低延迟策略
  - 推流改为异步 HTTP 任务，并恢复使用 ESP-IDF 标准 chunked MJPEG 响应 API；浏览器重连改为退避重试，不再每秒持续重试
  - 离开视频页面会关闭浏览器推流 socket，因此 `errno=104` 及摄像头停止/释放属于按需推流的正常生命周期行为

- **内部内存与 WiFi 资源配置**
  - LVGL 绘制缓冲降为单块 320×10 RGB565（6,400 字节），LVGL 内存池降为 20KiB；创建 LVGL 任务后 `app_main()` 直接返回，由 ESP-IDF 回收 main task 栈
  - WiFi RX 调整为静态 8、动态上限 24、BA 窗口 6；动态 TX 保持 32。保留 WiFi 通用 IRAM 优化，仅关闭 RX IRAM 优化
  - 针对 IPv4/WPA2 Station 专用场景，关闭未使用的 IPv6、SoftAP/DHCP Server、企业 WiFi、WPA3/OWE、SAE-PK/H2E 与 GMAC
  - 当前 `idf.py size` 结果：静态 DIRAM 134,935 字节（39.48%），不包含运行时摄像头/JPEG/WiFi 动态申请

- **控制台从 TinyUSB CDC 切换到芯片内置 USB-Serial-JTAG**
  - 芯片 ROM 级 USB-Serial-JTAG 虚拟串口从上电第一刻即工作，任意重启（panic、看门狗复位、重新上电）都不掉线、不重新枚举，ROM/二级 bootloader 与应用日志全程可见；无需 USB-TTL 转换器
  - 移除 `tusb_serial` 驱动与 `espressif/esp_tinyusb` 依赖（关闭 `CONFIG_TINYUSB_CDC_ENABLED`，启用 `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`）

- **摄像头改为仅 Web 预览**
  - 移除本地 LCD 摄像头页面（`camera_page`）与 JPEG 预览解码器（`jpeg_preview`）；主页相机图标改为弹出 Web 推流地址（`http://<device-ip>/`）
  - 原因：无可用 PSRAM 时内部 SRAM（可用堆约 250 KB）不足以同时承载相机帧缓冲、RGB565 解码输出、WiFi 与 LVGL；Web 推流仅需 JPEG 帧缓冲
  - 最终内存布局：两块 57,600 字节摄像头 DMA 缓冲仅在推流时分配，发送副本按 JPEG 压缩大小向上取整到 4KiB；LVGL 使用单块 320×10 绘制缓冲和 20KiB 内存池

- **SNTP 过时配置符号清理**
  - 用 IDF 5.5 的符号名替换过时的 `CONFIG_LWIP_SNTP` / `CONFIG_SNTP_*`（`CONFIG_LWIP_SNTP_MAX_SERVERS=3`），消除 confgen 未知符号警告；平滑同步由 API（`sntp_set_sync_mode`）配置，行为不变

### 新增

- **OV2640 摄像头**
  - DVP 8-bit 并行接口，SCCB 使用独立 I2C_NUM_1 总线（D0-D7: 11,9,8,10,12,18,17,16；VSYNC:6、DE:7、PCLK:13、XCLK:15、SCCB SCL:5、SDA:4）
  - 通过组件公开格式 API 在项目层派生 240×240 方形 JPEG 输出，不修改 `managed_components`
  - 引入 `esp_cam_sensor` 组件依赖；本板主动关闭 PSRAM，帧缓冲使用内部 DMA SRAM

- **局域网按需视频推流**
  - 新增模块化 `esp_http_server` HTTP-MJPEG 服务，提供 `/` 浏览页面和 `/stream` 推流接口
  - WiFi 获取 IP 后仅启动 HTTP 服务，摄像头采集和帧缓冲区只在实际使用时启动
  - 方形 JPEG 240×240 推流，最高 10 FPS，仅支持局域网访问且不启用鉴权
  - 同时只允许一个推流客户端，第二个客户端返回 HTTP 503
  - 通过引用计数 acquire/release 生命周期，仅在推流客户端存在时启动采集并分配帧缓冲

- **主页功能完善**
  - 恢复投喂主页为开机默认页面
  - 新增下一次投粮倒计时，仅显示小时和分钟，不显示秒
  - 未设置投喂计划时显示 `Next feed: No schedule`
  - WiFi 连接成功后，在 `Feed` 按钮下方显示设备 IP

### 变更

- **板卡配置**
  - 恢复 Cat 板 LCD IO 配置（MOSI:36、CLK:35、CS:37、DC:38、RST:47、BL:48）、SPI mode 0 及 Cat 板背光配置
  - 重新启用 Cat 板 GT911 触摸输入，ESP32-S3-EYE LCD 引脚保留为注释测试配置

- **文档更新**
  - 更新中英文 README，补充当前板卡引脚、界面行为、推流地址、参数及客户端限制

- **文档完善**
  - 根项目的中英文 README 说明文档
  - `CHANGELOG.md`（本更新日志文件）
  - 修正 `esp32libraries/README.md` 与 `README_zh.md` 中的目录路径以匹配实际示例名称

- **独立 LVGL 示例工程**（位于 `esp32libraries/example/LVGL/`）
  - 从主工程中提取核心 ST7789 LCD 驱动、GT911 触摸驱动及 LVGL 集成代码
  - TinyUSB CDC 虚拟串口调试日志
  - 交互式演示 UI，支持触摸按钮响应
  - 独立的 IDF 工程，包含编译配置和依赖声明

- **文档对齐**
  - 修正两份 README 中的电机引脚表，使其与 `feeder_motor.c` 一致（EN:45、STEP:39、DIR:40、MS1:41、MS2:42、MS3:3）
  - 修正项目结构树，将 README/CHANGELOG 移到仓库根目录
  - 修正 CHANGELOG 与 `feeder_motor.h` 中关于出粮实现的描述（GPTimer → FreeRTOS 任务 + GPIO 脉冲）

- **48 小时（两天）投喂周期**
- **按间隔天数的投喂计划**
  - 每条计划支持按天间隔重复：在固定时间（时:分）投喂，可设为每天 / 隔 1 天 / 隔 2 天……（最长 7 天）
  - 天数轮换依据本地自然日（北京时间），旧版 24 小时数据与中间版“每周期次数+间隔”配置自动清理

## [0.1.0] - 2026-06-22

### 新增

#### 核心应用

- 基于 ESP-IDF v5.5.1 构建系统（CMake）的项目框架
- 应用入口点（`main.c`），包含组件初始化和事件循环
- LVGL 事件任务以 50Hz 频率运行，用于 UI 刷新
- 全局头文件聚合所有模块依赖

#### 显示与触摸

- **ST7789** LCD 驱动 — 320×240 SPI 显示屏，带 PWM 背光控制
  - 像素/填充/位图/图像绘制 API
  - 亮度读写与 NVS 持久化
  - 自定义 SPI 总线配置（MOSI:36、CLK:35、CS:37、DC:38、RST:47、BL:48）
- **GT911** 电容触摸驱动 — I2C 接口（SDA:2、SCL:1、INT:21、RST:14）
  - 多点触控支持（最多 5 点）
  - 可配置分辨率、旋转方向和 I2C 地址
  - 触摸事件读取与坐标变换
- **LVGL**（v8.4.0）集成
  - 显示与输入设备绑定
  - 基于 ST7789 的自定义刷新回调
  - 基于 GT911 的触摸读取回调

#### 用户界面

- **主页面**（`ui.c`）— 页面切换，带消抖保护
- **应用启动页面**（`app_page.c`）— 应用图标网格
- **投喂控制页面**（`feeding_page.c`）
  - 仓格数量选择器（+/- 按钮）
  - 手动投喂触发
  - 投喂计划列表（最多 8 项）
  - 添加/编辑/删除计划项，含时间选择器
- **设置页面**（`setting_page.c`）
  - WiFi 配置入口
  - 亮度滑块
  - 系统信息显示
- **WiFi 配置页面**（`wifi_config_page.c`）
  - SSID 扫描与列表
  - 密码输入（屏幕键盘）
  - 连接状态反馈
- **投喂弹窗** — 显示投喂进度的覆盖层，含电机空闲检测

#### 电机与投喂

- **步进电机驱动**（A4988）— GPIO 触发步进脉冲生成
  - 通过专用 FreeRTOS 任务 + GPIO 忙等发脉冲异步出粮
  - 同步（阻塞）出粮用于校准
  - STEP/DIR/EN/MS 引脚定义于 `feeder_motor.c`
  - 空闲状态轮询检测完成
- **投喂计划管理器**
  - NVS 存储的计划数据（最多 8 项）
  - 计划项的增删改查操作
  - 后台调度任务（1 秒间隔）
  - 1 分钟冷却防止重复触发

#### 网络连接

- **WiFi Station 模式**
  - NVS 持久化凭据（SSID/密码）
  - 开机自动连接保存的配置
  - 连接/断开/状态 API
  - 连接成功回调注册
  - 屏幕 WiFi 配置流程
- **SNTP 时间同步**
  - 阿里云 NTP 服务器（ntp.aliyun.com）
  - 北京时间（UTC+8）
  - 本地时间获取与格式化输出
  - 带超时的阻塞同步

#### 系统服务

- **TinyUSB CDC** 虚拟串口 — 通过 USB 输出 ESP 日志
- **NVS 闪存**初始化，带自动分区修复
- **背光自动熄灭** — 5 分钟无操作自动熄灭背光，触摸唤醒
- **投喂事件背光唤醒**（手动或定时投喂）
