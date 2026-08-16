/* 模拟器桩：替代 ESP-IDF 的 esp_log.h，将日志输出到标准输出/错误 */
#ifndef SIM_ESP_LOG_H
#define SIM_ESP_LOG_H

#include <stdarg.h>
#include <stdio.h>

static inline void sim_log(const char *level, const char *tag, const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "[%s] %s: ", level, tag);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}

#define ESP_LOGE(tag, ...) sim_log("E", tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) sim_log("W", tag, __VA_ARGS__)
#define ESP_LOGI(tag, ...) sim_log("I", tag, __VA_ARGS__)
#define ESP_LOGD(tag, ...) sim_log("D", tag, __VA_ARGS__)
#define ESP_LOGV(tag, ...) sim_log("V", tag, __VA_ARGS__)

#endif /* SIM_ESP_LOG_H */
