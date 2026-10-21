/**
 * @file camera_preview_state.h
 * @brief Session intent and absolute deadlines, independent of SDK scheduling.
 * @author D_Code
 * @date 2026-09-11
 */
#ifndef __CAMERA_PREVIEW_STATE_H__
#define __CAMERA_PREVIEW_STATE_H__
#include "camera_preview.h"

#define CAMERA_PREVIEW_START_TIMEOUT_MS  5000U
#define CAMERA_PREVIEW_FRAME_TIMEOUT_MS  3000U
#define CAMERA_PREVIEW_STOP_TIMEOUT_MS   3000U

/* Caller serializes these small metadata operations; never holds the lock for SDK work. */
typedef struct {
    camera_preview_snapshot_t status;
    uint32_t started_ms;
    uint32_t last_frame_ms;
    uint32_t stopped_ms;
    uint8_t has_stop_deadline;
    uint8_t is_transaction_claimed;
} camera_preview_state_t;

void camera_preview_state_init(camera_preview_state_t *state, d_err_t result);
d_err_t camera_preview_state_start(camera_preview_state_t *state, uint32_t now_ms,
                                   uint32_t *session);
void camera_preview_state_stop(camera_preview_state_t *state, uint32_t session,
                               camera_preview_stop_e reason, uint32_t now_ms);
void camera_preview_state_check_time(camera_preview_state_t *state, uint32_t now_ms);
void camera_preview_state_fail(camera_preview_state_t *state, camera_preview_error_e error,
                               int sdk_result, uint32_t now_ms);
void camera_preview_state_finish(camera_preview_state_t *state, uint8_t is_fault);
uint8_t camera_preview_state_claim(camera_preview_state_t *state);
uint8_t camera_preview_state_can_publish(const camera_preview_state_t *state, uint32_t session);
void camera_preview_state_frame(camera_preview_state_t *state, uint32_t now_ms);
void camera_preview_state_publish(camera_preview_state_t *state);
void camera_preview_state_snapshot(const camera_preview_state_t *state, uint32_t now_ms,
                                   camera_preview_snapshot_t *snapshot);

#endif
