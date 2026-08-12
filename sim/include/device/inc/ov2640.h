/* 模拟器桩：替代固件 device/inc/ov2640.h（摄像头在 PC 上不可用） */
#ifndef SIM_OV2640_H
#define SIM_OV2640_H

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CAM_OUTPUT_WIDTH   320
#define CAM_OUTPUT_HEIGHT  240

esp_err_t ov2640_camera_init(void);
esp_err_t ov2640_camera_start(void);
esp_err_t ov2640_camera_stop(void);
const uint16_t *ov2640_camera_get_frame(int *w, int *h);
bool ov2640_camera_is_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* SIM_OV2640_H */
