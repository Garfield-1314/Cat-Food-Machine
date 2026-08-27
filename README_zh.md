# 🐱 Cat Food Machine — 智能猫粮投喂机

[English](./README.md) | 中文

基于 **ESP32-S3** 的智能猫粮投喂机，配备触控屏界面、WiFi 联网、定时投喂及步进电机控制功能。

## 📋 功能特性

- **触控屏 UI** — 2.8 寸 320×240 SPI 液晶屏 (ST7789)，搭载 LVGL 图形界面
- **电容触摸** — GT911 触摸传感器 (I2C)，流畅的用户交互体验
- **多页面界面** — 投喂主页（默认）、投喂计划页、设置页、WiFi 配置页
- **定时投喂** — 最多 8 组定时计划，可指定投喂时间（时:分）和间隔天数（每天 / 隔 1 天 / 隔 2 天……），数据持久化存储于 NVS
- **步进电机出粮** — A4988 驱动步进电机，1 仓格 = 90° 旋转
- **OV2640 摄像头** — DVP 8-bit 并行接口，方形 JPEG 240×240，按需采集
- **WiFi 联网** — Station 模式，支持保存凭据、界面配置及设备 IP 显示
- **局域网按需视频推流** — 通过 HTTP-MJPEG 提供视频流，浏览器访问 `http://<设备IP>/`，发送阻塞后自动重连
- **SNTP 时间同步** — 通过 NTP 自动同步时间（北京时间，UTC+8）
- **背光自动熄灭** — 5 分钟无操作自动熄灭背光，触摸唤醒
- **USB 调试串口** — 芯片内置 USB-Serial-JTAG 虚拟串口（ROM 级），从 bootloader 起即工作，重启不掉线，无需 USB-TTL 转换器

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
│   │   │   │                #   - ov2640.h, video_stream.h
│   │   │   │                #   - wifi_app.h, sntp_time.h
│   │   │   └── src/         # 驱动实现
│   │   ├── driver/          # 应用层驱动
│   │   │   ├── inc/         #   - feeder_motor.h, feeding_schedule.h
│   │   │   └── src/         # 驱动实现
│   │   └── ui/              # LVGL 用户界面
│   │       ├── inc/         #   - ui.h, app_page.h, feeding_page.h
│   │       │                #   - setting_page.h, wifi_config_page.h
│   │       └── src/         # UI 实现
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
git checkout v5.5.5
git submodule update --init --recursive --progress
./install.sh
```

### 2. 编译 & 烧录

```bash
cd src
source ./esp32libraries/esp-idf/export.sh
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

## 🖥️ 硬件配置

当前启用的是 Cat 板配置。ESP32-S3-EYE 的 LCD 引脚仍保留在源码中，
但处于注释状态。

> **⚠️ PSRAM 不可用（本板硬件限制）**：模块为 N16R8（16MB Flash + 8MB 八线
> PSRAM），但其数据线 D2/D3/D4 对应 GPIO35/36/37，与下方 LCD 的
> CLK/MOSI/CS 引脚冲突。**启用 PSRAM 会导致 MSPI 总线争用，开机卡死并
> 被看门狗反复复位（无限重启）**，因此固件中 `CONFIG_SPIRAM` 保持关闭。
> 仅当更换 PSRAM 引脚空闲的板子后，才可重新启用（见 `sdkconfig.defaults`
> 内注释）。

Flash 镜像头已设为实际的 16MB（`CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y`）。
如果从旧的 2MB 配置升级，请至少完整烧录一次 bootloader、分区表和应用，
否则旧 bootloader 仍可能报告 Flash 大小不匹配。

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

> *SCCB 使用独立的 I2C_NUM_1 总线（不与 GT911 的 I2C_NUM_0 冲突）。摄像头引脚定义位于 `ov2640.h`（编译期常量），应用层在组件公开的 320×240 JPEG 基础模式上配置 240×240 方形输出，不修改 `managed_components`。*

### 局域网按需视频推流

WiFi 获取 IP 后，设备会在 80 端口启动 HTTP 服务，但不会持续启动摄像头采集。
只有客户端访问推流地址时，才会启动摄像头采集（按需、用完即停）。

- 浏览器页面：`http://<设备IP>/`
- 原始 MJPEG 流：`http://<设备IP>/stream`
- 参数：OV2640 方形 JPEG 240×240，最高 10 FPS
- 范围：仅支持局域网访问，当前版本不启用鉴权
- 限制：同时只支持 1 个推流客户端，额外客户端返回 HTTP 503
- 内存：无 PSRAM 环境下使用两块 57,600 字节 DMA 帧缓冲，并按当前 JPEG
  压缩大小复用一块发送副本，网络阻塞不会再破坏采集帧
- 稳定性：单次 socket 发送等待 2 秒，`EAGAIN` 最多重试 2 次；
  发送变慢后不追赶补帧，避免 TCP 缓冲被突发帧塞满
- 自愈：浏览页在流最终断开后约 1 秒自动重连；页面转入后台时主动释放流，
  回到前台再连接。直接使用 `/stream` 的客户端需自行实现重连

> 本板无可用 PSRAM，LCD 本地预览已移除（内存不足以同时承载
> 帧缓冲 + 解码输出），摄像头画面统一通过 Web 推流查看。

### 内存与网络资源配置

| 项目 | 当前配置 | 分配时机 |
|---|---:|---|
| LVGL 绘制缓冲 | 320×10 RGB565 = 6,400 字节 | 启动时，内部 DMA SRAM |
| LVGL 内存池 | 20 KiB | 静态 DIRAM |
| OV2640 DMA 帧缓冲 | 2×57,600 字节 | Web 流连接时申请，断开后释放 |
| JPEG 发送副本 | 按压缩帧大小向上取整到 4 KiB | Web 流连接时按需扩展 |
| WiFi RX | 静态 8、动态上限 24、BA 窗口 6 | WiFi 运行时 |
| WiFi TX | 动态上限 32 | WiFi 运行时 |

`idf.py size` 对当前构建的结果为静态 DIRAM **134,935 字节（39.48%）**；
该数字不包括运行时按需申请的摄像头、JPEG 副本和 WiFi 动态缓冲。

当前是 IPv4 + WPA2 Station 专用配置：IPv6、SoftAP/DHCP Server、企业 WiFi、
WPA3/OWE 已关闭；保留 WiFi 通用 IRAM 优化以保证 MJPEG 发送，仅关闭 RX IRAM 快速路径。
如果需要对应功能或在弱信号下频繁丢包，应恢复相应 Kconfig，或优先将动态 RX
上限恢复到 32。

### 设备界面说明

- 开机默认显示投喂主页。
- 主页显示下一次投粮倒计时，格式为 `HH:MM`，不显示秒。
- 未设置投喂计划时显示 `Next feed: No schedule`。
- WiFi 获取 IP 后，设备 IP 会显示在主页 `Feed` 按钮下方。
- 点击主页的摄像头图标，可查看 Web 推流地址（`http://<设备IP>/`），
  用浏览器打开即可观看实时画面。

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
| USB-Serial-JTAG | ESP32-S3 | 内置调试串口（ROM 级，重启不掉线） |
| A4988      | —       | 步进电机驱动                 |
| OV2640     | —       | DVP 摄像头（方形 JPEG 240×240）  |
| esp_cam_sensor | ^1.1.0 | OV2640 传感器驱动           |
| esp_http_server | ESP-IDF | 局域网 HTTP 视频服务         |

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
