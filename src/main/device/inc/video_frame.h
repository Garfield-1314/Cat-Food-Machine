#ifndef __VIDEO_FRAME_H
#define __VIDEO_FRAME_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct video_frame video_frame_t;

/** @brief 创建按需视频帧读取器并获取摄像头采集使用权。 */
esp_err_t video_frame_create(video_frame_t **frame);

/**
 * @brief 复制一帧最新的原生 JPEG 图像。
 *
 * 返回的数据由读取器持有，在下一次调用或销毁读取器前有效。
 */
esp_err_t video_frame_get_jpeg(video_frame_t *frame,
                               const uint8_t **jpeg_data,
                               size_t *jpeg_size);

/** @brief 销毁读取器并释放摄像头采集使用权及缓冲区。 */
void video_frame_destroy(video_frame_t *frame);

#ifdef __cplusplus
}
#endif

#endif /* __VIDEO_FRAME_H */
