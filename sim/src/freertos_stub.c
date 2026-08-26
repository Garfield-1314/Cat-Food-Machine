/* 模拟器桩：FreeRTOS 任务 API（极简，直接在调用处同步执行） */
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

BaseType_t xTaskCreate(TaskFunction_t pxTaskCode,
                       const char *pcName,
                       uint32_t usStackDepth,
                       void *pvParameters,
                       uint32_t uxPriority,
                       TaskHandle_t *pxCreatedTask)
{
    (void)usStackDepth;
    (void)uxPriority;
    printf("[sim] xTaskCreate: %s\n", pcName ? pcName : "?");
    if (pxTaskCode) {
        pxTaskCode(pvParameters);
    }
    if (pxCreatedTask) {
        *pxCreatedTask = NULL;
    }
    return pdPASS;
}

void vTaskDelay(TickType_t xTicksToDelay)
{
    (void)xTicksToDelay;
}

void vTaskDelete(TaskHandle_t pxTaskToDelete)
{
    (void)pxTaskToDelete;
}
