#include "include.h"

static const char *TAG = "main";

/* ========== 投喂弹窗 ========== */
static lv_obj_t *s_feeding_popup = NULL;
static volatile bool s_feeding_active = false;
static volatile bool s_feeding_done = false;
static uint8_t s_feeding_amount = 0;

/* ========== 背光自动熄灭 ========== */
#define IDLE_TIMEOUT_MS  300000   /* 5 分钟无操作熄灭背光 */
static bool s_backlight_dimmed = false;

/* WiFi 连接成功后的回调 */
static void on_wifi_connected(void)
{
    ESP_LOGI(TAG, "WiFi connected, starting SNTP time sync...");
    sntp_time_init();
}

/* 手动喂食：外部可调用，喂指定仓位数 */
void manual_feeding_start(uint8_t slots)
{
    if (s_feeding_active) {
        ESP_LOGW(TAG, "Feeding already in progress");
        return;
    }

    /* 如果背光已熄灭，喂食时自动唤醒 */
    if (s_backlight_dimmed) {
        s_backlight_dimmed = false;
        uint8_t saved = lcd_st7789_get_brightness();
        if (saved == 0) saved = 100;
        lcd_st7789_set_brightness(saved);
        lv_disp_trig_activity(NULL);
        ESP_LOGI(TAG, "Backlight restored for manual feeding (%d%%)", saved);
    }

    s_feeding_amount = slots;
    s_feeding_active = true;
    s_feeding_done = false;

    feeder_motor_dispense(slots);
    ESP_LOGI(TAG, "Manual feeding started: %d slot(s)", slots);
}

/* 投喂触发回调：调度器匹配到投喂时间时调用 */
static void on_feeding_triggered(const feed_schedule_item_t *item)
{
    ESP_LOGI(TAG, ">>> FEEDING TRIGGERED! Time=%02d:%02d, Amount=%d slot(s)",
             item->hour, item->minute, item->amount);

    /* 如果背光已熄灭，投喂时自动唤醒 */
    if (s_backlight_dimmed) {
        s_backlight_dimmed = false;
        uint8_t saved = lcd_st7789_get_brightness();
        if (saved == 0) saved = 100;
        lcd_st7789_set_brightness(saved);
        lv_disp_trig_activity(NULL);  /* 重置 LVGL 空闲计时，防止立即再次熄灭 */
        ESP_LOGI(TAG, "Backlight restored for feeding (%d%%)", saved);
    }

    s_feeding_amount = item->amount;
    s_feeding_active = true;
    s_feeding_done = false;

    feeder_motor_dispense(item->amount);
}

/* LVGL 定时器回调：管理投喂弹窗的显示与隐藏（在 LVGL 上下文中执行） */
static void feeding_popup_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    /* === 背光自动熄灭 / 唤醒检测 === */
    uint32_t inactive_ms = lv_disp_get_inactive_time(NULL);

    if (inactive_ms > IDLE_TIMEOUT_MS && !s_backlight_dimmed) {
        /* 超过 5 分钟无操作，熄灭背光 */
        s_backlight_dimmed = true;
        lcd_st7789_set_brightness(0);
        ESP_LOGI(TAG, "Backlight dimmed (idle %lu ms)", (unsigned long)inactive_ms);
    } else if (inactive_ms < 2000 && s_backlight_dimmed) {
        /* 检测到新操作（最近2秒内有触摸），恢复亮度 */
        s_backlight_dimmed = false;
        uint8_t saved = lcd_st7789_get_brightness();
        if (saved == 0) {
            saved = 100;  /* 防止刚开机未设亮度时无法恢复 */
        }
        lcd_st7789_set_brightness(saved);
        ESP_LOGI(TAG, "Backlight restored to %d%%", saved);
    }

    /* 轮询电机状态：完成则标记 done */
    if (s_feeding_active && !s_feeding_done && feeder_motor_is_idle()) {
        s_feeding_done = true;
        ESP_LOGI(TAG, "Feeding done (motor idle)");
    }

    if (s_feeding_active && !s_feeding_done && s_feeding_popup == NULL) {
        /* 弹窗创建在顶层悬浮层上，切屏不会被销毁 */
        s_feeding_popup = lv_obj_create(lv_layer_top());
        lv_obj_set_size(s_feeding_popup, 220, 100);
        lv_obj_center(s_feeding_popup);
        lv_obj_set_style_bg_color(s_feeding_popup, lv_color_hex(0x001a27), 0);
        lv_obj_set_style_bg_opa(s_feeding_popup, 230, 0);
        lv_obj_set_style_border_width(s_feeding_popup, 2, 0);
        lv_obj_set_style_border_color(s_feeding_popup, lv_color_hex(0x00AA00), 0);
        lv_obj_set_style_radius(s_feeding_popup, 10, 0);
        lv_obj_set_style_pad_all(s_feeding_popup, 15, 0);

        /* 标题 */
        lv_obj_t *title = lv_label_create(s_feeding_popup);
        lv_label_set_text(title, LV_SYMBOL_PLAY " Feeding...");
        lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(title, lv_color_hex(0x00FF00), 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);

        /* 详细信息 */
        lv_obj_t *info = lv_label_create(s_feeding_popup);
        char buf[32];
        snprintf(buf, sizeof(buf), "%d slot(s)", s_feeding_amount);
        lv_label_set_text(info, buf);
        lv_obj_set_style_text_font(info, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(info, lv_color_white(), 0);
        lv_obj_center(info);

        ESP_LOGI(TAG, "Feeding popup shown");
    }

    if (s_feeding_done && s_feeding_popup != NULL) {
        /* 投喂完成，关闭弹窗 */
        if (lv_obj_is_valid(s_feeding_popup)) {
            lv_obj_del(s_feeding_popup);
        }
        s_feeding_popup = NULL;
        s_feeding_active = false;
        s_feeding_done = false;
        ESP_LOGI(TAG, "Feeding popup closed");
    }
}

void user_component_init(void)
{
    /* 初始化 NVS 闪存（必须最先调用，WiFi、SNTP、投喂计划都依赖它） */
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition needs erase, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_err);
    ESP_LOGI(TAG, "NVS flash initialized");

    tusb_serial_init();
    vTaskDelay(pdMS_TO_TICKS(1000));
    user_lvgl_init();
    create_ui();

    /* 初始化步进电机 (A4988) */
    feeder_motor_init();

    /* 从 NVS 加载投喂计划 */
    esp_err_t sched_err = feed_schedule_load();
    if (sched_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load feeding schedule: %s", esp_err_to_name(sched_err));
    } else {
        ESP_LOGI(TAG, "Feeding schedule loaded (%d items)", feed_schedule_get_count());
    }

    /* 启动投喂调度器（自动检查时间，触发投喂） */
    feeding_scheduler_start(on_feeding_triggered);

    /* 从 NVS 恢复背光亮度 */
    {
        nvs_handle_t nvs;
        uint8_t brightness = 100;
        if (nvs_open("lcd_config", NVS_READONLY, &nvs) == ESP_OK) {
            uint8_t v = 100;
            if (nvs_get_u8(nvs, "brightness", &v) == ESP_OK) {
                brightness = (v < 30) ? 30 : v;
            }
            nvs_close(nvs);
        }
        lcd_st7789_set_brightness(brightness);
        ESP_LOGI(TAG, "Backlight brightness restored to %d%%", brightness);
    }

    /* 创建 LVGL 定时器用于管理投喂弹窗（每 200ms 检查一次） */
    lv_timer_create(feeding_popup_timer_cb, 200, NULL);

    /* 初始化 WiFi (自动尝试连接上一次保存的网络) */
    wifi_app_init();

    /* 注册 WiFi 连接成功回调（连接后自动同步时间） */
    wifi_app_register_connected_cb(on_wifi_connected);

    /* 如果 WiFi 已连接（从 NVS 恢复），直接初始化 SNTP */
    if (wifi_app_is_connected()) {
        sntp_time_init();
    }
}

void lvgl_event_task(void *arg)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(20);  // 20ms => 50Hz

    for (;;) {
        lv_timer_handler();
        lv_tick_inc(20);

        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

void app_main(void)
{
    user_component_init();

    xTaskCreate(lvgl_event_task, "lvgl_event_task", 4096, NULL, 5, NULL);

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}