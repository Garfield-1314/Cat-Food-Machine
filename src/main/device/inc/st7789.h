#ifndef __ST7789_H
#define __ST7789_H

#include "esp_err.h"
#include "esp_lcd_panel_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LCD_WIDTH  320
#define LCD_HEIGHT 240

// LCD引脚定义 (按 example/LVGL)
#define LCD_SPI_HOST   SPI3_HOST
#define LCD_PIN_SDO   -1
#define LCD_PIN_SDA   36
#define LCD_PIN_CLK   35
#define LCD_PIN_CS    37
#define LCD_PIN_DC    38
#define LCD_PIN_RST   47
#define LCD_PIN_BL    48

// LCD引脚配置结构
typedef struct {
  int spi_host;      // SPI主机 (SPI2_HOST 或 SPI3_HOST)
  int pin_mosi;      // MOSI引脚
  int pin_clk;       // 时钟引脚
  int pin_cs;        // 片选引脚
  int pin_dc;        // 数据/命令引脚
  int pin_rst;       // 复位引脚
  int pin_bl;        // 背光引脚
  int lcd_width;     // 屏幕宽度
  int lcd_height;    // 屏幕高度
  int spi_freq_hz;   // SPI频率
} lcd_config_t;

/**
 * @brief 初始化ST7789 LCD显示屏
 *
 * @param config LCD配置参数
 * @return esp_err_t 成功返回ESP_OK
 */
esp_err_t lcd_st7789_init(void);

/**
 * @brief 获取LCD面板句柄
 *
 * @return esp_lcd_panel_handle_t LCD面板句柄
 */
esp_lcd_panel_handle_t lcd_st7789_get_panel_handle(void);

/**
 * @brief 绘制矩形
 *
 * @param x1 起始X坐标
 * @param y1 起始Y坐标
 * @param x2 结束X坐标
 * @param y2 结束Y坐标
 * @param color 颜色值 (RGB565格式)
 */
void lcd_st7789_draw_rect(int x1, int y1, int x2, int y2, uint16_t color);

/**
 * @brief 填充整个屏幕
 *
 * @param color 颜色值 (RGB565格式)
 */
void lcd_st7789_fill_screen(uint16_t color);

/**
 * @brief 绘制位图
 *
 * @param x1 起始X坐标
 * @param y1 起始Y坐标
 * @param x2 结束X坐标
 * @param y2 结束Y坐标
 * @param color_data 颜色数据数组
 */
void lcd_st7789_draw_bitmap(int x1, int y1, int x2, int y2,
                            const uint16_t *color_data);

/**
 * @brief 绘制图像
 *
 * @param x 起始X坐标
 * @param y 起始Y坐标
 * @param w 图像宽度
 * @param h 图像高度
 * @param image 图像数据数组 (RGB565格式)
 * @return esp_err_t 成功返回ESP_OK
 */
esp_err_t lcd_st7789_draw_image(int x, int y, int w, int h,
                                const uint16_t *image);

/**
 * @brief 绘制单个像素
 *
 * @param x X坐标
 * @param y Y坐标
 * @param color 颜色值 (RGB565格式)
 */
void lcd_st7789_draw_pixel(int x, int y, uint16_t color);

/**
 * @brief 填充矩形区域
 *
 * @param x 起始X坐标
 * @param y 起始Y坐标
 * @param w 矩形宽度
 * @param h 矩形高度
 * @param color 颜色值 (RGB565格式)
 */
void lcd_st7789_fill_rect(int x, int y, int w, int h, uint16_t color);

/**
 * @brief 获取当前背光亮度
 * @return 当前亮度百分比 (0-100)
 */
uint8_t lcd_st7789_get_brightness(void);

/**
 * @brief 设置背光亮度（PWM控制）
 *
 * @param percent 亮度百分比 (0 = 关闭, 1~99 = 对应亮度, 100 = 最亮)
 */
void lcd_st7789_set_brightness(uint8_t percent);

/**
 * @brief 设置背光状态（兼容旧接口）
 *
 * @param on true为开启(100%), false为关闭(0%)
 */
void lcd_st7789_set_backlight(bool on);

/**
 * @brief 获取屏幕宽度
 *
 * @return int 屏幕宽度
 */
int lcd_st7789_get_width(void);

/**
 * @brief 获取屏幕高度
 *
 * @return int 屏幕高度
 */
int lcd_st7789_get_height(void);

/**
 * @brief LCD刷新完成回调函数类型
 */
typedef bool (*lcd_flush_ready_cb_t)(void *user_ctx);

/**
 * @brief 注册LCD刷新完成回调
 *
 * @param cb 回调函数
 * @param user_ctx 用户上下文
 */
void lcd_st7789_register_flush_ready_cb(lcd_flush_ready_cb_t cb,
                                        void *user_ctx);

void ST7789_test_pattern(void);

#ifdef __cplusplus
}
#endif

#endif  // LCD_ST7789_H