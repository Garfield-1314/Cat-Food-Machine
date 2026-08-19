#include "device/inc/video_stream.h"

#include <errno.h>
#include <stdio.h>
#include <sys/socket.h>

#include "device/inc/ov2640.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "video_stream";

#define VIDEO_STREAM_PORT               80
#define VIDEO_STREAM_FPS                10
#define VIDEO_STREAM_FRAME_INTERVAL_MS (1000 / VIDEO_STREAM_FPS)
#define VIDEO_STREAM_SEND_WAIT_SECONDS   2
#define VIDEO_STREAM_SEND_RETRIES        2
#define VIDEO_STREAM_SEND_RETRY_DELAY_MS 20
#define JPEG_COPY_ALIGNMENT              4096
#define JPEG_MAX_BYTES                  (CAM_OUTPUT_WIDTH * CAM_OUTPUT_HEIGHT)

static httpd_handle_t s_http_server = NULL;
static SemaphoreHandle_t s_client_mutex = NULL;
static bool s_stream_client_active = false;

static const char s_index_html[] =
    "<!doctype html>"
    "<html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>Cat Food Machine</title>"
    "<style>body{background:#102a38;color:#fff;font-family:sans-serif;text-align:center}"
    "img{max-width:100%;height:auto;image-rendering:auto}</style></head>"
    "<body><h2>Cat Food Machine</h2>"
    "<img id=\"stream\" src=\"/stream\" alt=\"Camera stream\">"
    "<script>const img=document.getElementById('stream');let timer;"
    "function reconnect(){clearTimeout(timer);timer=setTimeout(()=>{"
    "img.src='/stream?t='+Date.now()},1000)}"
    "img.addEventListener('error',reconnect);"
    "document.addEventListener('visibilitychange',()=>{clearTimeout(timer);"
    "if(document.hidden){img.removeAttribute('src')}else{reconnect()}});"
    "</script></body></html>";

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

/* httpd_resp_send_chunk() 在底层可能已发出一部分 chunk，因此不能
 * 在上层重发整个 chunk。在 session 的底层 send 回调中重试，
 * httpd_send_all() 才能正确继续尚未发出的部分。 */
static int stream_socket_send(httpd_handle_t handle, int sockfd,
                              const char *buf, size_t buf_len, int flags)
{
    (void)handle;
    int timeout_retries = 0;

    while (true) {
        int sent = send(sockfd, buf, buf_len, flags);
        if (sent > 0) {
            return sent;
        }
        if (sent == 0) {
            return HTTPD_SOCK_ERR_FAIL;
        }

        int send_errno = errno;
        if (send_errno == EINTR) {
            continue;
        }
        if (send_errno == EAGAIN || send_errno == EWOULDBLOCK) {
            if (timeout_retries < VIDEO_STREAM_SEND_RETRIES) {
                timeout_retries++;
                vTaskDelay(pdMS_TO_TICKS(VIDEO_STREAM_SEND_RETRY_DELAY_MS));
                continue;
            }
            ESP_LOGW(TAG, "stream client stalled after %d send retries",
                     VIDEO_STREAM_SEND_RETRIES);
            return HTTPD_SOCK_ERR_TIMEOUT;
        }
        if (send_errno == EINVAL || send_errno == EBADF ||
            send_errno == EFAULT || send_errno == ENOTSOCK) {
            return HTTPD_SOCK_ERR_INVALID;
        }
        return HTTPD_SOCK_ERR_FAIL;
    }
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
    uint8_t *jpeg_copy = NULL;
    size_t jpeg_capacity = 0;
    uint32_t last_frame_id = 0;
    bool camera_acquired = false;
    bool socket_send_failed = false;
    int stream_sockfd = -1;

    if (!stream_client_claim()) {
        return send_stream_error(req, "503 Service Unavailable",
                                 "Only one stream client is supported");
    }

    esp_err_t ret = ESP_OK;

    stream_sockfd = httpd_req_to_sockfd(req);
    if (stream_sockfd < 0) {
        ret = ESP_FAIL;
        goto cleanup;
    }
    ret = httpd_sess_set_send_override(req->handle, stream_sockfd,
                                        stream_socket_send);
    if (ret != ESP_OK) {
        goto cleanup;
    }

    ret = ov2640_camera_acquire();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "failed to acquire camera: %s", esp_err_to_name(ret));
        ret = send_stream_error(req, "503 Service Unavailable",
                                "Camera is unavailable");
        goto cleanup;
    }
    camera_acquired = true;

    ret = httpd_resp_set_type(req, "multipart/x-mixed-replace; boundary=frame");
    if (ret != ESP_OK) {
        goto cleanup;
    }
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    httpd_resp_set_hdr(req, "Connection", "close");

    const TickType_t frame_interval_ticks =
        pdMS_TO_TICKS(VIDEO_STREAM_FRAME_INTERVAL_MS);
    while (true) {
        TickType_t frame_cycle_start = xTaskGetTickCount();
        size_t jpeg_size = 0;
        uint32_t frame_id = 0;
        esp_err_t copy_ret;

        while (true) {
            copy_ret = ov2640_camera_copy_jpeg_frame(
                jpeg_copy, jpeg_capacity, &jpeg_size, &frame_id);
            if (copy_ret != ESP_ERR_INVALID_SIZE) {
                break;
            }
            if (jpeg_size == 0 || jpeg_size > JPEG_MAX_BYTES) {
                copy_ret = ESP_ERR_INVALID_SIZE;
                break;
            }

            size_t new_capacity =
                (jpeg_size + JPEG_COPY_ALIGNMENT - 1) &
                ~(JPEG_COPY_ALIGNMENT - 1);
            uint8_t *new_copy = heap_caps_realloc(
                jpeg_copy, new_capacity, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            if (new_copy == NULL) {
                copy_ret = ESP_ERR_NO_MEM;
                break;
            }
            jpeg_copy = new_copy;
            jpeg_capacity = new_capacity;
            ESP_LOGI(TAG, "JPEG send buffer allocated (%u bytes)",
                     (unsigned)jpeg_capacity);
        }

        if (copy_ret == ESP_ERR_NOT_FOUND ||
            (copy_ret == ESP_OK && frame_id == last_frame_id)) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        if (copy_ret != ESP_OK) {
            ESP_LOGW(TAG, "failed to copy camera frame: %s",
                     esp_err_to_name(copy_ret));
            ret = copy_ret;
            break;
        }
        last_frame_id = frame_id;

        char part_header[128];
        int header_len = snprintf(part_header, sizeof(part_header),
                                  "--frame\r\nContent-Type: image/jpeg\r\n"
                                  "Content-Length: %u\r\n\r\n",
                                  (unsigned)jpeg_size);
        esp_err_t send_ret = ESP_OK;
        if (jpeg_size == 0 || header_len <= 0 ||
            header_len >= (int)sizeof(part_header)) {
            send_ret = ESP_FAIL;
        } else {
            send_ret = httpd_resp_send_chunk(req, part_header, header_len);
            if (send_ret == ESP_OK) {
                send_ret = httpd_resp_send_chunk(req, (const char *)jpeg_copy,
                                                  jpeg_size);
            }
            if (send_ret == ESP_OK) {
                send_ret = httpd_resp_send_chunk(req, "\r\n", 2);
            }
        }

        if (send_ret != ESP_OK) {
            socket_send_failed = true;
            ret = send_ret;
            break;
        }

        /* 只限制最高帧率；一次发送变慢后不追赶、不补发旧帧。 */
        TickType_t elapsed = xTaskGetTickCount() - frame_cycle_start;
        if (elapsed < frame_interval_ticks) {
            vTaskDelay(frame_interval_ticks - elapsed);
        }
    }

    /* 客户端主动断开时发送结束 chunk 通常会失败，因此只清理资源。 */
    if (ret == ESP_OK) {
        httpd_resp_send_chunk(req, NULL, 0);
    }

cleanup:
    if (jpeg_copy != NULL) {
        heap_caps_free(jpeg_copy);
    }
    if (camera_acquired) {
        ov2640_camera_release();
    }
    stream_client_release();

    /* 发送错误是流式连接的正常退出路径。显式排队关闭 session，
     * 同时返回 ESP_OK，避免 HTTPD 再把它记为 URI 执行错误。 */
    if (socket_send_failed && stream_sockfd >= 0 &&
        httpd_sess_trigger_close(req->handle, stream_sockfd) == ESP_OK) {
        return ESP_OK;
    }
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
    config.send_wait_timeout = VIDEO_STREAM_SEND_WAIT_SECONDS;

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
