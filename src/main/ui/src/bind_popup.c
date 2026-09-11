#include "ui/inc/bind_popup.h"

#include "esp_log.h"
#include "lvgl.h"
#include "device/inc/cloud_api.h"

static const char *TAG = "bind_popup";

static lv_obj_t *s_bind_popup = NULL;

static void bind_allow_cb(lv_event_t *e)
{
    (void)e;
    cloud_api_resolve_bind(true, NULL);
    bind_popup_hide();
}

static void bind_deny_cb(lv_event_t *e)
{
    (void)e;
    cloud_api_resolve_bind(false, "user_denied");
    bind_popup_hide();
}

void bind_popup_hide(void)
{
    if (s_bind_popup != NULL) {
        if (lv_obj_is_valid(s_bind_popup)) {
            lv_obj_del(s_bind_popup);
        }
        s_bind_popup = NULL;
    }
}

bool bind_popup_is_visible(void)
{
    return s_bind_popup != NULL;
}

void bind_popup_show(bool replacing)
{
    if (s_bind_popup != NULL) {
        return;
    }

    s_bind_popup = lv_obj_create(lv_layer_top());
    if (s_bind_popup == NULL) {
        return;
    }

    lv_obj_set_size(s_bind_popup, lv_pct(100), lv_pct(100));
    lv_obj_center(s_bind_popup);
    lv_obj_set_style_bg_color(s_bind_popup, lv_color_hex(0x001a27), 0);
    lv_obj_set_style_bg_opa(s_bind_popup, 235, 0);
    lv_obj_set_style_border_width(s_bind_popup, 2, 0);
    lv_obj_set_style_border_color(s_bind_popup, lv_color_hex(0x00AA00), 0);
    lv_obj_set_style_radius(s_bind_popup, 10, 0);
    lv_obj_set_style_pad_all(s_bind_popup, 0, 0);
    lv_obj_clear_flag(s_bind_popup, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_bind_popup, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *title = lv_label_create(s_bind_popup);
    lv_label_set_text(title, "Bind Request");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x00FF00), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    lv_obj_t *msg = lv_label_create(s_bind_popup);
    lv_label_set_text(msg, replacing
        ? "Device already bound.\nReplace with this account?"
        : "Allow this WeChat account\nto bind the device?");
    lv_obj_set_style_text_font(msg, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(msg, lv_color_white(), 0);
    lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(msg, LV_ALIGN_CENTER, 0, -30);

    lv_obj_t *deny_btn = lv_btn_create(s_bind_popup);
    lv_obj_set_size(deny_btn, 80, 34);
    lv_obj_set_pos(deny_btn, 45, 150);
    lv_obj_set_style_bg_color(deny_btn, lv_color_hex(0xAA0000), 0);
    lv_obj_t *deny_lbl = lv_label_create(deny_btn);
    lv_label_set_text(deny_lbl, "Deny");
    lv_obj_center(deny_lbl);
    lv_obj_add_event_cb(deny_btn, bind_deny_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *allow_btn = lv_btn_create(s_bind_popup);
    lv_obj_set_size(allow_btn, 80, 34);
    lv_obj_set_pos(allow_btn, 195, 150);
    lv_obj_set_style_bg_color(allow_btn, lv_color_hex(0x00AA00), 0);
    lv_obj_t *allow_lbl = lv_label_create(allow_btn);
    lv_label_set_text(allow_lbl, "Allow");
    lv_obj_center(allow_lbl);
    lv_obj_add_event_cb(allow_btn, bind_allow_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *foot = lv_label_create(s_bind_popup);
    lv_label_set_text(foot, "Auto-deny if no action");
    lv_obj_set_style_text_font(foot, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(foot, lv_color_hex(0x888888), 0);
    lv_obj_align(foot, LV_ALIGN_BOTTOM_MID, 0, -4);

    ESP_LOGI(TAG, "Bind confirmation popup shown (replacing=%d)", replacing ? 1 : 0);
}
