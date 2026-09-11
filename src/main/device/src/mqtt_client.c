#include "device/inc/mqtt_client.h"

#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "esp_log.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "mqtt_client.h"
#include "nvs_flash.h"
#include "cJSON.h"
#include "device/inc/wifi_app.h"
#include "device/inc/secure_msg.h"

static const char *TAG = "mqtt_client";

/* 心跳间隔：周期刷新 retained 在线状态，供云端判断在线 */
#define MQTT_HEARTBEAT_INTERVAL_US (60LL * 1000 * 1000)

/* MQTT 客户端句柄 */
static esp_mqtt_client_handle_t s_client = NULL;
static mqtt_state_t s_state = MQTT_STATE_DISCONNECTED;
static mqtt_message_cb_t s_message_cb = NULL;
static mqtt_connected_cb_t s_connected_cb = NULL;
static device_info_t s_device_info = {0};
static esp_timer_handle_t s_heartbeat_timer = NULL;

/* LWT（遗嘱）主题与载荷，需在客户端生命周期内保持有效 */
static char s_lwt_topic[128] = {0};
static char s_lwt_payload[160] = {0};

/* MQTT 服务器地址 */
static char s_broker_uri[128] = {0};

/* 主题定义 */
#define TOPIC_STATUS_FMT        "device/%s/status"
#define TOPIC_CMD_FMT           "device/%s/cmd"
#define TOPIC_BIND_FMT          "device/%s/bind"
#define TOPIC_BIND_ACK_FMT      "device/%s/bind_ack"
#define TOPIC_SCHEDULES_FMT     "device/%s/schedules"

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

/* 心跳定时器回调：刷新 retained 在线状态（esp_timer 任务上下文，publish 线程安全） */
static void mqtt_heartbeat_cb(void *arg)
{
    (void)arg;
    if (s_state == MQTT_STATE_CONNECTED) {
        mqtt_client_publish_status("online", s_device_info.bound);
    }
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

            /* 订阅命令主题与绑定请求主题（是否处理由 cloud_api 判断） */
            {
                char topic[128];
                snprintf(topic, sizeof(topic), TOPIC_CMD_FMT,
                         s_device_info.device_id);
                esp_mqtt_client_subscribe(s_client, topic, 1);
                ESP_LOGI(TAG, "Subscribed to: %s", topic);

                snprintf(topic, sizeof(topic), TOPIC_BIND_FMT,
                         s_device_info.device_id);
                esp_mqtt_client_subscribe(s_client, topic, 1);
                ESP_LOGI(TAG, "Subscribed to: %s", topic);
            }

            /* 发布在线状态 */
            mqtt_client_publish_status("online", s_device_info.bound);

            /* 通知上层（例如上报定时任务列表） */
            if (s_connected_cb) {
                s_connected_cb();
            }
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

    /* 启动在线状态心跳（周期刷新 retained 状态） */
    if (s_heartbeat_timer == NULL) {
        const esp_timer_create_args_t timer_args = {
            .callback = mqtt_heartbeat_cb,
            .name = "mqtt_heartbeat",
        };
        if (esp_timer_create(&timer_args, &s_heartbeat_timer) == ESP_OK) {
            esp_timer_start_periodic(s_heartbeat_timer, MQTT_HEARTBEAT_INTERVAL_US);
        } else {
            ESP_LOGW(TAG, "Failed to create heartbeat timer");
        }
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

    /* 配置 LWT：异常掉线时由 broker 发布离线状态（retained） */
    snprintf(s_lwt_topic, sizeof(s_lwt_topic), TOPIC_STATUS_FMT,
             s_device_info.device_id);
    snprintf(s_lwt_payload, sizeof(s_lwt_payload),
             "{\"status\":\"offline\",\"bound\":%s,\"deviceId\":\"%s\"}",
             s_device_info.bound ? "true" : "false", s_device_info.device_id);

    /* 配置 MQTT 客户端 */
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = s_broker_uri,
        .credentials.client_id = s_device_info.device_id,
        .session.last_will.topic = s_lwt_topic,
        .session.last_will.msg = s_lwt_payload,
        .session.last_will.msg_len = 0,
        .session.last_will.qos = 1,
        .session.last_will.retain = true,
    };

    /* 设置认证信息：公共 broker 不校验，但仍避免把 openid 作为密码外发 */
    mqtt_cfg.credentials.username = s_device_info.device_id;
    mqtt_cfg.credentials.authentication.password = s_device_info.temp_token;

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
    return mqtt_client_publish_ex(topic, payload, qos, false);
}

esp_err_t mqtt_client_publish_ex(const char *topic, const char *payload,
                                 int qos, bool retain)
{
    if (s_client == NULL || s_state != MQTT_STATE_CONNECTED) {
        ESP_LOGW(TAG, "MQTT not connected");
        return ESP_ERR_INVALID_STATE;
    }

    int msg_id = esp_mqtt_client_publish(s_client, topic, payload, 0, qos,
                                         retain ? 1 : 0);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Failed to publish to %s", topic);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Published to %s (%d bytes)", topic,
             payload != NULL ? (int)strlen(payload) : 0);
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

void mqtt_client_register_connected_cb(mqtt_connected_cb_t cb)
{
    s_connected_cb = cb;
}

esp_err_t mqtt_client_publish_status(const char *status, bool bound)
{
    char topic[128];
    snprintf(topic, sizeof(topic), TOPIC_STATUS_FMT, s_device_info.device_id);

    const char *ip = wifi_app_get_ip();
    if (ip == NULL) {
        ip = "";
    }

    cJSON *json = cJSON_CreateObject();
    if (json == NULL) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(json, "status", status);
    cJSON_AddBoolToObject(json, "bound", bound);
    cJSON_AddStringToObject(json, "deviceId", s_device_info.device_id);
    cJSON_AddStringToObject(json, "ip", ip);
    cJSON_AddNumberToObject(json, "ts", (double)time(NULL));

    esp_err_t err = ESP_FAIL;
    if (bound && s_device_info.user_id[0] != '\0') {
        /* 已绑定：状态加密签名，云端验签后才信任 */
        char *payload = secure_msg_pack_json(s_device_info.temp_token,
                                             s_device_info.user_id, json);
        if (payload != NULL) {
            err = mqtt_client_publish_ex(topic, payload, 1, true);
            free(payload);
        } else {
            ESP_LOGW(TAG, "Failed to pack status");
        }
    } else {
        /* 未绑定：仅发布最小明文信息，便于云端提示设备在线 */
        char *payload = cJSON_PrintUnformatted(json);
        if (payload != NULL) {
            err = mqtt_client_publish_ex(topic, payload, 1, true);
            free(payload);
        }
    }

    cJSON_Delete(json);
    return err;
}

esp_err_t mqtt_client_publish_feed_done(uint8_t amount)
{
    char topic[128];
    snprintf(topic, sizeof(topic), TOPIC_STATUS_FMT, s_device_info.device_id);

    cJSON *json = cJSON_CreateObject();
    if (json == NULL) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(json, "event", "feed_done");
    cJSON_AddNumberToObject(json, "amount", amount);
    cJSON_AddStringToObject(json, "deviceId", s_device_info.device_id);

    esp_err_t err = ESP_FAIL;
    if (s_device_info.bound && s_device_info.user_id[0] != '\0') {
        char *payload = secure_msg_pack_json(s_device_info.temp_token,
                                             s_device_info.user_id, json);
        if (payload != NULL) {
            err = mqtt_client_publish(topic, payload, 1);
            free(payload);
        }
    } else {
        char *payload = cJSON_PrintUnformatted(json);
        if (payload != NULL) {
            err = mqtt_client_publish(topic, payload, 1);
            free(payload);
        }
    }

    cJSON_Delete(json);
    return err;
}

bool mqtt_client_is_bound(void)
{
    return s_device_info.bound;
}

esp_err_t mqtt_client_set_bound_user(const char *user_id)
{
    if (user_id == NULL || user_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    strncpy(s_device_info.user_id, user_id, sizeof(s_device_info.user_id) - 1);
    s_device_info.user_id[sizeof(s_device_info.user_id) - 1] = '\0';
    s_device_info.bound = true;

    esp_err_t err = save_device_info();

    /* 命令主题常驻订阅，绑定后无需切换订阅 */
    if (s_client != NULL && s_state == MQTT_STATE_CONNECTED) {
        mqtt_client_publish_status("online", true);
    }

    return err;
}

esp_err_t mqtt_client_clear_binding(void)
{
    s_device_info.user_id[0] = '\0';
    s_device_info.bound = false;
    esp_err_t err = save_device_info();

    if (s_client != NULL && s_state == MQTT_STATE_CONNECTED) {
        mqtt_client_publish_status("online", false);
    }

    return err;
}

esp_err_t mqtt_client_regenerate_token(void)
{
    generate_temp_token();
    return save_device_info();
}

const device_info_t *mqtt_client_get_device_info(void)
{
    return &s_device_info;
}
