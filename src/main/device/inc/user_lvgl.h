#ifndef __USER_LVGL_H
#define __USER_LVGL_H

#include "lvgl.h"
#include "esp_err.h"

esp_err_t user_lvgl_init(void);
void lvgl_event_task(void *arg);

#endif  // __USER_LVGL_H
