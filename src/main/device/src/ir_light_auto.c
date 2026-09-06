#include "device/inc/ir_light_auto.h"

#include <stdint.h>

#include "device/inc/ir_light.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/*
 * OV2640 Sensor bank 寄存器（0xFF=0x01）。
 * 推流稳态下 sensor bank 处于激活状态，可直接经摄像头模块的读寄存器
 * 接口读取；控制周期短、只读不写，不会破坏摄像头驱动的 bank 缓存。
 */
#define OV2640_REG_GAIN   0x00 /* AGC 增益（自动模式下硬件持续更新） */
#define OV2640_REG_AEC    0x10 /* 自动曝光 */
#define OV2640_REG_YAVG   0x2F /* 平均亮度 Y */

/* ---- 任务参数 ---- */
#define IR_AUTO_TASK_NAME     "ir_light_auto"
#define IR_AUTO_TASK_STACK    3072
#define IR_AUTO_TASK_PRIORITY 2
#define IR_AUTO_PERIOD_MS     600
#define IR_AUTO_STEP_PERCENT  3   /* 单步调节幅度（%） */
#define IR_AUTO_SETTLE_CYCLES 2   /* 调整后等待的周期数，等自动曝光收敛 */
#define IR_AUTO_STOP_WAIT_MS  1000

/*
 * 亮度判据（以下为经验初值，需实机标定；任务日志会输出每次读数与动作，
 * 可据此调整死区与饱和阈值）。
 * YAVG 为传感器实测平均亮度 0-255：
 *  - 低于 IR_AUTO_YAVG_MIN 且曝光/增益已饱和（推不上去）-> 加亮
 *  - 高于 IR_AUTO_YAVG_MAX 且当前有亮度 -> 减亮
 */
#define IR_AUTO_YAVG_MIN 52
#define IR_AUTO_YAVG_MAX 110

/* 曝光/增益判“已饱和、画面仍暗”的阈值 */
#define IR_AUTO_AEC_SAT  200
#define IR_AUTO_GAIN_SAT 240

static const char *TAG = "ir_light_auto";
static ir_light_reg_read_fn s_read_fn = NULL;
static volatile TaskHandle_t s_task = NULL;
static volatile bool s_stop_requested = false;

static void ir_light_auto_control(void)
{
  uint8_t aec = 0;
  uint8_t gain = 0;
  uint8_t yavg = 0;

  if (s_read_fn == NULL) {
    return;
  }
  if (s_read_fn(OV2640_REG_AEC, &aec) != ESP_OK) {
    return; /* 读取失败：保持当前亮度 */
  }
  if (s_read_fn(OV2640_REG_GAIN, &gain) != ESP_OK) {
    return;
  }
  /* YAVG 仅作为亮度判据，读取失败时保守地不调节。 */
  if (s_read_fn(OV2640_REG_YAVG, &yavg) != ESP_OK) {
    return;
  }

  uint8_t current = ir_light_get_brightness();
  uint8_t max_pct = ir_light_get_max_brightness();
  int next = -1;

  if (current > max_pct) {
    /* 运行中上限被调低：先压回上限 */
    next = max_pct;
  } else if (yavg < IR_AUTO_YAVG_MIN &&
             (aec >= IR_AUTO_AEC_SAT || gain >= IR_AUTO_GAIN_SAT)) {
    /* 过暗：曝光/增益接近饱和仍不够亮 -> 加灯（不超过上限） */
    if (current < max_pct) {
      next = current + IR_AUTO_STEP_PERCENT;
      if (next > max_pct) {
        next = max_pct;
      }
    }
  } else if (yavg > IR_AUTO_YAVG_MAX && current > 0) {
    /* 够亮 -> 减灯，减轻环境光充足时的补光浪费 */
    next = current - IR_AUTO_STEP_PERCENT;
    if (next < 0) {
      next = 0;
    }
  }

  ESP_LOGD(TAG, "aec=%d gain=%d yavg=%d duty=%d%% max=%d%%", aec, gain, yavg,
           current, max_pct);

  if (next >= 0 && next != current) {
    if (ir_light_set_brightness((uint8_t)next) == ESP_OK) {
      ESP_LOGI(TAG, "auto adjust brightness %d%% -> %d%% (aec=%d gain=%d "
                    "yavg=%d)",
               current, next, aec, gain, yavg);
    }
  }
}

static void ir_light_auto_task(void *arg)
{
  (void)arg;
  int settle = 0;

  ESP_LOGI(TAG, "auto brightness task started (period=%dms step=%d%%)",
           IR_AUTO_PERIOD_MS, IR_AUTO_STEP_PERCENT);

  while (!s_stop_requested) {
    /* 周期唤醒执行一次控制；收到通知立即醒来检查退出标志。 */
    xTaskNotifyWait(0, ULONG_MAX, NULL, pdMS_TO_TICKS(IR_AUTO_PERIOD_MS));
    if (s_stop_requested) {
      break;
    }
    if (settle > 0) {
      settle--; /* 调整后的收敛等待期内不动作 */
      continue;
    }
    uint8_t duty_before = ir_light_get_brightness();
    ir_light_auto_control();
    if (ir_light_get_brightness() != duty_before) {
      settle = IR_AUTO_SETTLE_CYCLES;
    }
  }

  s_task = NULL;
  vTaskDelete(NULL);
}

esp_err_t ir_light_auto_init(ir_light_reg_read_fn read_fn)
{
  if (read_fn == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  s_read_fn = read_fn;
  return ESP_OK;
}

esp_err_t ir_light_auto_start(void)
{
  if (s_read_fn == NULL) {
    return ESP_ERR_INVALID_STATE;
  }
  if (s_task != NULL) {
    return ESP_OK;
  }

  s_stop_requested = false;
  BaseType_t ret = xTaskCreate(ir_light_auto_task, IR_AUTO_TASK_NAME,
                               IR_AUTO_TASK_STACK, NULL, IR_AUTO_TASK_PRIORITY,
                               (TaskHandle_t *)&s_task);
  if (ret != pdPASS) {
    s_task = NULL;
    ESP_LOGE(TAG, "failed to create auto brightness task");
    return ESP_FAIL;
  }
  return ESP_OK;
}

esp_err_t ir_light_auto_stop(void)
{
  TaskHandle_t task = s_task;
  if (task == NULL) {
    return ESP_OK;
  }

  s_stop_requested = true;
  xTaskNotifyGive(task);

  TickType_t deadline =
      xTaskGetTickCount() + pdMS_TO_TICKS(IR_AUTO_STOP_WAIT_MS);
  while (s_task != NULL && xTaskGetTickCount() < deadline) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  if (s_task != NULL) {
    /* 兜底：任务可能阻塞在寄存器读取中未及时退出。 */
    ESP_LOGW(TAG, "auto brightness task did not exit in time, force delete");
    s_task = NULL;
    vTaskDelete(task);
  }
  return ESP_OK;
}

bool ir_light_auto_is_running(void)
{
  return s_task != NULL;
}
