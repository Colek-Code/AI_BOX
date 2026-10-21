/** @file qwen_session_policy.h @brief Bounded session intent and cleanup policy. */
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

void qwen_session_policy_init(qwen_session_policy_t *policy, uint8_t automatic);
void qwen_session_policy_network(qwen_session_policy_t *policy, uint8_t ready);
void qwen_session_policy_runtime(qwen_session_policy_t *policy, uint8_t allowed);
d_err_t qwen_session_policy_start(qwen_session_policy_t *policy);
d_err_t qwen_session_policy_stop(qwen_session_policy_t *policy);
void qwen_session_policy_cleanup(qwen_session_policy_t *policy, uint8_t preserve);
/**
 * @brief 记录一次后台清理尝试的结果。
 * @param policy 由服务持有并已初始化的策略对象，调用方负责串行访问。
 * @param result D_OK 才表示清理完成；失败时保留 cleanup_pending。
 * @param fatal 非零表示结果不可靠，禁止再次开始并要求重启。
 */
void qwen_session_policy_set_cleanup_result(qwen_session_policy_t *policy,
                                           d_err_t result, uint8_t fatal);
void qwen_session_policy_failed(qwen_session_policy_t *policy, d_err_t error);
void qwen_session_policy_auth(qwen_session_policy_t *policy, uint8_t ready);
void qwen_session_policy_connected(qwen_session_policy_t *policy, uint8_t connected);
uint8_t qwen_session_policy_allows(const qwen_session_policy_t *policy);
void qwen_session_policy_snapshot(const qwen_session_policy_t *policy,
                                 qwen_session_snapshot_t *snapshot);
#endif
