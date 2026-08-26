#ifndef __OV2640_H
#define __OV2640_H

#include <stdint.h>
#include <stddef.h>
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

/* Cat Food Machine OV2640 方形 JPEG 格式：240x240 */
#define CAM_OUTPUT_WIDTH   240
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
 * @brief 获取摄像头采集使用权
 *
 * 多个消费者（LCD 预览、HTTP 推流）可以同时持有使用权。只有第一个
 * 消费者会真正启动摄像头。
 */
esp_err_t ov2640_camera_acquire(void);

/**
 * @brief 停止摄像头数据流
 *
 * @return esp_err_t 成功返回 ESP_OK
 */
esp_err_t ov2640_camera_stop(void);

/**
 * @brief 释放摄像头采集使用权
 *
 * 最后一个消费者释放使用权后才会停止摄像头。
 */
esp_err_t ov2640_camera_release(void);

/**
 * @brief 将最近一帧完整 JPEG 复制到调用方缓冲区
 *
 * 函数会检测复制期间 DMA 是否切换了缓冲区；发生竞争时会自动重试，
 * 因此返回 ESP_OK 后，调用方可以安全地异步发送该副本。
 *
 * @param[out] dst JPEG 输出缓冲区；可为 NULL 以查询当前帧所需大小
 * @param[in] capacity dst 的容量
 * @param[out] size JPEG 数据长度，缓冲区不足时返回所需容量
 * @param[out] frame_id 完整帧序号，可为 NULL
 * @return ESP_OK 复制成功；ESP_ERR_INVALID_SIZE 表示缓冲区不足；
 *         ESP_ERR_NOT_FOUND 表示当前暂无可安全复制的新帧
 */
esp_err_t ov2640_camera_copy_jpeg_frame(uint8_t *dst, size_t capacity,
                                        size_t *size, uint32_t *frame_id);

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
