/* 模拟器桩：替代固件 driver/inc/feeding_schedule.h */
#ifndef SIM_FEEDING_SCHEDULE_H
#define SIM_FEEDING_SCHEDULE_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_SCHEDULE_ITEMS 8
#define MAX_EVERY_DAYS     7

typedef struct {
    uint8_t hour;
    uint8_t minute;
    uint8_t amount;
    bool    enabled;
    uint8_t every_days;
} feed_schedule_item_t;

typedef void (*feeding_callback_t)(uint8_t amount);

int feed_schedule_get_count(void);
const feed_schedule_item_t *feed_schedule_get_item(int index);
esp_err_t feed_schedule_set_item(int index, const feed_schedule_item_t *item);
esp_err_t feed_schedule_add_item(const feed_schedule_item_t *item);
esp_err_t feed_schedule_remove_item(int index);
esp_err_t feed_schedule_save(void);
esp_err_t feed_schedule_load(void);

#ifdef __cplusplus
}
#endif

#endif /* SIM_FEEDING_SCHEDULE_H */
