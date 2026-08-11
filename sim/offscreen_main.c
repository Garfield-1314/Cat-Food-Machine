/* ============================================================
 * Cat Food Machine - 离屏渲染预览（无头，无需 SDL2）
 *
 * 用 LVGL 软件渲染把各页面渲染到内存 buffer，并保存为 PPM 图像。
 * 用于无头环境快速验证 UI 布局 / 生成预览图 / CI 截图。
 *
 * 用法: ./cat_food_sim_offscreen [输出前缀]   (默认输出到当前目录)
 * 每个页面输出一个 <前缀>page_<name>.ppm。
 * ============================================================ */
#include <stdio.h>
#include <string.h>

#include "lvgl.h"
#include "ui/inc/ui.h"
#include "ui/inc/feeding_page.h"
#include "ui/inc/setting_page.h"
#include "ui/inc/wifi_config_page.h"

#define HOR_RES 320
#define VER_RES 240

static lv_color_t s_buf[HOR_RES * VER_RES];
static lv_disp_draw_buf_t s_draw_buf;
static lv_disp_drv_t s_disp_drv;

/* LVGL 显示刷新回调：数据已绘入 s_buf（全屏单缓冲），无需额外拷贝 */
static void flush_cb(lv_disp_drv_t *disp_drv, const lv_area_t *area,
                     lv_color_t *color_p)
{
    (void)area;
    (void)color_p;
    lv_disp_flush_ready(disp_drv);
}

/* 把 RGB565 buffer 保存为 PPM(P6) 图像 */
static void save_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "cannot open %s\n", path);
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", HOR_RES, VER_RES);
    for (int i = 0; i < HOR_RES * VER_RES; i++) {
        uint16_t full = s_buf[i].full;
        unsigned r = (full >> 11) & 0x1F;
        unsigned g = (full >> 5) & 0x3F;
        unsigned b = full & 0x1F;
        r = (r << 3) | (r >> 2);
        g = (g << 2) | (g >> 4);
        b = (b << 3) | (b >> 2);
        fputc((int)r, f);
        fputc((int)g, f);
        fputc((int)b, f);
    }
    fclose(f);
    printf("saved %s (%dx%d)\n", path, HOR_RES, VER_RES);
}

/* 跑若干帧让布局/动画稳定 */
static void run_frames(int n)
{
    for (int i = 0; i < n; i++) {
        lv_tick_inc(10);
        lv_timer_handler();
    }
}

int main(int argc, char **argv)
{
    const char *prefix = (argc > 1) ? argv[1] : "";
    char path[128];

    lv_init();

    lv_disp_draw_buf_init(&s_draw_buf, s_buf, NULL, HOR_RES * VER_RES);
    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res  = HOR_RES;
    s_disp_drv.ver_res  = VER_RES;
    s_disp_drv.flush_cb = flush_cb;
    s_disp_drv.draw_buf = &s_draw_buf;
    lv_disp_drv_register(&s_disp_drv);

    /* 1) 应用启动页 */
    create_ui();
    run_frames(60);
    snprintf(path, sizeof(path), "%spage_app.ppm", prefix);
    save_ppm(path);

    /* 2) 投喂控制页 */
    {
        lv_obj_t *p = create_feeding_page();
        if (p) lv_scr_load(p);
        run_frames(60);
        snprintf(path, sizeof(path), "%spage_feeding.ppm", prefix);
        save_ppm(path);
    }

    /* 3) 设置页 */
    {
        create_setting_page();
        if (setting_page) lv_scr_load(setting_page);
        run_frames(60);
        snprintf(path, sizeof(path), "%spage_setting.ppm", prefix);
        save_ppm(path);
    }

    /* 4) WiFi 配置页 */
    {
        lv_obj_t *p = create_wifi_config_page();
        if (p) lv_scr_load(p);
        run_frames(60);
        snprintf(path, sizeof(path), "%spage_wifi.ppm", prefix);
        save_ppm(path);
    }

    return 0;
}
