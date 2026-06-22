#include "device/inc/st7789.h"

#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "LCD_ST7789";

// 全局变量
static esp_lcd_panel_handle_t panel_handle = NULL;
static esp_lcd_panel_io_handle_t io_handle = NULL;
static int lcd_width = 0;
static int lcd_height = 0;
static int backlight_pin = -1;
static uint8_t s_current_brightness = 100;

// 回调函数
static lcd_flush_ready_cb_t flush_ready_cb = NULL;
static void *flush_ready_user_ctx = NULL;

// 内部传输完成回调
static bool lcd_panel_io_color_trans_done(esp_lcd_panel_io_handle_t panel_io,
                                          esp_lcd_panel_io_event_data_t *edata,
                                          void *user_ctx)
{
  if (flush_ready_cb) {
    return flush_ready_cb(flush_ready_user_ctx);
  }
  return false;
}

void lcd_st7789_register_flush_ready_cb(lcd_flush_ready_cb_t cb,
                                        void *user_ctx)
{
  flush_ready_cb = cb;
  flush_ready_user_ctx = user_ctx;
}

lcd_config_t config = {
    .spi_host = LCD_SPI_HOST,
    .pin_mosi = LCD_PIN_SDA,
    .pin_clk = LCD_PIN_CLK,
    .pin_cs = LCD_PIN_CS,
    .pin_dc = LCD_PIN_DC,
    .pin_rst = LCD_PIN_RST,
    .pin_bl = LCD_PIN_BL,
    .lcd_width = LCD_WIDTH,
    .lcd_height = LCD_HEIGHT,
    .spi_freq_hz = 40 * 1000 * 1000,  // 降低SPI频率至40MHz以提高稳定性
};

esp_err_t lcd_st7789_init(void)
{
  esp_err_t ret = ESP_OK;
  // 保存配置
  lcd_width = config.lcd_width;
  lcd_height = config.lcd_height;
  backlight_pin = config.pin_bl;
  ESP_LOGI(TAG, "初始化ST7789 LCD显示屏 (%dx%d)", lcd_width, lcd_height);
  /* 配置背光 LEDC (PWM 控制) */
  if (config.pin_bl >= 0) {
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_10_BIT,  /* 0-1023 */
        .freq_hz = 5000,                        /* 5kHz PWM */
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel = {
        .gpio_num = config.pin_bl,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,   /* 初始关闭 */
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
  }
  // 配置SPI接口
  spi_bus_config_t buscfg = {
      .miso_io_num = -1,
      .mosi_io_num = config.pin_mosi,
      .sclk_io_num = config.pin_clk,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = config.lcd_width * config.lcd_height * 2,
      .flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_GPIO_PINS,
  };

  // 初始化SPI总线
  ret = spi_bus_initialize(config.spi_host, &buscfg, SPI_DMA_CH_AUTO);
  ESP_RETURN_ON_ERROR(ret, TAG, "SPI总线初始化失败");
  // 配置LCD IO
  esp_lcd_panel_io_spi_config_t io_config = {
      .dc_gpio_num = config.pin_dc,
      .cs_gpio_num = config.pin_cs,
      .pclk_hz = config.spi_freq_hz,
      .lcd_cmd_bits = 8,
      .lcd_param_bits = 8,
      .spi_mode = 0,
      .trans_queue_depth = 10,
      .on_color_trans_done = lcd_panel_io_color_trans_done,
      .user_ctx = NULL,
      .flags =
          {
              .dc_low_on_data = 0,
              .octal_mode = 0,
              .lsb_first = 0,
              .cs_high_active = 0,
          },
  };
  // 附加IO到SPI总线
  ret = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)config.spi_host,
                                 &io_config, &io_handle);
  ESP_RETURN_ON_ERROR(ret, TAG, "LCD IO附加失败");
  // ST7789面板配置
  esp_lcd_panel_dev_config_t panel_config = {
      .reset_gpio_num = config.pin_rst,
      .color_space = ESP_LCD_COLOR_SPACE_RGB,
      .bits_per_pixel = 16,
  };
  // 创建ST7789面板
  ret = esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle);

  // 复位面板
  ret = esp_lcd_panel_reset(panel_handle);

  // 初始化面板
  ret = esp_lcd_panel_init(panel_handle);

  // 设置显示方向为横屏（交换XY轴）
  ret = esp_lcd_panel_swap_xy(panel_handle, true);

  ret = esp_lcd_panel_mirror(panel_handle, false, true);

  // 开启显示
  ret = esp_lcd_panel_disp_on_off(panel_handle, true);

  // 开启背光（最亮）
  if (config.pin_bl >= 0) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 1023);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
  }

  ESP_LOGI(TAG, "LCD初始化完成");
  return ESP_OK;
}

esp_lcd_panel_handle_t lcd_st7789_get_panel_handle(void)
{
  return panel_handle;
}

void lcd_st7789_draw_rect(int x1, int y1, int x2, int y2, uint16_t color)
{
  if (panel_handle == NULL) {
    ESP_LOGE(TAG, "面板未初始化");
    return;
  }
  esp_lcd_panel_draw_bitmap(panel_handle, x1, y1, x2, y2, (void *)&color);
}

void lcd_st7789_fill_screen(uint16_t color)
{
  if (panel_handle == NULL) {
    ESP_LOGE(TAG, "面板未初始化");
    return;
  }

  // 使用更小的块大小来减少内存压力
  const int CHUNK_ROWS = 10;  // 一次处理10行
  int chunk_size = lcd_width * CHUNK_ROWS;
  uint16_t *color_data = malloc(chunk_size * sizeof(uint16_t));
  if (color_data == NULL) {
    ESP_LOGE(TAG, "内存分配失败，尝试更小的缓冲区");
    // 尝试更小的缓冲区（单行）
    color_data = malloc(lcd_width * sizeof(uint16_t));
    if (color_data == NULL) {
      ESP_LOGE(TAG, "单行缓冲区也分配失败");
      return;
    }
    // 填充缓冲区
    for (int i = 0; i < lcd_width; i++) {
      color_data[i] = color;
    }
    // 逐行绘制
    for (int y = 0; y < lcd_height; y++) {
      esp_lcd_panel_draw_bitmap(panel_handle, 0, y, lcd_width, y + 1,
                                color_data);
    }
  } else {
    // 填充缓冲区为指定颜色
    for (int i = 0; i < chunk_size; i++) {
      color_data[i] = color;
    }

    // 按块绘制
    for (int y = 0; y < lcd_height; y += CHUNK_ROWS) {
      int rows = (y + CHUNK_ROWS > lcd_height) ? (lcd_height - y) : CHUNK_ROWS;
      esp_lcd_panel_draw_bitmap(panel_handle, 0, y, lcd_width, y + rows,
                                color_data);
    }
  }

  free(color_data);
}

void lcd_st7789_draw_bitmap(int x1, int y1, int x2, int y2,
                            const uint16_t *color_data)
{
  if (panel_handle == NULL) {
    ESP_LOGE(TAG, "面板未初始化");
    return;
  }

  if (color_data == NULL) {
    ESP_LOGE(TAG, "颜色数据为空");
    return;
  }

  esp_lcd_panel_draw_bitmap(panel_handle, x1, y1, x2, y2, color_data);
}

esp_err_t lcd_st7789_draw_image(int x, int y, int w, int h,
                                const uint16_t *image)
{
  if (panel_handle == NULL) {
    ESP_LOGE(TAG, "面板未初始化");
    return ESP_ERR_INVALID_STATE;
  }

  if (image == NULL) {
    ESP_LOGE(TAG, "图像数据为空");
    return ESP_ERR_INVALID_ARG;
  }

  // 输入参数有效性检查
  if (w <= 0 || h <= 0) {
    return ESP_OK;
  }

  // 简单边界检查，防止越界导致驱动崩溃
  // 注意：LVGL通常保证传入的坐标在屏幕范围内，这里作为最后一道防线
  if (x < 0 || y < 0 || x + w > lcd_width || y + h > lcd_height) {
    ESP_LOGW(TAG, "绘制区域越界: (%d,%d) %dx%d, 屏幕:%dx%d", x, y, w, h,
             lcd_width, lcd_height);
    // 如果越界，可以选择裁剪或者直接返回错误。
    // 鉴于我们移除了复杂的裁剪逻辑，这里如果越界严重则不绘制
    if (x >= lcd_width || y >= lcd_height || x + w <= 0 || y + h <= 0) {
      return ESP_OK;
    }
    // 对于部分越界，esp_lcd_panel_draw_bitmap 可能会处理，或者导致断言失败
    // 为了安全，这里不做处理，依赖上层（LVGL）的正确性
  }

  // 直接使用传入的图像数据绘制
  // 注意：esp_lcd_panel_draw_bitmap的x2,y2参数是exclusive（不包含），所以需要+1
  // 并且我们不再使用中间缓冲区进行字节交换，请确保LVGL配置了 LV_COLOR_16_SWAP
  esp_err_t ret =
      esp_lcd_panel_draw_bitmap(panel_handle, x, y, x + w, y + h, image);

  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "绘制失败: (%d,%d) %dx%d, err=%d", x, y, w, h, ret);
  }

  return ret;
}

void lcd_st7789_draw_pixel(int x, int y, uint16_t color)
{
  if (panel_handle == NULL) {
    ESP_LOGE(TAG, "面板未初始化");
    return;
  }

  if (x >= lcd_width || y >= lcd_height || x < 0 || y < 0) {
    return;
  }

  esp_lcd_panel_draw_bitmap(panel_handle, x, y, x + 1, y + 1, (void *)&color);
}

void lcd_st7789_fill_rect(int x, int y, int w, int h, uint16_t color)
{
  if (panel_handle == NULL) {
    ESP_LOGE(TAG, "面板未初始化");
    return;
  }

  // 检查边界
  if (x >= lcd_width || y >= lcd_height || x < 0 || y < 0) {
    return;
  }

  // 调整绘制区域
  if (x + w > lcd_width) {
    w = lcd_width - x;
  }
  if (y + h > lcd_height) {
    h = lcd_height - y;
  }

  // 创建单行缓冲区
  size_t buffer_size = w * sizeof(uint16_t);
  uint16_t *line_buffer = malloc(buffer_size);
  if (line_buffer == NULL) {
    ESP_LOGE(TAG, "内存分配失败，需要%zu字节", buffer_size);
    ESP_LOGE(TAG, "空闲堆: %d字节", heap_caps_get_free_size(MALLOC_CAP_8BIT));
    return;
  }

  // 填充缓冲区为指定颜色
  for (int i = 0; i < w; i++) {
    line_buffer[i] = color;
  }

  // 逐行绘制
  for (int row = 0; row < h; row++) {
    esp_lcd_panel_draw_bitmap(panel_handle, x, y + row, x + w, y + row + 1,
                              line_buffer);
  }

  free(line_buffer);
}

uint8_t lcd_st7789_get_brightness(void)
{
  return s_current_brightness;
}

void lcd_st7789_set_brightness(uint8_t percent)
{
  if (backlight_pin < 0) return;

  if (percent > 100) percent = 100;

  s_current_brightness = percent;
  uint32_t duty = (uint32_t)percent * 1023 / 100;
  ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

void lcd_st7789_set_backlight(bool on)
{
  lcd_st7789_set_brightness(on ? 100 : 0);
}

int lcd_st7789_get_width(void)
{
  return lcd_width;
}

int lcd_st7789_get_height(void)
{
  return lcd_height;
}

void ST7789_test_pattern(void)
{
  // 测试填充颜色
  lcd_st7789_fill_screen(0xF800);  // 红色
  vTaskDelay(pdMS_TO_TICKS(1000));
  lcd_st7789_fill_screen(0x07E0);  // 绿色
  vTaskDelay(pdMS_TO_TICKS(1000));
  lcd_st7789_fill_screen(0x001F);  // 蓝色
  vTaskDelay(pdMS_TO_TICKS(1000));
  lcd_st7789_fill_screen(0xFFFF);  // 白色
  vTaskDelay(pdMS_TO_TICKS(1000));
  lcd_st7789_fill_screen(0x0000);  // 黑色
  vTaskDelay(pdMS_TO_TICKS(1000));
}