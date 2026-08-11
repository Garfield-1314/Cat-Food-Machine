/* 模拟器桩：替代 ESP-IDF 的 nvs_flash.h */
#ifndef SIM_NVS_FLASH_H
#define SIM_NVS_FLASH_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t nvs_flash_init(void);
esp_err_t nvs_flash_erase(void);

#ifdef __cplusplus
}
#endif

#endif /* SIM_NVS_FLASH_H */
