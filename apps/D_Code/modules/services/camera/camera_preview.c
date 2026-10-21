/**
 * @file camera_preview.c
 * @brief One persistent SDK owner, supervised independently by sys_timer.
 * @author D_Code
 * @date 2026-09-11
 */
#include "config/app_config.h"
#if D_CAMERA_PREVIEW_ENABLE
#include "camera_preview_state.h"
#include "camera_frame.h"
#include "framework/d_log.h"
#include "system/includes.h"
#include "device.h"
#include "video.h"
#include "video_ioctl.h"
#include "camera.h"
#include <string.h>

#define TAG "CAMERA"
#define CAMERA_PREVIEW_WORKER_PRIORITY 26
#define CAMERA_PREVIEW_WORKER_STACK_WORDS 2048
#define CAMERA_PREVIEW_MONITOR_MS 100U
#define CAMERA_PREVIEW_BAD_FRAME_LIMIT 3U

typedef enum {
    CAMERA_PREVIEW_FRAME_SKIPPED = 0,
    CAMERA_PREVIEW_FRAME_PROCESSED,
    CAMERA_PREVIEW_FRAME_FAILED,
} camera_preview_frame_result_e;

typedef struct {
    camera_preview_state_t policy;
    OS_SEM work_sem;
    int worker_pid;
    uint16_t supervisor_id;
    uint8_t has_init_attempt;
    /* The following fields belong exclusively to the worker, including in FAULT. */
    void *video;
    struct yuv_image frame;
    uint8_t has_sdk_frame;
} camera_preview_context_t;

static camera_preview_context_t s_camera = {
    .policy.status = { .phase = CAMERA_PREVIEW_UNAVAILABLE, .init_result = D_ERR_NOT_READY,
                       .is_cleanup_complete = 1U },
};
static camera_frame_pool_t s_camera_frames;
static uint8_t s_camera_capture_yuv[CAMERA_FRAME_INPUT_BYTES] __attribute__((aligned(32)));
static uint8_t s_camera_overlay_yuv[CAMERA_FRAME_INPUT_BYTES] __attribute__((aligned(32)));

/* Polling enforces deadlines and can revoke an expired request before returning. */
static uint8_t camera_preview_poll_request(uint32_t session)
{
    uint8_t is_requested;
    OS_ENTER_CRITICAL();
    camera_preview_state_check_time(&s_camera.policy, timer_get_ms());
    is_requested = camera_preview_state_can_publish(&s_camera.policy, session);
    OS_EXIT_CRITICAL();
    return is_requested;
}

static void camera_preview_mark_sdk_busy(uint8_t is_busy)
{
    OS_ENTER_CRITICAL();
    s_camera.policy.status.is_sdk_busy = is_busy;
    camera_preview_state_check_time(&s_camera.policy, timer_get_ms());
    OS_EXIT_CRITICAL();
}

static int camera_preview_ioctl(uint32_t command, uintptr_t argument)
{
    int result;
    camera_preview_mark_sdk_busy(1U);
    result = dev_ioctl(s_camera.video, command, (u32)argument);
    camera_preview_mark_sdk_busy(0U);
    return result;
}

static void camera_preview_fail(camera_preview_error_e error, int result, uint8_t is_fault)
{
    OS_ENTER_CRITICAL();
    camera_preview_state_fail(&s_camera.policy, error, result, timer_get_ms());
    camera_frame_discard_ready(&s_camera_frames);
    if (is_fault) {
        camera_preview_state_finish(&s_camera.policy, 1U);
    }
    OS_EXIT_CRITICAL();
    LOGW(TAG, "session failure: error=%u sdk=%d fault=%u", (unsigned)error, result, is_fault);
}

static int camera_preview_return_frame(void)
{
    if (!s_camera.has_sdk_frame) {
        return 0;
    }
    /* A failing CLEAR has an unknown outcome. Never submit it twice for this lease. */
    s_camera.has_sdk_frame = 0U;
    return camera_preview_ioctl(VIDIOC_CLEAR_OVERLAY, (uintptr_t)&s_camera.frame);
}

static uint8_t camera_preview_frame_is_valid(void)
{
    const struct yuv_image *frame = &s_camera.frame;
    /* This single-frame external allocation leaves no room for an offset full VGA image. */
    return frame->addr == s_camera_overlay_yuv && frame->size == CAMERA_FRAME_INPUT_BYTES &&
           frame->width == CAMERA_FRAME_INPUT_WIDTH && frame->height == CAMERA_FRAME_INPUT_HEIGHT;
}

static camera_preview_frame_result_e camera_preview_process_frame(uint32_t session)
{
    int slot = -1;
    d_err_t result;
    uint8_t can_publish;
    uint32_t now_ms = timer_get_ms();
    if (!camera_preview_frame_is_valid()) {
        return CAMERA_PREVIEW_FRAME_FAILED;
    }
    OS_ENTER_CRITICAL();
    camera_preview_state_frame(&s_camera.policy, now_ms);
    camera_preview_state_check_time(&s_camera.policy, now_ms);
    if (camera_preview_state_can_publish(&s_camera.policy, session) &&
        camera_frame_advance_if_due(&s_camera_frames, now_ms)) {
        slot = camera_frame_acquire_write(&s_camera_frames, session);
    }
    if (slot < 0) {
        s_camera.policy.status.dropped_frames++;
    }
    OS_EXIT_CRITICAL();
    if (slot < 0) {
        return CAMERA_PREVIEW_FRAME_SKIPPED;
    }

    result = camera_frame_convert(s_camera.frame.addr, s_camera_frames.pixels[slot]);
    OS_ENTER_CRITICAL();
    camera_preview_state_check_time(&s_camera.policy, timer_get_ms());
    can_publish = result == D_OK && camera_preview_state_can_publish(&s_camera.policy, session);
    camera_frame_finish_write(&s_camera_frames, slot, can_publish);
    if (can_publish) {
        camera_preview_state_publish(&s_camera.policy);
    }
    OS_EXIT_CRITICAL();
    return result == D_OK ? CAMERA_PREVIEW_FRAME_PROCESSED : CAMERA_PREVIEW_FRAME_FAILED;
}

static void camera_preview_close_session(void)
{
    int stop_result, frame_result, close_result;
    uint8_t is_idle;
    OS_ENTER_CRITICAL();
    camera_frame_discard_ready(&s_camera_frames);
    OS_EXIT_CRITICAL();
    stop_result = camera_preview_ioctl(VIDIOC_SET_HDWARE_STATE, 0U);
    frame_result = camera_preview_return_frame();
    if (stop_result != 0 || frame_result != 0) {
        camera_preview_fail(CAMERA_PREVIEW_ERROR_CLEANUP,
                            stop_result != 0 ? stop_result : frame_result, 1U);
        return;
    }
    camera_preview_mark_sdk_busy(1U);
    close_result = dev_close(s_camera.video);
    camera_preview_mark_sdk_busy(0U);
    if (close_result != 0) {
        camera_preview_fail(CAMERA_PREVIEW_ERROR_CLEANUP, close_result, 1U);
        return;
    }
    s_camera.video = NULL;
    /* UI may still be copying. Keep its lease and all storage alive until it returns. */
    do {
        OS_ENTER_CRITICAL();
        is_idle = camera_frame_is_idle(&s_camera_frames);
        if (is_idle) {
            camera_preview_state_finish(&s_camera.policy, 0U);
        }
        OS_EXIT_CRITICAL();
        if (!is_idle) {
            os_time_dly(1);
        }
    } while (!is_idle);
    LOGI(TAG, "stopped: SDK steps complete, RGB leases returned");
}

static void camera_preview_run_session(uint32_t session)
{
    struct camera_device_info sensor = {0};
    struct video_format format = {0};
    struct yuv_image overlay = {0};
    unsigned bad_frames = 0U;
    camera_preview_frame_result_e frame_result;
    int result;

    /* Once claimed, cancellation must not abandon the half-open vendor transaction. */
    camera_preview_mark_sdk_busy(1U);
    s_camera.video = dev_open("video0.2", NULL);
    camera_preview_mark_sdk_busy(0U);
    if (s_camera.video == NULL) {
        camera_preview_fail(CAMERA_PREVIEW_ERROR_NO_SENSOR, D_ERR_NOT_FOUND, 0U);
        OS_ENTER_CRITICAL();
        camera_preview_state_finish(&s_camera.policy, 0U);
        OS_EXIT_CRITICAL();
        return;
    }
    result = camera_preview_ioctl(CAMERA_GET_SENSOR_INFO, (uintptr_t)&sensor);
    if (result != 0 || sensor.width != CAMERA_FRAME_INPUT_WIDTH || sensor.height != CAMERA_FRAME_INPUT_HEIGHT) {
        camera_preview_fail(CAMERA_PREVIEW_ERROR_SENSOR_INFO, result != 0 ? result : D_ERR_INVALID_ARG, 1U);
        return;
    }
    format.type = VIDEO_BUF_TYPE_VIDEO_OVERLAY;
    format.pixelformat = VIDEO_PIX_FMT_YUV420;
    format.static_buf = s_camera_capture_yuv;
    format.sbuf_size = sizeof(s_camera_capture_yuv);
    result = camera_preview_ioctl(VIDIOC_SET_FMT, (uintptr_t)&format);
    if (result != 0) {
        camera_preview_fail(CAMERA_PREVIEW_ERROR_FORMAT, result, 1U);
        return;
    }
    overlay.addr = s_camera_overlay_yuv;
    overlay.size = sizeof(s_camera_overlay_yuv);
    result = camera_preview_ioctl(VIDIOC_SET_OVERLAY, (uintptr_t)&overlay);
    if (result != 0) {
        camera_preview_fail(CAMERA_PREVIEW_ERROR_OVERLAY, result, 1U);
        return;
    }
    /* Only this milestone establishes the normal close contract. No GET after cancellation. */
    s_camera_frames.next_process_ms = timer_get_ms();
    while (camera_preview_poll_request(session)) {
        memset(&s_camera.frame, 0, sizeof(s_camera.frame));
        result = camera_preview_ioctl(VIDIOC_GET_OVERLAY, (uintptr_t)&s_camera.frame);
        if (result != 0) {
            if (result == OS_TIMEOUT) {
                os_time_dly(1);
                continue;
            }
            camera_preview_fail(CAMERA_PREVIEW_ERROR_TRANSPORT, result, 0U);
            break;
        }
        s_camera.has_sdk_frame = 1U;
        if (!camera_preview_poll_request(session)) {
            break;
        }
        frame_result = camera_preview_process_frame(session);
        if (frame_result == CAMERA_PREVIEW_FRAME_FAILED) {
            bad_frames++;
        } else if (frame_result == CAMERA_PREVIEW_FRAME_PROCESSED) {
            bad_frames = 0U;
        }
        /* A cadence/slot skip neither succeeds nor breaks a processing failure streak. */
        if (bad_frames >= CAMERA_PREVIEW_BAD_FRAME_LIMIT) {
            camera_preview_fail(CAMERA_PREVIEW_ERROR_FRAME, D_ERR_IO, 0U);
            break;
        }
        if (!camera_preview_poll_request(session)) {
            break;
        }
        result = camera_preview_return_frame();
        if (result != 0) {
            camera_preview_fail(CAMERA_PREVIEW_ERROR_CLEANUP, result, 1U);
            return;
        }
    }
    camera_preview_close_session();
}

static void camera_preview_supervise(void *context)
{
    (void)context;
    OS_ENTER_CRITICAL();
    camera_preview_state_check_time(&s_camera.policy, timer_get_ms());
    OS_EXIT_CRITICAL();
}

static void camera_preview_worker(void *context)
{
    uint32_t session;
    uint8_t is_claimed;
    (void)context;
    for (;;) {
        /* The bounded idle poll also covers an unsuccessful work notification. */
        (void)os_sem_pend(&s_camera.work_sem, OS_TICKS_PER_SEC);
        OS_ENTER_CRITICAL();
        camera_preview_state_check_time(&s_camera.policy, timer_get_ms());
        is_claimed = camera_preview_state_claim(&s_camera.policy);
        session = s_camera.policy.status.session;
        if (!is_claimed && s_camera.policy.status.phase == CAMERA_PREVIEW_STOPPING &&
            !s_camera.policy.is_transaction_claimed) {
            camera_preview_state_finish(&s_camera.policy, 0U);
        }
        OS_EXIT_CRITICAL();
        if (is_claimed) {
            camera_preview_run_session(session);
        }
    }
}

d_err_t camera_preview_init(void)
{
    d_err_t result = D_OK;
    OS_ENTER_CRITICAL();
    if (s_camera.has_init_attempt) {
        result = s_camera.policy.status.init_result;
        OS_EXIT_CRITICAL();
        return result;
    }
    s_camera.has_init_attempt = 1U;
    OS_EXIT_CRITICAL();
    camera_frame_init(&s_camera_frames);
    if (os_sem_create(&s_camera.work_sem, 0) != OS_NO_ERR) {
        result = D_ERR_NO_MEMORY;
    } else {
        s_camera.supervisor_id = sys_timer_add_to_task("sys_timer", NULL,
                                                     camera_preview_supervise, CAMERA_PREVIEW_MONITOR_MS);
        if (s_camera.supervisor_id == 0U ||
            thread_fork("camera_preview", CAMERA_PREVIEW_WORKER_PRIORITY,
                        CAMERA_PREVIEW_WORKER_STACK_WORDS, 0, &s_camera.worker_pid,
                        camera_preview_worker, NULL) != 0) {
            result = D_ERR_NO_MEMORY;
        }
    }
    /* Failed initialization keeps any created objects alive; late callbacks remain safe. */
    OS_ENTER_CRITICAL();
    camera_preview_state_init(&s_camera.policy, result);
    OS_EXIT_CRITICAL();
    LOGI(TAG, "control init=%d; sensor remains closed", result);
    return result;
}

d_err_t camera_preview_request_start(uint32_t *session)
{
    d_err_t result;
    OS_ENTER_CRITICAL();
    camera_preview_state_check_time(&s_camera.policy, timer_get_ms());
    result = camera_preview_state_start(&s_camera.policy, timer_get_ms(), session);
    OS_EXIT_CRITICAL();
    if (result == D_OK) {
        (void)os_sem_post(&s_camera.work_sem);
    }
    return result;
}

void camera_preview_request_stop(uint32_t session, camera_preview_stop_e reason)
{
    uint8_t is_initialized;
    OS_ENTER_CRITICAL();
    camera_preview_state_stop(&s_camera.policy, session, reason, timer_get_ms());
    if (!s_camera.policy.status.is_requested) {
        camera_frame_discard_ready(&s_camera_frames);
    }
    is_initialized = s_camera.policy.status.is_initialized;
    OS_EXIT_CRITICAL();
    if (is_initialized) {
        (void)os_sem_post(&s_camera.work_sem);
    }
}

void camera_preview_get_snapshot(camera_preview_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }
    OS_ENTER_CRITICAL();
    camera_preview_state_snapshot(&s_camera.policy, timer_get_ms(), snapshot);
    OS_EXIT_CRITICAL();
}

d_err_t camera_preview_copy_latest(uint32_t session, void *destination,
                                   uint32_t capacity, uint32_t *sequence)
{
    int slot = -1;
    uint8_t is_current;
    uint32_t copied_sequence;
    if (destination == NULL || sequence == NULL || capacity < CAMERA_PREVIEW_RGB_BYTES) {
        return D_ERR_INVALID_ARG;
    }
    OS_ENTER_CRITICAL();
    camera_preview_state_check_time(&s_camera.policy, timer_get_ms());
    if (camera_preview_state_can_publish(&s_camera.policy, session)) {
        slot = camera_frame_acquire_read(&s_camera_frames, session, *sequence);
    }
    OS_EXIT_CRITICAL();
    if (slot < 0) {
        return D_ERR_NOT_READY;
    }
    copied_sequence = s_camera_frames.leases[slot].sequence;
    memcpy(destination, s_camera_frames.pixels[slot], CAMERA_PREVIEW_RGB_BYTES);
    OS_ENTER_CRITICAL();
    camera_preview_state_check_time(&s_camera.policy, timer_get_ms());
    is_current = camera_preview_state_can_publish(&s_camera.policy, session);
    camera_frame_release_read(&s_camera_frames, slot);
    if (is_current) {
        s_camera.policy.status.copied_frames++;
    }
    OS_EXIT_CRITICAL();
    if (!is_current) {
        return D_ERR_NOT_READY;
    }
    *sequence = copied_sequence;
    return D_OK;
}

d_err_t camera_preview_wait_stopped(uint32_t timeout_ms)
{
    camera_preview_snapshot_t snapshot;
    uint32_t began_ms = timer_get_ms();
    for (;;) {
        camera_preview_get_snapshot(&snapshot);
        if (snapshot.phase == CAMERA_PREVIEW_FAULT) {
            return D_ERR_IO;
        }
        if (snapshot.is_cleanup_complete) {
            return D_OK;
        }
        if ((uint32_t)(timer_get_ms() - began_ms) >= timeout_ms) {
            return D_ERR_TIMEOUT;
        }
        os_time_dly(1);
    }
}

void camera_preview_shutdown_begin(void)
{
    uint8_t is_initialized;
    OS_ENTER_CRITICAL();
    s_camera.policy.status.is_shutdown_blocked = 1U;
    s_camera.policy.status.shutdown_epoch++;
    s_camera.policy.status.stop_reason = CAMERA_PREVIEW_STOP_SHUTDOWN;
    s_camera.policy.status.revision++;
    camera_preview_state_stop(&s_camera.policy, s_camera.policy.status.session,
                              CAMERA_PREVIEW_STOP_SHUTDOWN, timer_get_ms());
    camera_frame_discard_ready(&s_camera_frames);
    is_initialized = s_camera.policy.status.is_initialized;
    OS_EXIT_CRITICAL();
    if (is_initialized) {
        (void)os_sem_post(&s_camera.work_sem);
    }
}

void camera_preview_shutdown_abort(void)
{
    OS_ENTER_CRITICAL();
    s_camera.policy.status.is_shutdown_blocked = 0U;
    s_camera.policy.status.revision++;
    OS_EXIT_CRITICAL();
}

#endif /* D_CAMERA_PREVIEW_ENABLE */
