/* 模拟器桩：替代固件的 include.h（避免引入 ESP-IDF 头文件）
 * 各页面实际用到的硬件头文件已由对应桩头单独提供。
 */
#ifndef SIM_INCLUDE_H
#define SIM_INCLUDE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 固件 main.c 提供的函数，这里提供桩实现（见 src/app_stub.c） */
void manual_feeding_start(uint8_t slots);

#ifdef __cplusplus
}
#endif

#endif /* SIM_INCLUDE_H */
