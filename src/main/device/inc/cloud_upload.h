#ifndef __CLOUD_UPLOAD_H
#define __CLOUD_UPLOAD_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 启动远程截图任务
 *
 * 每 5 秒抓取一帧 JPEG，base64 后以 retained 消息发布到
 * device/<device_id>/image，供小程序远程模式轮询。
 *
 * @return ESP_OK 成功
 */
esp_err_t cloud_upload_start(void);

/**
 * @brief 停止远程截图任务（异步，任务退出时清除 retained 图片）
 */
void cloud_upload_stop(void);

/**
 * @brief 查询远程截图任务是否在运行
 * @return true 运行中
 */
bool cloud_upload_is_active(void);

#ifdef __cplusplus
}
#endif

#endif /* __CLOUD_UPLOAD_H */
