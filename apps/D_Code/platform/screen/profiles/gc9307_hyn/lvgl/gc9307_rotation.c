/**
 * @file gc9307_rotation.c
 * @brief 仅在完整帧确认后公布方向；超时或完成不确定时不隐式释放占用。
 */
#include "gc9307_rotation.h"
#include <string.h>

/**
 * @brief 推进 32 位编号并跳过保留的零值。
 * @param[in] value 当前编号。
 * @return 递增后的非零编号；UINT32_MAX 回绕后为 1。
 */
static uint32_t gc9307_rotation_next(uint32_t value)
{
    value++;
    return value ? value : 1U;
}

/**
 * @brief 建立方向事务初态；完整合同见 gc9307_rotation.h。
 */
void gc9307_rotation_init(gc9307_rotation_t *state, uint16_t degrees)
{
    memset(state, 0, sizeof(*state));
    state->snapshot.actual_degrees = degrees;
    state->snapshot.target_degrees = degrees;
    state->snapshot.generation = 1U;
    state->render_degrees = degrees;
}

/**
 * @brief 登记方向切换；完整合同见 gc9307_rotation.h。
 */
d_err_t gc9307_rotation_request(gc9307_rotation_t *state, uint16_t degrees, uint32_t *id)
{
    if (id) *id = 0U;
    if (degrees != 0U && degrees != 180U) return D_ERR_INVALID_ARG;
    if (state->phase != GC9307_ROTATION_IDLE) return D_ERR_BUSY;
    if (degrees == state->snapshot.actual_degrees) {
        if (id) *id = state->snapshot.completed_id;
        return D_OK;
    }
    state->snapshot.request_id = gc9307_rotation_next(state->snapshot.request_id);
    state->snapshot.target_degrees = degrees;
    state->snapshot.result = D_OK;
    state->snapshot.is_busy = 1U;
    state->phase = GC9307_ROTATION_DRAIN;
    if (id) *id = state->snapshot.request_id;
    return D_OK;
}

/**
 * @brief 在旧作业排空后推进渲染代次；完整合同见 gc9307_rotation.h。
 */
void gc9307_rotation_drained(gc9307_rotation_t *state)
{
    if (state->phase != GC9307_ROTATION_DRAIN) return;
    state->snapshot.generation = gc9307_rotation_next(state->snapshot.generation);
    state->render_degrees = state->snapshot.target_degrees;
    state->phase = GC9307_ROTATION_FRAME;
}

/**
 * @brief 核对票据及代次后接纳帧完成；完整合同见 gc9307_rotation.h。
 */
void gc9307_rotation_complete(gc9307_rotation_t *state, uint32_t id,
                             uint32_t generation, d_err_t result)
{
    if ((state->phase != GC9307_ROTATION_FRAME && state->phase != GC9307_ROTATION_ROLLBACK) ||
        id != state->snapshot.request_id || generation != state->snapshot.generation) return;
    if (result == D_OK) {
        if (state->phase == GC9307_ROTATION_FRAME) {
            state->snapshot.actual_degrees = state->render_degrees;
        }
        state->snapshot.completed_id = id;
        state->snapshot.is_busy = 0U;
        state->phase = GC9307_ROTATION_IDLE;
        return;
    }
    state->snapshot.result = D_ERR_IO;
    if (result == D_ERR_INVALID_ARG && state->phase == GC9307_ROTATION_FRAME) {
        state->render_degrees = state->snapshot.actual_degrees;
        state->snapshot.generation = gc9307_rotation_next(state->snapshot.generation);
        state->phase = GC9307_ROTATION_ROLLBACK;
    } else {
        state->phase = GC9307_ROTATION_FAULT;
    }
}
