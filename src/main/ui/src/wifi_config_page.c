#include "ui/inc/wifi_config_page.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "device/inc/wifi_app.h"
#include "device/inc/sntp_time.h"
#include "ui/inc/ui.h"

/* WIFI_AUTH_OPEN 可能未被包含（模拟器无 esp_wifi 头），统一在此定义 */
#ifndef WIFI_AUTH_OPEN
#define WIFI_AUTH_OPEN 0
#endif

lv_obj_t *wifi_config_page = NULL;

/* 键盘组件 */
static lv_obj_t *kb = NULL;
static lv_obj_t *pass_label = NULL;
static lv_obj_t *pass_ta = NULL;
static lv_obj_t *status_label = NULL;
static lv_obj_t *connect_btn = NULL;

/* 从 AP 列表选定的 SSID（只读展示，无需用户输入） */
static char s_connect_ssid[33] = {0};
static lv_obj_t *ssid_confirm_label = NULL;

/* 手动输入区容器（列表模式时隐藏） */
static lv_obj_t *manual_cont = NULL;

/* AP 列表区 */
static lv_obj_t *ap_list_cont = NULL;
static lv_obj_t *rescan_btn = NULL;

/* 扫描状态 */
#define MAX_AP_RESULTS 24
static wifi_ap_info_t s_scan_results[MAX_AP_RESULTS];
static uint16_t s_scan_count = 0;
static volatile esp_err_t s_scan_err = ESP_OK;
static volatile bool s_scan_done = false;
static bool s_scan_task_running = false;
static lv_timer_t *scan_poll_timer = NULL;
static bool s_manual_mode = false;

/* 连接状态（由连接任务写入，轮询定时器展示） */
static volatile esp_err_t s_connect_result = ESP_OK;
static volatile bool s_connect_attempted = false;
static lv_timer_t *status_poll_timer = NULL;

/* 前向声明 */
static void start_scan(void);
static void rebuild_ap_list(void);
static void set_mode(bool manual);

/* 页面删除回调 */
static void wifi_config_page_delete_cb(lv_event_t *e)
{
    wifi_config_page = NULL;
    kb = NULL;
    pass_label = NULL;
    pass_ta = NULL;
    status_label = NULL;
    connect_btn = NULL;
    ssid_confirm_label = NULL;
    manual_cont = NULL;
    ap_list_cont = NULL;
    rescan_btn = NULL;
    if (scan_poll_timer) {
        lv_timer_del(scan_poll_timer);
        scan_poll_timer = NULL;
    }
    if (status_poll_timer) {
        lv_timer_del(status_poll_timer);
        status_poll_timer = NULL;
    }
    s_scan_done = false;
    s_scan_task_running = false;
    s_scan_count = 0;
    s_connect_attempted = false;
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

/* WiFi 连接任务函数（参数为堆上拷贝的 "ssid\0password" 组合，任务内不访问 LVGL） */
static void wifi_connect_task(void *arg)
{
    char *cred = (char *)arg;
    const char *ssid = cred;
    const char *password = cred + strlen(cred) + 1;
    s_connect_result = wifi_app_connect(ssid, password);
    s_connect_attempted = true;
    vTaskDelay(pdMS_TO_TICKS(100));
    free(cred);
    vTaskDelete(NULL);
}

/* 状态轮询：连接成功后自动更新标签，避免一直停留在 "Connecting..." */
static void status_poll_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (status_label == NULL) {
        return;
    }

    if (wifi_app_is_connected()) {
        char buf[48];
        const char *ssid = wifi_app_get_ssid();
        snprintf(buf, sizeof(buf), "Connected: %s", ssid ? ssid : "?");
        lv_label_set_text(status_label, buf);
        lv_obj_set_style_text_color(status_label, lv_color_hex(0x00FF00), 0);
        s_connect_attempted = false;
        return;
    }

    if (wifi_app_is_connecting()) {
        lv_label_set_text(status_label, "Connecting...");
        lv_obj_set_style_text_color(status_label, lv_color_hex(0xFFFF00), 0);
        return;
    }

    if (s_connect_attempted) {
        s_connect_attempted = false;
        if (s_connect_result == ESP_OK) {
            /* API 调用成功但连接最终失败（重试耗尽） */
            lv_label_set_text(status_label, "Connection failed");
        } else {
            char buf[64];
            snprintf(buf, sizeof(buf), "Connect failed: %s",
                     esp_err_to_name(s_connect_result));
            lv_label_set_text(status_label, buf);
        }
        lv_obj_set_style_text_color(status_label, lv_color_hex(0xFF0000), 0);
    }
}

/* 发起连接（凭据拷贝到堆上，避免切页后 textarea 缓冲失效） */
static void start_connect(const char *ssid, const char *password)
{
    if (kb != NULL) {
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }

    if (ssid == NULL || strlen(ssid) == 0) {
        lv_label_set_text(status_label, "Please enter SSID!");
        lv_obj_set_style_text_color(status_label, lv_color_hex(0xFF0000), 0);
        return;
    }

    lv_label_set_text(status_label, "Connecting...");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xFFFF00), 0);

    /* 保存配置并连接 */
    wifi_app_save_config(ssid, password);

    size_t ssid_len = strlen(ssid) + 1;
    size_t pass_len = strlen(password) + 1;
    char *cred = malloc(ssid_len + pass_len);
    if (cred == NULL) {
        lv_label_set_text(status_label, "OOM!");
        lv_obj_set_style_text_color(status_label, lv_color_hex(0xFF0000), 0);
        return;
    }
    memcpy(cred, ssid, ssid_len);
    memcpy(cred + ssid_len, password, pass_len);

    /* 在任务中连接，避免阻塞UI */
    xTaskCreate(wifi_connect_task, "wifi_connect", 4096, cred, 5, NULL);
}

/* 连接按钮回调（从 AP 列表选定网络后输入密码连接） */
static void connect_btn_cb(lv_event_t *e)
{
    (void)e;

    const char *password = lv_textarea_get_text(pass_ta);
    start_connect(s_connect_ssid, password);
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

/* ========== AP 列表 ========== */

/* AP 行点击回调 */
static void ap_row_click_cb(lv_event_t *e)
{
    lv_obj_t *row = lv_event_get_target(e);
    int idx = (int)(intptr_t)lv_obj_get_user_data(row);
    if (idx < 0 || idx >= s_scan_count) {
        return;
    }

    const wifi_ap_info_t *ap = &s_scan_results[idx];
    if (strlen(ap->ssid) == 0) {
        return;
    }

    if (ap->authmode == WIFI_AUTH_OPEN) {
        /* 开放网络：直接连接 */
        start_connect(ap->ssid, "");
    } else {
        /* 加密网络：SSID 已选定，只需输入密码 */
        strncpy(s_connect_ssid, ap->ssid, sizeof(s_connect_ssid) - 1);
        s_connect_ssid[sizeof(s_connect_ssid) - 1] = '\0';
        if (ssid_confirm_label != NULL) {
            lv_label_set_text(ssid_confirm_label, s_connect_ssid);
        }
        lv_textarea_set_text(pass_ta, "");
        set_mode(true);
        lv_label_set_text(status_label, "Enter password");
        lv_obj_set_style_text_color(status_label, lv_color_hex(0xFFFF00), 0);
        if (kb != NULL) {
            lv_keyboard_set_textarea(kb, pass_ta);
            lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static lv_obj_t *create_ap_row(int index)
{
    const wifi_ap_info_t *ap = &s_scan_results[index];

    lv_obj_t *row = lv_obj_create(ap_list_cont);
    lv_obj_set_size(row, lv_pct(100), 34);
    lv_obj_set_style_bg_opa(row, 80, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x002a47), 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(0x005a77), 0);
    lv_obj_set_style_pad_left(row, 10, 0);
    lv_obj_set_style_radius(row, 4, 0);

    /* SSID */
    lv_obj_t *name = lv_label_create(row);
    lv_label_set_text(name, ap->ssid);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(name, lv_color_white(), 0);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 5, 0);

    /* 加密提示 + 信号强度 */
    char rssi_str[24];
    if (ap->authmode == WIFI_AUTH_OPEN) {
        snprintf(rssi_str, sizeof(rssi_str), "Open %ddBm", ap->rssi);
    } else {
        snprintf(rssi_str, sizeof(rssi_str), "PSK %ddBm", ap->rssi);
    }
    lv_obj_t *rssi = lv_label_create(row);
    lv_label_set_text(rssi, rssi_str);
    lv_obj_set_style_text_font(rssi, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(rssi, lv_color_hex(0xAAAAAA), 0);
    lv_obj_align(rssi, LV_ALIGN_RIGHT_MID, -10, 0);

    lv_obj_set_user_data(row, (void *)(intptr_t)index);
    lv_obj_add_event_cb(row, ap_row_click_cb, LV_EVENT_CLICKED, NULL);

    return row;
}

/* 根据扫描结果重建 AP 列表 */
static void rebuild_ap_list(void)
{
    if (ap_list_cont == NULL) {
        return;
    }

    lv_obj_clean(ap_list_cont);

    if (s_scan_err != ESP_OK) {
        esp_err_t err = s_scan_err;
        s_scan_err = ESP_OK;
        lv_obj_t *l = lv_label_create(ap_list_cont);
        char buf[64];
        snprintf(buf, sizeof(buf), "Scan failed: %s.\nTap Scan to retry.",
                 esp_err_to_name(err));
        lv_label_set_text(l, buf);
        lv_obj_set_style_text_color(l, lv_color_hex(0xFF0000), 0);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(l, lv_pct(100));
        return;
    }

    if (s_scan_count == 0) {
        lv_obj_t *l = lv_label_create(ap_list_cont);
        lv_label_set_text(l, "No networks found.\nTap Scan to retry.");
        lv_obj_set_style_text_color(l, lv_color_hex(0x888888), 0);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(l, lv_pct(100));
        return;
    }

    for (int i = 0; i < s_scan_count; i++) {
        create_ap_row(i);
    }
}

/* ========== 扫描逻辑 ========== */

/* 扫描任务（不访问 LVGL，只写静态缓冲并置标志） */
static void wifi_scan_task(void *arg)
{
    (void)arg;
    uint16_t count = 0;
    esp_err_t err = wifi_app_scan(s_scan_results, &count, MAX_AP_RESULTS);
    s_scan_count = count;
    s_scan_err = err;
    s_scan_done = true;
    vTaskDelete(NULL);
}

/* LVGL 轮询定时器：扫描完成后渲染列表（避免跨任务碰 LVGL） */
static void scan_poll_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!s_scan_done) {
        return;
    }

    s_scan_done = false;
    s_scan_task_running = false;
    if (scan_poll_timer) {
        lv_timer_del(scan_poll_timer);
        scan_poll_timer = NULL;
    }
    rebuild_ap_list();
}

static void start_scan(void)
{
    if (s_scan_task_running) {
        return;
    }

    /* 先显示 "Scanning..." 占位 */
    if (ap_list_cont != NULL) {
        lv_obj_clean(ap_list_cont);
        lv_obj_t *l = lv_label_create(ap_list_cont);
        lv_label_set_text(l, "Scanning...");
        lv_obj_set_style_text_color(l, lv_color_hex(0xFFFF00), 0);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(l, lv_pct(100));
    }

    s_scan_count = 0;
    s_scan_done = false;
    s_scan_err = ESP_OK;
    s_scan_task_running = true;

    if (scan_poll_timer == NULL) {
        scan_poll_timer = lv_timer_create(scan_poll_timer_cb, 100, NULL);
    }

    xTaskCreate(wifi_scan_task, "wifi_scan", 4096, NULL, 5, NULL);
}

/* ========== 模式切换 ========== */

static void set_mode(bool manual)
{
    s_manual_mode = manual;
    if (manual) {
        lv_obj_clear_flag(manual_cont, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ap_list_cont, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(rescan_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(connect_btn, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(manual_cont, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ap_list_cont, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(rescan_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(connect_btn, LV_OBJ_FLAG_HIDDEN);
    }
}

/* Scan/Rescan 按钮回调：回到列表模式并重新扫描 */
static void rescan_btn_cb(lv_event_t *e)
{
    (void)e;
    if (s_manual_mode) {
        set_mode(false);
    }
    start_scan();
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

    /* ========== 手动输入区（container，列表模式下隐藏） ========== */
    manual_cont = lv_obj_create(wifi_config_page);
    lv_obj_set_size(manual_cont, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(manual_cont, 0, 0);
    lv_obj_set_style_bg_opa(manual_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(manual_cont, 0, 0);
    lv_obj_add_flag(manual_cont, LV_OBJ_FLAG_HIDDEN);

    /* 选定的 SSID 只读展示（密码输入界面顶部） */
    ssid_confirm_label = lv_label_create(manual_cont);
    lv_label_set_text(ssid_confirm_label, "");
    lv_obj_set_style_text_font(ssid_confirm_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ssid_confirm_label, lv_color_hex(0x00AAFF), 0);
    lv_obj_align(ssid_confirm_label, LV_ALIGN_TOP_MID, 0, 25);

    /* 密码输入 */
    pass_label = lv_label_create(manual_cont);
    lv_label_set_text(pass_label, "Password:");
    lv_obj_set_style_text_color(pass_label, lv_color_white(), 0);
    lv_obj_align(pass_label, LV_ALIGN_TOP_MID, -95, 40);

    pass_ta = lv_textarea_create(manual_cont);
    lv_textarea_set_placeholder_text(pass_ta, "Enter WiFi password");
    lv_textarea_set_max_length(pass_ta, 63);
    lv_textarea_set_password_mode(pass_ta, false);
    lv_obj_set_size(pass_ta, 260, 35);
    lv_obj_align(pass_ta, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_set_style_text_color(pass_ta, lv_color_white(), 0);
    lv_obj_set_style_bg_color(pass_ta, lv_color_hex(0x001a27), 0);
    lv_obj_set_style_border_width(pass_ta, 1, 0);
    lv_obj_set_style_border_color(pass_ta, lv_color_hex(0x005a77), 0);
    lv_obj_add_event_cb(pass_ta, ta_event_cb, LV_EVENT_ALL, NULL);

    /* ========== AP 列表区 ========== */
    rescan_btn = lv_btn_create(wifi_config_page);
    lv_obj_set_size(rescan_btn, 70, 30);
    lv_obj_set_pos(rescan_btn, 250, 0);
    lv_obj_t *rescan_label = lv_label_create(rescan_btn);
    lv_label_set_text(rescan_label, LV_SYMBOL_REFRESH " Scan");
    lv_obj_center(rescan_label);
    lv_obj_add_event_cb(rescan_btn, rescan_btn_cb, LV_EVENT_CLICKED, NULL);

    ap_list_cont = lv_obj_create(wifi_config_page);
    lv_obj_set_size(ap_list_cont, 300, 165);
    lv_obj_align(ap_list_cont, LV_ALIGN_TOP_MID, 0, 45);
    lv_obj_set_flex_flow(ap_list_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ap_list_cont, 5, 0);
    lv_obj_set_style_pad_all(ap_list_cont, 5, 0);
    lv_obj_set_style_bg_opa(ap_list_cont, 0, 0);
    lv_obj_set_style_border_width(ap_list_cont, 1, 0);
    lv_obj_set_style_border_color(ap_list_cont, lv_color_hex(0x005a77), 0);
    lv_obj_set_scrollbar_mode(ap_list_cont, LV_SCROLLBAR_MODE_AUTO);

    /* 状态标签（底部） */
    status_label = lv_label_create(wifi_config_page);
    lv_label_set_text(status_label, "");
    lv_obj_set_style_text_color(status_label, lv_color_white(), 0);
    lv_obj_align(status_label, LV_ALIGN_BOTTOM_MID, 0, -10);

    /* 连接状态轮询：连接成功/失败后自动更新底部标签 */
    status_poll_timer = lv_timer_create(status_poll_timer_cb, 500, NULL);

    /* 键盘 (初始隐藏) */
    kb = lv_keyboard_create(wifi_config_page);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(kb, pass_ta);
    lv_obj_add_event_cb(kb, kb_event_cb, LV_EVENT_ALL, NULL);

    /* 返回/连接按钮最后创建，保证始终在最上层（不被手动输入区覆盖） */
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

    lv_obj_add_event_cb(wifi_config_page, wifi_config_page_delete_cb,
                        LV_EVENT_DELETE, NULL);

    /* 默认列表模式 + 自动扫描 */
    set_mode(false);
    start_scan();

    return wifi_config_page;
}
