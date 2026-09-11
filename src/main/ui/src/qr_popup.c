#include "ui/inc/qr_popup.h"

#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "device/inc/mqtt_client.h"
#include "device/inc/qrcode_gen.h"
#include "device/inc/st7789.h"

/* 二维码弹窗显示时长（毫秒） */
#define QR_POPUP_TIMEOUT_MS 20000

/*
 * 画布 148px 时绑定数据(v5, 37 模块)恰好 4px/模块；
 * 白色底框 180px 提供 (180-148)/2 = 16px = 4 模块静区。
 */
#define QR_CANVAS_SIZE 148
#define QR_FRAME_SIZE  180

static lv_obj_t *s_qr_popup = NULL;
static lv_timer_t *s_close_timer = NULL;

static void qr_popup_close(void)
{
    if (s_close_timer != NULL) {
        lv_timer_del(s_close_timer);
        s_close_timer = NULL;
    }

    if (s_qr_popup != NULL) {
        if (lv_obj_is_valid(s_qr_popup)) {
            lv_obj_del(s_qr_popup);
        }
        s_qr_popup = NULL;
    }
}

/* 一次性定时器回调：LVGL 会在回调返回后自行删除该定时器 */
static void qr_popup_close_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    s_close_timer = NULL;
    qr_popup_close();
}

void qr_popup_hide(void)
{
    qr_popup_close();
}

static void qr_popup_clicked_cb(lv_event_t *e)
{
    (void)e;
    qr_popup_close();
}

/* 创建白色底框 + 二维码，失败时返回 NULL */
static lv_obj_t *create_qrcode(lv_obj_t *parent, const char *data)
{
    lv_obj_t *frame = lv_obj_create(parent);
    if (frame == NULL) {
        return NULL;
    }

    lv_obj_set_size(frame, QR_FRAME_SIZE, QR_FRAME_SIZE);
    lv_obj_set_style_bg_color(frame, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(frame, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(frame, 0, 0);
    lv_obj_set_style_radius(frame, 4, 0);
    lv_obj_set_style_pad_all(frame, 0, 0);
    lv_obj_clear_flag(frame, LV_OBJ_FLAG_SCROLLABLE);
    /* 让点击穿过白边落到遮罩上，便于点击关闭 */
    lv_obj_clear_flag(frame, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *qr = lv_qrcode_create(frame, QR_CANVAS_SIZE,
                                    lv_color_black(), lv_color_white());
    if (qr == NULL) {
        lv_obj_del(frame);
        return NULL;
    }
    lv_obj_center(qr);

    if (lv_qrcode_update(qr, data, strlen(data)) != LV_RES_OK) {
        lv_obj_del(frame);
        return NULL;
    }

    return frame;
}

static lv_obj_t *create_hint_label(lv_obj_t *parent, int y, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(label, QR_FRAME_SIZE);
    lv_obj_set_pos(label, (LCD_WIDTH - QR_FRAME_SIZE) / 2, y);
    return label;
}

void qr_popup_show(void)
{
    /* 已打开则先关闭，保证数据刷新 */
    if (s_qr_popup != NULL) {
        qr_popup_close();
    }

    s_qr_popup = lv_obj_create(lv_layer_top());
    if (s_qr_popup == NULL) {
        return;
    }

    lv_obj_set_size(s_qr_popup, lv_pct(100), lv_pct(100));
    lv_obj_center(s_qr_popup);
    lv_obj_set_style_bg_color(s_qr_popup, lv_color_hex(0x001a27), 0);
    lv_obj_set_style_bg_opa(s_qr_popup, 235, 0);
    lv_obj_set_style_border_width(s_qr_popup, 2, 0);
    lv_obj_set_style_border_color(s_qr_popup, lv_color_hex(0x00AA00), 0);
    lv_obj_set_style_radius(s_qr_popup, 10, 0);
    lv_obj_set_style_pad_all(s_qr_popup, 0, 0);
    lv_obj_clear_flag(s_qr_popup, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_qr_popup, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_qr_popup, qr_popup_clicked_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *title = lv_label_create(s_qr_popup);
    lv_label_set_text(title, "Scan to Bind");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x00FF00), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    const device_info_t *dev = mqtt_client_get_device_info();
    char bind_data[128];
    bool bind_ok = false;

    if (dev != NULL && dev->device_id[0] != '\0' && dev->temp_token[0] != '\0') {
        if (qrcode_gen_create_bind_data(dev->device_id, dev->temp_token,
                                        bind_data, sizeof(bind_data)) == ESP_OK) {
            bind_ok = true;
        }
    }

    const int frame_y = 22;
    lv_obj_t *frame = NULL;
    if (bind_ok) {
        frame = create_qrcode(s_qr_popup, bind_data);
    }
    if (frame != NULL) {
        lv_obj_set_pos(frame, (LCD_WIDTH - QR_FRAME_SIZE) / 2, frame_y);
    } else {
        create_hint_label(s_qr_popup, frame_y + 70, "Bind info\nunavailable");
    }

    /* 显示设备 ID 便于人工核对 */
    char id_text[40];
    if (dev != NULL && dev->device_id[0] != '\0') {
        snprintf(id_text, sizeof(id_text), "ID: %s", dev->device_id);
    } else {
        snprintf(id_text, sizeof(id_text), "ID: --");
    }
    create_hint_label(s_qr_popup, frame_y + QR_FRAME_SIZE + 2, id_text);

    lv_obj_t *foot = lv_label_create(s_qr_popup);
    lv_label_set_text(foot, "Tap to close");
    lv_obj_set_style_text_font(foot, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(foot, lv_color_hex(0x888888), 0);
    lv_obj_align(foot, LV_ALIGN_BOTTOM_MID, 0, -2);

    s_close_timer = lv_timer_create(qr_popup_close_timer_cb,
                                    QR_POPUP_TIMEOUT_MS, NULL);
    if (s_close_timer != NULL) {
        lv_timer_set_repeat_count(s_close_timer, 1);
    }
}
