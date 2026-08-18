# 🐱 Cat Food Machine — 智能猫粮投喂机

[English](./README.md) | 中文

基于 **ESP32-S3** 的智能猫粮投喂机，配备触控屏界面、WiFi 联网、定时投喂及步进电机控制功能。

## 📋 功能特性

- **触控屏 UI** — 2.8 寸 320×240 SPI 液晶屏 (ST7789)，搭载 LVGL 图形界面
- **电容触摸** — GT911 触摸传感器 (I2C)，流畅的用户交互体验
- **多页面界面** — 投喂主页（默认）、投喂计划页、摄像头预览页、设置页、WiFi 配置页
- **定时投喂** — 最多 8 组定时计划，可指定投喂时间（时:分）和间隔天数（每天 / 隔 1 天 / 隔 2 天……），数据持久化存储于 NVS
- **步进电机出粮** — A4988 驱动步进电机，1 仓格 = 90° 旋转
- **OV2640 摄像头** — DVP 8-bit 并行接口，原生 JPEG 320×240 画面在屏幕上预览
- **WiFi 联网** — Station 模式，支持保存凭据、界面配置及设备 IP 显示
- **局域网按需视频推流** — 通过 HTTP-MJPEG 提供视频流，浏览器访问 `http://<设备IP>/`
- **SNTP 时间同步** — 通过 NTP 自动同步时间（北京时间，UTC+8）
- **背光自动熄灭** — 5 分钟无操作自动熄灭背光，触摸唤醒
- **USB 虚拟串口** — TinyUSB CDC 用于调试日志输出与通信

## 🧱 项目结构

```
Cat-Food-Machine/
├── esp32libraries/          # ESP32 库子模块
│   ├── esp-idf/             # ESP-IDF 框架 (v5.5.1)
│   └── example/             # 示例工程 (LVGL, DVP 摄像头)
├── src/                     # 主应用固件
│   ├── CMakeLists.txt       # IDF 工程配置
│   ├── dependencies.lock    # 组件依赖锁定
│   ├── sdkconfig*           # 编译配置
│   ├── main/
│   │   ├── main.c           # 应用入口 & 投喂弹窗管理
│   │   ├── include.h        # 全局头文件包含
│   │   ├── device/          # 硬件设备驱动
│   │   │   ├── inc/         #   - st7789.h, gt911.h, user_lvgl.h
│   │   │   │                #   - ov2640.h, jpeg_preview.h
│   │   │   │                #   - video_frame.h, video_stream.h
│   │   │   │                #   - wifi_app.h, sntp_time.h
│   │   │   └── src/         # 驱动实现
│   │   ├── driver/          # 应用层驱动
│   │   │   ├── inc/         #   - feeder_motor.h, feeding_schedule.h
│   │   │   │                #   - tusb_serial.h
│   │   │   └── src/         # 驱动实现
│   │   └── ui/              # LVGL 用户界面
│   │       ├── inc/         #   - ui.h, app_page.h, feeding_page.h
│   │       │                #   - setting_page.h, wifi_config_page.h
│   │       │                #   - camera_page.h
│   │       └── src/         # UI 实现
├── sim/                     # 离线 UI 模拟器 (PC 调试界面, 无需烧录)
│   ├── main.c               #   SDL2 窗口交互版
│   ├── include/             #   桩头文件 (替代硬件依赖)
│   ├── src/                 #   桩实现
│   └── README.md            #   模拟器使用说明
├── README.md                # 英文说明
├── README_zh.md             # 本文件 (中文说明)
└── CHANGELOG.md             # 版本更新日志
```

## 🚀 快速开始

### 环境要求

- **ESP-IDF v5.5.1**（以子模块形式包含在项目中）
- ESP32-S3 开发板
- 所需硬件组件（见[硬件配置](#硬件配置)）

### 1. 克隆并初始化子模块

```bash
git clone --recursive https://github.com/Garfield-1314/Cat-Food-Machine.git
cd Cat-Food-Machine

# 如果已克隆但未初始化子模块：
git submodule init
git submodule update --progress

cd esp32libraries/esp-idf
git checkout v5.5.1
git submodule update --init --recursive --progress
./install.sh
```

### 2. 编译 & 烧录

```bash
cd src
source ../esp32libraries/esp-idf/export.sh
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

## 🖥️ 离线 UI 模拟器（无需烧录固件调试界面）

固件的 LVGL 界面可以在 **PC 上直接运行调试**，无需每次烧录到 ESP32-S3。

模拟器**直接复用** `src/main/ui/src/*.c`（同一份源码），仅用桩替换硬件依赖
（WiFi / SNTP / 投喂计划 / NVS / 电机等）。

### 环境要求

- CMake ≥ 3.12、C 编译器
- **SDL2**

### 构建与运行

```bash
# 安装 SDL2 (Ubuntu/Debian)
sudo apt install -y libsdl2-dev

cd sim
cmake -S . -B build
cmake --build build -j
./build/cat_food_sim       # 弹出 640×480 窗口，鼠标=触摸，可实时交互调试
```

### 固件改动如何同步

| 固件改动 | 模拟器同步 |
|---|---|
| 修改现有页面 | `cmake --build build` 即可 |
| 新增/删除页面 | `cmake --build build` 即可（自动检测），记得同步更新 `ui.c` 的 `switch_page_cb` |
| 新页面用到新硬件函数 | 需在 `sim/include/` + `sim/src/*_stub.c` 补桩 |

> 更多细节见 [`sim/README.md`](./sim/README.md)。

## 🖥️ 硬件配置

当前启用的是 Cat 板配置。ESP32-S3-EYE 的 LCD 引脚仍保留在源码中，
但处于注释状态。

### LCD 液晶屏 (ST7789) — SPI 接口

| 信号 | GPIO 引脚 |
|------|-----------|
| MOSI | 36        |
| CLK  | 35        |
| CS   | 37        |
| DC   | 38        |
| RST  | 47        |
| BL   | 48        |

**分辨率:** 320 × 240

### 触摸 (GT911) — I2C 接口

| 信号 | GPIO 引脚 |
|------|-----------|
| SDA  | 2         |
| SCL  | 1         |
| INT  | 21        |
| RST  | 14        |

### 步进电机 (A4988)

| 信号 | GPIO 引脚 |
|------|-----------|
| EN   | 45        |
| STEP | 39        |
| DIR  | 40        |
| MS1  | 41        |
| MS2  | 42        |
| MS3  | 3         |

> *引脚定义位于 `feeder_motor.c`（编译期常量），MS1/MS2/MS3 默认置高启用 16 微步细分；可根据您的自定义 PCB 进行调整。*

### OV2640 摄像头 — DVP 并行接口 + SCCB (I2C)

| 信号 | GPIO 引脚 |
|------|-----------|
| D0 ~ D7 | 11, 9, 8, 10, 12, 18, 17, 16 |
| VSYNC | 6        |
| DE (HREF) | 7    |
| PCLK | 13       |
| XCLK | 15       |
| SCCB SCL | 5    |
| SCCB SDA | 4    |

> *SCCB 使用独立的 I2C_NUM_1 总线（不与 GT911 的 I2C_NUM_0 冲突）。摄像头引脚定义位于 `ov2640.h`（编译期常量），分辨率为原生 JPEG 320×240。*

### 局域网按需视频推流

WiFi 获取 IP 后，设备会在 80 端口启动 HTTP 服务，但不会持续启动摄像头采集。
只有客户端访问推流地址，或打开设备上的摄像头页面时，才会启动摄像头采集。

- 浏览器页面：`http://<设备IP>/`
- 原始 MJPEG 流：`http://<设备IP>/stream`
- 参数：OV2640 原生 JPEG 320×240，最高 10 FPS；屏幕预览使用 ESP32-S3 ROM JPEG 解码器
- 范围：仅支持局域网访问，当前版本不启用鉴权
- 限制：同时只支持 1 个推流客户端，额外客户端返回 HTTP 503

客户端断开后，会释放该客户端持有的摄像头引用和帧缓冲区；当屏幕预览和推流
都不再使用摄像头时，摄像头才会停止。仅连接 WiFi 不会启动摄像头采集，主页和
投喂功能不受影响。

### 设备界面说明

- 开机默认显示投喂主页。
- 主页显示下一次投粮倒计时，格式为 `HH:MM`，不显示秒。
- 未设置投喂计划时显示 `Next feed: No schedule`。
- WiFi 获取 IP 后，设备 IP 会显示在主页 `Feed` 按钮下方。
- 点击主页的摄像头图标，可进入本地 320×240 摄像头预览。

## 📖 API 概览

### 手动投喂

```c
#include "include.h"

// 投喂指定仓格数 (1 ~ 10)
manual_feeding_start(2);  // 投喂 2 个仓
```

### 投喂计划

```c
#include "driver/inc/feeding_schedule.h"

// 添加一条计划：每天 08:00 投喂 2 仓
feed_schedule_item_t item = {
    .hour = 8, .minute = 0, .amount = 2, .enabled = true,
    .every_days = 1,   /* 1 = 每天, 2 = 隔 1 天, 3 = 隔 2 天 ... */
};
feed_schedule_add_item(&item);
feed_schedule_save();
```

> `every_days` 表示投喂间隔天数：1 = 每天，2 = 隔 1 天，3 = 隔 2 天……最多 7 天；具体落在哪几天按本地自然日（北京时间）轮换。

### WiFi 配置

```c
#include "device/inc/wifi_app.h"

// 连接 WiFi
wifi_app_connect("MyWiFi", "password123");

// 检查连接状态
bool connected = wifi_app_is_connected();
```

## 🛠️ 主要依赖

| 组件        | 版本     | 用途                       |
|------------|---------|---------------------------|
| ESP-IDF    | v5.5.1  | RTOS、驱动、网络             |
| LVGL       | v8.4.0  | 嵌入式 GUI 图形库           |
| ST7789     | —       | SPI 液晶屏控制器            |
| GT911      | —       | 电容触摸控制器               |
| TinyUSB    | —       | USB CDC 虚拟串口            |
| A4988      | —       | 步进电机驱动                 |
| OV2640     | —       | DVP 摄像头 (原生 JPEG 320×240)  |
| esp_cam_sensor | ^1.1.0 | OV2640 传感器驱动           |
| esp_http_server | ESP-IDF | 局域网 HTTP 视频服务         |
| esp_driver_jpeg | ESP-IDF | JPEG 驱动依赖（S3 使用 ROM 解码） |
| SDL2       | 2.30    | 离线模拟器窗口显示 (仅 PC 端)   |

## 🤝 贡献

欢迎贡献代码！请遵循以下步骤：

1. Fork 本项目
2. 创建您的特性分支 (`git checkout -b feature/AmazingFeature`)
3. 提交您的修改 (`git commit -m 'Add some AmazingFeature'`)
4. 推送到分支 (`git push origin feature/AmazingFeature`)
5. 提交 Pull Request

## 📝 开源协议

本项目基于 MIT 协议分发。详见 `LICENSE` 文件。

## 🙏 致谢

- 基于 [乐鑫 Espressif Systems](https://github.com/espressif) 的示例工程
- LVGL 图形库 — [lvgl/lvgl](https://github.com/lvgl/lvgl)

## 📬 联系方式

项目链接: [https://github.com/Garfield-1314/Cat-Food-Machine](https://github.com/Garfield-1314/Cat-Food-Machine)
