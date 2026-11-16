/** @file power_ctrl_pomodoro.h @brief 产品闹钟、番茄钟与提示音的关机准备边界。 */
#ifndef POWER_CTRL_POMODORO_H
#define POWER_CTRL_POMODORO_H
#include "framework/d_error.h"
#include <stdint.h>
/**
 * @brief 禁止闹钟与番茄钟新活动并请求后台收尾。
 */
void power_ctrl_pomodoro_begin(void);
/**
 * @brief 在同一总期限内依次等待闹钟、提示音及番茄钟空闲。
 * @param[in] timeout_ms 总等待期限，后续阶段使用剩余时间。
 * @return 全部就绪返回 D_OK；返回首个未完成或失败阶段的结果。
 */
d_err_t power_ctrl_pomodoro_wait_idle(uint32_t timeout_ms);
/**
 * @brief 依次撤销番茄钟与闹钟的关机静默限制。
 */
void power_ctrl_pomodoro_cancel(void);
#endif
