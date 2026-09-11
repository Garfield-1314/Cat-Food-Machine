#include "device/inc/cloud_upload.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "device/inc/mqtt_client.h"
#include "device/inc/ov2640.h"
#include "device/inc/secure_msg.h"

static const char *TAG = "cloud_upload";

#define CAPTURE_INTERVAL_MS   5000
#define CAPTURE_TASK_STACK    6144
#define CAPTURE_TASK_PRIO     4
#define CAPTURE_INITIAL_JPEG  (32 * 1024)
#define CAPTURE_MAX_JPEG      (256 * 1024)
#define IMAGE_TOPIC_FMT       "device/%s/image"

static TaskHandle_t s_capture_task = NULL;
static volatile bool s_running = false;

static uint8_t *alloc_buffer(size_t capacity)
{
#ifdef CONFIG_SPIRAM
    uint8_t *buffer = heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buffer != NULL) {
        return buffer;
    }
#endif
    return heap_caps_malloc(capacity, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

static void capture_task(void *arg)
{
    (void)arg;

    const device_info_t *dev = mqtt_client_get_device_info();
    char topic[128];
    snprintf(topic, sizeof(topic), IMAGE_TOPIC_FMT, dev->device_id);

    if (!dev->bound || dev->user_id[0] == '\0') {
        ESP_LOGW(TAG, "device not bound, capture aborted");
        s_running = false;
        s_capture_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    if (ov2640_camera_acquire() != ESP_OK) {
        ESP_LOGW(TAG, "camera acquire failed, capture aborted");
        s_running = false;
        s_capture_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    uint8_t *jpeg = alloc_buffer(CAPTURE_INITIAL_JPEG);
    size_t jpeg_cap = jpeg != NULL ? CAPTURE_INITIAL_JPEG : 0;

    while (s_running) {
        size_t jpeg_size = 0;
        uint32_t frame_id = 0;
        esp_err_t ret = ov2640_camera_copy_jpeg_frame(jpeg, jpeg_cap,
                                                      &jpeg_size, &frame_id);

        if (ret == ESP_ERR_INVALID_SIZE && jpeg_size > 0 &&
            jpeg_size <= CAPTURE_MAX_JPEG) {
            size_t new_cap = (jpeg_size + 63) & ~(size_t)63;
            uint8_t *new_jpeg = alloc_buffer(new_cap);
            if (new_jpeg != NULL) {
                heap_caps_free(jpeg);
                jpeg = new_jpeg;
                jpeg_cap = new_cap;
                continue;  /* 立即用新缓冲重试 */
            }
            ret = ESP_ERR_NO_MEM;
        }

        if (ret == ESP_ERR_NOT_FOUND) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "copy frame failed: %s", esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        /* 加密后再发布：公共 broker 上无法读取画面 */
        char *payload = secure_msg_pack_buffer(dev->temp_token, dev->user_id,
                                               jpeg, jpeg_size);
        if (payload != NULL) {
            mqtt_client_publish_ex(topic, payload, 1, true);
            ESP_LOGI(TAG, "snapshot published: jpeg=%u payload=%u bytes",
                     (unsigned)jpeg_size, (unsigned)strlen(payload));
            free(payload);
        } else {
            ESP_LOGW(TAG, "snapshot encrypt failed");
        }

        /* 分段延时，便于及时响应 stop */
        for (int i = 0; i < CAPTURE_INTERVAL_MS / 100 && s_running; i++) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    /* 清除 retained 图片并释放资源 */
    mqtt_client_publish_ex(topic, "", 1, true);
    ov2640_camera_release();
    heap_caps_free(jpeg);
    s_capture_task = NULL;
    ESP_LOGI(TAG, "capture task stopped");
    vTaskDelete(NULL);
}

esp_err_t cloud_upload_start(void)
{
    if (s_capture_task != NULL) {
        return ESP_OK;
    }

    s_running = true;
    if (xTaskCreate(capture_task, "cloud_capture", CAPTURE_TASK_STACK, NULL,
                    CAPTURE_TASK_PRIO, &s_capture_task) != pdPASS) {
        s_running = false;
        s_capture_task = NULL;
        ESP_LOGE(TAG, "failed to create capture task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "capture task started");
    return ESP_OK;
}

void cloud_upload_stop(void)
{
    if (s_capture_task == NULL) {
        return;
    }
    s_running = false;
    ESP_LOGI(TAG, "capture stop requested");
}

bool cloud_upload_is_active(void)
{
    return s_running;
}
