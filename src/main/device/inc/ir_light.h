#ifndef __IR_LIGHT_H
#define __IR_LIGHT_H

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 红外补光灯控制脚：TXD0 复用，由 LEDC PWM 调光点亮。 */
#define IR_LIGHT_GPIO            43
#define IR_LIGHT_DUTY_PERCENT    60  /* 默认点亮亮度 60% */
#define IR_LIGHT_DUTY_MIN        1   /* 用户可调光强下限 */
#define IR_LIGHT_DUTY_MAX        75  /* 用户可调光强上限 */
#define IR_LIGHT_PWM_FREQ_HZ     400 /* PWM 频率，人眼与摄像头不可见即可 */
#define IR_LIGHT_PWM_DUTY_MAX    ((1 << 10) - 1) /* 10-bit：0-1023 */

/**
 * @brief 初始化红外补光灯，初始化后默认关闭。
 */
esp_err_t ir_light_init(void);

/**
 * @brief 设置红外补光灯状态。
 *
 * @param on true 开启，false 关闭
 */
esp_err_t ir_light_set(bool on);

/** @brief 开启红外补光灯。 */
esp_err_t ir_light_on(void);

/** @brief 关闭红外补光灯。 */
esp_err_t ir_light_off(void);

/**
 * @brief 设置点亮时的 PWM 亮度（百分比，限幅 IR_LIGHT_DUTY_MIN~MAX）。
 *
 * 若补光灯当前已点亮，立即以新亮度生效；否则仅保存，下次点亮时使用。
 *
 * @param percent 光强百分比
 * @return esp_err_t 成功返回 ESP_OK
 */
esp_err_t ir_light_set_duty_percent(uint8_t percent);

/**
 * @brief 获取当前保存的点亮亮度（百分比）。
 * @return uint8_t 光强百分比
 */
uint8_t ir_light_get_duty_percent(void);

#ifdef __cplusplus
}
#endif

#endif /* __IR_LIGHT_H */
