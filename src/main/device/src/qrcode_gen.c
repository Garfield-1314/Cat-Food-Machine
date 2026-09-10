#include "device/inc/qrcode_gen.h"

#include <stdio.h>
#include <time.h>
#include "esp_log.h"

static const char *TAG = "qrcode_gen";

esp_err_t qrcode_gen_create_bind_data(const char *device_id, const char *temp_token,
                                      char *output, size_t output_size)
{
    if (!device_id || !temp_token || !output || output_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    /* 生成 JSON 格式的绑定数据 */
    int len = snprintf(output, output_size,
                      "{\"d\":\"%s\",\"t\":\"%s\",\"ts\":%lld}",
                      device_id, temp_token, (long long)time(NULL));

    if (len < 0 || (size_t)len >= output_size) {
        ESP_LOGE(TAG, "Output buffer too small");
        return ESP_ERR_INVALID_SIZE;
    }

    ESP_LOGI(TAG, "Bind data generated for device %s (%d bytes)", device_id, len);
    return ESP_OK;
}
