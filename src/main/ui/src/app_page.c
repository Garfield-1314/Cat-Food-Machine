#include "ui/inc/app_page.h"

#include <string.h>
#include "ui/inc/setting_page.h"
#include "ui/inc/ui.h"
#include "device/inc/wifi_app.h"
#include "device/inc/sntp_time.h"
#include "include.h"

// 创建APP界面（开机默认显示）
lv_obj_t *app_page;

/* 时间显示标签 */
static lv_obj_t *time_label = NULL;
static lv_obj_t *date_label = NULL;
static lv_obj_t *wifi_status_label = NULL;
static lv_obj_t *wifi_icon_label = NULL;

/* 定时刷新任务 */
static void app_page_timer_cb(lv_timer_t *timer)
{
  /* 更新时间显示：区分 WiFi 和 SNTP 状态 */
  if (sntp_time_is_synced()) {
    char time_str[16];
    char date_str[32];
    sntp_time_get_str(time_str, sizeof(time_str));
    sntp_time_get_date_str(date_str, sizeof(date_str));

    if (time_label) {
      lv_label_set_text(time_label, time_str);
    }
    if (date_label) {
      lv_label_set_text(date_label, date_str);
    }
  } else if (wifi_app_is_connected()) {
    /* WiFi 已连接但时间还未同步 */
    if (time_label) {
      lv_label_set_text(time_label, "Syncing...");
    }
  } else {
    /* WiFi 未连接 */
    if (time_label) {
      lv_label_set_text(time_label, "--:--:--");
    }
  }

  /* 更新 WiFi 状态 */
  if (wifi_status_label && wifi_icon_label) {
    if (wifi_app_is_connected()) {
      lv_label_set_text(wifi_status_label, wifi_app_get_ssid());
      lv_label_set_text(wifi_icon_label, LV_SYMBOL_WIFI);
      lv_obj_set_style_text_color(wifi_icon_label, lv_color_hex(0x00FF00), 0);
    } else {
      lv_label_set_text(wifi_status_label, "Not connected");
      lv_label_set_text(wifi_icon_label, LV_SYMBOL_CLOSE);
      lv_obj_set_style_text_color(wifi_icon_label, lv_color_hex(0xFF0000), 0);
    }
  }
}

/* 手动喂食按钮回调 */
static void manual_feed_btn_cb(lv_event_t *e)
{
    (void)e;
    manual_feeding_start(1);
}

static lv_timer_t *app_timer = NULL;

static void app_page_delete_cb(lv_event_t *e)
{
  app_page = NULL;
  time_label = NULL;
  date_label = NULL;
  wifi_status_label = NULL;
  wifi_icon_label = NULL;
  if (app_timer) {
    lv_timer_del(app_timer);
    app_timer = NULL;
  }
}

lv_obj_t *create_app_page(void)
{
  app_page = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(app_page, lv_color_hex(0x003a57), LV_PART_MAIN);

  // 标题
  lv_obj_t *title = lv_label_create(app_page);
  lv_label_set_text(title, "Cat Food Machine");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

  /* 手动喂食按钮（右上角） */
  lv_obj_t *manual_feed_btn = lv_btn_create(app_page);
  lv_obj_set_size(manual_feed_btn, 60, 30);
  lv_obj_set_pos(manual_feed_btn, 260, 0);
  lv_obj_t *manual_feed_label = lv_label_create(manual_feed_btn);
  lv_label_set_text(manual_feed_label, LV_SYMBOL_PLAY " Feed");
  lv_obj_center(manual_feed_label);
  lv_obj_add_event_cb(manual_feed_btn, manual_feed_btn_cb, LV_EVENT_CLICKED, NULL);

  /* WiFi 状态行（仅显示状态，不提供按钮） */
  wifi_icon_label = lv_label_create(app_page);
  lv_label_set_text(wifi_icon_label, LV_SYMBOL_CLOSE);
  lv_obj_set_style_text_color(wifi_icon_label, lv_color_hex(0xFF0000), 0);
  lv_obj_align(wifi_icon_label, LV_ALIGN_TOP_LEFT, 10, 35);

  wifi_status_label = lv_label_create(app_page);
  lv_label_set_text(wifi_status_label, "Not connected");
  lv_obj_set_style_text_color(wifi_status_label, lv_color_hex(0xCCCCCC), 0);
  lv_obj_align_to(wifi_status_label, wifi_icon_label, LV_ALIGN_OUT_RIGHT_MID, 5, 0);

  /* 时间显示（大数字时钟） */
  time_label = lv_label_create(app_page);
  lv_label_set_text(time_label, "--:--:--");
  lv_obj_set_style_text_font(time_label, &lv_font_montserrat_48, 0);
  lv_obj_set_style_text_color(time_label, lv_color_hex(0xFFFFFF), 0);
  lv_obj_align(time_label, LV_ALIGN_CENTER, 0, -20);

  /* 日期显示 */
  date_label = lv_label_create(app_page);
  lv_label_set_text(date_label, "----/--/--");
  lv_obj_set_style_text_font(date_label, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(date_label, lv_color_hex(0xAAAAAA), 0);
  lv_obj_align(date_label, LV_ALIGN_CENTER, 0, 20);

  // 底部应用图标区域
  // WiFi 设置应用图标
  lv_obj_t *wifi_app_btn = lv_btn_create(app_page);
  lv_obj_set_size(wifi_app_btn, 60, 60);
  lv_obj_align(wifi_app_btn, LV_ALIGN_BOTTOM_LEFT, 55, -25);
  lv_obj_t *wifi_app_label = lv_label_create(wifi_app_btn);
  lv_label_set_text(wifi_app_label, LV_SYMBOL_WIFI);
  lv_obj_center(wifi_app_label);
  lv_obj_add_event_cb(wifi_app_btn, switch_page_cb, LV_EVENT_CLICKED,
                      "wifi_config_page");

  // 投喂计划应用图标
  lv_obj_t *feed_btn = lv_btn_create(app_page);
  lv_obj_set_size(feed_btn, 60, 60);
  lv_obj_align(feed_btn, LV_ALIGN_BOTTOM_MID, 0, -25);
  lv_obj_t *feed_btn_label = lv_label_create(feed_btn);
  lv_label_set_text(feed_btn_label, LV_SYMBOL_LIST);
  lv_obj_center(feed_btn_label);
  lv_obj_add_event_cb(feed_btn, switch_page_cb, LV_EVENT_CLICKED,
                      "feeding_page");

  // 设置功能按钮（圆形图标）
  lv_obj_t *settings_btn = lv_btn_create(app_page);
  lv_obj_set_size(settings_btn, 60, 60);
  lv_obj_align(settings_btn, LV_ALIGN_BOTTOM_RIGHT, -55, -25);
  lv_obj_t *settings_label = lv_label_create(settings_btn);
  lv_label_set_text(settings_label, LV_SYMBOL_SETTINGS);
  lv_obj_center(settings_label);
  lv_obj_add_event_cb(settings_btn, switch_page_cb, LV_EVENT_CLICKED,
                      "settings_page");

  /* 启动定时器，每 1 秒刷新一次 */
  app_timer = lv_timer_create(app_page_timer_cb, 1000, NULL);
  lv_timer_ready(app_timer);  // 立即执行一次

  lv_obj_add_event_cb(app_page, app_page_delete_cb, LV_EVENT_DELETE, NULL);

  return app_page;
}