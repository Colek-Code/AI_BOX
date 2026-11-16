/** @file qwen_net.h @brief Qwen SDK 生命周期、注册与令牌同步的工作任务边界。 */
#ifndef __QWEN_NET_H__
#define __QWEN_NET_H__

#include "framework/d_error.h"

#include <stdint.h>

typedef int32_t (*qwen_net_event_cb_t)(uint32_t event, void *param);

/**
 * @brief 在生命周期工作任务中初始化 SDK、预装身份凭据并应用用户配置。
 * @param[in] event_cb 非空且在 SDK 使用期间持续有效的事件回调。
 * @return 成功或已准备返回 D_OK；参数、配置、SDK 或存储失败返回实际映射错误。
 * @note 调用者串行管理生命周期；准备失败会尝试受检 deinit，清理失败锁存重启要求，不假定资源已回收。
 */
d_err_t qwen_net_prepare(qwen_net_event_cb_t event_cb);
/**
 * @brief 必要时注册设备，然后获取或更新令牌。
 * @return 认证完成返回 D_OK；未准备或清理故障返回 D_ERR_NOT_READY；其余为 SDK、HTTP 或存盘错误。
 * @note 由生命周期工作任务持有准入后调用，包含同步网络与 VM 操作。
 */
d_err_t qwen_net_sync_auth(void);
/**
 * @brief 兼容入口尝试受检 SDK 清理并忽略返回值。
 * @note 需要确认清理时使用 deinit_checked。
 */
void qwen_net_deinit(void);
/**
 * @brief 受检清理 SDK 并只在成功后清除准备和令牌期限状态。
 * @return 已未准备或清理成功返回 D_OK；SDK 清理失败返回映射错误并锁存，后续返回 D_ERR_IO。
 * @note 失败时保留准备事实，不能继续开始或声称底层资源已释放。
 */
d_err_t qwen_net_deinit_checked(void);
/**
 * @brief 检查设备身份可用，并在激活模式下核对可用激活凭据。
 * @return 当前运行资格满足返回非零。
 */
int qwen_net_is_runtime_allowed(void);

/**
 * @brief 在已准备且无清理故障时读取 SDK 注册标记。
 * @return 已注册返回非零；未准备、清理故障或 License 关闭返回 0。
 */
int qwen_net_is_registered(void);
/**
 * @brief 在 SDK 可用时按当前加密 UTC 检查令牌。
 * @return 令牌有效返回非零；SDK 不可用或令牌无效返回 0。
 */
int qwen_net_is_token_valid(void);
/**
 * @brief 综合 SDK 令牌有效期、提前余量与刷新期限决定是否需更新。
 * @return 需要刷新或 SDK 不可用返回 1，仍可使用返回 0。
 */
int qwen_net_is_token_refresh_needed(void);


#endif
