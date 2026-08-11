/* 模拟器桩：替代 ESP-IDF 的 esp_mac.h */
#ifndef SIM_ESP_MAC_H
#define SIM_ESP_MAC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ESP_MAC_WIFI_STA 0

void esp_read_mac(uint8_t *mac, int type);

#ifdef __cplusplus
}
#endif

#endif /* SIM_ESP_MAC_H */
