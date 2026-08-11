/* 模拟器桩：SNTP 时间（直接使用 PC 本地时间，视为已同步） */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "device/inc/sntp_time.h"

static bool s_synced = false;

void sntp_time_init(void)
{
    s_synced = true;
}

bool sntp_time_is_synced(void)
{
    return s_synced;
}

bool sntp_time_get_local(struct tm *tm)
{
    time_t now = time(NULL);
    struct tm *lt = localtime(&now);
    if (lt) {
        *tm = *lt;
        return true;
    }
    return false;
}

void sntp_time_get_str(char *buf, size_t len)
{
    time_t now = time(NULL);
    struct tm *lt = localtime(&now);
    if (lt) {
        strftime(buf, len, "%H:%M:%S", lt);
    } else {
        snprintf(buf, len, "--:--:--");
    }
}

void sntp_time_get_date_str(char *buf, size_t len)
{
    time_t now = time(NULL);
    struct tm *lt = localtime(&now);
    if (lt) {
        strftime(buf, len, "%Y-%m-%d", lt);
    } else {
        snprintf(buf, len, "----");
    }
}

bool sntp_time_wait_for_sync(uint32_t timeout_ms)
{
    (void)timeout_ms;
    return true;
}
