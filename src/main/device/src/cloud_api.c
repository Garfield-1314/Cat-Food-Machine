#include "device/inc/cloud_api.h"

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "cloud_api";

/* 云端命令回调 */
static cloud_cmd_cb_t s_cmd_cb = NULL;

/* 主题匹配宏 */
#define TOPIC_BIND_RESULT_PREFIX    "device/"
#define TOPIC_BIND_RESULT_SUFFIX    "/bind_result"
#define TOPIC_COMMAND_SUFFIX        "/command"

esp_err_t cloud_api_init(void)
{
    ESP_LOGI(TAG, "Cloud API initialized");
    return ESP_OK;
}

void cloud_api_register_cmd_cb(cloud_cmd_cb_t cb)
{
    s_cmd_cb = cb;
}

bool cloud_api_parse_bind_result(const char *payload, char *user_id, size_t user_id_size)
{
    if (!payload || !user_id) {
        return false;
    }

    cJSON *json = cJSON_Parse(payload);
    if (!json) {
        ESP_LOGE(TAG, "Failed to parse bind result JSON");
        return false;
    }

    cJSON *success = cJSON_GetObjectItem(json, "success");
    cJSON *uid = cJSON_GetObjectItem(json, "userId");

    bool result = false;
    if (cJSON_IsBool(success) && cJSON_IsTrue(success) && cJSON_IsString(uid)) {
        strncpy(user_id, uid->valuestring, user_id_size - 1);
        user_id[user_id_size - 1] = '\0';
        result = true;
        ESP_LOGI(TAG, "Bind success, userId: %s", user_id);
    } else {
        ESP_LOGW(TAG, "Bind failed or invalid response");
    }

    cJSON_Delete(json);
    return result;
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
    } else {
        ESP_LOGW(TAG, "Unknown command: %s", type_str);
        cmd->type = CLOUD_CMD_UNKNOWN;
    }

    cJSON_Delete(json);
    return true;
}

void cloud_api_handle_mqtt_message(const char *topic, const char *payload, int payload_len)
{
    if (!topic || !payload) {
        return;
    }

    ESP_LOGI(TAG, "Handling MQTT message: topic=%s", topic);

    /* 检查是否是绑定结果 */
    if (strstr(topic, TOPIC_BIND_RESULT_PREFIX) && strstr(topic, TOPIC_BIND_RESULT_SUFFIX)) {
        char user_id[65] = {0};
        if (cloud_api_parse_bind_result(payload, user_id, sizeof(user_id))) {
            /* 绑定成功，通知上层 */
            cloud_cmd_t cmd = {
                .type = CLOUD_CMD_UNKNOWN,
                .user_id = {0}
            };
            strncpy(cmd.user_id, user_id, sizeof(cmd.user_id) - 1);

            if (s_cmd_cb) {
                s_cmd_cb(&cmd);
            }
        }
        return;
    }

    /* 检查是否是命令消息 */
    if (strstr(topic, TOPIC_COMMAND_SUFFIX)) {
        cloud_cmd_t cmd = {0};
        if (cloud_api_parse_command(payload, &cmd)) {
            if (s_cmd_cb) {
                s_cmd_cb(&cmd);
            }
        }
        return;
    }

    ESP_LOGW(TAG, "Unknown topic: %s", topic);
}
