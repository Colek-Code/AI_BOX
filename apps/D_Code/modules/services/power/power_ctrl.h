/**
 * @file power_ctrl.h
 * @brief 长按抬起确认关机、准备所有者与维护占用的公开合同。
 * @author D_Code
 * @date 2026-06-04
 */

#ifndef POWER_CTRL_H
#define POWER_CTRL_H

#include "framework/d_error.h"

#include <stdint.h>

typedef enum {
    POWER_CTRL_MODE_SOFT = 0,
    POWER_CTRL_MODE_HARD = 1,
} power_ctrl_mode_e;

typedef d_err_t (*power_ctrl_prepare_shutdown_cb_t)(void *user_data);
typedef d_err_t (*power_ctrl_shutdown_backend_cb_t)(void *user_data);

typedef struct {
    d_err_t (*begin)(void);
    d_err_t (*wait)(uint32_t timeout_ms);
    /** 非阻塞取消请求；D_OK 仅表示受理，提供者保留准入保护，
     * 并自行完成延迟释放。 */
    d_err_t (*cancel)(void);
} power_ctrl_prepare_ops_t;
/**
 * @brief 在服务启动前绑定唯一产品关机准备所有者。
 * @param[in] ops 静态生命周期的 begin、wait、cancel 操作集。
 * @return 完整描述首次或相同指针重复绑定返回 D_OK；参数无效返回 D_ERR_INVALID_ARG；不同所有者或旧回调已注册返回 D_ERR_BUSY。
 * @note 应在启动阶段串行绑定，不创建任务或开始关机。
 */
d_err_t power_ctrl_bind_prepare(const power_ctrl_prepare_ops_t *ops);

/**
 * @brief 初始化电源状态，产品按键绑定由独立装配负责。
 * @return 初始化成功返回 D_OK；在途按键或关机请求阻止安全重置时返回 D_ERR_BUSY。
 */
d_err_t power_ctrl_init(void);

/**
 * @brief 仅在没有在途按键请求或关机时清空电源状态。
 * @note 忙时保留状态并需稍后重试；旧物理长按序列持续拒绝直到终止事件。产品准备绑定具有启动生命周期。
 */
void power_ctrl_deinit(void);

/**
 * @brief 处理产品映射后的 SDK 长按、保持及抬起动作。
 * @param[in] key_action SDK 按键动作。
 * @return 返回 D_KEY_UNHANDLED、D_KEY_CONSUMED 或 D_KEY_REJECTED；仅表示输入处理结果。
 * @note 达到长按期限后在 UP 同步请求关机；调用者不可持输入、UI 或注册表锁。已取消物理序列保持拒绝直到终止事件。
 */
int power_ctrl_handle_key_action(uint8_t key_action);

/**
 * @brief 读取关机准备或执行是否在途。
 * @return 在途返回非零，关机撤销后返回 0。
 * @note 返回 0 不证明各准备提供者的延迟取消已完成，其准入保护仍可能保留。
 */
uint8_t power_ctrl_is_shutting_down(void);


/**
 * @brief 注册兼容的关机前回调与上下文对。
 * @param[in] callback 可空回调；返回 D_OK 或 D_ERR_NOT_READY 时允许继续关机。
 * @param[in] user_data 透传上下文；仍在执行的已捕获回调返回前保持有效。
 * @note 已绑定产品操作集时忽略本次设置；临界区内成对发布，回调在锁外执行。替换或清除不等待旧回调。
 */
void power_ctrl_set_prepare_shutdown_callback(power_ctrl_prepare_shutdown_cb_t callback, void *user_data);

/**
 * @brief 注册硬关机后端及其上下文。
 * @param[in] callback 硬关机实现；允许为空以撤销后端。
 * @param[in] user_data 透传上下文，已捕获调用返回前须有效。
 * @note 准备结束后才选择回调与上下文对；准备期间清除后端会以 D_ERR_NOT_READY 撤销关机。替换不等待旧调用。
 */
void power_ctrl_set_hard_shutdown_callback(power_ctrl_shutdown_backend_cb_t callback, void *user_data);

/**
 * @brief 按模式同步执行准入、服务收尾和关机后端。
 * @param[in] mode POWER_CTRL_MODE_SOFT 或 POWER_CTRL_MODE_HARD。
 * @return 成功提交软关机或硬后端成功返回 D_OK；否则返回准入、准备或取消的实际错误。
 * @note 可能等待多个后台服务；失败后逐项请求撤销，提供者负责延迟释放，错误返回不代表其资源已释放。
 */
d_err_t power_ctrl_request_shutdown(power_ctrl_mode_e mode);

/**
 * @brief 使用编译配置的默认模式执行关机。
 * @return power_ctrl_request_shutdown 的实际结果。
 */
d_err_t power_ctrl_request_default_shutdown(void);

#endif
