/* 模拟器桩：WiFi 功能（内存模拟，始终返回已连接/可连接） */
#include <string.h>

#include "device/inc/wifi_app.h"

static bool s_connected = false;
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
    if (ssid) {
        strncpy(s_ssid, ssid, 32);
        s_ssid[32] = '\0';
    }
    if (password) {
        strncpy(s_pass, password, 64);
        s_pass[64] = '\0';
    }
    s_connected = true;
    return ESP_OK;
}

void wifi_app_disconnect(void)
{
    s_connected = false;
}

bool wifi_app_is_connected(void)
{
    return s_connected;
}

const char *wifi_app_get_ssid(void)
{
    return s_ssid;
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
