#include "device/inc/jpeg_preview.h"

#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp32s3/rom/tjpgd.h"

static const char *TAG = "jpeg_preview";

#define JPEG_PREVIEW_WORK_BUFFER_BYTES 4096

typedef struct {
    const uint8_t *data;
    size_t size;
    size_t offset;
} jpeg_input_t;

typedef struct {
    uint16_t *output;
    uint16_t width;
    uint16_t height;
} jpeg_output_t;

typedef struct {
    jpeg_input_t input;
    jpeg_output_t output;
} jpeg_decode_context_t;

struct jpeg_preview {
    uint16_t width;
    uint16_t height;
    uint8_t *input;
    size_t input_capacity;
    uint8_t *work_buffer;
    uint16_t *output;
};

static UINT jpeg_input_func(JDEC *decoder, BYTE *buffer, UINT length)
{
    jpeg_decode_context_t *context = decoder->device;
    jpeg_input_t *input = &context->input;
    size_t available = input->size - input->offset;
    size_t to_read = length < available ? length : available;

    if (buffer != NULL && to_read > 0) {
        memcpy(buffer, input->data + input->offset, to_read);
    }
    input->offset += to_read;
    return (UINT)to_read;
}

static UINT jpeg_output_func(JDEC *decoder, void *bitmap, JRECT *rect)
{
    jpeg_decode_context_t *context = decoder->device;
    jpeg_output_t *output = &context->output;
    const uint8_t *rgb888 = bitmap;
    const uint16_t rect_width = rect->right - rect->left + 1;

    for (uint16_t y = rect->top; y <= rect->bottom; y++) {
        uint16_t *destination = output->output +
            (size_t)y * output->width + rect->left;
        for (uint16_t x = 0; x < rect_width; x++) {
            uint8_t red = rgb888[0];
            uint8_t green = rgb888[1];
            uint8_t blue = rgb888[2];
            uint16_t rgb565 = ((uint16_t)(red & 0xF8) << 8) |
                              ((uint16_t)(green & 0xFC) << 3) |
                              (blue >> 3);
            /* LV_COLOR_16_SWAP=y：LCD SPI 需要高低字节交换后的内存布局。 */
            destination[x] = (uint16_t)((rgb565 >> 8) | (rgb565 << 8));
            rgb888 += 3;
        }
    }
    return 1;
}

static void *alloc_buffer(size_t size, uint32_t caps)
{
    void *buffer = heap_caps_malloc(size, caps);
    if (buffer == NULL && (caps & MALLOC_CAP_SPIRAM) != 0) {
        buffer = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return buffer;
}

esp_err_t jpeg_preview_create(jpeg_preview_t **preview,
                              uint16_t width, uint16_t height)
{
    if (preview == NULL || width == 0 || height == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    *preview = NULL;

    jpeg_preview_t *instance = calloc(1, sizeof(*instance));
    if (instance == NULL) {
        return ESP_ERR_NO_MEM;
    }
    instance->width = width;
    instance->height = height;
    instance->input_capacity = (size_t)width * height;

    instance->input = alloc_buffer(instance->input_capacity,
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    instance->work_buffer = alloc_buffer(JPEG_PREVIEW_WORK_BUFFER_BYTES,
                                          MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    instance->output = alloc_buffer((size_t)width * height * sizeof(uint16_t),
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (instance->input == NULL || instance->work_buffer == NULL ||
        instance->output == NULL) {
        ESP_LOGE(TAG, "failed to allocate preview buffers");
        jpeg_preview_destroy(instance);
        return ESP_ERR_NO_MEM;
    }

    *preview = instance;
    return ESP_OK;
}

esp_err_t jpeg_preview_decode(jpeg_preview_t *preview,
                              const uint8_t *jpeg_data,
                              size_t jpeg_size,
                              const uint8_t **rgb565_data,
                              size_t *rgb565_size)
{
    if (preview == NULL || jpeg_data == NULL || jpeg_size == 0 ||
        rgb565_data == NULL || rgb565_size == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (jpeg_size > preview->input_capacity) {
        return ESP_ERR_INVALID_SIZE;
    }

    memcpy(preview->input, jpeg_data, jpeg_size);

    jpeg_decode_context_t context = {
        .input = {
            .data = preview->input,
            .size = jpeg_size,
            .offset = 0,
        },
        .output = {
            .output = preview->output,
            .width = preview->width,
            .height = preview->height,
        },
    };
    JDEC decoder;
    JRESULT result = jd_prepare(&decoder, jpeg_input_func,
                                preview->work_buffer,
                                JPEG_PREVIEW_WORK_BUFFER_BYTES, &context);
    if (result != JDR_OK || decoder.width != preview->width ||
        decoder.height != preview->height) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    result = jd_decomp(&decoder, jpeg_output_func, 0);
    if (result != JDR_OK) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    *rgb565_data = (const uint8_t *)preview->output;
    *rgb565_size = (size_t)preview->width * preview->height * sizeof(uint16_t);
    return ESP_OK;
}

void jpeg_preview_destroy(jpeg_preview_t *preview)
{
    if (preview == NULL) {
        return;
    }
    free(preview->input);
    free(preview->work_buffer);
    free(preview->output);
    free(preview);
}
