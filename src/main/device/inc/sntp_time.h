#ifndef __SNTP_TIME_H
#define __SNTP_TIME_H

#include <stdbool.h>
#include <time.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 SNTP 时间同步
 *        设置 NTP 服务器（阿里云）和时区（北京时间 UTC+8）
 */
void sntp_time_init(void);

/**
 * @brief 获取当前本地时间（北京时间）
 * @param tm 输出时间结构体
 * @return true 时间已同步，false 时间未同步
 */
bool sntp_time_get_local(struct tm *tm);

/**
 * @brief 获取格式化的时间字符串
 * @param buf 输出缓冲区
 * @param len 缓冲区大小
 */
void sntp_time_get_str(char *buf, size_t len);

/**
 * @brief 获取格式化的日期字符串
 * @param buf 输出缓冲区
 * @param len 缓冲区大小
 */
void sntp_time_get_date_str(char *buf, size_t len);

/**
 * @brief 检查时间是否已同步
 * @return true 已同步
 */
bool sntp_time_is_synced(void);

/**
 * @brief 等待时间同步完成（阻塞）
 * @param timeout_ms 超时时间（毫秒）
 * @return true 同步成功，false 超时
 */
bool sntp_time_wait_for_sync(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* __SNTP_TIME_H */