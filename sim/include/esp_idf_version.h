/* 模拟器桩：替代 ESP-IDF 的 esp_idf_version.h */
#ifndef SIM_ESP_IDF_VERSION_H
#define SIM_ESP_IDF_VERSION_H

#ifdef __cplusplus
extern "C" {
#endif

const char *esp_get_idf_version(void);

#ifdef __cplusplus
}
#endif

#endif /* SIM_ESP_IDF_VERSION_H */
