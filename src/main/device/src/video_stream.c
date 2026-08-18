#include "device/inc/video_stream.h"

#include <stdio.h>

#include "device/inc/ov2640.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "video_stream";

#define VIDEO_STREAM_PORT               80
#define VIDEO_STREAM_FPS                10
#define VIDEO_STREAM_FRAME_INTERVAL_MS (1000 / VIDEO_STREAM_FPS)

static httpd_handle_t s_http_server = NULL;
static SemaphoreHandle_t s_client_mutex = NULL;
static bool s_stream_client_active = false;

static const char s_index_html[] =
    "<!doctype html>"
    "<html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>Cat Food Machine</title>"
    "<style>body{background:#102a38;color:#fff;font-family:sans-serif;text-align:center}"
    "img{max-width:100%;height:auto;image-rendering:auto}</style></head>"
    "<body><h2>Cat Food Machine</h2><img src=\"/stream\" alt=\"Camera stream\"></body></html>";

static bool stream_client_claim(void)
{
    bool claimed = false;

    xSemaphoreTake(s_client_mutex, portMAX_DELAY);
    if (!s_stream_client_active) {
        s_stream_client_active = true;
        claimed = true;
    }
    xSemaphoreGive(s_client_mutex);

    return claimed;
}

static void stream_client_release(void)
{
    xSemaphoreTake(s_client_mutex, portMAX_DELAY);
    s_stream_client_active = false;
    xSemaphoreGive(s_client_mutex);
}

static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    return httpd_resp_send(req, s_index_html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t send_stream_error(httpd_req_t *req, const char *status,
                                   const char *message)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    esp_err_t ret = httpd_resp_send(req, message, HTTPD_RESP_USE_STRLEN);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "failed to send HTTP error %s: %s", status,
                 esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t stream_handler(httpd_req_t *req)
{
    if (!stream_client_claim()) {
        return send_stream_error(req, "503 Service Unavailable",
                                 "Only one stream client is supported");
    }

    esp_err_t ret = ESP_OK;

    ret = ov2640_camera_acquire();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "failed to acquire camera: %s", esp_err_to_name(ret));
        ret = send_stream_error(req, "503 Service Unavailable",
                                "Camera is unavailable");
        goto cleanup;
    }

    ret = httpd_resp_set_type(req, "multipart/x-mixed-replace; boundary=frame");
    if (ret != ESP_OK) {
        goto cleanup;
    }
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    httpd_resp_set_hdr(req, "Connection", "close");

    TickType_t next_frame_tick = xTaskGetTickCount();
    while (true) {
        const uint8_t *jpeg_data = NULL;
        size_t jpeg_size = 0;
        /* 直接引用相机帧缓冲（单缓冲），省去拷贝；覆盖窗口极小可接受 */
        jpeg_data = ov2640_camera_get_jpeg_frame(&jpeg_size, NULL, NULL);
        if (jpeg_data == NULL || jpeg_size == 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        char part_header[128];
        int header_len = snprintf(part_header, sizeof(part_header),
                                  "--frame\r\nContent-Type: image/jpeg\r\n"
                                  "Content-Length: %u\r\n\r\n",
                                  (unsigned)jpeg_size);
        if (header_len <= 0 || header_len >= (int)sizeof(part_header) ||
            httpd_resp_send_chunk(req, part_header, header_len) != ESP_OK ||
            httpd_resp_send_chunk(req, (const char *)jpeg_data, jpeg_size) != ESP_OK ||
            httpd_resp_send_chunk(req, "\r\n", 2) != ESP_OK) {
            ret = ESP_FAIL;
            break;
        }

        vTaskDelayUntil(&next_frame_tick,
                        pdMS_TO_TICKS(VIDEO_STREAM_FRAME_INTERVAL_MS));
    }

    /* 客户端主动断开时发送结束 chunk 通常会失败，因此只清理资源。 */
    if (ret == ESP_OK) {
        httpd_resp_send_chunk(req, NULL, 0);
    }

cleanup:
    ov2640_camera_release();
    stream_client_release();
    return ret;
}

esp_err_t video_stream_start(void)
{
    if (s_http_server != NULL) {
        return ESP_OK;
    }

    if (s_client_mutex == NULL) {
        s_client_mutex = xSemaphoreCreateMutex();
        if (s_client_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = VIDEO_STREAM_PORT;
    config.max_open_sockets = 2;
    config.max_uri_handlers = 2;

    esp_err_t ret = httpd_start(&s_http_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to start HTTP server: %s", esp_err_to_name(ret));
        s_http_server = NULL;
        return ret;
    }

    static const httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_handler,
        .user_ctx = NULL,
    };
    static const httpd_uri_t stream_uri = {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = stream_handler,
        .user_ctx = NULL,
    };

    ret = httpd_register_uri_handler(s_http_server, &index_uri);
    if (ret == ESP_OK) {
        ret = httpd_register_uri_handler(s_http_server, &stream_uri);
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to register HTTP routes: %s", esp_err_to_name(ret));
        httpd_stop(s_http_server);
        s_http_server = NULL;
        return ret;
    }

    ESP_LOGI(TAG, "HTTP-MJPEG server started on port %d", VIDEO_STREAM_PORT);
    return ESP_OK;
}

esp_err_t video_stream_stop(void)
{
    if (s_http_server == NULL) {
        return ESP_OK;
    }

    esp_err_t ret = httpd_stop(s_http_server);
    s_http_server = NULL;
    return ret;
}
