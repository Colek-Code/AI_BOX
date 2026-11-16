/** @file qwen_session_policy.c @brief 由服务串行访问的纯会话策略，不操作 SDK 资源。 */
#include "qwen_session_policy.h"
#include <string.h>

/**
 * @brief 清空并初始化纯会话策略；完整合同见 qwen_session_policy.h。
 */
void qwen_session_policy_init(qwen_session_policy_t *policy, uint8_t automatic)
{
    memset(policy, 0, sizeof(*policy));
    policy->initialized = 1U;
    policy->automatic = automatic;
}

/**
 * @brief 请求后台清理并使旧认证与连接事实失效；完整合同见 qwen_session_policy.h。
 */
void qwen_session_policy_cleanup(qwen_session_policy_t *policy, uint8_t preserve)
{
    if (!preserve && policy->requested) {
        policy->requested = 0U;
        policy->generation++;
    }
    if (!policy->cleanup_pending) {
        policy->cleanup_pending = 1U;
        policy->generation++;
    }
    policy->auth_ready = 0U;
    policy->connected = 0U;
    policy->revision++;
}

/**
 * @brief 更新网络事实，离线时请求清理，自动模式恢复时保留启动意图；完整合同见 qwen_session_policy.h。
 */
void qwen_session_policy_network(qwen_session_policy_t *policy, uint8_t ready)
{
    if (policy->wifi_ready == ready) return;
    policy->wifi_ready = ready;
    policy->revision++;
    if (!ready) {
        qwen_session_policy_cleanup(policy, 0U);
    } else if (policy->automatic && !policy->fatal) {
        policy->requested = 1U;
        policy->generation++;
    }
}

/**
 * @brief 更新运行资格，资格撤销时清除意图并申请清理；完整合同见 qwen_session_policy.h。
 */
void qwen_session_policy_runtime(qwen_session_policy_t *policy, uint8_t allowed)
{
    if (policy->runtime_allowed == allowed) return;
    policy->runtime_allowed = allowed;
    policy->revision++;
    if (!allowed) qwen_session_policy_cleanup(policy, 0U);
}

/**
 * @brief 在网络、运行资格和清理条件满足时记录启动意图；完整合同见 qwen_session_policy.h。
 */
d_err_t qwen_session_policy_start(qwen_session_policy_t *policy)
{
    if (!policy->initialized || !policy->wifi_ready || !policy->runtime_allowed)
        return D_ERR_NOT_READY;
    if (policy->fatal) return D_ERR_IO;
    if (policy->cleanup_pending) return D_ERR_BUSY;
    if (policy->requested) return D_OK;
    policy->requested = 1U;
    policy->start_error = D_OK;
    policy->generation++;
    policy->revision++;
    return D_OK;
}

/**
 * @brief 撤销启动意图并记录后台清理需求；完整合同见 qwen_session_policy.h。
 */
d_err_t qwen_session_policy_stop(qwen_session_policy_t *policy)
{
    if (!policy->initialized) return D_ERR_NOT_READY;
    if (policy->fatal) return D_ERR_IO;
    if (!policy->requested) return D_OK;
    policy->start_error = D_OK;
    qwen_session_policy_cleanup(policy, 0U);
    return D_OK;
}

/**
 * @brief 记录一次后台清理结果，只有 D_OK 才清除待清理状态；完整合同见 qwen_session_policy.h。
 */
void qwen_session_policy_set_cleanup_result(qwen_session_policy_t *policy,
                                           d_err_t result, uint8_t fatal)
{
    policy->cleanup_error = result;
    policy->cleanup_failed = result != D_OK;
    policy->cleanup_pending = result != D_OK;
    policy->fatal |= fatal;
    policy->connected = 0U;
    policy->auth_ready = 0U;
    policy->revision++;
}

/**
 * @brief 保存启动错误并撤销会话意图、申请清理；完整合同见 qwen_session_policy.h。
 */
void qwen_session_policy_failed(qwen_session_policy_t *policy, d_err_t error)
{
    policy->start_error = error;
    qwen_session_policy_cleanup(policy, 0U);
}

/**
 * @brief 在无清理和致命故障时更新认证就绪状态；完整合同见 qwen_session_policy.h。
 */
void qwen_session_policy_auth(qwen_session_policy_t *policy, uint8_t ready)
{
    if (policy->cleanup_pending || policy->fatal) return;
    if (policy->auth_ready != ready) {
        policy->auth_ready = ready;
        if (ready) policy->start_error = D_OK;
        policy->revision++;
    }
}

/**
 * @brief 仅在当前策略允许时发布已连接事实；完整合同见 qwen_session_policy.h。
 */
void qwen_session_policy_connected(qwen_session_policy_t *policy, uint8_t connected)
{
    connected = connected && qwen_session_policy_allows(policy);
    if (policy->connected != connected) {
        policy->connected = connected;
        policy->revision++;
    }
}

/**
 * @brief 检查初始化、网络、运行资格、请求和清理条件是否允许业务；完整合同见 qwen_session_policy.h。
 */
uint8_t qwen_session_policy_allows(const qwen_session_policy_t *policy)
{
    return policy->initialized && policy->wifi_ready && policy->runtime_allowed &&
           policy->requested && !policy->cleanup_pending && !policy->fatal;
}

/**
 * @brief 由纯策略生成阶段、错误与可操作性快照；完整合同见 qwen_session_policy.h。
 */
void qwen_session_policy_snapshot(const qwen_session_policy_t *policy,
                                 qwen_session_snapshot_t *snapshot)
{
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->generation = policy->generation;
    snapshot->revision = policy->revision;
    snapshot->initialized = policy->initialized;
    snapshot->wifi_ready = policy->wifi_ready;
    snapshot->runtime_allowed = policy->runtime_allowed;
    snapshot->auth_ready = policy->auth_ready;
    snapshot->requested = policy->requested;
    snapshot->connected = policy->connected;
    snapshot->fatal = policy->fatal;
    snapshot->error = policy->cleanup_error != D_OK ?
                      policy->cleanup_error : policy->start_error;
    snapshot->can_start = policy->initialized && policy->wifi_ready &&
        policy->runtime_allowed && !policy->requested &&
        !policy->cleanup_pending && !policy->fatal;
    snapshot->can_stop = !policy->fatal &&
        (policy->requested || policy->cleanup_pending);
    if (!policy->initialized)
        snapshot->phase = QWEN_SESSION_UNAVAILABLE;
    else if (policy->fatal || policy->cleanup_failed)
        snapshot->phase = QWEN_SESSION_ERROR;
    else if (policy->cleanup_pending)
        snapshot->phase = QWEN_SESSION_STOPPING;
    else if (!policy->runtime_allowed)
        snapshot->phase = QWEN_SESSION_UNAVAILABLE;
    else if (!policy->wifi_ready)
        snapshot->phase = QWEN_SESSION_OFFLINE;
    else if (policy->connected)
        snapshot->phase = QWEN_SESSION_ACTIVE;
    else if (policy->requested)
        snapshot->phase = QWEN_SESSION_STARTING;
    else if (policy->start_error != D_OK)
        snapshot->phase = QWEN_SESSION_ERROR;
    else
        snapshot->phase = policy->auth_ready ? QWEN_SESSION_READY : QWEN_SESSION_PREPARING;
}
