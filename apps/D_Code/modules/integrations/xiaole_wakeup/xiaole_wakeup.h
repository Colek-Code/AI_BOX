/**
 * @file    xiaole_wakeup.h
 * @brief   带会话代次与受检停止的小乐语音唤醒服务。
 * @author  D_Code
 * @date    2026-06-05
 */

#ifndef XIAOLE_WAKEUP_H
#define XIAOLE_WAKEUP_H

#include "framework/d_error.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 创建小乐唤醒工作任务，初始不占用麦克风。
 * @return 已经初始化或创建成功返回 D_OK；线程创建失败返回 D_ERR_IO。
 */
d_err_t xiaole_wakeup_init(void);
/**
 * @brief 禁止新启动，受检停止后退出并等待工作任务。
 * @note 停止或退出命令失败时保留任务与资源，不清空可能仍在用的状态。
 */
void xiaole_wakeup_deinit(void);
/**
 * @brief 受理指定会话代次的监听启动命令。
 * @param[in] generation 调用方持有的会话代次；DETECTED 事件回传同一 uint32_t 值。
 * @return 受理返回 D_OK；关机或算法停止故障返回 D_ERR_BUSY；未初始化或队列满返回相应错误。
 * @note 受理不等于麦克风已打开；重复 START 不改写正在监听的代次。消费者必须拒绝已被停止、维护或后续会话失效的检测事件。
 */
d_err_t xiaole_wakeup_start(uint32_t generation);
/**
 * @brief 非阻塞投递停止监听命令。
 * @return 命令受理返回 D_OK；未初始化或队列满返回对应错误。
 * @note 需 stop_wait 才能确认资源已释放。
 */
d_err_t xiaole_wakeup_stop(void);
/**
 * @brief 后台提交或加入唯一停止请求，并等待完成确认。
 * @param[in] timeout_ms 已初始化时要求非零的等待毫秒数。
 * @return 未初始化或停止成功返回 D_OK；零期限、投递失败、STOP 失败或超时返回对应错误。
 * @note 超时不撤销占用票据；重试加入同一命令，不在 UI 或小乐工作任务中等待。
 */
d_err_t xiaole_wakeup_stop_wait(uint32_t timeout_ms);
/**
 * @brief 读取监听是否处于 STARTING 或 LISTENING。
 * @return 这两个阶段返回 1；STOPPING 或 STOPPED 返回 0。
 * @note 0 不证明停止清理已完成，仍应以 stop_wait 确认。
 */
int xiaole_wakeup_is_running(void);

#ifdef __cplusplus
}
#endif

#endif /* XIAOLE_WAKEUP_H */
