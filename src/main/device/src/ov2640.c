#include "device/inc/ov2640.h"

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

#define CAM_FRAME_BYTES (CAM_OUTPUT_WIDTH * CAM_OUTPUT_HEIGHT)

/* DVP 控制器句柄 / sensor 句柄 */
static esp_cam_ctlr_handle_t s_cam_handle = NULL;
static esp_cam_sensor_device_t *s_sensor = NULL;
static i2c_master_bus_handle_t s_i2c_bus = NULL;
static esp_sccb_io_handle_t s_sccb_io = NULL;
static esp_cam_sensor_format_t s_active_format;

/* OV2640 register banks and the 240x240 crop/clock overrides.  The managed
 * sensor component exposes a JPEG 320x240 base mode; its public set_format()
 * API accepts a caller-owned format, so the project can derive the square
 * mode without modifying managed_components. */
#define OV2640_BANK_SEL_REG  0xff
#define OV2640_BANK_DSP      0x00
#define OV2640_BANK_SENSOR   0x01
#define OV2640_CLKRC_REG     0x11
#define OV2640_R_DVP_SP_REG  0xd3

typedef struct {
  uint8_t reg;
  uint8_t value;
} ov2640_reg_setting_t;

static const ov2640_reg_setting_t s_jpeg_240x240_overrides[] = {
    {OV2640_BANK_SEL_REG, OV2640_BANK_DSP},
    {0x51, 0x4b}, /* centered 240-pixel input window */
    {0x52, 0x4a},
    {0x53, 0x32},
    {0x54, 0x00},
    {0x55, 0x00},
    {0x57, 0x00},
    {0x5a, 0x3c}, /* output width / 4  = 60 */
    {0x5b, 0x3c}, /* output height / 4 = 60 */
    {0x5c, 0x00},
    {OV2640_BANK_SEL_REG, OV2640_BANK_SENSOR},
    {OV2640_CLKRC_REG, 0x83},
    {OV2640_BANK_SEL_REG, OV2640_BANK_DSP},
    {OV2640_R_DVP_SP_REG, 0x88},
};

/* 双 DMA 缓冲：采集持续进行，消费方只读取复制后的 JPEG。 */
static uint8_t *s_buf[2] = {NULL, NULL};
static int s_next_idx = 0;
static int s_write_idx = -1;
static int s_latest_idx = -1;
static size_t s_latest_size = 0;
static uint32_t s_frame_id = 0;
static uint32_t s_frame_state_version = 0;
static portMUX_TYPE s_frame_mux = portMUX_INITIALIZER_UNLOCKED;

static bool s_ready = false;
static bool s_running = false;
static uint32_t s_camera_users = 0;
static SemaphoreHandle_t s_camera_mutex = NULL;

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
  trans->buflen = CAM_FRAME_BYTES;
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

/* 从组件自带的 JPEG 320x240 基础配置派生方形 JPEG 240x240。 */
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
        f->width == 320 && f->height == 240) {
      base_format = f;
      break;
    }
  }
  if (base_format == NULL) {
    ESP_LOGE(TAG, "no JPEG 320x240 base format found for OV2640");
    return ESP_ERR_NOT_SUPPORTED;
  }

  s_active_format = *base_format;
  s_active_format.name = "DVP_8bit_JPEG_240x240_25fps";
  s_active_format.width = CAM_OUTPUT_WIDTH;
  s_active_format.height = CAM_OUTPUT_HEIGHT;
  s_active_format.fps = 25;

  esp_err_t ret = esp_cam_sensor_set_format(s_sensor, &s_active_format);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "set format failed: %s", esp_err_to_name(ret));
    return ret;
  }

  for (size_t i = 0;
       i < sizeof(s_jpeg_240x240_overrides) /
               sizeof(s_jpeg_240x240_overrides[0]);
       i++) {
    ret = esp_sccb_transmit_reg_a8v8(s_sccb_io,
                                     s_jpeg_240x240_overrides[i].reg,
                                     s_jpeg_240x240_overrides[i].value);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "240x240 register setup failed at %u: %s", (unsigned)i,
               esp_err_to_name(ret));
      return ret;
    }
  }

  ESP_LOGI(TAG, "format in use: %s", s_active_format.name);
  return ESP_OK;
}

esp_err_t ov2640_camera_init(void)
{
  esp_err_t ret = ESP_OK;

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
      .h_res = CAM_OUTPUT_WIDTH,
      .v_res = CAM_OUTPUT_HEIGHT,
      .dma_burst_size = 64,
      .pin = &pin_cfg,
      .bk_buffer_dis = 1,
      /* 与 ESP32-S3-EYE 官方例程一致，关闭 DVP 字节交换 */
      .byte_swap_en = 0,
      .pic_format_jpeg = 1,
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

/* 首次启动时分配帧缓冲。未启用 PSRAM 时直接使用内部 SRAM，
 * 避免每次 Web 连接都先做一次必然失败的 PSRAM 分配。 */
static esp_err_t cam_alloc_frame_buffers(void)
{
  if (s_buf[0] != NULL && s_buf[1] != NULL) {
    return ESP_OK;
  }

  for (int i = 0; i < 2; i++) {
#ifdef CONFIG_SPIRAM
    s_buf[i] = esp_cam_ctlr_alloc_buffer(
        s_cam_handle, CAM_FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    if (s_buf[i] == NULL) {
      ESP_LOGW(TAG, "PSRAM frame buffer %d alloc failed, try internal", i);
      s_buf[i] = esp_cam_ctlr_alloc_buffer(s_cam_handle, CAM_FRAME_BYTES,
                                            MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    }
#else
    s_buf[i] = esp_cam_ctlr_alloc_buffer(s_cam_handle, CAM_FRAME_BYTES,
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
  }
  ESP_LOGI(TAG, "frame buffers allocated (%d bytes each)", CAM_FRAME_BYTES);
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
  s_frame_state_version++;
  portEXIT_CRITICAL(&s_frame_mux);
}

esp_err_t ov2640_camera_start(void)
{
  if (!s_ready || s_cam_handle == NULL) {
    return ESP_ERR_INVALID_STATE;
  }
  if (s_running) {
    return ESP_OK;
  }

  /* 首次启动时分配帧缓冲（内部 SRAM 有限，不采集时不占用） */
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
  s_frame_state_version++;
  portEXIT_CRITICAL(&s_frame_mux);

  /* 按官方例程先让传感器开始输出，再启动 DVP 控制器 */
  int enable = 1;
  ret = esp_cam_sensor_ioctl(s_sensor, ESP_CAM_SENSOR_IOC_S_STREAM,
                             &enable);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "sensor stream start failed");
    return ret;
  }

  ret = esp_cam_ctlr_enable(s_cam_handle);
  ESP_RETURN_ON_ERROR(ret, TAG, "ctrlr enable failed");

  ret = esp_cam_ctlr_start(s_cam_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "ctrlr start failed");
    return ret;
  }

  s_running = true;
  ESP_LOGI(TAG, "camera stream started");
  return ESP_OK;
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
  if (s_running) {
    esp_cam_ctlr_stop(s_cam_handle);
    int enable = 0;
    esp_cam_sensor_ioctl(s_sensor, ESP_CAM_SENSOR_IOC_S_STREAM, &enable);
    esp_cam_ctlr_disable(s_cam_handle);
    s_running = false;
    ESP_LOGI(TAG, "camera stream stopped");
  }
  /* 无使用者后释放帧缓冲，归还内部 SRAM */
  cam_free_frame_buffers();
  return ESP_OK;
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
    uint32_t latest_frame_id;
    uint32_t version_before;

    portENTER_CRITICAL(&s_frame_mux);
    latest_idx = s_latest_idx;
    write_idx = s_write_idx;
    latest_size = s_latest_size;
    latest_frame_id = s_frame_id;
    version_before = s_frame_state_version;
    portEXIT_CRITICAL(&s_frame_mux);

    if (latest_idx < 0 || latest_size == 0 || latest_size > CAM_FRAME_BYTES ||
        latest_idx == write_idx) {
      return ESP_ERR_NOT_FOUND;
    }

    *size = latest_size;
    if (dst == NULL || capacity < latest_size) {
      return ESP_ERR_INVALID_SIZE;
    }

    memcpy(dst, s_buf[latest_idx], latest_size);

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
