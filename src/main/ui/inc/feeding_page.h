#ifndef __FEEDING_PAGE_H
#define __FEEDING_PAGE_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

extern lv_obj_t *feeding_page;

lv_obj_t *create_feeding_page(void);

/**
 * @brief 若投喂计划页面已打开，刷新列表显示
 */
void feeding_page_refresh(void);

#ifdef __cplusplus
}
#endif

#endif /* __FEEDING_PAGE_H */