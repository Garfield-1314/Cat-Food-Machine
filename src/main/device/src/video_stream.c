#include "device/inc/video_stream.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "device/inc/sntp_time.h"
#include "device/inc/ov2640.h"
#include "device/inc/wifi_app.h"
#include "driver/inc/feeding_control.h"
#include "driver/inc/feeder_motor.h"
#include "driver/inc/feeding_schedule.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "video_stream";

#define VIDEO_STREAM_PORT               80
/* 调试时可直接修改该参数并重新编译；当前默认固定 10 FPS。 */
#define VIDEO_STREAM_FPS                10
#define VIDEO_STREAM_FRAME_INTERVAL_MS (1000 / VIDEO_STREAM_FPS)
#define VIDEO_STREAM_SEND_WAIT_SECONDS   2
#define JPEG_COPY_ALIGNMENT              4096
#define JPEG_MAX_BYTES                  CAM_JPEG_BUFFER_BYTES
#define VIDEO_STREAM_TASK_STACK_SIZE     6144
#define VIDEO_STREAM_TASK_PRIORITY       5

static httpd_handle_t s_http_server = NULL;
static SemaphoreHandle_t s_client_mutex = NULL;
static bool s_stream_client_active = false;

static const char s_index_html[] =
    "<!doctype html>"
    "<html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Cat Food Machine</title>"
    "<style>"
    ":root{color-scheme:dark}*{box-sizing:border-box}body{margin:0;background:#102a38;"
    "color:#fff;font-family:system-ui,-apple-system,sans-serif}main{max-width:760px;"
    "margin:auto;padding:16px}header{display:flex;justify-content:space-between;"
    "align-items:center;gap:12px}h1{font-size:24px;margin:0 0 12px}h2{font-size:18px;"
    "margin:0 0 12px}.card{background:#173d50;border:1px solid #28627a;"
    "border-radius:10px;padding:14px;margin:12px 0}.muted{color:#a9bec7}.status{"
    "color:#9ff0bd;min-height:20px}.error{color:#ff9b9b}.row{display:flex;"
    "flex-wrap:wrap;gap:8px;align-items:center}.schedule{background:#123446;"
    "border:1px solid #28627a;border-radius:8px;padding:10px;margin:8px 0}"
    "label{font-size:12px;color:#b8cbd2;display:flex;flex-direction:column;"
    "gap:3px}input,select,button{font:inherit;border-radius:6px;border:1px solid #467d91;"
    "padding:7px;background:#0c2938;color:#fff}input{width:72px}button{cursor:pointer;"
    "background:#087f62;border-color:#28b88e}button.secondary{background:#245b73}"
    "button.danger{background:#8c3440;border-color:#d26b78}button:disabled{opacity:.5;"
    "cursor:not-allowed}img{display:block;width:100%;height:auto;border-radius:8px;"
    "background:#06151d;min-height:120px}.topline{color:#b8cbd2;font-size:14px}"
    "</style></head>"
    "<body><main><header><h1>Cat Food Machine</h1><div id='wifi' class='topline'>WiFi: --</div></header>"
    "<section class='card'><h2>Manual feed</h2><div class='row'><label>Slots"
    "<select id='feedAmount'><option>1</option><option>2</option><option>3</option>"
    "<option>4</option><option>5</option><option>6</option></select></label>"
    "<button id='feedBtn'>Feed now</button></div><p id='feedStatus' class='status'></p></section>"
    "<section class='card'><h2>Feeding schedules</h2><div id='scheduleList'></div>"
    "<div class='row'><button id='addBtn' class='secondary'>+ Add schedule</button>"
    "<button id='saveBtn'>Save schedules</button></div><p id='scheduleStatus' class='status'></p></section>"
    "<section class='card'><h2>Camera</h2><img id='stream' src='/stream' alt='Camera stream'>"
    "<p class='muted'>LAN only. Do not expose this device to the public Internet.</p></section>"
    "<p id='deviceStatus' class='topline'></p></main>"
    "<script>"
    "const $=id=>document.getElementById(id);let schedules=[],version=0;"
    "async function api(path,options={}){const r=await fetch(path,{cache:'no-store',...options});"
    "const t=await r.text();let d={};try{d=JSON.parse(t)}catch(e){}if(!r.ok){"
    "const x=new Error(d.message||t||r.statusText);x.status=r.status;x.data=d;throw x}return d}"
    "function message(el,text,error=false){el.textContent=text;el.className=error?'status error':'status'}"
    "function renderSchedules(){const box=$('scheduleList');box.innerHTML='';"
    "if(!schedules.length){box.innerHTML=\"<div class='muted'>No feeding schedules.</div>\";return}"
    "schedules.forEach((x,i)=>{const row=document.createElement('div');row.className='schedule';"
    "row.dataset.index=i;row.innerHTML=`<div class='row'><label>Hour<input data-field='hour' "
    "type='number' min='0' max='23' value='${x.hour}'></label><label>Minute<input data-field='minute' "
    "type='number' min='0' max='59' value='${x.minute}'></label><label>Slots<input data-field='amount' "
    "type='number' min='1' max='6' value='${x.amount}'></label><label>Every days<input data-field='every_days' "
    "type='number' min='1' max='7' value='${x.every_days}'></label><label>Enabled<input data-field='enabled' "
    "type='checkbox' ${x.enabled?'checked':''}></label><button data-remove='1' class='danger'>Delete</button></div>`;"
    "row.querySelectorAll('[data-field]').forEach(el=>el.onchange=()=>{const f=el.dataset.field;"
    "schedules[i][f]=f==='enabled'?el.checked:Number(el.value)});row.querySelector('[data-remove]').onclick=()=>{"
    "schedules.splice(i,1);renderSchedules()};box.appendChild(row)})}"
    "async function loadSchedules(){try{const d=await api('/api/schedules');schedules=d.items||[];"
    "version=d.version||0;renderSchedules();message($('scheduleStatus'),'Loaded')}catch(e){message($('scheduleStatus'),e.message,true)}}"
    "$('addBtn').onclick=()=>{if(schedules.length>=8){message($('scheduleStatus'),'Maximum 8 schedules',true);return}"
    "schedules.push({hour:8,minute:0,amount:1,enabled:true,every_days:1});renderSchedules()};"
    "$('saveBtn').onclick=async()=>{try{const d=await api('/api/schedules',{method:'PUT',headers:{'Content-Type':'application/json'},"
    "body:JSON.stringify({version:version,items:schedules})});schedules=d.items||[];version=d.version||version;"
    "renderSchedules();message($('scheduleStatus'),'Schedules saved')}catch(e){if(e.status===409)await loadSchedules();"
    "message($('scheduleStatus'),e.message,true)}};"
    "$('feedBtn').onclick=async()=>{const amount=Number($('feedAmount').value);if(!confirm('Feed '+amount+' slot(s) now?'))return;"
    "try{await api('/api/feed',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({amount})});"
    "message($('feedStatus'),'Feeding started')}catch(e){message($('feedStatus'),e.message,true)}};"
    "async function refreshStatus(){try{const d=await api('/api/status');const w=d.wifi||{};"
    "$('wifi').textContent='WiFi: '+(w.connected?(w.ssid||'connected'):'not connected')+(w.ip?' ('+w.ip+')':'');"
    "const f=d.feeding||{};$('feedBtn').disabled=!!f.active;$('feedBtn').textContent=f.active?'Feeding...':'Feed now';"
    "const n=d.next_feed||{};$('deviceStatus').textContent=(d.time_synced?'Time: '+(d.local_time||'--'):'Time is not synchronized')+"
    "(n.available?' | Next feed: '+n.local_time:' | No enabled schedule');if(f.active)message($('feedStatus'),"
    "'Feeding '+f.amount+' slot(s)...')}catch(e){$('deviceStatus').textContent='Status unavailable'}}}"
    "const img=$('stream');let timer,delay=1000;function reconnect(){clearTimeout(timer);timer=setTimeout(()=>{"
    "img.src='/stream?t='+Date.now();delay=Math.min(delay*2,10000)},delay)}img.onload=()=>delay=1000;"
    "img.onerror=reconnect;document.addEventListener('visibilitychange',()=>{clearTimeout(timer);"
    "if(document.hidden)img.removeAttribute('src');else reconnect()});loadSchedules();refreshStatus();"
    "setInterval(refreshStatus,1000);</script></body></html>";

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

/* ========== Web control API ========== */

#define WEB_API_BODY_MAX_BYTES 4096

static esp_err_t send_json_response(httpd_req_t *req, const char *status,
                                    cJSON *root)
{
    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (payload == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (status != NULL) {
        httpd_resp_set_status(req, status);
    }
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t ret = httpd_resp_send(req, payload, HTTPD_RESP_USE_STRLEN);
    free(payload);
    return ret;
}

static esp_err_t send_api_error(httpd_req_t *req, const char *status,
                                const char *code, const char *message)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(root, "error", code);
    cJSON_AddStringToObject(root, "message", message);
    return send_json_response(req, status, root);
}

static esp_err_t read_json_body(httpd_req_t *req, char **body)
{
    if (body == NULL || req->content_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (req->content_len > WEB_API_BODY_MAX_BYTES) {
        return ESP_ERR_INVALID_SIZE;
    }

    char *buffer = malloc(req->content_len + 1);
    if (buffer == NULL) {
        return ESP_ERR_NO_MEM;
    }

    size_t received = 0;
    while (received < req->content_len) {
        int ret = httpd_req_recv(req, buffer + received,
                                 req->content_len - received);
        if (ret <= 0) {
            free(buffer);
            return ESP_FAIL;
        }
        received += (size_t)ret;
    }
    buffer[received] = '\0';
    *body = buffer;
    return ESP_OK;
}

static bool json_integer_in_range(const cJSON *value, int min, int max,
                                  int *output)
{
    if (!cJSON_IsNumber(value) || !isfinite(value->valuedouble) ||
        value->valuedouble < min || value->valuedouble > max ||
        floor(value->valuedouble) != value->valuedouble) {
        return false;
    }
    if (output != NULL) {
        *output = (int)value->valuedouble;
    }
    return true;
}

static bool json_schedule_item(const cJSON *value,
                               feed_schedule_item_t *item)
{
    if (!cJSON_IsObject(value) || item == NULL) {
        return false;
    }

    int hour;
    int minute;
    int amount;
    int every_days;
    cJSON *hour_json = cJSON_GetObjectItemCaseSensitive(value, "hour");
    cJSON *minute_json = cJSON_GetObjectItemCaseSensitive(value, "minute");
    cJSON *amount_json = cJSON_GetObjectItemCaseSensitive(value, "amount");
    cJSON *enabled_json = cJSON_GetObjectItemCaseSensitive(value, "enabled");
    cJSON *every_json = cJSON_GetObjectItemCaseSensitive(value, "every_days");

    if (!json_integer_in_range(hour_json, 0, 23, &hour) ||
        !json_integer_in_range(minute_json, 0, 59, &minute) ||
        !json_integer_in_range(amount_json, 1, 6, &amount) ||
        !json_integer_in_range(every_json, 1, MAX_EVERY_DAYS, &every_days) ||
        !cJSON_IsBool(enabled_json)) {
        return false;
    }

    item->hour = (uint8_t)hour;
    item->minute = (uint8_t)minute;
    item->amount = (uint8_t)amount;
    item->enabled = cJSON_IsTrue(enabled_json);
    item->every_days = (uint8_t)every_days;
    return true;
}

static void schedule_item_to_json(cJSON *array,
                                  const feed_schedule_item_t *item)
{
    cJSON *object = cJSON_CreateObject();
    if (object == NULL) {
        return;
    }
    cJSON_AddNumberToObject(object, "hour", item->hour);
    cJSON_AddNumberToObject(object, "minute", item->minute);
    cJSON_AddNumberToObject(object, "amount", item->amount);
    cJSON_AddBoolToObject(object, "enabled", item->enabled);
    cJSON_AddNumberToObject(object, "every_days", item->every_days);
    cJSON_AddItemToArray(array, object);
}

static esp_err_t build_schedules_json(cJSON **root_out)
{
    feed_schedule_item_t items[MAX_SCHEDULE_ITEMS];
    size_t count = 0;
    uint32_t version = 0;
    esp_err_t ret = feed_schedule_get_snapshot(items, MAX_SCHEDULE_ITEMS,
                                               &count, &version);
    if (ret != ESP_OK) {
        return ret;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *array = cJSON_CreateArray();
    if (root == NULL || array == NULL) {
        cJSON_Delete(root);
        cJSON_Delete(array);
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddNumberToObject(root, "version", version);
    cJSON_AddNumberToObject(root, "max_items", MAX_SCHEDULE_ITEMS);
    cJSON_AddNumberToObject(root, "max_slots", 6);
    cJSON_AddItemToObject(root, "items", array);
    for (size_t i = 0; i < count; i++) {
        schedule_item_to_json(array, &items[i]);
    }
    *root_out = root;
    return ESP_OK;
}

static esp_err_t schedules_get_handler(httpd_req_t *req)
{
    cJSON *root = NULL;
    esp_err_t ret = build_schedules_json(&root);
    if (ret != ESP_OK) {
        return send_api_error(req, "500 Internal Server Error", "status_error",
                               "Failed to read schedules");
    }
    return send_json_response(req, NULL, root);
}

static esp_err_t schedules_put_handler(httpd_req_t *req)
{
    char *body = NULL;
    esp_err_t body_ret = read_json_body(req, &body);
    if (body_ret == ESP_ERR_INVALID_SIZE) {
        return send_api_error(req, "413 Payload Too Large", "body_too_large",
                               "Request body is too large");
    }
    if (body_ret != ESP_OK) {
        return send_api_error(req, "400 Bad Request", "invalid_body",
                               "A JSON request body is required");
    }

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (root == NULL || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return send_api_error(req, "400 Bad Request", "invalid_json",
                              "Request body must be a JSON object");
    }

    int version_value;
    cJSON *version_json = cJSON_GetObjectItemCaseSensitive(root, "version");
    if (!json_integer_in_range(version_json, 0, INT32_MAX, &version_value)) {
        cJSON_Delete(root);
        return send_api_error(req, "400 Bad Request", "invalid_version",
                              "A numeric schedule version is required");
    }

    cJSON *array = cJSON_GetObjectItemCaseSensitive(root, "items");
    if (!cJSON_IsArray(array) || cJSON_GetArraySize(array) > MAX_SCHEDULE_ITEMS) {
        cJSON_Delete(root);
        return send_api_error(req, "400 Bad Request", "invalid_items",
                              "items must contain at most 8 schedules");
    }

    feed_schedule_item_t items[MAX_SCHEDULE_ITEMS];
    int item_count = cJSON_GetArraySize(array);
    for (int i = 0; i < item_count; i++) {
        if (!json_schedule_item(cJSON_GetArrayItem(array, i), &items[i])) {
            cJSON_Delete(root);
            return send_api_error(req, "400 Bad Request", "invalid_schedule",
                                  "Schedule fields are out of range");
        }
    }
    cJSON_Delete(root);

    uint32_t new_version = 0;
    esp_err_t ret = feed_schedule_replace_and_save(
        items, (size_t)item_count, (uint32_t)version_value, &new_version);
    if (ret == ESP_ERR_INVALID_STATE) {
        return send_api_error(req, "409 Conflict", "stale_version",
                              "Schedules changed; reload before saving");
    }
    if (ret == ESP_ERR_INVALID_ARG) {
        return send_api_error(req, "400 Bad Request", "invalid_schedule",
                              "Schedule fields are out of range");
    }
    if (ret != ESP_OK) {
        return send_api_error(req, "500 Internal Server Error", "save_error",
                              "Failed to save schedules");
    }

    cJSON *response = NULL;
    ret = build_schedules_json(&response);
    if (ret != ESP_OK) {
        return send_api_error(req, "500 Internal Server Error", "status_error",
                              "Schedules were saved but could not be read");
    }
    (void)new_version;
    return send_json_response(req, NULL, response);
}

static esp_err_t feed_post_handler(httpd_req_t *req)
{
    char *body = NULL;
    esp_err_t body_ret = read_json_body(req, &body);
    if (body_ret == ESP_ERR_INVALID_SIZE) {
        return send_api_error(req, "413 Payload Too Large", "body_too_large",
                               "Request body is too large");
    }
    if (body_ret != ESP_OK) {
        return send_api_error(req, "400 Bad Request", "invalid_body",
                              "A JSON request body is required");
    }

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (root == NULL || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return send_api_error(req, "400 Bad Request", "invalid_json",
                              "Request body must be a JSON object");
    }

    int amount;
    cJSON *amount_json = cJSON_GetObjectItemCaseSensitive(root, "amount");
    bool valid = json_integer_in_range(amount_json, 1, 6, &amount);
    cJSON_Delete(root);
    if (!valid) {
        return send_api_error(req, "400 Bad Request", "invalid_amount",
                              "amount must be between 1 and 6");
    }

    esp_err_t ret = manual_feeding_start((uint8_t)amount);
    if (ret == ESP_ERR_INVALID_STATE) {
        return send_api_error(req, "409 Conflict", "feeding_busy",
                              "A feeding operation is already in progress");
    }
    if (ret == ESP_ERR_INVALID_ARG) {
        return send_api_error(req, "400 Bad Request", "invalid_amount",
                              "amount must be between 1 and 6");
    }
    if (ret != ESP_OK) {
        return send_api_error(req, "503 Service Unavailable", "feed_failed",
                              "The feeder could not start");
    }

    cJSON *response = cJSON_CreateObject();
    if (response == NULL) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddBoolToObject(response, "accepted", true);
    cJSON_AddNumberToObject(response, "amount", amount);
    return send_json_response(req, "202 Accepted", response);
}

static void add_nullable_string(cJSON *object, const char *name,
                                const char *value)
{
    if (value != NULL && value[0] != '\0') {
        cJSON_AddStringToObject(object, name, value);
    } else {
        cJSON_AddNullToObject(object, name);
    }
}

static bool format_local_time(time_t timestamp, char *buffer, size_t length)
{
    struct tm timeinfo;
    localtime_r(&timestamp, &timeinfo);
    return strftime(buffer, length, "%Y-%m-%dT%H:%M:%S%z", &timeinfo) > 0;
}

static esp_err_t status_get_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *wifi = cJSON_CreateObject();
    cJSON *feeding = cJSON_CreateObject();
    cJSON *next = cJSON_CreateObject();
    if (root == NULL || wifi == NULL || feeding == NULL || next == NULL) {
        cJSON_Delete(root);
        cJSON_Delete(wifi);
        cJSON_Delete(feeding);
        cJSON_Delete(next);
        return ESP_ERR_NO_MEM;
    }

    bool time_synced = sntp_time_is_synced();
    cJSON_AddBoolToObject(root, "time_synced", time_synced);
    time_t now;
    time(&now);
    char time_buffer[32];
    if (time_synced && format_local_time(now, time_buffer, sizeof(time_buffer))) {
        cJSON_AddStringToObject(root, "local_time", time_buffer);
    } else {
        cJSON_AddNullToObject(root, "local_time");
    }

    bool feeding_active = false;
    uint8_t feeding_amount = 0;
    feeding_get_status(&feeding_active, &feeding_amount);
    cJSON_AddBoolToObject(feeding, "active", feeding_active);
    cJSON_AddNumberToObject(feeding, "amount", feeding_amount);
    cJSON_AddBoolToObject(feeding, "motor_idle", feeder_motor_is_idle());
    cJSON_AddItemToObject(root, "feeding", feeding);

    cJSON_AddBoolToObject(wifi, "connected", wifi_app_is_connected());
    add_nullable_string(wifi, "ssid", wifi_app_get_ssid());
    add_nullable_string(wifi, "ip", wifi_app_get_ip());
    cJSON_AddItemToObject(root, "wifi", wifi);

    time_t next_time;
    bool has_next = feed_schedule_get_next_time(&next_time);
    cJSON_AddBoolToObject(next, "available", has_next);
    if (has_next && format_local_time(next_time, time_buffer, sizeof(time_buffer))) {
        cJSON_AddNumberToObject(next, "epoch", (double)next_time);
        cJSON_AddStringToObject(next, "local_time", time_buffer);
    } else {
        cJSON_AddNullToObject(next, "epoch");
        cJSON_AddNullToObject(next, "local_time");
    }
    cJSON_AddItemToObject(root, "next_feed", next);

    return send_json_response(req, NULL, root);
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

static uint8_t *stream_alloc_jpeg_buffer(size_t capacity)
{
#ifdef CONFIG_SPIRAM
  uint8_t *buffer = heap_caps_malloc(capacity,
                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (buffer != NULL) {
    return buffer;
  }
  ESP_LOGW(TAG, "PSRAM JPEG send buffer allocation failed, try internal");
#endif
  return heap_caps_malloc(capacity, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

static void stream_log_stats(uint32_t frames_sent, size_t bytes_sent,
                             uint32_t frames_skipped,
                             TickType_t stats_start)
{
  TickType_t elapsed_ticks = xTaskGetTickCount() - stats_start;
  uint32_t elapsed_ms = pdTICKS_TO_MS(elapsed_ticks);
  if (elapsed_ms == 0) {
    return;
  }

  uint32_t fps_x10 = (frames_sent * 10000U) / elapsed_ms;
  uint32_t avg_bytes = frames_sent == 0 ? 0 : bytes_sent / frames_sent;
  ESP_LOGI(TAG, "stats: fps=%u.%u, avg_jpeg=%u bytes, skipped=%u",
           fps_x10 / 10, fps_x10 % 10, (unsigned)avg_bytes,
           (unsigned)frames_skipped);
}

static void stream_task(void *arg)
{
    httpd_req_t *req = (httpd_req_t *)arg;
    uint8_t *jpeg_copy = NULL;
    size_t jpeg_capacity = 0;
    uint32_t last_frame_id = 0;
    bool camera_acquired = false;
    bool socket_send_failed = false;
    int stream_sockfd = -1;
    uint32_t frames_sent = 0;
    uint32_t frames_skipped = 0;
    size_t bytes_sent = 0;
    TickType_t stats_start = xTaskGetTickCount();

    esp_err_t ret = ESP_OK;

    stream_sockfd = httpd_req_to_sockfd(req);
    if (stream_sockfd < 0) {
        ret = ESP_FAIL;
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
            /* 当前副本没有需要保留的内容，先申请新缓冲再释放旧缓冲，
             * 这样可以安全地在 PSRAM 和内部 RAM 之间 fallback。 */
            uint8_t *new_copy = stream_alloc_jpeg_buffer(new_capacity);
            if (new_copy == NULL) {
                copy_ret = ESP_ERR_NO_MEM;
                break;
            }
            heap_caps_free(jpeg_copy);
            jpeg_copy = new_copy;
            jpeg_capacity = new_capacity;
            ESP_LOGI(TAG, "JPEG send buffer allocated (%u bytes)",
                     (unsigned)jpeg_capacity);
        }

        if (copy_ret == ESP_ERR_NOT_FOUND ||
            (copy_ret == ESP_OK && frame_id == last_frame_id)) {
            frames_skipped++;
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
            ESP_LOGW(TAG, "stream send failed: %s, frames_sent=%u, jpeg=%u",
                     esp_err_to_name(send_ret), (unsigned)frames_sent,
                     (unsigned)jpeg_size);
            socket_send_failed = true;
            ret = send_ret;
            break;
        }

        frames_sent++;
        bytes_sent += jpeg_size;
        if (xTaskGetTickCount() - stats_start >= pdMS_TO_TICKS(5000)) {
            stream_log_stats(frames_sent, bytes_sent, frames_skipped,
                             stats_start);
            frames_sent = 0;
            frames_skipped = 0;
            bytes_sent = 0;
            stats_start = xTaskGetTickCount();
        }

        /* 只限制最高帧率；一次发送变慢后不追赶、不补发旧帧。 */
        TickType_t elapsed = xTaskGetTickCount() - frame_cycle_start;
        if (elapsed < frame_interval_ticks) {
            vTaskDelay(frame_interval_ticks - elapsed);
        }
    }

cleanup:
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "stream task ended: %s, socket=%d",
                 esp_err_to_name(ret), stream_sockfd);
    }
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
        ret = ESP_OK;
    }

    /* async request 必须由拥有它的任务显式完成，否则 socket 会一直被
     * HTTPD 标记为占用，最终连首页也无法访问。 */
    if (httpd_req_async_handler_complete(req) != ESP_OK) {
        ESP_LOGW(TAG, "failed to complete async stream request");
    }
    vTaskDelete(NULL);
}

static esp_err_t stream_handler(httpd_req_t *req)
{
    if (!stream_client_claim()) {
        return send_stream_error(req, "503 Service Unavailable",
                                 "Only one stream client is supported");
    }

    httpd_req_t *async_req = NULL;
    esp_err_t ret = httpd_req_async_handler_begin(req, &async_req);
    if (ret != ESP_OK) {
        stream_client_release();
        ESP_LOGW(TAG, "failed to begin async stream request: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    if (xTaskCreate(stream_task, "video_stream", VIDEO_STREAM_TASK_STACK_SIZE,
                    async_req, VIDEO_STREAM_TASK_PRIORITY, NULL) != pdPASS) {
        ESP_LOGE(TAG, "failed to create async stream task");
        httpd_req_async_handler_complete(async_req);
        stream_client_release();
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
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
    /* async 推流长期占用一个 socket，同时为控制页面/API保留短请求连接。 */
    config.max_open_sockets = 6;
    config.max_uri_handlers = 6;
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
    static const httpd_uri_t status_uri = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = status_get_handler,
        .user_ctx = NULL,
    };
    static const httpd_uri_t schedules_get_uri = {
        .uri = "/api/schedules",
        .method = HTTP_GET,
        .handler = schedules_get_handler,
        .user_ctx = NULL,
    };
    static const httpd_uri_t schedules_put_uri = {
        .uri = "/api/schedules",
        .method = HTTP_PUT,
        .handler = schedules_put_handler,
        .user_ctx = NULL,
    };
    static const httpd_uri_t feed_post_uri = {
        .uri = "/api/feed",
        .method = HTTP_POST,
        .handler = feed_post_handler,
        .user_ctx = NULL,
    };

    ret = httpd_register_uri_handler(s_http_server, &index_uri);
    if (ret == ESP_OK) {
        ret = httpd_register_uri_handler(s_http_server, &stream_uri);
    }
    if (ret == ESP_OK) {
        ret = httpd_register_uri_handler(s_http_server, &status_uri);
    }
    if (ret == ESP_OK) {
        ret = httpd_register_uri_handler(s_http_server, &schedules_get_uri);
    }
    if (ret == ESP_OK) {
        ret = httpd_register_uri_handler(s_http_server, &schedules_put_uri);
    }
    if (ret == ESP_OK) {
        ret = httpd_register_uri_handler(s_http_server, &feed_post_uri);
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
