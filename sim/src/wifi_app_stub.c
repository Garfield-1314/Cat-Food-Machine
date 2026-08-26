/* 模拟器桩：WiFi 功能（内存模拟，始终返回已连接/可连接） */
#include <string.h>

#include "device/inc/wifi_app.h"

static bool s_connected = false;
static bool s_connecting = false;
static char s_ssid[33] = "SIM-WiFi";
static char s_pass[65] = "";
static wifi_connected_cb_t s_cb = NULL;

void wifi_app_init(void)
{
    /* 模拟器默认预置为已连接，便于 app_page 展示效果 */
    s_connected = true;
}

esp_err_t wifi_app_connect(const char *ssid, const char *password)
{
    s_connecting = true;
    if (ssid) {
        strncpy(s_ssid, ssid, 32);
        s_ssid[32] = '\0';
    }
    if (password) {
        strncpy(s_pass, password, 64);
        s_pass[64] = '\0';
    }
    s_connected = true;
    s_connecting = false;
    return ESP_OK;
}

void wifi_app_disconnect(void)
{
    s_connected = false;
    s_connecting = false;
}

bool wifi_app_is_connected(void)
{
    return s_connected;
}

bool wifi_app_is_connecting(void)
{
    return s_connecting;
}

const char *wifi_app_get_ssid(void)
{
    return s_ssid;
}

const char *wifi_app_get_ip(void)
{
    return s_connected ? "192.168.1.100" : NULL;
}

void wifi_app_register_connected_cb(wifi_connected_cb_t cb)
{
    s_cb = cb;
    if (s_connected && s_cb) {
        s_cb();
    }
}

void wifi_app_save_config(const char *ssid, const char *password)
{
    if (ssid) {
        strncpy(s_ssid, ssid, 32);
        s_ssid[32] = '\0';
    }
    if (password) {
        strncpy(s_pass, password, 64);
        s_pass[64] = '\0';
    }
}

esp_err_t wifi_app_scan(wifi_ap_info_t *results, uint16_t *count, uint16_t max_count)
{
    static const wifi_ap_info_t fake_aps[] = {
        {"SIM-Home",     -40, WIFI_AUTH_WPA2_PSK},
        {"SIM-Open",     -52, WIFI_AUTH_OPEN},
        {"SIM-Net",      -65, WIFI_AUTH_WPA_PSK},
        {"Garage IoT",   -72, WIFI_AUTH_WPA3_PSK},
        {"SIM-WiFi",     -80, WIFI_AUTH_OPEN},
    };

    uint16_t n = sizeof(fake_aps) / sizeof(fake_aps[0]);
    if (n > max_count) n = max_count;
    for (uint16_t i = 0; i < n; i++) {
        results[i] = fake_aps[i];
    }
    *count = n;
    return ESP_OK;
}
