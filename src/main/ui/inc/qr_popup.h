#ifndef __QR_POPUP_H
#define __QR_POPUP_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 弹出设备二维码窗口（绑定信息 + Web 预览地址）
 *
 * 窗口创建在顶层图层，定时自动关闭，点击遮罩也可立即关闭。
 */
void qr_popup_show(void);

#ifdef __cplusplus
}
#endif

#endif /* __QR_POPUP_H */
