#ifndef __JPEG_PREVIEW_H
#define __JPEG_PREVIEW_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct jpeg_preview jpeg_preview_t;

/** @brief 创建 LCD JPEG 预览解码器。 */
esp_err_t jpeg_preview_create(jpeg_preview_t **preview,
                              uint16_t width, uint16_t height);

/**
 * @brief 解码一帧 JPEG 并返回 RGB565 图像。
 *
 * 返回的数据由解码器持有，在下一次调用或销毁解码器前有效。
 */
esp_err_t jpeg_preview_decode(jpeg_preview_t *preview,
                              const uint8_t *jpeg_data,
                              size_t jpeg_size,
                              const uint8_t **rgb565_data,
                              size_t *rgb565_size);

/** @brief 销毁 LCD JPEG 预览解码器。 */
void jpeg_preview_destroy(jpeg_preview_t *preview);

#ifdef __cplusplus
}
#endif

#endif /* __JPEG_PREVIEW_H */
