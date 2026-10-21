/**
 * @file power_ctrl.c
 * @brief Power on/off management — 5-second long-press soft shutdown with KEY_EVENT_UP trigger
 * @author D_Code
 * @date 2026-06-04
 */

#include "config/app_config.h"

#if D_FEATURE_POWER_CONTROL

#include "power_ctrl.h"
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

#ifndef D_POWER_CTRL_KEY
#define D_POWER_CTRL_KEY D_KEY_PTT
#endif

#ifndef D_POWER_CTRL_LONG_PRESS_MS
#define D_POWER_CTRL_LONG_PRESS_MS 5000U
#endif

/* KEY_EVENT_LONG fires after an SDK-internal delay from the actual key-down moment.
 * This offset compensates so total perceived long-press from key-down
 * approximates D_POWER_CTRL_LONG_PRESS_MS. */
#ifndef D_POWER_CTRL_LONG_START_OFFSET_MS
#define D_POWER_CTRL_LONG_START_OFFSET_MS 1000U
#endif

#ifndef D_POWER_CTRL_DEFAULT_MODE
#define D_POWER_CTRL_DEFAULT_MODE POWER_CTRL_MODE_SOFT
#endif

extern void power_set_soft_poweroff(u32 ms); /* SDK function: no standalone header available */

static int power_ctrl_key_handler(uint8_t key_value, uint8_t key_action, void *user_data);
static int power_ctrl_handle_key_action(uint8_t key_action);
static int power_ctrl_handle_key_up(void);
static int power_ctrl_handle_key_hold(uint8_t key_action);

typedef struct {
    uint8_t is_initialized;
    uint8_t is_press_active;
    uint8_t is_shutdown_pending;
    uint8_t is_shutdown_running;
    uint32_t press_start_ms;
    power_ctrl_prepare_shutdown_cb_t prepare_shutdown_cb;
    void *prepare_shutdown_user_data;
    power_ctrl_shutdown_backend_cb_t hard_shutdown_cb;
    void *hard_shutdown_user_data;
} power_ctrl_ctx_t;

static power_ctrl_ctx_t s_power_ctrl_ctx;

uint8_t power_ctrl_is_shutting_down(void)
{
    uint8_t is_running;

    OS_ENTER_CRITICAL();
    is_running = s_power_ctrl_ctx.is_shutdown_running;
    OS_EXIT_CRITICAL();
    return is_running;
}

static uint32_t power_ctrl_now_ms(void)
{
    return (uint32_t)timer_get_ms();
}

static void power_ctrl_reset_key_tracking(void)
{
    s_power_ctrl_ctx.is_press_active = 0U;
    s_power_ctrl_ctx.is_shutdown_pending = 0U;
    s_power_ctrl_ctx.press_start_ms = 0U;
}

static uint32_t power_ctrl_get_elapsed_ms(uint32_t now_ms)
{
    uint32_t elapsed_ms;

    if (s_power_ctrl_ctx.press_start_ms == 0U) {
        return 0U;
    }

    elapsed_ms = now_ms - s_power_ctrl_ctx.press_start_ms;
    if (elapsed_ms > (UINT32_MAX - D_POWER_CTRL_LONG_START_OFFSET_MS)) {
        return UINT32_MAX;
    }

    return elapsed_ms + D_POWER_CTRL_LONG_START_OFFSET_MS;
}

static d_err_t power_ctrl_prepare_shutdown(void)
{
    d_err_t err;

    if (s_power_ctrl_ctx.prepare_shutdown_cb == NULL) {
        return D_OK;
    }

    err = s_power_ctrl_ctx.prepare_shutdown_cb(s_power_ctrl_ctx.prepare_shutdown_user_data);
    /* D_ERR_NOT_READY means the service is inactive; allow shutdown to proceed */
    if ((err == D_OK) || (err == D_ERR_NOT_READY)) {
        return D_OK;
    }

    return err;
}

static d_err_t power_ctrl_do_soft_shutdown(void)
{
    d_board_prepare_soft_poweroff_wakeup();
    power_set_soft_poweroff(0);
    return D_OK;
}

static int power_ctrl_key_handler(uint8_t key_value, uint8_t key_action, void *user_data)
{
    (void)user_data;

    if (key_value != D_POWER_CTRL_KEY) {
        return 0;
    }

    return power_ctrl_handle_key_action(key_action);
}

d_err_t power_ctrl_init(void)
{
    d_err_t err;

    if (s_power_ctrl_ctx.is_initialized != 0U) {
        power_ctrl_deinit();
    }
    memset(&s_power_ctrl_ctx, 0, sizeof(s_power_ctrl_ctx));
    err = d_key_register(D_POWER_CTRL_KEY, power_ctrl_key_handler, NULL);
    if (err != D_OK) {
        LOGE(TAG, "key register failed: key=%u err=%d", (unsigned)D_POWER_CTRL_KEY, err);
        return err;
    }
    s_power_ctrl_ctx.is_initialized = 1U;
    LOGI(TAG, "init done: key=%u long_ms=%u mode=%u",
         (unsigned)D_POWER_CTRL_KEY,
         (unsigned)D_POWER_CTRL_LONG_PRESS_MS,
         (unsigned)D_POWER_CTRL_DEFAULT_MODE);
    return D_OK;
}

void power_ctrl_deinit(void)
{
    if (s_power_ctrl_ctx.is_initialized != 0U) {
        (void)d_key_unregister(D_POWER_CTRL_KEY);
    }
    memset(&s_power_ctrl_ctx, 0, sizeof(s_power_ctrl_ctx));
}

void power_ctrl_set_prepare_shutdown_callback(power_ctrl_prepare_shutdown_cb_t callback, void *user_data)
{
    s_power_ctrl_ctx.prepare_shutdown_cb = callback;
    s_power_ctrl_ctx.prepare_shutdown_user_data = user_data;
}

void power_ctrl_set_hard_shutdown_callback(power_ctrl_shutdown_backend_cb_t callback, void *user_data)
{
    s_power_ctrl_ctx.hard_shutdown_cb = callback;
    s_power_ctrl_ctx.hard_shutdown_user_data = user_data;
}

static int power_ctrl_handle_key_up(void)
{
    uint32_t now_ms;
    uint32_t elapsed_ms;

    if (s_power_ctrl_ctx.is_press_active == 0U) {
        power_ctrl_reset_key_tracking();
        return 0;
    }
    if (s_power_ctrl_ctx.is_shutdown_pending != 0U) {
        LOGI(TAG, "shutdown key released: executing soft poweroff");
        power_ctrl_reset_key_tracking();
        (void)power_ctrl_request_default_shutdown();
        return 1;
    }
    now_ms = power_ctrl_now_ms();
    elapsed_ms = power_ctrl_get_elapsed_ms(now_ms);
    if (elapsed_ms >= D_POWER_CTRL_LONG_PRESS_MS) {
        LOGI(TAG, "shutdown key release matched: elapsed=%u", (unsigned)elapsed_ms);
        power_ctrl_reset_key_tracking();
        (void)power_ctrl_request_default_shutdown();
        return 1;
    }
    power_ctrl_reset_key_tracking();
    return 0;
}

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
    LOGI(TAG, "shutdown pending: action=%u elapsed=%u release to confirm",
         (unsigned)key_action, (unsigned)elapsed_ms);
    return 1;
}

static int power_ctrl_handle_key_action(uint8_t key_action)
{
    if (s_power_ctrl_ctx.is_initialized == 0U) {
        return 0;
    }
    if ((key_action == KEY_EVENT_CLICK) ||
        (key_action == KEY_EVENT_TRIPLE_CLICK)) {
        power_ctrl_reset_key_tracking();
        return 0;
    }
    if (key_action == KEY_EVENT_UP) {
        return power_ctrl_handle_key_up();
    }
    if ((key_action != KEY_EVENT_LONG) && (key_action != KEY_EVENT_HOLD)) {
        return 0;
    }
    return power_ctrl_handle_key_hold(key_action);
}

d_err_t power_ctrl_request_shutdown(power_ctrl_mode_e mode)
{
    d_err_t err;
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
    if (mode == POWER_CTRL_MODE_HARD && s_power_ctrl_ctx.hard_shutdown_cb == NULL) {
        return D_ERR_NOT_READY;
    }
#if D_FEATURE_OTA
    if (ota_service_shutdown_begin() != D_OK) {
        LOGW(TAG, "升级处理中，暂不能关机");
        return D_ERR_BUSY;
    }
#endif
    OS_ENTER_CRITICAL();
    if (s_power_ctrl_ctx.is_shutdown_running) {
        OS_EXIT_CRITICAL();
#if D_FEATURE_OTA
        ota_service_shutdown_cancel();
#endif
        return D_ERR_BUSY;
    }
    s_power_ctrl_ctx.is_shutdown_running = 1U;
    OS_EXIT_CRITICAL();

#if POWER_CTRL_HAS_APPEARANCE
    power_ctrl_pomodoro_begin();
    power_ctrl_appearance_begin();
#endif
#if D_FEATURE_QWEN || D_LOCAL_PROMPT_ENABLE
    d_device_volume_quiesce_begin();
#endif
#if D_CAMERA_PREVIEW_ENABLE
    /* Finish camera first: Qwen cleanup can become startable again on return. */
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

    err = s_power_ctrl_ctx.hard_shutdown_cb(s_power_ctrl_ctx.hard_shutdown_user_data);
    if (err == D_OK) {
        return D_OK;
    }

abort_shutdown:
#if D_FEATURE_OTA
    ota_service_shutdown_cancel();
#endif
#if D_FEATURE_QWEN || D_LOCAL_PROMPT_ENABLE
    /* Reopen the volume worker before reminders may request an alert lease. */
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

d_err_t power_ctrl_request_default_shutdown(void)
{
    return power_ctrl_request_shutdown((power_ctrl_mode_e)D_POWER_CTRL_DEFAULT_MODE);
}

#endif
