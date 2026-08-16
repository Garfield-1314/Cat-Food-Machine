/* 模拟器桩：替代固件 device/inc/wifi_app.h */
#ifndef SIM_WIFI_APP_H
#define SIM_WIFI_APP_H

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*wifi_connected_cb_t)(void);

/* 与固件一致的最小枚举（用于 wifi_ap_info_t.authmode） */
typedef enum {
    WIFI_AUTH_OPEN = 0,
    WIFI_AUTH_WPA_PSK = 1,
    WIFI_AUTH_WPA2_PSK = 2,
    WIFI_AUTH_WPA3_PSK = 3,
} wifi_auth_mode_t;

/* 扫描到的 AP 信息 */
typedef struct {
    char ssid[33];
    int8_t rssi;
    uint8_t authmode;
} wifi_ap_info_t;

void wifi_app_init(void);
esp_err_t wifi_app_connect(const char *ssid, const char *password);
void wifi_app_disconnect(void);
bool wifi_app_is_connected(void);
bool wifi_app_is_connecting(void);
const char *wifi_app_get_ssid(void);
void wifi_app_register_connected_cb(wifi_connected_cb_t cb);
void wifi_app_save_config(const char *ssid, const char *password);
esp_err_t wifi_app_scan(wifi_ap_info_t *results, uint16_t *count, uint16_t max_count);

#ifdef __cplusplus
}
#endif

#endif /* SIM_WIFI_APP_H */
