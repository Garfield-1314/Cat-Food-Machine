/* 模拟器桩：OV2640 摄像头（PC 上无硬件，返回未就绪） */
#include <stdio.h>

#include "device/inc/ov2640.h"

esp_err_t ov2640_camera_init(void)
{
    printf("[sim] ov2640 init (stub)\n");
    return ESP_OK;
}

esp_err_t ov2640_camera_start(void)
{
    printf("[sim] ov2640 start (stub, no camera)\n");
    return ESP_OK;
}

esp_err_t ov2640_camera_stop(void)
{
    return ESP_OK;
}

const uint16_t *ov2640_camera_get_frame(int *w, int *h)
{
    if (w) *w = CAM_OUTPUT_WIDTH;
    if (h) *h = CAM_OUTPUT_HEIGHT;
    return NULL;
}

bool ov2640_camera_is_ready(void)
{
    return false;
}
