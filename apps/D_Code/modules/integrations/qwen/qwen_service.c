/** @file qwen_service.c @brief 后台统一编排 Qwen 会话、音频交接、认证与运输生命周期。 */
#include "config/app_config.h"
#if D_LOCAL_PROMPT_ENABLE
#include "modules/services/audio/d_local_prompt.h"
#endif

#if D_FEATURE_QWEN

#include "qwen_service.h"
#include "qwen_net.h"
#include "qwen_auth_port.h"
#include "qwen_access.h"
#if D_QWEN_ACTIVATION_ENABLE
#include "qwen_activation.h"
static uint8_t s_activation_command_active;
#endif
#include "qwen_wss.h"
#include "qwen_audio.h"
#include "qwen_cmd.h"
#if D_FEATURE_AI_ALARM
#include "qwen_local_tool.h"
#include "qwen_request_gate.h"
#endif
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
#include "qwen_sdk_audio_io.h"

#if D_QWEN_HAL_ACCEPT_TEST_ENABLE
#include "sdk/include/qwen_test.h"
#endif

#define TAG "QWEN"
/* UI、提示音与 SDK 调用者共享会话策略，所有 I/O 均在此锁外执行。 */
static DEFINE_SPINLOCK(s_qwen_session_lock);
/* 公开输入适配在同步调用返回前借用服务资源。 */
static uint16_t s_key_calls;
static uint8_t s_key_closing;
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
    uint8_t wifi_reconfig_active; /* 会话锁保护的在途按键配置事务。 */
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
/* 唯一工作任务持有；未知期限令牌只准许当前代次的一次新握手。 */
static uint8_t s_wss_token_ready;
static uint32_t s_wss_token_generation;
/* MMI 中断与本地排空都在状态锁内登记；异步 STOP 尚未执行时也能拒绝旧排空。 */
static struct {
    uint32_t session, player;
    uint8_t claimed, interrupted;
} s_drain;
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
static uint8_t qwen_reconcile_wifi(void);
static void qwen_request_cleanup(const char *reason, uint8_t preserve);
static d_err_t qwen_worker_cleanup(void);
static d_err_t qwen_session_start_checked(const uint32_t *expected_generation);
static const qwen_audio_peer_t *s_audio_peer;
static uint32_t s_audio_peer_generation;
static uint8_t s_external_volume_detaching;
static uint16_t s_external_volume_calls;

/**
 * @brief 初始化前绑定静态产品音频同行操作集；完整合同见 qwen_service.h。
 */
d_err_t qwen_service_bind_audio_peer(const qwen_audio_peer_t *peer)
{
    if (peer == NULL || peer->begin == NULL || peer->poll == NULL || peer->release == NULL)
        return D_ERR_INVALID_ARG;
    if (s_qwen_ctx.is_initialized || (s_audio_peer != NULL && s_audio_peer != peer)) return D_ERR_BUSY;
    s_audio_peer = peer;
    return D_OK;
}

/**
 * @brief 综合会话策略、维护与闹铃意图检查当前业务准入。
 * @return 当前允许业务返回非零。
 * @note 此查询不持有后续资源借用，实际 SDK 操作需取得相应准入占用。
 */
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

/**
 * @brief 在会话锁内读取清理待办标记。
 * @return 仍待清理返回非零。
 */
static uint8_t qwen_session_cleanup_pending(void)
{
    uint8_t pending;
    spin_lock(&s_qwen_session_lock);
    pending = s_session.cleanup_pending;
    spin_unlock(&s_qwen_session_lock);
    return pending;
}

/**
 * @brief 在会话锁内读取当前会话代次。
 * @return 当前策略代次。
 */
static uint32_t qwen_session_generation(void)
{
    uint32_t generation;
    spin_lock(&s_qwen_session_lock);
    generation = s_session.generation;
    spin_unlock(&s_qwen_session_lock);
    return generation;
}

/**
 * @brief 为兼容同步 SDK 调用取得会话借用计数。
 * @return 成功返回 D_OK；闹铃、维护或计数上限返回 D_ERR_BUSY；服务或会话不允许返回 D_ERR_NOT_READY。
 * @note 成功后必须 qwen_sdk_leave；借用持续到状态与音频副作用结束。
 */
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

/**
 * @brief 归还一次已取得的同步 SDK 借用。
 */
static void qwen_sdk_leave(void)
{
    spin_lock(&s_qwen_session_lock);
    if (s_qwen_ctx.sdk_calls) s_qwen_ctx.sdk_calls--;
    spin_unlock(&s_qwen_session_lock);
}

/**
 * @brief 限时等待同步 SDK 借用及按键资源借用全部归还。
 * @param[in] timeout_ms 最多等待毫秒数。
 * @return 全部归还返回 D_OK，超时返回 D_ERR_TIMEOUT。
 * @note 等待不持会话或上下文锁，超时不能开始销毁借用资源。
 */
static d_err_t qwen_wait_sdk_calls(uint32_t timeout_ms)
{
    uint32_t started = timer_get_ms();
    for (;;) {
        uint8_t borrowed;
        spin_lock(&s_qwen_session_lock);
        borrowed = s_qwen_ctx.sdk_calls != 0U || s_key_calls != 0U;
        spin_unlock(&s_qwen_session_lock);
        if (!borrowed) return D_OK;
        if ((uint32_t)(timer_get_ms() - started) >= timeout_ms) return D_ERR_TIMEOUT;
        os_time_dly(1);
    }
}

/**
 * @brief 上下文互斥量有效时取得状态锁。
 */
static void qwen_ctx_lock(void)
{
    if (os_mutex_valid(&s_qwen_ctx.state_mutex) != 0) {
        os_mutex_pend(&s_qwen_ctx.state_mutex, 0);
    }
}

/**
 * @brief 上下文互斥量有效时释放状态锁。
 */
static void qwen_ctx_unlock(void)
{
    if (os_mutex_valid(&s_qwen_ctx.state_mutex) != 0) {
        os_mutex_post(&s_qwen_ctx.state_mutex);
    }
}

/**
 * @brief 记录会话清理意图并唤醒唯一工作任务。
 * @param[in] reason 唤醒日志原因。
 * @param[in] preserve 非零保留会话请求，0 结束会话。
 * @note 工具先失效；不在调用者中执行资源销毁。
 */
static void qwen_request_cleanup(const char *reason, uint8_t preserve)
{
#if D_FEATURE_AI_ALARM
    qwen_local_tool_session(qwen_session_generation(), 0U);
#endif
    spin_lock(&s_qwen_session_lock);
    qwen_session_policy_cleanup(&s_session, preserve);
    s_qwen_ctx.cleanup_retry = 1U;
    if (!preserve) s_qwen_ctx.is_wss_reconnect_pending = 0U;
    spin_unlock(&s_qwen_session_lock);
    qwen_schedule_work(reason);
}

/**
 * @brief 在空闲、网络与运行资格允许且无维护时恢复本地小乐监听。
 * @note 仅工作任务调用；不等待云令牌响应。已排队 START 在有限时间内去重，之后可重试异步启动失败。
 */
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
    /* START 是排队意图：短期去重，异步启动失败后允许有限重试。 */
    if (s_qwen_ctx.wakeup_start_pending &&
        (uint32_t)(now_ms - s_qwen_ctx.wakeup_start_ms) < QWEN_SERVICE_AUTH_POLL_MS)
        return;
    s_qwen_ctx.wakeup_start_ms = now_ms;
    s_qwen_ctx.wakeup_start_pending = 1U;
    result = xiaole_wakeup_start(generation);
    if (result != D_OK) LOGW(TAG, "standby wakeup start retry: %d", result);
#endif
}

/**
 * @brief 由唯一工作任务排空借用、停止音频与运输，再受检清理 SDK。
 * @return 全部收尾成功返回 D_OK；返回首个等待或清理错误，SDK deinit 失败锁存致命状态。
 * @note 不在 state_mutex 内等待；失败保留 cleanup_pending。只有成功才释放前台或产品音频占用，并按真实请求恢复待机或继续本地激活保存。
 */
static d_err_t qwen_worker_cleanup(void)
{
    d_err_t audio_result;
    d_err_t transport_result;
    d_err_t result;
    uint8_t fatal = 0U;
    s_wss_token_ready = 0U;
#if D_FEATURE_AI_ALARM
    qwen_local_tool_session(qwen_session_generation(), 0U);
    qwen_local_tool_tick((uint32_t)timer_get_ms());
#endif

    if (s_session.fatal) return D_ERR_IO;
    /* 同时关闭公开按键资源准入；先前借用的按键调用必须返回，工作任务才能销毁资源。 */
    spin_lock(&s_qwen_session_lock);
    s_qwen_ctx.cleanup_active = 1U;
    spin_unlock(&s_qwen_session_lock);
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
    /* 令牌刷新或运输恢复保留用户对话意图；只有会话真正结束才重置欢迎语，包括清理期间再次停止。 */
    if (!s_session.requested) s_qwen_ctx.ready_cue_sent = 0U;
    qwen_session_policy_set_cleanup_result(&s_session, result, fatal);
    s_qwen_ctx.cleanup_active = 0U;
    spin_unlock(&s_qwen_session_lock);
    LOGI(TAG, "session cleanup complete=%u result=%d fatal=%u",
         result == D_OK, result, fatal);
    qwen_set_state(result == D_OK ? QWEN_STATE_IDLE : QWEN_STATE_ERROR);
#if D_LOCAL_PROMPT_ENABLE
    if (result == D_OK && !s_session.requested) d_local_prompt_foreground_end();
#endif
    if (result == D_OK) {
#if D_PRODUCT_MUSIC_ENABLE
        {
            uint32_t released_generation;
            spin_lock(&s_qwen_session_lock);
            released_generation = !s_session.requested ? s_audio_peer_generation : 0U;
            if (released_generation) s_audio_peer_generation = 0U;
            spin_unlock(&s_qwen_session_lock);
            if (s_audio_peer != NULL && released_generation) s_audio_peer->release(released_generation);
        }
#endif
        qwen_restore_idle_wakeup();
    }
    /* 已受理的本地保存即使无 WiFi 也必须在清理后继续，否则离线任务消费 UI 唤醒后可能永久等待。 */
    if (result == D_OK && !s_qwen_ctx.worker_exit &&
        ((s_session.start_error == D_OK && s_qwen_ctx.is_wifi_connected)
#if D_QWEN_ACTIVATION_ENABLE
         || qwen_activation_needs_work()
#endif
        ))
        qwen_schedule_work("cleanup_ready");
    return result;
}

/**
 * @brief 以当前单调毫秒更新会话活动起点。
 */
static void qwen_mark_activity(void)
{
    s_qwen_ctx.last_activity_ms = (uint32_t)timer_get_ms();
}

/**
 * @brief 借用当前内部对话阶段的诊断名称。
 * @param[in] phase 对话阶段。
 * @return 静态阶段名，未知值返回 UNKNOWN。
 */
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

/**
 * @brief 检查对话阶段既非 IDLE 也非 FAILED。
 * @return 活动返回非零。
 */
static uint8_t qwen_dialog_phase_is_active(void)
{
    return (s_qwen_ctx.dialog_phase != QWEN_DIALOG_PHASE_IDLE) &&
           (s_qwen_ctx.dialog_phase != QWEN_DIALOG_PHASE_FAILED);
}

/**
 * @brief 按对话模式判断当前是否忙于一轮响应。
 * @return 忙时非零；Duplex 的 LISTENING 不算忙。
 */
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

/**
 * @brief 检查是否处于按键准备、拾音、结束或等待 ASR 的流程。
 * @return 属于 PTT 流程返回非零。
 */
static uint8_t qwen_dialog_phase_is_ptt_flow(void)
{
    return (s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_PTT_PREPARING) ||
           (s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_LISTENING) ||
           (s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_PTT_ENDING) ||
           (s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_WAIT_ASR);
}

/**
 * @brief 按模式核对是否可接受 SpeechStart，告别期间统一拒绝。
 * @return 可接受返回非零。
 */
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

/**
 * @brief 按当前模式及阶段核对是否可接受 ASR 文本。
 * @return 属于允许的上行或响应阶段返回非零。
 */
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

/**
 * @brief 按模式隔离不属于当前轮次阶段的 TTS 事件。
 * @return 当前阶段可接受 TTS 返回非零。
 */
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

/**
 * @brief 按模式与响应阶段核对是否接受 LLM 事件。
 * @return 当前可接受返回非零。
 */
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

/**
 * @brief 检查服务是否 READY、THINKING 或 PLAYING，可立即开始 PTT。
 * @return 满足返回非零。
 */
static uint8_t qwen_state_can_start_ptt(void)
{
    return (s_qwen_ctx.state == QWEN_STATE_READY) ||
           (s_qwen_ctx.state == QWEN_STATE_THINKING) ||
           (s_qwen_ctx.state == QWEN_STATE_PLAYING);
}

/**
 * @brief 检查注册、取令牌或建链期间是否可排队 PTT 意图。
 * @return 可排队返回非零。
 */
static uint8_t qwen_state_can_queue_ptt(void)
{
    return (s_qwen_ctx.state == QWEN_STATE_REGISTERING) ||
           (s_qwen_ctx.state == QWEN_STATE_TOKEN_REQ) ||
           (s_qwen_ctx.state == QWEN_STATE_CONNECTING);
}

/**
 * @brief 检查空闲或下行响应阶段是否可被新的 PTT 打断。
 * @return 允许返回非零。
 */
static uint8_t qwen_dialog_phase_can_start_ptt(void)
{
    return (s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_IDLE) ||
           (s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_WAIT_LLM) ||
           (s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_WAIT_TTS) ||
           (s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_PLAYING) ||
           (s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_WAIT_LOCAL_TTS_END);
}

/**
 * @brief 发布内部阶段与起点，并在从静止进入活动时唤醒工作任务。
 * @param[in] phase 目标阶段。
 * @param[in] reason 可空日志原因。
 * @note 相同非 IDLE 阶段会续期该阶段起点；不在此完成 SDK 动作。
 */
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

/**
 * @brief 读取当前阶段已持续的单调毫秒。
 * @return 当前计时与阶段起点的环形差值。
 */
static uint32_t qwen_dialog_phase_elapsed_ms(void)
{
    return (uint32_t)((uint32_t)timer_get_ms() - s_qwen_ctx.dialog_phase_start_ms);
}

/**
 * @brief 借用失败分类的静态诊断名称。
 * @param[in] failure_type 失败分类。
 * @return timeout、sdk、network、audio、request 或 unknown。
 */
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

/**
 * @brief 按已约定原因关键字分类失败。
 * @param[in] reason 可空内部原因串。
 * @return 由原因推导的失败类别，空原因返回 UNKNOWN。
 */
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

/**
 * @brief 依照网络、运行资格及 WSS 事实选择 IDLE、READY 或 CONNECTING。
 */
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

/**
 * @brief 结束失败轮次并请求音频停止；本地超时另外撤销准入和后台清理。
 * @param[in] reason 内部失败原因。
 * @param[in] err 要发布的错误码。
 * @note 本地期限不能取消云端旧轮次；超时先关闭准入，避免迟到 TTS 被误认成新告别请求。
 */
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
    if (failure_type == QWEN_FAILURE_TYPE_TIMEOUT) {
        /* 本地期限不取消云端轮次。释放状态锁前先撤销准入，运输清理由工作任务完成，
 * 避免迟到 TTS 被当成新告别请求。 */
        qwen_set_dialog_phase(QWEN_DIALOG_PHASE_FAILED, reason);
        qwen_request_cleanup(reason, 0U);
        qwen_set_state(QWEN_STATE_ERROR);
        return;
    }
    qwen_set_dialog_phase(QWEN_DIALOG_PHASE_IDLE, reason);
    qwen_set_idle_network_state();
}

/**
 * @brief 清除文本请求及提示意图，停止播放器并发布错误后恢复空闲网络状态。
 * @param[in] reason 内部失败原因。
 * @param[in] err 要发布的错误码。
 */
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

/**
 * @brief 与 MMI 事件序列化地检查 ASR、LLM、TTS 及本地排空期限。
 * @note 只有当前仍处于对应阶段才撤销，不能用旧快照取消已经推进的 TTS。
 */
static void qwen_check_dialog_timeout(void)
{
    uint32_t elapsed_ms;
    const char *reason = NULL;

    /* 期限决策与 MMI 事件序列化；已经推进阶段的 TTS 不能被旧超时观察撤销。 */
    qwen_ctx_lock();
    if (qwen_session_cleanup_pending() || s_qwen_ctx.is_idle_goodbye_pending ||
        !qwen_dialog_phase_is_active()) {
        qwen_ctx_unlock();
        return;
    }

    elapsed_ms = qwen_dialog_phase_elapsed_ms();
    if ((s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_PTT_ENDING) &&
        (elapsed_ms >= QWEN_DIALOG_END_TIMEOUT_MS)) {
        reason = "no_asr";
    } else if ((s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_WAIT_LLM) &&
        (elapsed_ms >= QWEN_DIALOG_RESPONSE_TIMEOUT_MS)) {
        reason = "no_llm";
    } else if ((s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_WAIT_TTS) &&
        (elapsed_ms >= QWEN_DIALOG_RESPONSE_TIMEOUT_MS)) {
        reason = "no_tts";
    } else if ((s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_WAIT_LOCAL_TTS_END) &&
        (elapsed_ms >= QWEN_DIALOG_LOCAL_DRAIN_TIMEOUT_MS)) {
        reason = "no_local_tts_end";
    }
    if (reason != NULL) qwen_finish_dialog_failed(reason, D_ERR_TIMEOUT);
    qwen_ctx_unlock();
}

/**
 * @brief 为闲置结束请求后台完整清理。
 * @param[in] reason 清理与唤醒原因。
 */
static void qwen_do_idle_disconnect(const char *reason)
{
    qwen_request_cleanup(reason, 0U);
}

/**
 * @brief 告别等待超过配置期限时请求断开清理。
 * @return 已超时并请求清理返回 1，否则为 0。
 */
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

/**
 * @brief 本地播放器仍有进展时续期空闲；停滞时请求停止。
 * @return 仍需为播放器保活返回 1，否则为 0。
 */
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

/**
 * @brief 综合会话、网络、运输、对话与本地播放条件核对闲置结束资格。
 * @return 可以开始闲置计时判断返回 1，否则为 0。
 */
static uint8_t qwen_idle_timeout_can_start(void)
{
    if ((s_qwen_ctx.is_initialized == 0U) ||
        (s_qwen_ctx.is_wifi_connected == 0U) ||
        (s_qwen_ctx.want_wss_connected == 0U) ||
        qwen_session_cleanup_pending() ||
        (qwen_is_dialog_busy() != 0U) ||
        (qwen_wss_is_connected() == 0)) {
        return 0U;
    }

    if (qwen_keep_idle_alive_for_player() != 0U) {
        return 0U;
    }

    return 1U;
}

/**
 * @brief 为普通响应生产者尝试取得共享 SDK 响应槽。
 * @return 取得返回非零；未启用工具功能时固定允许。
 * @note SDK 调用期间不持请求槽内部锁，发送者提取后释放槽所有权。
 */
static uint8_t qwen_response_begin(void)
{
#if D_FEATURE_AI_ALARM
    return qwen_request_begin(QWEN_REQUEST_NORMAL);
#else
    return 1U;
#endif
}

/**
 * @brief 向 SDK 提交 TTS 或文本问题，并发布响应槽受理结果。
 * @param[in] transcript 非零使用 c_mmi_tts，0 使用 c_mmi_question。
 * @param[in] text 有效请求文本。
 * @return SDK 返回的服务错误映射。
 * @note 只能在 response_begin 成功后调用；SDK 成功不等于云输出。
 */
static d_err_t qwen_response_submit(uint8_t transcript, const char *text)
{
    int32_t result = transcript ? c_mmi_tts((char *)text) : c_mmi_question((char *)text);
#if D_FEATURE_AI_ALARM
    qwen_request_submitted(result == 0);
    if (!result) qwen_local_tool_normal_request();
#endif
    return qwen_map_sdk_err(result);
}

/**
 * @brief 准备播放器后再次核对代次与告别意图，再提交固定告别 TTS。
 * @param[in] generation 本次告别占用捕获的会话代次。
 * @return 成功受理返回 D_OK；槽忙或准入失效返回 D_ERR_BUSY；其余为 SDK 结果。
 * @note 准备等待期间可被停止或维护撤销；拒绝时只归还本次响应槽。
 */
static d_err_t qwen_start_idle_goodbye_tts(uint32_t generation)
{
    d_err_t err;

    if (!qwen_response_begin()) return D_ERR_BUSY;
    qwen_audio_prepare_player();
    /* 准备会等待音频工作任务，期间停止或维护可撤销本请求；失败只归还本次响应槽，不重置其他所有者。 */
    qwen_ctx_lock();
    if (!qwen_session_allows_business() ||
        generation != qwen_session_generation() ||
        !s_qwen_ctx.is_idle_goodbye_pending) {
        qwen_ctx_unlock();
#if D_FEATURE_AI_ALARM
        qwen_request_submitted(0);
#endif
        return D_ERR_BUSY;
    }
    LOGI(TAG, "idle goodbye tts: len=%u text=%s",
         (unsigned)strlen(D_QWEN_IDLE_GOODBYE_TEXT),
         D_QWEN_IDLE_GOODBYE_TEXT);
    s_qwen_ctx.dialog_round_id++;
    s_qwen_ctx.is_text_request_ready = 0U;
    qwen_set_dialog_phase(QWEN_DIALOG_PHASE_WAIT_TTS, "idle_goodbye_tts");
    qwen_set_state(QWEN_STATE_THINKING);
    qwen_ctx_unlock();
    err = qwen_response_submit(1U, D_QWEN_IDLE_GOODBYE_TEXT);
    if (err != D_OK) {
        qwen_audio_stop_player();
        return err;
    }

    return D_OK;
}

/**
 * @brief 重新核对空闲事实，取得 SDK 借用后停止录音并提交告别。
 * @param[in] idle_ms 兼容观察值，函数内按最新活动时刻重新计算。
 * @return 本次已接管告别或清理返回 1，未满足准入返回 0。
 * @note Duplex 跳过不支持的 speech_pause；失败转后台清理，最后平衡 SDK 借用。
 */
static uint8_t qwen_start_idle_goodbye(uint32_t idle_ms)
{
    d_err_t err;
    uint32_t generation;

    /* 外层轮询仅为提示，按最新回调状态认领；锁外工作期间持 SDK 借用以阻止清理销毁。 */
    qwen_ctx_lock();
    idle_ms = (uint32_t)((uint32_t)timer_get_ms() - s_qwen_ctx.last_activity_ms);
    if (!D_QWEN_IDLE_TIMEOUT_MS || s_qwen_ctx.is_idle_goodbye_pending ||
        !qwen_session_allows_business() || !qwen_idle_timeout_can_start() ||
        idle_ms < (uint32_t)D_QWEN_IDLE_TIMEOUT_MS) {
        qwen_ctx_unlock();
        return 0U;
    }
#if D_FEATURE_AI_ALARM
    if (qwen_local_tool_busy()) {
        qwen_ctx_unlock();
        return 0U;
    }
#endif
    if (qwen_sdk_enter() != D_OK) {
        qwen_ctx_unlock();
        return 0U;
    }
    generation = qwen_session_generation();

    LOGI(TAG, "idle timeout: idle=%u timeout=%u",
         (unsigned)idle_ms,
         (unsigned)D_QWEN_IDLE_TIMEOUT_MS);
    s_qwen_ctx.is_idle_goodbye_pending = 1U;
    s_qwen_ctx.idle_goodbye_start_ms = (uint32_t)timer_get_ms();
    s_qwen_ctx.is_speech_active = 0U;
    s_qwen_ctx.is_text_request_ready = 0U;
    s_qwen_ctx.ready_cue_pending = 0U;
    s_qwen_ctx.ready_cue_sent = 1U;
    qwen_ctx_unlock();
    qwen_audio_stop_recorder();
#if (D_QWEN_DIALOG_MODE == D_QWEN_DIALOG_MODE_DUPLEX)
    LOGI(TAG, "idle goodbye speech_pause skipped: dialog=duplex");
#else
    err = qwen_map_sdk_err(c_mmi_speech_pause());
    if (err != D_OK) {
        LOGW(TAG, "idle goodbye speech_pause failed: %d, disconnect now",
             (int)err);
        qwen_do_idle_disconnect("idle_timeout_pause_failed");
        qwen_sdk_leave();
        return 1U;
    }

    LOGI(TAG, "idle goodbye speech_pause ok");
#endif
    err = qwen_start_idle_goodbye_tts(generation);
    if (err != D_OK) {
        LOGW(TAG, "idle goodbye tts failed: %d, disconnect now", (int)err);
        qwen_do_idle_disconnect("idle_timeout_no_tts");
    }
    qwen_sdk_leave();
    return 1U;
}

/**
 * @brief 与事件顺序一致地检查告别期限或当前空闲超时。
 * @return 已处理告别或清理返回非零，否则为 0。
 * @note 仅短决策持上下文锁；需要等待的 SDK 或音频工作在锁外执行。
 */
static uint8_t qwen_check_idle_timeout(void)
{
    uint32_t idle_ms;
    uint8_t handled;

    if (D_QWEN_IDLE_TIMEOUT_MS == 0U) {
        return 0U;
    }

    /* 快速检查也会修改活动时间或投递停滞播放器停止，因此与 MMI 回调保持顺序；
 * 不在此锁内等待 SDK 或音频操作。 */
    qwen_ctx_lock();
    if (s_qwen_ctx.is_idle_goodbye_pending != 0U) {
        handled = qwen_check_idle_goodbye_timeout();
        qwen_ctx_unlock();
        return handled;
    }

    if (qwen_idle_timeout_can_start() == 0U) {
        qwen_ctx_unlock();
        return 0U;
    }

    idle_ms = (uint32_t)((uint32_t)timer_get_ms() - s_qwen_ctx.last_activity_ms);
    if (idle_ms < (uint32_t)D_QWEN_IDLE_TIMEOUT_MS) {
        qwen_ctx_unlock();
        return 0U;
    }

    qwen_ctx_unlock();
    return qwen_start_idle_goodbye(idle_ms);
}

/**
 * @brief 映射 SDK 成功、参数与忙返回，其他结果统一未就绪。
 * @param[in] ret SDK 原始返回值。
 * @return D_OK、D_ERR_INVALID_ARG、D_ERR_BUSY 或 D_ERR_NOT_READY。
 */
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

/**
 * @brief 借用服务状态诊断名称。
 * @param[in] state 服务状态。
 * @return 静态状态名，未知值返回 UNKNOWN。
 */
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

/**
 * @brief 记录最终文本长度与至多 96 字节预览。
 * @param[in] label 可空诊断标签。
 * @param[in] text 可空零结尾文本。
 */
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

/**
 * @brief 按事件类型与长度选择同步借用发布或异步值投递。
 * @param[in] event_id ASR 或 LLM 文本事件。
 * @param[in] label 可空日志标签。
 * @param[in] text 可空零结尾文本。
 * @note LLM 与超出异步容量的文本同步发布；消费者不得在回调后保留原指针，事件长度受 uint16_t 上限约束。
 */
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

/**
 * @brief 以值载荷异步投递服务错误。
 * @param[in] code 错误码。
 */
static void qwen_post_error_event(int32_t code)
{
    d_event_post(EVT_QWEN_ERROR, &code, sizeof(code));
}

/**
 * @brief 状态变化时推进快照版本并投递状态值事件。
 * @param[in] state 新服务状态。
 */
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

/**
 * @brief 把上行、等待或播放阶段映射为公开服务状态。
 * @param[in] fallback_state 未映射阶段的后备状态。
 * @return 匹配的状态；无专门映射时返回 fallback_state。
 */
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

/**
 * @brief 认证返回后依据仍活动对话恢复展示状态，避免覆盖当前轮次。
 * @param[in] fallback_state 活动阶段未专门映射时的后备状态。
 */
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

/**
 * @brief 借用 MMI 事件诊断名称。
 * @param[in] event MMI 事件编号。
 * @return 静态名称，未列值返回 UNKNOWN。
 */
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
    case C_MMI_EVENT_SPEECH_INTERRUPT:
        return "SPEECH_INTERRUPT";
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

/**
 * @brief 告别在途时清除上行与文本标记并拒绝迟到 ASR。
 * @param[in] event 用于日志的 ASR 事件编号。
 * @return 告别在途返回 1，否则为 0。
 * @note 不停止正在播报的告别播放器。
 */
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

/**
 * @brief 借用 WiFi、NTP 与 WSS 总线事件名称。
 * @param[in] event 服务事件编号。
 * @return 静态事件名或 UNKNOWN。
 */
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

/**
 * @brief 借用内部事件来源分类名称。
 * @param[in] event 内部派发分类。
 * @return 静态来源名或 UNKNOWN。
 */
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

/**
 * @brief 记录事件来源与当前轮次状态快照。
 * @param[in] event 派发来源。
 * @param[in] code 原始事件编号。
 */
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

/**
 * @brief 把正毫秒向上换算到系统节拍。
 * @param[in] ms 毫秒等待值。
 * @return 0 输入返回 0，其余至少一拍。
 */
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

/**
 * @brief 记录工作任务的会话、认证与待办摘要。
 * @param[in] stage 可空阶段标签。
 */
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

/**
 * @brief 向 SDK 请求恢复心跳，失败只记录日志。
 */
static void qwen_resume_heartbeat(void)
{
    int32_t sdk_result;

    sdk_result = c_mmi_heartbeat_resume();
    if (sdk_result != 0) {
        LOGW(TAG, "heartbeat resume failed: %d", sdk_result);
    }
}

/**
 * @brief 唤醒现有工作任务；信号量未建立时只记录跳过。
 * @param[in] reason 可空唤醒原因。
 */
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

#if D_FEATURE_AI_ALARM
/**
 * @brief 把本地工具唤醒转交服务工作信号量。
 */
static void qwen_tool_wake(void) { qwen_schedule_work("local_tool"); }
#endif

/**
 * @brief 识别未知域中固定 change 兼容命令。
 * @param[in] msg 可空命令值对象。
 * @return 匹配返回 1，否则为 0。
 */
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

/**
 * @brief 按播放器代次核对本地 TTS 排空事件，再完成告别或恢复空闲。
 * @param[in] event 事件总线借用的事件对象。
 * @param[in] user_data 订阅上下文，本实现不使用。
 * @note 回调前后均核对代次与清理，过期事件不能结束新播放器。
 */
static void qwen_on_audio_event(const d_event_t *event, void *user_data)
{
    uint32_t generation;
    uint32_t session;
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
    session = qwen_session_generation();
    if (((s_drain.claimed || s_drain.interrupted) && s_drain.session == session && s_drain.player == generation) ||
        qwen_sdk_enter() != D_OK) {
        qwen_ctx_unlock();
        return;
    }
    s_drain.claimed = 1U;
    s_drain.interrupted = 0U;
    s_drain.session = session;
    s_drain.player = generation;
    qwen_ctx_unlock();
    /* SDK 自有锁只在产品状态锁外取得，借用保证回调期间不能销毁 SDK。 */
    if (session == qwen_session_generation() && generation == qwen_audio_player_generation() &&
        !qwen_session_cleanup_pending() && !c_mmi_audio_recv_all())
        LOGW(TAG, "SDK did not confirm local drain");
    qwen_sdk_leave();
    qwen_ctx_lock();
    if (s_drain.interrupted || s_drain.session != session || s_drain.player != generation ||
        session != qwen_session_generation() || generation != qwen_audio_player_generation() ||
        qwen_session_cleanup_pending()) {
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

/**
 * @brief 读取命令值并消费固定 general_change，当前只记录忽略。
 * @param[in] event 事件总线借用的事件对象。
 * @param[in] user_data 订阅上下文，本实现不使用。
 */
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

/**
 * @brief 把 NTP 完成提示转为后台唤醒，不在事件回调中认证。
 * @param[in] event 事件总线借用的事件对象。
 * @param[in] user_data 订阅上下文，本实现不使用。
 */
static void qwen_on_time_event(const d_event_t *event, void *user_data)
{
    (void)user_data;
    if ((event == NULL) || (event->id != EVT_NTP_SYNCED)) {
        return;
    }

    qwen_log_dispatch_event(QWEN_DISPATCH_EVT_NTP, (uint32_t)event->id);
    qwen_schedule_work("ntp_synced");
}

/**
 * @brief 核对运输代次后处理意外断连。
 * @param[in] event 事件总线借用的事件对象。
 * @param[in] user_data 订阅上下文，本实现不使用。
 */
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
/**
 * @brief 按检测代次原子核对并受理会话，拒绝时安排恢复待机。
 * @param[in] event 事件总线借用的事件对象。
 * @param[in] user_data 订阅上下文，本实现不使用。
 * @note 事件可能晚于完整维护或清理周期，不能先检查再无条件启动。
 */
static void qwen_on_xiaole_event(const d_event_t *event, void *user_data)
{
    d_err_t result;
    uint32_t generation;
    (void)user_data;
    if (event == NULL || event->id != EVT_XIAOLE_WAKEUP_DETECTED ||
        event->data == NULL || event->data_len != sizeof(generation)) return;
    memcpy(&generation, event->data, sizeof(generation));
    /* 事件可能晚于整次维护或清理；在同一会话锁内比较与受理，避免先检查后启动的竞争。 */
    result = qwen_session_start_checked(&generation);
    if (result != D_OK) qwen_schedule_work("xiaole_retry_standby");
}
#endif

/**
 * @brief 投递运输代次并唤醒服务，通知丢失时由任务复核真实断连。
 * @param[in] generation 断开的运输代次。
 */
static void qwen_post_wss_disconnected(uint32_t generation)
{
    d_event_post(EVT_QWEN_WSS_DISCONNECTED, &generation, sizeof(generation));
    /* 通知可能丢失，工作任务还会复核真实运输状态。 */
    qwen_schedule_work("wss_disconnected");
}

/**
 * @brief 请求结束会话并由后台清理网络及相关资源。
 */
static void qwen_cleanup_network(void)
{
    qwen_request_cleanup("cleanup_network", 0U);
}

#if (D_FEATURE_BLE_PROVISION) && (D_QWEN_DIALOG_MODE == D_QWEN_DIALOG_MODE_DUPLEX)
/**
 * @brief 检查当前服务状态是否允许按键触发重新配网。
 * @return 可处理状态返回 1，注册或建链阶段返回 0。
 */
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

/**
 * @brief 持有整个按键配置事务的占用，复核维护后原子忘记并离网。
 * @return 当前固定返回 1，表示该按键动作已处理或拒绝继续传播。
 * @note 等待回调状态后再次复核维护；在途 forget_saved 返回前不允许维护或反初始化宣称完成。失败解除重配标记以便按键重试。
 */
static int qwen_service_handle_wifi_reconfig_key(void)
{
    d_err_t err;
#if D_FEATURE_OTA
    uint8_t held;
#endif

    /* 整个调用含等待回调状态都保留事务占用；forget_saved 尚在执行时，维护与反初始化不能报告完成。 */
    spin_lock(&s_qwen_session_lock);
    if (!s_qwen_ctx.service_ready || s_qwen_ctx.wifi_reconfig_active
#if D_FEATURE_OTA
        || s_maintenance_hold
#endif
#if D_FEATURE_POWER_CONTROL
        || power_ctrl_is_shutting_down()
#endif
        ) { spin_unlock(&s_qwen_session_lock); return 1; }
    s_qwen_ctx.wifi_reconfig_active = 1U;
    spin_unlock(&s_qwen_session_lock);
    qwen_ctx_lock();
#if D_FEATURE_OTA
    /* 早期检查仅为提示，等待回调状态时维护可能抢先；认领清除凭据动作前再次检查。 */
    spin_lock(&s_qwen_session_lock);
    held = s_maintenance_hold;
    spin_unlock(&s_qwen_session_lock);
    if (held) { qwen_ctx_unlock(); goto release; }
#endif
#if D_FEATURE_POWER_CONTROL
    if (power_ctrl_is_shutting_down()) { qwen_ctx_unlock(); goto release; }
#endif
    if (s_qwen_ctx.is_wifi_reconfiguring != 0U) {
        LOGW(TAG, "wifi reconfig ignored: already running");
        qwen_ctx_unlock();
        goto release;
    }
    if (qwen_service_can_reconfig_wifi_by_key() == 0U) {
        LOGW(TAG, "wifi reconfig rejected: state=%s phase=%s",
             qwen_state_name(s_qwen_ctx.state),
             qwen_dialog_phase_name(s_qwen_ctx.dialog_phase));
        qwen_ctx_unlock();
        goto release;
    }

    LOGI(TAG, "wifi reconfig start: state=%s phase=%s",
         qwen_state_name(s_qwen_ctx.state),
         qwen_dialog_phase_name(s_qwen_ctx.dialog_phase));
    s_qwen_ctx.is_wifi_reconfiguring = 1U;
    qwen_cleanup_network();
    qwen_ctx_unlock();

    /* 在 WiFi connect_seq 内原子清除并离网，不留重连间隙；启用 BLE 配网时同时重开广播。 */
    err = wifi_service_forget_saved();
    if (err != D_OK) {
        LOGE(TAG, "wifi forget_saved failed: %d", err);
        /* 失败允许按键重试，成功路径在 EVT_WIFI_CONNECTED 时清除重配标记。 */
        qwen_ctx_lock();
        s_qwen_ctx.is_wifi_reconfiguring = 0U;
        qwen_ctx_unlock();
    }

release:
    spin_lock(&s_qwen_session_lock);
    s_qwen_ctx.wifi_reconfig_active = 0U;
    spin_unlock(&s_qwen_session_lock);
    return 1;
}
#endif

/**
 * @brief 准备 SDK 身份与音频配置，并在 WSS 接收前注册业务命令回调。
 * @return 全部准备成功返回 D_OK；配置、SDK 或命令注册失败返回实际错误。
 * @note 固定 SDK 注册会按同域、同名更新，重试仍在 WSS 接收访问未同步列表之前执行。
 */
static d_err_t qwen_prepare_network(void)
{
    d_err_t err;
    char device_name[QWEN_ACCESS_NAME_SIZE];

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
            qwen_set_state(QWEN_STATE_ERROR);
            return err;
        }
        /* 当前 SDK 按同域、同名和回调更新注册；重试发生在 WSS 接收访问未同步列表之前。 */
        s_qwen_ctx.is_cmd_registered = 1U;
    }

    if (qwen_access_get_device_name(device_name, sizeof(device_name)) == D_OK)
        LOGI(TAG, "device identity: %s", device_name);
    LOGI(TAG, "prepare network done");
    return D_OK;
}

#if D_QWEN_HAL_ACCEPT_TEST_ENABLE
/**
 * @brief 验收构建中只执行一次 SDK HAL 测试并保留其结果。
 * @return SDK 测试成功返回 D_OK，失败返回 D_ERR_IO。
 */
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

/**
 * @brief 检查 MMI 数据已初始化、当前会话允许且 WSS 已建立。
 * @return 三项满足返回非零。
 */
static uint8_t qwen_can_use_live_session(void)
{
    return s_qwen_ctx.mmi_data_initialized &&
        qwen_session_allows_business() && qwen_wss_is_connected();
}

/**
 * @brief MMI 已初始化时保留会话意图并请求认证前清理。
 * @return 已请求清理返回 1，不需要返回 0。
 * @note 令牌分析会调用 c_mmi_init，不能直接叠加到已初始化 SDK。
 */
static uint8_t qwen_request_mmi_cleanup_for_auth(void)
{
    if (!s_qwen_ctx.mmi_data_initialized) return 0U;
    /* 令牌分析会调用 c_mmi_init，已初始化 SDK 不能重复进入。 */
    qwen_request_cleanup("auth_refresh", 1U);
    return 1U;
}

/** @brief 注册与取票在准备和发送边界复核代次；支持原有的空闲预热。
 * @param[in] generation 工作任务取得准备占用时的代次。
 * @return 占用有效、网络可用且无竞争时返回非零。
 */
static int qwen_auth_admitted(uint32_t generation)
{
    int allowed;
#if D_FEATURE_POWER_CONTROL
    if (power_ctrl_is_shutting_down()) return 0;
#endif
#if D_LOCAL_PROMPT_ENABLE
    if (d_local_prompt_alarm_requested()) return 0;
#endif
    spin_lock(&s_qwen_session_lock);
    allowed = generation == s_session.generation && s_qwen_ctx.worker_prepare_active &&
              !s_qwen_ctx.worker_exit && s_session.wifi_ready && s_session.runtime_allowed &&
              !s_session.cleanup_pending && !s_session.fatal;
#if D_FEATURE_OTA
    allowed = allowed && !s_maintenance_hold;
#endif
#if D_LOCAL_PROMPT_ENABLE
    allowed = allowed && !s_alarm_hold_token;
#endif
    spin_unlock(&s_qwen_session_lock);
    return allowed;
}

/**
 * @brief 保留活动会话，按当前建连资格刷新令牌。
 * @param[in] prepare_generation 开始准备时的会话代次。
 * @return 无需更新或认证成功返回 D_OK；需先清理或认证失败返回实际错误。
 * @note 本地令牌过期只限制下一次 Upgrade，不单独销毁已经建立的对话；服务端拒绝、停止和维护仍可撤销。
 */
static d_err_t qwen_sync_auth_if_needed(uint32_t prepare_generation)
{
    d_err_t err;
    qwen_state_e auth_prev_state;
    uint8_t is_registered;
    uint8_t is_refresh_needed;
    uint32_t generation;
    uint8_t requested;

    is_registered = (uint8_t)qwen_net_is_registered();
    /* 令牌用于 HTTP Upgrade 认证，本地过期只限制下一次连接，不单独销毁已准入对话。
 * 服务端断连或拒绝、用户停止与维护仍会关闭该连接。 */
    if (is_registered && qwen_can_use_live_session()) {
        LOGI(TAG, "auth keep: established WSS; refresh before next connection");
        return D_OK;
    }
    is_refresh_needed = (uint8_t)qwen_net_is_token_refresh_needed();
    spin_lock(&s_qwen_session_lock);
    generation = s_session.generation;
    requested = qwen_session_policy_allows(&s_session);
    spin_unlock(&s_qwen_session_lock);
    if (generation != prepare_generation) return D_ERR_NOT_READY;
    if (requested && qwen_net_is_token_expiry_unknown() &&
        (!s_wss_token_ready || s_wss_token_generation != generation)) {
        is_refresh_needed = 1U;
    }
    LOGI(TAG, "auth gate: registered=%u token_valid=%u refresh=%u",
         (unsigned)is_registered,
         (unsigned)qwen_net_is_token_valid(),
         (unsigned)is_refresh_needed);
    if ((is_registered != 0U) && (is_refresh_needed == 0U)) {
        LOGI(TAG, "auth skip: token still valid");
        qwen_restore_state_after_auth_if_needed(s_qwen_ctx.state);
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

    s_wss_token_ready = 0U;
    err = qwen_net_sync_auth(prepare_generation, qwen_auth_admitted);
    if (err != D_OK) {
        LOGE(TAG, "Qwen auth failed: %d", err);
        qwen_set_state(QWEN_STATE_ERROR);
        return err;
    }

    LOGI(TAG, "auth sync done");
    if (requested && qwen_net_is_token_expiry_unknown()) {
        spin_lock(&s_qwen_session_lock);
        if (generation == s_session.generation && qwen_session_policy_allows(&s_session)) {
            s_wss_token_generation = generation;
            s_wss_token_ready = 1U;
        }
        spin_unlock(&s_qwen_session_lock);
    }
    qwen_restore_state_after_auth_if_needed(auth_prev_state);
    return D_OK;
}

/**
 * @brief 重连待办时停止生产音频，执行有界配置退避并增加下一次延迟。
 * @note 延时结束后复核退出与网络状态；不在此完成新连接。
 */
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

/**
 * @brief 把 MMI 重建待办转换为保留会话意图的后台清理。
 */
static void qwen_prepare_mmi_reinit_if_needed(void)
{
    if (s_qwen_ctx.is_mmi_reinit_pending != 0U) {
        s_qwen_ctx.is_mmi_reinit_pending = 0U;
        qwen_request_cleanup("mmi_reinit", 1U);
    }
}

/**
 * @brief 非阻塞轮询产品音频同行，并在回调后再次核对会话代次。
 * @return 同行就绪且代次仍允许返回非零；无同行时仅检查会话准入。
 */
static uint8_t qwen_audio_peer_admitted(void)
{
    uint8_t ready = 0U;
    uint32_t generation;
    if (!qwen_session_allows_business()) return 0U;
    if (s_audio_peer == NULL) return 1U;
    spin_lock(&s_qwen_session_lock);
    generation = s_audio_peer_generation;
    spin_unlock(&s_qwen_session_lock);
    if (generation != qwen_session_generation() ||
        s_audio_peer->poll(generation, &ready) != D_OK || !ready) return 0U;
    /* 产品回调可能与停止或维护并行，ready 仅属于回调返回后仍准入的同一代次。 */
    return generation == qwen_session_generation() && qwen_session_allows_business();
}

/**
 * @brief 为当前会话申请产品音频同行占用，并在交接期限内轮询。
 * @param[in] generation 申请占用的会话代次。
 * @return 无同行或已就绪返回 D_OK；取消返回 D_ERR_BUSY；期限或同行错误原样返回。
 * @note 回调前后核对代次，成功结果不跨已撤销会话复用。
 */
static d_err_t qwen_audio_peer_wait(uint32_t generation)
{
    uint32_t started = (uint32_t)timer_get_ms();
    uint8_t ready = 0U;
    d_err_t result;
    if (s_audio_peer == NULL) return D_OK;
    spin_lock(&s_qwen_session_lock);
    s_audio_peer_generation = generation;
    spin_unlock(&s_qwen_session_lock);
    result = s_audio_peer->begin(generation);
    if (result != D_OK) return result;
    do {
        if (generation != qwen_session_generation() || !qwen_session_allows_business()) return D_ERR_BUSY;
        result = s_audio_peer->poll(generation, &ready);
        if (generation != qwen_session_generation() || !qwen_session_allows_business()) return D_ERR_BUSY;
        if (result != D_OK || ready) return result;
        os_time_dly(1);
    } while ((uint32_t)((uint32_t)timer_get_ms() - started) < QWEN_SESSION_HANDOFF_MS);
    return D_ERR_TIMEOUT;
}

/** @brief 在真实握手前消费同代次的令牌资格。
 * @param[in] generation 媒体交接前捕获的会话代次。
 * @return 业务仍允许，且未知期限令牌属于本次建连时返回非零。
 */
static uint8_t qwen_worker_authorize_wss(uint32_t generation)
{
    uint8_t allowed;
    uint8_t unknown = qwen_net_is_token_expiry_unknown();
    if (unknown && !qwen_net_is_token_valid()) return 0U;
    spin_lock(&s_qwen_session_lock);
    allowed = generation == s_session.generation && qwen_session_policy_allows(&s_session) &&
              (!unknown || (s_wss_token_ready && s_wss_token_generation == generation));
    if (allowed) s_wss_token_ready = 0U;
    spin_unlock(&s_qwen_session_lock);
    return allowed;
}

/**
 * @brief 复核断连、令牌和音频交接后建链，并按代次发布连接结果。
 * @note 产品音乐、提示音、小乐麦克风均需完成受检交接；建链期间取消请求只安排后台清理，不直接在回调中销毁。
 */
static void qwen_connect_wss_if_needed(void)
{
    d_err_t err;
    uint32_t generation;
    uint8_t was_connected;

    if (!qwen_session_allows_business() || qwen_wss_is_connected()) return;
    /* 新握手复用旧 MMI 前先核对可能丢失的断连通知。 */
    qwen_ctx_lock();
    spin_lock(&s_qwen_session_lock);
    was_connected = s_session.connected;
    spin_unlock(&s_qwen_session_lock);
    if (was_connected) {
        qwen_handle_wss_disconnected();
        qwen_ctx_unlock();
        return;
    }
    qwen_ctx_unlock();
    /* 认证检查活动会话后运输仍可能断开；不能用旧凭据再次握手，先由工作任务刷新。 */
    if (!qwen_net_is_token_valid()) {
        qwen_schedule_work("connect_wait_auth");
        return;
    }
    generation = qwen_session_generation();
    qwen_set_state(QWEN_STATE_CONNECTING);
#if D_PRODUCT_MUSIC_ENABLE
    err = qwen_audio_peer_wait(generation);
    if (err != D_OK) {
        qwen_request_cleanup("media_handoff_failed", 0U);
        qwen_post_error_event((int32_t)err);
        return;
    }
#endif
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
    if (!qwen_worker_authorize_wss(generation)) {
        qwen_schedule_work("connect_wait_auth");
        return;
    }
    err = qwen_wss_connect();
    if (generation != qwen_session_generation() || !qwen_session_allows_business()) {
        /* 运输失败可能已经要求重连，不能清掉该请求。 */
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

/**
 * @brief 为固定欢迎语取得 SDK 与响应槽，音频预热后重新核对准入并提交。
 * @note 等待在状态锁外；SDK 回调可能早于提交返回，失败回退只修改仍匹配的会话与轮次。
 */
static void qwen_handle_ready_cue_if_needed(void)
{
    d_err_t err;
    uint32_t generation;
    uint32_t round;
    qwen_dialog_phase_e previous_phase;
    qwen_state_e previous_state;

    qwen_ctx_lock();
#if D_FEATURE_AI_ALARM
    if (qwen_local_tool_busy()) { qwen_ctx_unlock(); return; }
#endif

    if (s_qwen_ctx.ready_cue_pending == 0U) {
        qwen_ctx_unlock();
        return;
    }

    if (s_qwen_ctx.ready_cue_sent != 0U) {
        s_qwen_ctx.ready_cue_pending = 0U;
        qwen_ctx_unlock();
        return;
    }

    if ((s_qwen_ctx.is_text_request_ready == 0U) ||
        ((s_qwen_ctx.state != QWEN_STATE_READY) &&
         (s_qwen_ctx.state != QWEN_STATE_LISTENING))) {
        QWEN_TRACE("ready cue wait: state=%s phase=%s text_req=%u",
                   qwen_state_name(s_qwen_ctx.state),
                   qwen_dialog_phase_name(s_qwen_ctx.dialog_phase),
                   (unsigned)s_qwen_ctx.is_text_request_ready);
        qwen_ctx_unlock();
        return;
    }

#if (D_QWEN_DIALOG_MODE != D_QWEN_DIALOG_MODE_DUPLEX)
    if (qwen_dialog_phase_is_active() != 0U) {
        qwen_ctx_unlock();
        return;
    }
#endif

    if (qwen_sdk_enter() != D_OK) { qwen_ctx_unlock(); return; }
    if (!qwen_response_begin()) {
        qwen_sdk_leave(); qwen_ctx_unlock(); return;
    }
    generation = qwen_session_generation();
    qwen_ctx_unlock();
    LOGI(TAG, "ready cue trigger");
    qwen_audio_prepare_player();

    /* 音频预热在状态锁外等待，期间按键、回调或停止可撤销提示音；只取消本次响应槽。 */
    qwen_ctx_lock();
    if (!qwen_session_allows_business() || generation != qwen_session_generation() ||
        !s_qwen_ctx.ready_cue_pending || s_qwen_ctx.ready_cue_sent ||
        !s_qwen_ctx.is_text_request_ready ||
        (s_qwen_ctx.state != QWEN_STATE_READY && s_qwen_ctx.state != QWEN_STATE_LISTENING)
#if D_FEATURE_AI_ALARM
        || qwen_local_tool_busy()
#endif
#if (D_QWEN_DIALOG_MODE != D_QWEN_DIALOG_MODE_DUPLEX)
        || qwen_dialog_phase_is_active()
#endif
        ) {
        qwen_ctx_unlock();
#if D_FEATURE_AI_ALARM
        qwen_request_submitted(0);
#endif
        qwen_sdk_leave();
        return;
    }
    previous_phase = s_qwen_ctx.dialog_phase;
    previous_state = s_qwen_ctx.state;
    s_qwen_ctx.ready_cue_sent = 1U;
    s_qwen_ctx.ready_cue_pending = 0U;
    s_qwen_ctx.is_text_request_ready = 0U;
#if (D_QWEN_DIALOG_MODE != D_QWEN_DIALOG_MODE_DUPLEX)
    s_qwen_ctx.dialog_round_id++;
#endif
    round = s_qwen_ctx.dialog_round_id;
    if (!qwen_dialog_phase_can_accept_tts()) {
        qwen_set_dialog_phase(QWEN_DIALOG_PHASE_WAIT_TTS, "ready_cue");
        qwen_set_state(QWEN_STATE_THINKING);
    }
    qwen_ctx_unlock();
    /* SDK 回调可能早于提交返回，不覆盖已经推进的阶段。 */
    err = qwen_response_submit(1U, D_QWEN_READY_CUE_TEXT);
    if (err != D_OK) {
        qwen_ctx_lock();
        if (generation == qwen_session_generation() && qwen_session_allows_business() &&
            round == s_qwen_ctx.dialog_round_id) {
            s_qwen_ctx.ready_cue_sent = 0U;
            s_qwen_ctx.ready_cue_pending = 1U;
            s_qwen_ctx.is_text_request_ready = 1U;
            if (s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_WAIT_TTS) {
                qwen_set_dialog_phase(previous_phase, "ready_cue_rejected");
                qwen_set_state(previous_state);
            }
            qwen_audio_stop_player();
        }
        qwen_ctx_unlock();
        LOGW(TAG, "ready cue deferred: err=%d", err);
    }
    qwen_sdk_leave();
}

/**
 * @brief 消费仍按住的 PTT 意图，准备录音后再次检查按键并启动上行。
 * @note 准备失败结束本轮；准备期间已松开时仅停止录音，不发送过期开始。
 */
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
/**
 * @brief 按键对讲在 WiFi 在线但 WSS 断开时申请会话并保留按下意图。
 * @return 无需重连返回 0；已处理重连或启动拒绝返回 1。
 */
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

/**
 * @brief 在维护同一把会话锁下取得后台准备在途标记。
 * @return 允许准备并已置位返回非零，否则为 0。
 * @note 实际准备与认证在锁外运行；唯一工作任务仍持有资源收尾责任。
 */
static uint8_t qwen_worker_prepare_begin(void)
{
    uint8_t allowed;
    spin_lock(&s_qwen_session_lock);
    allowed = !s_session.cleanup_pending && !s_session.fatal && !s_qwen_ctx.worker_exit;
#if D_QWEN_ACTIVATION_ENABLE
    allowed = allowed && !s_activation_command_active;
#endif
#if D_FEATURE_OTA
    allowed = allowed && !s_maintenance_hold;
#endif
    if (allowed) s_qwen_ctx.worker_prepare_active = 1U;
    spin_unlock(&s_qwen_session_lock);
    return allowed;
}

/**
 * @brief 在会话锁内清除后台准备在途标记。
 */
static void qwen_worker_prepare_end(void)
{
    spin_lock(&s_qwen_session_lock);
    s_qwen_ctx.worker_prepare_active = 0U;
    spin_unlock(&s_qwen_session_lock);
}

#if D_QWEN_ACTIVATION_ENABLE
/**
 * @brief 按网络需求、关机、闹铃、维护与代次复核激活准备准入。
 * @param[in] generation 准备工作捕获的会话代次。
 * @return 当前仍允许返回非零。
 */
static int qwen_activation_still_admitted(uint32_t generation)
{
    int allowed;
    if (qwen_activation_needs_network() && !s_qwen_ctx.is_wifi_connected) return 0;
#if D_FEATURE_POWER_CONTROL
    if (power_ctrl_is_shutting_down()) return 0;
#endif
#if D_LOCAL_PROMPT_ENABLE
    if (d_local_prompt_alarm_requested()) return 0;
#endif
    spin_lock(&s_qwen_session_lock);
    allowed = generation == s_session.generation &&
        !s_session.cleanup_pending && !s_qwen_ctx.worker_exit;
#if D_FEATURE_OTA
    allowed = allowed && !s_maintenance_hold;
#endif
#if D_LOCAL_PROMPT_ENABLE
    allowed = allowed && !s_alarm_hold_token;
#endif
    spin_unlock(&s_qwen_session_lock);
    return allowed;
}

/**
 * @brief 在后台准备占用内先处理本地激活来源，再按实际需求检查网络与 NTP 并推进激活。
 * @note 所有早退均归还准备标记；离线候选保存不必等待网络。
 */
static void qwen_worker_activate(void)
{
    uint32_t generation;
    if (!qwen_activation_needs_work()) return;
    if (!qwen_worker_prepare_begin()) return;
    generation = qwen_session_generation();
    if (!qwen_activation_still_admitted(generation) || qwen_activation_worker_init() != D_OK) {
        qwen_worker_prepare_end();
        return;
    }
    (void)qwen_activation_prepare_source(qwen_activation_still_admitted, generation);
    if (qwen_activation_needs_network() &&
        (!s_qwen_ctx.is_wifi_connected || !qwen_auth_sync_network_time())) {
        qwen_worker_prepare_end();
        return;
    }
    if (qwen_activation_still_admitted(generation)) {
        (void)qwen_activation_step(qwen_activation_still_admitted,
                                  generation, (uint32_t)timer_get_ms());
    }
    qwen_worker_prepare_end();
}
#endif

/**
 * @brief 唯一后台任务按清理、超时、本地身份、激活、认证和音频建链顺序推进会话。
 * @param[in] priv 任务兼容参数，本实现不使用。
 * @note 先排清理再准备；每个耗时阶段后核对代次与网络。闲置时保留独立小乐监听，不靠定期刷新令牌重建监听；退出前仍由本任务受检清理。
 */
static void qwen_service_task(void *priv)
{
    d_err_t err;
    qwen_activation_error_e last_identity_error = QWEN_ACT_ERROR_NONE;
    int pend_result;
    uint32_t wait_ticks;
    uint32_t generation;
    uint8_t runtime_allowed;
#if D_FEATURE_AI_ALARM
    qwen_tool_notice_e tool_notice_seen = QWEN_TOOL_NOTICE_NONE;
    uint32_t tool_transaction_seen = 0U;
#endif

    (void)priv;
    LOGI(TAG, "worker start");
    while (!s_qwen_ctx.worker_exit) {
        /* WiFi 提示丢失不能让工作任务长期困在离线缓存。 */
        wait_ticks = qwen_ms_to_ticks(QWEN_SERVICE_AUTH_POLL_MS);
        if (qwen_dialog_phase_is_active())
            wait_ticks = qwen_ms_to_ticks(QWEN_SERVICE_WORK_POLL_MS);
#if D_FEATURE_AI_ALARM
        if (qwen_local_tool_busy()) wait_ticks = qwen_ms_to_ticks(QWEN_SERVICE_WORK_POLL_MS);
#endif
        pend_result = os_sem_pend(&s_qwen_ctx.work_sem, (int)wait_ticks);
        if (s_qwen_ctx.worker_exit) break;
        if (!qwen_reconcile_wifi()) continue;
#if D_FEATURE_AI_ALARM
        qwen_local_tool_session(qwen_session_generation(),
            (uint8_t)(qwen_session_allows_business() && !qwen_session_cleanup_pending()));
        if (qwen_sdk_enter() == D_OK) {
            qwen_local_tool_tick((uint32_t)timer_get_ms());
            if (s_qwen_ctx.mmi_data_initialized) {
                uint8_t sdk_state = c_mmi_get_major_state();
                qwen_local_tool_sdk_ready(sdk_state == C_MMI_STATE_LISTENING &&
                    s_qwen_ctx.is_speech_active && qwen_can_use_live_session());
            } else qwen_local_tool_sdk_ready(0U);
            qwen_sdk_leave();
        } else {
            qwen_local_tool_session(qwen_session_generation(), 0U);
            qwen_local_tool_tick((uint32_t)timer_get_ms());
        }
        if (qwen_local_tool_failed()) qwen_request_cleanup("local_tool_failed", 0U);
#endif

        /* 1. 先处理清理和超时；清理未完成时不能进入下一轮准备。 */
        if (qwen_session_cleanup_pending()) {
            if (s_qwen_ctx.cleanup_retry && !s_session.fatal) {
                s_qwen_ctx.cleanup_retry = 0U;
                (void)qwen_worker_cleanup();
            }
            continue;
        }
#if D_FEATURE_AI_ALARM
        if (qwen_local_tool_busy()) {
            qwen_ctx_lock();
            s_qwen_ctx.dialog_phase_start_ms = timer_get_ms();
            qwen_mark_activity();
            qwen_ctx_unlock();
        } else {
            qwen_tool_snapshot_t tool;
            qwen_local_tool_get_snapshot(&tool);
            if (tool.notice == QWEN_TOOL_NOTICE_NO_ANSWER &&
                (tool_notice_seen != tool.notice || tool_transaction_seen != tool.transaction)) {
                qwen_ctx_lock();
                qwen_mark_activity(); /* 有限等待结束后开启新的空闲计时窗口。 */
                qwen_ctx_unlock();
            }
            tool_notice_seen = tool.notice;
            tool_transaction_seen = tool.transaction;
            /* 本地云输出观察超时不调用会停止音频的通用失败处理。
 * 对话由真实 SDK 或 ASR 事件恢复，不伪造 Listening 转移。 */
            if (tool.notice != QWEN_TOOL_NOTICE_NO_ANSWER) qwen_check_dialog_timeout();
        }
#else
        qwen_check_dialog_timeout();
#endif
        if (qwen_check_idle_timeout() || qwen_session_cleanup_pending()) continue;
#if D_FEATURE_POWER_CONTROL
        if (power_ctrl_is_shutting_down()) continue;
#endif

        /* 身份 I/O 共享现有后台准备占用；UI 查询不读 Flash，维护需等占用归还。 */
        if (qwen_worker_prepare_begin()) {
            d_err_t identity_result = qwen_access_prepare_identity();
            qwen_worker_prepare_end();
            if (identity_result != D_OK) {
                qwen_activation_error_e identity_error = qwen_access_identity_error();
                if (identity_error != last_identity_error) {
                    LOGE(TAG, "device identity unavailable: reason=%u", (unsigned)identity_error);
                    qwen_post_error_event(identity_result);
                }
                last_identity_error = identity_error;
            } else last_identity_error = QWEN_ACT_ERROR_NONE;
        }
        /* 离线也重试已签发凭据的保存；网络激活自行核对 WiFi 与 NTP，仍先于运行资格检查。 */
#if D_QWEN_ACTIVATION_ENABLE
        qwen_worker_activate();
#endif
        if (!s_qwen_ctx.is_wifi_connected) continue;
#if D_QWEN_ACTIVATION_ENABLE
        if (!qwen_auth_sync_network_time()) continue;
#else
        /* SDK 先发布事件再置 NTP 就绪，轮询补齐该顺序竞争与迟启动，不依赖第二次事件。 */
        if (!d_crypto_timestamp_inited()) (void)qwen_auth_sync_network_time();
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
        /* 闲置时保持独立唤醒监听。每次请求连接前再检查过期，
 * 避免空闲刷新令牌按其 TTL 反复销毁 MMI 并重启小乐；此后到达的新请求唤醒下一轮。 */
        spin_lock(&s_qwen_session_lock);
        if (s_qwen_ctx.mmi_data_initialized && !s_session.requested && !s_session.connected) {
            spin_unlock(&s_qwen_session_lock);
            continue;
        }
        spin_unlock(&s_qwen_session_lock);
        if (!qwen_worker_prepare_begin()) continue;
        s_qwen_ctx.preparing_sdk = 1U;
        err = qwen_prepare_network();
        s_qwen_ctx.preparing_sdk = 0U;
        if (err == D_OK && d_crypto_timestamp_inited() == 0) {
            qwen_worker_prepare_end();
            continue;
        }
        if (err == D_OK) err = qwen_sync_auth_if_needed(generation);
        qwen_worker_prepare_end();
        /* 3. 耗时步骤返回后核对代次，丢弃已取消或已断网请求的结果。 */
        if (generation != qwen_session_generation() ||
            qwen_session_cleanup_pending() || !s_qwen_ctx.is_wifi_connected) {
            continue;
        }
#if D_FEATURE_POWER_CONTROL
        if (power_ctrl_is_shutting_down()) continue;
#endif
        spin_lock(&s_qwen_session_lock);
        if (s_session.generation != generation || s_session.cleanup_pending || !s_session.wifi_ready) {
            spin_unlock(&s_qwen_session_lock);
            continue;
        }
        if (err != D_OK) {
            qwen_session_policy_failed(&s_session, err);
            spin_unlock(&s_qwen_session_lock);
            s_qwen_ctx.cleanup_retry = 1U;
            qwen_post_error_event((int32_t)err);
            qwen_schedule_work("auth_failed");
            continue;
        }
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

/**
 * @brief 记录 SDK 数据已初始化，并在未清理时安排后台继续准备。
 * @note SDK 预热不创建自动会话意图，自动请求由网络与配置策略决定。
 */
static void qwen_handle_data_init(void)
{
    s_qwen_ctx.mmi_data_initialized = 1U;
    LOGI(TAG, "mmi data init");
    if (s_qwen_ctx.cleanup_active || qwen_session_cleanup_pending()) return;
    s_qwen_ctx.is_text_request_ready = 0U;
    qwen_set_dialog_phase(QWEN_DIALOG_PHASE_IDLE, "data_init");
    /* 自动会话意图归网络与配网策略，SDK 重新预热不主动申请会话。 */
    qwen_schedule_work("mmi_data_init");
}

/**
 * @brief 记录 SDK 数据已撤销，并在非受控清理阶段请求结束会话。
 */
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

/**
 * @brief 在上下文锁内核对准入和阶段，推进 MMI 语音、文本与 TTS 事件。
 * @param[in] event SDK MMI 事件编号。
 * @param[in] param 随事件提供的借用参数，文本类按零结尾字符解释。
 * @return 固定返回 UTIL_SUCCESS，包括被过滤的旧事件。
 * @note 清理和致命状态过滤普通业务事件；告别隔离 ASR、LLM。TTS_END 只标记云端流结束，本地排空由音频代次事件确认。
 */
static int32_t qwen_on_mmi_event(uint32_t event, void *param)
{
    const char *text;

    /* 这些可选通知的载荷仅在回调期间借用，产品无消费者。 */
    if (event == C_MMI_EVENT_STATE_CHANGE || event == C_MMI_EVENT_LATENCY ||
        event == C_MMI_EVENT_WORD_TIMESTAMP) return UTIL_SUCCESS;
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

#if D_FEATURE_AI_ALARM
    if (event == C_MMI_EVENT_ASR_START)
        qwen_local_tool_interrupt();
    /* 仅含工具且文本为空的回复不等同普通对话已结束。 */
    if (qwen_local_tool_busy() &&
        (event == C_MMI_EVENT_SPEECH_PREPARE ||
         (event == C_MMI_EVENT_LLM_COMPLETE && (!text || !*text)))) {
        qwen_schedule_work("local_tool_wait"); qwen_ctx_unlock(); return UTIL_SUCCESS;
    }
#endif

    switch (event) {
    case C_MMI_EVENT_ERROR:
        /* 现有工作任务负责清理，不留存错误对象或云端正文。 */
        LOGE(TAG, "SDK error event");
        qwen_request_cleanup("sdk_error", 0U);
        break;
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
    case C_MMI_EVENT_SPEECH_INTERRUPT:
        s_drain.session = qwen_session_generation();
        s_drain.player = qwen_audio_player_generation();
        s_drain.interrupted = 1U;
        qwen_audio_stop_player();
        if (s_qwen_ctx.is_idle_goodbye_pending) {
            qwen_do_idle_disconnect("idle_goodbye_interrupted");
        } else if (s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_WAIT_LOCAL_TTS_END ||
                   s_qwen_ctx.dialog_phase == QWEN_DIALOG_PHASE_PLAYING) {
            s_qwen_ctx.is_text_request_ready = 0U;
            qwen_set_dialog_phase(QWEN_DIALOG_PHASE_IDLE, "playback_interrupted");
            qwen_set_idle_network_state();
        }
        break;
    case C_MMI_EVENT_SPEECH_START:
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
        if (s_qwen_ctx.dialog_phase != QWEN_DIALOG_PHASE_LISTENING) {
            s_qwen_ctx.dialog_round_id++;
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

/**
 * @brief 根据当前业务与网络意图选择保留请求重连或完整结束。
 * @note 未初始化、已有清理或清理在途时不重复安排。
 */
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

/**
 * @brief 在 Qwen 锁外取 WiFi 快照，再按观察版本核对后发布网络事实。
 * @return 已成功复核返回 1；服务退出或观察竞争需重试返回 0。
 * @note 即使状态相同也推进观察版本，阻止更旧读取覆盖；离线撤销上行与连接意图并请求清理。
 */
static uint8_t qwen_reconcile_wifi(void)
{
    wifi_service_connection_snapshot_t snapshot;
    uint32_t revision;
    uint8_t ready, changed;

    spin_lock(&s_qwen_session_lock);
    if (!s_qwen_ctx.is_initialized || s_qwen_ctx.worker_exit) {
        spin_unlock(&s_qwen_session_lock);
        return 0U;
    }
    revision = s_qwen_ctx.wifi_revision;
    spin_unlock(&s_qwen_session_lock);
    /* 提供者使用独立互斥量，不能持 Qwen 锁进入它。 */
    wifi_service_get_connection_snapshot(&snapshot);

    qwen_ctx_lock();
    spin_lock(&s_qwen_session_lock);
    if (!s_qwen_ctx.is_initialized || s_qwen_ctx.worker_exit) {
        spin_unlock(&s_qwen_session_lock);
        qwen_ctx_unlock();
        return 0U;
    }
    if (revision != s_qwen_ctx.wifi_revision) {
        spin_unlock(&s_qwen_session_lock);
        qwen_ctx_unlock();
        qwen_schedule_work("wifi_snapshot_retry");
        return 0U;
    }
    /* 相同状态的观察也取代旧读取，但不刷新活动时间。 */
    s_qwen_ctx.wifi_revision++;
    ready = snapshot.state == WIFI_SERVICE_CONN_CONNECTED;
    changed = s_qwen_ctx.is_wifi_connected != ready || s_session.wifi_ready != ready;
    s_qwen_ctx.is_wifi_connected = ready;
    qwen_session_policy_network(&s_session, ready);
    spin_unlock(&s_qwen_session_lock);
    if (changed) {
        qwen_mark_activity();
        if (ready) {
            s_qwen_ctx.is_wifi_reconfiguring = 0U;
        } else {
            s_qwen_ctx.want_wss_connected = 0U;
            s_qwen_ctx.is_wss_reconnect_pending = 0U;
            s_qwen_ctx.is_mmi_reinit_pending = 0U;
            s_qwen_ctx.is_ptt_key_down = 0U;
            s_qwen_ctx.is_ptt_start_pending = 0U;
            qwen_set_dialog_phase(QWEN_DIALOG_PHASE_IDLE, "wifi_disconnected");
            qwen_request_cleanup("wifi_disconnected", 0U);
        }
    }
    qwen_ctx_unlock();
    return 1U;
}

/**
 * @brief 把 WiFi 事件作为提示，复核提供者真实连接状态后唤醒后台。
 * @param[in] event 可空联网或断网事件。
 * @param[in] user_data 订阅上下文，本实现不使用。
 */
static void qwen_on_wifi_event(const d_event_t *event, void *user_data)
{
    (void)user_data;
    if (event == NULL || (event->id != EVT_WIFI_CONNECTED && event->id != EVT_WIFI_DISCONNECTED)) return;
    qwen_log_dispatch_event(QWEN_DISPATCH_EVT_WIFI, (uint32_t)event->id);
    if (qwen_reconcile_wifi()) qwen_schedule_work("wifi_notice");
}

/**
 * @brief 设置 Qwen 实际音量，提示音可用时同步其媒体音量。
 * @param[in] volume_percent 目标音量百分比。
 * @param[in] context 后端兼容上下文，本实现不使用。
 * @return 音频未初始化返回 D_ERR_NOT_READY；其余为实际音量或提示音停止、同步结果。
 * @note 可选提示音启动失败不直接禁用 Qwen 音量；仍需确认其未保留播放占用。
 */
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
        /* 可选提示音启动失败不能直接禁用 Qwen 真实音量。 */
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

/**
 * @brief 关闭外部音量准入并等候借用归还，或重试注销本服务注册后端。
 * @return 解除成功或实际后端已不同返回 D_OK；仍在途返回 D_ERR_BUSY；其他错误原样返回。
 * @note 成功前不允许销毁音频对象。
 */
static d_err_t qwen_service_unregister_volume_backend(void)
{
#if D_PRODUCT_MUSIC_ENABLE
    uint32_t started = (uint32_t)timer_get_ms();
    for (;;) {
        uint8_t idle;
        spin_lock(&s_qwen_session_lock);
        s_external_volume_detaching = 1U;
        idle = s_external_volume_calls == 0U;
        spin_unlock(&s_qwen_session_lock);
        if (idle) return D_OK;
        if ((uint32_t)((uint32_t)timer_get_ms() - started) >= D_DEVICE_VOLUME_TIMEOUT_MS) return D_ERR_BUSY;
        os_time_dly(1);
    }
#else
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
#endif
}

/**
 * @brief 先确认音量后端不再借用，再反初始化 Qwen 音频。
 * @return 成功返回 D_OK；注销失败不销毁音频并返回实际错误。
 */
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

/**
 * @brief 创建音频前绑定准入探针，再装配实际音量所有权。
 * @return 成功返回 D_OK；初始化或注册失败返回实际错误。
 * @note 注册失败且后端仍忙时保留音频对象，只有确认注销且本次新建时才清理。
 */
static d_err_t qwen_service_init_audio(void)
{
    uint8_t had_audio = qwen_audio_is_initialized();
    d_err_t result;
    /* 任务创建前绑定，包括没有产品音乐同行的配置。 */
    qwen_audio_bind_admission(qwen_audio_peer_admitted);
    if (qwen_audio_init() != D_OK) {
        return D_ERR_IO;
    }

#if D_PRODUCT_MUSIC_ENABLE
    spin_lock(&s_qwen_session_lock);
    s_external_volume_detaching = 0U;
    spin_unlock(&s_qwen_session_lock);
    result = D_OK;
#else
    result = d_device_volume_register_backend(&s_qwen_volume_backend);
#endif
    if (result != D_OK) {
        /* BUSY 后端可能仍使用音频对象，必须保留其所有者。 */
        if (qwen_service_unregister_volume_backend() == D_OK && !had_audio) {
            qwen_audio_deinit();
        }
        return result;
    }

    return D_OK;
}

/**
 * @brief 在外部音量借用计数保护下同步设置 Qwen 播放音量；完整合同见 qwen_service.h。
 */
d_err_t qwen_service_apply_device_volume(uint8_t percent)
{
    d_err_t result;
    spin_lock(&s_qwen_session_lock);
    if (s_external_volume_detaching || !qwen_audio_is_initialized()) {
        spin_unlock(&s_qwen_session_lock); return D_ERR_NOT_READY;
    }
    ++s_external_volume_calls;
    spin_unlock(&s_qwen_session_lock);
    result = qwen_audio_set_player_volume(percent);
    spin_lock(&s_qwen_session_lock);
    --s_external_volume_calls;
    spin_unlock(&s_qwen_session_lock);
    return result;
}

/**
 * @brief 订阅 WiFi、NTP、音频、命令、WSS 与可选小乐事件。
 * @return 全部成功返回 D_OK；任一失败返回 D_ERR_IO，外层生命周期入口负责统一撤销。
 */
static d_err_t qwen_service_subscribe_events(void)
{
    if ((d_event_subscribe(EVT_WIFI_CONNECTED, qwen_on_wifi_event, NULL) != 0) ||
        (d_event_subscribe(EVT_WIFI_DISCONNECTED, qwen_on_wifi_event, NULL) != 0) ||
        (d_event_subscribe(EVT_NTP_SYNCED, qwen_on_time_event, NULL) != 0) ||
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
/**
 * @brief 向提示音提供者报告真实会话、连接或清理占用。
 * @return 仍占用或致命故障返回非零，单纯历史认证错误不算音频占用。
 */
static uint8_t qwen_local_prompt_busy(void)
{
    qwen_session_snapshot_t snapshot;
    /* 公开就绪先撤回，资源收尾随后完成。 */
    spin_lock(&s_qwen_session_lock);
    qwen_session_policy_snapshot(&s_session, &snapshot);
    spin_unlock(&s_qwen_session_lock);
    /* can_stop 涵盖待清理及清理失败；仅记忆认证错误不持有音频。 */
    return snapshot.requested || snapshot.connected || snapshot.fatal || snapshot.can_stop;
}

/**
 * @brief 为非零闹铃票据取得音频交接占用，停止自动会话并请求后台清理；完整合同见 qwen_service.h。
 */
d_err_t qwen_service_alarm_begin(uint32_t token)
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
    /* 显式打断后，网络再次就绪不能自行重建对话。 */
    s_session.automatic = 0U;
    /* 即使 IDLE 下 session_stop 会早退，也强制走完整后台清理。 */
    qwen_session_policy_cleanup(&s_session, 0U);
    /* 此票据为新的显式重试，旧失败不能作为它的完成结果。 */
    s_session.cleanup_failed = 0U;
    s_session.cleanup_error = D_OK;
    s_qwen_ctx.cleanup_retry = 1U;
    s_qwen_ctx.is_wss_reconnect_pending = 0U;
    spin_unlock(&s_qwen_session_lock);
    qwen_schedule_work("alarm_audio_claim");
    return D_OK;
}

/**
 * @brief 核对闹铃票据并检查音频会话清理及 SDK、按键借用已归还；完整合同见 qwen_service.h。
 */
d_err_t qwen_service_alarm_poll(uint32_t token, uint8_t *ready)
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
        !s_session.requested && !s_session.connected && !s_qwen_ctx.sdk_calls && !s_key_calls;
    spin_unlock(&s_qwen_session_lock);
#if D_FEATURE_XIAOLE_WAKEUP
    if (complete && xiaole_wakeup_is_running()) return D_ERR_IO;
#endif
    *ready = complete;
    return result;
}

/**
 * @brief 只解除匹配闹铃票据并唤醒后台恢复待机资格；完整合同见 qwen_service.h。
 */
void qwen_service_alarm_release(uint32_t token)
{
    uint8_t released = 0U;
    spin_lock(&s_qwen_session_lock);
    if (token && token == s_alarm_hold_token) { s_alarm_hold_token = 0U; released = 1U; }
    spin_unlock(&s_qwen_session_lock);
    if (released) qwen_schedule_work("alarm_audio_released");
}

static const d_local_prompt_handoff_t s_qwen_alarm_handoff = {
    qwen_service_alarm_begin, qwen_service_alarm_poll, qwen_service_alarm_release,
};
#endif

#if D_FEATURE_OTA
/**
 * @brief 非阻塞取得维护准入，关闭自动会话并请求完整后台清理；完整合同见 qwen_service.h。
 */
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

/**
 * @brief 检查维护清理、SDK 借用、按键事务及后台准备均已完成；完整合同见 qwen_service.h。
 */
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
        !s_session.requested && !s_session.connected && !s_qwen_ctx.sdk_calls && !s_key_calls &&
        !s_qwen_ctx.worker_prepare_active && !s_qwen_ctx.wifi_reconfig_active;
    spin_unlock(&s_qwen_session_lock);
#if D_FEATURE_XIAOLE_WAKEUP
    if (complete && xiaole_wakeup_is_running()) return D_ERR_BUSY;
#endif
    *ready = complete;
    return result;
}

/**
 * @brief 确认维护全部就绪后解除占用并安排恢复本地待机；完整合同见 qwen_service.h。
 */
d_err_t qwen_service_maintenance_release(void)
{
    uint8_t ready;
    d_err_t result = qwen_service_maintenance_poll(&ready);
    if (result != D_OK || !ready) return result != D_OK ? result : D_ERR_BUSY;
    spin_lock(&s_qwen_session_lock); s_maintenance_hold = 0U; spin_unlock(&s_qwen_session_lock);
    /* 这里只恢复待机资格，自动对话保持关闭，直到用户显式开始新会话。 */
    qwen_schedule_work("ota_released");
    return D_OK;
}
#endif

/**
 * @brief 装配接入与音频、创建持久工作任务并订阅事件，最后开放输入准入；完整合同见 qwen_service.h。
 */
d_err_t qwen_service_init(void)
{
#if D_FEATURE_AI_ALARM
    qwen_local_tool_waker(qwen_tool_wake);
#endif
    d_err_t err;

    if (s_qwen_ctx.is_initialized != 0U) {
        return D_ERR_BUSY;
    }
    err = qwen_access_configure_product();
    if (err != D_OK) return err;
#if D_QWEN_ACTIVATION_ENABLE
    err = qwen_activation_init();
    if (err != D_OK) return err;
    s_activation_command_active = 0U;
#endif
#if D_LOCAL_PROMPT_ENABLE
#if !D_PRODUCT_MUSIC_ENABLE
    if (d_local_prompt_register_foreground_probe(qwen_local_prompt_busy) != D_OK) return D_ERR_BUSY;
    if (d_local_prompt_register_alarm_handoff(&s_qwen_alarm_handoff) != D_OK) return D_ERR_BUSY;
#endif
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
    (void)qwen_reconcile_wifi();
    if (s_qwen_ctx.is_wifi_connected) qwen_schedule_work("initial_wifi_snapshot");
#if D_FEATURE_POWER_CONTROL
#if !D_PRODUCT_MUSIC_ENABLE
    power_ctrl_set_prepare_shutdown_callback(qwen_service_prepare_shutdown, NULL);
#endif
#endif
    spin_lock(&s_qwen_session_lock);
    s_key_closing = 0U;
    s_qwen_ctx.service_ready = 1U;
    spin_unlock(&s_qwen_session_lock);
    qwen_schedule_work("service_ready");
    LOGI(TAG, "Qwen service init done, ptt_key=%u", (unsigned)D_QWEN_PTT_KEY_VALUE);

    return D_OK;
}

/**
 * @brief 关闭输入并等待借用、音量与唯一工作任务受检收尾；完整合同见 qwen_service.h。
 */
void qwen_service_deinit(void)
{
    uint8_t was_ready;

    if (s_qwen_ctx.is_initialized == 0U) {
        return;
    }
    spin_lock(&s_qwen_session_lock);
    /* 要求生命周期调用者重试忙碌清理前，先关闭输入准入。 */
    s_key_closing = 1U;
    if (s_qwen_ctx.wifi_reconfig_active || s_key_calls) {
        spin_unlock(&s_qwen_session_lock);
        LOGW(TAG, "service teardown waits for key transaction; retry deinit");
        return;
    }
    was_ready = s_qwen_ctx.service_ready;
    s_qwen_ctx.service_ready = 0U;
    spin_unlock(&s_qwen_session_lock);
    if (qwen_service_unregister_volume_backend() != D_OK) {
        spin_lock(&s_qwen_session_lock);
        s_qwen_ctx.service_ready = was_ready;
        spin_unlock(&s_qwen_session_lock);
        return;
    }

    d_event_unsubscribe(EVT_WIFI_CONNECTED, qwen_on_wifi_event);
    d_event_unsubscribe(EVT_WIFI_DISCONNECTED, qwen_on_wifi_event);
    d_event_unsubscribe(EVT_NTP_SYNCED, qwen_on_time_event);
    d_event_unsubscribe(EVT_QWEN_TTS_LOCAL_END, qwen_on_audio_event);
    d_event_unsubscribe(EVT_QWEN_CMD_RECEIVED, qwen_on_cmd_event);
    d_event_unsubscribe(EVT_QWEN_WSS_DISCONNECTED, qwen_on_wss_event);
#if D_FEATURE_XIAOLE_WAKEUP
    d_event_unsubscribe(EVT_XIAOLE_WAKEUP_DETECTED, qwen_on_xiaole_event);
#endif
#if D_FEATURE_POWER_CONTROL
#if !D_PRODUCT_MUSIC_ENABLE
    power_ctrl_set_prepare_shutdown_callback(NULL, NULL);
#endif
#endif
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

#if D_PRODUCT_MUSIC_ENABLE
/**
 * @brief 只撤销仍属于指定代次的产品音频准入，并安排后台清理。
 * @param[in] generation 本次准入捕获的会话代次。
 * @note 迟到准入失败不能取消已替换的新会话。
 */
static void qwen_cancel_audio_admission(uint32_t generation)
{
    uint8_t canceled = 0U;
    spin_lock(&s_qwen_session_lock);
    if (s_session.generation == generation && s_session.requested) {
        qwen_session_policy_cleanup(&s_session, 0U);
        s_qwen_ctx.cleanup_retry = 1U;
        s_qwen_ctx.is_wss_reconnect_pending = 0U;
        canceled = 1U;
    }
    spin_unlock(&s_qwen_session_lock);
    /* 工具取消与音频排空由工作任务完成；迟到准入结果不能取消已替换的新代次。 */
    if (canceled) qwen_schedule_work("media_request_canceled");
}
#endif

/**
 * @brief 与维护同锁核对可选检测代次，受理会话并申请产品音频占用。
 * @param[in] expected_generation 可空；非空要求仍匹配检测时会话代次。
 * @return 受理返回 D_OK；服务、代次或运行资格未就绪及占用冲突返回对应错误。
 * @note 产品 begin 非阻塞；其返回前后再次核对仍请求同代次，失败只释放并取消本次代次。
 */
static d_err_t qwen_session_start_checked(const uint32_t *expected_generation)
{
    d_err_t result;
#if D_PRODUCT_MUSIC_ENABLE
    uint32_t accepted_generation;
#endif

    if (!s_qwen_ctx.service_ready) return D_ERR_NOT_READY;
#if D_LOCAL_PROMPT_ENABLE
    if (d_local_prompt_alarm_requested()) return D_ERR_BUSY;
#endif
    if (!qwen_reconcile_wifi()) return D_ERR_BUSY;
    spin_lock(&s_qwen_session_lock);
#if D_QWEN_ACTIVATION_ENABLE
    if (s_activation_command_active) { spin_unlock(&s_qwen_session_lock); return D_ERR_BUSY; }
#endif
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
#if D_PRODUCT_MUSIC_ENABLE
    accepted_generation = s_session.generation;
#endif
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
#if D_PRODUCT_MUSIC_ENABLE
        if (s_audio_peer != NULL) {
            uint8_t still_requested;
            spin_lock(&s_qwen_session_lock);
            still_requested = s_session.requested && s_session.generation == accepted_generation;
            if (still_requested) s_audio_peer_generation = accepted_generation;
            spin_unlock(&s_qwen_session_lock);
            if (!still_requested) return D_ERR_BUSY;
            result = s_audio_peer->begin(accepted_generation);
            spin_lock(&s_qwen_session_lock);
            still_requested = s_session.requested && s_session.generation == accepted_generation;
            spin_unlock(&s_qwen_session_lock);
            if (result != D_OK || !still_requested) {
                s_audio_peer->release(accepted_generation);
                qwen_cancel_audio_admission(accepted_generation);
                return result != D_OK ? result : D_ERR_BUSY;
            }
        }
#endif
#if D_LOCAL_PROMPT_ENABLE
        d_local_prompt_foreground_begin();
#endif
        qwen_schedule_work("session_start");
    }
    return result;
}

/**
 * @brief 非阻塞受理会话启动，重复有效请求保持幂等；完整合同见 qwen_service.h。
 */
d_err_t qwen_service_session_start(void)
{
    return qwen_session_start_checked(NULL);
}

/**
 * @brief 空闲会话下提交 UI 激活码，或以 NULL 重试已有激活工作；完整合同见 qwen_service.h。
 */
d_err_t qwen_service_activation_request(const char *code)
{
#if D_QWEN_ACTIVATION_ENABLE
    d_err_t result;
    if (!s_qwen_ctx.service_ready) return D_ERR_NOT_READY;
#if D_LOCAL_PROMPT_ENABLE
    if (d_local_prompt_alarm_requested()) return D_ERR_BUSY;
#endif
#if D_FEATURE_POWER_CONTROL
    if (power_ctrl_is_shutting_down()) return D_ERR_BUSY;
#endif
    spin_lock(&s_qwen_session_lock);
    if (s_activation_command_active || s_qwen_ctx.worker_prepare_active ||
        s_session.requested || s_session.connected || s_session.cleanup_pending || s_session.fatal
#if D_FEATURE_OTA
        || s_maintenance_hold
#endif
#if D_LOCAL_PROMPT_ENABLE
        || s_alarm_hold_token
#endif
        ) { spin_unlock(&s_qwen_session_lock); return D_ERR_BUSY; }
    s_activation_command_active = 1U;
    spin_unlock(&s_qwen_session_lock);
    result = code ? qwen_activation_submit(code) : qwen_activation_retry();
    spin_lock(&s_qwen_session_lock);
    if (result == D_OK) {
        qwen_session_policy_runtime(&s_session, 0U);
        qwen_session_policy_cleanup(&s_session, 0U);
        s_qwen_ctx.cleanup_retry = 1U;
    }
    s_activation_command_active = 0U;
    spin_unlock(&s_qwen_session_lock);
    if (result == D_OK) qwen_schedule_work("activation_input");
    return result;
#else
    (void)code;
    return D_ERR_NOT_READY;
#endif
}

/**
 * @brief 撤销会话启动意图并请求后台清理；完整合同见 qwen_service.h。
 */
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

/**
 * @brief 复制服务缓存、纯策略与工具通知，不调用网络或 SDK；完整合同见 qwen_service.h。
 */
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
#if D_FEATURE_AI_ALARM
    qwen_local_tool_get_snapshot(&snapshot->tool);
#endif
#if D_LOCAL_PROMPT_ENABLE
    if (d_local_prompt_alarm_requested()) snapshot->session.can_start = 0U;
#endif
    return D_OK;
}

/**
 * @brief 在持 SDK 借用时检查 PTT 资格，先发布轮次再提交 SDK 开始。
 * @return 受理返回 D_OK；Duplex 或状态不符返回 D_ERR_NOT_READY；已有上行返回 D_ERR_BUSY；其余为 SDK 映射。
 * @note SDK 返回前可同步回调，错误回退只影响仍匹配的会话和轮次。
 */
static d_err_t qwen_speech_start_impl(void)
{
#if (D_QWEN_DIALOG_MODE == D_QWEN_DIALOG_MODE_DUPLEX)
    LOGI(TAG, "speech_start ignored: dialog=duplex state=%s", qwen_state_name(s_qwen_ctx.state));
    return D_ERR_NOT_READY;
#else
    d_err_t err;
    uint32_t generation;
    uint32_t round;

    qwen_ctx_lock();
    LOGI(TAG, "speech_start req: state=%s wifi=%u speech=%u key_down=%u",
         qwen_state_name(s_qwen_ctx.state),
         (unsigned)s_qwen_ctx.is_wifi_connected,
         (unsigned)s_qwen_ctx.is_speech_active,
         (unsigned)s_qwen_ctx.is_ptt_key_down);

    if ((s_qwen_ctx.is_initialized == 0U) || (s_qwen_ctx.is_wifi_connected == 0U) ||
        !qwen_session_allows_business()) {
        LOGW(TAG, "speech_start reject: not ready");
        qwen_ctx_unlock();
        return D_ERR_NOT_READY;
    }

    if ((s_qwen_ctx.state != QWEN_STATE_READY) &&
        (s_qwen_ctx.state != QWEN_STATE_PLAYING) &&
        (s_qwen_ctx.state != QWEN_STATE_THINKING)) {
        LOGW(TAG, "speech_start reject: state=%s", qwen_state_name(s_qwen_ctx.state));
        qwen_ctx_unlock();
        return D_ERR_NOT_READY;
    }

    if (s_qwen_ctx.is_speech_active != 0U) {
        LOGW(TAG, "speech_start reject: speech active");
        qwen_ctx_unlock();
        return D_ERR_BUSY;
    }

    generation = qwen_session_generation();
    if (s_qwen_ctx.dialog_phase != QWEN_DIALOG_PHASE_PTT_PREPARING)
        s_qwen_ctx.dialog_round_id++;
    round = s_qwen_ctx.dialog_round_id;
    s_qwen_ctx.is_speech_active = 1U;
    qwen_set_dialog_phase(QWEN_DIALOG_PHASE_PTT_PREPARING, "speech_start");
    qwen_ctx_unlock();
    err = qwen_map_sdk_err(c_mmi_speech_start());
    if (err != D_OK) {
        LOGW(TAG, "speech_start sdk err=%d", err);
        qwen_ctx_lock();
        if (generation == qwen_session_generation() && round == s_qwen_ctx.dialog_round_id)
            qwen_finish_dialog_failed("sdk_speech_start", err);
        qwen_ctx_unlock();
        return err;
    }
    LOGI(TAG, "speech_start accepted");
    return D_OK;
#endif
}

/**
 * @brief 在持 SDK 借用时撤销上行意图并提交 SDK 结束。
 * @return 成功或无上行返回 D_OK；未就绪或 SDK 失败返回实际错误；Duplex 分支固定 D_OK。
 */
static d_err_t qwen_speech_end_impl(void)
{
#if (D_QWEN_DIALOG_MODE == D_QWEN_DIALOG_MODE_DUPLEX)
    LOGI(TAG, "speech_end ignored: dialog=duplex state=%s", qwen_state_name(s_qwen_ctx.state));
    return D_OK;
#else
    d_err_t err;
    uint32_t generation;
    uint32_t round;

    qwen_ctx_lock();
    LOGI(TAG, "speech_end req: state=%s wifi=%u speech=%u key_down=%u",
         qwen_state_name(s_qwen_ctx.state),
         (unsigned)s_qwen_ctx.is_wifi_connected,
         (unsigned)s_qwen_ctx.is_speech_active,
         (unsigned)s_qwen_ctx.is_ptt_key_down);

    if ((s_qwen_ctx.is_initialized == 0U) || (s_qwen_ctx.is_wifi_connected == 0U) ||
        !qwen_session_allows_business()) {
        LOGW(TAG, "speech_end reject: not ready");
        qwen_ctx_unlock();
        return D_ERR_NOT_READY;
    }

    if (s_qwen_ctx.is_speech_active == 0U) {
        LOGI(TAG, "speech_end skip: speech inactive");
        qwen_ctx_unlock();
        return D_OK;
    }

    generation = qwen_session_generation();
    round = s_qwen_ctx.dialog_round_id;
    s_qwen_ctx.is_speech_active = 0U;
    if (qwen_dialog_phase_is_active() != 0U)
        qwen_set_dialog_phase(QWEN_DIALOG_PHASE_PTT_ENDING, "speech_end");
    qwen_ctx_unlock();
    err = qwen_map_sdk_err(c_mmi_speech_end());
    if (err != D_OK) {
        LOGW(TAG, "speech_end sdk err=%d", err);
        qwen_ctx_lock();
        if (generation == qwen_session_generation() && round == s_qwen_ctx.dialog_round_id)
            qwen_finish_dialog_failed("sdk_speech_end", err);
        qwen_ctx_unlock();
        return err;
    }

    LOGI(TAG, "speech_end accepted");
    return D_OK;
#endif
}

/**
 * @brief 在持 SDK 借用时暂停 SDK，并清本地对话与音频意图。
 * @return SDK 暂停映射结果；未初始化返回 D_ERR_NOT_READY。
 * @note 即使 SDK 出错仍按实现清本地标记并投递音频停止。
 */
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

/**
 * @brief 持借用时核对文本与响应槽准入，发布等待 LLM 阶段后提交。
 * @param[in] text 非空有效问题文本。
 * @return 受理返回 D_OK；参数、工具或槽冲突、状态与 SDK 失败返回对应错误。
 * @note SDK 调用在上下文锁外，失败只回退仍匹配的轮次。
 */
static d_err_t qwen_question_impl(const char *text)
{
    d_err_t err;
    uint32_t generation;
    uint32_t round;

    qwen_ctx_lock();
#if D_FEATURE_AI_ALARM
    if (qwen_local_tool_busy()) { qwen_ctx_unlock(); return D_ERR_BUSY; }
#endif

    if ((text == NULL) || (text[0] == '\0')) {
        qwen_ctx_unlock();
        return D_ERR_INVALID_ARG;
    }

    if ((s_qwen_ctx.is_initialized == 0U) || (s_qwen_ctx.state != QWEN_STATE_READY) ||
        (s_qwen_ctx.is_text_request_ready == 0U) || (qwen_dialog_phase_is_active() != 0U) ||
        !qwen_session_allows_business()) {
        LOGW(TAG, "question reject: state=%s phase=%s text_req=%u",
             qwen_state_name(s_qwen_ctx.state),
             qwen_dialog_phase_name(s_qwen_ctx.dialog_phase),
             (unsigned)s_qwen_ctx.is_text_request_ready);
        qwen_ctx_unlock();
        return D_ERR_NOT_READY;
    }

    if (!qwen_response_begin()) { qwen_ctx_unlock(); return D_ERR_BUSY; }
    generation = qwen_session_generation();
    s_qwen_ctx.is_text_request_ready = 0U;
    round = ++s_qwen_ctx.dialog_round_id;
    qwen_set_dialog_phase(QWEN_DIALOG_PHASE_WAIT_LLM, "question");
    qwen_set_state(QWEN_STATE_THINKING);
    qwen_ctx_unlock();
    /* SDK 可能同步回复，调用前先发布本次准入。 */
    err = qwen_response_submit(0U, text);
    if (err != D_OK) {
        qwen_ctx_lock();
        if (generation == qwen_session_generation() && round == s_qwen_ctx.dialog_round_id)
            qwen_finish_request_failed("sdk_question", err);
        qwen_ctx_unlock();
        return err;
    }
    return D_OK;
}

/**
 * @brief 持借用时核对文本与响应槽准入，发布等待 TTS 阶段后提交。
 * @param[in] text 非空有效播报文本。
 * @return 受理返回 D_OK；参数、冲突、状态与 SDK 失败返回对应错误。
 */
static d_err_t qwen_tts_impl(const char *text)
{
    d_err_t err;
    uint32_t generation;
    uint32_t round;

    qwen_ctx_lock();
#if D_FEATURE_AI_ALARM
    if (qwen_local_tool_busy()) { qwen_ctx_unlock(); return D_ERR_BUSY; }
#endif

    if ((text == NULL) || (text[0] == '\0')) {
        qwen_ctx_unlock();
        return D_ERR_INVALID_ARG;
    }

    if ((s_qwen_ctx.is_initialized == 0U) || (s_qwen_ctx.state != QWEN_STATE_READY) ||
        (s_qwen_ctx.is_text_request_ready == 0U) || (qwen_dialog_phase_is_active() != 0U) ||
        !qwen_session_allows_business()) {
        LOGW(TAG, "tts reject: state=%s phase=%s text_req=%u",
             qwen_state_name(s_qwen_ctx.state),
             qwen_dialog_phase_name(s_qwen_ctx.dialog_phase),
             (unsigned)s_qwen_ctx.is_text_request_ready);
        qwen_ctx_unlock();
        return D_ERR_NOT_READY;
    }

    if (!qwen_response_begin()) { qwen_ctx_unlock(); return D_ERR_BUSY; }
    generation = qwen_session_generation();
    s_qwen_ctx.is_text_request_ready = 0U;
    round = ++s_qwen_ctx.dialog_round_id;
    qwen_set_dialog_phase(QWEN_DIALOG_PHASE_WAIT_TTS, "tts");
    qwen_set_state(QWEN_STATE_THINKING);
    qwen_ctx_unlock();
    err = qwen_response_submit(1U, text);
    if (err != D_OK) {
        qwen_ctx_lock();
        if (generation == qwen_session_generation() && round == s_qwen_ctx.dialog_round_id)
            qwen_finish_request_failed("sdk_tts", err);
        qwen_ctx_unlock();
        return err;
    }
    return D_OK;
}

/**
 * @brief 在当前会话准入下非阻塞提交统一设备音量请求；完整合同见 qwen_service.h。
 */
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

/**
 * @brief 取得 SDK 借用后同步执行一个无参数业务动作，最后归还。
 * @param[in] operation 非空同步动作回调。
 * @return 准入失败或动作的实际结果。
 */
static d_err_t qwen_sdk_run(d_err_t (*operation)(void))
{
    d_err_t result = qwen_sdk_enter();
    if (result != D_OK) return result;
    result = operation();
    qwen_sdk_leave();
    return result;
}

/**
 * @brief 取得 SDK 借用后提交按键对讲开始上行；完整合同见 qwen_service.h。
 */
d_err_t qwen_service_speech_start(void)
{
    return qwen_sdk_run(qwen_speech_start_impl);
}

/**
 * @brief 取得 SDK 借用后提交按键对讲结束上行；完整合同见 qwen_service.h。
 */
d_err_t qwen_service_speech_end(void)
{
    return qwen_sdk_run(qwen_speech_end_impl);
}

/**
 * @brief 取得 SDK 借用后提交暂停，清本地上行意图并请求音频停止；完整合同见 qwen_service.h。
 */
d_err_t qwen_service_speech_pause(void)
{
    return qwen_sdk_run(qwen_speech_pause_impl);
}

/**
 * @brief 在就绪、可文本请求且无活动轮次时提交 UTF-8 文本问题；完整合同见 qwen_service.h。
 */
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

/**
 * @brief 在文本准入允许时提交直接 TTS 请求；完整合同见 qwen_service.h。
 */
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

/**
 * @brief 单击取消未播欢迎语，空闲时仅停播放器，否则请求暂停当前轮次。
 * @param[in] key_type 用于诊断的按键类型。
 * @param[in] key_action 用于诊断的点击动作。
 * @param[in] key_value 用于诊断的产品按键值。
 * @return 空闲处理成功返回 D_OK；其余为 speech_pause 结果。
 */
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
/**
 * @brief 后台请求完整清理，并在关机期限内等待清理及按键借用结束。
 * @param[in] user_data 兼容关机上下文，本实现不使用。
 * @return 未初始化或完成返回 D_OK；致命故障返回 D_ERR_IO；期限到返回 D_ERR_TIMEOUT。
 */
static d_err_t qwen_service_prepare_shutdown(void *user_data)
{
    uint32_t started;
    uint8_t is_clean;
    qwen_service_snapshot_t snapshot;

    (void)user_data;
    if (!s_qwen_ctx.is_initialized) return D_OK;
    qwen_request_cleanup("shutdown", 0U);
    started = timer_get_ms();
    do {
        spin_lock(&s_qwen_session_lock);
        qwen_session_policy_snapshot(&s_session, &snapshot.session);
        is_clean = !s_session.cleanup_pending && !s_qwen_ctx.wifi_reconfig_active && !s_key_calls;
        spin_unlock(&s_qwen_session_lock);
        if (is_clean) return D_OK;
        if (snapshot.session.fatal) return D_ERR_IO;
        os_time_dly(1);
    } while ((uint32_t)(timer_get_ms() - started) < QWEN_SESSION_SHUTDOWN_MS);
    return D_ERR_TIMEOUT;
}
#endif

/**
 * @brief 请求后台完整清理并限时等候按键事务与清理结束；完整合同见 qwen_service.h。
 */
d_err_t qwen_service_shutdown_wait(void)
{
#if D_FEATURE_POWER_CONTROL
    return qwen_service_prepare_shutdown(NULL);
#else
    return D_OK;
#endif
}

/**
 * @brief 从兼容唤醒来源受理会话启动并记录来源；完整合同见 qwen_service.h。
 */
d_err_t qwen_service_wakeup_from_idle(const char *source)
{
    d_err_t result = qwen_service_session_start();
    LOGI(TAG, "session trigger source=%s result=%d", source != NULL ? source : "unknown", result);
    return result;
}

/**
 * @brief 按模式把产品输入映射为打断、按住说话、抬起结束或重新配网。
 * @param[in] key_value 产品已匹配的按键值。
 * @param[in] key_action SDK 动作。
 * @param[in] user_data 兼容上下文，本实现不使用。
 * @return 按键已处理返回 1，Duplex 中未匹配动作可返回 0。
 * @note 由外层资源借用保护；这里不宣称 SDK、WiFi 或音频动作已经完成。
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
        if (!qwen_can_use_live_session() && qwen_net_is_token_valid() == 0) {
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

/**
 * @brief 处理已由产品映射的输入，并在同步调用期间借用服务资源；完整合同见 qwen_service.h。
 */
int qwen_service_handle_key_event(uint8_t key_type, uint8_t key_action, uint8_t key_value)
{
    int result;
    (void)key_type;
    spin_lock(&s_qwen_session_lock);
    if (!s_qwen_ctx.service_ready || s_key_closing) {
        spin_unlock(&s_qwen_session_lock);
        return D_KEY_REJECTED;
    }
    if (s_qwen_ctx.cleanup_active
#if D_FEATURE_OTA
        || s_maintenance_hold
#endif
#if D_LOCAL_PROMPT_ENABLE
        || s_alarm_hold_token
#endif
        ) {
        result = D_KEY_REJECTED;
#if D_QWEN_DIALOG_MODE == D_QWEN_DIALOG_MODE_PUSH2TALK
        if (key_action == KEY_EVENT_UP) {
            /* 只清输入意图而不借用资源；清理可保留重连请求，但不能保留用户已经松开的按下状态。 */
            s_qwen_ctx.is_ptt_key_down = 0U;
            s_qwen_ctx.is_ptt_start_pending = 0U;
            s_qwen_ctx.is_ptt_prepare_failed = 0U;
            result = D_KEY_CONSUMED;
        }
#endif
        spin_unlock(&s_qwen_session_lock);
        return result;
    }
    if (s_key_calls == UINT16_MAX) {
        spin_unlock(&s_qwen_session_lock);
        return D_KEY_REJECTED;
    }
    s_key_calls++;
    spin_unlock(&s_qwen_session_lock);
    result = qwen_ptt_handler(key_value, key_action, NULL);
    spin_lock(&s_qwen_session_lock);
    s_key_calls--;
    spin_unlock(&s_qwen_session_lock);
    return result;
}

/**
 * @brief 读取当前缓存的公开服务状态；完整合同见 qwen_service.h。
 */
qwen_state_e qwen_service_get_state(void)
{
    return s_qwen_ctx.state;
}

/**
 * @brief 检查内部轮次活动或公开状态是否仍属于工作阶段；完整合同见 qwen_service.h。
 */
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
