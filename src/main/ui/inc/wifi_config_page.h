#ifndef __WIFI_CONFIG_PAGE_H
#define __WIFI_CONFIG_PAGE_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

extern lv_obj_t *wifi_config_page;

lv_obj_t *create_wifi_config_page(void);

#ifdef __cplusplus
}
#endif

#endif /* __WIFI_CONFIG_PAGE_H */