#include "device/inc/gt911.h"

#include <string.h>
static const char *TAG = "Touch_GT911";

// 私有函数声明
static esp_err_t write_byte_data(gt911_dev_t *dev, uint16_t reg, uint8_t val);
static esp_err_t read_byte_data(gt911_dev_t *dev, uint16_t reg, uint8_t *data);
// static esp_err_t write_block_data(gt911_dev_t *dev, uint16_t reg, uint8_t
// *val, uint8_t size);
static esp_err_t read_block_data(gt911_dev_t *dev, uint16_t reg, uint8_t *buf,
                                 uint8_t size);
static void calculate_checksum(gt911_dev_t *dev);
static esp_err_t reflash_config(gt911_dev_t *dev);
static tp_point_t read_point(gt911_dev_t *dev, uint8_t *data);

esp_err_t gt911_init_default(gt911_dev_t *dev)
{
  // 使用默认配置初始化设备结构体
  *dev = (gt911_dev_t)GT911_DEFAULT_CONFIG;
  return gt911_init(dev);
}

esp_err_t gt911_init(gt911_dev_t *dev)
{
  esp_err_t ret;

  // 配置I2C总线
  i2c_master_bus_config_t bus_config = {
      .i2c_port = I2C_NUM_0,
      .sda_io_num = dev->pin_sda,
      .scl_io_num = dev->pin_scl,
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .glitch_ignore_cnt = 7,
      .flags.enable_internal_pullup = true,
  };

  ret = i2c_new_master_bus(&bus_config, &dev->bus_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "I2C master bus initialization failed");
    return ret;
  }

  // 配置I2C设备
  i2c_device_config_t dev_config = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = dev->addr,
      .scl_speed_hz = 200000,
  };

  ret =
      i2c_master_bus_add_device(dev->bus_handle, &dev_config, &dev->dev_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "I2C device add failed");
    i2c_del_master_bus(dev->bus_handle);
    return ret;
  }

  // 配置GPIO
  gpio_config_t io_conf = {};

  // 配置INT引脚为输出
  io_conf.mode = GPIO_MODE_OUTPUT;
  io_conf.pin_bit_mask = (1ULL << dev->pin_int);
  io_conf.pull_down_en = 0;
  io_conf.pull_up_en = 0;
  gpio_config(&io_conf);

  // 配置RST引脚为输出
  io_conf.pin_bit_mask = (1ULL << dev->pin_rst);
  gpio_config(&io_conf);

  // 执行复位
  return gt911_reset(dev);
}

esp_err_t gt911_reset(gt911_dev_t *dev)
{
  // 复位序列
  gpio_set_level(dev->pin_int, 0);
  gpio_set_level(dev->pin_rst, 0);
  vTaskDelay(pdMS_TO_TICKS(10));

  gpio_set_level(dev->pin_int, (dev->addr == GT911_ADDR2) ? 1 : 0);
  vTaskDelay(pdMS_TO_TICKS(1));

  gpio_set_level(dev->pin_rst, 1);
  vTaskDelay(pdMS_TO_TICKS(5));

  gpio_set_level(dev->pin_int, 0);
  vTaskDelay(pdMS_TO_TICKS(50));

  // 重新配置INT引脚为输入
  gpio_config_t io_conf = {
      .mode = GPIO_MODE_INPUT,
      .pin_bit_mask = (1ULL << dev->pin_int),
      .pull_up_en = 0,
      .pull_down_en = 0,
  };
  gpio_config(&io_conf);

  vTaskDelay(pdMS_TO_TICKS(50));

  // 读取配置
  uint8_t config_buf[GT911_CONFIG_SIZE];
  esp_err_t ret =
      read_block_data(dev, GT911_CONFIG_START, config_buf, GT911_CONFIG_SIZE);
  if (ret == ESP_OK) {
    memcpy(dev->config_buf, config_buf, GT911_CONFIG_SIZE);
    gt911_set_resolution(dev, dev->width, dev->height);
  }

  return ret;
}

esp_err_t gt911_set_rotation(gt911_dev_t *dev, uint8_t rotation)
{
  if (rotation > ROTATION_NORMAL) {
    return ESP_ERR_INVALID_ARG;
  }
  dev->rotation = rotation;
  return ESP_OK;
}

esp_err_t gt911_set_resolution(gt911_dev_t *dev, uint16_t width,
                               uint16_t height)
{
  dev->width = width;
  dev->height = height;

  dev->config_buf[GT911_X_OUTPUT_MAX_LOW - GT911_CONFIG_START] = width & 0xFF;
  dev->config_buf[GT911_X_OUTPUT_MAX_HIGH - GT911_CONFIG_START] =
      (width >> 8) & 0xFF;
  dev->config_buf[GT911_Y_OUTPUT_MAX_LOW - GT911_CONFIG_START] = height & 0xFF;
  dev->config_buf[GT911_Y_OUTPUT_MAX_HIGH - GT911_CONFIG_START] =
      (height >> 8) & 0xFF;

  return reflash_config(dev);
}

esp_err_t gt911_read(gt911_dev_t *dev)
{
  uint8_t point_info;
  esp_err_t ret = read_byte_data(dev, GT911_POINT_INFO, &point_info);
  if (ret != ESP_OK) {
    return ret;
  }

  uint8_t buffer_status = (point_info >> 7) & 1;
  // uint8_t proximity_valid = (point_info >> 5) & 1;
  // uint8_t have_key = (point_info >> 4) & 1;
  dev->is_large_detect = (point_info >> 6) & 1;
  dev->touches = point_info & 0xF;

  dev->is_touched = (dev->touches > 0);

  if (buffer_status == 1 && dev->is_touched) {
    for (uint8_t i = 0; i < dev->touches; i++) {
      uint8_t data[7];
      ret = read_block_data(dev, GT911_POINT_1 + i * 8, data, 7);
      if (ret == ESP_OK) {
        dev->points[i] = read_point(dev, data);
      }
    }
  }

  // 清除触摸状态
  return write_byte_data(dev, GT911_POINT_INFO, 0);
}

uint8_t gt911_get_touch_count(gt911_dev_t *dev) { return dev->touches; }

bool gt911_is_touched(gt911_dev_t *dev) { return dev->is_touched; }

tp_point_t gt911_get_point(gt911_dev_t *dev, uint8_t index)
{
  if (index < 5) {
    return dev->points[index];
  }
  return (tp_point_t){0};
}

// 私有函数实现
static void calculate_checksum(gt911_dev_t *dev)
{
  uint8_t checksum = 0;
  for (uint8_t i = 0; i < GT911_CONFIG_SIZE; i++) {
    checksum += dev->config_buf[i];
  }
  checksum = (~checksum) + 1;
  dev->config_buf[GT911_CONFIG_CHKSUM - GT911_CONFIG_START] = checksum;
}

static esp_err_t reflash_config(gt911_dev_t *dev)
{
  calculate_checksum(dev);

  esp_err_t ret = write_byte_data(
      dev, GT911_CONFIG_CHKSUM,
      dev->config_buf[GT911_CONFIG_CHKSUM - GT911_CONFIG_START]);
  if (ret != ESP_OK) {
    return ret;
  }

  return write_byte_data(dev, GT911_CONFIG_FRESH, 1);
}

static tp_point_t read_point(gt911_dev_t *dev, uint8_t *data)
{
  uint16_t temp;
  uint8_t id = data[0];
  uint16_t x = ((uint16_t)data[2] << 8) | data[1];
  uint16_t y = ((uint16_t)data[4] << 8) | data[3];
  uint16_t size = ((uint16_t)data[6] << 8) | data[5];

  switch (dev->rotation) {
    case ROTATION_NORMAL:
      x = dev->width - x;
      y = dev->height - y;
      break;
    case ROTATION_LEFT:
      temp = x;
      x = dev->width - y;
      y = temp;
      break;
    case ROTATION_INVERTED:
      // 保持不变
      break;
    case ROTATION_RIGHT:
      temp = x;
      x = y;
      y = dev->height - temp;
      break;
    default:
      break;
  }

  return (tp_point_t){id, x, y, size};
}

static esp_err_t write_byte_data(gt911_dev_t *dev, uint16_t reg, uint8_t val)
{
  uint8_t write_buf[3] = {(uint8_t)(reg >> 8),    // 高字节
                          (uint8_t)(reg & 0xFF),  // 低字节
                          val};

  return i2c_master_transmit(dev->dev_handle, write_buf, 3, 1000);
}

static esp_err_t read_byte_data(gt911_dev_t *dev, uint16_t reg, uint8_t *data)
{
  uint8_t reg_buf[2] = {(uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF)};

  return i2c_master_transmit_receive(dev->dev_handle, reg_buf, 2, data, 1,
                                     1000);
}

static esp_err_t read_block_data(gt911_dev_t *dev, uint16_t reg, uint8_t *buf,
                                 uint8_t size)
{
  uint8_t reg_buf[2] = {(uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF)};

  return i2c_master_transmit_receive(dev->dev_handle, reg_buf, 2, buf, size,
                                     1000);
}