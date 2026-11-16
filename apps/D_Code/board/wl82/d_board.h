/**
 * @file d_board.h
 * @brief 声明 D_Code WL82 板级公开入口，不承担业务电源策略。
 * @author D_Code
 * @date 2026-06-04
 */

#ifndef D_BOARD_H
#define D_BOARD_H

/**
 * @brief 在软件关机前切换为板级长按唤醒配置，并清理 SDK 唤醒源。
 * @note 由电源流程紧邻 power_set_soft_poweroff 调用；EMI 板或启用电源控制的配置执行，其他配置为空操作。
 * @note 不在此执行关机或等待业务清理，也不承诺 SDK 之外的硬件唤醒行为。
 */
void d_board_prepare_soft_poweroff_wakeup(void);

#endif /* D_BOARD_H */
