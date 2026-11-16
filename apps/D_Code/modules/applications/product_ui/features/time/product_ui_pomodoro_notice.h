/** @file product_ui_pomodoro_notice.h @brief UI 线程全局非模态完成提示，导航延后到当前页面更新之外。 */
#ifndef PRODUCT_UI_POMODORO_NOTICE_H
#define PRODUCT_UI_POMODORO_NOTICE_H
#include <stdint.h>
/**
 * @brief 观察未确认完成事件，按遮挡、静默及显示期限协调全局提示。
 * @note 每个新事件至多发一次唤醒；控制中心、模态反馈、闹钟及 OTA 浮层阻挡时不覆盖其交互。只由 UI 周期串行调用。
 */
void product_ui_pomodoro_notice_process(void);
/**
 * @brief 读取最近周期观察到的未确认完成事件标志。
 * @return 非零表示仍有未确认事件，不保证提示控件当前可见。
 */
uint8_t product_ui_pomodoro_notice_pending(void);
#endif
