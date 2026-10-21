/** @file qwen_session_policy.c @brief Pure policy, serialized by the service. */
#include "qwen_session_policy.h"
#include <string.h>

void qwen_session_policy_init(qwen_session_policy_t *policy, uint8_t automatic)
{
    memset(policy, 0, sizeof(*policy));
    policy->initialized = 1U;
    policy->automatic = automatic;
}

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

void qwen_session_policy_runtime(qwen_session_policy_t *policy, uint8_t allowed)
{
    if (policy->runtime_allowed == allowed) return;
    policy->runtime_allowed = allowed;
    policy->revision++;
    if (!allowed) qwen_session_policy_cleanup(policy, 0U);
}

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

d_err_t qwen_session_policy_stop(qwen_session_policy_t *policy)
{
    if (!policy->initialized) return D_ERR_NOT_READY;
    if (policy->fatal) return D_ERR_IO;
    if (!policy->requested) return D_OK;
    policy->start_error = D_OK;
    qwen_session_policy_cleanup(policy, 0U);
    return D_OK;
}

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

void qwen_session_policy_failed(qwen_session_policy_t *policy, d_err_t error)
{
    policy->start_error = error;
    qwen_session_policy_cleanup(policy, 0U);
}

void qwen_session_policy_auth(qwen_session_policy_t *policy, uint8_t ready)
{
    if (policy->cleanup_pending || policy->fatal) return;
    if (policy->auth_ready != ready) {
        policy->auth_ready = ready;
        if (ready) policy->start_error = D_OK;
        policy->revision++;
    }
}

void qwen_session_policy_connected(qwen_session_policy_t *policy, uint8_t connected)
{
    connected = connected && qwen_session_policy_allows(policy);
    if (policy->connected != connected) {
        policy->connected = connected;
        policy->revision++;
    }
}

uint8_t qwen_session_policy_allows(const qwen_session_policy_t *policy)
{
    return policy->initialized && policy->wifi_ready && policy->runtime_allowed &&
           policy->requested && !policy->cleanup_pending && !policy->fatal;
}

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
