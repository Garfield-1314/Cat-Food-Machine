#ifndef GT911_H
#define GT911_H

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// GT911引脚定义 (按 example/LVGL)
#define GT911_PIN_SCL GPIO_NUM_1
#define GT911_PIN_SDA GPIO_NUM_2
#define GT911_PIN_INT GPIO_NUM_21
#define GT911_PIN_RST GPIO_NUM_14

#define GT911_ADDR1 (uint8_t)0x5D
#define GT911_ADDR2 (uint8_t)0x14

#define ROTATION_LEFT (uint8_t)0
#define ROTATION_INVERTED (uint8_t)1
#define ROTATION_RIGHT (uint8_t)2
#define ROTATION_NORMAL (uint8_t)3

// 寄存器定义（与原始头文件相同）
#define GT911_COMMAND (uint16_t)0x8040
#define GT911_CONFIG_START (uint16_t)0x8047
// 屏幕分辨率相关寄存器
#define GT911_X_OUTPUT_MAX_LOW (uint16_t)0x8048
#define GT911_X_OUTPUT_MAX_HIGH (uint16_t)0x8049
#define GT911_Y_OUTPUT_MAX_LOW (uint16_t)0x804A
#define GT911_Y_OUTPUT_MAX_HIGH (uint16_t)0x804B
// ... 其他寄存器定义保持不变

#define GT911_CONFIG_SIZE (uint16_t)0xFF - 0x46
#define GT911_POINT_INFO (uint16_t)0X814E
#define GT911_POINT_1 (uint16_t)0X814F
#define GT911_CONFIG_CHKSUM (uint16_t)0X80FF
#define GT911_CONFIG_FRESH (uint16_t)0X8100

// 默认GT911配置
#define GT911_DEFAULT_CONFIG \
  {.pin_sda = GT911_PIN_SDA, \
   .pin_scl = GT911_PIN_SCL, \
   .pin_int = GT911_PIN_INT, \
   .pin_rst = GT911_PIN_RST, \
   .width = 320,             \
   .height = 240,            \
   .addr = GT911_ADDR1,      \
   .rotation = ROTATION_NORMAL}

typedef struct {
  uint8_t id;
  uint16_t x;
  uint16_t y;
  uint16_t size;
} tp_point_t;

typedef struct {
  i2c_master_bus_handle_t bus_handle;
  i2c_master_dev_handle_t dev_handle;
  gpio_num_t pin_sda;
  gpio_num_t pin_scl;
  gpio_num_t pin_int;
  gpio_num_t pin_rst;
  uint16_t width;
  uint16_t height;
  uint8_t addr;
  uint8_t rotation;
  uint8_t config_buf[GT911_CONFIG_SIZE];

  // 状态变量
  uint8_t is_large_detect;
  uint8_t touches;
  bool is_touched;
  tp_point_t points[5];
} gt911_dev_t;

// 函数声明
esp_err_t gt911_init(gt911_dev_t *dev);
esp_err_t gt911_init_default(gt911_dev_t *dev);
esp_err_t gt911_reset(gt911_dev_t *dev);
esp_err_t gt911_set_rotation(gt911_dev_t *dev, uint8_t rotation);
esp_err_t gt911_set_resolution(gt911_dev_t *dev, uint16_t width,
                               uint16_t height);
esp_err_t gt911_read(gt911_dev_t *dev);
uint8_t gt911_get_touch_count(gt911_dev_t *dev);
bool gt911_is_touched(gt911_dev_t *dev);
tp_point_t gt911_get_point(gt911_dev_t *dev, uint8_t index);

#endif