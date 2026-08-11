cmake_minimum_required(VERSION 3.12)
project(cat_food_sim C)

# 让 option() 尊重已设置的普通变量（避免 LVGL 的 option() 清除 LV_CONF_PATH）
if(POLICY CMP0077)
  cmake_policy(SET CMP0077 NEW)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# ---- LVGL 配置：lv_conf.h 放本目录，供 LVGL 编译时查找 ----
# 引入 LVGL（非 ESP 平台会走 custom.cmake，导出 lvgl 目标）
add_subdirectory(${PROJECT_SOURCE_DIR}/../src/managed_components/lvgl__lvgl lvgl)

# 把本目录加入 LVGL include path，确保 lv_conf.h 可被 LVGL 找到
target_include_directories(lvgl PUBLIC ${PROJECT_SOURCE_DIR})

set(UI_DIR ${PROJECT_SOURCE_DIR}/../src/main)

# 自动收集固件 UI 页面源码（原样引用，不改动）。
# CONFIGURE_DEPENDS：新增/删除页面 .c 后只需 cmake --build，CMake 会自动检测并重新 configure。
file(GLOB UI_SRCS CONFIGURE_DEPENDS ${UI_DIR}/ui/src/*.c)

# 自动收集模拟器桩实现
file(GLOB STUB_SRCS CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/src/*.c)

# 桩头最优先；随后是固件 src/main（解析 "ui/inc/*.h" 与 "include.h"）
set(SIM_INCS ${PROJECT_SOURCE_DIR}/include ${UI_DIR})

# ---- 离屏截图版（无需 SDL2，用于无头验证 / 生成预览图）----
add_executable(cat_food_sim_offscreen offscreen_main.c ${UI_SRCS} ${STUB_SRCS})
target_include_directories(cat_food_sim_offscreen PRIVATE ${SIM_INCS})
target_link_libraries(cat_food_sim_offscreen PRIVATE lvgl)

# ---- SDL2 窗口交互版（可选，需安装 libsdl2-dev）----
find_package(SDL2 QUIET)
if(SDL2_FOUND)
  add_executable(cat_food_sim main.c ${UI_SRCS} ${STUB_SRCS})
  target_include_directories(cat_food_sim PRIVATE ${SIM_INCS})
  target_link_libraries(cat_food_sim PRIVATE lvgl SDL2::SDL2)
  message(STATUS "SDL2 found: 窗口模拟器 cat_food_sim 已启用")
else()
  message(WARNING "SDL2 未找到：跳过窗口模拟器 cat_food_sim。"
                  "安装 libsdl2-dev 后重新运行 cmake 以启用交互窗口。")
endif()
