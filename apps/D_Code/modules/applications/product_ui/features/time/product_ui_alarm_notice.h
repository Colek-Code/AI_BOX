/** @file product_ui_alarm_notice.h @brief UI 线程全局闹铃浮层及导航屏障，直到音频资源释放才撤下。 */
#ifndef PRODUCT_UI_ALARM_NOTICE_H
#define PRODUCT_UI_ALARM_NOTICE_H
#include <stdint.h>
/**
 * @brief 投影全局闹铃与错过状态，消费组动作并在真实资源归还后撤下浮层。
 * @note 仅 UI 周期调用；浮层存在时保留下层页面，每个新响铃组独立唤醒，创建失败按有限间隔重试。
 */
void product_ui_alarm_notice_process(void);
/**
 * @brief 查询全局闹铃浮层是否正在阻断普通导航。
 * @return 根对象存在返回非零，否则为零。
 */
uint8_t product_ui_alarm_notice_blocking(void);
/**
 * @brief 读取最近周期观察到的错过提醒标志。
 * @return 非零表示存在 notice 记录，不等价于正在响铃或浮层可见。
 */
uint8_t product_ui_alarm_notice_pending(void);
#endif
