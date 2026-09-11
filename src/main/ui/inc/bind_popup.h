#ifndef __BIND_POPUP_H
#define __BIND_POPUP_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 显示绑定确认弹窗（Allow / Deny）
 *
 * 弹窗创建在顶层图层，用户点击按钮后提交确认结果；无操作时由外部定时器超时拒绝。
 *
 * @param replacing 设备已绑定时为 true，提示将更换绑定账号
 */
void bind_popup_show(bool replacing);

/**
 * @brief 关闭绑定确认弹窗
 */
void bind_popup_hide(void);

/**
 * @brief 绑定确认弹窗是否可见
 * @return true 可见
 */
bool bind_popup_is_visible(void);

#ifdef __cplusplus
}
#endif

#endif /* __BIND_POPUP_H */
