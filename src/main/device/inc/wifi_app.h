#ifndef __WIFI_APP_H
#define __WIFI_APP_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* WiFi 连接状态回调 */
typedef void (*wifi_connected_cb_t)(void);

/* 扫描到的 AP 信息 */
typedef struct {
    char ssid[33];   /* null 结尾的 SSID */
    int8_t rssi;     /* 信号强度 (dBm) */
    uint8_t authmode; /* wifi_auth_mode_t 取值 */
} wifi_ap_info_t;

/**
 * @brief 初始化 WiFi Station 模式
 *        从 NVS 读取保存的 SSID/密码，自动尝试连接
 */
void wifi_app_init(void);

/**
 * @brief 使用指定的 SSID 和密码连接 WiFi
 * @param ssid  WiFi SSID
 * @param password WiFi 密码
 * @return ESP_OK 成功，否则失败
 */
esp_err_t wifi_app_connect(const char *ssid, const char *password);

/**
 * @brief 断开 WiFi 连接
 */
void wifi_app_disconnect(void);

/**
 * @brief 扫描附近的 WiFi 热点
 * @param results 输出 AP 数组（由调用者提供）
 * @param count   输入为数组容量，输出为实际扫描到的 AP 数
 * @param max_count 最多返回的 AP 数量
 * @return ESP_OK 成功，否则失败
 */
esp_err_t wifi_app_scan(wifi_ap_info_t *results, uint16_t *count, uint16_t max_count);

/**
 * @brief 获取当前 WiFi 连接状态
 * @return true 已连接，false 未连接
 */
bool wifi_app_is_connected(void);

/**
 * @brief 获取当前连接的 SSID
 * @return SSID 字符串，未连接返回 NULL
 */
const char *wifi_app_get_ssid(void);

/**
 * @brief 注册 WiFi 连接成功回调
 * @param cb 回调函数
 */
void wifi_app_register_connected_cb(wifi_connected_cb_t cb);

/**
 * @brief 保存 WiFi 配置到 NVS
 * @param ssid  SSID
 * @param password 密码
 */
void wifi_app_save_config(const char *ssid, const char *password);

/**
 * @brief 从 NVS 读取 WiFi 配置
 * @param ssid  输出 SSID 缓冲区 (至少 32 字节)
 * @param password 输出密码缓冲区 (至少 64 字节)
 * @return true 读取成功，false 无保存的配置
 */
bool wifi_app_load_config(char *ssid, char *password);

#ifdef __cplusplus
}
#endif

#endif /* __WIFI_APP_H */