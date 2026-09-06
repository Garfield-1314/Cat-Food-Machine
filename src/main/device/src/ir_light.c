#include "device/inc/ir_light.h"

#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "ir_light";
static bool s_initialized = false;
static bool s_on = false;
static uint8_t s_duty_percent = IR_LIGHT_DUTY_PERCENT;

/* 与 LCD 背光 (TIMER_0/CHANNEL_0) 错开，避免冲突。 */
#define IR_LIGHT_PWM_TIMER    LEDC_TIMER_1
#define IR_LIGHT_PWM_CHANNEL  LEDC_CHANNEL_1

static uint32_t duty_from_percent(uint32_t percent)
{
  return IR_LIGHT_PWM_DUTY_MAX * percent / 100;
}

static esp_err_t apply_current_duty(void)
{
  uint32_t duty = duty_from_percent(s_duty_percent);
  esp_err_t ret = ledc_set_duty(LEDC_LOW_SPEED_MODE, IR_LIGHT_PWM_CHANNEL,
                                duty);
  if (ret == ESP_OK) {
    ret = ledc_update_duty(LEDC_LOW_SPEED_MODE, IR_LIGHT_PWM_CHANNEL);
  }
  return ret;
}

esp_err_t ir_light_init(void)
{
  if (s_initialized) {
    return ir_light_off();
  }

  ledc_timer_config_t ledc_timer = {
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .timer_num = IR_LIGHT_PWM_TIMER,
      .duty_resolution = LEDC_TIMER_10_BIT, /* 0-1023 */
      .freq_hz = IR_LIGHT_PWM_FREQ_HZ,
      .clk_cfg = LEDC_AUTO_CLK,
  };
  esp_err_t ret = ledc_timer_config(&ledc_timer);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "ledc timer config failed: %s", esp_err_to_name(ret));
    return ret;
  }

  ledc_channel_config_t ledc_channel = {
      .gpio_num = IR_LIGHT_GPIO,
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .channel = IR_LIGHT_PWM_CHANNEL,
      .timer_sel = IR_LIGHT_PWM_TIMER,
      .duty = 0, /* 默认关闭 */
      .hpoint = 0,
  };
  ret = ledc_channel_config(&ledc_channel);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "ledc channel config failed: %s", esp_err_to_name(ret));
    return ret;
  }

  s_initialized = true;
  ESP_LOGI(TAG, "initialized on GPIO%d, PWM %dHz, duty %d%%", IR_LIGHT_GPIO,
           IR_LIGHT_PWM_FREQ_HZ, IR_LIGHT_DUTY_PERCENT);
  return ESP_OK;
}

esp_err_t ir_light_set(bool on)
{
  if (!s_initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  uint32_t duty = on ? duty_from_percent(s_duty_percent) : 0;
  esp_err_t ret = ledc_set_duty(LEDC_LOW_SPEED_MODE, IR_LIGHT_PWM_CHANNEL,
                                duty);
  if (ret == ESP_OK) {
    ret = ledc_update_duty(LEDC_LOW_SPEED_MODE, IR_LIGHT_PWM_CHANNEL);
  }
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "failed to turn %s: %s", on ? "on" : "off",
             esp_err_to_name(ret));
    return ret;
  }
  s_on = on;

  ESP_LOGI(TAG, "IR light %s (duty %lu/%lu)", on ? "on" : "off",
           (unsigned long)duty, (unsigned long)IR_LIGHT_PWM_DUTY_MAX);
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

esp_err_t ir_light_set_duty_percent(uint8_t percent)
{
  if (!s_initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  if (percent < IR_LIGHT_DUTY_MIN) {
    percent = IR_LIGHT_DUTY_MIN;
  }
  if (percent > IR_LIGHT_DUTY_MAX) {
    percent = IR_LIGHT_DUTY_MAX;
  }

  s_duty_percent = percent;

  /* 已点亮时立即以新亮度生效。 */
  if (s_on) {
    esp_err_t ret = apply_current_duty();
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "failed to apply duty %d%%: %s", percent,
               esp_err_to_name(ret));
      return ret;
    }
  }
  ESP_LOGI(TAG, "IR light duty set to %d%%", percent);
  return ESP_OK;
}

uint8_t ir_light_get_duty_percent(void)
{
  return s_duty_percent;
}
