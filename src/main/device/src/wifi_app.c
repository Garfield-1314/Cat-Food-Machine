#include "device/inc/wifi_app.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "lwip/err.h"
#include "lwip/sys.h"

static const char *TAG = "wifi_app";

/* NVS key for WiFi config */
#define NVS_NAMESPACE  "wifi_config"
#define NVS_KEY_SSID   "wifi_ssid"
#define NVS_KEY_PASS   "wifi_pass"

/* Max retry count */
#define WIFI_MAX_RETRY     5

/* 10 分钟周期性重试定时器（毫秒） */
#define RETRY_TIMER_PERIOD_MS  (10 * 60 * 1000)

/* Static variables */
static int s_retry_num = 0;
static bool s_is_connected = false;
static bool s_connecting = false;  /* 正在连接中 */
static char s_current_ssid[32] = {0};
static wifi_connected_cb_t s_connected_cb = NULL;
static esp_timer_handle_t s_retry_timer = NULL;

/* 10 分钟重试定时器回调 */
static void retry_timer_cb(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Periodic retry (10 min interval)...");
    s_retry_num = 0;
    s_connecting = true;
    esp_wifi_connect();
}

static void stop_retry_timer(void)
{
    if (s_retry_timer != NULL) {
        esp_timer_stop(s_retry_timer);
        esp_timer_delete(s_retry_timer);
        s_retry_timer = NULL;
    }
}

static void start_retry_timer(void)
{
    if (s_retry_timer != NULL) {
        return;  /* 已有定时器在运行 */
    }
    esp_timer_create_args_t timer_args = {
        .callback = &retry_timer_cb,
        .arg = NULL,
        .name = "wifi_retry",
        .dispatch_method = ESP_TIMER_TASK,
    };
    if (esp_timer_create(&timer_args, &s_retry_timer) == ESP_OK) {
        esp_timer_start_once(s_retry_timer, RETRY_TIMER_PERIOD_MS * 1000ULL);
        ESP_LOGI(TAG, "10 min retry timer started");
    }
}

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_is_connected = false;
        s_connecting = false;
        if (s_retry_num < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            s_connecting = true;
            ESP_LOGI(TAG, "retry to connect to the AP (%d/%d)", s_retry_num, WIFI_MAX_RETRY);
        } else {
            ESP_LOGI(TAG, "connect to the AP fail after %d retries, will retry every 10 min", WIFI_MAX_RETRY);
            start_retry_timer();
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(TAG, "connected to AP");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        s_is_connected = true;
        s_connecting = false;
        stop_retry_timer();

        /* 触发连接成功的回调 */
        if (s_connected_cb) {
            s_connected_cb();
        }
    }
}

void wifi_app_init(void)
{
    /* 初始化 NVS (如果尚未初始化) */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    /* 尝试从 NVS 读取已保存的 WiFi 配置，在 start 之前设置 */
    char saved_ssid[32] = {0};
    char saved_pass[64] = {0};
    bool has_saved_config = wifi_app_load_config(saved_ssid, saved_pass);

    if (has_saved_config) {
        /* 在 start 之前设置好配置，start 后不会触发 WIFI_EVENT_STA_START 自动连接 */
        wifi_config_t wifi_config = {
            .sta = {
                .threshold.authmode = WIFI_AUTH_WPA2_PSK,
                .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
            },
        };
        strncpy((char *)wifi_config.sta.ssid, saved_ssid, sizeof(wifi_config.sta.ssid) - 1);
        strncpy((char *)wifi_config.sta.password, saved_pass, sizeof(wifi_config.sta.password) - 1);
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

        strncpy(s_current_ssid, saved_ssid, sizeof(s_current_ssid) - 1);
        s_current_ssid[sizeof(s_current_ssid) - 1] = '\0';
    }

    /* 启动 WiFi */
    ESP_ERROR_CHECK(esp_wifi_start());

    if (has_saved_config) {
        /* 配置已在 start 之前设置，这里主动发起连接 */
        esp_err_t ret = esp_wifi_connect();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to auto-connect: %s", esp_err_to_name(ret));
        } else {
            s_retry_num = 0;
            s_connecting = true;
            ESP_LOGI(TAG, "Found saved WiFi config, auto-connecting to SSID: %s", saved_ssid);
        }
    } else {
        ESP_LOGI(TAG, "No saved WiFi config, waiting for user to configure");
    }
}

esp_err_t wifi_app_connect(const char *ssid, const char *password)
{
    if (ssid == NULL || strlen(ssid) == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    /* 停止后台重试定时器（用户主动连接时） */
    stop_retry_timer();

    /* 如果已经连接同一个 SSID，不需要重复连接 */
    if (s_is_connected && strcmp(s_current_ssid, ssid) == 0) {
        ESP_LOGI(TAG, "Already connected to SSID: %s, skipping", ssid);
        return ESP_OK;
    }

    /* 如果正在连接同一个 SSID，不需要重复 */
    if (s_connecting && strcmp(s_current_ssid, ssid) == 0) {
        ESP_LOGI(TAG, "Already connecting to SSID: %s, skipping", ssid);
        return ESP_OK;
    }

    /* 如果已经连接到别的 WiFi，需要先断开 */
    if (s_is_connected || s_connecting) {
        ESP_LOGI(TAG, "Disconnecting from current AP before connecting to new one");
        esp_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(200));
        s_is_connected = false;
        s_connecting = false;
    }

    /* 保存当前 SSID */
    strncpy(s_current_ssid, ssid, sizeof(s_current_ssid) - 1);
    s_current_ssid[sizeof(s_current_ssid) - 1] = '\0';
    s_retry_num = 0;

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        },
    };
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    if (password) {
        strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    }

    esp_err_t ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set WiFi config: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect: %s", esp_err_to_name(ret));
        return ret;
    }
    s_connecting = true;

    ESP_LOGI(TAG, "Connecting to SSID: %s", ssid);

    return ESP_OK;
}

void wifi_app_disconnect(void)
{
    stop_retry_timer();
    esp_wifi_disconnect();
    s_is_connected = false;
    memset(s_current_ssid, 0, sizeof(s_current_ssid));
}

bool wifi_app_is_connected(void)
{
    return s_is_connected;
}

const char *wifi_app_get_ssid(void)
{
    return s_is_connected ? s_current_ssid : NULL;
}

void wifi_app_register_connected_cb(wifi_connected_cb_t cb)
{
    s_connected_cb = cb;
}

void wifi_app_save_config(const char *ssid, const char *password)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
        return;
    }

    err = nvs_set_str(nvs_handle, NVS_KEY_SSID, ssid);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save SSID: %s", esp_err_to_name(err));
    }

    err = nvs_set_str(nvs_handle, NVS_KEY_PASS, password);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save password: %s", esp_err_to_name(err));
    }

    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(err));
    }

    nvs_close(nvs_handle);
    ESP_LOGI(TAG, "WiFi config saved to NVS");
}

bool wifi_app_load_config(char *ssid, char *password)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return false;
    }

    size_t ssid_len = 32;
    size_t pass_len = 64;
    err = nvs_get_str(nvs_handle, NVS_KEY_SSID, ssid, &ssid_len);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return false;
    }

    err = nvs_get_str(nvs_handle, NVS_KEY_PASS, password, &pass_len);
    nvs_close(nvs_handle);

    return (err == ESP_OK);
}