#ifndef __IR_LIGHT_H
#define __IR_LIGHT_H

#include <stdbool.h>

#include "driver/gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 红外补光灯控制脚：TXD0 复用为普通 GPIO，高电平点亮。 */
#define IR_LIGHT_GPIO       GPIO_NUM_43
#define IR_LIGHT_ON_LEVEL   1
#define IR_LIGHT_OFF_LEVEL  0

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

#ifdef __cplusplus
}
#endif

#endif /* __IR_LIGHT_H */
