#include "driver/inc/feeder_motor.h"

#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "feeder_motor";

/* ========== 引脚定义 ========== */
#define MOTOR_EN_GPIO   GPIO_NUM_45
#define MOTOR_STEP_GPIO GPIO_NUM_39
#define MOTOR_DIR_GPIO  GPIO_NUM_40
#define MOTOR_MS1_GPIO  GPIO_NUM_41
#define MOTOR_MS2_GPIO  GPIO_NUM_42
#define MOTOR_MS3_GPIO  GPIO_NUM_3

/* ========== 运动参数 ========== */
/* 硬件规格：
 *   - 步进电机步距角 1.8° → 200 整步/转
 *   - A4988 16 细分 → 3200 微步/电机转
 *   - 齿轮减速：GT20(20齿) → 90齿，减速比 4.5:1
 *   - 1 个仓位 = 输出轴旋转 90°
 *   计算：3200 微步 × (90° × 4.5 / 360°) = 3600 微步/仓位
 */
#define STEPS_PER_SLOT   3600

/* STEP 脉冲宽度 (微秒) */
#define STEP_PULSE_US    500    /* 0.5ms 高电平 */
#define STEP_DELAY_US    500    /* 0.5ms 低电平间隔 */

/* ========== 任务参数 ========== */
#define DISPENSE_TASK_STACK 4096
#define DISPENSE_TASK_PRIO  5

/* ========== 电机状态 ========== */
static volatile bool s_running = false;

/* ========== 步进脉冲发送 ========== */
static void send_step_pulses(uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        gpio_set_level(MOTOR_STEP_GPIO, 1);
        esp_rom_delay_us(STEP_PULSE_US);
        gpio_set_level(MOTOR_STEP_GPIO, 0);
        esp_rom_delay_us(STEP_DELAY_US);
    }
}

/* ========== 投喂任务 ========== */
static void dispense_task(void *arg)
{
    uint8_t slots = (uint8_t)(intptr_t)arg;

    uint32_t total_steps = (uint32_t)slots * STEPS_PER_SLOT;
    ESP_LOGI(TAG, "Dispensing %d slot(s) => %d steps", slots, total_steps);

    /* 使能电机 */
    gpio_set_level(MOTOR_EN_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(5));

    /* 设置方向 */
    gpio_set_level(MOTOR_DIR_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(2));

    /* 发送步进脉冲 */
    send_step_pulses(total_steps);

    /* 完成 */
    vTaskDelay(pdMS_TO_TICKS(5));
    gpio_set_level(MOTOR_EN_GPIO, 1);

    s_running = false;
    ESP_LOGI(TAG, "Dispense complete");
    vTaskDelete(NULL);
}

/* ========== 初始化 ========== */
void feeder_motor_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << MOTOR_EN_GPIO) |
                        (1ULL << MOTOR_STEP_GPIO) |
                        (1ULL << MOTOR_DIR_GPIO) |
                        (1ULL << MOTOR_MS1_GPIO) |
                        (1ULL << MOTOR_MS2_GPIO) |
                        (1ULL << MOTOR_MS3_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    /* 初始状态：EN=高(禁止)，16 细分 MS1=1,MS2=1,MS3=1 */
    /* 16 细分 */
    gpio_set_level(MOTOR_EN_GPIO, 1);
    gpio_set_level(MOTOR_STEP_GPIO, 0);
    gpio_set_level(MOTOR_DIR_GPIO, 0);
    gpio_set_level(MOTOR_MS1_GPIO, 1);   
    gpio_set_level(MOTOR_MS2_GPIO, 1);
    gpio_set_level(MOTOR_MS3_GPIO, 1);

    s_running = false;
    ESP_LOGI(TAG, "Feeder motor initialized (FreeRTOS task, %d steps/slot)", STEPS_PER_SLOT);
}

/* ========== 查询电机是否空闲 ========== */
bool feeder_motor_is_idle(void)
{
    return !s_running;
}

/* ========== 投喂（异步，专用RTOS任务） ========== */
void feeder_motor_dispense(uint8_t slots)
{
    if (slots == 0 || slots > 10) {
        ESP_LOGW(TAG, "Invalid slot count: %d (must be 1-10)", slots);
        return;
    }

    if (s_running) {
        ESP_LOGW(TAG, "Motor already running");
        return;
    }

    s_running = true;
    xTaskCreate(dispense_task, "dispense", DISPENSE_TASK_STACK,
                (void *)(intptr_t)slots, DISPENSE_TASK_PRIO, NULL);
}

/* ========== 同步投喂（阻塞） ========== */
void feeder_motor_dispense_sync(uint8_t slots)
{
    if (slots == 0 || slots > 10) {
        ESP_LOGW(TAG, "Invalid slot count: %d (must be 1-10)", slots);
        return;
    }

    uint32_t total_steps = (uint32_t)slots * STEPS_PER_SLOT;
    ESP_LOGI(TAG, "Dispensing %d slot(s) synced => %d steps", slots, total_steps);

    gpio_set_level(MOTOR_EN_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(5));
    gpio_set_level(MOTOR_DIR_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(2));

    send_step_pulses(total_steps);

    vTaskDelay(pdMS_TO_TICKS(5));
    gpio_set_level(MOTOR_EN_GPIO, 1);

    ESP_LOGI(TAG, "Dispense complete");
}