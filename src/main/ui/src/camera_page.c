#include "ui/inc/camera_page.h"

#include <string.h>
#include "esp_log.h"
#include "ui/inc/ui.h"
#include "device/inc/ov2640.h"

static const char *TAG = "camera_page";

lv_obj_t *camera_page = NULL;

/* 摄像头图像描述（动态指向帧缓冲） */
static lv_img_dsc_t cam_img_dsc = {
    .header.always_zero = 0,
    .header.w = CAM_OUTPUT_WIDTH,
    .header.h = CAM_OUTPUT_HEIGHT,
    .data_size = CAM_OUTPUT_WIDTH * CAM_OUTPUT_HEIGHT * 2,
    .header.cf = LV_IMG_CF_TRUE_COLOR,
    .data = NULL,
};

static lv_obj_t *cam_img = NULL;
static lv_obj_t *status_label = NULL;
static lv_timer_t *refresh_timer = NULL;
static bool cam_ready = false;

/* 定时刷新：将最新帧绑定到 lv_img 并重绘 */
static void camera_refresh_cb(lv_timer_t *timer)
{
    (void)timer;
    if (cam_img == NULL) return;

    int w = 0, h = 0;
    const uint16_t *frame = ov2640_camera_get_frame(&w, &h);
    if (frame == NULL) {
        return;
    }

    cam_img_dsc.header.w = w;
    cam_img_dsc.header.h = h;
    cam_img_dsc.data_size = w * h * 2;
    cam_img_dsc.data = (const uint8_t *)frame;
    lv_img_set_src(cam_img, &cam_img_dsc);
    lv_obj_invalidate(cam_img);
}

/* 页面删除回调 */
static void camera_page_delete_cb(lv_event_t *e)
{
    (void)e;
    camera_page = NULL;
    cam_img = NULL;
    status_label = NULL;
    if (refresh_timer) {
        lv_timer_del(refresh_timer);
        refresh_timer = NULL;
    }
    ov2640_camera_stop();
    ESP_LOGI(TAG, "camera page deleted");
}

lv_obj_t *create_camera_page(void)
{
    /* 页面已存在则直接切换 */
    if (camera_page != NULL) {
        lv_scr_load(camera_page);
        return camera_page;
    }

    cam_ready = ov2640_camera_is_ready();

    camera_page = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(camera_page, lv_color_hex(0x000000), LV_PART_MAIN);

    /* 返回按钮（左上角） */
    lv_obj_t *back_btn = lv_btn_create(camera_page);
    lv_obj_set_size(back_btn, 60, 30);
    lv_obj_set_pos(back_btn, 0, 0);
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT " Back");
    lv_obj_center(back_label);
    lv_obj_add_event_cb(back_btn, switch_page_cb, LV_EVENT_CLICKED, "app_page");

    if (cam_ready) {
        /* 全屏摄像头画面（下方 320x240 区域） */
        cam_img = lv_img_create(camera_page);
        lv_obj_set_pos(cam_img, 0, 30);
        lv_img_set_src(cam_img, NULL);

        refresh_timer = lv_timer_create(camera_refresh_cb, 40, NULL);

        ov2640_camera_start();
        ESP_LOGI(TAG, "camera started");
    } else {
        /* 摄像头未初始化：显示提示 */
        status_label = lv_label_create(camera_page);
        lv_label_set_text(status_label, "Camera not ready.\nCheck OV2640 wiring & log.");
        lv_obj_set_style_text_font(status_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(status_label, lv_color_hex(0xFF0000), 0);
        lv_obj_center(status_label);
    }

    lv_obj_add_event_cb(camera_page, camera_page_delete_cb, LV_EVENT_DELETE, NULL);

    return camera_page;
}
