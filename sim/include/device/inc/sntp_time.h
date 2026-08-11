/* 模拟器桩：替代固件 device/inc/sntp_time.h */
#ifndef SIM_SNTP_TIME_H
#define SIM_SNTP_TIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

void sntp_time_init(void);
bool sntp_time_get_local(struct tm *tm);
void sntp_time_get_str(char *buf, size_t len);
void sntp_time_get_date_str(char *buf, size_t len);
bool sntp_time_is_synced(void);
bool sntp_time_wait_for_sync(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* SIM_SNTP_TIME_H */
