#ifndef __QRCODE_GEN_H
#define __QRCODE_GEN_H

#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 生成设备绑定二维码数据
 * @param device_id 设备ID
 * @param temp_token 临时token
 * @param output 输出缓冲区
 * @param output_size 缓冲区大小
 * @return ESP_OK 成功
 */
esp_err_t qrcode_gen_create_bind_data(const char *device_id, const char *temp_token,
                                      char *output, size_t output_size);

#ifdef __cplusplus
}
#endif

#endif /* __QRCODE_GEN_H */
