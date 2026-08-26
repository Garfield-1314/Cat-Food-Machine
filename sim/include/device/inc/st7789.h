/* 模拟器桩：替代固件 device/inc/st7789.h（仅 UI 用到的背光接口） */
#ifndef SIM_ST7789_H
#define SIM_ST7789_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void lcd_st7789_set_brightness(uint8_t brightness);
uint8_t lcd_st7789_get_brightness(void);

#ifdef __cplusplus
}
#endif

#endif /* SIM_ST7789_H */
