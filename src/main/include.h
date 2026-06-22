#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "driver/inc/tusb_serial.h"
#include "device/inc/user_lvgl.h"
#include "device/inc/st7789.h"
#include "device/inc/gt911.h"
#include "device/inc/wifi_app.h"
#include "device/inc/sntp_time.h"

#include "ui/inc/ui.h"

#include "driver/inc/feeder_motor.h"
#include "driver/inc/feeding_schedule.h"

/* 手动喂食 */
void manual_feeding_start(uint8_t slots);
