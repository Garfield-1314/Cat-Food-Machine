#include "ui/inc/wifi_config_page.h"

#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "device/inc/wifi_app.h"
#include "device/inc/sntp_time.h"
#include "ui/inc/ui.h"

static const char *TAG = "wifi_config_page";

lv_obj_t *wifi_config_page = NULL;

/* 键盘组件 */
static lv_obj_t *kb = NULL;
static lv_obj_t *ssid_ta = NULL;
static lv_obj_t *pass_ta = NULL;
static lv_obj_t *status_label = NULL;
static lv_obj_t *connect_btn = NULL;

/* 页面删除回调 */
static void wifi_config_page_delete_cb(lv_event_t *e)
{
    wifi_config_page = NULL;
    kb = NULL;
    ssid_ta = NULL;
    pass_ta = NULL;
    status_label = NULL;
    connect_btn = NULL;
}

/* 文本框聚焦时自动弹出键盘 */
static void ta_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = lv_event_get_target(e);

    if (code == LV_EVENT_FOCUSED) {
        if (kb != NULL) {
            lv_keyboard_set_textarea(kb, ta);
            lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
        }
    } else if (code == LV_EVENT_DEFOCUSED) {
        if (kb != NULL) {
            lv_keyboard_set_textarea(kb, NULL);
            lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/* WiFi 连接任务函数 */
static void wifi_connect_task(void *arg)
{
    const char *ssid = (const char *)arg;
    /* pass_ta 是全局静态变量，可以直接访问 */
    const char *password = lv_textarea_get_text(pass_ta);
    wifi_app_connect(ssid, password);
    vTaskDelay(pdMS_TO_TICKS(100));
    vTaskDelete(NULL);
}

/* 连接按钮回调 */
static void connect_btn_cb(lv_event_t *e)
{
    if (kb != NULL) {
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }

    const char *ssid = lv_textarea_get_text(ssid_ta);
    const char *password = lv_textarea_get_text(pass_ta);

    if (strlen(ssid) == 0) {
        lv_label_set_text(status_label, "Please enter SSID!");
        return;
    }

    lv_label_set_text(status_label, "Connecting...");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xFFFF00), 0);

    /* 保存配置并连接 */
    wifi_app_save_config(ssid, password);

    /* 在任务中连接，避免阻塞UI */
    xTaskCreate(wifi_connect_task, "wifi_connect", 4096, (void *)ssid, 5, NULL);
}

/* 键盘的"完成/连接"按钮回调 - 处理键盘关闭和触发的回车 */
static void kb_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        /* 用户点击键盘的完成按钮 */
        if (kb != NULL) {
            lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
        }

        /* 如果有焦点在文本区域，移除焦点 */
        lv_obj_t *focused = lv_group_get_focused(lv_group_get_default());
        if (focused != NULL) {
            lv_obj_clear_state(focused, LV_STATE_FOCUSED);
        }
    }
}

lv_obj_t *create_wifi_config_page(void)
{
    if (wifi_config_page != NULL) {
        lv_scr_load(wifi_config_page);
        return wifi_config_page;
    }

    wifi_config_page = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(wifi_config_page, lv_color_hex(0x003a57), LV_PART_MAIN);

    /* 标题 */
    lv_obj_t *title = lv_label_create(wifi_config_page);
    lv_label_set_text(title, "WiFi Settings");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    /* 返回按钮 */
    lv_obj_t *back_btn = lv_btn_create(wifi_config_page);
    lv_obj_set_size(back_btn, 70, 30);
    lv_obj_set_pos(back_btn, 0, 0);
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT " Back");
    lv_obj_center(back_label);
    lv_obj_add_event_cb(back_btn, switch_page_cb, LV_EVENT_CLICKED, "app_page");

    /* 连接按钮（右上角，与 Back 对称） */
    connect_btn = lv_btn_create(wifi_config_page);
    lv_obj_set_size(connect_btn, 70, 30);
    lv_obj_set_pos(connect_btn, 250, 0);
    lv_obj_t *btn_label = lv_label_create(connect_btn);
    lv_label_set_text(btn_label, "Connect");
    lv_obj_center(btn_label);
    lv_obj_add_event_cb(connect_btn, connect_btn_cb, LV_EVENT_CLICKED, NULL);

    /* SSID 输入（上半部分） */
    lv_obj_t *ssid_label = lv_label_create(wifi_config_page);
    lv_label_set_text(ssid_label, "SSID:");
    lv_obj_set_style_text_color(ssid_label, lv_color_white(), 0);
    lv_obj_align(ssid_label, LV_ALIGN_TOP_MID, -110, 55);

    ssid_ta = lv_textarea_create(wifi_config_page);
    lv_textarea_set_placeholder_text(ssid_ta, "Enter WiFi name");
    lv_textarea_set_max_length(ssid_ta, 31);
    lv_obj_set_size(ssid_ta, 260, 35);
    lv_obj_align(ssid_ta, LV_ALIGN_TOP_MID, 0, 75);
    lv_obj_set_style_text_color(ssid_ta, lv_color_white(), 0);
    lv_obj_set_style_bg_color(ssid_ta, lv_color_hex(0x001a27), 0);
    lv_obj_set_style_border_width(ssid_ta, 1, 0);
    lv_obj_set_style_border_color(ssid_ta, lv_color_hex(0x005a77), 0);
    lv_obj_add_event_cb(ssid_ta, ta_event_cb, LV_EVENT_ALL, NULL);

    /* 密码输入（下半部分） */
    lv_obj_t *pass_label = lv_label_create(wifi_config_page);
    lv_label_set_text(pass_label, "Password:");
    lv_obj_set_style_text_color(pass_label, lv_color_white(), 0);
    lv_obj_align(pass_label, LV_ALIGN_TOP_MID, -95, 130);

    pass_ta = lv_textarea_create(wifi_config_page);
    lv_textarea_set_placeholder_text(pass_ta, "Enter WiFi password");
    lv_textarea_set_max_length(pass_ta, 63);
    lv_textarea_set_password_mode(pass_ta, true);
    lv_obj_set_size(pass_ta, 260, 35);
    lv_obj_align(pass_ta, LV_ALIGN_TOP_MID, 0, 150);
    lv_obj_set_style_text_color(pass_ta, lv_color_white(), 0);
    lv_obj_set_style_bg_color(pass_ta, lv_color_hex(0x001a27), 0);
    lv_obj_set_style_border_width(pass_ta, 1, 0);
    lv_obj_set_style_border_color(pass_ta, lv_color_hex(0x005a77), 0);
    lv_obj_add_event_cb(pass_ta, ta_event_cb, LV_EVENT_ALL, NULL);

    /* 状态标签（底部） */
    status_label = lv_label_create(wifi_config_page);
    lv_label_set_text(status_label, "");
    lv_obj_set_style_text_color(status_label, lv_color_white(), 0);
    lv_obj_align(status_label, LV_ALIGN_BOTTOM_MID, 0, -10);

    /* 键盘 (初始隐藏) */
    kb = lv_keyboard_create(wifi_config_page);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(kb, ssid_ta);
    lv_obj_add_event_cb(kb, kb_event_cb, LV_EVENT_ALL, NULL);

    lv_obj_add_event_cb(wifi_config_page, wifi_config_page_delete_cb,
                        LV_EVENT_DELETE, NULL);

    return wifi_config_page;
}