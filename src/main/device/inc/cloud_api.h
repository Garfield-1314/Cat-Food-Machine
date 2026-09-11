#ifndef __CLOUD_API_H
#define __CLOUD_API_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/inc/feeding_schedule.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 云端命令类型 */
typedef enum {
    CLOUD_CMD_FEED = 0,          /* 手动喂食 */
    CLOUD_CMD_ADD_SCHEDULE,      /* 添加定时任务 */
    CLOUD_CMD_UPDATE_SCHEDULE,   /* 更新定时任务 */
    CLOUD_CMD_DELETE_SCHEDULE,   /* 删除定时任务 */
    CLOUD_CMD_GET_STATUS,        /* 获取状态 */
    CLOUD_CMD_UNBIND,            /* 解绑设备 */
    CLOUD_CMD_SYNC_SCHEDULES,    /* 全量同步定时任务 */
    CLOUD_CMD_GET_SCHEDULES,     /* 上报定时任务列表 */
    CLOUD_CMD_START_CAPTURE,     /* 开始远程截图 */
    CLOUD_CMD_STOP_CAPTURE,      /* 停止远程截图 */
    CLOUD_CMD_UNKNOWN
} cloud_cmd_type_t;

/* 云端命令结构体 */
typedef struct {
    cloud_cmd_type_t type;
    char device_id[17];
    char user_id[65];
    char token[33];
    union {
        struct {
            uint8_t slots;       /* 喂食仓位数 */
        } feed;
        struct {
            int index;           /* 定时任务索引 */
            uint8_t hour;
            uint8_t minute;
            uint8_t amount;
            bool enabled;
            uint8_t every_days;
        } schedule;
        struct {
            feed_schedule_item_t items[MAX_SCHEDULE_ITEMS];
            int count;
        } schedules;
    } params;
} cloud_cmd_t;

/* 云端命令回调 */
typedef void (*cloud_cmd_cb_t)(const cloud_cmd_t *cmd);

/**
 * @brief 初始化云端 API 模块
 * @return ESP_OK 成功
 */
esp_err_t cloud_api_init(void);

/**
 * @brief 处理 MQTT 消息
 * @param topic 消息主题
 * @param payload 消息内容
 * @param payload_len 消息长度
 */
void cloud_api_handle_mqtt_message(const char *topic, const char *payload, int payload_len);

/**
 * @brief 注册云端命令回调
 * @param cb 回调函数
 */
void cloud_api_register_cmd_cb(cloud_cmd_cb_t cb);

/**
 * @brief 解析云端命令（明文 JSON，调用前已完成验签解密）
 * @param payload 消息内容
 * @param cmd 输出命令结构体
 * @return true 解析成功, false 解析失败
 */
bool cloud_api_parse_command(const char *payload, cloud_cmd_t *cmd);

/**
 * @brief 以 retained 消息上报当前定时任务列表到 device/<id>/schedules
 */
void cloud_api_report_schedules(void);

#ifdef __cplusplus
}
#endif

#endif /* __CLOUD_API_H */
