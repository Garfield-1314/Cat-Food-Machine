#ifndef __FEEDER_MOTOR_H
#define __FEEDER_MOTOR_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化步进电机 (A4988 驱动)
 *        设置 GPIO 模式，初始状态为禁止 (EN=高)
 */
void feeder_motor_init(void);

/**
 * @brief 投喂指定仓位数（异步，GPTimer 硬件定时器发脉冲）
 *        1 个仓位 = 电机旋转 90°
 *
 * @param slots 仓位数 (1 ~ 10)
 */
void feeder_motor_dispense(uint8_t slots);

/**
 * @brief 检查电机是否空闲（可用于轮询投喂完成状态）
 * @return true 电机空闲（投喂完成），false 电机运行中
 */
bool feeder_motor_is_idle(void);

/**
 * @brief 同步投喂（阻塞版）
 *        直接在当前上下文中执行步进脉冲，直到旋转完成
 *
 * @param slots 仓位数 (1 ~ 10)
 */
void feeder_motor_dispense_sync(uint8_t slots);

#ifdef __cplusplus
}
#endif

#endif /* __FEEDER_MOTOR_H */