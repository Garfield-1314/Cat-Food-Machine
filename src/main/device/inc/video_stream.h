#ifndef __VIDEO_STREAM_H
#define __VIDEO_STREAM_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 启动局域网 HTTP 视频服务
 *
 * 仅启动 HTTP 服务，不会启动摄像头采集。客户端访问 /stream 后才会
 * 获取摄像头资源。
 */
esp_err_t video_stream_start(void);

/**
 * @brief 停止局域网 HTTP 视频服务
 */
esp_err_t video_stream_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* __VIDEO_STREAM_H */
