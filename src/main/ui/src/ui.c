#include "ui/inc/ui.h"

#include <string.h>

#include "ui/inc/app_page.h"
#include "ui/inc/setting_page.h"
#include "ui/inc/wifi_config_page.h"
#include "ui/inc/feeding_page.h"

static bool s_page_switch_pending = false;

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

static void switch_page_now(void *user_data)
{
  const char *page_name = (const char *)user_data;
  lv_obj_t *target = NULL;
  lv_obj_t *old_screen = lv_scr_act();

  s_page_switch_pending = false;

  if (page_name == NULL) {
    return;
  }

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

  if (target != NULL) {
    /*
     * LVGL requires an active screen while loading another screen.  Loading
     * first also gives the old page's delete callback a valid transition
     * point, after which it is safe to release the old page and its timers.
     */
    lv_scr_load(target);

    if (old_screen != NULL && old_screen != target && lv_obj_is_valid(old_screen)) {
      lv_obj_del(old_screen);
    }
  }
}

void switch_page_cb(lv_event_t *e)
{
  const char *page_name = (const char *)lv_event_get_user_data(e);
  if (page_name == NULL) {
    return;
  }

  if (s_page_switch_pending) {
    return;
  }
  s_page_switch_pending = true;

  /* Defer deletion until the current LVGL input event has completed. */
  if (lv_async_call(switch_page_now, (void *)page_name) != LV_RES_OK) {
    /* If the async timer cannot be allocated, perform the same safe
     * load-then-delete transition synchronously. */
    s_page_switch_pending = false;
    switch_page_now((void *)page_name);
  }
}

void create_ui(void)
{
  /* 开机默认显示主界面 */
  lv_obj_t *page = create_app_page();
  lv_scr_load(page);
}
