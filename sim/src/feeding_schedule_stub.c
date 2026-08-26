/* 模拟器桩：投喂计划（内存数组，预置两条演示计划便于调试） */
#include <string.h>
#include <time.h>

#include "driver/inc/feeding_schedule.h"

static feed_schedule_item_t s_items[MAX_SCHEDULE_ITEMS];
static int s_count = 0;

/* 预置演示数据：仅在首次访问时填充 */
static void seed(void)
{
    if (s_count != 0) {
        return;
    }
    s_items[0] = (feed_schedule_item_t){ 8, 0, 2, true, 1 };   /* 每天 08:00 喂 2 仓 */
    s_items[1] = (feed_schedule_item_t){ 18, 30, 1, true, 2 }; /* 隔 1 天 18:30 喂 1 仓 */
    s_count = 2;
}

int feed_schedule_get_count(void)
{
    seed();
    return s_count;
}

const feed_schedule_item_t *feed_schedule_get_item(int index)
{
    seed();
    if (index < 0 || index >= s_count) {
        return NULL;
    }
    return &s_items[index];
}

bool feed_schedule_get_next_time(time_t *next_time)
{
    if (next_time == NULL) {
        return false;
    }

    seed();

    time_t now = time(NULL);
    struct tm now_tm;
    struct tm *local_tm = localtime(&now);
    if (local_tm == NULL) {
        return false;
    }
    now_tm = *local_tm;

    struct tm midnight_tm = now_tm;
    midnight_tm.tm_hour = 0;
    midnight_tm.tm_min = 0;
    midnight_tm.tm_sec = 0;
    midnight_tm.tm_isdst = -1;

    time_t nearest = (time_t)-1;
    for (int i = 0; i < s_count; i++) {
        if (!s_items[i].enabled) {
            continue;
        }
        uint8_t every_days = s_items[i].every_days ? s_items[i].every_days : 1;

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
            local_tm = localtime(&candidate_midnight);
            if (local_tm == NULL) {
                continue;
            }
            candidate_tm = *local_tm;
            candidate_tm.tm_hour = s_items[i].hour;
            candidate_tm.tm_min = s_items[i].minute;
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

    if (nearest == (time_t)-1) {
        return false;
    }

    *next_time = nearest;
    return true;
}

esp_err_t feed_schedule_set_item(int index, const feed_schedule_item_t *item)
{
    seed();
    if (index < 0 || index >= s_count || item == NULL) {
        return ESP_FAIL;
    }
    s_items[index] = *item;
    return ESP_OK;
}

esp_err_t feed_schedule_add_item(const feed_schedule_item_t *item)
{
    seed();
    if (item == NULL || s_count >= MAX_SCHEDULE_ITEMS) {
        return ESP_FAIL;
    }
    s_items[s_count++] = *item;
    return ESP_OK;
}

esp_err_t feed_schedule_remove_item(int index)
{
    seed();
    if (index < 0 || index >= s_count) {
        return ESP_FAIL;
    }
    for (int i = index; i < s_count - 1; i++) {
        s_items[i] = s_items[i + 1];
    }
    s_count--;
    return ESP_OK;
}

esp_err_t feed_schedule_save(void)
{
    return ESP_OK;
}

esp_err_t feed_schedule_load(void)
{
    return ESP_OK;
}
