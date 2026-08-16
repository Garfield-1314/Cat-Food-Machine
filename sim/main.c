/* ============================================================
 * Cat Food Machine - 离线 UI 模拟器 (SDL2)
 *
 * 在 PC 上以 SDL2 窗口(320x240)渲染 LVGL UI，鼠标模拟触摸。
 * 复用固件 ui/src/*.c 页面源码，硬件调用由 sim/include + sim/src 桩提供。
 *
 * 构建与运行见 sim/README.md。
 * ============================================================ */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "SDL2/SDL.h"
#include "lvgl.h"
#include "ui/inc/ui.h"

#define HOR_RES 320
#define VER_RES 240
#define SCALE   2   /* 窗口放大倍数（2 => 640x480 窗口） */

static SDL_Window   *s_win;
static SDL_Renderer *s_ren;
static SDL_Texture  *s_tex;

static lv_disp_draw_buf_t s_draw_buf;
static lv_disp_drv_t      s_disp_drv;
static lv_indev_drv_t     s_indev_drv;

/* 全屏 RGB565 单缓冲 */
static lv_color_t s_buf[HOR_RES * VER_RES];

/* ARGB8888 中转缓冲：把 LVGL 的 RGB565 转为通用纹理格式，
 * 避免部分渲染器（如 WSLg 软件渲染）对 RGB565 纹理支持不佳导致花屏 */
static uint32_t s_argb[HOR_RES * VER_RES];

/* 鼠标（模拟触摸）状态 */
static int  s_mouse_x = 0;
static int  s_mouse_y = 0;
static bool s_mouse_pressed = false;

/* LVGL 显示刷新回调：把整屏 RGB565 转 ARGB8888 更新到 SDL 纹理并呈现。
 * 使用 full_refresh（强制整屏刷新），故 color_p 始终是整屏数据，
 * 直接全屏转换，不依赖任何区域映射。 */
static void disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area,
                       lv_color_t *color_p)
{
    (void)area;
    SDL_ConvertPixels(HOR_RES, VER_RES,
                      SDL_PIXELFORMAT_RGB565, color_p, HOR_RES * sizeof(lv_color_t),
                      SDL_PIXELFORMAT_ARGB8888, s_argb, HOR_RES * 4);

    SDL_UpdateTexture(s_tex, NULL, s_argb, HOR_RES * 4);
    /* 全屏纹理覆盖整个窗口，无需先 Clear（避免闪烁/撕裂） */
    SDL_RenderCopy(s_ren, s_tex, NULL, NULL);
    SDL_RenderPresent(s_ren);
    lv_disp_flush_ready(disp_drv);
}

/* LVGL 输入回调：返回鼠标位置/按下状态 */
static void touch_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
{
    (void)indev_drv;
    data->point.x = s_mouse_x;
    data->point.y = s_mouse_y;
    data->state = s_mouse_pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
}

int main(void)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    s_win = SDL_CreateWindow("Cat Food Machine - UI Simulator",
                             SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             HOR_RES * SCALE, VER_RES * SCALE, 0);
    if (!s_win) {
        fprintf(stderr, "SDL_CreateWindow failed\n");
        return 1;
    }

    s_ren = SDL_CreateRenderer(s_win, -1,
                               SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!s_ren) {
        s_ren = SDL_CreateRenderer(s_win, -1, SDL_RENDERER_ACCELERATED);
    }
    if (!s_ren) {
        s_ren = SDL_CreateRenderer(s_win, -1, 0);
    }
    if (!s_ren) {
        fprintf(stderr, "SDL_CreateRenderer failed\n");
        return 1;
    }

    s_tex = SDL_CreateTexture(s_ren, SDL_PIXELFORMAT_ARGB8888,
                              SDL_TEXTUREACCESS_STREAMING, HOR_RES, VER_RES);
    if (!s_tex) {
        fprintf(stderr, "SDL_CreateTexture failed\n");
        return 1;
    }

    /* ---------- LVGL 初始化 ---------- */
    lv_init();

    lv_disp_draw_buf_init(&s_draw_buf, s_buf, NULL, HOR_RES * VER_RES);

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res  = HOR_RES;
    s_disp_drv.ver_res  = VER_RES;
    s_disp_drv.flush_cb = disp_flush;
    s_disp_drv.draw_buf = &s_draw_buf;
    s_disp_drv.full_refresh = 1;  /* 强制整屏刷新，避免局部刷新错位导致花屏 */
    lv_disp_drv_register(&s_disp_drv);

    lv_indev_drv_init(&s_indev_drv);
    s_indev_drv.type    = LV_INDEV_TYPE_POINTER;
    s_indev_drv.read_cb = touch_read;
    lv_indev_drv_register(&s_indev_drv);

    /* 启动 UI（与固件 user_component_init() 中的 create_ui() 一致） */
    create_ui();

    /* ---------- 主循环 ---------- */
    bool running = true;
    SDL_Event ev;
    while (running) {
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_QUIT:
                running = false;
                break;
            case SDL_MOUSEMOTION:
                s_mouse_x = ev.motion.x / SCALE;
                s_mouse_y = ev.motion.y / SCALE;
                break;
            case SDL_MOUSEBUTTONDOWN:
                s_mouse_pressed = true;
                s_mouse_x = ev.button.x / SCALE;
                s_mouse_y = ev.button.y / SCALE;
                break;
            case SDL_MOUSEBUTTONUP:
                s_mouse_pressed = false;
                s_mouse_x = ev.button.x / SCALE;
                s_mouse_y = ev.button.y / SCALE;
                break;
            default:
                break;
            }
        }

        lv_tick_inc(5);
        lv_timer_handler();
        SDL_Delay(5);
    }

    SDL_DestroyTexture(s_tex);
    SDL_DestroyRenderer(s_ren);
    SDL_DestroyWindow(s_win);
    SDL_Quit();
    return 0;
}
