#include "ui/inc/feeding_page.h"

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "ui/inc/ui.h"
#include "driver/inc/feeding_schedule.h"
#include "driver/inc/feeder_motor.h"

static const char *TAG = "feeding_page";

lv_obj_t *feeding_page = NULL;

/* ========== UI 组件（静态引用） ========== */
static lv_obj_t *list_cont = NULL;
static lv_obj_t *add_btn = NULL;
static lv_obj_t *save_btn = NULL;

/* ========== 编辑窗口组件 ========== */
static lv_obj_t *edit_dlg = NULL;
static lv_obj_t *hour_roller = NULL;
static lv_obj_t *min_roller = NULL;
static lv_obj_t *amount_slider = NULL;
static lv_obj_t *amount_label = NULL;
static lv_obj_t *enable_switch = NULL;
static int edit_index = -1;  /* -1 表示新增 */

/* ========== 前向声明 ========== */
static void rebuild_list(void);
static void open_edit_dialog(int index);
static void close_edit_dialog(void);

/* ========== 回调函数 ========== */

/* 列表行点击回调 */
static void row_click_cb(lv_event_t *e)
{
    lv_obj_t *row = lv_event_get_target(e);
    int idx = (int)(intptr_t)lv_obj_get_user_data(row);
    open_edit_dialog(idx);
}

/* 保存按钮文字恢复定时器回调 */
static void restore_save_btn_text_cb(lv_timer_t *t)
{
    lv_obj_t *btn = (lv_obj_t *)t->user_data;
    if (btn) {
        lv_obj_t *label = lv_obj_get_child(btn, 0);
        if (label) {
            lv_label_set_text(label, "Save");
        }
    }
    lv_timer_del(t);
}

/* ========== 页面删除回调 ========== */
static void feeding_page_delete_cb(lv_event_t *e)
{
    (void)e;
    feeding_page = NULL;
    list_cont = NULL;
    add_btn = NULL;
    save_btn = NULL;
    edit_dlg = NULL;
}

/* ========== 列表项创建 ========== */

static lv_obj_t *create_list_row(int index)
{
    const feed_schedule_item_t *item = feed_schedule_get_item(index);
    if (item == NULL) return NULL;

    /* 行容器 */
    lv_obj_t *row = lv_obj_create(list_cont);
    lv_obj_set_size(row, lv_pct(100), 50);
    lv_obj_set_style_bg_opa(row, 80, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x002a47), 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(0x005a77), 0);
    lv_obj_set_style_pad_all(row, 5, 0);
    lv_obj_set_style_pad_left(row, 10, 0);
    lv_obj_set_style_radius(row, 4, 0);

    /* 时间文本: "HH:MM" */
    char time_str[16];
    snprintf(time_str, sizeof(time_str), "%02d:%02d", item->hour, item->minute);
    lv_obj_t *time_lbl = lv_label_create(row);
    lv_label_set_text(time_lbl, time_str);
    lv_obj_set_style_text_font(time_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(time_lbl, lv_color_white(), 0);
    lv_obj_align(time_lbl, LV_ALIGN_LEFT_MID, 5, 0);

    /* 投喂量文本: "xN slots" */
    char amt_str[16];
    snprintf(amt_str, sizeof(amt_str), "x%d", item->amount);
    lv_obj_t *amt_lbl = lv_label_create(row);
    lv_label_set_text(amt_lbl, amt_str);
    lv_obj_set_style_text_font(amt_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(amt_lbl, lv_color_hex(0x00FF00), 0);
    lv_obj_align(amt_lbl, LV_ALIGN_CENTER, 20, 0);

    /* 启用/禁用指示 */
    lv_obj_t *status_lbl = lv_label_create(row);
    lv_label_set_text(status_lbl, item->enabled ? "ON" : "OFF");
    lv_obj_set_style_text_color(status_lbl,
        item->enabled ? lv_color_hex(0x00FF00) : lv_color_hex(0xFF0000), 0);
    lv_obj_align(status_lbl, LV_ALIGN_RIGHT_MID, -10, 0);

    /* 点击整行进入编辑 */
    lv_obj_set_user_data(row, (void *)(intptr_t)index);
    lv_obj_add_event_cb(row, row_click_cb, LV_EVENT_CLICKED, NULL);

    return row;
}

/* ========== 重建列表 ========== */
static void rebuild_list(void)
{
    if (list_cont == NULL) return;

    /* 清除现有子对象 */
    lv_obj_clean(list_cont);

    int count = feed_schedule_get_count();

    if (count == 0) {
        /* 显示空提示 */
        lv_obj_set_flex_flow(list_cont, LV_FLEX_FLOW_COLUMN);
        lv_obj_t *no_data_label = lv_label_create(list_cont);
        lv_label_set_text(no_data_label, "No feeding schedules.\nTap + to add one.");
        lv_obj_set_style_text_color(no_data_label, lv_color_hex(0x888888), 0);
        lv_obj_set_style_text_align(no_data_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(no_data_label, lv_pct(100));
        return;
    }

    lv_obj_set_flex_flow(list_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list_cont, 5, 0);

    for (int i = 0; i < count; i++) {
        create_list_row(i);
    }
}

/* ========== 编辑对话框 ========== */

static void edit_dlg_save_cb(lv_event_t *e)
{
    (void)e;
    char buf[8];

    /* 小时 */
    lv_roller_get_selected_str(hour_roller, buf, sizeof(buf));
    uint8_t hour = (uint8_t)atoi(buf);

    /* 分钟 */
    lv_roller_get_selected_str(min_roller, buf, sizeof(buf));
    uint8_t minute = (uint8_t)atoi(buf);

    /* 仓位数量 */
    uint8_t amount = (uint8_t)lv_slider_get_value(amount_slider);

    /* 启用状态 */
    bool enabled = lv_obj_has_state(enable_switch, LV_STATE_CHECKED);

    feed_schedule_item_t item = {
        .hour = hour,
        .minute = minute,
        .amount = amount,
        .enabled = enabled,
    };

    esp_err_t err;
    if (edit_index < 0) {
        err = feed_schedule_add_item(&item);
    } else {
        err = feed_schedule_set_item(edit_index, &item);
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save item: %s", esp_err_to_name(err));
    }

    close_edit_dialog();
    rebuild_list();
}

static void edit_dlg_delete_cb(lv_event_t *e)
{
    (void)e;
    if (edit_index >= 0) {
        feed_schedule_remove_item(edit_index);
    }
    close_edit_dialog();
    rebuild_list();
}

static void edit_dlg_cancel_cb(lv_event_t *e)
{
    (void)e;
    close_edit_dialog();
}

static void amount_slider_cb(lv_event_t *e)
{
    (void)e;
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", (int)lv_slider_get_value(amount_slider));
    lv_label_set_text(amount_label, buf);
}

static void open_edit_dialog(int index)
{
    if (edit_dlg != NULL) {
        lv_obj_del(edit_dlg);
        edit_dlg = NULL;
    }
    edit_index = index;

    const feed_schedule_item_t *item = (index >= 0) ? feed_schedule_get_item(index) : NULL;

    /* 创建全屏覆盖的对话框 */
    edit_dlg = lv_obj_create(feeding_page);
    lv_obj_set_size(edit_dlg, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(edit_dlg, 0, 0);
    lv_obj_set_style_bg_color(edit_dlg, lv_color_hex(0x001a27), 0);
    lv_obj_set_style_bg_opa(edit_dlg, 230, 0);
    lv_obj_set_style_border_width(edit_dlg, 0, 0);
    lv_obj_set_style_pad_all(edit_dlg, 10, 0);

    /* 取消按钮（左上角） */
    lv_obj_t *cancel_btn_el = lv_btn_create(edit_dlg);
    lv_obj_set_size(cancel_btn_el, 70, 30);
    lv_obj_set_pos(cancel_btn_el, 0, 0);
    lv_obj_t *cancel_lbl = lv_label_create(cancel_btn_el);
    lv_label_set_text(cancel_lbl, LV_SYMBOL_LEFT " Back");
    lv_obj_center(cancel_lbl);
    lv_obj_add_event_cb(cancel_btn_el, edit_dlg_cancel_cb, LV_EVENT_CLICKED, NULL);

    /* 保存按钮（右上角） */
    lv_obj_t *save_btn_el = lv_btn_create(edit_dlg);
    lv_obj_set_size(save_btn_el, 70, 30);
    lv_obj_set_pos(save_btn_el, 250, 0);
    lv_obj_t *save_lbl = lv_label_create(save_btn_el);
    lv_label_set_text(save_lbl, "Save");
    lv_obj_center(save_lbl);
    lv_obj_add_event_cb(save_btn_el, edit_dlg_save_cb, LV_EVENT_CLICKED, NULL);

    /* 标题 */
    lv_obj_t *title = lv_label_create(edit_dlg);
    lv_label_set_text(title, (index < 0) ? "Add Schedule" : "Edit Schedule");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);

    /* --- 小时选择 --- */
    lv_obj_t *h_label = lv_label_create(edit_dlg);
    lv_label_set_text(h_label, "Hour");
    lv_obj_set_style_text_color(h_label, lv_color_white(), 0);
    lv_obj_align(h_label, LV_ALIGN_TOP_LEFT, 10, 40);

    hour_roller = lv_roller_create(edit_dlg);
    lv_roller_set_options(hour_roller,
        "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23",
        LV_ROLLER_MODE_NORMAL);
    lv_obj_set_size(hour_roller, 60, 80);
    lv_obj_align(hour_roller, LV_ALIGN_TOP_LEFT, 10, 65);
    lv_obj_set_style_text_color(hour_roller, lv_color_white(), 0);
    if (item) {
        lv_roller_set_selected(hour_roller, item->hour, LV_ANIM_OFF);
    }

    /* --- 分钟选择 --- */
    lv_obj_t *m_label = lv_label_create(edit_dlg);
    lv_label_set_text(m_label, "Minute");
    lv_obj_set_style_text_color(m_label, lv_color_white(), 0);
    lv_obj_align(m_label, LV_ALIGN_TOP_LEFT, 90, 40);

    min_roller = lv_roller_create(edit_dlg);
    lv_roller_set_options(min_roller,
        "00\n05\n10\n15\n20\n25\n30\n35\n40\n45\n50\n55",
        LV_ROLLER_MODE_NORMAL);
    lv_obj_set_size(min_roller, 60, 80);
    lv_obj_align(min_roller, LV_ALIGN_TOP_LEFT, 90, 65);
    lv_obj_set_style_text_color(min_roller, lv_color_white(), 0);
    if (item) {
        int min_idx = item->minute / 5;
        if (min_idx > 11) min_idx = 11;
        lv_roller_set_selected(min_roller, min_idx, LV_ANIM_OFF);
    }

    /* --- 投喂量 --- */
    lv_obj_t *a_label = lv_label_create(edit_dlg);
    lv_label_set_text(a_label, "Amount (slots)");
    lv_obj_set_style_text_color(a_label, lv_color_white(), 0);
    lv_obj_align(a_label, LV_ALIGN_TOP_LEFT, 170, 40);

    amount_slider = lv_slider_create(edit_dlg);
    lv_obj_set_size(amount_slider, 120, 20);
    lv_slider_set_range(amount_slider, 1, 10);
    lv_slider_set_value(amount_slider, item ? item->amount : 1, LV_ANIM_OFF);
    lv_obj_align(amount_slider, LV_ALIGN_TOP_LEFT, 170, 65);
    lv_obj_set_style_bg_color(amount_slider, lv_color_hex(0x003a57), LV_PART_MAIN);
    lv_obj_set_style_bg_color(amount_slider, lv_color_hex(0x005a77), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(amount_slider, lv_color_hex(0x00AA00), LV_PART_KNOB);
    lv_obj_add_event_cb(amount_slider, amount_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

    amount_label = lv_label_create(edit_dlg);
    char amt_str[8];
    snprintf(amt_str, sizeof(amt_str), "%d", item ? item->amount : 1);
    lv_label_set_text(amount_label, amt_str);
    lv_obj_set_style_text_color(amount_label, lv_color_hex(0x00FF00), 0);
    lv_obj_align_to(amount_label, amount_slider, LV_ALIGN_OUT_RIGHT_MID, 10, 0);

    /* --- 启用开关（右下角） --- */
    lv_obj_t *e_label = lv_label_create(edit_dlg);
    lv_label_set_text(e_label, "Enabled");
    lv_obj_set_style_text_color(e_label, lv_color_white(), 0);
    lv_obj_align(e_label, LV_ALIGN_BOTTOM_RIGHT, -90, -15);

    enable_switch = lv_switch_create(edit_dlg);
    lv_obj_align(enable_switch, LV_ALIGN_BOTTOM_RIGHT, -20, -20);
    if (item) {
        if (item->enabled) {
            lv_obj_add_state(enable_switch, LV_STATE_CHECKED);
        }
    } else {
        lv_obj_add_state(enable_switch, LV_STATE_CHECKED);  /* 默认启用 */
    }

    /* 删除（左下角，仅编辑模式显示） */
    if (index >= 0) {
        lv_obj_t *del_btn_el = lv_btn_create(edit_dlg);
        lv_obj_set_size(del_btn_el, 70, 30);
        lv_obj_set_pos(del_btn_el, 0, 210);
        lv_obj_set_style_bg_color(del_btn_el, lv_color_hex(0xAA0000), 0);
        lv_obj_t *del_lbl = lv_label_create(del_btn_el);
        lv_label_set_text(del_lbl, "Delete");
        lv_obj_center(del_lbl);
        lv_obj_add_event_cb(del_btn_el, edit_dlg_delete_cb, LV_EVENT_CLICKED, NULL);
    }

    /* 先触发一次更新显示 */
    amount_slider_cb(NULL);
}

static void close_edit_dialog(void)
{
    if (edit_dlg) {
        lv_obj_del(edit_dlg);
        edit_dlg = NULL;
    }
    edit_index = -1;
    hour_roller = NULL;
    min_roller = NULL;
    amount_slider = NULL;
    amount_label = NULL;
    enable_switch = NULL;
}

/* ========== 添加按钮回调 ========== */
static void add_btn_cb(lv_event_t *e)
{
    (void)e;
    open_edit_dialog(-1);
}

/* ========== 保存全部按钮回调 ========== */
static void save_btn_cb(lv_event_t *e)
{
    (void)e;
    esp_err_t err = feed_schedule_save();
    lv_obj_t *label = lv_obj_get_child(save_btn, 0);
    if (label) {
        if (err == ESP_OK) {
            lv_label_set_text(label, "Saved!");
        } else {
            lv_label_set_text(label, "Error!");
        }
        /* 创建定时器恢复按钮文字 */
        lv_timer_create(restore_save_btn_text_cb, 1500, save_btn);
    }
}

/* ========== 创建页面 ========== */
lv_obj_t *create_feeding_page(void)
{
    /* 如果页面已存在，直接切换 */
    if (feeding_page != NULL) {
        lv_scr_load(feeding_page);
        return feeding_page;
    }

    feeding_page = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(feeding_page, lv_color_hex(0x003a57), LV_PART_MAIN);

    /* 标题 */
    lv_obj_t *title = lv_label_create(feeding_page);
    lv_label_set_text(title, "Feeding Schedule");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    /* 返回按钮 */
    lv_obj_t *back_btn = lv_btn_create(feeding_page);
    lv_obj_set_size(back_btn, 60, 30);
    lv_obj_set_pos(back_btn, 0, 0);
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT " Back");
    lv_obj_center(back_label);
    lv_obj_add_event_cb(back_btn, switch_page_cb, LV_EVENT_CLICKED, "app_page");

    /* 保存按钮（右上角） */
    save_btn = lv_btn_create(feeding_page);
    lv_obj_set_size(save_btn, 60, 30);
    lv_obj_set_pos(save_btn, 260, 0);
    lv_obj_t *save_label = lv_label_create(save_btn);
    lv_label_set_text(save_label, "Save");
    lv_obj_center(save_label);
    lv_obj_add_event_cb(save_btn, save_btn_cb, LV_EVENT_CLICKED, NULL);

    /* 列表容器 */
    list_cont = lv_obj_create(feeding_page);
    lv_obj_set_size(list_cont, 300, 160);
    lv_obj_align(list_cont, LV_ALIGN_TOP_MID, 0, 45);
    lv_obj_set_flex_flow(list_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_opa(list_cont, 0, 0);
    lv_obj_set_style_border_width(list_cont, 0, 0);
    lv_obj_set_style_pad_all(list_cont, 5, 0);
    lv_obj_set_style_pad_row(list_cont, 5, 0);

    /* 添加按钮（底部居中） */
    add_btn = lv_btn_create(feeding_page);
    lv_obj_set_size(add_btn, 200, 40);
    lv_obj_align(add_btn, LV_ALIGN_BOTTOM_MID, 0, -25);
    lv_obj_t *add_label = lv_label_create(add_btn);
    lv_label_set_text(add_label, "+ Add Schedule");
    lv_obj_center(add_label);
    lv_obj_add_event_cb(add_btn, add_btn_cb, LV_EVENT_CLICKED, NULL);

    /* 构建列表 */
    rebuild_list();

    lv_obj_add_event_cb(feeding_page, feeding_page_delete_cb, LV_EVENT_DELETE, NULL);

    return feeding_page;
}