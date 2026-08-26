/* 模拟器桩：替代 FreeRTOS 的 FreeRTOS.h（极简版本，仅供 UI 编译） */
#ifndef SIM_FREERTOS_H
#define SIM_FREERTOS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t TickType_t;
typedef void *TaskHandle_t;
typedef int BaseType_t;

#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define portMAX_DELAY     0xFFFFFFFFU
#define pdPASS            1
#define pdFALSE           0
#define pdTRUE            1

#ifdef __cplusplus
}
#endif

#endif /* SIM_FREERTOS_H */
