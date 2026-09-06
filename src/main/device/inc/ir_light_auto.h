#ifndef __IR_LIGHT_AUTO_H
#define __IR_LIGHT_AUTO_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 传感器寄存器读取回调。
 *
 * 自动调节通过它读取摄像头传感器的曝光/增益寄存器来估计画面明暗。
 *
 * @param[in] reg 寄存器地址
 * @param[out] value 寄存器值
 * @return esp_err_t 读取成功返回 ESP_OK
 */
typedef esp_err_t (*ir_light_reg_read_fn)(uint8_t reg, uint8_t *value);

/**
 * @brief 注册寄存器读取回调（在摄像头初始化成功后调用一次）。
 *
 * @param read_fn 读取回调，不可为 NULL
 */
esp_err_t ir_light_auto_init(ir_light_reg_read_fn read_fn);

/**
 * @brief 启动自动亮度调节（创建控制任务）。
 *
 * 仅在摄像头推流期间调用；任务按画面明暗在 [0, 亮度上限] 内调节 PWM。
 */
esp_err_t ir_light_auto_start(void);

/**
 * @brief 停止自动亮度调节。
 *
 * 幂等；任务结束后补光灯状态保持调用前的值，由调用方负责熄灯。
 */
esp_err_t ir_light_auto_stop(void);

/** @brief 检查自动亮度调节任务是否在运行。 */
bool ir_light_auto_is_running(void);

#ifdef __cplusplus
}
#endif

#endif /* __IR_LIGHT_AUTO_H */
