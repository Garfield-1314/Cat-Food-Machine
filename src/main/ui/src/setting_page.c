#include "ui/inc/setting_page.h"

#include "esp_chip_info.h"
#include "esp_idf_version.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "ui/inc/ui.h"
#include "ui/inc/wifi_config_page.h"
#include "device/inc/st7789.h"

#define FIRMWARE_VERSION "0.0.1"

/* NVS 背光亮度存储 */
#define NVS_NAMESPACE  "lcd_config"
#define NVS_KEY_BRIGHT "brightness"

lv_obj_t *setting_page;

/* 背光亮度滑块 */
static lv_obj_t *brightness_slider = NULL;
static lv_obj_t *brightness_label = NULL;

static void setting_page_delete_cb(lv_event_t *e)
{
  setting_page = NULL;
  brightness_slider = NULL;
  brightness_label = NULL;
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

/* 滑块回调 */
static void brightness_slider_cb(lv_event_t *e)
{
    (void)e;
    if (brightness_slider == NULL || brightness_label == NULL) return;

    uint8_t val = (uint8_t)lv_slider_get_value(brightness_slider);
    lcd_st7789_set_brightness(val);
    lv_label_set_text_fmt(brightness_label, "%d%%", val);
    save_brightness(val);
}

void create_setting_page(void)
{
  setting_page = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(setting_page, lv_color_hex(0x003a57), LV_PART_MAIN);

  lv_obj_t *title = lv_label_create(setting_page);
  lv_label_set_text(title, "System Info");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

  // 返回按钮
  lv_obj_t *back_btn = lv_btn_create(setting_page);
  lv_obj_set_size(back_btn, 70, 30);
  lv_obj_set_pos(back_btn, 0, 0);
  lv_obj_t *back_label = lv_label_create(back_btn);
  lv_label_set_text(back_label, LV_SYMBOL_LEFT " Back");
  lv_obj_center(back_label);
  lv_obj_add_event_cb(back_btn, switch_page_cb, LV_EVENT_CLICKED, "app_page");

  // 信息显示区域
  lv_obj_t *info_cont = lv_obj_create(setting_page);
  lv_obj_set_size(info_cont, 280, 180);
  lv_obj_align(info_cont, LV_ALIGN_TOP_MID, 0, 45);
  lv_obj_set_flex_flow(info_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_bg_opa(info_cont, 100, 0);
  lv_obj_set_style_bg_color(info_cont, lv_color_hex(0x002a47), 0);
  lv_obj_set_style_border_width(info_cont, 1, 0);
  lv_obj_set_style_border_color(info_cont, lv_color_white(), 0);
  lv_obj_set_style_pad_all(info_cont, 10, 0);
  lv_obj_set_style_pad_row(info_cont, 5, 0);

  // 1. IDF 版本
  lv_obj_t *l_idf = lv_label_create(info_cont);
  lv_label_set_text_fmt(l_idf, "IDF Version: %s", esp_get_idf_version());
  lv_obj_set_style_text_color(l_idf, lv_color_white(), 0);

  // 2. LVGL 版本
  lv_obj_t *l_lvgl = lv_label_create(info_cont);
  lv_label_set_text_fmt(l_lvgl, "LVGL Version: %d.%d.%d", LVGL_VERSION_MAJOR,
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
  lv_label_set_text(l_touch, "Touch: GT911");
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

  /* ========== 背光亮度控制 ========== */
  lv_obj_t *bl_label = lv_label_create(setting_page);
  lv_label_set_text(bl_label, "Backlight");
  lv_obj_set_style_text_color(bl_label, lv_color_white(), 0);
  lv_obj_align(bl_label, LV_ALIGN_TOP_LEFT, 20, 235);

  brightness_label = lv_label_create(setting_page);
  lv_label_set_text(brightness_label, "100%");
  lv_obj_set_style_text_color(brightness_label, lv_color_hex(0x00FF00), 0);
  lv_obj_align_to(brightness_label, bl_label, LV_ALIGN_OUT_RIGHT_MID, 10, 0);

  brightness_slider = lv_slider_create(setting_page);
  lv_obj_set_size(brightness_slider, 200, 20);
  lv_slider_set_range(brightness_slider, 30, 100);
  lv_obj_align(brightness_slider, LV_ALIGN_TOP_LEFT, 20, 260);
  lv_obj_set_style_bg_color(brightness_slider, lv_color_hex(0x003a57), LV_PART_MAIN);
  lv_obj_set_style_bg_color(brightness_slider, lv_color_hex(0x005a77), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(brightness_slider, lv_color_hex(0x00AA00), LV_PART_KNOB);
  lv_slider_set_value(brightness_slider, load_brightness(), LV_ANIM_OFF);
  lv_obj_add_event_cb(brightness_slider, brightness_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

  /* 同步显示当前值 */
  brightness_slider_cb(NULL);

  lv_obj_add_event_cb(setting_page, setting_page_delete_cb, LV_EVENT_DELETE,
                      NULL);
}