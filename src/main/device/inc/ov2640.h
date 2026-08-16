#ifndef __OV2640_H
#define __OV2640_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* OV2640 DVP 引脚定义（按官方 dvp_spi_lcd_example） */
#define CAM_SCCB_SCL_IO    GPIO_NUM_5
#define CAM_SCCB_SDA_IO    GPIO_NUM_4
#define CAM_SCCB_I2C_PORT  I2C_NUM_1

#define CAM_DVP_D0_IO      GPIO_NUM_11
#define CAM_DVP_D1_IO      GPIO_NUM_9
#define CAM_DVP_D2_IO      GPIO_NUM_8
#define CAM_DVP_D3_IO      GPIO_NUM_10
#define CAM_DVP_D4_IO      GPIO_NUM_12
#define CAM_DVP_D5_IO      GPIO_NUM_18
#define CAM_DVP_D6_IO      GPIO_NUM_17
#define CAM_DVP_D7_IO      GPIO_NUM_16
#define CAM_DVP_VSYNC_IO   GPIO_NUM_6
#define CAM_DVP_DE_IO      GPIO_NUM_7
#define CAM_DVP_PCLK_IO    GPIO_NUM_13
#define CAM_DVP_XCLK_IO    GPIO_NUM_15

#define CAM_XCLK_FREQ_HZ   (20 * 1000 * 1000)

/* 摄像头输出分辨率（RGB565） */
#define CAM_OUTPUT_WIDTH   320
#define CAM_OUTPUT_HEIGHT  240

/**
 * @brief 初始化 OV2640 摄像头（DVP + SCCB + sensor）
 *
 * @return esp_err_t 成功返回 ESP_OK
 */
esp_err_t ov2640_camera_init(void);

/**
 * @brief 启动摄像头数据流（注册回调 + 使能 DVP 控制器）
 *
 * @return esp_err_t 成功返回 ESP_OK
 */
esp_err_t ov2640_camera_start(void);

/**
 * @brief 停止摄像头数据流
 *
 * @return esp_err_t 成功返回 ESP_OK
 */
esp_err_t ov2640_camera_stop(void);

/**
 * @brief 获取最近一帧完整 RGB565 图像数据
 *
 * @param[out] w 输出图像宽度
 * @param[out] h 输出图像高度
 * @return const uint16_t* RGB565 帧数据指针（0 则不可用）
 */
const uint16_t *ov2640_camera_get_frame(int *w, int *h);

/**
 * @brief 检查摄像头是否就绪（已初始化并检测到 OV2640）
 *
 * @return true 就绪
 */
bool ov2640_camera_is_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* __OV2640_H */
