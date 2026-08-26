/* 模拟器桩：LCD 背光（仅记录亮度，无需真实硬件） */
#include <stdio.h>

#include "device/inc/st7789.h"

static uint8_t s_brightness = 100;

void lcd_st7789_set_brightness(uint8_t brightness)
{
    s_brightness = brightness;
    printf("[sim] backlight set to %d%%\n", brightness);
}

uint8_t lcd_st7789_get_brightness(void)
{
    return s_brightness;
}
