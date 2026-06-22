#include "ui/inc/ui.h"

#include <string.h>

#include "ui/inc/app_page.h"
#include "ui/inc/setting_page.h"
#include "ui/inc/wifi_config_page.h"
#include "ui/inc/feeding_page.h"

// 通用消抖函数
bool check_debounce(uint32_t *last_time, uint32_t debounce_ms)
{
  uint32_t current_time = lv_tick_get();

  if (current_time - *last_time < debounce_ms) {
    return false;  // 在消抖时间内，忽略
  }

  *last_time = current_time;
  return true;  // 允许执行
}

void switch_page_cb(lv_event_t *e)
{
  const char *page_name = (const char *)lv_event_get_user_data(e);
  lv_obj_t *target = NULL;

  if (strcmp(page_name, "app_page") == 0) {
    target = create_app_page();
  } else if (strcmp(page_name, "settings_page") == 0) {
    create_setting_page();
    target = setting_page;
  } else if (strcmp(page_name, "wifi_config_page") == 0) {
    target = create_wifi_config_page();
  } else if (strcmp(page_name, "feeding_page") == 0) {
    target = create_feeding_page();
  }

  if (target) {
    lv_scr_load_anim(target, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
  }
}

void create_ui(void)
{
  lv_obj_t *page = create_app_page();
  lv_scr_load(page);
}
