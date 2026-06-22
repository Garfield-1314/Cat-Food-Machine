idf_component_register(
SRCS "main.c"

"driver/src/tusb_serial.c"
"driver/src/feeder_motor.c"
"driver/src/feeding_schedule.c"

"device/src/st7789.c"
"device/src/gt911.c"
"device/src/user_lvgl.c"
"device/src/wifi_app.c"
"device/src/sntp_time.c"

"ui/src/ui.c"
"ui/src/app_page.c"
"ui/src/setting_page.c"
"ui/src/wifi_config_page.c"
"ui/src/feeding_page.c"

                    INCLUDE_DIRS "."
                    REQUIRES esp_lcd esp_driver_i2c nvs_flash esp_timer esp_wifi esp_netif esp_event lwip
)
