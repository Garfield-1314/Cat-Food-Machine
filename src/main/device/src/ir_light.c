#include "device/inc/ir_light.h"

#include "esp_log.h"

static const char *TAG = "ir_light";
static bool s_initialized = false;

esp_err_t ir_light_init(void)
{
  if (s_initialized) {
    return ir_light_off();
  }

  gpio_config_t io_conf = {
      .pin_bit_mask = 1ULL << IR_LIGHT_GPIO,
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_ENABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };

  esp_err_t ret = gpio_config(&io_conf);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "failed to configure GPIO%d: %s", IR_LIGHT_GPIO,
             esp_err_to_name(ret));
    return ret;
  }

  ret = gpio_set_level(IR_LIGHT_GPIO, IR_LIGHT_OFF_LEVEL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "failed to turn off during init: %s",
             esp_err_to_name(ret));
    return ret;
  }

  s_initialized = true;
  ESP_LOGI(TAG, "initialized on TXD0/GPIO%d, default off", IR_LIGHT_GPIO);
  return ESP_OK;
}

esp_err_t ir_light_set(bool on)
{
  if (!s_initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  int level = on ? IR_LIGHT_ON_LEVEL : IR_LIGHT_OFF_LEVEL;
  esp_err_t ret = gpio_set_level(IR_LIGHT_GPIO, level);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "failed to turn %s: %s", on ? "on" : "off",
             esp_err_to_name(ret));
    return ret;
  }

  ESP_LOGI(TAG, "IR light %s", on ? "on" : "off");
  return ESP_OK;
}

esp_err_t ir_light_on(void)
{
  return ir_light_set(true);
}

esp_err_t ir_light_off(void)
{
  return ir_light_set(false);
}
