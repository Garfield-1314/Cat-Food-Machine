#include "device/inc/sntp_time.h"

#include <string.h>
#include <sys/time.h>
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "sntp_time";

/* NTP 服务器地址（国内较快的服务器）*/
#define NTP_SERVER1 "ntp.aliyun.com"
#define NTP_SERVER2 "ntp1.aliyun.com"
#define NTP_SERVER3 "pool.ntp.org"

/* 时区设置为北京时间 (UTC+8) */
#define TIMEZONE "CST-8"

/* 24 小时重新同步 */
#define RESYNC_INTERVAL_SEC  (24 * 60 * 60)

static bool s_time_synced = false;

/* 24h 重新同步的定时器 */
static esp_timer_handle_t s_resync_timer = NULL;

/* 前向声明 */
static void time_sync_cb(struct timeval *tv);

/* 重新同步任务 */
static void resync_timer_cb(void *arg)
{
    ESP_LOGI(TAG, "24h elapsed, re-synchronizing time...");

    /* 重置同步状态，准备重新同步 */
    s_time_synced = false;

    /* 停止旧的 SNTP（如果还在运行）并重新初始化 */
    esp_sntp_stop();

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, NTP_SERVER1);
    esp_sntp_setservername(1, NTP_SERVER2);
    esp_sntp_setservername(2, NTP_SERVER3);
    esp_sntp_set_time_sync_notification_cb(time_sync_cb);
    sntp_set_sync_mode(SNTP_SYNC_MODE_SMOOTH);

    esp_sntp_init();
}

/* 启动 24 小时重新同步定时器 */
static void start_resync_timer(void)
{
    if (s_resync_timer != NULL) {
        return;
    }

    esp_timer_create_args_t timer_args = {
        .callback = &resync_timer_cb,
        .arg = NULL,
        .name = "sntp_resync",
        .dispatch_method = ESP_TIMER_TASK,
    };

    esp_err_t ret = esp_timer_create(&timer_args, &s_resync_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create resync timer: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_timer_start_once(s_resync_timer, RESYNC_INTERVAL_SEC * 1000000ULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start resync timer: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "24h resync timer started");
    }
}

/* 时间同步完成回调 */
static void time_sync_cb(struct timeval *tv)
{
    s_time_synced = true;
    ESP_LOGI(TAG, "Time synchronized successfully");

    /* 设置时区为北京时间 */
    setenv("TZ", TIMEZONE, 1);
    tzset();

    /* 打印当前时间 */
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    char strftime_buf[64];
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "Current Beijing time: %s", strftime_buf);

    /* 启动 24h 定时器，下次重新同步 */
    start_resync_timer();
}

void sntp_time_init(void)
{
    /* 检查时间是否已经设置 */
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    if (timeinfo.tm_year >= (2024 - 1900)) {
        /* 时间已经设置过（可能是从 RTC 恢复） */
        s_time_synced = true;
        ESP_LOGI(TAG, "Time already set");

        /* 仍然启动 24h 重新同步定时器 */
        start_resync_timer();
        return;
    }

    ESP_LOGI(TAG, "Initializing SNTP...");

    /* 配置 SNTP - 使用经典 API */
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, NTP_SERVER1);
    esp_sntp_setservername(1, NTP_SERVER2);
    esp_sntp_setservername(2, NTP_SERVER3);
    esp_sntp_set_time_sync_notification_cb(time_sync_cb);
    sntp_set_sync_mode(SNTP_SYNC_MODE_SMOOTH);

    esp_sntp_init();

    ESP_LOGI(TAG, "SNTP initialized, waiting for time sync...");
}

bool sntp_time_get_local(struct tm *tm)
{
    if (!s_time_synced) {
        return false;
    }

    time_t now;
    time(&now);
    localtime_r(&now, tm);
    return true;
}

void sntp_time_get_str(char *buf, size_t len)
{
    struct tm timeinfo;
    if (!sntp_time_get_local(&timeinfo)) {
        snprintf(buf, len, "--:--:--");
        return;
    }
    strftime(buf, len, "%H:%M:%S", &timeinfo);
}

void sntp_time_get_date_str(char *buf, size_t len)
{
    struct tm timeinfo;
    if (!sntp_time_get_local(&timeinfo)) {
        snprintf(buf, len, "----/--/--");
        return;
    }
    strftime(buf, len, "%Y/%m/%d", &timeinfo);
}

bool sntp_time_is_synced(void)
{
    return s_time_synced;
}

bool sntp_time_wait_for_sync(uint32_t timeout_ms)
{
    if (s_time_synced) {
        return true;
    }

    int retry = 0;
    const int retry_count = timeout_ms / 1000;
    while (retry < retry_count && !s_time_synced) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        retry++;
    }

    return s_time_synced;
}