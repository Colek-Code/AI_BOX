/** @file qwen_session_policy.h @brief 有界会话意图与受检清理的纯策略合同。 */
#ifndef QWEN_SESSION_POLICY_H
#define QWEN_SESSION_POLICY_H
#include <stdint.h>
#include "framework/d_error.h"

typedef enum {
    QWEN_SESSION_UNAVAILABLE = 0,
    QWEN_SESSION_OFFLINE,
    QWEN_SESSION_PREPARING,
    QWEN_SESSION_READY,
    QWEN_SESSION_STARTING,
    QWEN_SESSION_ACTIVE,
    QWEN_SESSION_STOPPING,
    QWEN_SESSION_ERROR,
} qwen_session_phase_e;

typedef struct {
    uint32_t generation;
    uint32_t revision;
    uint8_t initialized;
    uint8_t wifi_ready;
    uint8_t runtime_allowed;
    uint8_t auth_ready;
    uint8_t requested;
    uint8_t connected;
    uint8_t cleanup_pending;
    uint8_t cleanup_failed;
    uint8_t fatal;
    uint8_t automatic;
    d_err_t start_error;
    d_err_t cleanup_error;
} qwen_session_policy_t;

typedef struct {
    qwen_session_phase_e phase;
    uint32_t generation;
    uint32_t revision;
    d_err_t error;
    uint8_t initialized;
    uint8_t wifi_ready;
    uint8_t runtime_allowed;
    uint8_t auth_ready;
    uint8_t requested;
    uint8_t connected;
    uint8_t can_start;
    uint8_t can_stop;
    uint8_t fatal;
} qwen_session_snapshot_t;

/**
 * @brief 清空并初始化纯会话策略。
 * @param[out] policy 非空策略输出。
 * @param[in] automatic 非零表示网络恢复时自动申请会话。
 */
void qwen_session_policy_init(qwen_session_policy_t *policy, uint8_t automatic);
/**
 * @brief 更新网络事实，离线时请求清理，自动模式恢复时保留启动意图。
 * @param[in,out] policy 非空、已初始化的策略对象，调用者负责串行访问。
 * @param[in] ready 网络就绪事实。
 */
void qwen_session_policy_network(qwen_session_policy_t *policy, uint8_t ready);
/**
 * @brief 更新运行资格，资格撤销时清除意图并申请清理。
 * @param[in,out] policy 非空、已初始化的策略对象，调用者负责串行访问。
 * @param[in] allowed 当前是否允许运行业务。
 */
void qwen_session_policy_runtime(qwen_session_policy_t *policy, uint8_t allowed);
/**
 * @brief 在网络、运行资格和清理条件满足时记录启动意图。
 * @param[in,out] policy 非空、已初始化的策略对象，调用者负责串行访问。
 * @return 受理或原本已请求返回 D_OK；未就绪、致命故障或清理在途返回对应错误。
 * @note 只改策略，不完成认证或连接。
 */
d_err_t qwen_session_policy_start(qwen_session_policy_t *policy);
/**
 * @brief 撤销启动意图并记录后台清理需求。
 * @param[in,out] policy 非空、已初始化的策略对象，调用者负责串行访问。
 * @return 受理或本来无请求返回 D_OK；未初始化返回 D_ERR_NOT_READY；致命故障返回 D_ERR_IO。
 */
d_err_t qwen_session_policy_stop(qwen_session_policy_t *policy);
/**
 * @brief 请求后台清理并使旧认证与连接事实失效。
 * @param[in,out] policy 非空、已初始化的策略对象，调用者负责串行访问。
 * @param[in] preserve 非零保留 requested；0 撤销当前请求。
 * @note 推进代次与版本，不在此释放实际资源。
 */
void qwen_session_policy_cleanup(qwen_session_policy_t *policy, uint8_t preserve);
/**
 * @brief 记录一次后台清理结果，只有 D_OK 才清除待清理状态。
 * @param[in,out] policy 非空、已初始化的策略对象，调用者负责串行访问。
 * @param[in] result 真实清理结果。
 * @param[in] fatal 非零锁存致命故障，后续禁止开始。
 * @note 失败保留 cleanup_pending；致命标志不会被之后的普通成功清除。
 */
void qwen_session_policy_set_cleanup_result(qwen_session_policy_t *policy,
                                           d_err_t result, uint8_t fatal);
/**
 * @brief 保存启动错误并撤销会话意图、申请清理。
 * @param[in,out] policy 非空、已初始化的策略对象，调用者负责串行访问。
 * @param[in] error 启动错误。
 */
void qwen_session_policy_failed(qwen_session_policy_t *policy, d_err_t error);
/**
 * @brief 在无清理和致命故障时更新认证就绪状态。
 * @param[in,out] policy 非空、已初始化的策略对象，调用者负责串行访问。
 * @param[in] ready 认证就绪事实。
 */
void qwen_session_policy_auth(qwen_session_policy_t *policy, uint8_t ready);
/**
 * @brief 仅在当前策略允许时发布已连接事实。
 * @param[in,out] policy 非空、已初始化的策略对象，调用者负责串行访问。
 * @param[in] connected 底层当前连接事实。
 */
void qwen_session_policy_connected(qwen_session_policy_t *policy, uint8_t connected);
/**
 * @brief 检查初始化、网络、运行资格、请求和清理条件是否允许业务。
 * @param[in] policy 非空只读策略，调用者序列化访问。
 * @return 全部满足返回非零。
 */
uint8_t qwen_session_policy_allows(const qwen_session_policy_t *policy);
/**
 * @brief 由纯策略生成阶段、错误与可操作性快照。
 * @param[in] policy 非空只读策略。
 * @param[out] snapshot 非空快照输出。
 * @note 错误优先展示 cleanup_error；阶段按实现中的清理、资格、网络和连接优先级推导。
 */
void qwen_session_policy_snapshot(const qwen_session_policy_t *policy,
                                 qwen_session_snapshot_t *snapshot);
#endif
