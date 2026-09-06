#include "ui/inc/setting_page.h"

#include "esp_chip_info.h"
#include "esp_idf_version.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "ui/inc/ui.h"
#include "ui/inc/wifi_config_page.h"
#include "device/inc/ir_light.h"
#include "device/inc/ov2640.h"
#include "device/inc/st7789.h"

#define FIRMWARE_VERSION "0.2.2"

/* NVS 背光亮度存储 */
#define NVS_NAMESPACE  "lcd_config"
#define NVS_KEY_BRIGHT "brightness"

/* NVS 摄像头设置存储（红外光强 + 曝光等级） */
#define NVS_CAM_NS       "cam_config"
#define NVS_KEY_IR_DUTY  "ir_duty"
#define NVS_KEY_AE_LEVEL "ae_level"

/* 控制行布局（320x240 屏幕） */
#define CTRL_LABEL_X   12
#define CTRL_SLIDER_X  105
#define CTRL_SLIDER_W  155
#define CTRL_SLIDER_H  18

lv_obj_t *setting_page;

/* 背光亮度滑块 */
static lv_obj_t *brightness_slider = NULL;
static lv_obj_t *brightness_label = NULL;
static lv_timer_t *brightness_save_timer = NULL;
static uint8_t pending_brightness = 100;
static bool brightness_dirty = false;

/* 红外光强滑块 */
static lv_obj_t *ir_light_slider = NULL;
static lv_obj_t *ir_light_label = NULL;
static uint8_t pending_ir_duty = 60;

/* 曝光等级滑块（0..4 映射 -2..+2） */
static lv_obj_t *exposure_slider = NULL;
static lv_obj_t *exposure_label = NULL;
static int8_t pending_ae_level = -1;

/* 摄像头设置共用防抖保存定时器 */
static lv_timer_t *cam_save_timer = NULL;
static bool cam_dirty = false;

static void save_brightness(uint8_t val);
static void save_cam_settings(void);
static void schedule_cam_save(void);

static void save_brightness_timer_cb(lv_timer_t *timer)
{
  (void)timer;
  if (brightness_dirty) {
    save_brightness(pending_brightness);
    brightness_dirty = false;
  }
  brightness_save_timer = NULL;
}

static void cam_save_timer_cb(lv_timer_t *timer)
{
  (void)timer;
  if (cam_dirty) {
    save_cam_settings();
    cam_dirty = false;
  }
  cam_save_timer = NULL;
}

static void setting_page_delete_cb(lv_event_t *e)
{
  if (brightness_save_timer != NULL) {
    lv_timer_del(brightness_save_timer);
    brightness_save_timer = NULL;
  }
  if (brightness_dirty) {
    save_brightness(pending_brightness);
    brightness_dirty = false;
  }
  if (cam_save_timer != NULL) {
    lv_timer_del(cam_save_timer);
    cam_save_timer = NULL;
  }
  if (cam_dirty) {
    save_cam_settings();
    cam_dirty = false;
  }
  setting_page = NULL;
  brightness_slider = NULL;
  brightness_label = NULL;
  ir_light_slider = NULL;
  ir_light_label = NULL;
  exposure_slider = NULL;
  exposure_label = NULL;
}

/* 从 NVS 读取亮度 */
static uint8_t load_brightness(void)
{
    nvs_handle_t nvs;
    uint8_t val = 100;  /* 默认最亮 */
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        uint8_t v = 100;
        if (nvs_get_u8(nvs, NVS_KEY_BRIGHT, &v) == ESP_OK) {
            val = v;
        }
        nvs_close(nvs);
    }
    return val;
}

/* 保存亮度到 NVS */
static void save_brightness(uint8_t val)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_u8(nvs, NVS_KEY_BRIGHT, val);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

/* 从 NVS 读取红外光强（默认 60） */
static uint8_t load_ir_duty(void)
{
    nvs_handle_t nvs;
    uint8_t val = IR_LIGHT_DUTY_PERCENT;
    if (nvs_open(NVS_CAM_NS, NVS_READONLY, &nvs) == ESP_OK) {
        uint8_t v = IR_LIGHT_DUTY_PERCENT;
        if (nvs_get_u8(nvs, NVS_KEY_IR_DUTY, &v) == ESP_OK) {
            val = v;
        }
        nvs_close(nvs);
    }
    return val;
}

/* 从 NVS 读取曝光等级（默认 -1） */
static int8_t load_ae_level(void)
{
    nvs_handle_t nvs;
    int8_t val = CAM_AE_LEVEL;
    if (nvs_open(NVS_CAM_NS, NVS_READONLY, &nvs) == ESP_OK) {
        int8_t v = CAM_AE_LEVEL;
        if (nvs_get_i8(nvs, NVS_KEY_AE_LEVEL, &v) == ESP_OK) {
            val = v;
        }
        nvs_close(nvs);
    }
    return val;
}

/* 保存摄像头设置到 NVS */
static void save_cam_settings(void)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_CAM_NS, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_u8(nvs, NVS_KEY_IR_DUTY, pending_ir_duty);
        nvs_set_i8(nvs, NVS_KEY_AE_LEVEL, pending_ae_level);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

/* 防抖保存：800ms 内连续滑动只写一次 Flash */
static void schedule_cam_save(void)
{
    if (cam_save_timer == NULL) {
        cam_save_timer = lv_timer_create(cam_save_timer_cb, 800, NULL);
        if (cam_save_timer != NULL) {
            lv_timer_set_repeat_count(cam_save_timer, 1);
        } else {
            /* 定时器创建失败时仍保证用户设置不会丢失。 */
            save_cam_settings();
            cam_dirty = false;
        }
    } else {
        lv_timer_reset(cam_save_timer);
    }
}

/* 滑块回调 */
static void brightness_slider_cb(lv_event_t *e)
{
    (void)e;
    if (brightness_slider == NULL || brightness_label == NULL) return;

    uint8_t val = (uint8_t)lv_slider_get_value(brightness_slider);
    lcd_st7789_set_brightness(val);
    lv_label_set_text_fmt(brightness_label, "%d%%", val);
    pending_brightness = val;
    brightness_dirty = true;

    if (brightness_save_timer == NULL) {
        brightness_save_timer = lv_timer_create(save_brightness_timer_cb, 800, NULL);
        if (brightness_save_timer != NULL) {
            lv_timer_set_repeat_count(brightness_save_timer, 1);
        } else {
            /* 定时器创建失败时仍保证用户设置不会丢失。 */
            save_brightness(pending_brightness);
            brightness_dirty = false;
        }
    } else {
        lv_timer_reset(brightness_save_timer);
    }
}

/* 红外光强滑块回调 */
static void ir_light_slider_cb(lv_event_t *e)
{
    (void)e;
    if (ir_light_slider == NULL || ir_light_label == NULL) return;

    uint8_t val = (uint8_t)lv_slider_get_value(ir_light_slider);
    ir_light_set_duty_percent(val);
    lv_label_set_text_fmt(ir_light_label, "%d%%", val);
    pending_ir_duty = val;
    cam_dirty = true;
    schedule_cam_save();
}

/* 曝光等级滑块回调（滑块 0..4 → 等级 -2..+2） */
static void exposure_slider_cb(lv_event_t *e)
{
    (void)e;
    if (exposure_slider == NULL || exposure_label == NULL) return;

    int8_t level = (int8_t)lv_slider_get_value(exposure_slider) - 2;
    ov2640_camera_set_ae_level(level);
    lv_label_set_text_fmt(exposure_label, "%+d", level);
    pending_ae_level = level;
    cam_dirty = true;
    schedule_cam_save();
}

/* 创建一个“标签 + 滑块 + 数值”控制行 */
static void create_control_row(int y, const char *caption, lv_obj_t **slider_out,
                               lv_obj_t **value_out)
{
  lv_obj_t *label = lv_label_create(setting_page);
  lv_label_set_text(label, caption);
  lv_obj_set_width(label, 90);
  lv_obj_set_style_text_color(label, lv_color_white(), 0);
  lv_obj_set_pos(label, CTRL_LABEL_X, y + 1);

  lv_obj_t *slider = lv_slider_create(setting_page);
  lv_obj_set_size(slider, CTRL_SLIDER_W, CTRL_SLIDER_H);
  lv_obj_set_pos(slider, CTRL_SLIDER_X, y);
  lv_obj_set_style_bg_color(slider, lv_color_hex(0x003a57), LV_PART_MAIN);
  lv_obj_set_style_bg_color(slider, lv_color_hex(0x005a77), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(slider, lv_color_hex(0x00AA00), LV_PART_KNOB);

  lv_obj_t *value = lv_label_create(setting_page);
  lv_label_set_text(value, "--");
  lv_obj_set_style_text_color(value, lv_color_hex(0x00FF00), 0);
  lv_obj_align_to(value, slider, LV_ALIGN_OUT_RIGHT_MID, 8, 0);

  *slider_out = slider;
  *value_out = value;
}

void create_setting_page(void)
{
  setting_page = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(setting_page, lv_color_hex(0x003a57), LV_PART_MAIN);

  lv_obj_t *title = lv_label_create(setting_page);
  lv_label_set_text(title, "System Info");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

  // 返回按钮
  lv_obj_t *back_btn = lv_btn_create(setting_page);
  lv_obj_set_size(back_btn, 70, 30);
  lv_obj_set_pos(back_btn, 0, 0);
  lv_obj_t *back_label = lv_label_create(back_btn);
  lv_label_set_text(back_label, LV_SYMBOL_LEFT " Back");
  lv_obj_center(back_label);
  lv_obj_add_event_cb(back_btn, switch_page_cb, LV_EVENT_CLICKED, "app_page");

  /* ========== 背光亮度控制 ========== */
  create_control_row(40, "Backlight", &brightness_slider, &brightness_label);
  uint8_t saved_brightness = load_brightness();
  if (saved_brightness < 30) saved_brightness = 30;
  lv_slider_set_range(brightness_slider, 30, 100);
  lv_slider_set_value(brightness_slider, saved_brightness, LV_ANIM_OFF);
  lv_obj_add_event_cb(brightness_slider, brightness_slider_cb,
                      LV_EVENT_VALUE_CHANGED, NULL);

  /* 同步应用当前值，但不要因为打开设置页而写一次 Flash。 */
  pending_brightness = saved_brightness;
  lcd_st7789_set_brightness(saved_brightness);
  lv_label_set_text_fmt(brightness_label, "%d%%", saved_brightness);

  /* ========== 红外补光灯光强控制 ========== */
  create_control_row(70, "IR Light", &ir_light_slider, &ir_light_label);
  uint8_t saved_ir_duty = load_ir_duty();
  if (saved_ir_duty < IR_LIGHT_DUTY_MIN) saved_ir_duty = IR_LIGHT_DUTY_MIN;
  if (saved_ir_duty > IR_LIGHT_DUTY_MAX) saved_ir_duty = IR_LIGHT_DUTY_MAX;
  lv_slider_set_range(ir_light_slider, IR_LIGHT_DUTY_MIN, IR_LIGHT_DUTY_MAX);
  lv_slider_set_value(ir_light_slider, saved_ir_duty, LV_ANIM_OFF);
  lv_obj_add_event_cb(ir_light_slider, ir_light_slider_cb,
                      LV_EVENT_VALUE_CHANGED, NULL);

  /* 保存当前值；若补光灯已点亮会立即生效。 */
  pending_ir_duty = saved_ir_duty;
  ir_light_set_duty_percent(saved_ir_duty);
  lv_label_set_text_fmt(ir_light_label, "%d%%", saved_ir_duty);

  /* ========== 摄像头曝光等级控制 ========== */
  create_control_row(100, "Exposure", &exposure_slider, &exposure_label);
  int8_t saved_ae_level = load_ae_level();
  if (saved_ae_level < -2) saved_ae_level = -2;
  if (saved_ae_level > 2) saved_ae_level = 2;
  lv_slider_set_range(exposure_slider, 0, 4);
  lv_slider_set_value(exposure_slider, saved_ae_level + 2, LV_ANIM_OFF);
  lv_obj_add_event_cb(exposure_slider, exposure_slider_cb,
                      LV_EVENT_VALUE_CHANGED, NULL);

  /* 同步应用当前值，但不要因为打开设置页而写一次 Flash。 */
  pending_ae_level = saved_ae_level;
  ov2640_camera_set_ae_level(saved_ae_level);
  lv_label_set_text_fmt(exposure_label, "%+d", saved_ae_level);

  // 信息显示区域
  lv_obj_t *info_cont = lv_obj_create(setting_page);
  lv_obj_set_size(info_cont, 280, 116);
  lv_obj_align(info_cont, LV_ALIGN_TOP_MID, 0, 122);
  lv_obj_set_flex_flow(info_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_bg_opa(info_cont, 100, 0);
  lv_obj_set_style_bg_color(info_cont, lv_color_hex(0x002a47), 0);
  lv_obj_set_style_border_width(info_cont, 1, 0);
  lv_obj_set_style_border_color(info_cont, lv_color_white(), 0);
  lv_obj_set_style_pad_all(info_cont, 6, 0);
  lv_obj_set_style_pad_row(info_cont, 1, 0);
  lv_obj_set_style_text_font(info_cont, &lv_font_montserrat_10, 0);

  // 1. IDF 版本
  lv_obj_t *l_idf = lv_label_create(info_cont);
  lv_label_set_text_fmt(l_idf, "IDF: %s", esp_get_idf_version());
  lv_obj_set_style_text_color(l_idf, lv_color_white(), 0);

  // 2. LVGL 版本
  lv_obj_t *l_lvgl = lv_label_create(info_cont);
  lv_label_set_text_fmt(l_lvgl, "LVGL: %d.%d.%d", LVGL_VERSION_MAJOR,
                        LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
  lv_obj_set_style_text_color(l_lvgl, lv_color_white(), 0);

  // 3. 芯片型号
  esp_chip_info_t chip_info;
  esp_chip_info(&chip_info);
  const char *chip_name = "Unknown";
  switch (chip_info.model) {
    case CHIP_ESP32:
      chip_name = "ESP32";
      break;
    case CHIP_ESP32S2:
      chip_name = "ESP32-S2";
      break;
    case CHIP_ESP32S3:
      chip_name = "ESP32-S3";
      break;
    case CHIP_ESP32C3:
      chip_name = "ESP32-C3";
      break;
    case CHIP_ESP32C6:
      chip_name = "ESP32-C6";
      break;
    case CHIP_ESP32H2:
      chip_name = "ESP32-H2";
      break;
    default:
      chip_name = "ESP Series";
      break;
  }
  lv_obj_t *l_chip = lv_label_create(info_cont);
  lv_label_set_text_fmt(l_chip, "Chip: %s (Rev %d)", chip_name,
                        chip_info.revision);
  lv_obj_set_style_text_color(l_chip, lv_color_white(), 0);

  // 4. 触摸芯片
  lv_obj_t *l_touch = lv_label_create(info_cont);
#ifdef BOARD_ESP32_S3_EYE
  lv_label_set_text(l_touch, "Touch: None (ESP32-S3-EYE)");
#else
  lv_label_set_text(l_touch, "Touch: GT911");
#endif
  lv_obj_set_style_text_color(l_touch, lv_color_white(), 0);

  // 5. 固件版本
  lv_obj_t *l_fw = lv_label_create(info_cont);
  lv_label_set_text_fmt(l_fw, "Firmware: v%s", FIRMWARE_VERSION);
  lv_obj_set_style_text_color(l_fw, lv_color_white(), 0);

  // 6. MAC 地址
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  lv_obj_t *l_mac = lv_label_create(info_cont);
  lv_label_set_text_fmt(l_mac, "MAC: %02X:%02X:%02X:%02X:%02X:%02X", mac[0],
                        mac[1], mac[2], mac[3], mac[4], mac[5]);
  lv_obj_set_style_text_color(l_mac, lv_color_white(), 0);

  lv_obj_add_event_cb(setting_page, setting_page_delete_cb, LV_EVENT_DELETE,
                      NULL);
}
