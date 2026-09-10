#include "device/inc/mqtt_client.h"

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_event.h"
#include "mqtt_client.h"
#include "nvs_flash.h"
#include "cJSON.h"

static const char *TAG = "mqtt_client";

/* MQTT 客户端句柄 */
static esp_mqtt_client_handle_t s_client = NULL;
static mqtt_state_t s_state = MQTT_STATE_DISCONNECTED;
static mqtt_message_cb_t s_message_cb = NULL;
static device_info_t s_device_info = {0};

/* MQTT 服务器地址 */
static char s_broker_uri[128] = {0};

/* 主题定义 */
#define TOPIC_STATUS_FMT        "device/%s/status"
#define TOPIC_COMMAND_FMT       "device/%s/command"
#define TOPIC_USER_COMMAND_FMT  "user/%s/device/%s/command"
#define TOPIC_BIND_QUERY_FMT    "device/%s/bind_query"
#define TOPIC_BIND_RESULT_FMT   "device/%s/bind_result"

/* 从 NVS 加载设备信息 */
static esp_err_t load_device_info(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("device_info", NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No device info in NVS, will generate new one");
        return err;
    }

    size_t len = sizeof(s_device_info.device_id);
    nvs_get_str(nvs, "device_id", s_device_info.device_id, &len);

    len = sizeof(s_device_info.user_id);
    nvs_get_str(nvs, "user_id", s_device_info.user_id, &len);

    len = sizeof(s_device_info.temp_token);
    nvs_get_str(nvs, "temp_token", s_device_info.temp_token, &len);

    uint8_t bound = 0;
    nvs_get_u8(nvs, "bound", &bound);
    s_device_info.bound = (bound != 0);

    nvs_close(nvs);

    ESP_LOGI(TAG, "Loaded device info: id=%s, bound=%d",
             s_device_info.device_id, s_device_info.bound);
    return ESP_OK;
}

/* 保存设备信息到 NVS */
static esp_err_t save_device_info(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("device_info", NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }

    nvs_set_str(nvs, "device_id", s_device_info.device_id);
    nvs_set_str(nvs, "user_id", s_device_info.user_id);
    nvs_set_str(nvs, "temp_token", s_device_info.temp_token);
    nvs_set_u8(nvs, "bound", s_device_info.bound ? 1 : 0);

    nvs_commit(nvs);
    nvs_close(nvs);

    ESP_LOGI(TAG, "Saved device info to NVS");
    return ESP_OK;
}

/* 生成设备ID (使用 MAC 地址) */
static void generate_device_id(void)
{
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    snprintf(s_device_info.device_id, sizeof(s_device_info.device_id),
             "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/* 生成临时 token */
static void generate_temp_token(void)
{
    const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    for (int i = 0; i < 32; i++) {
        int index = esp_random() % (sizeof(charset) - 1);
        s_device_info.temp_token[i] = charset[index];
    }
    s_device_info.temp_token[32] = '\0';
}

/* MQTT 事件处理 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                              int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT Connected");
            s_state = MQTT_STATE_CONNECTED;

            /* 订阅命令主题 */
            if (s_device_info.bound) {
                char topic[128];
                snprintf(topic, sizeof(topic), TOPIC_USER_COMMAND_FMT,
                        s_device_info.user_id, s_device_info.device_id);
                esp_mqtt_client_subscribe(s_client, topic, 1);
                ESP_LOGI(TAG, "Subscribed to: %s", topic);
            } else {
                char topic[128];
                snprintf(topic, sizeof(topic), TOPIC_BIND_RESULT_FMT,
                        s_device_info.device_id);
                esp_mqtt_client_subscribe(s_client, topic, 1);
                ESP_LOGI(TAG, "Subscribed to bind result: %s", topic);
            }

            /* 发布在线状态 */
            mqtt_client_publish_status("online", s_device_info.bound);
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT Disconnected");
            s_state = MQTT_STATE_DISCONNECTED;
            break;

        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT Data: topic=%.*s", event->topic_len, event->topic);

            /* 调用消息回调 */
            if (s_message_cb && event->topic_len > 0 && event->data_len > 0) {
                char *topic = strndup(event->topic, event->topic_len);
                char *payload = strndup(event->data, event->data_len);
                s_message_cb(topic, payload, event->data_len);
                free(topic);
                free(payload);
            }
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT Error");
            s_state = MQTT_STATE_ERROR;
            break;

        default:
            break;
    }
}

esp_err_t mqtt_client_init(void)
{
    /* 加载设备信息 */
    esp_err_t err = load_device_info();
    if (err != ESP_OK) {
        /* 首次启动，生成新的设备信息 */
        generate_device_id();
        generate_temp_token();
        s_device_info.user_id[0] = '\0';
        s_device_info.bound = false;
        save_device_info();
    }

    return ESP_OK;
}

esp_err_t mqtt_client_start(const char *broker_uri, const char *device_id,
                           const char *user_id, const char *temp_token)
{
    if (s_client != NULL) {
        ESP_LOGW(TAG, "MQTT client already started");
        return ESP_OK;
    }

    /* 保存配置 */
    strncpy(s_broker_uri, broker_uri, sizeof(s_broker_uri) - 1);
    if (device_id) strncpy(s_device_info.device_id, device_id, sizeof(s_device_info.device_id) - 1);
    if (user_id) strncpy(s_device_info.user_id, user_id, sizeof(s_device_info.user_id) - 1);
    if (temp_token) strncpy(s_device_info.temp_token, temp_token, sizeof(s_device_info.temp_token) - 1);

    /* 配置 MQTT 客户端 */
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = s_broker_uri,
        .credentials.client_id = s_device_info.device_id,
    };

    /* 设置认证信息 */
    if (s_device_info.bound && s_device_info.user_id[0] != '\0') {
        mqtt_cfg.credentials.username = s_device_info.device_id;
        mqtt_cfg.credentials.authentication.password = s_device_info.user_id;
    } else {
        mqtt_cfg.credentials.username = s_device_info.device_id;
        mqtt_cfg.credentials.authentication.password = s_device_info.temp_token;
    }

    s_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_client == NULL) {
        ESP_LOGE(TAG, "Failed to init MQTT client");
        return ESP_FAIL;
    }

    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);

    esp_err_t err = esp_mqtt_client_start(s_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(err));
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
        return err;
    }

    s_state = MQTT_STATE_CONNECTING;
    ESP_LOGI(TAG, "MQTT client started, connecting to %s", s_broker_uri);
    return ESP_OK;
}

esp_err_t mqtt_client_stop(void)
{
    if (s_client == NULL) {
        return ESP_OK;
    }

    /* 发布离线状态 */
    mqtt_client_publish_status("offline", s_device_info.bound);

    esp_err_t err = esp_mqtt_client_stop(s_client);
    if (err == ESP_OK) {
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
        s_state = MQTT_STATE_DISCONNECTED;
    }
    return err;
}

esp_err_t mqtt_client_publish(const char *topic, const char *payload, int qos)
{
    if (s_client == NULL || s_state != MQTT_STATE_CONNECTED) {
        ESP_LOGW(TAG, "MQTT not connected");
        return ESP_ERR_INVALID_STATE;
    }

    int msg_id = esp_mqtt_client_publish(s_client, topic, payload, 0, qos, 0);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Failed to publish to %s", topic);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Published to %s: %s", topic, payload);
    return ESP_OK;
}

esp_err_t mqtt_client_subscribe(const char *topic, int qos)
{
    if (s_client == NULL || s_state != MQTT_STATE_CONNECTED) {
        return ESP_ERR_INVALID_STATE;
    }

    int msg_id = esp_mqtt_client_subscribe(s_client, topic, qos);
    if (msg_id < 0) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t mqtt_client_unsubscribe(const char *topic)
{
    if (s_client == NULL || s_state != MQTT_STATE_CONNECTED) {
        return ESP_ERR_INVALID_STATE;
    }

    int msg_id = esp_mqtt_client_unsubscribe(s_client, topic);
    if (msg_id < 0) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

mqtt_state_t mqtt_client_get_state(void)
{
    return s_state;
}

void mqtt_client_register_message_cb(mqtt_message_cb_t cb)
{
    s_message_cb = cb;
}

esp_err_t mqtt_client_publish_status(const char *status, bool bound)
{
    char topic[128];
    snprintf(topic, sizeof(topic), TOPIC_STATUS_FMT, s_device_info.device_id);

    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "status", status);
    cJSON_AddBoolToObject(json, "bound", bound);
    cJSON_AddStringToObject(json, "deviceId", s_device_info.device_id);

    if (bound) {
        cJSON_AddStringToObject(json, "userId", s_device_info.user_id);
    }

    char *payload = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    esp_err_t err = mqtt_client_publish(topic, payload, 1);
    free(payload);
    return err;
}

esp_err_t mqtt_client_publish_feed_done(uint8_t amount)
{
    char topic[128];
    snprintf(topic, sizeof(topic), TOPIC_STATUS_FMT, s_device_info.device_id);

    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "event", "feed_done");
    cJSON_AddNumberToObject(json, "amount", amount);
    cJSON_AddStringToObject(json, "deviceId", s_device_info.device_id);

    char *payload = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    esp_err_t err = mqtt_client_publish(topic, payload, 1);
    free(payload);
    return err;
}

bool mqtt_client_is_bound(void)
{
    return s_device_info.bound;
}

const device_info_t *mqtt_client_get_device_info(void)
{
    return &s_device_info;
}
