/**
 * @file lv_conf.h
 * Cat Food Machine UI 模拟器 - LVGL v8.4.0 配置
 * 与固件分辨率(320x240)和颜色深度(RGB565)保持一致。
 */
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* ===================== 颜色 ===================== */
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0
#define LV_COLOR_SCREEN_TRANSP 0

/* ===================== 内存 ===================== */
#define LV_MEM_CUSTOM 0
#if LV_MEM_CUSTOM == 0
    #define LV_MEM_SIZE (64U * 1024U)
    #define LV_MEM_ADR 0
#endif

/* ===================== 日志 ===================== */
#define LV_USE_LOG 1
#define LV_LOG_LEVEL 1

/* ============= 显示 / 输入刷新周期 ============= */
#define LV_DISP_DEF_REFR_PERIOD 30
#define LV_INDEV_DEF_READ_PERIOD 30
#define LV_TICK_CUSTOM 0
#define LV_DPI_DEF 130

/* ================ 内置字体 ================ */
/* UI 使用了 montserrat 10 / 12 / 14 / 18 */
#define LV_FONT_MONTSERRAT_10 1
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_22 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_MONTSERRAT_36 1
#define LV_FONT_MONTSERRAT_40 1
#define LV_FONT_MONTSERRAT_44 1
#define LV_FONT_MONTSERRAT_48 1

/* ================ 常用组件 ================ */
#define LV_USE_ANIM 1
#define LV_USE_LABEL 1
#define LV_USE_BTN 1
#define LV_USE_IMG 1
#define LV_USE_LINE 1
#define LV_USE_ARC 1
#define LV_USE_BAR 1
#define LV_USE_SLIDER 1
#define LV_USE_ROLLER 1
#define LV_USE_SWITCH 1
#define LV_USE_TEXTAREA 1
#define LV_USE_KEYBOARD 1
#define LV_USE_BTNMATRIX 1
#define LV_USE_CHECKBOX 1
#define LV_USE_DROPDOWN 1
#define LV_USE_LIST 1
#define LV_USE_LED 1
#define LV_USE_SPINNER 1
#define LV_USE_TABLE 1
#define LV_USE_CHART 1
#define LV_USE_CANVAS 1
#define LV_USE_MSGBOX 1
#define LV_USE_TABVIEW 1
#define LV_USE_TILEVIEW 1
#define LV_USE_WIN 1
#define LV_USE_METER 1
#define LV_USE_SPINBOX 1
#define LV_USE_COLORWHEEL 1
#define LV_USE_ANIMIMG 1
#define LV_USE_CALENDAR 1
#define LV_USE_MENU 1

#endif /* LV_CONF_H */
