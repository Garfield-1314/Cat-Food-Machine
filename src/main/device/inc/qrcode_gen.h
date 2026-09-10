#ifndef __QRCODE_GEN_H
#define __QRCODE_GEN_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 二维码数据结构 */
typedef struct {
    uint8_t *data;      /* 二维码位图数据 */
    int size;           /* 二维码尺寸 (模块数) */
    int scale;          /* 缩放比例 */
} qrcode_t;

/**
 * @brief 初始化二维码生成模块
 * @return ESP_OK 成功
 */
esp_err_t qrcode_gen_init(void);

/**
 * @brief 生成二维码
 * @param qr 输出二维码结构
 * @param data 要编码的数据
 * @param ecc_level 纠错级别 (0-3, 0=Low, 1=Medium, 2=Quartile, 3=High)
 * @return ESP_OK 成功
 */
esp_err_t qrcode_gen_create(qrcode_t *qr, const char *data, int ecc_level);

/**
 * @brief 释放二维码资源
 * @param qr 二维码结构
 */
void qrcode_gen_free(qrcode_t *qr);

/**
 * @brief 在 LVGL 画布上绘制二维码
 * @param qr 二维码结构
 * @param canvas LVGL 画布对象
 * @param x 起始 X 坐标
 * @param y 起始 Y 坐标
 * @param color 二维码颜色
 * @param bg_color 背景颜色
 */
void qrcode_gen_draw_lvgl(const qrcode_t *qr, void *canvas, int x, int y,
                          uint32_t color, uint32_t bg_color);

/**
 * @brief 生成设备绑定二维码数据
 * @param device_id 设备ID
 * @param temp_token 临时token
 * @param output 输出缓冲区
 * @param output_size 缓冲区大小
 * @return ESP_OK 成功
 */
esp_err_t qrcode_gen_create_bind_data(const char *device_id, const char *temp_token,
                                      char *output, size_t output_size);

#ifdef __cplusplus
}
#endif

#endif /* __QRCODE_GEN_H */
