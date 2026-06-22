# Changelog / 更新日志

All notable changes to the **Cat Food Machine** project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased] / 未发布

### Added / 新增

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
- **Feeding popup** — Animated overlay showing feeding progress with motor idle detection
- **投喂弹窗** — 显示投喂进度的动画覆盖层，含电机空闲检测

#### Motor & Feeding / 电机与投喂
- **Stepper motor driver** (A4988) — GPIO-triggered step pulse generation
- **步进电机驱动** (A4988) — GPIO 触发步进脉冲生成
  - Asynchronous dispensing via GPTimer hardware timer — 通过 GPTimer 硬件定时器异步出粮
  - Synchronous (blocking) dispensing for calibration — 同步（阻塞）出粮用于校准
  - Configurable STEP/DIR/EN pins — 可配置 STEP/DIR/EN 引脚
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