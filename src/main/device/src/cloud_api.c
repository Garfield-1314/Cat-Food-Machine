#include "device/inc/cloud_api.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "esp_log.h"
#include "cJSON.h"
#include "device/inc/mqtt_client.h"
#include "device/inc/secure_msg.h"

static const char *TAG = "cloud_api";

/* 云端命令回调 */
static cloud_cmd_cb_t s_cmd_cb = NULL;

#define TOPIC_CMD_FMT        "device/%s/cmd"
#define TOPIC_BIND_FMT       "device/%s/bind"
#define TOPIC_BIND_ACK_FMT   "device/%s/bind_ack"
#define TOPIC_SCHEDULES_FMT  "device/%s/schedules"

esp_err_t cloud_api_init(void)
{
    ESP_LOGI(TAG, "Cloud API initialized");
    return ESP_OK;
}

void cloud_api_register_cmd_cb(cloud_cmd_cb_t cb)
{
    s_cmd_cb = cb;
}

bool cloud_api_parse_command(const char *payload, cloud_cmd_t *cmd)
{
    if (!payload || !cmd) {
        return false;
    }

    memset(cmd, 0, sizeof(cloud_cmd_t));

    cJSON *json = cJSON_Parse(payload);
    if (!json) {
        ESP_LOGE(TAG, "Failed to parse command JSON");
        return false;
    }

    cJSON *cmd_type = cJSON_GetObjectItem(json, "cmd");
    if (!cJSON_IsString(cmd_type)) {
        ESP_LOGE(TAG, "Missing command type");
        cJSON_Delete(json);
        return false;
    }

    const char *type_str = cmd_type->valuestring;

    if (strcmp(type_str, "feed") == 0) {
        cmd->type = CLOUD_CMD_FEED;
        cJSON *slots = cJSON_GetObjectItem(json, "slots");
        if (cJSON_IsNumber(slots)) {
            cmd->params.feed.slots = (uint8_t)slots->valueint;
        } else {
            cmd->params.feed.slots = 1; /* 默认1个仓位 */
        }
        ESP_LOGI(TAG, "Command: feed %d slots", cmd->params.feed.slots);
    } else if (strcmp(type_str, "add_schedule") == 0) {
        cmd->type = CLOUD_CMD_ADD_SCHEDULE;
        cJSON *hour = cJSON_GetObjectItem(json, "hour");
        cJSON *minute = cJSON_GetObjectItem(json, "minute");
        cJSON *amount = cJSON_GetObjectItem(json, "amount");
        cJSON *enabled = cJSON_GetObjectItem(json, "enabled");
        cJSON *every_days = cJSON_GetObjectItem(json, "every_days");

        if (cJSON_IsNumber(hour)) cmd->params.schedule.hour = (uint8_t)hour->valueint;
        if (cJSON_IsNumber(minute)) cmd->params.schedule.minute = (uint8_t)minute->valueint;
        if (cJSON_IsNumber(amount)) cmd->params.schedule.amount = (uint8_t)amount->valueint;
        if (cJSON_IsBool(enabled)) cmd->params.schedule.enabled = cJSON_IsTrue(enabled);
        if (cJSON_IsNumber(every_days)) cmd->params.schedule.every_days = (uint8_t)every_days->valueint;

        ESP_LOGI(TAG, "Command: add_schedule %02d:%02d amount=%d every=%d",
                cmd->params.schedule.hour, cmd->params.schedule.minute,
                cmd->params.schedule.amount, cmd->params.schedule.every_days);
    } else if (strcmp(type_str, "update_schedule") == 0) {
        cmd->type = CLOUD_CMD_UPDATE_SCHEDULE;
        cJSON *index = cJSON_GetObjectItem(json, "index");
        if (cJSON_IsNumber(index)) cmd->params.schedule.index = index->valueint;

        cJSON *hour = cJSON_GetObjectItem(json, "hour");
        cJSON *minute = cJSON_GetObjectItem(json, "minute");
        cJSON *amount = cJSON_GetObjectItem(json, "amount");
        cJSON *enabled = cJSON_GetObjectItem(json, "enabled");
        cJSON *every_days = cJSON_GetObjectItem(json, "every_days");

        if (cJSON_IsNumber(hour)) cmd->params.schedule.hour = (uint8_t)hour->valueint;
        if (cJSON_IsNumber(minute)) cmd->params.schedule.minute = (uint8_t)minute->valueint;
        if (cJSON_IsNumber(amount)) cmd->params.schedule.amount = (uint8_t)amount->valueint;
        if (cJSON_IsBool(enabled)) cmd->params.schedule.enabled = cJSON_IsTrue(enabled);
        if (cJSON_IsNumber(every_days)) cmd->params.schedule.every_days = (uint8_t)every_days->valueint;

        ESP_LOGI(TAG, "Command: update_schedule[%d] %02d:%02d", cmd->params.schedule.index,
                cmd->params.schedule.hour, cmd->params.schedule.minute);
    } else if (strcmp(type_str, "delete_schedule") == 0) {
        cmd->type = CLOUD_CMD_DELETE_SCHEDULE;
        cJSON *index = cJSON_GetObjectItem(json, "index");
        if (cJSON_IsNumber(index)) cmd->params.schedule.index = index->valueint;
        ESP_LOGI(TAG, "Command: delete_schedule[%d]", cmd->params.schedule.index);
    } else if (strcmp(type_str, "get_status") == 0) {
        cmd->type = CLOUD_CMD_GET_STATUS;
        ESP_LOGI(TAG, "Command: get_status");
    } else if (strcmp(type_str, "unbind") == 0) {
        cmd->type = CLOUD_CMD_UNBIND;
        ESP_LOGI(TAG, "Command: unbind");
    } else if (strcmp(type_str, "get_schedules") == 0) {
        cmd->type = CLOUD_CMD_GET_SCHEDULES;
        ESP_LOGI(TAG, "Command: get_schedules");
    } else if (strcmp(type_str, "sync_schedules") == 0) {
        cmd->type = CLOUD_CMD_SYNC_SCHEDULES;
        cJSON *arr = cJSON_GetObjectItem(json, "schedules");
        int count = 0;
        if (cJSON_IsArray(arr)) {
            cJSON *el = NULL;
            cJSON_ArrayForEach(el, arr) {
                if (count >= MAX_SCHEDULE_ITEMS) {
                    break;
                }
                if (!cJSON_IsObject(el)) {
                    continue;
                }

                feed_schedule_item_t *item = &cmd->params.schedules.items[count];
                cJSON *hour = cJSON_GetObjectItem(el, "hour");
                cJSON *minute = cJSON_GetObjectItem(el, "minute");
                cJSON *amount = cJSON_GetObjectItem(el, "amount");
                cJSON *enabled = cJSON_GetObjectItem(el, "enabled");
                cJSON *every_days = cJSON_GetObjectItem(el, "every_days");

                if (cJSON_IsNumber(hour)) item->hour = (uint8_t)hour->valueint;
                if (cJSON_IsNumber(minute)) item->minute = (uint8_t)minute->valueint;
                if (cJSON_IsNumber(amount)) item->amount = (uint8_t)amount->valueint;
                if (cJSON_IsBool(enabled)) item->enabled = cJSON_IsTrue(enabled);
                if (cJSON_IsNumber(every_days)) item->every_days = (uint8_t)every_days->valueint;
                count++;
            }
        }
        cmd->params.schedules.count = count;
        ESP_LOGI(TAG, "Command: sync_schedules (%d items)", count);
    } else if (strcmp(type_str, "start_capture") == 0) {
        cmd->type = CLOUD_CMD_START_CAPTURE;
        ESP_LOGI(TAG, "Command: start_capture");
    } else if (strcmp(type_str, "stop_capture") == 0) {
        cmd->type = CLOUD_CMD_STOP_CAPTURE;
        ESP_LOGI(TAG, "Command: stop_capture");
    } else {
        ESP_LOGW(TAG, "Unknown command: %s", type_str);
        cmd->type = CLOUD_CMD_UNKNOWN;
    }

    cJSON_Delete(json);
    return true;
}

/* 绑定确认：使用 token 派生密钥加密，retained 供云端拉取；不包含 openid 等敏感信息 */
static void cloud_api_publish_bind_ack(const char *nonce)
{
    const device_info_t *dev = mqtt_client_get_device_info();
    if (dev == NULL || dev->device_id[0] == '\0') {
        return;
    }

    cJSON *obj = cJSON_CreateObject();
    if (obj == NULL) {
        return;
    }
    cJSON_AddBoolToObject(obj, "ack", true);
    cJSON_AddStringToObject(obj, "nonce", nonce != NULL ? nonce : "");
    cJSON_AddNumberToObject(obj, "ts", (double)time(NULL));

    char *payload = secure_msg_pack_json(dev->temp_token, NULL, obj);
    cJSON_Delete(obj);
    if (payload == NULL) {
        ESP_LOGW(TAG, "Failed to pack bind ack");
        return;
    }

    char topic[128];
    snprintf(topic, sizeof(topic), TOPIC_BIND_ACK_FMT, dev->device_id);
    mqtt_client_publish_ex(topic, payload, 1, true);
    free(payload);
}

static void cloud_api_handle_bind(const char *payload)
{
    const device_info_t *dev = mqtt_client_get_device_info();
    if (dev == NULL || dev->bound) {
        ESP_LOGW(TAG, "Bind ignored (already bound)");
        return;
    }

    cJSON *obj = secure_msg_unpack_json(dev->temp_token, NULL, payload);
    if (obj == NULL) {
        ESP_LOGW(TAG, "Bind request verify failed");
        return;
    }

    if (!secure_msg_accept_message(obj)) {
        cJSON_Delete(obj);
        return;
    }

    cJSON *action = cJSON_GetObjectItem(obj, "action");
    cJSON *uid = cJSON_GetObjectItem(obj, "userId");
    cJSON *nonce = cJSON_GetObjectItem(obj, "nonce");

    if (cJSON_IsString(action) && strcmp(action->valuestring, "bind") == 0 &&
        cJSON_IsString(uid) && uid->valuestring[0] != '\0' &&
        cJSON_IsString(nonce)) {
        cloud_api_publish_bind_ack(nonce->valuestring);
        esp_err_t err = mqtt_client_set_bound_user(uid->valuestring);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to persist binding: %s", esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "Device bound to user: %s", uid->valuestring);
            cloud_api_report_schedules();
        }
    } else {
        ESP_LOGW(TAG, "Bind request payload invalid");
    }

    cJSON_Delete(obj);
}

static void cloud_api_handle_command(const char *payload)
{
    const device_info_t *dev = mqtt_client_get_device_info();
    if (dev == NULL || !dev->bound || dev->user_id[0] == '\0') {
        ESP_LOGW(TAG, "Command ignored (not bound)");
        return;
    }

    cJSON *obj = secure_msg_unpack_json(dev->temp_token, dev->user_id, payload);
    if (obj == NULL) {
        ESP_LOGW(TAG, "Command verify failed");
        return;
    }

    if (!secure_msg_accept_message(obj)) {
        cJSON_Delete(obj);
        return;
    }

    char *plain = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    if (plain == NULL) {
        return;
    }

    cloud_cmd_t cmd;
    if (cloud_api_parse_command(plain, &cmd)) {
        if (s_cmd_cb) {
            s_cmd_cb(&cmd);
        }
    }
    free(plain);
}

void cloud_api_handle_mqtt_message(const char *topic, const char *payload, int payload_len)
{
    if (!topic || !payload) {
        return;
    }

    const device_info_t *dev = mqtt_client_get_device_info();
    if (dev == NULL || dev->device_id[0] == '\0') {
        return;
    }

    ESP_LOGI(TAG, "Handling MQTT message: topic=%s", topic);

    char expect_cmd[96];
    char expect_bind[96];
    snprintf(expect_cmd, sizeof(expect_cmd), TOPIC_CMD_FMT, dev->device_id);
    snprintf(expect_bind, sizeof(expect_bind), TOPIC_BIND_FMT, dev->device_id);

    if (strcmp(topic, expect_cmd) == 0) {
        cloud_api_handle_command(payload);
        return;
    }

    if (strcmp(topic, expect_bind) == 0) {
        cloud_api_handle_bind(payload);
        return;
    }

    ESP_LOGW(TAG, "Unknown topic: %s", topic);
}

void cloud_api_report_schedules(void)
{
    const device_info_t *dev = mqtt_client_get_device_info();
    if (dev == NULL || dev->device_id[0] == '\0') {
        return;
    }

    /* 未绑定时不上报，避免在公共 broker 泄露投喂计划 */
    if (!dev->bound || dev->user_id[0] == '\0') {
        return;
    }

    char topic[128];
    snprintf(topic, sizeof(topic), TOPIC_SCHEDULES_FMT, dev->device_id);

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return;
    }
    cJSON *arr = cJSON_AddArrayToObject(root, "schedules");

    int count = feed_schedule_get_count();
    for (int i = 0; i < count; i++) {
        const feed_schedule_item_t *item = feed_schedule_get_item(i);
        if (item == NULL) {
            continue;
        }
        cJSON *obj = cJSON_CreateObject();
        if (obj == NULL) {
            continue;
        }
        cJSON_AddNumberToObject(obj, "hour", item->hour);
        cJSON_AddNumberToObject(obj, "minute", item->minute);
        cJSON_AddNumberToObject(obj, "amount", item->amount);
        cJSON_AddBoolToObject(obj, "enabled", item->enabled);
        cJSON_AddNumberToObject(obj, "every_days", item->every_days);
        cJSON_AddItemToArray(arr, obj);
    }
    cJSON_AddNumberToObject(root, "count", count);
    cJSON_AddNumberToObject(root, "ts", (double)time(NULL));

    char *payload = secure_msg_pack_json(dev->temp_token, dev->user_id, root);
    cJSON_Delete(root);
    if (payload != NULL) {
        mqtt_client_publish_ex(topic, payload, 1, true);
        free(payload);
    }
}
