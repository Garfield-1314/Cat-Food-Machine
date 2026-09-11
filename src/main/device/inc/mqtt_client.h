#ifndef __MQTT_CLIENT_H
#define __MQTT_CLIENT_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* MQTT 连接状态 */
typedef enum {
    MQTT_STATE_DISCONNECTED = 0,
    MQTT_STATE_CONNECTING,
    MQTT_STATE_CONNECTED,
    MQTT_STATE_ERROR
} mqtt_state_t;

/* MQTT 消息回调 */
typedef void (*mqtt_message_cb_t)(const char *topic, const char *payload, int payload_len);

/* MQTT 连接成功回调 */
typedef void (*mqtt_connected_cb_t)(void);

/* 设备信息结构体 */
typedef struct {
    char device_id[17];      /* 设备ID (MAC地址) */
    char user_id[65];        /* 用户 openId */
    char temp_token[33];     /* 临时token (未绑定时使用) */
    bool bound;              /* 是否已绑定 */
} device_info_t;

/**
 * @brief 初始化 MQTT 客户端
 * @return ESP_OK 成功
 */
esp_err_t mqtt_client_init(void);

/**
 * @brief 启动 MQTT 连接
 * @param broker_uri MQTT 服务器地址 (如 "mqtt://broker.example.com")
 * @param device_id 设备ID
 * @param user_id 用户ID (已绑定时使用)
 * @param temp_token 临时token (未绑定时使用)
 * @return ESP_OK 成功
 */
esp_err_t mqtt_client_start(const char *broker_uri, const char *device_id,
                           const char *user_id, const char *temp_token);

/**
 * @brief 停止 MQTT 连接
 * @return ESP_OK 成功
 */
esp_err_t mqtt_client_stop(void);

/**
 * @brief 发布消息
 * @param topic 主题
 * @param payload 消息内容
 * @param qos QoS 等级 (0, 1, 2)
 * @return ESP_OK 成功
 */
esp_err_t mqtt_client_publish(const char *topic, const char *payload, int qos);

/**
 * @brief 发布消息（可指定 retain）
 * @param topic 主题
 * @param payload 消息内容
 * @param qos QoS 等级 (0, 1, 2)
 * @param retain 是否保留消息
 * @return ESP_OK 成功
 */
esp_err_t mqtt_client_publish_ex(const char *topic, const char *payload,
                                 int qos, bool retain);

/**
 * @brief 订阅主题
 * @param topic 主题
 * @param qos QoS 等级
 * @return ESP_OK 成功
 */
esp_err_t mqtt_client_subscribe(const char *topic, int qos);

/**
 * @brief 取消订阅
 * @param topic 主题
 * @return ESP_OK 成功
 */
esp_err_t mqtt_client_unsubscribe(const char *topic);

/**
 * @brief 获取 MQTT 连接状态
 * @return MQTT 状态
 */
mqtt_state_t mqtt_client_get_state(void);

/**
 * @brief 注册消息回调
 * @param cb 回调函数
 */
void mqtt_client_register_message_cb(mqtt_message_cb_t cb);

/**
 * @brief 注册 MQTT 连接成功回调
 * @param cb 回调函数
 */
void mqtt_client_register_connected_cb(mqtt_connected_cb_t cb);

/**
 * @brief 发布设备状态
 * @param status 状态字符串 ("online"/"offline")
 * @param bound 是否已绑定
 * @return ESP_OK 成功
 */
esp_err_t mqtt_client_publish_status(const char *status, bool bound);

/**
 * @brief 发布喂食完成事件
 * @param amount 喂食仓位数
 * @return ESP_OK 成功
 */
esp_err_t mqtt_client_publish_feed_done(uint8_t amount);

/**
 * @brief 检查设备是否已绑定
 * @return true 已绑定, false 未绑定
 */
bool mqtt_client_is_bound(void);

/**
 * @brief 标记设备已绑定到指定用户并持久化到 NVS
 * @param user_id 用户 openId
 * @return ESP_OK 成功
 */
esp_err_t mqtt_client_set_bound_user(const char *user_id);

/**
 * @brief 清除绑定状态（解绑）并持久化到 NVS
 * @return ESP_OK 成功
 */
esp_err_t mqtt_client_clear_binding(void);

/**
 * @brief 轮换临时 token（解绑后调用，使旧二维码失效）
 * @return ESP_OK 成功
 */
esp_err_t mqtt_client_regenerate_token(void);

/**
 * @brief 获取设备信息
 * @return 设备信息指针
 */
const device_info_t *mqtt_client_get_device_info(void);

#ifdef __cplusplus
}
#endif

#endif /* __MQTT_CLIENT_H */
