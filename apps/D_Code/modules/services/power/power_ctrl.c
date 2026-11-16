/**
 * @file power_ctrl.c
 * @brief 以长按期限及抬起确认编排软关机和受检资源收尾。
 * @author D_Code
 * @date 2026-06-04
 */

#include "config/app_config.h"

#if D_FEATURE_POWER_CONTROL

#include "power_ctrl.h"
#if D_FEATURE_WEBSOCKET
#include "modules/services/websocket/ws_service.h"
#endif
#if D_FEATURE_OTA
#include "modules/services/ota/ota_service.h"
#endif
#include "board/wl82/d_board.h"
#include "modules/services/key/d_key_service.h"
#include "framework/d_log.h"
#include "event/key_event.h"
#include "system/includes.h"
#if defined(D_LVGL_ENABLE) && defined(D_UI_TARGET) && defined(D_UI_PRODUCT) && (D_UI_TARGET == D_UI_PRODUCT)
#include "power_ctrl_appearance.h"
#include "power_ctrl_pomodoro.h"
#define POWER_CTRL_HAS_APPEARANCE 1
#define POWER_CTRL_APPEARANCE_TIMEOUT_MS 2000U
#else
#define POWER_CTRL_HAS_APPEARANCE 0
#endif
#if D_FEATURE_QWEN || D_LOCAL_PROMPT_ENABLE
#include "modules/services/audio/d_device_volume.h"
#endif
#if D_CAMERA_PREVIEW_ENABLE
#include "modules/services/camera/camera_preview.h"
#define POWER_CTRL_CAMERA_STOP_TIMEOUT_MS 3000U
#endif

#include <string.h>

#define TAG "PWR"

#ifndef D_POWER_CTRL_LONG_PRESS_MS
#define D_POWER_CTRL_LONG_PRESS_MS 5000U
#endif

/* KEY_EVENT_LONG 在实际按下后经过 SDK 内部延迟才触发；
 * 用该偏移补偿，使用户从按下起感知的总长按时间
 * 接近 D_POWER_CTRL_LONG_PRESS_MS。 */
#ifndef D_POWER_CTRL_LONG_START_OFFSET_MS
#define D_POWER_CTRL_LONG_START_OFFSET_MS 1000U
#endif

#ifndef D_POWER_CTRL_DEFAULT_MODE
#define D_POWER_CTRL_DEFAULT_MODE POWER_CTRL_MODE_SOFT
#endif

extern void power_set_soft_poweroff(u32 ms); /* SDK 前向声明：当前没有独立可包含头文件。 */

static void power_ctrl_reset_key_tracking(void);
static int power_ctrl_handle_key_up(void);
static int power_ctrl_handle_key_hold(uint8_t key_action);

typedef struct {
    uint8_t is_initialized;
    uint8_t is_press_active;
    uint8_t is_shutdown_pending;
    uint8_t is_shutdown_running;
    uint8_t is_key_request_active;
    uint32_t press_start_ms;
    power_ctrl_prepare_shutdown_cb_t prepare_shutdown_cb;
    void *prepare_shutdown_user_data;
    power_ctrl_shutdown_backend_cb_t hard_shutdown_cb;
    void *hard_shutdown_user_data;
} power_ctrl_ctx_t;

static power_ctrl_ctx_t s_power_ctrl_ctx;
/* 跨服务反初始化与重初始化保留，直到旧物理序列终止。 */
static uint8_t s_key_cancelled_until_up;
static const power_ctrl_prepare_ops_t *s_prepare_ops;


/**
 * @brief 在服务启动前绑定唯一产品关机准备所有者；完整合同见 power_ctrl.h。
 */
d_err_t power_ctrl_bind_prepare(const power_ctrl_prepare_ops_t *ops)
{
    if (ops == NULL || ops->begin == NULL || ops->wait == NULL || ops->cancel == NULL)
        return D_ERR_INVALID_ARG;
    if (s_prepare_ops != NULL && s_prepare_ops != ops) return D_ERR_BUSY;
    if (s_power_ctrl_ctx.prepare_shutdown_cb != NULL) return D_ERR_BUSY;
    s_prepare_ops = ops;
    return D_OK;
}

/**
 * @brief 读取关机准备或执行是否在途；完整合同见 power_ctrl.h。
 */
uint8_t power_ctrl_is_shutting_down(void)
{
    uint8_t is_running;

    OS_ENTER_CRITICAL();
    is_running = s_power_ctrl_ctx.is_shutdown_running;
    OS_EXIT_CRITICAL();
    return is_running;
}

/**
 * @brief 读取设备单调毫秒计时。
 * @return 当前毫秒计时值，按 uint32_t 环形解释。
 */
static uint32_t power_ctrl_now_ms(void)
{
    return (uint32_t)timer_get_ms();
}

/**
 * @brief 清除本次物理按键序列的活动与关机待确认状态。
 * @note 调用者遵守现有按键状态序列化；独立取消锁存不在此清除。
 */
static void power_ctrl_reset_key_tracking(void)
{
    s_power_ctrl_ctx.is_press_active = 0U;
    s_power_ctrl_ctx.is_shutdown_pending = 0U;
    s_power_ctrl_ctx.press_start_ms = 0U;
}

/**
 * @brief 读取长按已持续时间，并补偿 SDK LONG 起始延迟。
 * @param[in] now_ms 当前毫秒计时值。
 * @return 无活动按键返回 0；其余为补偿后的毫秒数，溢出时饱和到 UINT32_MAX。
 */
static uint32_t power_ctrl_get_elapsed_ms(uint32_t now_ms)
{
    uint32_t elapsed_ms;

    /* 时钟回绕后的零值仍有效，使用显式按键活动标记判定起点有效性。 */
    if (s_power_ctrl_ctx.is_press_active == 0U) {
        return 0U;
    }

    elapsed_ms = now_ms - s_power_ctrl_ctx.press_start_ms;
    if (elapsed_ms > (UINT32_MAX - D_POWER_CTRL_LONG_START_OFFSET_MS)) {
        return UINT32_MAX;
    }

    return elapsed_ms + D_POWER_CTRL_LONG_START_OFFSET_MS;
}

/**
 * @brief 依次等待辅助及产品准备，或调用兼容准备回调。
 * @return 准备允许继续返回 D_OK；其余返回实际错误。
 * @note 兼容回调的 D_ERR_NOT_READY 视为服务未活动并允许继续；回调在临界区外执行。
 */
static d_err_t power_ctrl_prepare_shutdown(void)
{
    d_err_t err;
    power_ctrl_prepare_shutdown_cb_t callback;
    void *user_data;

    if (s_prepare_ops != NULL) return s_prepare_ops->wait(10000U);

    OS_ENTER_CRITICAL();
    callback = s_power_ctrl_ctx.prepare_shutdown_cb;
    user_data = s_power_ctrl_ctx.prepare_shutdown_user_data;
    OS_EXIT_CRITICAL();
    if (callback == NULL) {
        return D_OK;
    }

    err = callback(user_data);
    /* D_ERR_NOT_READY 表示兼容服务未活动，允许继续关机。 */
    if ((err == D_OK) || (err == D_ERR_NOT_READY)) {
        return D_OK;
    }

    return err;
}

/**
 * @brief 设置板级软关机唤醒条件并提交 SDK 软关机。
 * @return SDK 调用返回后固定返回 D_OK。
 */
static d_err_t power_ctrl_do_soft_shutdown(void)
{
    d_board_prepare_soft_poweroff_wakeup();
    power_set_soft_poweroff(0);
    return D_OK;
}

/**
 * @brief 初始化电源状态，产品按键绑定由独立装配负责；完整合同见 power_ctrl.h。
 */
d_err_t power_ctrl_init(void)
{
    power_ctrl_deinit();
    OS_ENTER_CRITICAL();
    if (s_power_ctrl_ctx.is_initialized) {
        OS_EXIT_CRITICAL();
        return D_ERR_BUSY;
    }
    memset(&s_power_ctrl_ctx, 0, sizeof(s_power_ctrl_ctx));
    s_power_ctrl_ctx.is_initialized = 1U;
    OS_EXIT_CRITICAL();
    LOGI(TAG, "init done: long_ms=%u mode=%u",
         (unsigned)D_POWER_CTRL_LONG_PRESS_MS,
         (unsigned)D_POWER_CTRL_DEFAULT_MODE);
    return D_OK;
}

/**
 * @brief 仅在没有在途按键请求或关机时清空电源状态；完整合同见 power_ctrl.h。
 */
void power_ctrl_deinit(void)
{
    OS_ENTER_CRITICAL();
    if (s_power_ctrl_ctx.is_key_request_active || s_power_ctrl_ctx.is_shutdown_running) {
        OS_EXIT_CRITICAL();
        LOGW(TAG, "deinit waits for shutdown/key request; retry later");
        return;
    }
    if (s_power_ctrl_ctx.is_press_active) s_key_cancelled_until_up = 1U;
    memset(&s_power_ctrl_ctx, 0, sizeof(s_power_ctrl_ctx));
    OS_EXIT_CRITICAL();
}

/**
 * @brief 注册兼容的关机前回调与上下文对；完整合同见 power_ctrl.h。
 */
void power_ctrl_set_prepare_shutdown_callback(power_ctrl_prepare_shutdown_cb_t callback, void *user_data)
{
    OS_ENTER_CRITICAL();
    if (s_prepare_ops != NULL) {
        OS_EXIT_CRITICAL();
        return;
    }
    s_power_ctrl_ctx.prepare_shutdown_cb = callback;
    s_power_ctrl_ctx.prepare_shutdown_user_data = user_data;
    OS_EXIT_CRITICAL();
}

/**
 * @brief 注册硬关机后端及其上下文；完整合同见 power_ctrl.h。
 */
void power_ctrl_set_hard_shutdown_callback(power_ctrl_shutdown_backend_cb_t callback, void *user_data)
{
    OS_ENTER_CRITICAL();
    s_power_ctrl_ctx.hard_shutdown_cb = callback;
    s_power_ctrl_ctx.hard_shutdown_user_data = user_data;
    OS_EXIT_CRITICAL();
}

/**
 * @brief 在抬起时确认本序列是否达到关机长按条件，并重置跟踪。
 * @return 需要请求关机返回 1，否则返回 0。
 * @note 由已持电源状态临界区的按键分发调用。
 */
static int power_ctrl_handle_key_up(void)
{
    uint32_t now_ms;
    uint32_t elapsed_ms;

    if (s_power_ctrl_ctx.is_press_active == 0U) {
        power_ctrl_reset_key_tracking();
        return 0;
    }
    if (s_power_ctrl_ctx.is_shutdown_pending != 0U) {
        power_ctrl_reset_key_tracking();
        return 1;
    }
    now_ms = power_ctrl_now_ms();
    elapsed_ms = power_ctrl_get_elapsed_ms(now_ms);
    if (elapsed_ms >= D_POWER_CTRL_LONG_PRESS_MS) {
        power_ctrl_reset_key_tracking();
        return 1;
    }
    power_ctrl_reset_key_tracking();
    return 0;
}

/**
 * @brief 建立长按起点并在达到期限后锁存待抬起确认。
 * @param[in] key_action SDK 长按或保持动作；当前不参与分支计算。
 * @return 达到期限或已待确认返回 1，否则返回 0。
 * @note 由已持电源状态临界区的按键分发调用，不在此执行关机。
 */
static int power_ctrl_handle_key_hold(uint8_t key_action)
{
    uint32_t now_ms;
    uint32_t elapsed_ms;

    now_ms = power_ctrl_now_ms();
    if (s_power_ctrl_ctx.is_press_active == 0U) {
        s_power_ctrl_ctx.is_press_active = 1U;
        s_power_ctrl_ctx.is_shutdown_pending = 0U;
        s_power_ctrl_ctx.press_start_ms = now_ms;
    }
    if (s_power_ctrl_ctx.is_shutdown_pending != 0U) {
        return 1;
    }
    elapsed_ms = power_ctrl_get_elapsed_ms(now_ms);
    if (elapsed_ms < D_POWER_CTRL_LONG_PRESS_MS) {
        return 0;
    }
    s_power_ctrl_ctx.is_shutdown_pending = 1U;
    (void)key_action;
    return 1;
}

/**
 * @brief 处理产品映射后的 SDK 长按、保持及抬起动作；完整合同见 power_ctrl.h。
 */
int power_ctrl_handle_key_action(uint8_t key_action)
{
    int result = D_KEY_UNHANDLED;
    uint8_t request_shutdown = 0U;
    uint8_t became_pending = 0U;

    OS_ENTER_CRITICAL();
    if (s_power_ctrl_ctx.is_key_request_active) {
        if (key_action == KEY_EVENT_LONG || key_action == KEY_EVENT_HOLD ||
            key_action == KEY_EVENT_UP) result = D_KEY_REJECTED;
        goto finish;
    }
    if ((key_action == KEY_EVENT_CLICK) ||
        (key_action == KEY_EVENT_TRIPLE_CLICK)) {
        power_ctrl_reset_key_tracking();
        if (s_key_cancelled_until_up) {
            s_key_cancelled_until_up = 0U;
            result = D_KEY_REJECTED;
        }
        goto finish;
    }
    if (key_action != KEY_EVENT_LONG && key_action != KEY_EVENT_HOLD &&
        key_action != KEY_EVENT_UP) goto finish;
    if (!s_power_ctrl_ctx.is_initialized) {
        s_key_cancelled_until_up = key_action != KEY_EVENT_UP;
        result = D_KEY_REJECTED;
        goto finish;
    }
    if (s_key_cancelled_until_up) {
        power_ctrl_reset_key_tracking();
        if (key_action == KEY_EVENT_UP) s_key_cancelled_until_up = 0U;
        result = D_KEY_REJECTED;
        goto finish;
    }
    if (s_power_ctrl_ctx.is_shutdown_running
        ) {
        power_ctrl_reset_key_tracking();
        if (key_action != KEY_EVENT_UP) s_key_cancelled_until_up = 1U;
        result = D_KEY_REJECTED;
        goto finish;
    }
    if (key_action == KEY_EVENT_UP) {
        result = power_ctrl_handle_key_up();
        if (result == D_KEY_CONSUMED) {
            s_power_ctrl_ctx.is_key_request_active = 1U;
            request_shutdown = 1U;
        }
    } else {
        const uint8_t was_pending = s_power_ctrl_ctx.is_shutdown_pending;
        result = power_ctrl_handle_key_hold(key_action);
        became_pending = !was_pending && s_power_ctrl_ctx.is_shutdown_pending;
    }
finish:
    OS_EXIT_CRITICAL();
    if (became_pending) LOGI(TAG, "shutdown pending: release to confirm");
    if (request_shutdown) {
        /* 短暂输入占用排除维护，直到真实所有者作出原子准入决定；
         * 不在 IRQ 临界区内等待。 */
        LOGI(TAG, "shutdown key released: requesting soft poweroff");
        (void)power_ctrl_request_default_shutdown();
        OS_ENTER_CRITICAL();
        s_power_ctrl_ctx.is_key_request_active = 0U;
        OS_EXIT_CRITICAL();
    }
    return result;
}

/**
 * @brief 按模式同步执行准入、服务收尾和关机后端；完整合同见 power_ctrl.h。
 */
d_err_t power_ctrl_request_shutdown(power_ctrl_mode_e mode)
{
    d_err_t err;
    power_ctrl_shutdown_backend_cb_t hard_callback;
    void *hard_user_data;
#if POWER_CTRL_HAS_APPEARANCE
    uint32_t appearance_started_ms;
#endif
#if D_FEATURE_QWEN || D_LOCAL_PROMPT_ENABLE
    uint32_t volume_started_ms;
#endif
#if D_CAMERA_PREVIEW_ENABLE
    uint32_t camera_started_ms;
#endif

    if (s_power_ctrl_ctx.is_initialized == 0U) {
        return D_ERR_NOT_READY;
    }
    if (mode != POWER_CTRL_MODE_SOFT && mode != POWER_CTRL_MODE_HARD) {
        return D_ERR_INVALID_ARG;
    }
    if (mode == POWER_CTRL_MODE_HARD) {
        OS_ENTER_CRITICAL();
        hard_callback = s_power_ctrl_ctx.hard_shutdown_cb;
        OS_EXIT_CRITICAL();
        if (hard_callback == NULL) return D_ERR_NOT_READY;
    }
#if D_FEATURE_OTA
    if (ota_service_shutdown_begin() != D_OK) {
        LOGW(TAG, "升级处理中，暂不能关机");
        return D_ERR_BUSY;
    }
#endif
    OS_ENTER_CRITICAL();
    if (s_power_ctrl_ctx.is_shutdown_running
        ) {
        OS_EXIT_CRITICAL();
#if D_FEATURE_OTA
        ota_service_shutdown_cancel();
#endif
        return D_ERR_BUSY;
    }
    s_power_ctrl_ctx.is_shutdown_running = 1U;
    OS_EXIT_CRITICAL();

    if (s_prepare_ops != NULL) {
        err = s_prepare_ops->begin();
        if (err != D_OK) goto abort_shutdown;
    }

#if D_FEATURE_WEBSOCKET
    {
        uint8_t ready = 0U;
        uint32_t started = power_ctrl_now_ms();
        err = ws_service_maintenance_begin(WS_HOLD_POWER);
        if (err != D_OK) goto abort_shutdown;
        while (!ready) {
            err = ws_service_maintenance_poll(&ready);
            if (err != D_OK) goto abort_shutdown;
            if (ready) break;
            if ((uint32_t)(power_ctrl_now_ms() - started) >= 10000U) {
                err = D_ERR_TIMEOUT; goto abort_shutdown;
            }
            os_time_dly(1);
        }
    }
#endif

#if POWER_CTRL_HAS_APPEARANCE
    power_ctrl_pomodoro_begin();
    power_ctrl_appearance_begin();
#endif
#if D_FEATURE_QWEN || D_LOCAL_PROMPT_ENABLE
    d_device_volume_quiesce_begin();
#endif
#if D_CAMERA_PREVIEW_ENABLE
    /* 先完成相机停止；Qwen 清理返回后可能重新允许启动。 */
    camera_started_ms = power_ctrl_now_ms();
    camera_preview_shutdown_begin();
    err = camera_preview_wait_stopped(POWER_CTRL_CAMERA_STOP_TIMEOUT_MS);
    LOGI(TAG, "camera shutdown preparation: err=%d elapsed_ms=%u", err,
         (unsigned)(power_ctrl_now_ms() - camera_started_ms));
    if (err != D_OK) {
        goto abort_shutdown;
    }
#endif

#if D_FEATURE_QWEN || D_LOCAL_PROMPT_ENABLE
    volume_started_ms = power_ctrl_now_ms();
    err = d_device_volume_wait_idle(D_DEVICE_VOLUME_TIMEOUT_MS);
    LOGI(TAG, "volume shutdown preparation: err=%d elapsed_ms=%u", err,
         (unsigned)(power_ctrl_now_ms() - volume_started_ms));
    if (err != D_OK) goto abort_shutdown;
#endif
#if POWER_CTRL_HAS_APPEARANCE
    err = power_ctrl_pomodoro_wait_idle(POWER_CTRL_APPEARANCE_TIMEOUT_MS);
    if (err != D_OK) goto abort_shutdown;
    appearance_started_ms = power_ctrl_now_ms();
    err = power_ctrl_appearance_wait_idle(POWER_CTRL_APPEARANCE_TIMEOUT_MS);
    LOGI(TAG, "appearance shutdown preparation: err=%d elapsed_ms=%u", err,
         (unsigned)(power_ctrl_now_ms() - appearance_started_ms));
    if (err != D_OK) goto abort_shutdown;
#endif
    err = power_ctrl_prepare_shutdown();
    if (err != D_OK) {
        LOGW(TAG, "prepare shutdown failed: mode=%u err=%d", (unsigned)mode, err);
        goto abort_shutdown;
    }

    LOGI(TAG, "shutdown request: mode=%u", (unsigned)mode);
    if (mode == POWER_CTRL_MODE_SOFT) {
        return power_ctrl_do_soft_shutdown();
    }

    /* 准备期间可能替换或清除后端，此时才成对选择回调与上下文。 */
    OS_ENTER_CRITICAL();
    hard_callback = s_power_ctrl_ctx.hard_shutdown_cb;
    hard_user_data = s_power_ctrl_ctx.hard_shutdown_user_data;
    OS_EXIT_CRITICAL();
    if (hard_callback == NULL) {
        err = D_ERR_NOT_READY;
        goto abort_shutdown;
    }
    err = hard_callback(hard_user_data);
    if (err == D_OK) {
        return D_OK;
    }

abort_shutdown:
    if (s_prepare_ops != NULL) {
        d_err_t cancel_result = s_prepare_ops->cancel();
        if (cancel_result != D_OK) {
            LOGE(TAG, "shutdown cancellation request failed: %d", cancel_result);
            err = cancel_result;
        }
    }
#if D_FEATURE_WEBSOCKET
    /* 取消后的清理在现有工作任务中继续，不另建定时器或任务。 */
    ws_service_maintenance_cancel(WS_HOLD_POWER);
#endif
#if D_FEATURE_OTA
    ota_service_shutdown_cancel();
#endif
#if D_FEATURE_QWEN || D_LOCAL_PROMPT_ENABLE
    /* 在提醒可能重新申请闹铃占用前，先解除音量工作任务的准入限制。 */
    d_device_volume_quiesce_cancel();
#endif
#if POWER_CTRL_HAS_APPEARANCE
    power_ctrl_pomodoro_cancel();
    power_ctrl_appearance_cancel();
#endif
#if D_CAMERA_PREVIEW_ENABLE
    camera_preview_shutdown_abort();
#endif
    power_ctrl_reset_key_tracking();
    OS_ENTER_CRITICAL();
    s_power_ctrl_ctx.is_shutdown_running = 0U;
    OS_EXIT_CRITICAL();
    return err;
}

/**
 * @brief 使用编译配置的默认模式执行关机；完整合同见 power_ctrl.h。
 */
d_err_t power_ctrl_request_default_shutdown(void)
{
    return power_ctrl_request_shutdown((power_ctrl_mode_e)D_POWER_CTRL_DEFAULT_MODE);
}

#endif
