#include "device/inc/video_frame.h"

#include <stdlib.h>
#include <string.h>

#include "device/inc/ov2640.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "video_frame";

#define VIDEO_FIRST_FRAME_TIMEOUT_MS 2000
#define VIDEO_JPEG_BUFFER_BYTES       (CAM_OUTPUT_WIDTH * CAM_OUTPUT_HEIGHT)

struct video_frame {
    uint8_t *jpeg_buffer;
    size_t jpeg_buffer_size;
};

static bool wait_for_first_frame(void)
{
    TickType_t deadline = xTaskGetTickCount() +
                          pdMS_TO_TICKS(VIDEO_FIRST_FRAME_TIMEOUT_MS);

    while (ov2640_camera_get_jpeg_frame(NULL, NULL, NULL) == NULL) {
        if ((int32_t)(deadline - xTaskGetTickCount()) <= 0) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return true;
}

esp_err_t video_frame_create(video_frame_t **frame)
{
    if (frame == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *frame = NULL;

    if (!ov2640_camera_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }

    video_frame_t *instance = calloc(1, sizeof(*instance));
    if (instance == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = ov2640_camera_acquire();
    if (ret != ESP_OK) {
        free(instance);
        return ret;
    }

    instance->jpeg_buffer = heap_caps_malloc(
        VIDEO_JPEG_BUFFER_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (instance->jpeg_buffer == NULL) {
        instance->jpeg_buffer = heap_caps_malloc(
            VIDEO_JPEG_BUFFER_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (instance->jpeg_buffer == NULL) {
        ret = ESP_ERR_NO_MEM;
        goto cleanup_camera;
    }
    instance->jpeg_buffer_size = VIDEO_JPEG_BUFFER_BYTES;

    if (!wait_for_first_frame()) {
        ESP_LOGW(TAG, "camera did not produce a frame in time");
        ret = ESP_ERR_TIMEOUT;
        goto cleanup_buffer;
    }

    *frame = instance;
    return ESP_OK;

cleanup_buffer:
    free(instance->jpeg_buffer);
cleanup_camera:
    ov2640_camera_release();
    free(instance);
    return ret;
}

esp_err_t video_frame_get_jpeg(video_frame_t *frame,
                               const uint8_t **jpeg_data,
                               size_t *jpeg_size)
{
    if (frame == NULL || jpeg_data == NULL || jpeg_size == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t source_size = 0;
    const uint8_t *source = ov2640_camera_get_jpeg_frame(
        &source_size, NULL, NULL);
    if (source == NULL || source_size == 0) {
        return ESP_ERR_NOT_FOUND;
    }
    if (source_size > frame->jpeg_buffer_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    memcpy(frame->jpeg_buffer, source, source_size);
    *jpeg_data = frame->jpeg_buffer;
    *jpeg_size = source_size;
    return ESP_OK;
}

void video_frame_destroy(video_frame_t *frame)
{
    if (frame == NULL) {
        return;
    }
    free(frame->jpeg_buffer);
    ov2640_camera_release();
    free(frame);
}
