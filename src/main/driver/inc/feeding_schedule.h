#ifndef __FEEDING_SCHEDULE_H
#define __FEEDING_SCHEDULE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_SCHEDULE_ITEMS  8

/**
 * @brief 单个投喂计划项
 */
typedef struct {
    uint8_t hour;       /* 小时 0-23 */
    uint8_t minute;     /* 分钟 0-59 */
    uint8_t amount;     /* 仓位数量 1-10 */
    bool    enabled;    /* 是否启用 */
} feed_schedule_item_t;

/**
 * @brief 投喂触发回调函数类型
 * @param item 被触发的投喂计划项
 */
typedef void (*feeding_callback_t)(const feed_schedule_item_t *item);

/**
 * @brief 从 NVS 加载投喂计划
 * @return ESP_OK 成功，否则失败
 */
esp_err_t feed_schedule_load(void);

/**
 * @brief 保存投喂计划到 NVS
 * @return ESP_OK 成功，否则失败
 */
esp_err_t feed_schedule_save(void);

/**
 * @brief 获取投喂计划总数
 * @return 当前计划项数量
 */
int feed_schedule_get_count(void);

/**
 * @brief 获取指定索引的投喂计划项
 * @param index 索引 (0 ~ count-1)
 * @return 指向计划项的指针，越界返回 NULL
 */
const feed_schedule_item_t *feed_schedule_get_item(int index);

/**
 * @brief 设置指定索引的投喂计划项
 * @param index 索引 (0 ~ count-1)
 * @param item  新的计划项数据
 * @return ESP_OK 成功，否则失败
 */
esp_err_t feed_schedule_set_item(int index, const feed_schedule_item_t *item);

/**
 * @brief 添加一个新的投喂计划项
 * @param item 计划项数据
 * @return ESP_OK 成功，否则失败（已达最大数量）
 */
esp_err_t feed_schedule_add_item(const feed_schedule_item_t *item);

/**
 * @brief 删除指定索引的投喂计划项
 * @param index 索引
 * @return ESP_OK 成功，否则失败
 */
esp_err_t feed_schedule_remove_item(int index);

/**
 * @brief 启动投喂调度器后台任务
 *        每秒检查当前时间，如果匹配某个启用的投喂项，
 *        则调用注册的回调函数执行投喂。
 *        同一投喂项在1分钟内不会重复触发。
 *
 * @param cb 投喂触发回调函数 (不可为 NULL)
 */
void feeding_scheduler_start(feeding_callback_t cb);

/**
 * @brief 停止投喂调度器后台任务
 */
void feeding_scheduler_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* __FEEDING_SCHEDULE_H */