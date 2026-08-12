# 🖥️ Cat Food Machine — 离线 UI 模拟器

在 PC 上运行固件的 LVGL UI（320×240），**无需每次烧录固件**即可调试界面。

它**原样复用**固件的 `src/main/ui/src/*.c` 页面源码，只是把硬件/ESP-IDF 依赖替换成
`include/`（桩头文件）与 `src/*_stub.c`（桩实现），因此固件代码零改动。

## 为什么需要桩？

UI 页面原本依赖 ESP-IDF 硬件 API，PC 上无法编译。桩提供了同签名假实现：

| 被替换的依赖 | 桩 |
|---|---|
| `wifi_app_*` | `src/wifi_app_stub.c` |
| `sntp_time_*` | `src/sntp_time_stub.c` |
| `feed_schedule_*` | `src/feeding_schedule_stub.c` |
| NVS (`nvs_*`) | `src/nvs_stub.c` |
| `esp_*` / IDF 版本 / MAC | `src/esp_stub.c` |
| `lcd_st7789_set_brightness` | `src/st7789_stub.c` |
| FreeRTOS 任务 | `src/freertos_stub.c` |

`include/` 中的桩头文件优先级高于固件头文件，保证 `#include` 解析到桩。

## 依赖

- **CMake ≥ 3.12**、C 编译器
- **LVGL**：直接引用 `src/managed_components/lvgl__lvgl`（与固件同版本 v8.4.0）
- **SDL2**（窗口交互版需要）

## 构建

先安装 SDL2（以 Ubuntu/Debian 为例）：

```bash
sudo apt update
sudo apt install -y libsdl2-dev
```

然后：

```bash
cd sim
cmake -S . -B build
cmake --build build -j
./build/cat_food_sim
```

弹出 **640×480 窗口**（内部 320×240，鼠标=触摸），可切换所有 5 个页面进行交互调试。

## 目录结构

```
sim/
├── CMakeLists.txt        # 构建脚本
├── lv_conf.h             # LVGL 配置（与固件同分辨率/色深）
├── main.c                # SDL2 窗口模拟器主程序
├── README.md
├── include/              # 桩头文件（含 device/inc、driver/inc、freertos 等）
└── src/                  # 桩实现
```

## 固件改动如何同步到模拟器

模拟器**直接引用**固件 `src/main/ui/src/*.c`（同一份源码，非复制），因此：

| 固件改动 | 模拟器同步方式 |
|---|---|
| **修改现有页面**（`ui.c` / `app_page.c` / `feeding_page.c` / `setting_page.c` / `wifi_config_page.c`） | `cmake --build build` 即可生效 |
| **新增页面文件**（`ui/src/xxx_page.c`） | 只需 `cmake --build build`（`GLOB CONFIGURE_DEPENDS` 自动检测新文件）。记得在固件 `ui.c` 的 `switch_page_cb` 注册，若用到新硬件函数则补桩 |
| **删除页面文件** | 只需 `cmake --build build`（自动移除）。记得清理固件 `ui.c` 里的引用 |
| **新页面用到新的硬件/驱动函数** | 需在 `include/` + `src/*_stub.c` 补对应桩（硬件函数在 PC 上无法运行） |

## 注意事项

- 桩中的 `feed_schedule` 预置了 2 条演示计划，便于查看投喂页列表效果。
- `wifi_app` 桩默认视为已连接，主页会显示 WiFi 图标与时间。
- 修改 `lv_conf.h` 可调整内存/字体/组件开关。
- 若新增 UI 页面依赖新的硬件函数，需要在桩头/桩实现中补上对应签名。
