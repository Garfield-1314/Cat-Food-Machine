#ifndef __FEEDING_CONTROL_H
#define __FEEDING_CONTROL_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start a manual feed asynchronously.
 *
 * The call is safe from the LVGL task, scheduler task, or HTTP task.  It does
 * not call LVGL directly; the UI task observes the request through its normal
 * timer and updates the local display.
 */
esp_err_t manual_feeding_start(uint8_t slots);

/**
 * @brief Get a stable snapshot of the application-level feeding status.
 */
void feeding_get_status(bool *active, uint8_t *amount);

#ifdef __cplusplus
}
#endif

#endif /* __FEEDING_CONTROL_H */
