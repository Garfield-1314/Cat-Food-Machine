# 🐱 Cat Food Machine — 智能猫粮投喂机

[English](./README.md) | 中文

基于 **ESP32-S3** 的智能猫粮投喂机，配备触控屏界面、WiFi 联网、定时投喂及步进电机控制功能。

## 📋 功能特性

- **触控屏 UI** — 2.8 寸 320×240 SPI 液晶屏 (ST7789)，搭载 LVGL 图形界面
- **电容触摸** — GT911 触摸传感器 (I2C)，流畅的用户交互体验
- **多页面界面** — 投喂主页（默认）、投喂计划页、设置页、WiFi 配置页
- **定时投喂** — 最多 8 组定时计划，可指定投喂时间（时:分）和间隔天数（每天 / 隔 1 天 / 隔 2 天……），数据持久化存储于 NVS
- **步进电机出粮** — A4988 驱动步进电机，1 仓格 = 90° 旋转
- **OV2640 摄像头** — OV2640 DVP 8-bit 并行接口，原生 640×480 JPEG，按需采集
- **WiFi 联网** — Station 模式，支持保存一个 WiFi 配置、界面配置及设备 IP 显示
- **局域网按需视频推流** — 通过 HTTP-MJPEG 提供视频流，浏览器访问 `http://<设备IP>/`，发送阻塞后自动重连
- **SNTP 时间同步** — 通过 NTP 自动同步时间（北京时间，UTC+8）
- **背光自动熄灭** — 5 分钟无操作自动熄灭背光，触摸唤醒
- **USB 调试串口** — 芯片内置 USB-Serial-JTAG 虚拟串口（ROM 级），从 bootloader 起即工作，重启不掉线，无需 USB-TTL 转换器

## 🧱 项目结构

```
Cat-Food-Machine/
├── esp32libraries/          # ESP32 库子模块
│   ├── esp-idf/             # ESP-IDF 框架 (v5.5.5)
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
│   │   │   │                #   - ov2640.h, video_stream.h, ir_light.h
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

## 🧩 工程架构与运行流程

固件是一个 ESP-IDF 应用组件，启动顺序为：初始化 NVS、ST7789/LVGL 和
GT911、A4988 电机、OV2640 摄像头、主页 UI、投喂计划、背光以及 WiFi。
WiFi 获取 IPv4 地址后启动 SNTP 和 HTTP 服务；随后 `main` 任务返回，独立的
LVGL 任务以 50 Hz 运行界面循环。

- 投喂调度器每秒检查一次启用的计划；系统时间未同步前不会触发电机。
- WiFi 扫描在 FreeRTOS 任务中执行，结果只通过 LVGL 定时器回调更新界面。
  扫描期间可能会暂时中断自动连接。
- Web 推流使用异步 HTTP 任务。第一个推流客户端申请摄像头使用权并打开红外
  补光灯；最后一个摄像头使用者断开后停止摄像头、关闭补光灯并释放缓冲区。
- LVGL 页面切换会延迟到当前触摸事件完成后执行；旧页面及其定时器会在页面
  删除时释放。

## 🚀 快速开始

### 环境要求

- **ESP-IDF v5.5.5**（以子模块形式包含，且由 `dependencies.lock` 锁定）
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
source ../esp32libraries/esp-idf/export.sh
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

## 🖥️ 硬件配置

当前启用的是 Cat 板配置。ESP32-S3-EYE 的 LCD 引脚仍保留在源码中，
但处于注释状态。

> 模块为 N16R8（16MB Flash + 8MB 八线 PSRAM）。当前使用的新 PCB 将 LCD
> 调整到 GPIO41/42/3/38，避开 PSRAM 使用的 GPIO26~37，因此固件已启用
> `CONFIG_SPIRAM=y` 和 `CONFIG_SPIRAM_MODE_OCT=y`。OV2640 帧缓冲、Web
> JPEG 发送副本和 LVGL 绘制缓冲均优先使用 PSRAM。

Flash 镜像头已设为实际的 16MB（`CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y`）。
如果从旧的 2MB 配置升级，请至少完整烧录一次 bootloader、分区表和应用，
否则旧 bootloader 仍可能报告 Flash 大小不匹配。

当前选择 ESP-IDF 的 single-app-large 分区方案：24 KiB NVS、4 KiB PHY 初始化
分区，以及一个 1,500 KiB 的 factory 应用分区。当前固件没有 OTA 应用分区。

### 持久化数据

NVS 分区用于保存少量设备配置，而不是文件系统：

- `wifi_config`：1 组 SSID/密码；保存新网络会覆盖旧网络。
- `feed_sched`：最多 8 组投喂计划及其数量、时间、启用状态和重复间隔。
- `lcd_config`：背光亮度（30–100%）。

恢复出厂或擦除 NVS 后，WiFi 配置和投喂计划会恢复为空，背光使用默认亮度。

### LCD 液晶屏 (ST7789) — SPI 接口

| 信号 | GPIO 引脚 |
|------|-----------|
| MOSI | 41        |
| CLK  | 42        |
| CS   | 3         |
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
| 细分设置 | 硬件固定为 16 细分 |

> *固件只控制 EN、STEP、DIR 三个引脚。当前 PCB 已在硬件上固定 A4988 的细分设置，MS1/MS2/MS3 不是软件 GPIO。EN 为低电平使能，初始化及每次投喂完成后都会禁止电机。*

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

> *SCCB 使用独立的 I2C_NUM_1 总线（不与 GT911 的 I2C_NUM_0 冲突）。摄像头引脚定义位于 `ov2640.h`（编译期常量），应用层通过组件公开 API 选择原生 640×480 JPEG，并设置 JPEG quality=10，不修改 `managed_components`。*

注意：`sdkconfig` 中保留的是 esp_cam_sensor 组件的基础格式配置
`CONFIG_CAMERA_OV2640_DVP_JPEG_320X240_50FPS`；应用启动时会通过公开 API
重新选择 640×480 JPEG，因此最终 Web 输出以应用代码为准。

### 红外补光灯

| 信号 | GPIO 引脚 | 逻辑 |
|------|-----------|------|
| MOS 控制 | TXD0 / GPIO43 | 高电平开启，低电平关闭 |

独立的 `ir_light.c` 模块负责 GPIO 配置和开关控制，初始化后默认输出低电平。补光灯仅在 OV2640 实际采集期间自动开启；Web 推流断开或摄像头停止后自动关闭。建议 MOS 栅极外接约 10 kΩ 下拉电阻，确保复位和启动阶段补光灯保持关闭。GPIO43 同时是 ESP32-S3 的 UART0 TXD，本工程使用芯片内置 USB-Serial-JTAG 控制台，因此可复用为普通 GPIO；如果使用 UART0，则不能同时复用该引脚。

### 局域网按需视频推流

WiFi 获取 IP 后，设备会在 80 端口启动 HTTP 服务，但不会持续启动摄像头采集。
只有客户端访问推流地址时，才会启动摄像头采集（按需、用完即停）。

- 浏览器页面：`http://<设备IP>/`
- 原始 MJPEG 流：`http://<设备IP>/stream`
- 参数：OV2640 原生 JPEG 640×480，最高 10 FPS
- 范围：仅支持局域网访问，当前版本不启用鉴权
- 限制：同时只支持 1 个推流客户端，额外客户端返回 HTTP 503
- 内存：使用两块 2 MiB DMA 帧缓冲，优先分配到 PSRAM；另按当前 JPEG
  压缩大小申请一块 PSRAM 发送副本，网络阻塞不会再破坏采集帧
- 稳定性：HTTP 服务的 socket 发送超时为 2 秒；发送失败后结束当前推流，
  浏览器页面会重新连接；发送变慢后不追赶补帧，避免 TCP 缓冲被突发帧塞满
- 自愈：浏览页在流最终断开后约 1 秒自动重连；页面转入后台时主动释放流，
  回到前台再连接。直接使用 `/stream` 的客户端需自行实现重连

> 摄像头画面统一通过 Web 推流查看；摄像头和 Web 缓冲使用 PSRAM，避免
> 占用 LVGL 和系统所需的内部 RAM。

### 内存与网络资源配置

| 项目 | 当前配置 | 分配时机 |
|---|---:|---|
| LVGL 绘制缓冲 | 320×10 RGB565 = 6,400 字节 | 启动时，优先使用 PSRAM，失败回退内部 DMA SRAM |
| LVGL 内存池 | 64 KiB | 静态 DIRAM |
| OV2640 DMA 帧缓冲 | 2×2 MiB | Web 流连接时申请，优先使用 PSRAM，断开后释放 |
| JPEG 发送副本 | 按压缩帧大小向上取整到 4 KiB，最大约 2 MiB | 按需扩展，优先 PSRAM、内部 RAM 回退，断开后释放 |
| WiFi RX | 静态 8、动态上限 24、BA 窗口 6 | WiFi 运行时 |
| WiFi TX | 动态上限 32 | WiFi 运行时 |

本次构建的静态 DIRAM 为 **188,775 / 341,760 字节（55.24%）**，应用镜像为
`0x115880` 字节，1,500 KiB factory 应用分区剩余 `0x61780` 字节（26%）。
这些静态数据不包括运行时按需申请的摄像头、JPEG 副本和 WiFi 动态缓冲；
PSRAM 实际运行时使用量需要在目标设备上实测，或根据分配路径估算。

按源码中的应用缓冲上限计算，单个推流客户端同时使用的应用级 PSRAM 优先
申请量最多约为 **6,297,856 字节（4 MiB 摄像头帧缓冲 + 2 MiB JPEG 副本
+ 6,400 字节 LVGL 绘制缓冲）**。这是代码可确定的上限，不是完整 PSRAM
实测占用；它不包括 WiFi/ESP-IDF/LVGL 组件自身的动态分配、堆管理开销和
内部 RAM 回退分配。未推流时，摄像头的 4 MiB 帧缓冲和 JPEG 副本均应已释放。

8 MB PSRAM 作为 LVGL 绘制缓冲、两块摄像头 DMA 帧缓冲和每个推流任务 JPEG
副本的优先分配区域。LVGL 的 64 KiB 固定内存池仍位于内部 RAM（保持关闭
`CONFIG_LV_MEM_CUSTOM`）；PSRAM 申请失败时，LVGL 和摄像头路径会在支持的
情况下回退到合适的内部 DMA 内存。

当前是 IPv4 + WPA2 Station 专用配置：IPv6、SoftAP/DHCP Server、企业 WiFi、
WPA3/OWE 已关闭；保留 WiFi 通用 IRAM 优化以保证 MJPEG 发送，仅关闭 RX IRAM 快速路径。
如果需要对应功能或在弱信号下频繁丢包，应恢复相应 Kconfig，或优先将动态 RX
上限恢复到 32。

### 设备界面说明

- 开机默认显示投喂主页。
- 主页显示下一次投粮倒计时，格式为 `DD:HH:MM`，不显示秒。
- 未设置投喂计划时显示 `Next feed: No schedule`。
- WiFi 获取 IP 后，设备 IP 会显示在主页 `Feed` 按钮下方。
- WiFi 页面会扫描附近热点，并支持选择一个热点进行连接。
- 当前仅在 NVS 中保存一个 WiFi 配置；保存新的网络会覆盖原来的 SSID
  和密码，重启后自动尝试连接最后保存的网络。
- WiFi 扫描最多返回 24 个热点；加密网络需要输入密码，开放网络可直接连接。
  当前 WiFi 页面不提供 SoftAP 配网，也没有手动输入 SSID 的输入框。
- 已保存网络断开后会立即最多重试 5 次；仍不可用时改为每 10 分钟重试一次。
  开始扫描会暂停正在进行的自动连接，必要时扫描结束后恢复周期重试。
- 点击连接时会先把 SSID/密码写入 NVS，再启动连接任务；即使连接失败，该配置
  仍会作为下次启动使用的网络，直到保存新的网络为止。
- 投喂计划页最多保存 8 组计划。每组计划支持 1–10 个仓位、时分、启用状态
  和 1–7 天的重复间隔；页面上的分钟以 5 分钟为步进。
- 设置页显示 IDF/LVGL 版本、芯片版本、触摸芯片、固件版本、WiFi MAC 地址，
  并提供 30–100% 的背光滑块。
- 连续 5 分钟没有触摸操作时背光自动关闭；触摸或投喂活动会唤醒背光。
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

当前固件只保存一个 WiFi 配置。调用 `wifi_app_save_config()` 保存新网络时，
会替换已有配置；WiFi 页面最多显示 24 个扫描结果，但这些结果只是临时数据，
不代表已保存的网络数量。

## 🛠️ 主要依赖

| 组件        | 版本     | 用途                       |
|------------|---------|---------------------------|
| ESP-IDF    | v5.5.5  | RTOS、驱动、网络             |
| LVGL       | v8.4.0  | 嵌入式 GUI 图形库           |
| ST7789     | —       | SPI 液晶屏控制器            |
| GT911      | —       | 电容触摸控制器               |
| USB-Serial-JTAG | ESP32-S3 | 内置调试串口（ROM 级，重启不掉线） |
| A4988      | —       | 步进电机驱动                 |
| OV2640     | —       | DVP 摄像头（640×480 JPEG）     |
| esp_cam_sensor | ^1.1.0（锁定为 1.7.0） | OV2640 传感器驱动       |
| esp_http_server | ESP-IDF | 局域网 HTTP 视频服务         |

## 🤝 贡献

欢迎贡献代码！请遵循以下步骤：

1. Fork 本项目
2. 创建您的特性分支 (`git checkout -b feature/AmazingFeature`)
3. 提交您的修改 (`git commit -m 'Add some AmazingFeature'`)
4. 推送到分支 (`git push origin feature/AmazingFeature`)
5. 提交 Pull Request

## 📝 开源协议

当前仓库根目录没有 `LICENSE` 文件。再次分发项目之前，请先确认并补充正式的
开源协议。

## 🙏 致谢

- 基于 [乐鑫 Espressif Systems](https://github.com/espressif) 的示例工程
- LVGL 图形库 — [lvgl/lvgl](https://github.com/lvgl/lvgl)

## 📬 联系方式

项目链接: [https://github.com/Garfield-1314/Cat-Food-Machine](https://github.com/Garfield-1314/Cat-Food-Machine)
