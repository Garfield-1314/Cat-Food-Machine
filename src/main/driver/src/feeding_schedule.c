#include "driver/inc/feeding_schedule.h"

#include <string.h>
#include <time.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "feeding_schedule";

/* ========== NVS 配置 ========== */
#define NVS_NAMESPACE   "feed_sched"
#define KEY_COUNT       "count"
#define KEY_ITEM_FMT    "item_%d"
#define KEY_OBSOLETE    "config"  /* 旧版"每周期次数+间隔"配置键，保存时清理 */

/* 旧版 24h 计划项大小 (hour/minute/amount/enabled)，用于 NVS 自动迁移 */
#define LEGACY_ITEM_SIZE 4

/* ========== 内部状态 ========== */
static feed_schedule_item_t s_items[MAX_SCHEDULE_ITEMS];
static int s_count = 0;

static TaskHandle_t s_scheduler_task = NULL;
static feeding_callback_t s_callback = NULL;

/* 记录每个投喂项上次触发的时间 (unix timestamp)，防止1分钟内重复触发 */
static time_t s_last_triggered[MAX_SCHEDULE_ITEMS];

/* 互斥锁保护数据访问 */
static SemaphoreHandle_t s_mutex = NULL;

/* ========== 内部辅助 ========== */

static void lock(void)
{
    if (s_mutex) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
    }
}

static void unlock(void)
{
    if (s_mutex) {
        xSemaphoreGive(s_mutex);
    }
}

/* 校验并修正计划项参数 */
static void sanitize_item(feed_schedule_item_t *item)
{
    if (item->hour > 23) item->hour = 0;
    if (item->minute > 59) item->minute = 0;
    if (item->amount < 1) item->amount = 1;
    if (item->amount > 6) item->amount = 6;
    if (item->every_days < 1) item->every_days = 1;
    if (item->every_days > MAX_EVERY_DAYS) item->every_days = MAX_EVERY_DAYS;
}

/* 清理旧版"每周期次数+间隔"配置键 */
static void erase_obsolete_keys(nvs_handle_t nvs)
{
    nvs_erase_key(nvs, KEY_OBSOLETE);
}

/* ========== NVS 读写 ========== */

esp_err_t feed_schedule_load(void)
{
    nvs_handle_t nvs;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "NVS namespace '%s' not found, using defaults", NVS_NAMESPACE);
            s_count = 0;
            return ESP_OK;
        }
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }

    lock();

    int32_t count = 0;
    err = nvs_get_i32(nvs, KEY_COUNT, &count);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No schedule count found, using defaults");
        s_count = 0;
        nvs_close(nvs);
        unlock();
        return ESP_OK;
    }

    if (count < 0) count = 0;
    if (count > MAX_SCHEDULE_ITEMS) count = MAX_SCHEDULE_ITEMS;

    bool migrated = false;

    for (int i = 0; i < count; i++) {
        char key[16];
        snprintf(key, sizeof(key), KEY_ITEM_FMT, i);

        size_t len = sizeof(feed_schedule_item_t);
        err = nvs_get_blob(nvs, key, &s_items[i], &len);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to read item %d, stop loading", i);
            count = i;  /* 只加载已成功读取的 */
            break;
        }

        if (len == LEGACY_ITEM_SIZE) {
            /* 旧版 24h 格式 (hour/minute/amount/enabled)，默认每天投喂 */
            s_items[i].every_days = 1;
            migrated = true;
            ESP_LOGI(TAG, "Item %d: legacy 24h format, every_days set to 1 (daily)", i);
        }

        sanitize_item(&s_items[i]);
    }

    s_count = count;
    nvs_close(nvs);
    unlock();

    ESP_LOGI(TAG, "Loaded %d schedule items from NVS", s_count);

    if (migrated) {
        ESP_LOGI(TAG, "Legacy item format detected, upgrading schedule data...");
        return feed_schedule_save();
    }

    return ESP_OK;
}

esp_err_t feed_schedule_save(void)
{
    nvs_handle_t nvs;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for write: %s", esp_err_to_name(err));
        return err;
    }

    lock();

    int32_t count = s_count;
    err = nvs_set_i32(nvs, KEY_COUNT, count);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write count: %s", esp_err_to_name(err));
        nvs_close(nvs);
        unlock();
        return err;
    }

    for (int i = 0; i < count; i++) {
        char key[16];
        snprintf(key, sizeof(key), KEY_ITEM_FMT, i);

        err = nvs_set_blob(nvs, key, &s_items[i], sizeof(feed_schedule_item_t));
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to write item %d: %s", i, esp_err_to_name(err));
            nvs_close(nvs);
            unlock();
            return err;
        }
    }

    erase_obsolete_keys(nvs);  /* 清理旧版配置键，忽略错误 */

    err = nvs_commit(nvs);
    nvs_close(nvs);
    unlock();

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Saved %d schedule items to NVS", count);
    } else {
        ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(err));
    }

    return err;
}

/* ========== 数据访问接口 ========== */

int feed_schedule_get_count(void)
{
    int c;
    lock();
    c = s_count;
    unlock();
    return c;
}

const feed_schedule_item_t *feed_schedule_get_item(int index)
{
    const feed_schedule_item_t *item = NULL;
    lock();
    if (index >= 0 && index < s_count) {
        item = &s_items[index];
    }
    unlock();
    return item;
}

bool feed_schedule_get_next_time(time_t *next_time)
{
    if (next_time == NULL) {
        return false;
    }

    time_t now;
    time(&now);

    struct tm now_tm;
    localtime_r(&now, &now_tm);
    if (now_tm.tm_year < (2024 - 1900)) {
        return false;
    }

    /* 从今天零点开始逐日查找，最多检查一个完整的轮换周期。 */
    struct tm midnight_tm = now_tm;
    midnight_tm.tm_hour = 0;
    midnight_tm.tm_min = 0;
    midnight_tm.tm_sec = 0;
    midnight_tm.tm_isdst = -1;
    time_t midnight = mktime(&midnight_tm);
    if (midnight == (time_t)-1) {
        return false;
    }

    time_t nearest = (time_t)-1;
    lock();

    for (int i = 0; i < s_count; i++) {
        const feed_schedule_item_t *item = &s_items[i];
        if (!item->enabled) {
            continue;
        }
        uint8_t every_days = item->every_days ? item->every_days : 1;

        for (int day_offset = 0; day_offset <= MAX_EVERY_DAYS; day_offset++) {
            struct tm candidate_midnight_tm = midnight_tm;
            candidate_midnight_tm.tm_mday += day_offset;
            candidate_midnight_tm.tm_isdst = -1;

            time_t candidate_midnight = mktime(&candidate_midnight_tm);
            if (candidate_midnight == (time_t)-1 ||
                (candidate_midnight / 86400) % every_days != 0) {
                continue;
            }

            struct tm candidate_tm;
            localtime_r(&candidate_midnight, &candidate_tm);
            candidate_tm.tm_hour = item->hour;
            candidate_tm.tm_min = item->minute;
            candidate_tm.tm_sec = 0;
            candidate_tm.tm_isdst = -1;

            time_t candidate = mktime(&candidate_tm);
            if (candidate == (time_t)-1 || candidate < now) {
                continue;
            }

            if (nearest == (time_t)-1 || candidate < nearest) {
                nearest = candidate;
            }
            break;
        }
    }

    unlock();

    if (nearest == (time_t)-1) {
        return false;
    }

    *next_time = nearest;
    return true;
}

esp_err_t feed_schedule_set_item(int index, const feed_schedule_item_t *item)
{
    if (item == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    lock();
    if (index < 0 || index >= s_count) {
        unlock();
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(&s_items[index], item, sizeof(feed_schedule_item_t));
    sanitize_item(&s_items[index]);
    unlock();

    ESP_LOGI(TAG, "Set item %d: %02d:%02d every %dd amount=%d enabled=%d",
             index, s_items[index].hour, s_items[index].minute,
             s_items[index].every_days, s_items[index].amount, s_items[index].enabled);
    return ESP_OK;
}

esp_err_t feed_schedule_add_item(const feed_schedule_item_t *item)
{
    if (item == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    lock();
    if (s_count >= MAX_SCHEDULE_ITEMS) {
        unlock();
        ESP_LOGW(TAG, "Cannot add more items, max=%d", MAX_SCHEDULE_ITEMS);
        return ESP_ERR_NO_MEM;
    }
    memcpy(&s_items[s_count], item, sizeof(feed_schedule_item_t));
    sanitize_item(&s_items[s_count]);
    s_last_triggered[s_count] = 0;  /* 重置触发记录 */
    s_count++;
    unlock();

    ESP_LOGI(TAG, "Added item %d: %02d:%02d every %dd amount=%d enabled=%d",
             s_count - 1, s_items[s_count - 1].hour, s_items[s_count - 1].minute,
             s_items[s_count - 1].every_days, s_items[s_count - 1].amount,
             s_items[s_count - 1].enabled);
    return ESP_OK;
}

esp_err_t feed_schedule_remove_item(int index)
{
    lock();
    if (index < 0 || index >= s_count) {
        unlock();
        return ESP_ERR_INVALID_ARG;
    }

    /* 将后续元素向前移动 */
    for (int i = index; i < s_count - 1; i++) {
        memcpy(&s_items[i], &s_items[i + 1], sizeof(feed_schedule_item_t));
        s_last_triggered[i] = s_last_triggered[i + 1];
    }
    s_count--;
    unlock();

    ESP_LOGI(TAG, "Removed item at index %d, count now=%d", index, s_count);
    return ESP_OK;
}

/* ========== 天数轮换与调度器 ========== */

/* 本地自然日到 epoch 的天数（北京时间），用于"每隔几天"的天数轮换 */
static int get_local_day_index(void)
{
    time_t now;
    struct tm tm;

    time(&now);
    localtime_r(&now, &tm);

    tm.tm_hour = 0;
    tm.tm_min = 0;
    tm.tm_sec = 0;
    tm.tm_isdst = -1;

    time_t local_midnight = mktime(&tm);
    if (local_midnight == (time_t)-1) {
        return 0;
    }
    return (int)(local_midnight / 86400);
}

/* 时间是否已同步（年份合理），防止开机未同步时误投喂 */
static bool time_valid(void)
{
    time_t now;
    struct tm tm;

    time(&now);
    localtime_r(&now, &tm);
    return (tm.tm_year >= (2024 - 1900));
}

static void scheduler_task_func(void *arg)
{
    ESP_LOGI(TAG, "Scheduler task started");

    while (1) {
        /* 每秒检查一次 */
        vTaskDelay(pdMS_TO_TICKS(1000));

        if (s_callback == NULL) {
            continue;
        }

        /* 时间未同步前不调度，防止开机误投喂 */
        if (!time_valid()) {
            continue;
        }

        /* 获取当前时间 */
        time_t now;
        struct tm tm;
        time(&now);
        localtime_r(&now, &tm);

        int current_hour = tm.tm_hour;
        int current_min = tm.tm_min;
        int day_index = get_local_day_index();

        lock();

        for (int i = 0; i < s_count; i++) {
            if (!s_items[i].enabled) {
                continue;
            }

            /* 时间匹配，且当天属于该计划项的天数轮换 */
            if (s_items[i].hour == current_hour &&
                s_items[i].minute == current_min &&
                (day_index % s_items[i].every_days) == 0) {
                /* 检查是否在1分钟内重复触发 */
                if (now - s_last_triggered[i] >= 60) {
                    s_last_triggered[i] = now;
                    ESP_LOGI(TAG, "Triggering feed item %d: %02d:%02d every %dd amount=%d",
                             i, s_items[i].hour, s_items[i].minute,
                             s_items[i].every_days, s_items[i].amount);

                    /* 解锁后再回调，避免死锁 */
                    unlock();
                    s_callback(s_items[i].amount);
                    lock();
                }
            }
        }

        unlock();
    }
}

void feeding_scheduler_start(feeding_callback_t cb)
{
    if (cb == NULL) {
        ESP_LOGE(TAG, "Callback cannot be NULL");
        return;
    }

    s_callback = cb;

    if (s_scheduler_task != NULL) {
        ESP_LOGW(TAG, "Scheduler already running");
        return;
    }

    /* 初始化互斥锁 */
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
    }

    /* 重置触发记录 */
    memset(s_last_triggered, 0, sizeof(s_last_triggered));

    xTaskCreate(scheduler_task_func, "feed_sched", 3072, NULL, 3, &s_scheduler_task);
    ESP_LOGI(TAG, "Scheduler started");
}

void feeding_scheduler_stop(void)
{
    if (s_scheduler_task != NULL) {
        vTaskDelete(s_scheduler_task);
        s_scheduler_task = NULL;
        ESP_LOGI(TAG, "Scheduler stopped");
    }
}
