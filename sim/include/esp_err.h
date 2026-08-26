/* 模拟器桩：替代 ESP-IDF 的 esp_err.h */
#ifndef SIM_ESP_ERR_H
#define SIM_ESP_ERR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int esp_err_t;

#define ESP_OK                        0
#define ESP_FAIL                      (-1)

/* 常用错误码（供逻辑判断使用） */
#define ESP_ERR_NVS_NOT_FOUND         0x1101
#define ESP_ERR_NVS_NO_FREE_PAGES     0x1102
#define ESP_ERR_NVS_NEW_VERSION_FOUND 0x1103

static inline const char *esp_err_to_name(esp_err_t code)
{
    (void)code;
    return "sim-ok";
}

#ifdef __cplusplus
}
#endif

#endif /* SIM_ESP_ERR_H */
