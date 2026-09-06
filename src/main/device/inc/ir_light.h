#ifndef __IR_LIGHT_H
#define __IR_LIGHT_H

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 红外补光灯控制脚：TXD0 复用为普通 GPIO，LEDC PWM 输出，占空比决定亮度。 */
#define IR_LIGHT_GPIO       GPIO_NUM_43

#define IR_LIGHT_MAX_PERCENT 100
#define IR_LIGHT_MIN_PERCENT 0

/**
 * @brief 初始化红外补光灯 LEDC PWM，初始化后默认关闭。
 *
 * 亮度上限默认 60%，可用 ir_light_set_max_brightness() 修改并持久化到 NVS。
 */
esp_err_t ir_light_init(void);

/**
 * @brief 设置红外补光灯亮度。
 *
 * @param percent 亮度百分比，0 表示熄灭，100 表示最亮（占空比上限）
 */
esp_err_t ir_light_set_brightness(uint8_t percent);

/** @brief 获取当前输出亮度百分比。 */
uint8_t ir_light_get_brightness(void);

/**
 * @brief 设置红外补光灯状态。
 *
 * @param on true 以当前亮度上限开启，false 关闭
 */
esp_err_t ir_light_set(bool on);

/** @brief 以当前亮度上限开启红外补光灯。 */
esp_err_t ir_light_on(void);

/** @brief 关闭红外补光灯。 */
esp_err_t ir_light_off(void);

/**
 * @brief 设置自动调节允许的最大亮度（上限），并保存到 NVS。
 *
 * @param percent 上限百分比 (0-100)
 */
esp_err_t ir_light_set_max_brightness(uint8_t percent);

/** @brief 获取自动调节允许的最大亮度（上限）。 */
uint8_t ir_light_get_max_brightness(void);

#ifdef __cplusplus
}
#endif

#endif /* __IR_LIGHT_H */
