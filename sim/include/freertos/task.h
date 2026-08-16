/* 模拟器桩：替代 FreeRTOS 的 task.h（极简版本，仅供 UI 编译） */
#ifndef SIM_FREERTOS_TASK_H
#define SIM_FREERTOS_TASK_H

#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*TaskFunction_t)(void *);

BaseType_t xTaskCreate(TaskFunction_t pxTaskCode,
                       const char *pcName,
                       uint32_t usStackDepth,
                       void *pvParameters,
                       uint32_t uxPriority,
                       TaskHandle_t *pxCreatedTask);

void vTaskDelay(TickType_t xTicksToDelay);
void vTaskDelete(TaskHandle_t pxTaskToDelete);

#ifdef __cplusplus
}
#endif

#endif /* SIM_FREERTOS_TASK_H */
