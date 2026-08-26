/* 模拟器桩：替代 ESP-IDF 的 esp_chip_info.h */
#ifndef SIM_ESP_CHIP_INFO_H
#define SIM_ESP_CHIP_INFO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CHIP_ESP32   = 1,
    CHIP_ESP32S2 = 2,
    CHIP_ESP32S3 = 9,
    CHIP_ESP32C3 = 5,
    CHIP_ESP32C6 = 13,
    CHIP_ESP32H2 = 16,
} esp_chip_model_t;

typedef struct {
    esp_chip_model_t model;
    uint32_t features;
    uint16_t cores;
    uint8_t  revision;
    const char *rom_version;
} esp_chip_info_t;

void esp_chip_info(esp_chip_info_t *out_info);

#ifdef __cplusplus
}
#endif

#endif /* SIM_ESP_CHIP_INFO_H */
