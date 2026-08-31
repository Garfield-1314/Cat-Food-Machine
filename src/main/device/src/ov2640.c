#include "device/inc/ov2640.h"
#include "device/inc/ir_light.h"

#include <string.h>

#include "driver/i2c_master.h"
#include "esp_cam_ctlr.h"
#include "esp_cam_ctlr_dvp.h"
#include "esp_cam_sensor.h"
#include "esp_cam_sensor_detect.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_sccb_i2c.h"
#include "esp_sccb_intf.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "ov2640";

/* DVP 控制器句柄 / sensor 句柄 */
static esp_cam_ctlr_handle_t s_cam_handle = NULL;
static esp_cam_sensor_device_t *s_sensor = NULL;
static i2c_master_bus_handle_t s_i2c_bus = NULL;
static esp_sccb_io_handle_t s_sccb_io = NULL;
static esp_cam_sensor_format_t s_active_format;

/* 双 DMA 缓冲：采集持续进行，消费方只读取复制后的 JPEG。 */
static uint8_t *s_buf[2] = {NULL, NULL};
static int s_next_idx = 0;
static int s_write_idx = -1;
static int s_latest_idx = -1;
static size_t s_latest_size = 0;
static uint32_t s_frame_id = 0;
static uint32_t s_frame_state_version = 0;
static uint32_t s_buffer_limit_hits = 0;
static uint32_t s_completed_frames = 0;
static uint32_t s_invalid_jpeg_frames = 0;
static uint8_t s_last_frame_byte0 = 0;
static uint8_t s_last_frame_byte1 = 0;
static portMUX_TYPE s_frame_mux = portMUX_INITIALIZER_UNLOCKED;

static bool s_ready = false;
static bool s_running = false;
static uint32_t s_camera_users = 0;
static SemaphoreHandle_t s_camera_mutex = NULL;

/* DVP JPEG 模式在较大 OV2640 分辨率下可能被驱动误判为 size=0。
 * 这里让驱动按字节流接收，再在任务上下文中查找 JPEG 结束标志。 */
static size_t cam_find_jpeg_size(const uint8_t *buffer, size_t size)
{
  if (buffer == NULL || size < 4 || buffer[0] != 0xff || buffer[1] != 0xd8) {
    return 0;
  }

  for (size_t off = 2; off + 1 < size; off++) {
    if (buffer[off] == 0xff && buffer[off + 1] == 0xd9) {
      return off + 2;
    }
  }
  return 0;
}

/* ========== DVP 帧回调（ISR / 低优先级上下文） ========== */
static bool cam_get_new_trans(esp_cam_ctlr_handle_t handle,
                              esp_cam_ctlr_trans_t *trans, void *user_data)
{
  (void)handle;
  (void)user_data;

  portENTER_CRITICAL_ISR(&s_frame_mux);
  int i = s_next_idx;
  s_next_idx = (s_next_idx + 1) % 2;
  s_write_idx = i;
  s_frame_state_version++;
  trans->buffer = s_buf[i];
  trans->buflen = CAM_JPEG_BUFFER_BYTES;
  portEXIT_CRITICAL_ISR(&s_frame_mux);
  return false;
}

static bool cam_trans_finished(esp_cam_ctlr_handle_t handle,
                               esp_cam_ctlr_trans_t *trans, void *user_data)
{
  (void)handle;
  (void)user_data;
  for (int i = 0; i < 2; i++) {
    if (trans->buffer == s_buf[i]) {
      portENTER_CRITICAL_ISR(&s_frame_mux);
      s_latest_size = trans->received_size;
      s_latest_idx = i;
      const uint8_t *frame_buffer = (const uint8_t *)trans->buffer;
      s_last_frame_byte0 = frame_buffer[0];
      s_last_frame_byte1 = frame_buffer[1];
      s_completed_frames++;
      if (trans->received_size == 0) {
        s_invalid_jpeg_frames++;
      }
      if (trans->received_size >= CAM_JPEG_BUFFER_BYTES) {
        /* The DVP driver caps received_size at the DMA buffer capacity. Keep
         * this counter in the callback and log it from task context below. */
        s_buffer_limit_hits++;
      }
      s_frame_id++;
      s_frame_state_version++;
      portEXIT_CRITICAL_ISR(&s_frame_mux);
      break;
    }
  }
  return false;
}

/* ========== 传感器初始化（参考 esp-idf sensor_init 组件） ========== */
static esp_err_t sensor_sccb_init(void)
{
  i2c_master_bus_config_t bus_conf = {
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .i2c_port = CAM_SCCB_I2C_PORT,
      .sda_io_num = CAM_SCCB_SDA_IO,
      .scl_io_num = CAM_SCCB_SCL_IO,
      .flags.enable_internal_pullup = true,
  };
  ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_conf, &s_i2c_bus), TAG,
                      "i2c bus init failed");

  esp_cam_sensor_config_t cam_config = {
      .reset_pin = -1,
      .pwdn_pin = -1,
      .xclk_pin = -1,
      .xclk_freq_hz = CAM_XCLK_FREQ_HZ,
  };

  for (esp_cam_sensor_detect_fn_t *p = &__esp_cam_sensor_detect_fn_array_start;
       p < &__esp_cam_sensor_detect_fn_array_end; ++p) {
    sccb_i2c_config_t i2c_config = {
        .scl_speed_hz = 400000,
        .device_address = p->sccb_addr,
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    };
    ESP_RETURN_ON_ERROR(sccb_new_i2c_io(s_i2c_bus, &i2c_config, &s_sccb_io),
                        TAG, "sccb io create failed");
    cam_config.sccb_handle = s_sccb_io;
    cam_config.sensor_port = p->port;

    s_sensor = (*(p->detect))(&cam_config);
    if (s_sensor) {
      if (p->port != ESP_CAM_SENSOR_DVP) {
        ESP_LOGE(TAG, "detected sensor with mismatched interface");
        esp_cam_sensor_del_dev(s_sensor);
        s_sensor = NULL;
        return ESP_ERR_INVALID_ARG;
      }
      break;
    }
    esp_sccb_del_i2c_io(s_sccb_io);
    s_sccb_io = NULL;
  }

  if (!s_sensor) {
    ESP_LOGE(TAG, "failed to detect camera sensor (OV2640)");
    return ESP_ERR_NOT_FOUND;
  }
  ESP_LOGI(TAG, "detected sensor: %s", esp_cam_sensor_get_name(s_sensor));
  return ESP_OK;
}

/* 选择组件自带的 JPEG 640x480 格式，并设置可调的 JPEG 质量。 */
static esp_err_t sensor_set_format(void)
{
  esp_cam_sensor_format_array_t fmt_array = {0};
  ESP_RETURN_ON_ERROR(esp_cam_sensor_query_format(s_sensor, &fmt_array), TAG,
                      "query format failed");

  const esp_cam_sensor_format_t *base_format = NULL;
  for (int i = 0; i < fmt_array.count; i++) {
    const esp_cam_sensor_format_t *f = &fmt_array.format_array[i];
    ESP_LOGI(TAG, "fmt[%d]: %s", i, f->name);
    if (f->format == ESP_CAM_SENSOR_PIXFORMAT_JPEG &&
        f->width == CAM_OUTPUT_WIDTH && f->height == CAM_OUTPUT_HEIGHT) {
      base_format = f;
      break;
    }
  }
  if (base_format == NULL) {
    ESP_LOGE(TAG, "no JPEG %dx%d format found for OV2640", CAM_OUTPUT_WIDTH,
             CAM_OUTPUT_HEIGHT);
    return ESP_ERR_NOT_SUPPORTED;
  }

  s_active_format = *base_format;
  s_active_format.name = "DVP_8bit_JPEG_640x480";

  esp_err_t ret = esp_cam_sensor_set_format(s_sensor, &s_active_format);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "set format failed: %s", esp_err_to_name(ret));
    return ret;
  }

  int32_t jpeg_quality = CAM_JPEG_QUALITY;
  ret = esp_cam_sensor_set_para_value(s_sensor, ESP_CAM_SENSOR_JPEG_QUALITY,
                                      &jpeg_quality, sizeof(jpeg_quality));
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "set JPEG quality %d failed: %s", jpeg_quality,
             esp_err_to_name(ret));
    return ret;
  }

  /* 画面 180 度旋转：水平镜像 + 垂直翻转（与 LCD 显示方向一致） */
  int32_t hmirror = 1;
  ret = esp_cam_sensor_set_para_value(s_sensor, ESP_CAM_SENSOR_HMIRROR,
                                      &hmirror, sizeof(hmirror));
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "set hmirror failed: %s", esp_err_to_name(ret));
    return ret;
  }
  int32_t vflip = 1;
  ret = esp_cam_sensor_set_para_value(s_sensor, ESP_CAM_SENSOR_VFLIP,
                                      &vflip, sizeof(vflip));
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "set vflip failed: %s", esp_err_to_name(ret));
    return ret;
  }

  ESP_LOGI(TAG, "format in use: %s, JPEG quality: %d", s_active_format.name,
           jpeg_quality);
  return ESP_OK;
}

esp_err_t ov2640_camera_init(void)
{
  esp_err_t ret = ESP_OK;

  /* 先将补光灯配置为输出低电平，确保摄像头未使用时保持关闭。 */
  ret = ir_light_init();
  if (ret != ESP_OK) {
    return ret;
  }

  if (s_camera_mutex == NULL) {
    s_camera_mutex = xSemaphoreCreateMutex();
    if (s_camera_mutex == NULL) {
      ESP_LOGE(TAG, "failed to create camera mutex");
      return ESP_ERR_NO_MEM;
    }
  }
  /* ---------- 1. DVP 控制器 ----------
   *
   * esp_cam_new_dvp_ctlr() starts the XCLK output on CAM_DVP_XCLK_IO.
   * OV2640 SCCB detection must happen after this, otherwise the sensor has
   * no clock and normally cannot acknowledge on the SCCB bus.
   */
  esp_cam_ctlr_dvp_pin_config_t pin_cfg = {
      .data_width = 8,
      .data_io = {CAM_DVP_D0_IO, CAM_DVP_D1_IO, CAM_DVP_D2_IO, CAM_DVP_D3_IO,
                  CAM_DVP_D4_IO, CAM_DVP_D5_IO, CAM_DVP_D6_IO, CAM_DVP_D7_IO},
      .vsync_io = CAM_DVP_VSYNC_IO,
      .de_io = CAM_DVP_DE_IO,
      .pclk_io = CAM_DVP_PCLK_IO,
      .xclk_io = CAM_DVP_XCLK_IO,
  };

  esp_cam_ctlr_dvp_config_t dvp_config = {
      .ctlr_id = 0,
      .clk_src = CAM_CLK_SRC_DEFAULT,
      .h_res = CAM_DVP_DMA_WIDTH,
      .v_res = CAM_DVP_DMA_HEIGHT,
      .dma_burst_size = 64,
      .pin = &pin_cfg,
      .bk_buffer_dis = 1,
      /* 与 ESP32-S3-EYE 官方例程一致，关闭 DVP 字节交换 */
      .byte_swap_en = 0,
      .input_data_color_type = CAM_CTLR_COLOR_RGB565,
      /* Work around the ESP32-S3 DVP JPEG size=0 issue for OV2640 >= VGA.
       * The sensor still outputs JPEG; the application finds FFD9 below. */
      .pic_format_jpeg = 0,
      .xclk_freq = CAM_XCLK_FREQ_HZ,
  };

  ret = esp_cam_new_dvp_ctlr(&dvp_config, &s_cam_handle);
  ESP_RETURN_ON_ERROR(ret, TAG, "dvp controller init failed");

  /* 帧缓冲延迟到首次采集时分配（内部 SRAM 有限，不采集时不占用） */

  /* ---------- 2. SCCB + 传感器检测 ---------- */
  ret = sensor_sccb_init();
  if (ret != ESP_OK) {
    return ret;
  }

  /* ---------- 4. 设置传感器输出格式 ---------- */
  ret = sensor_set_format();
  if (ret != ESP_OK) {
    return ret;
  }

  /* ---------- 5. 注册 DVP 回调 ---------- */
  esp_cam_ctlr_evt_cbs_t cbs = {
      .on_get_new_trans = cam_get_new_trans,
      .on_trans_finished = cam_trans_finished,
  };
  ret = esp_cam_ctlr_register_event_callbacks(s_cam_handle, &cbs, NULL);
  ESP_RETURN_ON_ERROR(ret, TAG, "register callbacks failed");

  s_ready = true;
  ESP_LOGI(TAG, "OV2640 initialized (%dx%d JPEG)", CAM_OUTPUT_WIDTH,
           CAM_OUTPUT_HEIGHT);
  return ESP_OK;
}

/* 首次启动时分配帧缓冲，优先使用 PSRAM DMA 内存。 */
static esp_err_t cam_alloc_frame_buffers(void)
{
  if (s_buf[0] != NULL && s_buf[1] != NULL) {
    return ESP_OK;
  }

  for (int i = 0; i < 2; i++) {
#ifdef CONFIG_SPIRAM
    s_buf[i] = esp_cam_ctlr_alloc_buffer(
        s_cam_handle, CAM_JPEG_BUFFER_BYTES,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    if (s_buf[i] == NULL) {
      ESP_LOGW(TAG, "PSRAM frame buffer %d alloc failed, try internal", i);
      s_buf[i] = esp_cam_ctlr_alloc_buffer(
          s_cam_handle, CAM_JPEG_BUFFER_BYTES,
          MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    }
#else
    s_buf[i] = esp_cam_ctlr_alloc_buffer(
        s_cam_handle, CAM_JPEG_BUFFER_BYTES,
        MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
#endif
    if (s_buf[i] == NULL) {
      ESP_LOGE(TAG, "frame buffer %d alloc failed", i);
      for (int j = 0; j < i; j++) {
        heap_caps_free(s_buf[j]);
        s_buf[j] = NULL;
      }
      return ESP_ERR_NO_MEM;
    }
    memset(s_buf[i], 0, CAM_JPEG_BUFFER_BYTES);
  }
  ESP_LOGI(TAG, "frame buffers allocated (%d bytes each, PSRAM preferred)",
           CAM_JPEG_BUFFER_BYTES);
  return ESP_OK;
}

static void cam_free_frame_buffers(void)
{
  for (int i = 0; i < 2; i++) {
    if (s_buf[i] != NULL) {
      heap_caps_free(s_buf[i]);
      s_buf[i] = NULL;
    }
  }

  portENTER_CRITICAL(&s_frame_mux);
  s_next_idx = 0;
  s_write_idx = -1;
  s_latest_idx = -1;
  s_latest_size = 0;
  s_frame_id = 0;
  s_buffer_limit_hits = 0;
  s_completed_frames = 0;
  s_invalid_jpeg_frames = 0;
  s_last_frame_byte0 = 0;
  s_last_frame_byte1 = 0;
  s_frame_state_version++;
  portEXIT_CRITICAL(&s_frame_mux);
}

esp_err_t ov2640_camera_start(void)
{
  if (!s_ready || s_cam_handle == NULL) {
    return ESP_ERR_INVALID_STATE;
  }
  if (s_running) {
    return ir_light_on();
  }

  /* 首次启动时分配帧缓冲（不采集时不占用） */
  esp_err_t ret = cam_alloc_frame_buffers();
  if (ret != ESP_OK) {
    return ret;
  }

  portENTER_CRITICAL(&s_frame_mux);
  s_next_idx = 0;
  s_write_idx = -1;
  s_latest_idx = -1;
  s_latest_size = 0;
  s_frame_id = 0;
  s_buffer_limit_hits = 0;
  s_completed_frames = 0;
  s_invalid_jpeg_frames = 0;
  s_last_frame_byte0 = 0;
  s_last_frame_byte1 = 0;
  s_frame_state_version++;
  portEXIT_CRITICAL(&s_frame_mux);

  bool sensor_stream_started = false;
  bool controller_enabled = false;
  bool controller_started = false;

  /* 补光灯先于传感器出流打开，保证首帧也能获得夜视补光。 */
  ret = ir_light_on();
  if (ret != ESP_OK) {
    goto start_failed;
  }

  /* 按官方例程先让传感器开始输出，再启动 DVP 控制器 */
  int enable = 1;
  ret = esp_cam_sensor_ioctl(s_sensor, ESP_CAM_SENSOR_IOC_S_STREAM,
                             &enable);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "sensor stream start failed");
    goto start_failed;
  }
  sensor_stream_started = true;

  ret = esp_cam_ctlr_enable(s_cam_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "ctrlr enable failed: %s", esp_err_to_name(ret));
    goto start_failed;
  }
  controller_enabled = true;

  ret = esp_cam_ctlr_start(s_cam_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "ctrlr start failed: %s", esp_err_to_name(ret));
    goto start_failed;
  }
  controller_started = true;

  s_running = true;
  ESP_LOGI(TAG, "camera stream started");
  return ESP_OK;

start_failed:
  /* 启动失败时回滚所有已启动的资源，尤其不能让补光灯保持点亮。 */
  if (controller_started) {
    esp_cam_ctlr_stop(s_cam_handle);
  }
  if (sensor_stream_started) {
    int disable = 0;
    esp_cam_sensor_ioctl(s_sensor, ESP_CAM_SENSOR_IOC_S_STREAM, &disable);
  }
  if (controller_enabled) {
    esp_cam_ctlr_disable(s_cam_handle);
  }
  ir_light_off();
  return ret;
}

esp_err_t ov2640_camera_acquire(void)
{
  if (s_camera_mutex == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  xSemaphoreTake(s_camera_mutex, portMAX_DELAY);

  if (s_camera_users > 0) {
    s_camera_users++;
    xSemaphoreGive(s_camera_mutex);
    return ESP_OK;
  }

  esp_err_t ret = ov2640_camera_start();
  if (ret == ESP_OK) {
    s_camera_users = 1;
  }

  xSemaphoreGive(s_camera_mutex);
  return ret;
}

esp_err_t ov2640_camera_stop(void)
{
  esp_err_t ret = ESP_OK;

  if (s_running) {
    esp_err_t step_ret = esp_cam_ctlr_stop(s_cam_handle);
    if (step_ret != ESP_OK) {
      ret = step_ret;
    }
    int enable = 0;
    step_ret = esp_cam_sensor_ioctl(s_sensor, ESP_CAM_SENSOR_IOC_S_STREAM,
                                    &enable);
    if (ret == ESP_OK && step_ret != ESP_OK) {
      ret = step_ret;
    }
    step_ret = esp_cam_ctlr_disable(s_cam_handle);
    if (ret == ESP_OK && step_ret != ESP_OK) {
      ret = step_ret;
    }
    s_running = false;
    ESP_LOGI(TAG, "camera stream stopped");
  }

  /* 即使当前状态已是 stopped，也强制确保补光灯关闭。 */
  esp_err_t light_ret = ir_light_off();
  if (ret == ESP_OK && light_ret != ESP_OK) {
    ret = light_ret;
  }

  /* 无使用者后释放帧缓冲，归还 PSRAM（或内部 RAM fallback） */
  cam_free_frame_buffers();
  return ret;
}

esp_err_t ov2640_camera_release(void)
{
  if (s_camera_mutex == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  xSemaphoreTake(s_camera_mutex, portMAX_DELAY);

  if (s_camera_users == 0) {
    xSemaphoreGive(s_camera_mutex);
    return ESP_ERR_INVALID_STATE;
  }

  s_camera_users--;
  esp_err_t ret = ESP_OK;
  if (s_camera_users == 0) {
    ret = ov2640_camera_stop();
  }

  xSemaphoreGive(s_camera_mutex);
  return ret;
}

esp_err_t ov2640_camera_copy_jpeg_frame(uint8_t *dst, size_t capacity,
                                        size_t *size, uint32_t *frame_id)
{
  if (size == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  *size = 0;
  if (frame_id != NULL) {
    *frame_id = 0;
  }
  if (!s_running) {
    return ESP_ERR_INVALID_STATE;
  }

  for (int attempt = 0; attempt < 3; attempt++) {
    int latest_idx;
    int write_idx;
    size_t latest_size;
    size_t jpeg_size;
    uint32_t latest_frame_id;
    uint32_t version_before;
    uint32_t buffer_limit_hits;
    uint32_t completed_frames;
    uint32_t invalid_jpeg_frames;
    uint8_t last_frame_byte0;
    uint8_t last_frame_byte1;

    portENTER_CRITICAL(&s_frame_mux);
    latest_idx = s_latest_idx;
    write_idx = s_write_idx;
    latest_size = s_latest_size;
    latest_frame_id = s_frame_id;
    version_before = s_frame_state_version;
    buffer_limit_hits = s_buffer_limit_hits;
    s_buffer_limit_hits = 0;
    completed_frames = s_completed_frames;
    invalid_jpeg_frames = s_invalid_jpeg_frames;
    last_frame_byte0 = s_last_frame_byte0;
    last_frame_byte1 = s_last_frame_byte1;
    portEXIT_CRITICAL(&s_frame_mux);

    if (buffer_limit_hits > 0) {
      ESP_LOGE(TAG,
               "JPEG reached frame buffer limit (%u bytes); frame may be truncated",
               CAM_JPEG_BUFFER_BYTES);
    }

    if (latest_idx < 0 || latest_size == 0 ||
        latest_size > CAM_JPEG_BUFFER_BYTES ||
        latest_idx == write_idx) {
      static TickType_t last_no_frame_log = 0;
      TickType_t now = xTaskGetTickCount();
      if (now - last_no_frame_log >= pdMS_TO_TICKS(2000)) {
        ESP_LOGW(TAG,
                 "no valid JPEG: completed=%u invalid=%u latest_size=%u head=%02x%02x",
                 (unsigned)completed_frames, (unsigned)invalid_jpeg_frames,
                 (unsigned)latest_size, last_frame_byte0, last_frame_byte1);
        last_no_frame_log = now;
      }
      return ESP_ERR_NOT_FOUND;
    }

    jpeg_size = cam_find_jpeg_size(s_buf[latest_idx], latest_size);
    if (jpeg_size == 0) {
      portENTER_CRITICAL(&s_frame_mux);
      s_invalid_jpeg_frames++;
      portEXIT_CRITICAL(&s_frame_mux);
      static TickType_t last_no_eoi_log = 0;
      TickType_t now = xTaskGetTickCount();
      if (now - last_no_eoi_log >= pdMS_TO_TICKS(2000)) {
        ESP_LOGW(TAG, "JPEG EOI not found: raw_size=%u head=%02x%02x",
                 (unsigned)latest_size, last_frame_byte0, last_frame_byte1);
        last_no_eoi_log = now;
      }
      return ESP_ERR_NOT_FOUND;
    }

    *size = jpeg_size;
    if (dst == NULL || capacity < jpeg_size) {
      return ESP_ERR_INVALID_SIZE;
    }

    memcpy(dst, s_buf[latest_idx], jpeg_size);

    portENTER_CRITICAL(&s_frame_mux);
    bool unchanged = version_before == s_frame_state_version &&
                     latest_idx == s_latest_idx &&
                     latest_idx != s_write_idx &&
                     latest_size == s_latest_size;
    portEXIT_CRITICAL(&s_frame_mux);

    if (unchanged) {
      if (frame_id != NULL) {
        *frame_id = latest_frame_id;
      }
      return ESP_OK;
    }
  }

  *size = 0;
  return ESP_ERR_NOT_FOUND;
}

bool ov2640_camera_is_ready(void)
{
  return s_ready;
}
