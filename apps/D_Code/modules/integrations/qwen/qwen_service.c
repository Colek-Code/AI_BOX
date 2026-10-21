#include "config/app_config.h"
#if D_LOCAL_PROMPT_ENABLE
#include "modules/services/audio/d_local_prompt.h"
#endif

#if D_FEATURE_QWEN

#include "qwen_service.h"
#include "qwen_net.h"
#include "qwen_wss.h"
#include "qwen_audio.h"
#include "qwen_cmd.h"
#include "modules/services/audio/d_device_volume.h"
#include "modules/services/wifi/wifi_service.h"
#include "framework/d_crypto.h"
#include "framework/d_event_bus.h"
#include "modules/services/key/d_key_service.h"
#include "framework/d_log.h"
#if D_FEATURE_XIAOLE_WAKEUP
#include "modules/integrations/xiaole_wakeup/xiaole_wakeup.h"
#endif
#if D_FEATURE_POWER_CONTROL
#include "modules/services/power/power_ctrl.h"
#endif
#if (D_FEATURE_BLE_PROVISION) && (D_QWEN_DIALOG_MODE == D_QWEN_DIALOG_MODE_DUPLEX)
#include "modules/services/ble_provisioning/ble_service.h"
#include "modules/services/wifi/wifi_service.h"
#endif
#include "event/key_event.h"
#include "os/os_api.h"
#include "system/spinlock.h"
#include "sys_common.h"

#include <stdint.h>
#include <string.h>

#ifndef CONFIG_PLATFORM_AC7911
#define CONFIG_PLATFORM_AC7911 1
#endif

#include "sdk/include/c_mmi.h"

#if D_QWEN_HAL_ACCEPT_TEST_ENABLE
#include "sdk/include/qwen_test.h"
#endif

#define TAG "QWEN"
/* UI, prompt and SDK callers share this policy; all IO stays outside it. */
static DEFINE_SPINLOCK(s_qwen_session_lock);
#define QWEN_SERVICE_TASK_PRIO 10
#define QWEN_SERVICE_TASK_STACK (8U * 1024U)
#define QWEN_TEXT_EVENT_MAX_LEN D_EVENT_DATA_MAX
#define QWEN_TEXT_LOG_PREVIEW_LEN 96U
#define QWEN_WSS_RECONNECT_DELAY_MS 3000U
#define QWEN_WSS_RECONNECT_DELAY_MAX_MS 12000U
#define QWEN_MS_PER_SECOND 1000U
#define QWEN_SERVICE_WORK_POLL_MS 500U
#define QWEN_SERVICE_AUTH_POLL_MS 5000U
#define QWEN_SESSION_HANDOFF_MS 2000U
#define QWEN_SESSION_SHUTDOWN_MS 10000U
#define QWEN_DIALOG_END_TIMEOUT_MS 6000U
#define QWEN_DIALOG_RESPONSE_TIMEOUT_MS 15000U
#define QWEN_VOLUME_BACKEND_RELEASE_RETRIES 10U
#define QWEN_VOLUME_BACKEND_RELEASE_DELAY_TICKS 1
#if (D_QWEN_DS_AUDIO_FORMAT == D_QWEN_AUDIO_FORMAT_RAW_OPUS)
#define QWEN_DIALOG_LOCAL_DRAIN_TIMEOUT_MS 300000U
#else
#define QWEN_DIALOG_LOCAL_DRAIN_TIMEOUT_MS 90000U
#endif
#define QWEN_CMD_CHANGE_TEXT "change"

#ifndef D_QWEN_PTT_KEY_VALUE
#define D_QWEN_PTT_KEY_VALUE D_KEY_PTT
#endif

#ifndef D_QWEN_READY_CUE_TEXT
#define D_QWEN_READY_CUE_TEXT "你好，我来了"
#endif

#ifndef D_QWEN_IDLE_GOODBYE_TEXT
#define D_QWEN_IDLE_GOODBYE_TEXT "我退下了"
#endif

#ifndef D_QWEN_IDLE_TIMEOUT_MS
#define D_QWEN_IDLE_TIMEOUT_MS 0U
#endif

#ifndef D_QWEN_IDLE_GOODBYE_TIMEOUT_MS
#define D_QWEN_IDLE_GOODBYE_TIMEOUT_MS 8000U
#endif

#ifndef D_QWEN_PLAYER_STALL_TIMEOUT_MS
#define D_QWEN_PLAYER_STALL_TIMEOUT_MS 12000U
#endif

#ifndef D_QWEN_WIFI_RECONFIG_KEY
#define D_QWEN_WIFI_RECONFIG_KEY D_QWEN_PTT_KEY_VALUE
#endif

#ifndef D_QWEN_WIFI_RECONFIG_ACTION
#define D_QWEN_WIFI_RECONFIG_ACTION KEY_EVENT_TRIPLE_CLICK
#endif

static int qwen_ptt_handler(uint8_t key_value, uint8_t key_action, void *user_data);
static d_err_t qwen_service_init_audio(void);
#if (D_QWEN_DIALOG_MODE == D_QWEN_DIALOG_MODE_PUSH2TALK)
static uint8_t qwen_prepare_wakeup_reconnect(void);
#endif
#if D_FEATURE_XIAOLE_WAKEUP
static void qwen_on_xiaole_event(const d_event_t *event, void *user_data);
#endif
#if D_FEATURE_POWER_CONTROL
static d_err_t qwen_service_prepare_shutdown(void *user_data);
#endif

#ifdef D_QWEN_TRACE_ENABLE
#define QWEN_TRACE(fmt, ...) LOGD(TAG, fmt, ##__VA_ARGS__)
#else
#define QWEN_TRACE(fmt, ...) do { } while (0)
#endif

typedef enum {
    QWEN_DIALOG_PHASE_IDLE = 0,
    QWEN_DIALOG_PHASE_PTT_PREPARING,
    QWEN_DIALOG_PHASE_LISTENING,
    QWEN_DIALOG_PHASE_PTT_ENDING,
    QWEN_DIALOG_PHASE_WAIT_ASR,
    QWEN_DIALOG_PHASE_WAIT_LLM,
    QWEN_DIALOG_PHASE_WAIT_TTS,
    QWEN_DIALOG_PHASE_PLAYING,
    QWEN_DIALOG_PHASE_WAIT_LOCAL_TTS_END,
    QWEN_DIALOG_PHASE_FAILED,
} qwen_dialog_phase_e;

typedef enum {
    QWEN_FAILURE_TYPE_UNKNOWN = 0,
    QWEN_FAILURE_TYPE_TIMEOUT,
    QWEN_FAILURE_TYPE_SDK,
    QWEN_FAILURE_TYPE_NETWORK,
    QWEN_FAILURE_TYPE_AUDIO,
    QWEN_FAILURE_TYPE_REQUEST,
} qwen_failure_type_e;

typedef enum {
    QWEN_DISPATCH_EVT_MMI = 0,
    QWEN_DISPATCH_EVT_WIFI,
    QWEN_DISPATCH_EVT_NTP,
    QWEN_DISPATCH_EVT_AUDIO,
    QWEN_DISPATCH_EVT_CMD,
    QWEN_DISPATCH_EVT_KEY,
    QWEN_DISPATCH_EVT_WSS,
} qwen_dispatch_event_e;

typedef struct {
    qwen_state_e state;
    qwen_dialog_phase_e dialog_phase;
    uint8_t is_initialized;
    uint8_t service_ready;
    uint8_t is_wifi_connected;
    uint8_t is_speech_active;
    uint8_t is_ptt_key_down;
    uint8_t is_ptt_prepare_failed;
    uint8_t is_ptt_start_pending;
    uint8_t want_wss_connected;
    uint8_t ready_cue_sent;
    uint8_t ready_cue_pending;
    uint8_t is_text_request_ready;
#if D_QWEN_HAL_ACCEPT_TEST_ENABLE
    uint8_t is_hal_accept_test_done;
#endif
    uint8_t is_cmd_registered;
    uint8_t is_wss_reconnect_pending;
    uint8_t is_mmi_reinit_pending;
    volatile uint8_t mmi_data_initialized;
    uint8_t is_wifi_reconfiguring;
    uint8_t is_idle_goodbye_pending;
    uint8_t worker_exit;
    volatile uint8_t cleanup_active;
    volatile uint8_t preparing_sdk;
    volatile uint8_t worker_prepare_active;
    volatile uint8_t cleanup_retry;
#if D_FEATURE_XIAOLE_WAKEUP
    uint8_t wakeup_start_pending;
    uint32_t wakeup_start_ms;
#endif
    uint32_t microphone_generation;
    uint32_t wifi_revision;
    volatile uint32_t sdk_calls;
#if D_QWEN_HAL_ACCEPT_TEST_ENABLE
    int32_t hal_accept_test_ret;
#endif
    uint32_t dialog_round_id;
    uint32_t dialog_phase_start_ms;
    uint32_t idle_goodbye_start_ms;
    uint32_t last_activity_ms;
    uint32_t wss_reconnect_delay_ms;
    int worker_task_pid;
    OS_SEM work_sem;
    OS_MUTEX state_mutex;
} qwen_context_t;

static qwen_context_t s_qwen_ctx;
static qwen_session_policy_t s_session;
#if D_FEATURE_OTA
static uint8_t s_maintenance_hold;
static uint8_t s_maintenance_saved_auto;
static uint8_t s_maintenance_suspended_auto;
#endif
#if D_LOCAL_PROMPT_ENABLE
static uint32_t s_alarm_hold_token;
static uint8_t s_alarm_suspended_auto;
static uint8_t s_alarm_saved_auto;
#endif
static const char *qwen_state_name(qwen_state_e state);
static void qwen_set_state(qwen_state_e state);
static void qwen_post_error_event(int32_t code);
static int32_t qwen_on_mmi_event(uint32_t event, void *param);
static void qwen_handle_wss_disconnected(void);
static d_err_t qwen_map_sdk_err(int32_t ret);
static void qwen_set_dialog_phase(qwen_dialog_phase_e phase, const char *reason);
static void qwen_schedule_work(const char *reason);
static void qwen_request_cleanup(const char *reason, uint8_t preserve);
static d_err_t qwen_worker_cleanup(void);
static d_err_t qwen_session_start_checked(const uint32_t *expected_generation);

static uint8_t qwen_session_allows_business(void)
{
    uint8_t allowed;
    spin_lock(&s_qwen_session_lock);
    allowed = qwen_session_policy_allows(&s_session);
#if D_FEATURE_OTA
    allowed = allowed && !s_maintenance_hold;
#endif
#if D_LOCAL_PROMPT_ENABLE
    allowed = allowed && !s_alarm_hold_token;
#endif
    spin_unlock(&s_qwen_session_lock);
#if D_LOCAL_PROMPT_ENABLE
    if (allowed && d_local_prompt_alarm_requested()) allowed = 0U;
#endif
    return allowed;
}

static uint8_t qwen_session_cleanup_pending(void)
{
    uint8_t pending;
    spin_lock(&s_qwen_session_lock);
    pending = s_session.cleanup_pending;
    spin_unlock(&s_qwen_session_lock);
    return pending;
}

static uint32_t qwen_session_generation(void)
{
    uint32_t generation;
    spin_lock(&s_qwen_session_lock);
    generation = s_session.generation;
    spin_unlock(&s_qwen_session_lock);
    return generation;
}

/* Public legacy SDK calls hold a lease until their state/audio side effects finish. */
static d_err_t qwen_sdk_enter(void)
{
    d_err_t result = D_OK;
#if D_LOCAL_PROMPT_ENABLE
    if (d_local_prompt_alarm_requested()) return D_ERR_BUSY;
#endif
    spin_lock(&s_qwen_session_lock);
#if D_FEATURE_OTA
    if (s_maintenance_hold) { spin_unlock(&s_qwen_session_lock); return D_ERR_BUSY; }
#endif
#if D_LOCAL_PROMPT_ENABLE
    if (s_alarm_hold_token) { spin_unlock(&s_qwen_session_lock); return D_ERR_BUSY; }
#endif
    if (!s_qwen_ctx.service_ready || !qwen_session_policy_allows(&s_session))
        result = D_ERR_NOT_READY;
    else if (s_qwen_ctx.sdk_calls == 0xFFFFU)
        result = D_ERR_BUSY;
    else
        s_qwen_ctx.sdk_calls++;
    spin_unlock(&s_qwen_session_lock);
    return result;
}

static void qwen_sdk_leave(void)
{
    spin_lock(&s_qwen_session_lock);
    if (s_qwen_ctx.sdk_calls) s_qwen_ctx.sdk_calls--;
    spin_unlock(&s_qwen_session_lock);
}

static d_err_t qwen_wait_sdk_calls(uint32_t timeout_ms)
{
    uint32_t started = timer_get_ms();
    while (s_qwen_ctx.sdk_calls) {
        if ((uint32_t)(timer_get_ms() - started) >= timeout_ms) return D_ERR_TIMEOUT;
        os_time_dly(1);
    }
    return D_OK;
}

static void qwen_ctx_lock(void)
{
    if (os_mutex_valid(&s_qwen_ctx.state_mutex) != 0) {
        os_mutex_pend(&s_qwen_ctx.state_mutex, 0);
    }
}

static void qwen_ctx_unlock(void)
{
    if (os_mutex_valid(&s_qwen_ctx.state_mutex) != 0) {
        os_mutex_post(&s_qwen_ctx.state_mutex);
    }
}

static void qwen_request_cleanup(const char *reason, uint8_t preserve)
{
    spin_lock(&s_qwen_session_lock);
    qwen_session_policy_cleanup(&s_session, preserve);
    s_qwen_ctx.cleanup_retry = 1U;
    if (!preserve) s_qwen_ctx.is_wss_reconnect_pending = 0U;
    spin_unlock(&s_qwen_session_lock);
    qwen_schedule_work(reason);
}

/* Worker-only: local listening must not wait for a cloud Token response. */
static void qwen_restore_idle_wakeup(void)
{
#if D_FEATURE_XIAOLE_WAKEUP && !D_QWEN_HAL_ACCEPT_TEST_ENABLE
    uint8_t can_restore;
    uint32_t generation;
    uint32_t now_ms;
    d_err_t result;
#if D_LOCAL_PROMPT_ENABLE
    if (d_local_prompt_alarm_requested()) return;
#endif

#if D_FEATURE_POWER_CONTROL
    if (power_ctrl_is_shutting_down()) return;
#endif
    if (!qwen_net_is_runtime_allowed()) return;
    spin_lock(&s_qwen_session_lock);
    can_restore = s_qwen_ctx.service_ready && !s_qwen_ctx.worker_exit &&
        s_qwen_ctx.is_wifi_connected && !s_qwen_ctx.is_wifi_reconfiguring &&
        !s_qwen_ctx.cleanup_active && s_session.runtime_allowed &&
        !s_session.requested && !s_session.connected &&
        !s_session.cleanup_pending && !s_session.fatal;
#if D_LOCAL_PROMPT_ENABLE
    if (s_alarm_hold_token) can_restore = 0U;
#endif
#if D_FEATURE_OTA
    if (s_maintenance_hold) can_restore = 0U;
#endif
    generation = s_session.generation;
    spin_unlock(&s_qwen_session_lock);
    if (!can_restore) return;
    if (xiaole_wakeup_is_running()) {
        s_qwen_ctx.wakeup_start_pending = 0U;
        return;
    }
    now_ms = (uint32_t)timer_get_ms();
    /* START is queued: suppress duplicates, but retry failed asynchronous starts. */
    if (s_qwen_ctx.wakeup_start_pending &&
        (uint32_t)(now_ms - s_qwen_ctx.wakeup_start_ms) < QWEN_SERVICE_AUTH_POLL_MS)
        return;
    s_qwen_ctx.wakeup_start_ms = now_ms;
    s_qwen_ctx.wakeup_start_pending = 1U;
    result = xiaole_wakeup_start(generation);
    if (result != D_OK) LOGW(TAG, "standby wakeup start retry: %d", result);
#endif
}

/* Single worker owns all teardown. Never wait while holding state_mutex. */
static d_err_t qwen_worker_cleanup(void)
{
    d_err_t audio_result;
    d_err_t transport_result;
    d_err_t result;
    uint8_t fatal = 0U;

    if (s_session.fatal) return D_ERR_IO;
    s_qwen_ctx.cleanup_active = 1U;
    result = qwen_wait_sdk_calls(QWEN_SESSION_HANDOFF_MS);
    if (result != D_OK) {
        qwen_audio_stop_all();
        spin_lock(&s_qwen_session_lock);
        qwen_session_policy_set_cleanup_result(&s_session, result, 0U);
        s_qwen_ctx.cleanup_active = 0U;
        spin_unlock(&s_qwen_session_lock);
        qwen_set_state(QWEN_STATE_ERROR);
        return result;
    }
    s_qwen_ctx.want_wss_connected = 0U;
    s_qwen_ctx.is_speech_active = 0U;
    /* PTT重连保留原按键请求；完整结束或Duplex模式清除按键状态。 */
    if ((D_QWEN_DIALOG_MODE != D_QWEN_DIALOG_MODE_PUSH2TALK) ||
        !s_session.requested) {
        s_qwen_ctx.is_ptt_key_down = 0U;
        s_qwen_ctx.is_ptt_start_pending = 0U;
    }
    s_qwen_ctx.is_text_request_ready = 0U;
    s_qwen_ctx.ready_cue_pending = 0U;
    s_qwen_ctx.ready_cue_sent = 0U;
    s_qwen_ctx.is_idle_goodbye_pending = 0U;
    s_qwen_ctx.is_mmi_reinit_pending = 0U;
    s_qwen_ctx.microphone_generation = 0U;
    qwen_set_dialog_phase(QWEN_DIALOG_PHASE_IDLE, "session_cleanup");

    audio_result = qwen_audio_stop_all_wait(QWEN_SESSION_HANDOFF_MS);
    transport_result = qwen_wss_disconnect_checked();
    result = audio_result != D_OK ? audio_result : transport_result;
#if D_FEATURE_XIAOLE_WAKEUP
    {
        d_err_t wakeup_result = xiaole_wakeup_stop_wait(QWEN_SESSION_HANDOFF_MS);
        if (result == D_OK) result = wakeup_result;
        if (wakeup_result == D_OK) s_qwen_ctx.wakeup_start_pending = 0U;
    }
#endif
    if (result == D_OK) {
        result = qwen_net_deinit_checked();
        fatal = result != D_OK;
        if (result == D_OK) {
            s_qwen_ctx.is_cmd_registered = 0U;
            s_qwen_ctx.mmi_data_initialized = 0U;
        }
    }
    spin_lock(&s_qwen_session_lock);
    qwen_session_policy_set_cleanup_result(&s_session, result, fatal);
    s_qwen_ctx.cleanup_active = 0U;
    spin_unlock(&s_qwen_session_lock);
    LOGI(TAG, "session cleanup complete=%u result=%d fatal=%u",
         result == D_OK, result, fatal);
    qwen_set_state(result == D_OK ? QWEN_STATE_IDLE : QWEN_STATE_ERROR);
#if D_LOCAL_PROMPT_ENABLE
    if (result == D_OK && !s_session.requested) d_local_prompt_foreground_end();
#endif
    if (result == D_OK) qwen_restore_idle_wakeup();
    if (result == D_OK && s_session.start_error == D_OK &&
        !s_qwen_ctx.worker_exit && s_qwen_ctx.is_wifi_connected)
        qwen_schedule_work("cleanup_ready");
    return result;
}

static void qwen_mark_activity(void)
{
    s_qwen_ctx.last_activity_ms = (uint32_t)timer_get_ms();
}

static const char *qwen_dialog_phase_name(qwen_dialog_phase_e phase)
{
    switch (phase) {
    case QWEN_DIALOG_PHASE_IDLE:
        return "IDLE";
    case QWEN_DIALOG_PHASE_PTT_PREPARING:
        return "PTT_PREPARING";
    case QWEN_DIALOG_PHASE_LISTENING:
        return "LISTENING";
    case QWEN_DIALOG_PHASE_PTT_ENDING:
        return "PTT_ENDING";
    case QWEN_DIALOG_PHASE_WAIT_ASR:
        return "WAIT_ASR";
    case QWEN_DIALOG_PHASE_WAIT_LLM:
        return "WAIT_LLM";
    case QWEN_DIALOG_PHASE_WAIT_TTS:
        return "WAIT_TTS";
    case QWEN_DIALOG_PHASE_PLAYING:
        return "PLAYING";
    case QWEN_DIALOG_PHASE_WAIT_LOCAL_TTS_END:
        return "WAIT_LOCAL_TTS_END";
    case QWEN_DIALOG_PHASE_FAILED:
        return "FAILED";
    default:
        return "UNKNOWN";
    }
}

static uint8_t qwen_dialog_phase_is_active(void)
{
    return (s_qwen_ctx.dialog_phase != QWEN_DIALOG_PHASE_IDLE) &&
           (s_qwen_ctx.dialog_phase != QWEN_DIALOG_PHASE_FAILED);
}

static uint8_t qwen_is_dialog_busy(void)
{
#if (D_QWEN_DIALOG_MODE == D_QWEN_DIALOG_MODE_DUPLEX)
    return (s_qwen_ctx.dialog_phase != QWEN_DIALOG_PHASE_IDLE) &&
           (s_qwen_ctx.dialog_phase != QWEN_DIALOG_PHASE_FAILED) &&
           (s_qwen_ctx.dialog_phase != QWEN_DIALOG_PHASE_LISTENING);
#else
    return qwen_dialog_phase_is_active();
#endif
}

static uint8_t qwen_dialog_phase_is_ptt_flow(void)
{
    return (s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_PTT_PREPARING) ||
           (s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_LISTENING) ||
           (s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_PTT_ENDING) ||
           (s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_WAIT_ASR);
}

static uint8_t qwen_dialog_phase_can_accept_speech_start(void)
{
    if (s_qwen_ctx.is_idle_goodbye_pending != 0U) {
        return 0U;
    }

#if (D_QWEN_DIALOG_MODE == D_QWEN_DIALOG_MODE_DUPLEX)
    return (s_qwen_ctx.dialog_phase != QWEN_DIALOG_PHASE_FAILED) ? 1U : 0U;
#else
    return (s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_PTT_PREPARING) ||
           (s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_LISTENING);
#endif
}

static uint8_t qwen_dialog_phase_can_accept_asr_text(void)
{
#if (D_QWEN_DIALOG_MODE == D_QWEN_DIALOG_MODE_DUPLEX)
    return (s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_LISTENING) ||
           (s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_WAIT_LLM) ||
           (s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_WAIT_TTS) ||
           (s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_PLAYING) ||
           (s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_WAIT_LOCAL_TTS_END);
#else
    return (qwen_dialog_phase_is_ptt_flow() != 0U) ||
           (s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_WAIT_LLM);
#endif
}

static uint8_t qwen_dialog_phase_can_accept_tts(void)
{
#if (D_QWEN_DIALOG_MODE == D_QWEN_DIALOG_MODE_DUPLEX)
    return (s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_LISTENING) ||
           (s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_WAIT_LLM) ||
           (s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_WAIT_TTS) ||
           (s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_PLAYING) ||
           (s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_WAIT_LOCAL_TTS_END);
#else
    return (s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_WAIT_LLM) ||
           (s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_WAIT_TTS);
#endif
}

static uint8_t qwen_dialog_phase_can_accept_llm(void)
{
#if (D_QWEN_DIALOG_MODE == D_QWEN_DIALOG_MODE_DUPLEX)
    return (s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_LISTENING) ||
           (s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_WAIT_LLM) ||
           (s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_WAIT_TTS) ||
           (s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_PLAYING) ||
           (s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_WAIT_LOCAL_TTS_END);
#else
    return (s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_WAIT_LLM) ||
           (s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_WAIT_TTS) ||
           (s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_PLAYING) ||
           (s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_WAIT_LOCAL_TTS_END);
#endif
}

static uint8_t qwen_state_can_start_ptt(void)
{
    return (s_qwen_ctx.state == QWEN_STATE_READY) ||
           (s_qwen_ctx.state == QWEN_STATE_THINKING) ||
           (s_qwen_ctx.state == QWEN_STATE_PLAYING);
}

static uint8_t qwen_state_can_queue_ptt(void)
{
    return (s_qwen_ctx.state == QWEN_STATE_REGISTERING) ||
           (s_qwen_ctx.state == QWEN_STATE_TOKEN_REQ) ||
           (s_qwen_ctx.state == QWEN_STATE_CONNECTING);
}

static uint8_t qwen_dialog_phase_can_start_ptt(void)
{
    return (s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_IDLE) ||
           (s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_WAIT_LLM) ||
           (s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_WAIT_TTS) ||
           (s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_PLAYING) ||
           (s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_WAIT_LOCAL_TTS_END);
}

static void qwen_set_dialog_phase(qwen_dialog_phase_e phase, const char *reason)
{
    qwen_dialog_phase_e old_phase;

    if (s_qwen_ctx.dialog_phase == phase) {
        if (phase != QWEN_DIALOG_PHASE_IDLE) {
            s_qwen_ctx.dialog_phase_start_ms = (uint32_t)timer_get_ms();
        }
        return;
    }

    old_phase = s_qwen_ctx.dialog_phase;
    s_qwen_ctx.dialog_phase = phase;
    s_qwen_ctx.dialog_phase_start_ms = (uint32_t)timer_get_ms();
    LOGI(TAG, "phase %s -> %s round=%u state=%s reason=%s text_req=%u speech=%u key=%u",
         qwen_dialog_phase_name(old_phase),
         qwen_dialog_phase_name(phase),
         (unsigned)s_qwen_ctx.dialog_round_id,
         qwen_state_name(s_qwen_ctx.state),
         (reason != NULL) ? reason : "unknown",
         (unsigned)s_qwen_ctx.is_text_request_ready,
         (unsigned)s_qwen_ctx.is_speech_active,
         (unsigned)s_qwen_ctx.is_ptt_key_down);
    if (((old_phase == QWEN_DIALOG_PHASE_IDLE) || (old_phase == QWEN_DIALOG_PHASE_FAILED)) &&
        (qwen_dialog_phase_is_active() != 0U) && (os_sem_valid(&s_qwen_ctx.work_sem) != 0)) {
        os_sem_post(&s_qwen_ctx.work_sem);
    }
}

static uint32_t qwen_dialog_phase_elapsed_ms(void)
{
    return (uint32_t)((uint32_t)timer_get_ms() - s_qwen_ctx.dialog_phase_start_ms);
}

static const char *qwen_failure_type_name(qwen_failure_type_e failure_type)
{
    switch (failure_type) {
    case QWEN_FAILURE_TYPE_TIMEOUT:
        return "timeout";
    case QWEN_FAILURE_TYPE_SDK:
        return "sdk";
    case QWEN_FAILURE_TYPE_NETWORK:
        return "network";
    case QWEN_FAILURE_TYPE_AUDIO:
        return "audio";
    case QWEN_FAILURE_TYPE_REQUEST:
        return "request";
    default:
        return "unknown";
    }
}

static qwen_failure_type_e qwen_failure_type_from_reason(const char *reason)
{
    if ((reason == NULL) || (reason[0] == '\0')) {
        return QWEN_FAILURE_TYPE_UNKNOWN;
    }

    if ((strncmp(reason, "no_", 3U) == 0) || (strstr(reason, "timeout") != NULL)) {
        return QWEN_FAILURE_TYPE_TIMEOUT;
    }

    if (strncmp(reason, "sdk_", 4U) == 0) {
        return QWEN_FAILURE_TYPE_SDK;
    }

    if ((strstr(reason, "wss") != NULL) || (strstr(reason, "network") != NULL)) {
        return QWEN_FAILURE_TYPE_NETWORK;
    }

    if ((strstr(reason, "audio") != NULL) || (strstr(reason, "recorder") != NULL) ||
        (strstr(reason, "player") != NULL) || (strstr(reason, "ptt_prepare") != NULL)) {
        return QWEN_FAILURE_TYPE_AUDIO;
    }

    return QWEN_FAILURE_TYPE_REQUEST;
}

static void qwen_set_idle_network_state(void)
{
    if (s_qwen_ctx.is_wifi_connected == 0U) {
        qwen_set_state(QWEN_STATE_IDLE);
        return;
    }

    if (qwen_net_is_runtime_allowed() == 0) {
        qwen_set_state(QWEN_STATE_IDLE);
        return;
    }

    if (qwen_wss_is_connected() != 0) {
        qwen_set_state(QWEN_STATE_READY);
        return;
    }

    qwen_set_state(QWEN_STATE_CONNECTING);
}

static void qwen_finish_dialog_failed(const char *reason, d_err_t err)
{
    qwen_failure_type_e failure_type;

    failure_type = qwen_failure_type_from_reason(reason);
    LOGW(TAG, "round failed: round=%u phase=%s elapsed=%u type=%s reason=%s code=%d",
         (unsigned)s_qwen_ctx.dialog_round_id,
         qwen_dialog_phase_name(s_qwen_ctx.dialog_phase),
         (unsigned)qwen_dialog_phase_elapsed_ms(),
         qwen_failure_type_name(failure_type),
         (reason != NULL) ? reason : "unknown",
         (int)err);
    s_qwen_ctx.is_speech_active = 0U;
    s_qwen_ctx.is_ptt_key_down = 0U;
    s_qwen_ctx.is_ptt_start_pending = 0U;
    s_qwen_ctx.is_text_request_ready = 0U;
    s_qwen_ctx.ready_cue_pending = 0U;
    qwen_audio_stop_recorder();
    qwen_audio_stop_player();
    qwen_post_error_event((int32_t)err);
    qwen_set_dialog_phase(QWEN_DIALOG_PHASE_IDLE, reason);
    qwen_set_idle_network_state();
}

static void qwen_finish_request_failed(const char *reason, d_err_t err)
{
    qwen_failure_type_e failure_type;

    failure_type = qwen_failure_type_from_reason(reason);
    LOGW(TAG, "request failed: round=%u state=%s phase=%s type=%s reason=%s code=%d",
         (unsigned)s_qwen_ctx.dialog_round_id,
         qwen_state_name(s_qwen_ctx.state),
         qwen_dialog_phase_name(s_qwen_ctx.dialog_phase),
         qwen_failure_type_name(failure_type),
         (reason != NULL) ? reason : "unknown",
         (int)err);
    s_qwen_ctx.is_text_request_ready = 0U;
    s_qwen_ctx.ready_cue_pending = 0U;
    qwen_audio_stop_player();
    qwen_post_error_event((int32_t)err);
    qwen_set_dialog_phase(QWEN_DIALOG_PHASE_IDLE, reason);
    qwen_set_idle_network_state();
}

static void qwen_check_dialog_timeout(void)
{
    uint32_t elapsed_ms;

    if (s_qwen_ctx.is_idle_goodbye_pending != 0U) {
        return;
    }

    if (qwen_dialog_phase_is_active() == 0U) {
        return;
    }

    elapsed_ms = qwen_dialog_phase_elapsed_ms();
    if ((s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_PTT_ENDING) &&
        (elapsed_ms >= QWEN_DIALOG_END_TIMEOUT_MS)) {
        qwen_finish_dialog_failed("no_asr", D_ERR_TIMEOUT);
        return;
    }

    if ((s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_WAIT_LLM) &&
        (elapsed_ms >= QWEN_DIALOG_RESPONSE_TIMEOUT_MS)) {
        qwen_finish_dialog_failed("no_llm", D_ERR_TIMEOUT);
        return;
    }

    if ((s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_WAIT_TTS) &&
        (elapsed_ms >= QWEN_DIALOG_RESPONSE_TIMEOUT_MS)) {
        qwen_finish_dialog_failed("no_tts", D_ERR_TIMEOUT);
        return;
    }

    if ((s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_WAIT_LOCAL_TTS_END) &&
        (elapsed_ms >= QWEN_DIALOG_LOCAL_DRAIN_TIMEOUT_MS)) {
        qwen_finish_dialog_failed("no_local_tts_end", D_ERR_TIMEOUT);
    }
}

static void qwen_do_idle_disconnect(const char *reason)
{
    qwen_request_cleanup(reason, 0U);
}

static uint8_t qwen_check_idle_goodbye_timeout(void)
{
    uint32_t idle_ms;

    idle_ms = (uint32_t)((uint32_t)timer_get_ms() -
                         s_qwen_ctx.idle_goodbye_start_ms);
    if (idle_ms < (uint32_t)D_QWEN_IDLE_GOODBYE_TIMEOUT_MS) {
        return 0U;
    }

    LOGW(TAG, "idle goodbye timeout: elapsed=%u timeout=%u",
         (unsigned)idle_ms,
         (unsigned)D_QWEN_IDLE_GOODBYE_TIMEOUT_MS);
    qwen_do_idle_disconnect("idle_goodbye_timeout");
    return 1U;
}

static uint8_t qwen_keep_idle_alive_for_player(void)
{
    if (qwen_audio_is_player_active() == 0U) {
        return 0U;
    }

    if (qwen_audio_is_player_stalled(D_QWEN_PLAYER_STALL_TIMEOUT_MS) != 0U) {
        LOGW(TAG, "player stalled: timeout=%u, stop before idle",
             (unsigned)D_QWEN_PLAYER_STALL_TIMEOUT_MS);
        qwen_audio_stop_player();
        return 0U;
    }

    qwen_mark_activity();
    return 1U;
}

static uint8_t qwen_idle_timeout_can_start(void)
{
    if ((s_qwen_ctx.is_initialized == 0U) ||
        (s_qwen_ctx.is_wifi_connected == 0U) ||
        (s_qwen_ctx.want_wss_connected == 0U) ||
        (qwen_is_dialog_busy() != 0U) ||
        (qwen_wss_is_connected() == 0)) {
        return 0U;
    }

    if (qwen_keep_idle_alive_for_player() != 0U) {
        return 0U;
    }

    return 1U;
}

static d_err_t qwen_start_idle_goodbye_tts(void)
{
    d_err_t err;

    qwen_audio_prepare_player();
    LOGI(TAG, "idle goodbye tts: len=%u text=%s",
         (unsigned)strlen(D_QWEN_IDLE_GOODBYE_TEXT),
         D_QWEN_IDLE_GOODBYE_TEXT);
    s_qwen_ctx.dialog_round_id++;
    s_qwen_ctx.is_text_request_ready = 0U;
    qwen_set_dialog_phase(QWEN_DIALOG_PHASE_WAIT_TTS, "idle_goodbye_tts");
    qwen_set_state(QWEN_STATE_THINKING);
    err = qwen_map_sdk_err(c_mmi_tts((char *)D_QWEN_IDLE_GOODBYE_TEXT));
    if (err != D_OK) {
        qwen_audio_stop_player();
        return err;
    }

    return D_OK;
}

static uint8_t qwen_start_idle_goodbye(uint32_t idle_ms)
{
    d_err_t err;

    LOGI(TAG, "idle timeout: idle=%u timeout=%u",
         (unsigned)idle_ms,
         (unsigned)D_QWEN_IDLE_TIMEOUT_MS);
    s_qwen_ctx.is_idle_goodbye_pending = 1U;
    s_qwen_ctx.idle_goodbye_start_ms = (uint32_t)timer_get_ms();
    s_qwen_ctx.is_speech_active = 0U;
    s_qwen_ctx.is_text_request_ready = 0U;
    s_qwen_ctx.ready_cue_pending = 0U;
    s_qwen_ctx.ready_cue_sent = 1U;
    qwen_audio_stop_recorder();
#if (D_QWEN_DIALOG_MODE == D_QWEN_DIALOG_MODE_DUPLEX)
    LOGI(TAG, "idle goodbye speech_pause skipped: dialog=duplex");
#else
    err = qwen_map_sdk_err(c_mmi_speech_pause());
    if (err != D_OK) {
        LOGW(TAG, "idle goodbye speech_pause failed: %d, disconnect now",
             (int)err);
        qwen_do_idle_disconnect("idle_timeout_pause_failed");
        return 1U;
    }

    LOGI(TAG, "idle goodbye speech_pause ok");
#endif
    err = qwen_start_idle_goodbye_tts();
    if (err != D_OK) {
        LOGW(TAG, "idle goodbye tts failed: %d, disconnect now", (int)err);
        qwen_do_idle_disconnect("idle_timeout_no_tts");
    }
    return 1U;
}

static uint8_t qwen_check_idle_timeout(void)
{
    uint32_t idle_ms;

    if (D_QWEN_IDLE_TIMEOUT_MS == 0U) {
        return 0U;
    }

    if (s_qwen_ctx.is_idle_goodbye_pending != 0U) {
        return qwen_check_idle_goodbye_timeout();
    }

    if (qwen_idle_timeout_can_start() == 0U) {
        return 0U;
    }

    idle_ms = (uint32_t)((uint32_t)timer_get_ms() - s_qwen_ctx.last_activity_ms);
    if (idle_ms < (uint32_t)D_QWEN_IDLE_TIMEOUT_MS) {
        return 0U;
    }

    return qwen_start_idle_goodbye(idle_ms);
}

static d_err_t qwen_map_sdk_err(int32_t ret)
{
    if (ret == 0) {
        return D_OK;
    }

    if (ret == UTIL_ERR_INVALID_PARAM) {
        return D_ERR_INVALID_ARG;
    }

    if (ret == UTIL_ERR_BUSY) {
        return D_ERR_BUSY;
    }

    return D_ERR_NOT_READY;
}

static const char *qwen_state_name(qwen_state_e state)
{
    switch (state) {
    case QWEN_STATE_IDLE:
        return "IDLE";
    case QWEN_STATE_REGISTERING:
        return "REGISTERING";
    case QWEN_STATE_TOKEN_REQ:
        return "TOKEN_REQ";
    case QWEN_STATE_CONNECTING:
        return "CONNECTING";
    case QWEN_STATE_READY:
        return "READY";
    case QWEN_STATE_LISTENING:
        return "LISTENING";
    case QWEN_STATE_THINKING:
        return "THINKING";
    case QWEN_STATE_PLAYING:
        return "PLAYING";
    case QWEN_STATE_ERROR:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}

static void qwen_log_final_text(const char *label, const char *text)
{
    size_t text_len;
    size_t preview_len;

    if ((label == NULL) || (text == NULL) || (text[0] == '\0')) {
        LOGI(TAG, "%s len=0", (label != NULL) ? label : "text");
        return;
    }

    text_len = strlen(text);
    preview_len = text_len;
    if (preview_len > QWEN_TEXT_LOG_PREVIEW_LEN) {
        preview_len = QWEN_TEXT_LOG_PREVIEW_LEN;
    }

    LOGI(TAG, "%s len=%u preview=%.*s%s",
         label,
         (unsigned)text_len,
         (int)preview_len,
         text,
         (text_len > preview_len) ? "..." : "");
}

static void qwen_post_text_event(d_event_id_t event_id, const char *label, const char *text)
{
    char payload[QWEN_TEXT_EVENT_MAX_LEN];
    size_t raw_len;
    size_t copy_len;
    uint8_t truncated;

    if ((text == NULL) || (text[0] == '\0')) {
        QWEN_TRACE("%s skipped: empty", (label != NULL) ? label : "text");
        return;
    }

    raw_len = strlen(text);
    truncated = 0U;
    copy_len = raw_len;

    if ((event_id == EVT_QWEN_LLM_PARTIAL_RESULT) ||
        (event_id == EVT_QWEN_LLM_RESULT)) {
        QWEN_TRACE("%s sync len=%u",
                   (label != NULL) ? label : "text",
                   (unsigned)raw_len);
        d_event_publish(event_id, text,
                        (uint16_t)((raw_len < 0xFFFFU) ?
                                   (raw_len + 1U) : 0xFFFFU));
        return;
    }

    if (copy_len >= sizeof(payload)) {
        copy_len = sizeof(payload) - 1U;
        truncated = 1U;
    }

    if (truncated != 0U) {
        LOGW(TAG, "%s async limit: raw=%u max=%u, publish sync",
             (label != NULL) ? label : "text",
             (unsigned)raw_len,
             (unsigned)sizeof(payload));
        d_event_publish(event_id, text, (uint16_t)((raw_len < 0xFFFFU) ? (raw_len + 1U) : 0xFFFFU));
        return;
    }

    memcpy(payload, text, copy_len);
    payload[copy_len] = '\0';
    QWEN_TRACE("%s len=%u", (label != NULL) ? label : "text", (unsigned)copy_len);
    d_event_post(event_id, payload, (uint16_t)(copy_len + 1U));
}

static void qwen_post_error_event(int32_t code)
{
    d_event_post(EVT_QWEN_ERROR, &code, sizeof(code));
}

static void qwen_set_state(qwen_state_e state)
{
    qwen_state_e old_state;

    if (s_qwen_ctx.state == state) {
        return;
    }

    old_state = s_qwen_ctx.state;
    spin_lock(&s_qwen_session_lock);
    s_qwen_ctx.state = state;
    s_session.revision++;
    spin_unlock(&s_qwen_session_lock);
    LOGI(TAG, "state %s -> %s", qwen_state_name(old_state), qwen_state_name(state));
    d_event_post(EVT_QWEN_STATE_CHANGED, &state, sizeof(state));
}

static qwen_state_e qwen_state_from_dialog_phase(qwen_state_e fallback_state)
{
    switch (s_qwen_ctx.dialog_phase) {
    case QWEN_DIALOG_PHASE_LISTENING:
    case QWEN_DIALOG_PHASE_PTT_ENDING:
    case QWEN_DIALOG_PHASE_WAIT_ASR:
        return QWEN_STATE_LISTENING;
    case QWEN_DIALOG_PHASE_WAIT_LLM:
    case QWEN_DIALOG_PHASE_WAIT_TTS:
        return QWEN_STATE_THINKING;
    case QWEN_DIALOG_PHASE_PLAYING:
    case QWEN_DIALOG_PHASE_WAIT_LOCAL_TTS_END:
        return QWEN_STATE_PLAYING;
    default:
        return fallback_state;
    }
}

static void qwen_restore_state_after_auth_if_needed(qwen_state_e fallback_state)
{
    qwen_state_e restore_state;

    if ((s_qwen_ctx.is_wifi_connected == 0U) || (qwen_wss_is_connected() == 0)) {
        return;
    }

    if (qwen_dialog_phase_is_active() != 0U) {
        restore_state = qwen_state_from_dialog_phase(fallback_state);
        qwen_set_state(restore_state);
        return;
    }

    if ((s_qwen_ctx.state == QWEN_STATE_REGISTERING) ||
        (s_qwen_ctx.state == QWEN_STATE_TOKEN_REQ)) {
        qwen_set_state(QWEN_STATE_READY);
    }
}

static const char *qwen_mmi_event_name(uint32_t event)
{
    switch (event) {
    case C_MMI_EVENT_USER_CONFIG:
        return "USER_CONFIG";
    case C_MMI_EVENT_DATA_INIT:
        return "DATA_INIT";
    case C_MMI_EVENT_SPEECH_READY:
        return "SPEECH_READY";
    case C_MMI_EVENT_SPEECH_PREPARE:
        return "SPEECH_PREPARE";
    case C_MMI_EVENT_SPEECH_START:
        return "SPEECH_START";
    case C_MMI_EVENT_SPEECH_RESTART:
        return "SPEECH_RESTART";
    case C_MMI_EVENT_ASR_START:
        return "ASR_START";
    case C_MMI_EVENT_ASR_INCOMPLETE:
        return "ASR_INCOMPLETE";
    case C_MMI_EVENT_ASR_COMPLETE:
        return "ASR_COMPLETE";
    case C_MMI_EVENT_ASR_END:
        return "ASR_END";
    case C_MMI_EVENT_LLM_INCOMPLETE:
        return "LLM_INCOMPLETE";
    case C_MMI_EVENT_LLM_COMPLETE:
        return "LLM_COMPLETE";
    case C_MMI_EVENT_TTS_START:
        return "TTS_START";
    case C_MMI_EVENT_TTS_END:
        return "TTS_END";
    case C_MMI_EVENT_DATA_DEINIT:
        return "DATA_DEINIT";
    case C_MMI_EVENT_HEARTBEAT:
        return "HEARTBEAT";
    default:
        return "UNKNOWN";
    }
}

static uint8_t qwen_idle_goodbye_ignore_asr(uint32_t event)
{
    if (s_qwen_ctx.is_idle_goodbye_pending == 0U) {
        return 0U;
    }

    s_qwen_ctx.is_speech_active = 0U;
    s_qwen_ctx.is_text_request_ready = 0U;
    LOGI(TAG, "idle goodbye asr ignored: event=%s state=%s phase=%s round=%u",
         qwen_mmi_event_name(event),
         qwen_state_name(s_qwen_ctx.state),
         qwen_dialog_phase_name(s_qwen_ctx.dialog_phase),
         (unsigned)s_qwen_ctx.dialog_round_id);
    return 1U;
}

static const char *qwen_service_event_name(uint32_t event)
{
    switch (event) {
    case EVT_WIFI_CONNECTED:
        return "WIFI_CONNECTED";
    case EVT_WIFI_DISCONNECTED:
        return "WIFI_DISCONNECTED";
    case EVT_NTP_SYNCED:
        return "NTP_SYNCED";
    case EVT_QWEN_WSS_DISCONNECTED:
        return "QWEN_WSS_DISCONNECTED";
    default:
        return "UNKNOWN";
    }
}

static const char *qwen_dispatch_event_name(qwen_dispatch_event_e event)
{
    switch (event) {
    case QWEN_DISPATCH_EVT_MMI:
        return "MMI";
    case QWEN_DISPATCH_EVT_WIFI:
        return "WIFI";
    case QWEN_DISPATCH_EVT_NTP:
        return "NTP";
    case QWEN_DISPATCH_EVT_AUDIO:
        return "AUDIO";
    case QWEN_DISPATCH_EVT_CMD:
        return "CMD";
    case QWEN_DISPATCH_EVT_KEY:
        return "KEY";
    case QWEN_DISPATCH_EVT_WSS:
        return "WSS";
    default:
        return "UNKNOWN";
    }
}

static void qwen_log_dispatch_event(qwen_dispatch_event_e event, uint32_t code)
{
    LOGI(TAG, "dispatch event=%s code=%u state=%s phase=%s round=%u text_req=%u speech=%u key=%u",
         qwen_dispatch_event_name(event),
         (unsigned)code,
         qwen_state_name(s_qwen_ctx.state),
         qwen_dialog_phase_name(s_qwen_ctx.dialog_phase),
         (unsigned)s_qwen_ctx.dialog_round_id,
         (unsigned)s_qwen_ctx.is_text_request_ready,
         (unsigned)s_qwen_ctx.is_speech_active,
         (unsigned)s_qwen_ctx.is_ptt_key_down);
}

static uint32_t qwen_ms_to_ticks(uint32_t ms)
{
    uint64_t scaled_ticks;
    uint32_t ticks;

    if (ms == 0U) {
        return 0U;
    }

    scaled_ticks = ((uint64_t)ms * (uint64_t)OS_TICKS_PER_SEC) + (QWEN_MS_PER_SECOND - 1U);
    ticks = (uint32_t)(scaled_ticks / (uint64_t)QWEN_MS_PER_SECOND);
    if (ticks == 0U) {
        ticks = 1U;
    }

    return ticks;
}

static void qwen_log_worker_snapshot(const char *stage)
{
    LOGI(TAG, "%s: state=%d phase=%s round=%u wifi=%u ntp=%u want_wss=%u speech=%u key=%u ptt_pending=%u text_req=%u reconnect=%u reinit=%u",
         (stage != NULL) ? stage : "snapshot",
         (int)s_qwen_ctx.state,
         qwen_dialog_phase_name(s_qwen_ctx.dialog_phase),
         (unsigned)s_qwen_ctx.dialog_round_id,
         (unsigned)s_qwen_ctx.is_wifi_connected,
         (unsigned)d_crypto_timestamp_inited(),
         (unsigned)s_qwen_ctx.want_wss_connected,
         (unsigned)s_qwen_ctx.is_speech_active,
         (unsigned)s_qwen_ctx.is_ptt_key_down,
         (unsigned)s_qwen_ctx.is_ptt_start_pending,
         (unsigned)s_qwen_ctx.is_text_request_ready,
         (unsigned)s_qwen_ctx.is_wss_reconnect_pending,
         (unsigned)s_qwen_ctx.is_mmi_reinit_pending);
}

static void qwen_resume_heartbeat(void)
{
    int32_t sdk_result;

    sdk_result = c_mmi_heartbeat_resume();
    if (sdk_result != 0) {
        LOGW(TAG, "heartbeat resume failed: %d", sdk_result);
    }
}

static void qwen_schedule_work(const char *reason)
{
    if (os_sem_valid(&s_qwen_ctx.work_sem) == 0) {
        LOGW(TAG, "schedule work skipped: sem invalid reason=%s",
             (reason != NULL) ? reason : "unknown");
        return;
    }

    LOGI(TAG, "schedule work: reason=%s",
         (reason != NULL) ? reason : "unknown");
    os_sem_post(&s_qwen_ctx.work_sem);
}

static uint8_t qwen_cmd_is_general_change(const qwen_cmd_msg_t *msg)
{
    if (msg == NULL) {
        return 0U;
    }

    if (msg->domain != QWEN_CMD_DOMAIN_UNKNOWN) {
        return 0U;
    }

    return (strcmp(msg->text, QWEN_CMD_CHANGE_TEXT) == 0) ? 1U : 0U;
}

static void qwen_on_audio_event(const d_event_t *event, void *user_data)
{
    uint32_t generation;
    (void)user_data;
    if ((event == NULL) || (event->id != EVT_QWEN_TTS_LOCAL_END) ||
        event->data == NULL || event->data_len != sizeof(generation)) {
        return;
    }
    memcpy(&generation, event->data, sizeof(generation));
    if (generation != qwen_audio_player_generation() ||
        qwen_session_cleanup_pending()) return;

    qwen_ctx_lock();
    if (generation != qwen_audio_player_generation() || qwen_session_cleanup_pending()) {
        qwen_ctx_unlock();
        return;
    }
    qwen_mark_activity();
    qwen_log_dispatch_event(QWEN_DISPATCH_EVT_AUDIO, (uint32_t)event->id);
    LOGI(TAG, "local tts end: state=%s phase=%s round=%u goodbye=%u",
         qwen_state_name(s_qwen_ctx.state),
         qwen_dialog_phase_name(s_qwen_ctx.dialog_phase),
         (unsigned)s_qwen_ctx.dialog_round_id,
         (unsigned)s_qwen_ctx.is_idle_goodbye_pending);
    if (s_qwen_ctx.is_idle_goodbye_pending != 0U) {
        qwen_do_idle_disconnect("idle_goodbye_done");
        qwen_ctx_unlock();
        return;
    }
    if (s_qwen_ctx.dialog_phase != QWEN_DIALOG_PHASE_WAIT_LOCAL_TTS_END) {
        qwen_ctx_unlock();
        return;
    }

    qwen_set_dialog_phase(QWEN_DIALOG_PHASE_IDLE, "local_tts_end");
    qwen_set_idle_network_state();
    qwen_ctx_unlock();
}

static void qwen_on_cmd_event(const d_event_t *event, void *user_data)
{
    qwen_cmd_msg_t msg;

    (void)user_data;
    if ((event == NULL) || (event->id != EVT_QWEN_CMD_RECEIVED) ||
        (event->data == NULL) || (event->data_len < sizeof(qwen_cmd_msg_t))) {
        return;
    }

    qwen_log_dispatch_event(QWEN_DISPATCH_EVT_CMD, (uint32_t)event->id);
    memcpy(&msg, event->data, sizeof(msg));
    if (qwen_cmd_is_general_change(&msg) == 0U) {
        return;
    }

    LOGW(TAG, "sdk special cmd consumed: route=general_change action=ignored state=%s phase=%s round=%u",
         qwen_state_name(s_qwen_ctx.state),
         qwen_dialog_phase_name(s_qwen_ctx.dialog_phase),
         (unsigned)s_qwen_ctx.dialog_round_id);
}

static void qwen_on_time_event(const d_event_t *event, void *user_data)
{
    (void)user_data;
    if ((event == NULL) || (event->id != EVT_NTP_SYNCED)) {
        return;
    }

    qwen_log_dispatch_event(QWEN_DISPATCH_EVT_NTP, (uint32_t)event->id);
    qwen_schedule_work("ntp_synced");
}

#if D_DEVICE_CLOUD_ENABLE
static void qwen_on_provision_event(const d_event_t *event, void *user_data)
{
    (void)user_data;
    if ((event == NULL) || (event->id != EVT_PROVISION_OK)) {
        return;
    }

    qwen_ctx_lock();
    qwen_mark_activity();
    if (D_QWEN_SESSION_START_POLICY == D_QWEN_SESSION_START_AUTO)
        (void)qwen_service_session_start();
    qwen_schedule_work("provision_ok");
    qwen_ctx_unlock();
}
#endif

static void qwen_on_wss_event(const d_event_t *event, void *user_data)
{
    uint32_t generation;
    (void)user_data;
    if ((event == NULL) || (event->id != EVT_QWEN_WSS_DISCONNECTED) ||
        event->data == NULL || event->data_len != sizeof(generation)) {
        return;
    }
    memcpy(&generation, event->data, sizeof(generation));
    if (generation != qwen_wss_generation()) return;

    qwen_ctx_lock();
    if (generation != qwen_wss_generation()) {
        qwen_ctx_unlock();
        return;
    }
    qwen_mark_activity();
    qwen_log_dispatch_event(QWEN_DISPATCH_EVT_WSS, (uint32_t)event->id);
    qwen_handle_wss_disconnected();
    qwen_ctx_unlock();
}

#if D_FEATURE_XIAOLE_WAKEUP
static void qwen_on_xiaole_event(const d_event_t *event, void *user_data)
{
    d_err_t result;
    uint32_t generation;
    (void)user_data;
    if (event == NULL || event->id != EVT_XIAOLE_WAKEUP_DETECTED ||
        event->data == NULL || event->data_len != sizeof(generation)) return;
    memcpy(&generation, event->data, sizeof(generation));
    /* Delivery can lag behind a whole maintenance/cleanup cycle. Comparing
     * and accepting under the session lock prevents check-then-start races. */
    result = qwen_session_start_checked(&generation);
    if (result != D_OK) qwen_schedule_work("xiaole_retry_standby");
}
#endif

static void qwen_post_wss_disconnected(uint32_t generation)
{
    d_event_post(EVT_QWEN_WSS_DISCONNECTED, &generation, sizeof(generation));
}

static void qwen_cleanup_network(void)
{
    qwen_request_cleanup("cleanup_network", 0U);
}

#if (D_FEATURE_BLE_PROVISION) && (D_QWEN_DIALOG_MODE == D_QWEN_DIALOG_MODE_DUPLEX)
static uint8_t qwen_service_can_reconfig_wifi_by_key(void)
{
    switch (s_qwen_ctx.state) {
    case QWEN_STATE_IDLE:
    case QWEN_STATE_READY:
    case QWEN_STATE_LISTENING:
    case QWEN_STATE_THINKING:
    case QWEN_STATE_PLAYING:
    case QWEN_STATE_ERROR:
        return 1U;
    default:
        return 0U;
    }
}

static int qwen_service_handle_wifi_reconfig_key(void)
{
    d_err_t err;
#if D_FEATURE_OTA
    uint8_t held;
    spin_lock(&s_qwen_session_lock); held = s_maintenance_hold; spin_unlock(&s_qwen_session_lock);
    if (held) return 1;
#endif

    qwen_ctx_lock();
    if (s_qwen_ctx.is_wifi_reconfiguring != 0U) {
        LOGW(TAG, "wifi reconfig ignored: already running");
        qwen_ctx_unlock();
        return 1;
    }
    if (qwen_service_can_reconfig_wifi_by_key() == 0U) {
        LOGW(TAG, "wifi reconfig rejected: state=%s phase=%s",
             qwen_state_name(s_qwen_ctx.state),
             qwen_dialog_phase_name(s_qwen_ctx.dialog_phase));
        qwen_ctx_unlock();
        return 1;
    }

    LOGI(TAG, "wifi reconfig start: state=%s phase=%s",
         qwen_state_name(s_qwen_ctx.state),
         qwen_dialog_phase_name(s_qwen_ctx.dialog_phase));
    s_qwen_ctx.is_wifi_reconfiguring = 1U;
    qwen_cleanup_network();
    qwen_ctx_unlock();

    /*
     * Atomic clear+leave under wifi connect-seq (no reconnect gap).
     * Also re-opens BLE provisioning adv when D_FEATURE_BLE_PROVISION.
     */
    err = wifi_service_forget_saved();
    if (err != D_OK) {
        LOGE(TAG, "wifi forget_saved failed: %d", err);
        /* Allow key retry; success path clears on EVT_WIFI_CONNECTED. */
        qwen_ctx_lock();
        s_qwen_ctx.is_wifi_reconfiguring = 0U;
        qwen_ctx_unlock();
        return 1;
    }

    return 1;
}
#endif

static d_err_t qwen_prepare_network(void)
{
    d_err_t err;

    LOGI(TAG, "prepare network start");
    err = qwen_net_prepare(qwen_on_mmi_event);
    if (err == D_ERR_NOT_READY) {
        LOGW(TAG, "Qwen 配置未完成，跳过联网初始化");
        return err;
    }

    if (err != D_OK) {
        LOGE(TAG, "Qwen prepare failed: %d", err);
        qwen_set_state(QWEN_STATE_ERROR);
        return err;
    }

    if (s_qwen_ctx.is_cmd_registered == 0U) {
        err = qwen_cmd_register_handlers();
        if (err != D_OK) {
            LOGW(TAG, "cmd handlers register failed: %d", err);
        }
        s_qwen_ctx.is_cmd_registered = 1U;
    }

    LOGI(TAG, "prepare network done");
    return D_OK;
}

#if D_QWEN_HAL_ACCEPT_TEST_ENABLE
static d_err_t qwen_run_hal_accept_test_if_needed(void)
{
    int32_t ret;

    if (s_qwen_ctx.is_hal_accept_test_done != 0U) {
        return s_qwen_ctx.hal_accept_test_ret == UTIL_SUCCESS ? D_OK : D_ERR_IO;
    }

    LOGI(TAG, "hal accept test start");
    ret = qwen_sdk_test();
    s_qwen_ctx.is_hal_accept_test_done = 1U;
    s_qwen_ctx.hal_accept_test_ret = ret;
    if (ret != UTIL_SUCCESS) {
        LOGE(TAG, "hal accept test failed: ret=%d", ret);
        qwen_set_state(QWEN_STATE_ERROR);
        return D_ERR_IO;
    }

    LOGI(TAG, "hal accept test done");
    return D_OK;
}
#endif

static uint8_t qwen_request_mmi_cleanup_for_auth(void)
{
    if (!s_qwen_ctx.mmi_data_initialized) return 0U;
    /* Token analysis calls c_mmi_init(), which rejects an already initialized SDK. */
    qwen_request_cleanup("auth_refresh", 1U);
    return 1U;
}

static d_err_t qwen_sync_auth_if_needed(void)
{
    d_err_t err;
    qwen_state_e auth_prev_state;
    uint8_t is_registered;
    uint8_t is_token_valid;
    uint8_t is_refresh_needed;

    is_registered = (uint8_t)qwen_net_is_registered();
    is_token_valid = (uint8_t)qwen_net_is_token_valid();
    is_refresh_needed = (uint8_t)qwen_net_is_token_refresh_needed();
    LOGI(TAG, "auth gate: registered=%u token_valid=%u refresh=%u",
         (unsigned)is_registered,
         (unsigned)is_token_valid,
         (unsigned)is_refresh_needed);
    if ((is_registered != 0U) && (is_refresh_needed == 0U)) {
        LOGI(TAG, "auth skip: token still valid");
        qwen_restore_state_after_auth_if_needed(s_qwen_ctx.state);
        return D_OK;
    }
    if ((is_registered != 0U) &&
        (is_token_valid != 0U) &&
        (qwen_dialog_phase_is_active() != 0U)) {
        LOGI(TAG, "auth defer: phase=%s state=%s",
             qwen_dialog_phase_name(s_qwen_ctx.dialog_phase),
             qwen_state_name(s_qwen_ctx.state));
        return D_OK;
    }

    if (qwen_request_mmi_cleanup_for_auth()) return D_ERR_NOT_READY;
    auth_prev_state = s_qwen_ctx.state;
    if (is_registered == 0U) {
        if (qwen_dialog_phase_is_active() == 0U) {
            qwen_set_state(QWEN_STATE_REGISTERING);
        }
    } else {
        if (qwen_dialog_phase_is_active() == 0U) {
            qwen_set_state(QWEN_STATE_TOKEN_REQ);
        } else {
            LOGI(TAG, "auth background: state=%s phase=%s",
                 qwen_state_name(s_qwen_ctx.state),
                 qwen_dialog_phase_name(s_qwen_ctx.dialog_phase));
        }
    }

    err = qwen_net_sync_auth();
    if (err != D_OK) {
        LOGE(TAG, "Qwen auth failed: %d", err);
        qwen_set_state(QWEN_STATE_ERROR);
        return err;
    }

    LOGI(TAG, "auth sync done");
    qwen_restore_state_after_auth_if_needed(auth_prev_state);
    return D_OK;
}

static void qwen_wait_wss_reconnect_if_needed(void)
{
    uint32_t delay_ms;
    uint32_t delay_ticks;

    if (s_qwen_ctx.is_wss_reconnect_pending == 0U) {
        return;
    }

    qwen_audio_stop_all();
    s_qwen_ctx.is_speech_active = 0U;
    s_qwen_ctx.is_text_request_ready = 0U;
    delay_ms = s_qwen_ctx.wss_reconnect_delay_ms;
    if (delay_ms < QWEN_WSS_RECONNECT_DELAY_MS) {
        delay_ms = QWEN_WSS_RECONNECT_DELAY_MS;
    }
    LOGW(TAG, "wss reconnect delay %u ms", (unsigned)delay_ms);
    delay_ticks = qwen_ms_to_ticks(delay_ms);
    os_time_dly((int)delay_ticks);
    if ((s_qwen_ctx.worker_exit != 0U) || (s_qwen_ctx.is_wifi_connected == 0U)) {
        return;
    }

    s_qwen_ctx.is_wss_reconnect_pending = 0U;
    if (delay_ms <= (QWEN_WSS_RECONNECT_DELAY_MAX_MS / 2U)) {
        s_qwen_ctx.wss_reconnect_delay_ms = delay_ms * 2U;
    } else {
        s_qwen_ctx.wss_reconnect_delay_ms = QWEN_WSS_RECONNECT_DELAY_MAX_MS;
    }
}

static void qwen_prepare_mmi_reinit_if_needed(void)
{
    if (s_qwen_ctx.is_mmi_reinit_pending != 0U) {
        s_qwen_ctx.is_mmi_reinit_pending = 0U;
        qwen_request_cleanup("mmi_reinit", 1U);
    }
}

static d_err_t qwen_sync_auth_for_worker(uint8_t force_auth)
{
    d_err_t err;
    qwen_state_e auth_prev_state;

    if (force_auth == 0U) {
        return qwen_sync_auth_if_needed();
    }

    if (qwen_request_mmi_cleanup_for_auth()) return D_ERR_NOT_READY;
    auth_prev_state = s_qwen_ctx.state;
    if (qwen_dialog_phase_is_active() == 0U) {
        qwen_set_state(QWEN_STATE_TOKEN_REQ);
    } else {
        LOGI(TAG, "reauth background: state=%s phase=%s",
             qwen_state_name(s_qwen_ctx.state),
             qwen_dialog_phase_name(s_qwen_ctx.dialog_phase));
    }
    err = qwen_net_sync_auth();
    if (err != D_OK) {
        LOGE(TAG, "Qwen reauth failed: %d", err);
        qwen_set_state(QWEN_STATE_ERROR);
        return err;
    }

    LOGI(TAG, "reauth sync done");
    qwen_restore_state_after_auth_if_needed(auth_prev_state);
    return D_OK;
}

static void qwen_connect_wss_if_needed(void)
{
    d_err_t err;
    uint32_t generation;

    if (!qwen_session_allows_business() || qwen_wss_is_connected()) return;
    generation = qwen_session_generation();
    qwen_set_state(QWEN_STATE_CONNECTING);
#if D_LOCAL_PROMPT_ENABLE
    d_local_prompt_foreground_begin();
    err = d_local_prompt_wait_stopped(QWEN_SESSION_HANDOFF_MS);
    if (err != D_OK) {
        spin_lock(&s_qwen_session_lock);
        qwen_session_policy_failed(&s_session, err);
        spin_unlock(&s_qwen_session_lock);
        s_qwen_ctx.cleanup_retry = 1U;
        qwen_post_error_event((int32_t)err);
        qwen_schedule_work("prompt_handoff_failed");
        return;
    }
#endif
    qwen_wait_wss_reconnect_if_needed();
    if (!qwen_session_allows_business() || generation != qwen_session_generation()) return;
#if D_FEATURE_XIAOLE_WAKEUP
    if (s_qwen_ctx.microphone_generation != generation) {
        err = xiaole_wakeup_stop_wait(QWEN_SESSION_HANDOFF_MS);
        if (err != D_OK) {
            spin_lock(&s_qwen_session_lock);
            qwen_session_policy_failed(&s_session, err);
            spin_unlock(&s_qwen_session_lock);
            s_qwen_ctx.cleanup_retry = 1U;
            qwen_schedule_work("microphone_handoff_failed");
            return;
        }
        s_qwen_ctx.microphone_generation = generation;
    }
#endif
    if (!qwen_session_allows_business() || generation != qwen_session_generation()) return;
#if D_FEATURE_POWER_CONTROL
    if (power_ctrl_is_shutting_down()) return;
#endif
    err = qwen_wss_connect();
    if (generation != qwen_session_generation() || !qwen_session_allows_business()) {
        /* A transport failure may have requested reconnect; do not erase that intent. */
        qwen_request_cleanup("connect_canceled", 1U);
        return;
    }
    spin_lock(&s_qwen_session_lock);
    if (err == D_OK) qwen_session_policy_connected(&s_session, 1U);
    else qwen_session_policy_failed(&s_session, err);
    spin_unlock(&s_qwen_session_lock);
    if (err != D_OK) {
        s_qwen_ctx.cleanup_retry = 1U;
        qwen_post_error_event((int32_t)err);
        qwen_schedule_work("connect_failed");
    }
}

static void qwen_handle_ready_cue_if_needed(void)
{
    d_err_t err;

    if (s_qwen_ctx.ready_cue_pending == 0U) {
        return;
    }

    if (s_qwen_ctx.ready_cue_sent != 0U) {
        s_qwen_ctx.ready_cue_pending = 0U;
        return;
    }

    if ((s_qwen_ctx.is_text_request_ready == 0U) ||
        ((s_qwen_ctx.state != QWEN_STATE_READY) &&
         (s_qwen_ctx.state != QWEN_STATE_LISTENING))) {
        QWEN_TRACE("ready cue wait: state=%s phase=%s text_req=%u",
                   qwen_state_name(s_qwen_ctx.state),
                   qwen_dialog_phase_name(s_qwen_ctx.dialog_phase),
                   (unsigned)s_qwen_ctx.is_text_request_ready);
        return;
    }

#if (D_QWEN_DIALOG_MODE != D_QWEN_DIALOG_MODE_DUPLEX)
    if (qwen_dialog_phase_is_active() != 0U) {
        return;
    }
#endif

    LOGI(TAG, "ready cue trigger");
    qwen_audio_prepare_player();

    LOGI(TAG, "tts req: len=%u text=%s", (unsigned)strlen(D_QWEN_READY_CUE_TEXT), D_QWEN_READY_CUE_TEXT);
    err = qwen_map_sdk_err(c_mmi_tts((char *)D_QWEN_READY_CUE_TEXT));
    if (err != D_OK) {
        qwen_audio_stop_player();

        LOGW(TAG, "ready cue deferred: err=%d state=%s", err, qwen_state_name(s_qwen_ctx.state));
        return;
    }

    s_qwen_ctx.ready_cue_sent = 1U;
    s_qwen_ctx.ready_cue_pending = 0U;
    s_qwen_ctx.is_text_request_ready = 0U;
#if (D_QWEN_DIALOG_MODE != D_QWEN_DIALOG_MODE_DUPLEX)
    s_qwen_ctx.dialog_round_id++;
    qwen_set_dialog_phase(QWEN_DIALOG_PHASE_WAIT_TTS, "ready_cue");
    qwen_set_state(QWEN_STATE_THINKING);
#endif
}

static void qwen_handle_pending_ptt_start(void)
{
    d_err_t err;

    if (s_qwen_ctx.is_ptt_start_pending == 0U) {
        return;
    }

    if (s_qwen_ctx.is_ptt_key_down == 0U) {
        s_qwen_ctx.is_ptt_start_pending = 0U;
        qwen_set_dialog_phase(QWEN_DIALOG_PHASE_IDLE, "ptt_start_canceled");
        LOGI(TAG, "ptt start canceled before worker");
        return;
    }

    if ((s_qwen_ctx.state != QWEN_STATE_READY) &&
        (s_qwen_ctx.state != QWEN_STATE_PLAYING) &&
        (s_qwen_ctx.state != QWEN_STATE_THINKING)) {
        LOGW(TAG, "ptt start pending wait: state=%s", qwen_state_name(s_qwen_ctx.state));
        return;
    }

    s_qwen_ctx.is_ptt_start_pending = 0U;
    if (s_qwen_ctx.state == QWEN_STATE_PLAYING) {
        qwen_audio_stop_player();
    }
    err = qwen_audio_prepare_recorder();
    if (err != D_OK) {
        s_qwen_ctx.is_ptt_key_down = 0U;
        s_qwen_ctx.is_ptt_prepare_failed = 1U;
        qwen_finish_dialog_failed("ptt_prepare_failed", err);
        LOGW(TAG, "ptt recorder prepare failed: err=%d", err);
        return;
    }

    s_qwen_ctx.is_ptt_prepare_failed = 0U;
    if (s_qwen_ctx.is_ptt_key_down == 0U) {
        qwen_audio_stop_recorder();
        qwen_set_dialog_phase(QWEN_DIALOG_PHASE_IDLE, "ptt_start_canceled");
        LOGI(TAG, "ptt start canceled after recorder ready");
        return;
    }
    err = qwen_service_speech_start();
    if (err != D_OK) {
        s_qwen_ctx.is_ptt_key_down = 0U;
        qwen_finish_dialog_failed("ptt_start_rejected", err);
        LOGW(TAG, "ptt key down rejected: err=%d", err);
    }
}

#if (D_QWEN_DIALOG_MODE == D_QWEN_DIALOG_MODE_PUSH2TALK)
static uint8_t qwen_prepare_wakeup_reconnect(void)
{
    if ((s_qwen_ctx.is_wifi_connected == 0U) || (qwen_wss_is_connected() != 0)) {
        return 0U;
    }

    if (qwen_service_session_start() != D_OK) return 1U;
    s_qwen_ctx.want_wss_connected = 1U;
    s_qwen_ctx.is_mmi_reinit_pending = 1U;
    s_qwen_ctx.is_ptt_key_down = 1U;
    s_qwen_ctx.is_ptt_start_pending = 1U;
    s_qwen_ctx.is_ptt_prepare_failed = 0U;
    qwen_set_state(QWEN_STATE_CONNECTING);
    qwen_schedule_work("wakeup_reconnect");
    return 1U;
}
#endif

/* Preparation/auth run outside the lock, but admission and the in-flight
 * marker share the maintenance lock. The sole worker still owns cleanup. */
static uint8_t qwen_worker_prepare_begin(void)
{
    uint8_t allowed;
    spin_lock(&s_qwen_session_lock);
    allowed = !s_session.cleanup_pending && !s_session.fatal && !s_qwen_ctx.worker_exit;
#if D_FEATURE_OTA
    allowed = allowed && !s_maintenance_hold;
#endif
    if (allowed) s_qwen_ctx.worker_prepare_active = 1U;
    spin_unlock(&s_qwen_session_lock);
    return allowed;
}

static void qwen_worker_prepare_end(void)
{
    spin_lock(&s_qwen_session_lock);
    s_qwen_ctx.worker_prepare_active = 0U;
    spin_unlock(&s_qwen_session_lock);
}

static void qwen_service_task(void *priv)
{
    d_err_t err;
    int pend_result;
    uint32_t wait_ticks;
    uint32_t generation;
    qwen_service_snapshot_t snapshot;
    uint8_t runtime_allowed;

    (void)priv;
    LOGI(TAG, "worker start");
    while (!s_qwen_ctx.worker_exit) {
        (void)qwen_service_get_snapshot(&snapshot);
        wait_ticks = snapshot.session.wifi_ready ?
                     qwen_ms_to_ticks(QWEN_SERVICE_AUTH_POLL_MS) : 0U;
        if (qwen_dialog_phase_is_active())
            wait_ticks = qwen_ms_to_ticks(QWEN_SERVICE_WORK_POLL_MS);
        pend_result = os_sem_pend(&s_qwen_ctx.work_sem, (int)wait_ticks);
        if (s_qwen_ctx.worker_exit) break;

        /* 1. 先处理清理和超时；清理未完成时不能进入下一轮准备。 */
        if (qwen_session_cleanup_pending()) {
            if (s_qwen_ctx.cleanup_retry && !s_session.fatal) {
                s_qwen_ctx.cleanup_retry = 0U;
                (void)qwen_worker_cleanup();
            }
            continue;
        }
        qwen_check_dialog_timeout();
        if (qwen_check_idle_timeout() || qwen_session_cleanup_pending()) continue;
        if (!s_qwen_ctx.is_wifi_connected) continue;
#if D_FEATURE_POWER_CONTROL
        if (power_ctrl_is_shutting_down()) continue;
#endif

        /* 2. 确认运行时可用，再准备SDK与鉴权；准备本身不产生启动请求。 */
        runtime_allowed = (uint8_t)qwen_net_is_runtime_allowed();
        spin_lock(&s_qwen_session_lock);
        qwen_session_policy_runtime(&s_session, runtime_allowed);
        spin_unlock(&s_qwen_session_lock);
        if (!runtime_allowed || qwen_session_cleanup_pending()) {
            if (qwen_session_cleanup_pending()) {
                s_qwen_ctx.cleanup_retry = 1U;
                qwen_schedule_work("runtime_cleanup");
            }
            continue;
        }
        if (pend_result == OS_TIMEOUT && qwen_dialog_phase_is_active()) continue;
        qwen_prepare_mmi_reinit_if_needed();
        if (qwen_session_cleanup_pending()) continue;
        qwen_restore_idle_wakeup();
        generation = qwen_session_generation();
#if D_QWEN_HAL_ACCEPT_TEST_ENABLE
        if (d_crypto_timestamp_inited() == 0) continue;
        err = qwen_run_hal_accept_test_if_needed();
        if (err != D_OK) qwen_post_error_event((int32_t)err);
        continue;
#endif
        if (!qwen_worker_prepare_begin()) continue;
        s_qwen_ctx.preparing_sdk = 1U;
        err = qwen_prepare_network();
        s_qwen_ctx.preparing_sdk = 0U;
        if (err == D_OK && d_crypto_timestamp_inited() == 0) {
            qwen_worker_prepare_end();
            continue;
        }
        if (err == D_OK) err = qwen_sync_auth_for_worker(0U);
        qwen_worker_prepare_end();
        /* 3. 耗时步骤返回后核对代次，丢弃已取消或已断网请求的结果。 */
        if (generation != qwen_session_generation() ||
            qwen_session_cleanup_pending() || !s_qwen_ctx.is_wifi_connected) {
            continue;
        }
#if D_FEATURE_POWER_CONTROL
        if (power_ctrl_is_shutting_down()) continue;
#endif
        if (err != D_OK) {
            spin_lock(&s_qwen_session_lock);
            qwen_session_policy_failed(&s_session, err);
            spin_unlock(&s_qwen_session_lock);
            s_qwen_ctx.cleanup_retry = 1U;
            qwen_post_error_event((int32_t)err);
            qwen_schedule_work("auth_failed");
            continue;
        }
        spin_lock(&s_qwen_session_lock);
        qwen_session_policy_auth(&s_session, 1U);
        s_qwen_ctx.want_wss_connected = qwen_session_policy_allows(&s_session);
        spin_unlock(&s_qwen_session_lock);
        /* 4. 无请求则保持本地待机；有效请求才交接麦克风并连接会话。 */
        if (!s_qwen_ctx.want_wss_connected) {
            qwen_set_state(QWEN_STATE_READY);
            continue;
        }
        qwen_connect_wss_if_needed();
#if (D_QWEN_DIALOG_MODE == D_QWEN_DIALOG_MODE_PUSH2TALK)
        if (qwen_session_allows_business()) qwen_handle_pending_ptt_start();
#endif
        if (qwen_session_allows_business()) qwen_handle_ready_cue_if_needed();
    }
    spin_lock(&s_qwen_session_lock);
    qwen_session_policy_cleanup(&s_session, 0U);
    spin_unlock(&s_qwen_session_lock);
    (void)qwen_worker_cleanup();
    LOGI(TAG, "worker exit");
    s_qwen_ctx.worker_task_pid = 0;
}

static void qwen_handle_data_init(void)
{
    s_qwen_ctx.mmi_data_initialized = 1U;
    LOGI(TAG, "mmi data init");
    if (s_qwen_ctx.cleanup_active || qwen_session_cleanup_pending()) return;
    s_qwen_ctx.is_text_request_ready = 0U;
    qwen_set_dialog_phase(QWEN_DIALOG_PHASE_IDLE, "data_init");
    /* Network/provision policy owns AUTO intent; SDK rewarming is passive. */
    qwen_schedule_work("mmi_data_init");
}

static void qwen_handle_data_deinit(void)
{
    s_qwen_ctx.mmi_data_initialized = 0U;
    LOGI(TAG, "mmi data deinit");
    if (s_qwen_ctx.cleanup_active || s_qwen_ctx.preparing_sdk) return;
    s_qwen_ctx.is_speech_active = 0U;
    s_qwen_ctx.is_ptt_key_down = 0U;
    s_qwen_ctx.is_ptt_start_pending = 0U;
    s_qwen_ctx.want_wss_connected = 0U;
    s_qwen_ctx.ready_cue_pending = 0U;
    s_qwen_ctx.is_text_request_ready = 0U;
    qwen_set_dialog_phase(QWEN_DIALOG_PHASE_IDLE, "data_deinit");
    qwen_request_cleanup("mmi_data_deinit", 0U);
    qwen_set_state(QWEN_STATE_IDLE);
}

static int32_t qwen_on_mmi_event(uint32_t event, void *param)
{
    const char *text;

    text = (const char *)param;
    qwen_ctx_lock();
    if (s_session.fatal ||
        (s_qwen_ctx.cleanup_active && event != C_MMI_EVENT_DATA_DEINIT)) {
        qwen_ctx_unlock();
        return UTIL_SUCCESS;
    }
    if (event != C_MMI_EVENT_USER_CONFIG && event != C_MMI_EVENT_DATA_INIT &&
        event != C_MMI_EVENT_DATA_DEINIT && !qwen_session_allows_business()) {
        qwen_ctx_unlock();
        return UTIL_SUCCESS;
    }
    if (event != C_MMI_EVENT_HEARTBEAT) {
        qwen_mark_activity();
    }
    qwen_log_dispatch_event(QWEN_DISPATCH_EVT_MMI, event);
    LOGI(TAG, "mmi event=%s(%u)", qwen_mmi_event_name(event), (unsigned)event);

    switch (event) {
    case C_MMI_EVENT_USER_CONFIG:
        c_mmi_reset_dialog_id();
        break;
    case C_MMI_EVENT_DATA_INIT:
        s_qwen_ctx.is_mmi_reinit_pending = 0U;
        qwen_handle_data_init();
        break;
    case C_MMI_EVENT_SPEECH_READY:
        s_qwen_ctx.is_text_request_ready = 0U;
        s_qwen_ctx.is_wss_reconnect_pending = 0U;
        s_qwen_ctx.wss_reconnect_delay_ms = QWEN_WSS_RECONNECT_DELAY_MS;
        qwen_resume_heartbeat();
        if (qwen_dialog_phase_is_active() == 0U) {
            qwen_set_state(QWEN_STATE_READY);
        } else {
            LOGI(TAG, "speech ready keep state=%s phase=%s round=%u",
                 qwen_state_name(s_qwen_ctx.state),
                 qwen_dialog_phase_name(s_qwen_ctx.dialog_phase),
                 (unsigned)s_qwen_ctx.dialog_round_id);
        }
#if (D_QWEN_DIALOG_MODE == D_QWEN_DIALOG_MODE_PUSH2TALK)
        if (s_qwen_ctx.is_ptt_start_pending != 0U) {
            qwen_schedule_work("speech_ready_ptt");
        }
#endif
        break;
    case C_MMI_EVENT_SPEECH_PREPARE:
        if (s_qwen_ctx.is_idle_goodbye_pending != 0U) {
            s_qwen_ctx.is_text_request_ready = 0U;
            s_qwen_ctx.ready_cue_pending = 0U;
            s_qwen_ctx.ready_cue_sent = 1U;
            LOGI(TAG, "idle goodbye speech prepare ignored: state=%s phase=%s round=%u",
                 qwen_state_name(s_qwen_ctx.state),
                 qwen_dialog_phase_name(s_qwen_ctx.dialog_phase),
                 (unsigned)s_qwen_ctx.dialog_round_id);
            break;
        }
        s_qwen_ctx.is_text_request_ready = 1U;
        if (qwen_dialog_phase_is_active() == 0U) {
            qwen_set_state(QWEN_STATE_READY);
        } else {
            LOGI(TAG, "speech prepare keep state=%s phase=%s round=%u text_req=%u",
                 qwen_state_name(s_qwen_ctx.state),
                 qwen_dialog_phase_name(s_qwen_ctx.dialog_phase),
                 (unsigned)s_qwen_ctx.dialog_round_id,
                 (unsigned)s_qwen_ctx.is_text_request_ready);
        }
        if ((s_qwen_ctx.ready_cue_sent == 0U) && (s_qwen_ctx.ready_cue_pending == 0U) &&
            (s_qwen_ctx.is_ptt_key_down == 0U)) {
#if (D_QWEN_DIALOG_MODE == D_QWEN_DIALOG_MODE_DUPLEX)
            s_qwen_ctx.ready_cue_pending = 1U;
            LOGI(TAG, "ready cue pending");
            qwen_schedule_work("ready_cue_pending");
#else
            if (qwen_dialog_phase_is_active() == 0U) {
                s_qwen_ctx.ready_cue_pending = 1U;
                LOGI(TAG, "ready cue pending");
                qwen_schedule_work("ready_cue_pending");
            }
#endif
        }
#if (D_QWEN_DIALOG_MODE == D_QWEN_DIALOG_MODE_PUSH2TALK)
        if (s_qwen_ctx.is_ptt_start_pending != 0U) {
            qwen_schedule_work("speech_prepare_ptt");
        }
#endif
        break;
    case C_MMI_EVENT_SPEECH_START:
    case C_MMI_EVENT_SPEECH_RESTART:
#if (D_QWEN_DIALOG_MODE == D_QWEN_DIALOG_MODE_DUPLEX)
        if ((s_qwen_ctx.is_idle_goodbye_pending == 0U) &&
            (s_qwen_ctx.ready_cue_pending != 0U) &&
            (s_qwen_ctx.ready_cue_sent == 0U)) {
            qwen_schedule_work("ready_cue_first");
        }
#endif
        if (qwen_dialog_phase_can_accept_speech_start() == 0U) {
            LOGI(TAG, "speech start ignored: state=%s phase=%s round=%u",
                 qwen_state_name(s_qwen_ctx.state),
                 qwen_dialog_phase_name(s_qwen_ctx.dialog_phase),
                 (unsigned)s_qwen_ctx.dialog_round_id);
            break;
        }
#if (D_QWEN_DIALOG_MODE == D_QWEN_DIALOG_MODE_DUPLEX)
        if (s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_WAIT_TTS) {
            s_qwen_ctx.is_speech_active = 1U;
            qwen_audio_start_recorder();
            LOGI(TAG, "speech start keep tts wait");
            break;
        }
        if ((event == C_MMI_EVENT_SPEECH_RESTART) ||
            (s_qwen_ctx.dialog_phase != QWEN_DIALOG_PHASE_LISTENING)) {
            s_qwen_ctx.dialog_round_id++;
        }
        if (event == C_MMI_EVENT_SPEECH_RESTART) {
            qwen_audio_stop_player();
        }
#endif
        s_qwen_ctx.is_speech_active = 1U;
#if (D_QWEN_DIALOG_MODE == D_QWEN_DIALOG_MODE_DUPLEX)
        if ((s_qwen_ctx.ready_cue_pending == 0U) || (s_qwen_ctx.ready_cue_sent != 0U)) {
            s_qwen_ctx.is_text_request_ready = 0U;
        }
#else
        s_qwen_ctx.is_text_request_ready = 0U;
#endif
        qwen_set_dialog_phase(QWEN_DIALOG_PHASE_LISTENING, "mmi_speech_start");
        qwen_set_state(QWEN_STATE_LISTENING);
        qwen_audio_start_recorder();
        break;
    case C_MMI_EVENT_ASR_START:
        if (qwen_idle_goodbye_ignore_asr(event) != 0U) {
            break;
        }
#if (D_QWEN_DIALOG_MODE == D_QWEN_DIALOG_MODE_DUPLEX)
        qwen_audio_stop_player();
#endif
        break;
    case C_MMI_EVENT_HEARTBEAT:
        break;
    case C_MMI_EVENT_ASR_INCOMPLETE:
        if (qwen_idle_goodbye_ignore_asr(event) != 0U) {
            break;
        }
        if ((text != NULL) && (text[0] != '\0')) {
            qwen_log_final_text("asr_partial", text);
        }
        break;
    case C_MMI_EVENT_ASR_COMPLETE:
        if (qwen_idle_goodbye_ignore_asr(event) != 0U) {
            break;
        }
        if (qwen_dialog_phase_can_accept_asr_text() == 0U) {
            LOGI(TAG, "asr final ignored: state=%s phase=%s round=%u",
                 qwen_state_name(s_qwen_ctx.state),
                 qwen_dialog_phase_name(s_qwen_ctx.dialog_phase),
                 (unsigned)s_qwen_ctx.dialog_round_id);
            break;
        }
        qwen_log_final_text("asr_final", text);
        qwen_post_text_event(EVT_QWEN_ASR_RESULT, "asr_final", text);
        break;
    case C_MMI_EVENT_ASR_END:
        if (qwen_idle_goodbye_ignore_asr(event) != 0U) {
            break;
        }
#if (D_QWEN_DIALOG_MODE == D_QWEN_DIALOG_MODE_DUPLEX)
        if (qwen_dialog_phase_is_active() == 0U) {
            LOGI(TAG, "asr end ignored: state=%s phase=%s round=%u",
                 qwen_state_name(s_qwen_ctx.state),
                 qwen_dialog_phase_name(s_qwen_ctx.dialog_phase),
                 (unsigned)s_qwen_ctx.dialog_round_id);
            break;
        }
        s_qwen_ctx.is_speech_active = 0U;
        s_qwen_ctx.is_text_request_ready = 0U;
        qwen_set_dialog_phase(QWEN_DIALOG_PHASE_WAIT_LLM, "mmi_asr_end");
        qwen_set_state(QWEN_STATE_THINKING);
#else
        if (qwen_dialog_phase_is_ptt_flow() == 0U) {
            LOGI(TAG, "asr end ignored: state=%s phase=%s round=%u",
                 qwen_state_name(s_qwen_ctx.state),
                 qwen_dialog_phase_name(s_qwen_ctx.dialog_phase),
                 (unsigned)s_qwen_ctx.dialog_round_id);
            break;
        }
        qwen_audio_stop_recorder();
        s_qwen_ctx.is_speech_active = 0U;
        s_qwen_ctx.is_text_request_ready = 0U;
        qwen_set_dialog_phase(QWEN_DIALOG_PHASE_WAIT_LLM, "mmi_asr_end");
        qwen_set_state(QWEN_STATE_THINKING);
#endif
        break;
    case C_MMI_EVENT_LLM_INCOMPLETE:
        if (s_qwen_ctx.is_idle_goodbye_pending != 0U) {
            s_qwen_ctx.is_text_request_ready = 0U;
            LOGI(TAG, "idle goodbye llm partial ignored");
            break;
        }
        if (qwen_dialog_phase_can_accept_llm() == 0U) {
            LOGI(TAG, "llm incomplete ignored: state=%s phase=%s round=%u",
                 qwen_state_name(s_qwen_ctx.state),
                 qwen_dialog_phase_name(s_qwen_ctx.dialog_phase),
                 (unsigned)s_qwen_ctx.dialog_round_id);
            break;
        }
        s_qwen_ctx.is_text_request_ready = 0U;
        qwen_post_text_event(EVT_QWEN_LLM_PARTIAL_RESULT, "llm_partial", text);
        if (s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_WAIT_LLM) {
            qwen_set_dialog_phase(QWEN_DIALOG_PHASE_WAIT_LLM, "mmi_llm_incomplete");
            qwen_set_state(QWEN_STATE_THINKING);
        }
        break;
    case C_MMI_EVENT_LLM_COMPLETE:
        if (s_qwen_ctx.is_idle_goodbye_pending != 0U) {
            s_qwen_ctx.is_text_request_ready = 0U;
            LOGI(TAG, "idle goodbye llm complete ignored: len=%u",
                 (unsigned)((text != NULL) ? strlen(text) : 0U));
            break;
        }
        if (qwen_dialog_phase_can_accept_llm() == 0U) {
            LOGI(TAG, "llm complete ignored: state=%s phase=%s round=%u",
                 qwen_state_name(s_qwen_ctx.state),
                 qwen_dialog_phase_name(s_qwen_ctx.dialog_phase),
                 (unsigned)s_qwen_ctx.dialog_round_id);
            break;
        }
        qwen_log_final_text("llm_final", text);
        qwen_post_text_event(EVT_QWEN_LLM_RESULT, "llm_final", text);
        s_qwen_ctx.is_text_request_ready = 0U;
        if ((text == NULL) || (text[0] == '\0')) {
            LOGW(TAG, "llm complete empty: state=%s phase=%s round=%u",
                 qwen_state_name(s_qwen_ctx.state),
                 qwen_dialog_phase_name(s_qwen_ctx.dialog_phase),
                 (unsigned)s_qwen_ctx.dialog_round_id);
            if (s_qwen_ctx.dialog_phase != QWEN_DIALOG_PHASE_WAIT_LLM) {
                break;
            }
            qwen_set_dialog_phase(QWEN_DIALOG_PHASE_IDLE, "mmi_llm_empty");
            qwen_set_idle_network_state();
            break;
        }
        if (s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_WAIT_LLM) {
            qwen_set_dialog_phase(QWEN_DIALOG_PHASE_WAIT_TTS, "mmi_llm_complete");
            qwen_set_state(QWEN_STATE_THINKING);
        } else {
            LOGI(TAG, "llm complete keep state=%s phase=%s round=%u",
                 qwen_state_name(s_qwen_ctx.state),
                 qwen_dialog_phase_name(s_qwen_ctx.dialog_phase),
                 (unsigned)s_qwen_ctx.dialog_round_id);
        }
        break;
    case C_MMI_EVENT_TTS_START:
        if (s_qwen_ctx.is_idle_goodbye_pending != 0U) {
            d_event_post(EVT_QWEN_TTS_START, NULL, 0);
            qwen_audio_start_player();
            s_qwen_ctx.is_text_request_ready = 0U;
            qwen_set_dialog_phase(QWEN_DIALOG_PHASE_PLAYING,
                                  "idle_goodbye_tts_start");
            qwen_set_state(QWEN_STATE_PLAYING);
            break;
        }
        if (qwen_dialog_phase_can_accept_tts() == 0U) {
            LOGI(TAG, "tts start ignored: state=%s phase=%s round=%u",
                 qwen_state_name(s_qwen_ctx.state),
                 qwen_dialog_phase_name(s_qwen_ctx.dialog_phase),
                 (unsigned)s_qwen_ctx.dialog_round_id);
            break;
        }
        d_event_post(EVT_QWEN_TTS_START, NULL, 0);
        qwen_audio_start_player();
        s_qwen_ctx.is_text_request_ready = 0U;
#if (D_QWEN_DIALOG_MODE == D_QWEN_DIALOG_MODE_DUPLEX)
        qwen_set_dialog_phase(QWEN_DIALOG_PHASE_LISTENING, "mmi_tts_start");
        qwen_set_state(QWEN_STATE_LISTENING);
#else
        qwen_set_dialog_phase(QWEN_DIALOG_PHASE_PLAYING, "mmi_tts_start");
        qwen_set_state(QWEN_STATE_PLAYING);
#endif
        break;
    case C_MMI_EVENT_TTS_END:
        if (s_qwen_ctx.is_idle_goodbye_pending != 0U) {
            d_event_post(EVT_QWEN_TTS_END, NULL, 0);
            qwen_audio_finish_player();
            s_qwen_ctx.is_speech_active = 0U;
            s_qwen_ctx.is_text_request_ready = 0U;
            qwen_set_dialog_phase(QWEN_DIALOG_PHASE_WAIT_LOCAL_TTS_END,
                                  "idle_goodbye_tts_end");
            qwen_set_state(QWEN_STATE_PLAYING);
            break;
        }
#if (D_QWEN_DIALOG_MODE == D_QWEN_DIALOG_MODE_DUPLEX)
        if ((s_qwen_ctx.dialog_phase != QWEN_DIALOG_PHASE_PLAYING) &&
            (s_qwen_ctx.dialog_phase != QWEN_DIALOG_PHASE_LISTENING)) {
            LOGI(TAG, "tts end ignored: state=%s phase=%s round=%u",
                 qwen_state_name(s_qwen_ctx.state),
                 qwen_dialog_phase_name(s_qwen_ctx.dialog_phase),
                 (unsigned)s_qwen_ctx.dialog_round_id);
            break;
        }
        d_event_post(EVT_QWEN_TTS_END, NULL, 0);
        qwen_audio_finish_player();
        s_qwen_ctx.is_text_request_ready = 0U;
        qwen_set_dialog_phase(QWEN_DIALOG_PHASE_LISTENING, "mmi_tts_end");
        qwen_set_state(QWEN_STATE_LISTENING);
#else
        if (s_qwen_ctx.dialog_phase != QWEN_DIALOG_PHASE_PLAYING) {
            LOGI(TAG, "tts end ignored: state=%s phase=%s round=%u",
                 qwen_state_name(s_qwen_ctx.state),
                 qwen_dialog_phase_name(s_qwen_ctx.dialog_phase),
                 (unsigned)s_qwen_ctx.dialog_round_id);
            break;
        }
        d_event_post(EVT_QWEN_TTS_END, NULL, 0);
        qwen_audio_finish_player();
        s_qwen_ctx.is_speech_active = 0U;
        s_qwen_ctx.is_text_request_ready = 0U;
        qwen_set_dialog_phase(QWEN_DIALOG_PHASE_WAIT_LOCAL_TTS_END, "mmi_tts_end");
        qwen_set_state(QWEN_STATE_PLAYING);
#endif
        break;
    case C_MMI_EVENT_DATA_DEINIT:
        qwen_handle_data_deinit();
        break;
    default:
        break;
    }

    qwen_ctx_unlock();
    return UTIL_SUCCESS;
}

static void qwen_handle_wss_disconnected(void)
{
    if (!s_qwen_ctx.is_initialized || s_qwen_ctx.cleanup_active ||
        qwen_session_cleanup_pending()) return;
    if (qwen_session_allows_business() && s_qwen_ctx.is_wifi_connected) {
        s_qwen_ctx.is_wss_reconnect_pending = 1U;
        qwen_request_cleanup("wss_reconnect", 1U);
    } else {
        qwen_request_cleanup("wss_disconnected", 0U);
    }
}

static void qwen_on_wifi_event(const d_event_t *event, void *user_data)
{
    (void)user_data;
    if (event == NULL) {
        return;
    }

    qwen_ctx_lock();
    qwen_log_dispatch_event(QWEN_DISPATCH_EVT_WIFI, (uint32_t)event->id);
    qwen_mark_activity();
    LOGI(TAG, "event=%s(%u)", qwen_service_event_name(event->id), (unsigned)event->id);
    if (event->id == EVT_WIFI_CONNECTED) {
        spin_lock(&s_qwen_session_lock);
        s_qwen_ctx.wifi_revision++;
        qwen_session_policy_network(&s_session, 1U);
        spin_unlock(&s_qwen_session_lock);
        s_qwen_ctx.is_wifi_connected = 1U;
        s_qwen_ctx.is_wifi_reconfiguring = 0U;
        qwen_schedule_work("wifi_connected");
        qwen_ctx_unlock();
        return;
    }

    if (event->id == EVT_WIFI_DISCONNECTED) {
        spin_lock(&s_qwen_session_lock);
        s_qwen_ctx.wifi_revision++;
        qwen_session_policy_network(&s_session, 0U);
        spin_unlock(&s_qwen_session_lock);
        s_qwen_ctx.is_wifi_connected = 0U;
        s_qwen_ctx.want_wss_connected = 0U;
        s_qwen_ctx.is_wss_reconnect_pending = 0U;
        s_qwen_ctx.is_mmi_reinit_pending = 0U;
        s_qwen_ctx.is_ptt_key_down = 0U;
        s_qwen_ctx.is_ptt_start_pending = 0U;
        qwen_set_dialog_phase(QWEN_DIALOG_PHASE_IDLE, "wifi_disconnected");
        qwen_request_cleanup("wifi_disconnected", 0U);
    }
    qwen_ctx_unlock();
}

static d_err_t qwen_service_set_device_volume(uint8_t volume_percent,
                                               void *context)
{
    d_err_t result;
    (void)context;
    if (qwen_audio_is_initialized() == 0U) {
        return D_ERR_NOT_READY;
    }

    result = qwen_audio_set_player_volume(volume_percent);
#if D_LOCAL_PROMPT_ENABLE
    if (result == D_OK) {
        d_local_prompt_snapshot_t prompt;
        d_local_prompt_get_snapshot(&prompt);
        /* An optional cue startup failure must not disable Qwen's real volume. */
        if (prompt.ready) result = d_local_prompt_apply_volume(volume_percent, D_DEVICE_VOLUME_TIMEOUT_MS);
        else result = d_local_prompt_wait_stopped(0U);
    }
#endif
    return result;
}

static const d_device_volume_backend_t s_qwen_volume_backend = {
    .set_volume = qwen_service_set_device_volume,
    .context = NULL,
};

static d_err_t qwen_service_unregister_volume_backend(void)
{
    d_err_t err = D_ERR_BUSY;
    uint8_t attempt;

    for (attempt = 0U; attempt < QWEN_VOLUME_BACKEND_RELEASE_RETRIES; ++attempt) {
        err = d_device_volume_unregister_backend(&s_qwen_volume_backend);
        if (err != D_ERR_BUSY) {
            break;
        }
        if ((attempt + 1U) < QWEN_VOLUME_BACKEND_RELEASE_RETRIES) {
            os_time_dly(QWEN_VOLUME_BACKEND_RELEASE_DELAY_TICKS);
        }
    }

    if (err == D_ERR_NOT_FOUND) {
        return D_OK;
    }
    if (err != D_OK) {
        LOGE(TAG, "Qwen volume backend release failed: %d", err);
    }

    return err;
}

static d_err_t qwen_service_deinit_audio(void)
{
    d_err_t err;

    err = qwen_service_unregister_volume_backend();
    if (err != D_OK) {
        return err;
    }

    qwen_audio_deinit();
    return D_OK;
}

static d_err_t qwen_service_init_audio(void)
{
    uint8_t had_audio = qwen_audio_is_initialized();
    d_err_t result;
    if (qwen_audio_init() != D_OK) {
        return D_ERR_IO;
    }

    result = d_device_volume_register_backend(&s_qwen_volume_backend);
    if (result != D_OK) {
        /* A BUSY backend may still be using the audio object. Preserve its owner. */
        if (qwen_service_unregister_volume_backend() == D_OK && !had_audio) {
            qwen_audio_deinit();
        }
        return result;
    }

    return D_OK;
}

static d_err_t qwen_service_subscribe_events(void)
{
    if ((d_event_subscribe(EVT_WIFI_CONNECTED, qwen_on_wifi_event, NULL) != 0) ||
        (d_event_subscribe(EVT_WIFI_DISCONNECTED, qwen_on_wifi_event, NULL) != 0) ||
        (d_event_subscribe(EVT_NTP_SYNCED, qwen_on_time_event, NULL) != 0) ||
#if D_DEVICE_CLOUD_ENABLE
        (d_event_subscribe(EVT_PROVISION_OK, qwen_on_provision_event, NULL) != 0) ||
#endif
        (d_event_subscribe(EVT_QWEN_TTS_LOCAL_END, qwen_on_audio_event, NULL) != 0) ||
        (d_event_subscribe(EVT_QWEN_CMD_RECEIVED, qwen_on_cmd_event, NULL) != 0) ||
        (d_event_subscribe(EVT_QWEN_WSS_DISCONNECTED, qwen_on_wss_event, NULL) != 0)) {
        return D_ERR_IO;
    }
#if D_FEATURE_XIAOLE_WAKEUP
    if (d_event_subscribe(EVT_XIAOLE_WAKEUP_DETECTED, qwen_on_xiaole_event, NULL) != 0) {
        return D_ERR_IO;
    }
#endif
    return D_OK;
}

#if D_LOCAL_PROMPT_ENABLE
static uint8_t qwen_local_prompt_busy(void)
{
    qwen_service_snapshot_t snapshot;
    if (qwen_service_get_snapshot(&snapshot) != D_OK) return 0U;
    /* can_stop includes pending/failed cleanup; a remembered auth error alone owns no audio. */
    return snapshot.session.requested || snapshot.session.connected ||
        snapshot.session.fatal || snapshot.session.can_stop;
}

static d_err_t qwen_alarm_handoff_begin(uint32_t token)
{
    if (!token) return D_ERR_INVALID_ARG;
    spin_lock(&s_qwen_session_lock);
    if (!s_qwen_ctx.service_ready) { spin_unlock(&s_qwen_session_lock); return D_ERR_NOT_READY; }
    if (s_session.fatal) { spin_unlock(&s_qwen_session_lock); return D_ERR_IO; }
    if (s_alarm_hold_token == token) { spin_unlock(&s_qwen_session_lock); return D_OK; }
    if (s_alarm_hold_token && (uint32_t)(token - s_alarm_hold_token) >= 0x80000000U) {
        spin_unlock(&s_qwen_session_lock); return D_ERR_BUSY;
    }
    s_alarm_hold_token = token;
    if (!s_alarm_suspended_auto) {
        s_alarm_saved_auto = s_session.automatic;
        s_alarm_suspended_auto = 1U;
    }
    /* Network readiness must not recreate a conversation after this explicit interruption. */
    s_session.automatic = 0U;
    /* Force the complete worker path even when session_stop() would return early in IDLE. */
    qwen_session_policy_cleanup(&s_session, 0U);
    /* This token is a new explicit retry; the old failure is not its completion result. */
    s_session.cleanup_failed = 0U;
    s_session.cleanup_error = D_OK;
    s_qwen_ctx.cleanup_retry = 1U;
    s_qwen_ctx.is_wss_reconnect_pending = 0U;
    spin_unlock(&s_qwen_session_lock);
    qwen_schedule_work("alarm_audio_claim");
    return D_OK;
}

static d_err_t qwen_alarm_handoff_poll(uint32_t token, uint8_t *ready)
{
    d_err_t result = D_OK;
    uint8_t complete;
    if (ready == NULL) return D_ERR_INVALID_ARG;
    *ready = 0U;
    spin_lock(&s_qwen_session_lock);
    if (!token || token != s_alarm_hold_token) result = D_ERR_BUSY;
    else if (!s_qwen_ctx.service_ready) result = D_ERR_NOT_READY;
    else if (s_session.fatal) result = D_ERR_IO;
    else if (s_session.cleanup_failed) result = s_session.cleanup_error;
    complete = result == D_OK && !s_session.cleanup_pending && !s_qwen_ctx.cleanup_active &&
        !s_session.requested && !s_session.connected && !s_qwen_ctx.sdk_calls;
    spin_unlock(&s_qwen_session_lock);
#if D_FEATURE_XIAOLE_WAKEUP
    if (complete && xiaole_wakeup_is_running()) return D_ERR_IO;
#endif
    *ready = complete;
    return result;
}

static void qwen_alarm_handoff_release(uint32_t token)
{
    uint8_t released = 0U;
    spin_lock(&s_qwen_session_lock);
    if (token && token == s_alarm_hold_token) { s_alarm_hold_token = 0U; released = 1U; }
    spin_unlock(&s_qwen_session_lock);
    if (released) qwen_schedule_work("alarm_audio_released");
}

static const d_local_prompt_handoff_t s_qwen_alarm_handoff = {
    qwen_alarm_handoff_begin, qwen_alarm_handoff_poll, qwen_alarm_handoff_release,
};
#endif

#if D_FEATURE_OTA
d_err_t qwen_service_maintenance_begin(void)
{
    spin_lock(&s_qwen_session_lock);
    if (s_maintenance_hold) { spin_unlock(&s_qwen_session_lock); return D_OK; }
    if (!s_qwen_ctx.service_ready || s_session.fatal) {
        spin_unlock(&s_qwen_session_lock); return D_ERR_NOT_READY;
    }
    s_maintenance_hold = 1U;
    s_maintenance_saved_auto = s_session.automatic;
#if D_LOCAL_PROMPT_ENABLE
    if (s_alarm_suspended_auto) s_maintenance_saved_auto = s_alarm_saved_auto;
#endif
    s_maintenance_suspended_auto = 1U;
    s_session.automatic = 0U;
    qwen_session_policy_cleanup(&s_session, 0U);
    s_session.cleanup_failed = 0U;
    s_session.cleanup_error = D_OK;
    s_qwen_ctx.cleanup_retry = 1U;
    s_qwen_ctx.is_wss_reconnect_pending = 0U;
    spin_unlock(&s_qwen_session_lock);
    qwen_schedule_work("ota_maintenance");
    return D_OK;
}

d_err_t qwen_service_maintenance_poll(uint8_t *ready)
{
    d_err_t result = D_OK;
    uint8_t complete;
    if (!ready) return D_ERR_INVALID_ARG;
    *ready = 0U;
    spin_lock(&s_qwen_session_lock);
    if (!s_maintenance_hold) result = D_ERR_NOT_READY;
    else if (s_session.fatal) result = D_ERR_IO;
    else if (s_session.cleanup_failed) result = s_session.cleanup_error;
    complete = result == D_OK && !s_session.cleanup_pending && !s_qwen_ctx.cleanup_active &&
        !s_session.requested && !s_session.connected && !s_qwen_ctx.sdk_calls &&
        !s_qwen_ctx.worker_prepare_active;
    spin_unlock(&s_qwen_session_lock);
#if D_FEATURE_XIAOLE_WAKEUP
    if (complete && xiaole_wakeup_is_running()) return D_ERR_BUSY;
#endif
    *ready = complete;
    return result;
}

d_err_t qwen_service_maintenance_release(void)
{
    uint8_t ready;
    d_err_t result = qwen_service_maintenance_poll(&ready);
    if (result != D_OK || !ready) return result != D_OK ? result : D_ERR_BUSY;
    spin_lock(&s_qwen_session_lock); s_maintenance_hold = 0U; spin_unlock(&s_qwen_session_lock);
    /* Only standby eligibility returns. Automatic conversation remains disabled
     * until the user explicitly starts a new session. */
    qwen_schedule_work("ota_released");
    return D_OK;
}
#endif

d_err_t qwen_service_init(void)
{
    d_err_t err;
    wifi_service_status_t wifi_status;
    uint32_t wifi_revision;

    if (s_qwen_ctx.is_initialized != 0U) {
        return D_ERR_BUSY;
    }
#if D_LOCAL_PROMPT_ENABLE
    if (d_local_prompt_register_foreground_probe(qwen_local_prompt_busy) != D_OK) return D_ERR_BUSY;
    if (d_local_prompt_register_alarm_handoff(&s_qwen_alarm_handoff) != D_OK) return D_ERR_BUSY;
    s_alarm_hold_token = 0U;
    s_alarm_suspended_auto = 0U;
#endif

    memset(&s_qwen_ctx, 0, sizeof(s_qwen_ctx));
    qwen_session_policy_init(&s_session,
        D_QWEN_SESSION_START_POLICY == D_QWEN_SESSION_START_AUTO);
    qwen_session_policy_runtime(&s_session, (uint8_t)qwen_net_is_runtime_allowed());
    s_qwen_ctx.wss_reconnect_delay_ms = QWEN_WSS_RECONNECT_DELAY_MS;
    s_qwen_ctx.last_activity_ms = (uint32_t)timer_get_ms();
    if (os_mutex_create(&s_qwen_ctx.state_mutex) != OS_NO_ERR) {
        return D_ERR_IO;
    }
    qwen_set_state(QWEN_STATE_IDLE);
    if (qwen_service_init_audio() != D_OK) {
        os_mutex_del(&s_qwen_ctx.state_mutex, OS_DEL_ALWAYS);
        memset(&s_qwen_ctx, 0, sizeof(s_qwen_ctx));
        return D_ERR_IO;
    }
    if (os_sem_create(&s_qwen_ctx.work_sem, 0) != OS_NO_ERR) {
        os_mutex_del(&s_qwen_ctx.state_mutex, OS_DEL_ALWAYS);
        (void)qwen_service_deinit_audio();
        memset(&s_qwen_ctx, 0, sizeof(s_qwen_ctx));
        return D_ERR_IO;
    }
    qwen_wss_set_disconnect_callback(qwen_post_wss_disconnected);
    if (thread_fork("qwen_svc", QWEN_SERVICE_TASK_PRIO, QWEN_SERVICE_TASK_STACK, 0,
                    &s_qwen_ctx.worker_task_pid, qwen_service_task, NULL) != OS_NO_ERR) {
        qwen_wss_set_disconnect_callback(NULL);
        os_sem_del(&s_qwen_ctx.work_sem, OS_DEL_ALWAYS);
        os_mutex_del(&s_qwen_ctx.state_mutex, OS_DEL_ALWAYS);
        (void)qwen_service_deinit_audio();
        memset(&s_qwen_ctx, 0, sizeof(s_qwen_ctx));
        return D_ERR_IO;
    }
    s_qwen_ctx.is_initialized = 1U;
    err = qwen_service_subscribe_events();
    if (err != D_OK) {
        LOGE(TAG, "Qwen event subscription failed: %d", err);
        qwen_service_deinit();
        return err;
    }
    spin_lock(&s_qwen_session_lock);
    wifi_revision = s_qwen_ctx.wifi_revision;
    spin_unlock(&s_qwen_session_lock);
    wifi_service_get_status(&wifi_status);
    spin_lock(&s_qwen_session_lock);
    if (wifi_revision == s_qwen_ctx.wifi_revision) {
        s_qwen_ctx.is_wifi_connected = wifi_status.state == WIFI_SERVICE_CONN_CONNECTED;
        qwen_session_policy_network(&s_session, s_qwen_ctx.is_wifi_connected);
    }
    spin_unlock(&s_qwen_session_lock);
    if (s_qwen_ctx.is_wifi_connected) qwen_schedule_work("initial_wifi_snapshot");
#if D_FEATURE_POWER_CONTROL
    power_ctrl_set_prepare_shutdown_callback(qwen_service_prepare_shutdown, NULL);
#endif
    err = d_key_register(D_QWEN_PTT_KEY_VALUE, qwen_ptt_handler, NULL);
    if (err != D_OK) {
        LOGE(TAG, "Qwen key register failed: key=%u err=%d", (unsigned)D_QWEN_PTT_KEY_VALUE, err);
        qwen_service_deinit();
        return err;
    }
    s_qwen_ctx.service_ready = 1U;
    qwen_schedule_work("service_ready");
    LOGI(TAG, "Qwen service init done, ptt_key=%u", (unsigned)D_QWEN_PTT_KEY_VALUE);

    return D_OK;
}

void qwen_service_deinit(void)
{
    if (s_qwen_ctx.is_initialized == 0U) {
        return;
    }
    if (qwen_service_unregister_volume_backend() != D_OK) {
        return;
    }
    s_qwen_ctx.service_ready = 0U;

    d_event_unsubscribe(EVT_WIFI_CONNECTED, qwen_on_wifi_event);
    d_event_unsubscribe(EVT_WIFI_DISCONNECTED, qwen_on_wifi_event);
    d_event_unsubscribe(EVT_NTP_SYNCED, qwen_on_time_event);
#if D_DEVICE_CLOUD_ENABLE
    d_event_unsubscribe(EVT_PROVISION_OK, qwen_on_provision_event);
#endif
    d_event_unsubscribe(EVT_QWEN_TTS_LOCAL_END, qwen_on_audio_event);
    d_event_unsubscribe(EVT_QWEN_CMD_RECEIVED, qwen_on_cmd_event);
    d_event_unsubscribe(EVT_QWEN_WSS_DISCONNECTED, qwen_on_wss_event);
#if D_FEATURE_XIAOLE_WAKEUP
    d_event_unsubscribe(EVT_XIAOLE_WAKEUP_DETECTED, qwen_on_xiaole_event);
#endif
#if D_FEATURE_POWER_CONTROL
    power_ctrl_set_prepare_shutdown_callback(NULL, NULL);
#endif
    d_key_unregister(D_QWEN_PTT_KEY_VALUE);
    qwen_wss_set_disconnect_callback(NULL);
    s_qwen_ctx.worker_exit = 1U;
    s_qwen_ctx.is_wifi_connected = 0U;
    s_qwen_ctx.want_wss_connected = 0U;
    qwen_schedule_work("service_deinit");
    if (s_qwen_ctx.worker_task_pid != 0) {
        thread_kill(&s_qwen_ctx.worker_task_pid, KILL_WAIT);
        s_qwen_ctx.worker_task_pid = 0;
    }
    if (qwen_session_cleanup_pending()) {
        LOGE(TAG, "service teardown incomplete; retain callback state until restart");
        return;
    }
    if (os_sem_valid(&s_qwen_ctx.work_sem) != 0) {
        os_sem_del(&s_qwen_ctx.work_sem, OS_DEL_ALWAYS);
    }
    qwen_audio_deinit();
    if (os_mutex_valid(&s_qwen_ctx.state_mutex) != 0) {
        os_mutex_del(&s_qwen_ctx.state_mutex, OS_DEL_ALWAYS);
    }
    memset(&s_qwen_ctx, 0, sizeof(s_qwen_ctx));
}

static d_err_t qwen_session_start_checked(const uint32_t *expected_generation)
{
    d_err_t result;

    if (!s_qwen_ctx.service_ready) return D_ERR_NOT_READY;
#if D_LOCAL_PROMPT_ENABLE
    if (d_local_prompt_alarm_requested()) return D_ERR_BUSY;
#endif
    spin_lock(&s_qwen_session_lock);
    if (expected_generation != NULL && *expected_generation != s_session.generation) {
        spin_unlock(&s_qwen_session_lock);
        return D_ERR_NOT_READY;
    }
#if D_FEATURE_OTA
    if (s_maintenance_hold) { spin_unlock(&s_qwen_session_lock); return D_ERR_BUSY; }
#endif
#if D_LOCAL_PROMPT_ENABLE
    if (s_alarm_hold_token) { spin_unlock(&s_qwen_session_lock); return D_ERR_BUSY; }
#endif
    result = qwen_session_policy_start(&s_session);
#if D_FEATURE_OTA
    if (result == D_OK && s_maintenance_suspended_auto) {
        s_session.automatic = s_maintenance_saved_auto;
        s_maintenance_suspended_auto = 0U;
    }
#endif
#if D_LOCAL_PROMPT_ENABLE
    if (result == D_OK && s_alarm_suspended_auto) {
        s_session.automatic = s_alarm_saved_auto;
        s_alarm_suspended_auto = 0U;
    }
#endif
    spin_unlock(&s_qwen_session_lock);
    if (result == D_OK) {
#if D_LOCAL_PROMPT_ENABLE
        d_local_prompt_foreground_begin();
#endif
        qwen_schedule_work("session_start");
    }
    return result;
}

d_err_t qwen_service_session_start(void)
{
    return qwen_session_start_checked(NULL);
}

d_err_t qwen_service_session_stop(void)
{
    d_err_t result;
    uint8_t should_wake = 0U;

    if (!s_qwen_ctx.service_ready) return D_ERR_NOT_READY;
    spin_lock(&s_qwen_session_lock);
    result = qwen_session_policy_stop(&s_session);
    if (result == D_OK && s_session.cleanup_pending) {
        s_qwen_ctx.cleanup_retry = 1U;
        should_wake = 1U;
    }
    spin_unlock(&s_qwen_session_lock);
    if (should_wake) qwen_schedule_work("session_stop");
    return result;
}

d_err_t qwen_service_get_snapshot(qwen_service_snapshot_t *snapshot)
{
    if (snapshot == NULL) return D_ERR_INVALID_ARG;
    memset(snapshot, 0, sizeof(*snapshot));
    spin_lock(&s_qwen_session_lock);
    if (!s_qwen_ctx.service_ready) {
        spin_unlock(&s_qwen_session_lock);
        return D_ERR_NOT_READY;
    }
    qwen_session_policy_snapshot(&s_session, &snapshot->session);
    snapshot->state = s_qwen_ctx.state;
#if D_LOCAL_PROMPT_ENABLE
    if (s_alarm_hold_token) snapshot->session.can_start = 0U;
#endif
#if D_FEATURE_OTA
    if (s_maintenance_hold) snapshot->session.can_start = 0U;
#endif
    spin_unlock(&s_qwen_session_lock);
#if D_LOCAL_PROMPT_ENABLE
    if (d_local_prompt_alarm_requested()) snapshot->session.can_start = 0U;
#endif
    return D_OK;
}

static d_err_t qwen_speech_start_impl(void)
{
#if (D_QWEN_DIALOG_MODE == D_QWEN_DIALOG_MODE_DUPLEX)
    LOGI(TAG, "speech_start ignored: dialog=duplex state=%s", qwen_state_name(s_qwen_ctx.state));
    return D_ERR_NOT_READY;
#else
    d_err_t err;

    LOGI(TAG, "speech_start req: state=%s wifi=%u speech=%u key_down=%u",
         qwen_state_name(s_qwen_ctx.state),
         (unsigned)s_qwen_ctx.is_wifi_connected,
         (unsigned)s_qwen_ctx.is_speech_active,
         (unsigned)s_qwen_ctx.is_ptt_key_down);

    if ((s_qwen_ctx.is_initialized == 0U) || (s_qwen_ctx.is_wifi_connected == 0U)) {
        LOGW(TAG, "speech_start reject: not ready");
        return D_ERR_NOT_READY;
    }

    if ((s_qwen_ctx.state != QWEN_STATE_READY) &&
        (s_qwen_ctx.state != QWEN_STATE_PLAYING) &&
        (s_qwen_ctx.state != QWEN_STATE_THINKING)) {
        LOGW(TAG, "speech_start reject: state=%s", qwen_state_name(s_qwen_ctx.state));
        return D_ERR_NOT_READY;
    }

    if (s_qwen_ctx.is_speech_active != 0U) {
        LOGW(TAG, "speech_start reject: speech active");
        return D_ERR_BUSY;
    }

    err = qwen_map_sdk_err(c_mmi_speech_start());
    if (err != D_OK) {
        LOGW(TAG, "speech_start sdk err=%d", err);
        qwen_finish_dialog_failed("sdk_speech_start", err);
        return err;
    }

    s_qwen_ctx.is_speech_active = 1U;
    if (s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_IDLE) {
        s_qwen_ctx.dialog_round_id++;
        qwen_set_dialog_phase(QWEN_DIALOG_PHASE_PTT_PREPARING, "speech_start");
    }
    LOGI(TAG, "speech_start accepted");
    return D_OK;
#endif
}

static d_err_t qwen_speech_end_impl(void)
{
#if (D_QWEN_DIALOG_MODE == D_QWEN_DIALOG_MODE_DUPLEX)
    LOGI(TAG, "speech_end ignored: dialog=duplex state=%s", qwen_state_name(s_qwen_ctx.state));
    return D_OK;
#else
    d_err_t err;

    LOGI(TAG, "speech_end req: state=%s wifi=%u speech=%u key_down=%u",
         qwen_state_name(s_qwen_ctx.state),
         (unsigned)s_qwen_ctx.is_wifi_connected,
         (unsigned)s_qwen_ctx.is_speech_active,
         (unsigned)s_qwen_ctx.is_ptt_key_down);

    if ((s_qwen_ctx.is_initialized == 0U) || (s_qwen_ctx.is_wifi_connected == 0U)) {
        LOGW(TAG, "speech_end reject: not ready");
        return D_ERR_NOT_READY;
    }

    if (s_qwen_ctx.is_speech_active == 0U) {
        LOGI(TAG, "speech_end skip: speech inactive");
        return D_OK;
    }

    err = qwen_map_sdk_err(c_mmi_speech_end());
    if (err != D_OK) {
        LOGW(TAG, "speech_end sdk err=%d", err);
        qwen_finish_dialog_failed("sdk_speech_end", err);
        return err;
    }

    s_qwen_ctx.is_speech_active = 0U;
    if (qwen_dialog_phase_is_active() != 0U) {
        qwen_set_dialog_phase(QWEN_DIALOG_PHASE_PTT_ENDING, "speech_end");
    }
    LOGI(TAG, "speech_end accepted");
    return D_OK;
#endif
}

static d_err_t qwen_speech_pause_impl(void)
{
    d_err_t err;

    LOGI(TAG, "speech_pause req: state=%s speech=%u", qwen_state_name(s_qwen_ctx.state), (unsigned)s_qwen_ctx.is_speech_active);

    if (s_qwen_ctx.is_initialized == 0U) {
        return D_ERR_NOT_READY;
    }

    err = qwen_map_sdk_err(c_mmi_speech_pause());
    if (err != D_OK) {
        qwen_post_error_event((int32_t)err);
    }

    s_qwen_ctx.is_speech_active = 0U;
    s_qwen_ctx.is_ptt_key_down = 0U;
    s_qwen_ctx.is_ptt_start_pending = 0U;
    s_qwen_ctx.is_text_request_ready = 0U;
    qwen_set_dialog_phase(QWEN_DIALOG_PHASE_IDLE, "speech_pause");
    qwen_audio_stop_all();
    qwen_set_idle_network_state();
    return err;
}

static d_err_t qwen_question_impl(const char *text)
{
    d_err_t err;

    if ((text == NULL) || (text[0] == '\0')) {
        return D_ERR_INVALID_ARG;
    }

    if ((s_qwen_ctx.is_initialized == 0U) || (s_qwen_ctx.state != QWEN_STATE_READY) ||
        (s_qwen_ctx.is_text_request_ready == 0U) || (qwen_dialog_phase_is_active() != 0U)) {
        LOGW(TAG, "question reject: state=%s phase=%s text_req=%u",
             qwen_state_name(s_qwen_ctx.state),
             qwen_dialog_phase_name(s_qwen_ctx.dialog_phase),
             (unsigned)s_qwen_ctx.is_text_request_ready);
        return D_ERR_NOT_READY;
    }

    LOGI(TAG, "question req: len=%u", (unsigned)strlen(text));
    err = qwen_map_sdk_err(c_mmi_question((char *)text));
    if (err != D_OK) {
        qwen_finish_request_failed("sdk_question", err);
        return err;
    }

    s_qwen_ctx.is_text_request_ready = 0U;
    s_qwen_ctx.dialog_round_id++;
    qwen_set_dialog_phase(QWEN_DIALOG_PHASE_WAIT_LLM, "question");
    qwen_set_state(QWEN_STATE_THINKING);
    return D_OK;
}

static d_err_t qwen_tts_impl(const char *text)
{
    d_err_t err;

    if ((text == NULL) || (text[0] == '\0')) {
        return D_ERR_INVALID_ARG;
    }

    if ((s_qwen_ctx.is_initialized == 0U) || (s_qwen_ctx.state != QWEN_STATE_READY) ||
        (s_qwen_ctx.is_text_request_ready == 0U) || (qwen_dialog_phase_is_active() != 0U)) {
        LOGW(TAG, "tts reject: state=%s phase=%s text_req=%u",
             qwen_state_name(s_qwen_ctx.state),
             qwen_dialog_phase_name(s_qwen_ctx.dialog_phase),
             (unsigned)s_qwen_ctx.is_text_request_ready);
        return D_ERR_NOT_READY;
    }

    LOGI(TAG, "tts req: len=%u text=%s", (unsigned)strlen(text), text);
    err = qwen_map_sdk_err(c_mmi_tts((char *)text));
    if (err != D_OK) {
        qwen_finish_request_failed("sdk_tts", err);
        return err;
    }

    s_qwen_ctx.is_text_request_ready = 0U;
    s_qwen_ctx.dialog_round_id++;
    qwen_set_dialog_phase(QWEN_DIALOG_PHASE_WAIT_TTS, "tts");
    qwen_set_state(QWEN_STATE_THINKING);
    return D_OK;
}

d_err_t qwen_service_request_volume(qwen_volume_operation_e operation,
                                    int16_t value, uint32_t *request_id)
{
    d_err_t result;
    if (request_id != NULL) *request_id = 0U;
    if ((operation == QWEN_VOLUME_SET && (value < 0 || value > 100)) ||
        (operation == QWEN_VOLUME_ADJUST && (value < -100 || value > 100)) ||
        (operation == QWEN_VOLUME_MUTE && (value < 0 || value > 1)) ||
        operation > QWEN_VOLUME_MUTE || operation < QWEN_VOLUME_SET) {
        return D_ERR_INVALID_ARG;
    }
    result = qwen_sdk_enter();
    if (result != D_OK) return result;
    if (operation == QWEN_VOLUME_SET)
        result = d_device_volume_request_set((uint8_t)value, request_id);
    else if (operation == QWEN_VOLUME_ADJUST)
        result = d_device_volume_request_adjust((int8_t)value, request_id);
    else
        result = d_device_volume_request_mute((uint8_t)value, request_id);
    qwen_sdk_leave();
    return result;
}

static d_err_t qwen_sdk_run(d_err_t (*operation)(void))
{
    d_err_t result = qwen_sdk_enter();
    if (result != D_OK) return result;
    result = operation();
    qwen_sdk_leave();
    return result;
}

d_err_t qwen_service_speech_start(void)
{
    return qwen_sdk_run(qwen_speech_start_impl);
}

d_err_t qwen_service_speech_end(void)
{
    return qwen_sdk_run(qwen_speech_end_impl);
}

d_err_t qwen_service_speech_pause(void)
{
    return qwen_sdk_run(qwen_speech_pause_impl);
}

d_err_t qwen_service_question(const char *text)
{
    d_err_t result;
    if (text == NULL || text[0] == '\0') return D_ERR_INVALID_ARG;
    result = qwen_sdk_enter();
    if (result != D_OK) return result;
    result = qwen_question_impl(text);
    qwen_sdk_leave();
    return result;
}

d_err_t qwen_service_tts(const char *text)
{
    d_err_t result;
    if (text == NULL || text[0] == '\0') return D_ERR_INVALID_ARG;
    result = qwen_sdk_enter();
    if (result != D_OK) return result;
    result = qwen_tts_impl(text);
    qwen_sdk_leave();
    return result;
}

static d_err_t qwen_service_interrupt_by_click(uint8_t key_type, uint8_t key_action, uint8_t key_value)
{
    d_err_t err;

    LOGI(TAG, "ptt click interrupt: type=%u action=%u value=%u state=%s phase=%s",
         (unsigned)key_type,
         (unsigned)key_action,
         (unsigned)key_value,
         qwen_state_name(s_qwen_ctx.state),
         qwen_dialog_phase_name(s_qwen_ctx.dialog_phase));
    if (s_qwen_ctx.ready_cue_pending != 0U) {
        s_qwen_ctx.ready_cue_pending = 0U;
        s_qwen_ctx.ready_cue_sent = 1U;
        LOGI(TAG, "ready cue canceled by click");
    }
    if ((qwen_dialog_phase_is_active() == 0U) &&
        (s_qwen_ctx.is_speech_active == 0U) &&
        (s_qwen_ctx.state == QWEN_STATE_READY)) {
        qwen_audio_stop_player();
        return D_OK;
    }

    err = qwen_service_speech_pause();
    if (err != D_OK) {
        LOGW(TAG, "ptt click interrupt failed: err=%d", err);
    }
    return err;
}

#if D_FEATURE_POWER_CONTROL
static d_err_t qwen_service_prepare_shutdown(void *user_data)
{
    uint32_t started;
    qwen_service_snapshot_t snapshot;

    (void)user_data;
    if (!s_qwen_ctx.is_initialized) return D_OK;
    qwen_request_cleanup("shutdown", 0U);
    started = timer_get_ms();
    do {
        spin_lock(&s_qwen_session_lock);
        qwen_session_policy_snapshot(&s_session, &snapshot.session);
        spin_unlock(&s_qwen_session_lock);
        if (!qwen_session_cleanup_pending()) return D_OK;
        if (snapshot.session.fatal) return D_ERR_IO;
        os_time_dly(1);
    } while ((uint32_t)(timer_get_ms() - started) < QWEN_SESSION_SHUTDOWN_MS);
    return D_ERR_TIMEOUT;
}
#endif

d_err_t qwen_service_wakeup_from_idle(const char *source)
{
    d_err_t result = qwen_service_session_start();
    LOGI(TAG, "session trigger source=%s result=%d", source != NULL ? source : "unknown", result);
    return result;
}

/**
 * @brief PTT 按键回调（由 d_key_service 派发）
 * @note  key_value 已由 key service 匹配，无需再检查
 */
static int qwen_ptt_handler(uint8_t key_value, uint8_t key_action, void *user_data)
{
    (void)user_data;

#if (D_QWEN_DIALOG_MODE == D_QWEN_DIALOG_MODE_DUPLEX)
#if D_FEATURE_BLE_PROVISION
    if (key_action != KEY_EVENT_HOLD) {
        LOGI(TAG, "duplex key in: value=%u action=%u cfg_key=%u cfg_action=%u",
             (unsigned)key_value,
             (unsigned)key_action,
             (unsigned)D_QWEN_WIFI_RECONFIG_KEY,
             (unsigned)D_QWEN_WIFI_RECONFIG_ACTION);
    }
    if ((key_value == D_QWEN_WIFI_RECONFIG_KEY) &&
        (key_action == D_QWEN_WIFI_RECONFIG_ACTION)) {
        return qwen_service_handle_wifi_reconfig_key();
    }
#endif
    if ((key_action == KEY_EVENT_CLICK) &&
        (s_qwen_ctx.want_wss_connected == 0U) &&
        (s_qwen_ctx.is_wifi_connected != 0U) &&
        (s_qwen_ctx.is_initialized != 0U)) {
        return (qwen_service_wakeup_from_idle("duplex_idle_click") == D_OK) ? 1 : 0;
    }
    return 0;
#else
    d_err_t err;

    if (key_action != KEY_EVENT_HOLD) {
        QWEN_TRACE("ptt in: action=%u value=%u", (unsigned)key_action, (unsigned)key_value);
        qwen_log_dispatch_event(QWEN_DISPATCH_EVT_KEY, (uint32_t)key_action);
    }

    if (key_action == KEY_EVENT_CLICK) {
        (void)qwen_service_interrupt_by_click(0, key_action, key_value);
        return 1;
    }

    if ((key_action == KEY_EVENT_LONG) || (key_action == KEY_EVENT_HOLD)) {
        if (s_qwen_ctx.ready_cue_pending != 0U) {
            s_qwen_ctx.ready_cue_pending = 0U;
            s_qwen_ctx.ready_cue_sent = 1U;
            LOGI(TAG, "ready cue canceled by key down");
        }
        if (s_qwen_ctx.is_ptt_key_down != 0U) {
            return 1;
        }
        if ((key_action == KEY_EVENT_HOLD) && (s_qwen_ctx.is_ptt_prepare_failed != 0U)) {
            return 1;
        }
        if (qwen_state_can_start_ptt() == 0U) {
            if ((qwen_state_can_queue_ptt() != 0U) && (qwen_dialog_phase_is_active() == 0U)) {
                LOGI(TAG, "ptt key down pending: action=%u value=%u state=%s",
                     (unsigned)key_action,
                     (unsigned)key_value,
                     qwen_state_name(s_qwen_ctx.state));
                s_qwen_ctx.is_ptt_key_down = 1U;
                s_qwen_ctx.is_ptt_start_pending = 1U;
                s_qwen_ctx.is_ptt_prepare_failed = 0U;
                qwen_schedule_work("ptt_wait_ready");
                return 1;
            }
            if (key_action != KEY_EVENT_HOLD) {
                LOGW(TAG, "ptt key down ignored: state=%s", qwen_state_name(s_qwen_ctx.state));
            }
            return 1;
        }
        if (qwen_prepare_wakeup_reconnect() != 0U) {
            return 1;
        }
        if (qwen_dialog_phase_can_start_ptt() == 0U) {
            if (key_action != KEY_EVENT_HOLD) {
                LOGW(TAG, "ptt key down ignored: phase=%s", qwen_dialog_phase_name(s_qwen_ctx.dialog_phase));
            }
            return 1;
        }
        if (qwen_net_is_token_valid() == 0) {
            if (key_action != KEY_EVENT_HOLD) {
                LOGW(TAG, "ptt key down wait auth: state=%s", qwen_state_name(s_qwen_ctx.state));
                qwen_schedule_work("ptt_wait_auth");
            }
            return 1;
        }
        LOGI(TAG, "ptt key down: action=%u value=%u state=%s",
             (unsigned)key_action,
             (unsigned)key_value,
             qwen_state_name(s_qwen_ctx.state));
        s_qwen_ctx.is_ptt_key_down = 1U;
        s_qwen_ctx.is_ptt_start_pending = 1U;
        s_qwen_ctx.dialog_round_id++;
        qwen_set_dialog_phase(QWEN_DIALOG_PHASE_PTT_PREPARING, "ptt_key_down");
        s_qwen_ctx.is_ptt_prepare_failed = 0U;
        qwen_schedule_work("ptt_key_down");
        return 1;
    }

    if (key_action == KEY_EVENT_UP) {
        s_qwen_ctx.is_ptt_prepare_failed = 0U;
        if (s_qwen_ctx.is_ptt_key_down == 0U) {
            return 1;
        }
        LOGI(TAG, "ptt key up: action=%u value=%u state=%s",
             (unsigned)key_action,
             (unsigned)key_value,
             qwen_state_name(s_qwen_ctx.state));
        s_qwen_ctx.is_ptt_key_down = 0U;
        if (s_qwen_ctx.is_ptt_start_pending != 0U) {
            s_qwen_ctx.is_ptt_start_pending = 0U;
            qwen_set_dialog_phase(QWEN_DIALOG_PHASE_IDLE, "ptt_start_canceled");
            return 1;
        }
        err = qwen_service_speech_end();
        if (err != D_OK) {
            qwen_audio_stop_recorder();
            LOGW(TAG, "ptt key up rejected: err=%d", err);
        }
        return 1;
    }

    return 1;
#endif
}

int qwen_service_handle_key_event(uint8_t key_type, uint8_t key_action, uint8_t key_value)
{
    (void)key_type;
    return qwen_ptt_handler(key_value, key_action, NULL);
}

qwen_state_e qwen_service_get_state(void)
{
    return s_qwen_ctx.state;
}

int qwen_service_is_working(void)
{
    if (qwen_dialog_phase_is_active() != 0U) {
        return 1;
    }

    if ((s_qwen_ctx.state == QWEN_STATE_IDLE) || (s_qwen_ctx.state == QWEN_STATE_READY)) {
        return 0;
    }

    return 1;
}

#endif
