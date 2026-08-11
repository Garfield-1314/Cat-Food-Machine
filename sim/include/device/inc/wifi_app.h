/* 模拟器桩：替代固件 device/inc/wifi_app.h */
#ifndef SIM_WIFI_APP_H
#define SIM_WIFI_APP_H

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*wifi_connected_cb_t)(void);

void wifi_app_init(void);
esp_err_t wifi_app_connect(const char *ssid, const char *password);
void wifi_app_disconnect(void);
bool wifi_app_is_connected(void);
const char *wifi_app_get_ssid(void);
void wifi_app_register_connected_cb(wifi_connected_cb_t cb);
void wifi_app_save_config(const char *ssid, const char *password);

#ifdef __cplusplus
}
#endif

#endif /* SIM_WIFI_APP_H */
