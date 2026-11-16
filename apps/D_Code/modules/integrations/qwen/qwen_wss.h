/**
 * @file    qwen_wss.h
 * @brief   带运输代次和受检双线程退出的 Qwen WSS 管理接口。
 * @author  D_Code
 * @date    2026-05-19
 */

#ifndef __QWEN_WSS_H__
#define __QWEN_WSS_H__

#include "framework/d_error.h"
#include <stdint.h>

typedef void (*qwen_wss_disconnect_callback_t)(uint32_t generation);

/**
 * @brief 保存意外断连回调。
 * @param[in] callback 可空回调，传入旧连接代次；在收发使用期间须持续有效。
 * @note 装配时设置；意外断连由运输线程通知，主动受检断开不依赖此回调收尾。
 */
void qwen_wss_set_disconnect_callback(qwen_wss_disconnect_callback_t callback);

/**
 * @brief 使用已认证的 SDK WSS 参数建立 TLS、Upgrade 与双收发线程。
 * @return 成功或已连接返回 D_OK；参数未就绪、端口、同步、握手或任务创建失败返回对应错误。
 * @note 由生命周期工作任务串行调用；包含同步握手和等待。连接成功不表示会话业务已进入 ACTIVE。
 */
d_err_t qwen_wss_connect(void);

/**
 * @brief 兼容入口请求受检断开并忽略结果。
 * @note 确认线程与资源收尾时使用 disconnect_checked。
 */
void qwen_wss_disconnect(void);
/**
 * @brief 撤销运输代次，唤醒套接字 I/O，等待双线程退出后释放 TLS 与接收缓冲。
 * @return 完整收尾返回 D_OK；操作锁或已有关闭冲突返回 D_ERR_BUSY。
 * @note 仅生命周期工作任务调用，不能从收发线程等待自身退出。失败不表示资源已释放。
 */
d_err_t qwen_wss_disconnect_checked(void);

/**
 * @brief 同时检查本地连接标记与 SDK ESTABLISHED 状态。
 * @return 两项满足返回 1，否则为 0。
 */
int qwen_wss_is_connected(void);
/**
 * @brief 读取连接开始或断开时推进的运输代次。
 * @return 当前 uint32_t 运输代次。
 */
uint32_t qwen_wss_generation(void);

#endif
