#include "device/inc/ir_light.h"

#include <stdint.h>

#include "driver/ledc.h"
#include "esp_log.h"
#include "nvs.h"

/* LEDC 资源：与 LCD 背光（TIMER_0 / CHANNEL_0）分开使用。 */
#define IR_LIGHT_LEDC_MODE      LEDC_LOW_SPEED_MODE
#define IR_LIGHT_LEDC_TIMER     LEDC_TIMER_1
#define IR_LIGHT_LEDC_CHANNEL   LEDC_CHANNEL_1
#define IR_LIGHT_LEDC_DUTY_RES  LEDC_TIMER_11_BIT /* 0-2047 */
#define IR_LIGHT_LEDC_DUTY_MAX  2047              /* 2^11 - 1 */

/*
 * 20kHz 超出人耳频段且对摄像头滚动快门安全。
 * 必须显式使用 APB(80MHz) 时钟：LEDC_AUTO_CLK 可能落到 8MHz RTC 时钟，
 * 11bit 分辨率下最高只能到约 3.9kHz，无法满足 20kHz。
 */
#define IR_LIGHT_LEDC_FREQ_HZ   20000

/* NVS 亮度上限存储（自动调节的 clamp 上限） */
#define IR_LIGHT_NVS_NAMESPACE  "ir_config"
#define IR_LIGHT_NVS_KEY_MAX    "max"
#define IR_LIGHT_MAX_DEFAULT    60

static const char *TAG = "ir_light";
static bool s_initialized = false;
static uint8_t s_brightness = 0;
static uint8_t s_max_brightness = IR_LIGHT_MAX_DEFAULT;

/* 从 NVS 读取亮度上限 */
static uint8_t ir_light_load_max_brightness(void)
{
  nvs_handle_t nvs;
  uint8_t val = IR_LIGHT_MAX_DEFAULT;
  if (nvs_open(IR_LIGHT_NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
    uint8_t v = IR_LIGHT_MAX_DEFAULT;
    if (nvs_get_u8(nvs, IR_LIGHT_NVS_KEY_MAX, &v) == ESP_OK) {
      val = v;
    }
    nvs_close(nvs);
  }
  if (val > IR_LIGHT_MAX_PERCENT) {
    val = IR_LIGHT_MAX_PERCENT;
  }
  return val;
}

/* 保存亮度上限到 NVS */
static esp_err_t ir_light_save_max_brightness(uint8_t val)
{
  nvs_handle_t nvs;
  esp_err_t ret = nvs_open(IR_LIGHT_NVS_NAMESPACE, NVS_READWRITE, &nvs);
  if (ret != ESP_OK) {
    return ret;
  }
  ret = nvs_set_u8(nvs, IR_LIGHT_NVS_KEY_MAX, val);
  if (ret == ESP_OK) {
    ret = nvs_commit(nvs);
  }
  nvs_close(nvs);
  return ret;
}

esp_err_t ir_light_init(void)
{
  if (s_initialized) {
    return ESP_OK;
  }

  s_max_brightness = ir_light_load_max_brightness();

  ledc_timer_config_t ledc_timer = {
      .speed_mode = IR_LIGHT_LEDC_MODE,
      .timer_num = IR_LIGHT_LEDC_TIMER,
      .duty_resolution = IR_LIGHT_LEDC_DUTY_RES,
      .freq_hz = IR_LIGHT_LEDC_FREQ_HZ,
      .clk_cfg = LEDC_USE_APB_CLK,
  };
  esp_err_t ret = ledc_timer_config(&ledc_timer);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "failed to configure LEDC timer: %s", esp_err_to_name(ret));
    return ret;
  }

  ledc_channel_config_t ledc_channel = {
      .gpio_num = IR_LIGHT_GPIO,
      .speed_mode = IR_LIGHT_LEDC_MODE,
      .channel = IR_LIGHT_LEDC_CHANNEL,
      .timer_sel = IR_LIGHT_LEDC_TIMER,
      .duty = 0, /* 默认关闭 */
      .hpoint = 0,
  };
  ret = ledc_channel_config(&ledc_channel);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "failed to configure LEDC channel on GPIO%d: %s",
             IR_LIGHT_GPIO, esp_err_to_name(ret));
    return ret;
  }

  /* 保持下拉，避免启动早期引脚悬空导致补光灯误亮。 */
  ret = gpio_set_pull_mode(IR_LIGHT_GPIO, GPIO_PULLDOWN_ONLY);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "failed to set pull-down on GPIO%d: %s", IR_LIGHT_GPIO,
             esp_err_to_name(ret));
  }

  s_brightness = 0;
  s_initialized = true;
  ESP_LOGI(TAG, "initialized on TXD0/GPIO%d, default off, max brightness %d%%",
           IR_LIGHT_GPIO, s_max_brightness);
  return ESP_OK;
}

esp_err_t ir_light_set_brightness(uint8_t percent)
{
  if (!s_initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  if (percent > IR_LIGHT_MAX_PERCENT) {
    percent = IR_LIGHT_MAX_PERCENT;
  }

  uint32_t duty = (uint32_t)percent * IR_LIGHT_LEDC_DUTY_MAX / 100;
  esp_err_t ret = ledc_set_duty(IR_LIGHT_LEDC_MODE, IR_LIGHT_LEDC_CHANNEL,
                                duty);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "failed to set duty: %s", esp_err_to_name(ret));
    return ret;
  }
  ret = ledc_update_duty(IR_LIGHT_LEDC_MODE, IR_LIGHT_LEDC_CHANNEL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "failed to update duty: %s", esp_err_to_name(ret));
    return ret;
  }

  s_brightness = percent;
  ESP_LOGD(TAG, "brightness set to %d%%", percent);
  return ESP_OK;
}

uint8_t ir_light_get_brightness(void)
{
  return s_brightness;
}

esp_err_t ir_light_set(bool on)
{
  if (on) {
    return ir_light_on();
  }
  return ir_light_off();
}

esp_err_t ir_light_on(void)
{
  esp_err_t ret = ir_light_set_brightness(s_max_brightness);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "IR light on (%d%%)", s_max_brightness);
  }
  return ret;
}

esp_err_t ir_light_off(void)
{
  esp_err_t ret = ir_light_set_brightness(0);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "IR light off");
  }
  return ret;
}

esp_err_t ir_light_set_max_brightness(uint8_t percent)
{
  if (!s_initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  if (percent > IR_LIGHT_MAX_PERCENT) {
    percent = IR_LIGHT_MAX_PERCENT;
  }

  esp_err_t ret = ir_light_save_max_brightness(percent);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "failed to save max brightness to NVS: %s",
             esp_err_to_name(ret));
    return ret;
  }

  s_max_brightness = percent;
  ESP_LOGI(TAG, "max brightness set to %d%%", percent);
  return ESP_OK;
}

uint8_t ir_light_get_max_brightness(void)
{
  return s_max_brightness;
}
