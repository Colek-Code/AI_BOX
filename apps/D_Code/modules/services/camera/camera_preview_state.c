/**
 * @file camera_preview_state.c
 * @brief 相机会话请求策略；SDK 收尾事实仅由资源所有者发布。
 * @author D_Code
 * @date 2026-09-11
 */
#include "camera_preview_state.h"
#include <string.h>

/**
 * @brief 建立初始化终态；完整合同见 camera_preview_state.h。
 */
void camera_preview_state_init(camera_preview_state_t *state, d_err_t result)
{
    memset(state, 0, sizeof(*state));
    state->status.init_result = result;
    state->status.is_init_complete = 1U;
    state->status.is_initialized = result == D_OK;
    state->status.is_cleanup_complete = 1U;
    state->status.phase = result == D_OK ? CAMERA_PREVIEW_IDLE : CAMERA_PREVIEW_UNAVAILABLE;
    state->status.error = result == D_OK ? CAMERA_PREVIEW_ERROR_NONE : CAMERA_PREVIEW_ERROR_INIT;
}

/**
 * @brief 受理预览意图；完整合同见 camera_preview_state.h。
 */
d_err_t camera_preview_state_start(camera_preview_state_t *state, uint32_t now_ms,
                                   uint32_t *session)
{
    camera_preview_snapshot_t *status = &state->status;
    if (session == NULL) {
        return D_ERR_INVALID_ARG;
    }
    if (!status->is_initialized) {
        return D_ERR_NOT_READY;
    }
    if (status->phase == CAMERA_PREVIEW_FAULT) {
        return D_ERR_IO;
    }
    if (status->is_shutdown_blocked) {
        return D_ERR_BUSY;
    }
    if (status->is_requested) {
        *session = status->session;
        return D_OK;
    }
    if (!status->is_cleanup_complete) {
        return D_ERR_BUSY;
    }
    if (++status->session == 0U) {
        ++status->session;
    }
    status->revision++;
    status->phase = CAMERA_PREVIEW_STARTING;
    status->error = CAMERA_PREVIEW_ERROR_NONE;
    status->sdk_result = 0;
    status->is_requested = 1U;
    status->is_cleanup_complete = 0U;
    status->is_stop_overdue = 0U;
    status->stop_elapsed_ms = 0U;
    status->stop_reason = CAMERA_PREVIEW_STOP_PAGE;
    state->started_ms = now_ms;
    state->last_frame_ms = now_ms;
    state->has_stop_deadline = 0U;
    state->is_transaction_claimed = 0U;
    *session = status->session;
    return D_OK;
}

/**
 * @brief 撤销匹配会话意图；完整合同见 camera_preview_state.h。
 */
void camera_preview_state_stop(camera_preview_state_t *state, uint32_t session,
                               camera_preview_stop_e reason, uint32_t now_ms)
{
    camera_preview_snapshot_t *status = &state->status;
    if (session != status->session || status->is_cleanup_complete ||
        status->phase == CAMERA_PREVIEW_FAULT) {
        return;
    }
    status->is_requested = 0U;
    status->phase = CAMERA_PREVIEW_STOPPING;
    if (!state->has_stop_deadline) {
        state->has_stop_deadline = 1U;
        state->stopped_ms = now_ms;
        status->stop_reason = reason;
        status->revision++;
    }
}

/**
 * @brief 记录错误并请求停止；完整合同见 camera_preview_state.h。
 */
void camera_preview_state_fail(camera_preview_state_t *state, camera_preview_error_e error,
                               int sdk_result, uint32_t now_ms)
{
    state->status.error = error;
    state->status.sdk_result = sdk_result;
    state->status.revision++;
    camera_preview_state_stop(state, state->status.session, CAMERA_PREVIEW_STOP_ERROR, now_ms);
}

/**
 * @brief 检查绝对期限；完整合同见 camera_preview_state.h。
 */
void camera_preview_state_check_time(camera_preview_state_t *state, uint32_t now_ms)
{
    camera_preview_snapshot_t *status = &state->status;
    if (status->phase == CAMERA_PREVIEW_STARTING &&
        (uint32_t)(now_ms - state->started_ms) >= CAMERA_PREVIEW_START_TIMEOUT_MS) {
        /* 即使监督执行偏晚，首次停止期限仍从实际超时点起算。 */
        camera_preview_state_fail(state, CAMERA_PREVIEW_ERROR_START_TIMEOUT, D_ERR_TIMEOUT,
                                  state->started_ms + CAMERA_PREVIEW_START_TIMEOUT_MS);
    } else if (status->phase == CAMERA_PREVIEW_RUNNING &&
               (uint32_t)(now_ms - state->last_frame_ms) >= CAMERA_PREVIEW_FRAME_TIMEOUT_MS) {
        camera_preview_state_fail(state, CAMERA_PREVIEW_ERROR_FRAME_TIMEOUT, D_ERR_TIMEOUT,
                                  state->last_frame_ms + CAMERA_PREVIEW_FRAME_TIMEOUT_MS);
    }
    if (state->has_stop_deadline && !status->is_cleanup_complete) {
        status->stop_elapsed_ms = now_ms - state->stopped_ms;
        if (status->stop_elapsed_ms >= CAMERA_PREVIEW_STOP_TIMEOUT_MS && !status->is_stop_overdue) {
            status->is_stop_overdue = 1U;
            status->revision++;
        }
    }
}

/**
 * @brief 领取一次 SDK 事务执行权；完整合同见 camera_preview_state.h。
 */
uint8_t camera_preview_state_claim(camera_preview_state_t *state)
{
    if (!state->status.is_requested || state->is_transaction_claimed) {
        return 0U;
    }
    state->is_transaction_claimed = 1U;
    return 1U;
}

/**
 * @brief 核对当前会话发布资格；完整合同见 camera_preview_state.h。
 */
uint8_t camera_preview_state_can_publish(const camera_preview_state_t *state, uint32_t session)
{
    return state->status.is_requested && state->status.session == session &&
           (state->status.phase == CAMERA_PREVIEW_STARTING || state->status.phase == CAMERA_PREVIEW_RUNNING);
}

/**
 * @brief 更新有效输入帧计数；完整合同见 camera_preview_state.h。
 */
void camera_preview_state_frame(camera_preview_state_t *state, uint32_t now_ms)
{
    state->last_frame_ms = now_ms;
    state->status.captured_frames++;
}

/**
 * @brief 发布运行阶段及帧计数；完整合同见 camera_preview_state.h。
 */
void camera_preview_state_publish(camera_preview_state_t *state)
{
    if (!state->status.is_requested) {
        return;
    }
    if (state->status.phase != CAMERA_PREVIEW_RUNNING) {
        state->status.revision++;
    }
    state->status.phase = CAMERA_PREVIEW_RUNNING;
    state->status.published_frames++;
}

/**
 * @brief 发布所有者确认的收尾状态；完整合同见 camera_preview_state.h。
 */
void camera_preview_state_finish(camera_preview_state_t *state, uint8_t is_fault)
{
    camera_preview_snapshot_t *status = &state->status;
    status->is_requested = 0U;
    status->is_sdk_busy = 0U;
    status->is_cleanup_complete = !is_fault;
    status->phase = is_fault ? CAMERA_PREVIEW_FAULT :
                    (status->error == CAMERA_PREVIEW_ERROR_NONE ? CAMERA_PREVIEW_IDLE : CAMERA_PREVIEW_ERROR);
    status->revision++;
    state->is_transaction_claimed = 0U;
}

/**
 * @brief 生成含时效诊断的局部快照；完整合同见 camera_preview_state.h。
 */
void camera_preview_state_snapshot(const camera_preview_state_t *state, uint32_t now_ms,
                                   camera_preview_snapshot_t *snapshot)
{
    camera_preview_state_t current = *state;
    camera_preview_state_check_time(&current, now_ms);
    *snapshot = current.status;
    snapshot->can_start = snapshot->is_initialized && !snapshot->is_shutdown_blocked &&
                          snapshot->is_cleanup_complete && snapshot->phase != CAMERA_PREVIEW_FAULT;
}
