#include "device/inc/qrcode_gen.h"

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "lvgl.h"

static const char *TAG = "qrcode_gen";

/* QR 码编码表 (简化版本，实际应用需要完整的 Reed-Solomon 编码) */
/* 这里使用一个简化的实现，实际项目中建议使用成熟的 QR 码库 */

/* 简化的 QR 码生成函数 */
static int get_qrcode_version(int data_len)
{
    /* 简化的版本计算，实际需要根据数据长度和纠错级别计算 */
    if (data_len <= 17) return 1;
    if (data_len <= 32) return 2;
    if (data_len <= 53) return 3;
    if (data_len <= 78) return 4;
    if (data_len <= 106) return 5;
    return 6;
}

static int get_qrcode_size(int version)
{
    return 17 + 4 * version;
}

/* 创建 QR 码矩阵 */
static uint8_t *create_qrcode_matrix(int size)
{
    int total = size * size;
    uint8_t *matrix = (uint8_t *)heap_caps_malloc(total, MALLOC_CAP_DEFAULT);
    if (matrix) {
        memset(matrix, 0, total);
    }
    return matrix;
}

/* 设置 QR 码模块 */
static void set_module(uint8_t *matrix, int size, int x, int y, bool value)
{
    if (x >= 0 && x < size && y >= 0 && y < size) {
        matrix[y * size + x] = value ? 1 : 0;
    }
}

/* 获取 QR 码模块 */
static bool get_module(const uint8_t *matrix, int size, int x, int y)
{
    if (x >= 0 && x < size && y >= 0 && y < size) {
        return matrix[y * size + x] != 0;
    }
    return false;
}

/* 添加定位图案 */
static void add_finder_pattern(uint8_t *matrix, int size, int x, int y)
{
    for (int dy = -1; dy <= 7; dy++) {
        for (int dx = -1; dx <= 7; dx++) {
            int px = x + dx;
            int py = y + dy;
            if (px < 0 || px >= size || py < 0 || py >= size) continue;

            bool value = false;
            if (dx >= 0 && dx <= 6 && (dy == 0 || dy == 6)) value = true;
            if (dy >= 0 && dy <= 6 && (dx == 0 || dx == 6)) value = true;
            if (dx >= 2 && dx <= 4 && dy >= 2 && dy <= 4) value = true;

            set_module(matrix, size, px, py, value);
        }
    }
}

/* 添加时序图案 */
static void add_timing_pattern(uint8_t *matrix, int size)
{
    for (int i = 8; i < size - 8; i++) {
        bool value = (i % 2 == 0);
        set_module(matrix, size, i, 6, value);
        set_module(matrix, size, 6, i, value);
    }
}

/* 简化的 QR 码生成 (仅用于演示，实际项目需要完整的 QR 码库) */
static esp_err_t generate_simple_qrcode(qrcode_t *qr, const char *data)
{
    int data_len = strlen(data);
    int version = get_qrcode_version(data_len);
    int size = get_qrcode_size(version);

    qr->size = size;
    qr->scale = 4; /* 默认缩放 */
    qr->data = create_qrcode_matrix(size);
    if (!qr->data) {
        ESP_LOGE(TAG, "Failed to allocate QR matrix");
        return ESP_ERR_NO_MEM;
    }

    /* 添加定位图案 */
    add_finder_pattern(qr->data, size, 0, 0);
    add_finder_pattern(qr->data, size, size - 7, 0);
    add_finder_pattern(qr->data, size, 0, size - 7);

    /* 添加时序图案 */
    add_timing_pattern(qr->data, size);

    /* 注意：这里只是创建了一个基本的 QR 码结构 */
    /* 实际项目中需要：
     * 1. 数据编码
     * 2. 纠错编码
     * 3. 掩码图案
     * 4. 格式信息
     * 建议使用成熟的 QR 码生成库
     */

    ESP_LOGI(TAG, "Generated QR code: version=%d, size=%d", version, size);
    return ESP_OK;
}

esp_err_t qrcode_gen_init(void)
{
    ESP_LOGI(TAG, "QR code generator initialized");
    return ESP_OK;
}

esp_err_t qrcode_gen_create(qrcode_t *qr, const char *data, int ecc_level)
{
    if (!qr || !data) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Creating QR code for: %s", data);
    return generate_simple_qrcode(qr, data);
}

void qrcode_gen_free(qrcode_t *qr)
{
    if (qr && qr->data) {
        heap_caps_free(qr->data);
        qr->data = NULL;
        qr->size = 0;
    }
}

void qrcode_gen_draw_lvgl(const qrcode_t *qr, void *canvas, int x, int y,
                          uint32_t color, uint32_t bg_color)
{
    if (!qr || !canvas || !qr->data) return;

    lv_obj_t *canvas_obj = (lv_obj_t *)canvas;
    int scale = qr->scale;

    /* 绘制背景 */
    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.bg_color = lv_color_hex(bg_color);
    rect_dsc.bg_opa = LV_OPA_COVER;
    rect_dsc.border_width = 0;

    /* 绘制二维码模块 */
    for (int py = 0; py < qr->size; py++) {
        for (int px = 0; px < qr->size; px++) {
            if (get_module(qr->data, qr->size, px, py)) {
                rect_dsc.bg_color = lv_color_hex(color);
            } else {
                rect_dsc.bg_color = lv_color_hex(bg_color);
            }

            lv_area_t coords = {
                .x1 = x + px * scale,
                .y1 = y + py * scale,
                .x2 = x + (px + 1) * scale - 1,
                .y2 = y + (py + 1) * scale - 1
            };

            lv_draw_rect(&coords, &rect_dsc);
        }
    }
}

esp_err_t qrcode_gen_create_bind_data(const char *device_id, const char *temp_token,
                                      char *output, size_t output_size)
{
    if (!device_id || !temp_token || !output) {
        return ESP_ERR_INVALID_ARG;
    }

    /* 生成 JSON 格式的绑定数据 */
    int len = snprintf(output, output_size,
                      "{\"d\":\"%s\",\"t\":\"%s\",\"ts\":%lld}",
                      device_id, temp_token, (long long)time(NULL));

    if (len >= output_size) {
        ESP_LOGE(TAG, "Output buffer too small");
        return ESP_ERR_INVALID_SIZE;
    }

    ESP_LOGI(TAG, "Bind data: %s", output);
    return ESP_OK;
}
