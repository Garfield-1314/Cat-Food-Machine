#include "device/inc/wifi_app.h"

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "lwip/err.h"
#include "lwip/sys.h"

static const char *TAG = "wifi_app";

/*
 * 调试用 WiFi 配置：默认留空，使用 NVS 配置。
 * 填写 test_ssid 后，开机直接使用该 SSID/密码连接，不读取 NVS。
 */
static const char test_ssid[] = "";
static const char test_key[] = "";

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
static volatile bool s_is_connected = false;
static volatile bool s_connecting = false;  /* 正在连接中 */
static volatile bool s_user_switch = false; /* 用户主动切换/断开引起的断开事件，抑制自动重试 */
static volatile bool s_scan_in_progress = false;
static char s_current_ssid[33] = {0};
static char s_current_ip[16] = {0};
static wifi_connected_cb_t s_connected_cb = NULL;
static esp_timer_handle_t s_retry_timer = NULL;

/* 10 分钟重试定时器回调 */
static void retry_timer_cb(void *arg)
{
    (void)arg;
    if (s_is_connected || s_connecting || s_scan_in_progress) {
        ESP_LOGI(TAG, "Periodic retry skipped (WiFi operation already in progress)");
        return;
    }

    ESP_LOGI(TAG, "Periodic retry (10 min interval)...");
    s_retry_num = 0;
    esp_err_t ret = esp_wifi_connect();
    if (ret == ESP_OK) {
        s_connecting = true;
    } else {
        s_connecting = false;
        ESP_LOGW(TAG, "Periodic WiFi connect failed: %s", esp_err_to_name(ret));
    }
    /* 定时器使用周期模式，不在回调中重新操作自身句柄。 */
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
    esp_err_t ret = esp_timer_create(&timer_args, &s_retry_timer);
    if (ret == ESP_OK) {
        ret = esp_timer_start_periodic(s_retry_timer,
                                       RETRY_TIMER_PERIOD_MS * 1000ULL);
    }
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "10 min retry timer started");
    } else {
        ESP_LOGE(TAG, "failed to start 10 min retry timer: %s",
                 esp_err_to_name(ret));
        if (s_retry_timer != NULL) {
            esp_timer_delete(s_retry_timer);
            s_retry_timer = NULL;
        }
    }
}

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_is_connected = false;
        s_current_ip[0] = '\0';
        s_connecting = false;
        if (s_scan_in_progress) {
            /* 扫描前主动中断连接，扫描期间禁止自动重连抢占 WiFi。 */
            s_user_switch = false;
            ESP_LOGI(TAG, "disconnect for WiFi scan, skip auto retry");
        } else if (s_user_switch) {
            /* 用户主动切换/断开引起的事件：不自动重试，等用户重新发起连接 */
            s_user_switch = false;
            ESP_LOGI(TAG, "user-initiated disconnect, skip auto retry");
        } else if (s_retry_num < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            s_connecting = true;
            ESP_LOGI(TAG, "retry to connect to the AP (%d/%d)", s_retry_num, WIFI_MAX_RETRY);
        } else {
            ESP_LOGI(TAG, "connect to the AP fail after %d retries, will retry every 10 min", WIFI_MAX_RETRY);
            start_retry_timer();
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        s_user_switch = false;
        ESP_LOGI(TAG, "connected to AP");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        snprintf(s_current_ip, sizeof(s_current_ip), IPSTR,
                 IP2STR(&event->ip_info.ip));
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
    /* NVS 已在 user_component_init() 中统一初始化（含擦除重试），这里只依赖 nvs_open */

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

    /* 调试配置优先；为空时才从 NVS 读取已保存的 WiFi 配置 */
    char saved_ssid[33] = {0};
    char saved_pass[64] = {0};
    bool has_test_config = (test_ssid[0] != '\0');
    bool has_saved_config = false;
    const char *connect_ssid = NULL;
    const char *connect_password = NULL;

    if (has_test_config) {
        connect_ssid = test_ssid;
        connect_password = test_key;
        ESP_LOGI(TAG, "Debug WiFi config enabled, skipping NVS load");
    } else {
        has_saved_config = wifi_app_load_config(saved_ssid, saved_pass);
        if (has_saved_config) {
            connect_ssid = saved_ssid;
            connect_password = saved_pass;
        }
    }

    if (has_test_config || has_saved_config) {
        /* 在 start 之前设置好配置，start 后不会触发 WIFI_EVENT_STA_START 自动连接 */
        wifi_config_t wifi_config = {
            .sta = {
                .threshold.authmode = WIFI_AUTH_WPA2_PSK,
                .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
            },
        };
        strncpy((char *)wifi_config.sta.ssid, connect_ssid,
                sizeof(wifi_config.sta.ssid) - 1);
        strncpy((char *)wifi_config.sta.password, connect_password,
                sizeof(wifi_config.sta.password) - 1);
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

        strncpy(s_current_ssid, connect_ssid, sizeof(s_current_ssid) - 1);
        s_current_ssid[sizeof(s_current_ssid) - 1] = '\0';
    }

    /* 启动 WiFi */
    ESP_ERROR_CHECK(esp_wifi_start());

    if (has_test_config || has_saved_config) {
        /* 配置已在 start 之前设置，这里主动发起连接 */
        esp_err_t ret = esp_wifi_connect();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to auto-connect: %s", esp_err_to_name(ret));
        } else {
            s_retry_num = 0;
            s_connecting = true;
            ESP_LOGI(TAG, "%s WiFi config, auto-connecting to SSID: %s",
                     has_test_config ? "Debug" : "Saved", connect_ssid);
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

    /* 正在连接同一个 SSID：用户再次点击说明想重试（如改对密码），打断旧尝试后重新发起 */
    if (s_connecting && strcmp(s_current_ssid, ssid) == 0) {
        ESP_LOGI(TAG, "Re-connecting to SSID: %s, restarting attempt", ssid);
        s_user_switch = true;
        esp_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(100));
        s_connecting = false;
        s_retry_num = 0;
    }

    /* 如果已经连接到别的 WiFi，需要先断开 */
    if (s_is_connected || s_connecting) {
        ESP_LOGI(TAG, "Disconnecting from current AP before connecting to new one");
        s_user_switch = true;
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
    s_user_switch = true;
    esp_wifi_disconnect();
    s_is_connected = false;
    s_connecting = false;
    memset(s_current_ssid, 0, sizeof(s_current_ssid));
}

esp_err_t wifi_app_scan(wifi_ap_info_t *results, uint16_t *count, uint16_t max_count)
{
    if (results == NULL || count == NULL || max_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_scan_config_t scan_cfg = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 300,
    };

    /* 自动连接中的 STA 不允许扫描。先中断连接，并在驱动状态稳定后重试。 */
    bool resume_periodic_retry = s_connecting;
    s_scan_in_progress = true;
    if (resume_periodic_retry) {
        s_user_switch = true;
        esp_err_t disconnect_ret = esp_wifi_disconnect();
        if (disconnect_ret != ESP_OK) {
            ESP_LOGW(TAG, "WiFi disconnect before scan failed: %s",
                     esp_err_to_name(disconnect_ret));
        }
        s_connecting = false;
    }

    esp_err_t ret = ESP_ERR_WIFI_STATE;
    for (int attempt = 0; attempt < 10; attempt++) {
        ret = esp_wifi_scan_start(&scan_cfg, true);
        if (ret != ESP_ERR_WIFI_STATE) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Scan start failed: %s", esp_err_to_name(ret));
        goto scan_finished;
    }

    uint16_t ap_num = 0;
    ret = esp_wifi_scan_get_ap_num(&ap_num);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Scan get_ap_num failed: %s", esp_err_to_name(ret));
        goto scan_finished;
    }

    if (ap_num > max_count) {
        ap_num = max_count;
    }

    if (ap_num == 0) {
        *count = 0;
        ret = ESP_OK;
        goto scan_finished;
    }

    wifi_ap_record_t *aps = calloc(ap_num, sizeof(wifi_ap_record_t));
    if (aps == NULL) {
        ret = ESP_ERR_NO_MEM;
        goto scan_finished;
    }

    ret = esp_wifi_scan_get_ap_records(&ap_num, aps);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Scan get_ap_records failed: %s", esp_err_to_name(ret));
        free(aps);
        goto scan_finished;
    }

    for (uint16_t i = 0; i < ap_num; i++) {
        memcpy(results[i].ssid, aps[i].ssid, sizeof(aps[i].ssid));
        results[i].ssid[sizeof(results[i].ssid) - 1] = '\0';
        results[i].rssi = aps[i].rssi;
        results[i].authmode = (uint8_t)aps[i].authmode;
    }
    *count = ap_num;

    free(aps);

    ret = ESP_OK;

scan_finished:
    s_scan_in_progress = false;
    s_user_switch = false;
    if (resume_periodic_retry && !s_is_connected) {
        /* 扫描结束后恢复原本的后台周期重试。 */
        start_retry_timer();
    }
    return ret;
}

bool wifi_app_is_connected(void)
{
    return s_is_connected;
}

bool wifi_app_is_connecting(void)
{
    return s_connecting;
}

const char *wifi_app_get_ssid(void)
{
    return s_is_connected ? s_current_ssid : NULL;
}

const char *wifi_app_get_ip(void)
{
    return s_is_connected ? s_current_ip : NULL;
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

    size_t ssid_len = 33;
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
