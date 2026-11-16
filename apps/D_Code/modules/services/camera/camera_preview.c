/**
 * @file camera_preview.c
 * @brief 由持久工作任务独占 SDK，sys_timer 独立监督期限。
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
/* 预览尽力运行，不抢占 UI 绘制与显示完成工作任务。 */
#define CAMERA_PREVIEW_WORKER_PRIORITY 1
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
    /* 以下字段始终由工作任务独占，FAULT 阶段也不转移所有权。 */
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

/**
 * @brief 在短临界区内检查期限并核对会话发布资格。
 * @param[in] session 待核对的会话编号。
 * @return 指定会话仍被请求且可发布时返回非零。
 */
static uint8_t camera_preview_poll_request(uint32_t session)
{
    uint8_t is_requested;
    OS_ENTER_CRITICAL();
    camera_preview_state_check_time(&s_camera.policy, timer_get_ms());
    is_requested = camera_preview_state_can_publish(&s_camera.policy, session);
    OS_EXIT_CRITICAL();
    return is_requested;
}

/**
 * @brief 更新 SDK 在途标记并检查期限。
 * @param[in] is_busy 非零表示即将进入 SDK 调用，0 表示调用已返回。
 */
static void camera_preview_mark_sdk_busy(uint8_t is_busy)
{
    OS_ENTER_CRITICAL();
    s_camera.policy.status.is_sdk_busy = is_busy;
    camera_preview_state_check_time(&s_camera.policy, timer_get_ms());
    OS_EXIT_CRITICAL();
}

/**
 * @brief 在 SDK 控制调用前后更新在途标记。
 * @param[in] command SDK 控制命令。
 * @param[in] argument SDK 命令所需的整数或指针参数。
 * @return dev_ioctl 的原始返回值。
 * @note 仅由相机工作任务调用；SDK 阻塞期间不持元数据临界区。
 */
static int camera_preview_ioctl(uint32_t command, uintptr_t argument)
{
    int result;
    camera_preview_mark_sdk_busy(1U);
    result = dev_ioctl(s_camera.video, command, (u32)argument);
    camera_preview_mark_sdk_busy(0U);
    return result;
}

/**
 * @brief 记录错误并撤销意图，必要时冻结为 FAULT。
 * @param[in] error 业务错误分类。
 * @param[in] result SDK 原始结果。
 * @param[in] is_fault 非零表示不再自动正常收尾。
 * @note 丢弃 READY 帧，保留在途读写占用与 SDK 所有权。
 */
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

/**
 * @brief 对持有的 SDK 帧最多提交一次 CLEAR。
 * @return 无帧时返回 0；有帧时返回 CLEAR 的原始结果。
 * @note CLEAR 失败的结果不确定，先撤销本地持帧标记，不能对同一占用重复提交。
 */
static int camera_preview_return_frame(void)
{
    if (!s_camera.has_sdk_frame) {
        return 0;
    }
    /* CLEAR 失败时结果不确定，同一帧占用不能重复提交。 */
    s_camera.has_sdk_frame = 0U;
    return camera_preview_ioctl(VIDIOC_CLEAR_OVERLAY, (uintptr_t)&s_camera.frame);
}

/**
 * @brief 核对 SDK 帧是否完全落在指定的单帧外部缓冲。
 * @return 地址、尺寸与 VGA 格式全部匹配返回非零。
 */
static uint8_t camera_preview_frame_is_valid(void)
{
    const struct yuv_image *frame = &s_camera.frame;
    /* 外部分配仅容纳一张完整 VGA 帧，不允许地址偏移后仍按完整帧读取。 */
    return frame->addr == s_camera_overlay_yuv && frame->size == CAMERA_FRAME_INPUT_BYTES &&
           frame->width == CAMERA_FRAME_INPUT_WIDTH && frame->height == CAMERA_FRAME_INPUT_HEIGHT;
}

/**
 * @brief 按节拍领取 RGB 写槽，在锁外转换并重新核对发布资格。
 * @param[in] session 当前 SDK 事务所属会话。
 * @return 返回跳过、已处理或处理失败的枚举值。
 * @note 节拍或槽不足只丢帧，不计作格式转换成功。
 */
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

/**
 * @brief 停止硬件、归还 SDK 帧并关闭设备，再等待 RGB 读写占用归还。
 * @note SDK 清理失败进入 FAULT，保留尚未确认释放的资源；不提前撤销 UI 读占用。
 */
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
    /* UI 可能仍在复制，必须保留读占用及全部存储直到读者返回。 */
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

/**
 * @brief 顺序执行传感器配置、采帧处理及会话收尾。
 * @param[in] session 已经领取事务执行权的会话编号。
 * @note 领取后不能因取消而遗弃半开 SDK 事务；未建立正常关闭合同的配置错误进入 FAULT。
 */
static void camera_preview_run_session(uint32_t session)
{
    struct camera_device_info sensor = {0};
    struct video_format format = {0};
    struct yuv_image overlay = {0};
    unsigned bad_frames = 0U;
    camera_preview_frame_result_e frame_result;
    uint32_t stage_started_ms;
    uint32_t elapsed_ms;
    uint32_t get_max_ms = 0U;
    uint32_t process_max_ms = 0U;
    uint32_t process_total_ms = 0U;
    uint32_t process_attempts = 0U;
    int result;

    /* 已领取事务后，即使取消也不能遗弃半开的 SDK 事务。 */
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
    /* 到达此阶段才建立正常关闭合同；取消后不再发起 GET。 */
    s_camera_frames.next_process_ms = timer_get_ms();
    while (camera_preview_poll_request(session)) {
        memset(&s_camera.frame, 0, sizeof(s_camera.frame));
        stage_started_ms = timer_get_ms();
        result = camera_preview_ioctl(VIDIOC_GET_OVERLAY, (uintptr_t)&s_camera.frame);
        elapsed_ms = timer_get_ms() - stage_started_ms;
        if (elapsed_ms > get_max_ms) {
            get_max_ms = elapsed_ms;
        }
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
        stage_started_ms = timer_get_ms();
        frame_result = camera_preview_process_frame(session);
        if (frame_result != CAMERA_PREVIEW_FRAME_SKIPPED) {
            elapsed_ms = timer_get_ms() - stage_started_ms;
            process_total_ms += elapsed_ms;
            process_attempts++;
            if (elapsed_ms > process_max_ms) {
                process_max_ms = elapsed_ms;
            }
        }
        if (frame_result == CAMERA_PREVIEW_FRAME_FAILED) {
            bad_frames++;
        } else if (frame_result == CAMERA_PREVIEW_FRAME_PROCESSED) {
            bad_frames = 0U;
        }
        /* 节拍或槽不足导致的跳过既不算成功，也不中断连续处理失败计数。 */
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
        /* SDK 帧就绪不代表已经让出调度；归还帧占用后主动延时。 */
        os_time_dly(1);
    }
    camera_preview_close_session();
    /* 墙钟耗时包含抢占；仅退出时汇总输出，避免逐帧日志。 */
    LOGI(TAG, "session timing: attempts=%u process_avg_ms=%u process_max_ms=%u get_max_ms=%u",
         process_attempts, process_attempts ? process_total_ms / process_attempts : 0U,
         process_max_ms, get_max_ms);
}

/**
 * @brief 独立定时检查会话期限，不操作 SDK。
 * @param[in] context 定时回调兼容参数，本实现不使用。
 */
static void camera_preview_supervise(void *context)
{
    (void)context;
    OS_ENTER_CRITICAL();
    camera_preview_state_check_time(&s_camera.policy, timer_get_ms());
    OS_EXIT_CRITICAL();
}

/**
 * @brief 持续领取预览请求，并独占运行 SDK 会话。
 * @param[in] context 任务兼容参数，本实现不使用。
 * @note 限时空闲轮询兜底唤醒失败；取消尚未领取的事务可直接完成状态收尾。
 */
static void camera_preview_worker(void *context)
{
    uint32_t session;
    uint8_t is_claimed;
    (void)context;
    for (;;) {
        /* 有界空闲轮询也覆盖工作通知失败。 */
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

/**
 * @brief 一次性建立监督与工作对象；完整合同见 camera_preview.h。
 */
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
    /* 初始化失败也保留已创建对象，避免迟到回调访问已释放存储。 */
    OS_ENTER_CRITICAL();
    camera_preview_state_init(&s_camera.policy, result);
    OS_EXIT_CRITICAL();
    LOGI(TAG, "control init=%d; sensor remains closed", result);
    return result;
}

/**
 * @brief 更新请求意图并通知工作任务；完整合同见 camera_preview.h。
 */
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

/**
 * @brief 撤销会话并丢弃无人读取帧；完整合同见 camera_preview.h。
 */
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

/**
 * @brief 复制当前会话元数据；完整合同见 camera_preview.h。
 */
void camera_preview_get_snapshot(camera_preview_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }
    OS_ENTER_CRITICAL();
    camera_preview_state_snapshot(&s_camera.policy, timer_get_ms(), snapshot);
    OS_EXIT_CRITICAL();
}

/**
 * @brief 领取读占用并复制新帧；完整合同见 camera_preview.h。
 */
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

/**
 * @brief 限时等待清理完成；完整合同见 camera_preview.h。
 */
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

/**
 * @brief 禁止启动并请求关机停止；完整合同见 camera_preview.h。
 */
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

/**
 * @brief 撤销关机启动禁止；完整合同见 camera_preview.h。
 */
void camera_preview_shutdown_abort(void)
{
    OS_ENTER_CRITICAL();
    s_camera.policy.status.is_shutdown_blocked = 0U;
    s_camera.policy.status.revision++;
    OS_EXIT_CRITICAL();
}

#endif /* D_CAMERA_PREVIEW_ENABLE */
