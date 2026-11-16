/** @file product_ui_media_runtime.c @brief 产品语音、闹铃、音乐及音量的所有者协调，不持有 SDK 句柄或页面对象。 */
#include "config/app_config.h"
#if D_PRODUCT_MUSIC_ENABLE
#include "product_ui_media_runtime.h"
#include "modules/services/local_music/music_service.h"
#include "modules/services/audio/d_device_volume.h"
#include "modules/services/audio/d_local_prompt.h"
#if D_FEATURE_QWEN
#include "modules/integrations/qwen/qwen_service.h"
#endif
#if D_FEATURE_POWER_CONTROL
#include "modules/services/power/power_ctrl.h"
#endif
#include "system/includes.h"
#include "system/spinlock.h"
#include "system/timer.h"

static DEFINE_SPINLOCK(s_media_lock);
/* 预留阶段只描述本协调器，不代表其他所有者的解码状态；
 * begin 在锁外执行期间仍可能收到释放意图。 */
typedef enum {
    PRODUCT_MEDIA_HANDOFF_IDLE,
    PRODUCT_MEDIA_HANDOFF_BEGINNING,
    PRODUCT_MEDIA_HANDOFF_RESERVED
} product_media_handoff_phase_e;

typedef struct {
    uint32_t generation;
    product_media_handoff_phase_e phase;
    d_err_t begin_result;
    uint8_t is_release_requested;
} product_media_voice_state_t;

static product_media_voice_state_t s_voice;
typedef struct {
    uint32_t token;
    product_media_handoff_phase_e phase;
    d_err_t begin_result;
    uint8_t is_release_requested;
    uint8_t is_qwen_held;
} product_media_alert_state_t;

static product_media_alert_state_t s_alert;
#define PRODUCT_MEDIA_MAINT_OTA 1U
#define PRODUCT_MEDIA_MAINT_POWER 2U
#define PRODUCT_MEDIA_VOLUME_MUSIC 1U
#define PRODUCT_MEDIA_VOLUME_QWEN 2U
#define PRODUCT_MEDIA_VOLUME_PROMPT 4U
#define PRODUCT_MEDIA_FOREGROUND_MUSIC 2U
#define PRODUCT_MEDIA_FOREGROUND_VOLUME_FAULT 4U
#define PRODUCT_MEDIA_RECONCILE_MS 100U

/* 独立客户端可同时持有各自维护占用。 */
typedef struct {
    uint32_t generation;
    uint8_t owners;
    uint8_t release_requested;
} product_media_maintenance_state_t;

/* 执行在途、故障保留与恢复确认是独立状态，不能相互替代。 */
typedef struct {
    uint32_t transaction;
    uint8_t is_applying;
    uint8_t has_fault;
    uint8_t fault_mask;
    uint32_t hold_ticket;
    uint32_t recovery_transaction;
    uint8_t is_recovery_pending;
    uint8_t is_recovering;
    uint8_t recovery_percent;
} product_media_volume_state_t;

typedef struct {
    uint8_t is_ready;
    uint8_t is_release_pending;
    uint16_t reconcile_timer; /* 仅由串行启动阶段创建。 */
} product_media_startup_state_t;

static product_media_maintenance_state_t s_media_maintenance;
static product_media_volume_state_t s_media_volume;
static product_media_startup_state_t s_media_startup;
static uint8_t s_music_prepare_in_progress;
static void product_ui_media_reconcile_volume(void);
static void product_ui_media_reconcile_maintenance(void);
static void product_ui_media_reconcile_releases(void);

/**
 * @brief 合并协调器语音预留状态与可取得的 Qwen 会话忙状态。
 * @return 存在预留或观察到会话请求、连接、可停止或致命状态返回非零，否则为零。
 * @note 先后两次检查预留状态，覆盖锁外快照期间发生的预留竞争。
 */
static uint8_t product_ui_media_voice_busy(void)
{
#if D_FEATURE_QWEN
    qwen_service_snapshot_t voice;
    uint8_t reserved, observed_busy = 0U;
    spin_lock(&s_media_lock);
    reserved = s_voice.phase != PRODUCT_MEDIA_HANDOFF_IDLE;
    spin_unlock(&s_media_lock);
    if (reserved) return 1U;
    if (qwen_service_get_snapshot(&voice) == D_OK)
        observed_busy = voice.session.requested || voice.session.connected || voice.session.can_stop || voice.session.fatal;
    /* 拆卸中快照可能不可用，锁外取快照期间也可能并发出现 begin 预留。 */
    spin_lock(&s_media_lock);
    reserved = s_voice.phase != PRODUCT_MEDIA_HANDOFF_IDLE;
    spin_unlock(&s_media_lock);
    return reserved || observed_busy;
#endif
    return 0U;
}

/**
 * @brief 检查启动、维护、闹铃、语音、音量及关机门禁，决定音乐准入。
 * @return 全部允许返回 D_OK；任一占用或在途条件返回 BUSY。
 */
static d_err_t product_ui_media_permit(void)
{
    uint8_t allowed;
    d_device_volume_snapshot_t volume;
    spin_lock(&s_media_lock);
    allowed = s_media_startup.is_ready && !s_media_maintenance.owners && s_alert.phase == PRODUCT_MEDIA_HANDOFF_IDLE &&
        s_voice.phase == PRODUCT_MEDIA_HANDOFF_IDLE && !s_media_volume.has_fault;
    spin_unlock(&s_media_lock);
    if (!allowed || product_ui_media_voice_busy() || d_local_prompt_alarm_requested()) return D_ERR_BUSY;
    d_device_volume_get_snapshot(&volume);
    if (volume.is_in_progress || volume.is_pending) return D_ERR_BUSY;
#if D_FEATURE_POWER_CONTROL
    if (power_ctrl_is_shutting_down()) return D_ERR_BUSY;
#endif
    return D_OK;
}

/**
 * @brief 标记音乐准备在途，取得提示音前台位并限时等待提示音释放。
 * @return 音量正在应用返回 BUSY；其余返回提示音最多 1500 毫秒等待的结果。
 * @note 等待发生在媒体锁外；音乐准备在途标志阻止音量形成循环等待。
 */
static d_err_t product_ui_media_prepare(void)
{
    d_err_t result;
    spin_lock(&s_media_lock);
    if (s_media_volume.is_applying) {
        spin_unlock(&s_media_lock);
        return D_ERR_BUSY;
    }
    s_music_prepare_in_progress = 1U;
    spin_unlock(&s_media_lock);
    d_local_prompt_foreground_set(PRODUCT_MEDIA_FOREGROUND_MUSIC, 1U);
    result = d_local_prompt_wait_stopped(1500U);
    spin_lock(&s_media_lock);
    s_music_prepare_in_progress = 0U;
    spin_unlock(&s_media_lock);
    return result;
}

/**
 * @brief 音乐释放后撤销其提示音前台位，并推进挂起的释放意图。
 */
static void product_ui_media_released(void)
{
    d_local_prompt_foreground_set(PRODUCT_MEDIA_FOREGROUND_MUSIC, 0U);
    product_ui_media_reconcile_releases();
}

/**
 * @brief 合并保留故障、音量在途、音乐资源及语音状态作为提示音前台探针。
 * @return 任一条件成立返回非零，否则为零。
 */
static uint8_t product_ui_media_foreground_busy(void)
{
    music_snapshot_t music;
    d_device_volume_snapshot_t volume;
    uint8_t fault;
    music_service_get_snapshot(&music);
    d_device_volume_get_snapshot(&volume);
    spin_lock(&s_media_lock);
    fault = s_media_volume.has_fault;
    spin_unlock(&s_media_lock);
    return fault || volume.is_in_progress || music.owned || music.busy || product_ui_media_voice_busy();
}

/**
 * @brief 按半周期环形比较判断非零票据是否新于既有票据。
 * @param[in] ticket 待受理票据。
 * @param[in] previous 此前已观察的票据。
 * @return 非零且旧值为空，或差值在半周期内返回非零；相同、零或过旧返回零。
 */
static uint8_t product_ui_media_newer(uint32_t ticket, uint32_t previous)
{
    return ticket && (!previous || (ticket != previous && (uint32_t)(ticket - previous) < 0x80000000U));
}

#if D_FEATURE_QWEN
/**
 * @brief 在媒体锁内复核语音代次、未释放意图及其他准入占用。
 * @param[in] generation 本次语音请求代次。
 * @return 当前代次仍允许继续返回非零，否则为零。
 * @note 调用方持有 s_media_lock；每次锁外轮询返回后必须重新检查。
 */
static uint8_t product_ui_media_voice_admitted_locked(uint32_t generation)
{
    return generation == s_voice.generation && s_voice.phase != PRODUCT_MEDIA_HANDOFF_IDLE &&
        !s_voice.is_release_requested && s_alert.phase == PRODUCT_MEDIA_HANDOFF_IDLE &&
        !s_media_maintenance.owners && !s_media_volume.has_fault;
}

/**
 * @brief 预留新语音代次，在锁外请求音乐持有，再记录开始结果。
 * @param[in] generation 非零且环形递增的语音代次。
 * @return 同代次在开始中返回 D_OK，已预留返回原开始结果；旧代次返回 BUSY；新代次返回音乐持有开始结果。
 * @note 开始受理与完成分离，实际准备由 poll 确认；锁外 begin 期间到达的释放意图仍保留。
 */
static d_err_t product_ui_media_voice_begin(uint32_t generation)
{
    d_err_t result;
    spin_lock(&s_media_lock);
    if (generation == s_voice.generation && s_voice.phase != PRODUCT_MEDIA_HANDOFF_IDLE &&
        !s_voice.is_release_requested) {
        /* 重复请求加入已受理事务；完成确认由 poll 负责。 */
        result = s_voice.phase == PRODUCT_MEDIA_HANDOFF_BEGINNING ? D_OK : s_voice.begin_result;
        spin_unlock(&s_media_lock);
        return result;
    }
    if (!product_ui_media_newer(generation, s_voice.generation)) {
        spin_unlock(&s_media_lock);
        return D_ERR_BUSY;
    }
    s_voice.generation = generation;
    s_voice.phase = PRODUCT_MEDIA_HANDOFF_BEGINNING;
    s_voice.is_release_requested = 0U;
    spin_unlock(&s_media_lock);
    result = music_service_hold_begin(MUSIC_HOLD_VOICE, generation);
    spin_lock(&s_media_lock);
    if (s_voice.generation == generation) {
        s_voice.begin_result = result;
        s_voice.phase = PRODUCT_MEDIA_HANDOFF_RESERVED;
    }
    spin_unlock(&s_media_lock);
    return result;
}

/**
 * @brief 检查语音代次及准入，再在锁外轮询音乐，返回前重新验证。
 * @param[in] generation 待确认语音代次。
 * @param[out] ready 必填，默认清零；只有匹配且真实完成才可置位。
 * @return 参数为空为 INVALID_ARG，失配或准入撤销为 BUSY；开始中返回 D_OK 且不就绪，其余为音乐结果。
 */
static d_err_t product_ui_media_voice_poll(uint32_t generation, uint8_t *ready)
{
    uint8_t matches, pending, initialized;
    d_err_t result;
    if (ready == NULL) return D_ERR_INVALID_ARG;
    *ready = 0U;
    spin_lock(&s_media_lock);
    matches = product_ui_media_voice_admitted_locked(generation);
    pending = s_voice.phase == PRODUCT_MEDIA_HANDOFF_BEGINNING;
    initialized = s_media_startup.is_ready;
    result = s_voice.begin_result;
    spin_unlock(&s_media_lock);
    if (!matches) return D_ERR_BUSY;
    if (!initialized || pending) return D_OK;
    if (result == D_OK) result = music_service_hold_poll(ready);
    spin_lock(&s_media_lock);
    if (!product_ui_media_voice_admitted_locked(generation)) {
        *ready = 0U;
        result = D_ERR_BUSY;
    }
    spin_unlock(&s_media_lock);
    return result;
}

/**
 * @brief 为匹配语音代次保留释放意图并尝试协调。
 * @param[in] generation 当前语音代次；过期或空闲时忽略。
 */
static void product_ui_media_voice_release(uint32_t generation)
{
    spin_lock(&s_media_lock);
    if (generation != s_voice.generation || s_voice.phase == PRODUCT_MEDIA_HANDOFF_IDLE) {
        spin_unlock(&s_media_lock);
        return;
    }
    s_voice.is_release_requested = 1U;
    spin_unlock(&s_media_lock);
    product_ui_media_reconcile_releases();
}
static const qwen_audio_peer_t s_voice_peer = {
    product_ui_media_voice_begin, product_ui_media_voice_poll, product_ui_media_voice_release
};
#endif

/**
 * @brief 在媒体锁内检查闹铃票据仍匹配且尚未请求释放。
 * @param[in] token 待检查闹铃占用票据。
 * @return 匹配活跃票据返回非零，否则为零；BEGINNING 可匹配但不能授予就绪。
 */
static uint8_t product_ui_media_alarm_matches_locked(uint32_t token)
{
    return token == s_alert.token && s_alert.phase != PRODUCT_MEDIA_HANDOFF_IDLE &&
        !s_alert.is_release_requested;
}

/**
 * @brief 预留新闹铃票据并锁外取得音乐及可用 Qwen 的闹铃占用。
 * @param[in] token 非零且递增的闹铃占用票据。
 * @return 旧票据返回 BUSY；同票据开始中返回 NOT_READY、已预留返回 D_OK；新事务受理后返回 D_OK。
 * @note 新事务部分开始失败也保留所有者并返回受理，真实开始错误由 poll 报告，使后续取消仍可按票据释放。
 */
static d_err_t product_ui_media_alarm_begin(uint32_t token)
{
    d_err_t result;
    uint8_t qwen_held = 0U;
    spin_lock(&s_media_lock);
    if (product_ui_media_alarm_matches_locked(token)) {
        result = s_alert.phase == PRODUCT_MEDIA_HANDOFF_BEGINNING ? D_ERR_NOT_READY : D_OK;
        spin_unlock(&s_media_lock);
        return result;
    }
    if (!product_ui_media_newer(token, s_alert.token)) {
        spin_unlock(&s_media_lock);
        return D_ERR_BUSY;
    }
    s_alert.token = token;
    s_alert.phase = PRODUCT_MEDIA_HANDOFF_BEGINNING;
    s_alert.is_release_requested = 0U;
    s_alert.is_qwen_held = 0U;
    s_alert.begin_result = D_OK;
    spin_unlock(&s_media_lock);
    result = music_service_hold_begin(MUSIC_HOLD_ALERT, token);
#if D_FEATURE_QWEN
    if (result == D_OK) {
        qwen_service_snapshot_t voice;
        if (qwen_service_get_snapshot(&voice) == D_OK) {
            result = qwen_service_alarm_begin(token);
            qwen_held = result == D_OK;
        }
    }
#endif
    spin_lock(&s_media_lock);
    if (s_alert.token == token) {
        s_alert.is_qwen_held = qwen_held;
        s_alert.begin_result = result;
        s_alert.phase = PRODUCT_MEDIA_HANDOFF_RESERVED;
    }
    spin_unlock(&s_media_lock);
    /* 所有者已受理事务；部分开始错误交给 poll 报告，
     * 让之后的提示音取消仍按票据执行释放。 */
    return D_OK;
}

/**
 * @brief 按闹铃票据轮询音乐与已取得的 Qwen 占用，并在锁外返回后复核票据。
 * @param[in] token 当前闹铃占用票据。
 * @param[out] ready 必填，默认清零；全部所需所有者完成时为非零。
 * @return 参数无效返回 INVALID_ARG，失配返回 BUSY，开始中返回 D_OK 且不就绪；否则报告开始或后端结果。
 */
static d_err_t product_ui_media_alarm_poll(uint32_t token, uint8_t *ready)
{
    d_err_t result;
    uint8_t matches, pending, qwen_held;
    if (ready == NULL) return D_ERR_INVALID_ARG;
    *ready = 0U;
    spin_lock(&s_media_lock);
    matches = product_ui_media_alarm_matches_locked(token);
    pending = s_alert.phase == PRODUCT_MEDIA_HANDOFF_BEGINNING;
    qwen_held = s_alert.is_qwen_held;
    result = s_alert.begin_result;
    spin_unlock(&s_media_lock);
    if (!matches) return D_ERR_BUSY;
    if (pending) return D_OK;
    if (result == D_OK) result = music_service_hold_poll(ready);
#if D_FEATURE_QWEN
    if (result == D_OK && *ready && qwen_held) result = qwen_service_alarm_poll(token, ready);
#else
    (void)qwen_held;
#endif
    spin_lock(&s_media_lock);
    if (!product_ui_media_alarm_matches_locked(token) || s_alert.phase == PRODUCT_MEDIA_HANDOFF_BEGINNING) {
        *ready = 0U;
        result = D_ERR_BUSY;
    }
    spin_unlock(&s_media_lock);
    return result;
}

/**
 * @brief 给当前闹铃票据保留释放意图，再尝试协调资源解除。
 * @param[in] token 待释放的闹铃占用票据；不匹配时忽略。
 */
static void product_ui_media_alarm_release(uint32_t token)
{
    spin_lock(&s_media_lock);
    if (token != s_alert.token || s_alert.phase == PRODUCT_MEDIA_HANDOFF_IDLE) {
        spin_unlock(&s_media_lock);
        return;
    }
    s_alert.is_release_requested = 1U;
    spin_unlock(&s_media_lock);
    product_ui_media_reconcile_releases();
}

/**
 * @brief 音乐真实收敛后，按票据推进启动、语音、闹铃及其他挂起释放。
 * @note 后端调用在锁外执行，更新本地状态前核对原票据；无法完成时保留意图供之后继续。
 */
static void product_ui_media_reconcile_releases(void)
{
    uint32_t voice, alert;
    uint8_t voice_pending, alert_pending, startup_pending, qwen_held, ready;
    d_err_t result;
    if (music_service_hold_poll(&ready) != D_OK || !ready) return;
    spin_lock(&s_media_lock);
    voice = s_voice.generation;
    alert = s_alert.token;
    voice_pending = s_voice.phase == PRODUCT_MEDIA_HANDOFF_RESERVED && s_voice.is_release_requested;
    alert_pending = s_alert.phase == PRODUCT_MEDIA_HANDOFF_RESERVED && s_alert.is_release_requested;
    qwen_held = s_alert.is_qwen_held;
    startup_pending = s_media_startup.is_release_pending;
    spin_unlock(&s_media_lock);
    if (startup_pending && music_service_hold_release(MUSIC_HOLD_STARTUP, 1U) == D_OK) {
        spin_lock(&s_media_lock);
        s_media_startup.is_release_pending = 0U;
        spin_unlock(&s_media_lock);
    }
    if (voice_pending) {
        result = music_service_hold_release(MUSIC_HOLD_VOICE, voice);
        if (result == D_OK || result == D_ERR_NOT_FOUND) {
            spin_lock(&s_media_lock);
            if (s_voice.generation == voice) {
                s_voice.phase = PRODUCT_MEDIA_HANDOFF_IDLE;
                s_voice.is_release_requested = 0U;
            }
            spin_unlock(&s_media_lock);
        }
    }
    if (alert_pending) {
        result = music_service_hold_release(MUSIC_HOLD_ALERT, alert);
        if (result == D_OK || result == D_ERR_NOT_FOUND) {
#if D_FEATURE_QWEN
            if (qwen_held) qwen_service_alarm_release(alert);
#else
            (void)qwen_held;
#endif
            spin_lock(&s_media_lock);
            if (s_alert.token == alert) {
                s_alert.phase = PRODUCT_MEDIA_HANDOFF_IDLE;
                s_alert.is_qwen_held = 0U;
                s_alert.is_release_requested = 0U;
            }
            spin_unlock(&s_media_lock);
        }
    }
    product_ui_media_reconcile_maintenance();
    product_ui_media_reconcile_volume();
}

static const d_local_prompt_handoff_t s_alarm_peer = {
    product_ui_media_alarm_begin, product_ui_media_alarm_poll, product_ui_media_alarm_release
};

/**
 * @brief 计算一个音量操作总预算的剩余毫秒数。
 * @param[in] started 本操作开始时的单调毫秒计数。
 * @return 到达 D_DEVICE_VOLUME_TIMEOUT_MS 返回零，否则返回剩余值。
 */
static uint32_t product_ui_media_volume_remaining(uint32_t started)
{
    uint32_t elapsed = (uint32_t)timer_get_ms() - started;
    return elapsed >= D_DEVICE_VOLUME_TIMEOUT_MS ? 0U : D_DEVICE_VOLUME_TIMEOUT_MS - elapsed;
}

/**
 * @brief 在统一预算内依次应用音乐、Qwen 及提示音音量，并记录可能受影响的来源。
 * @param[in] percent 由设备音量所有者校验的目标百分比。
 * @param[out] touched 非空来源位掩码；失败也保留已触及来源供回滚。
 * @return 全部已触及来源成功返回 D_OK；无后端为 NOT_READY，首个失败或预算耗尽返回对应错误。
 */
static d_err_t product_ui_media_apply_volume(uint8_t percent, uint8_t *touched)
{
    music_snapshot_t music;
    d_local_prompt_snapshot_t prompt;
    uint32_t started = (uint32_t)timer_get_ms(), remaining;
    d_err_t result = D_OK;
    *touched = 0U;
    music_service_get_snapshot(&music);
    if (music.ready) {
        *touched |= PRODUCT_MEDIA_VOLUME_MUSIC;
        result = music_service_apply_volume(percent, D_DEVICE_VOLUME_TIMEOUT_MS);
    }
    if (result != D_OK) return result;
#if D_FEATURE_QWEN
    if (!product_ui_media_volume_remaining(started)) return D_ERR_TIMEOUT;
    {
        uint8_t voice_busy;
        result = qwen_service_apply_device_volume(percent);
        voice_busy = product_ui_media_voice_busy();
        if (result != D_ERR_NOT_READY || voice_busy) *touched |= PRODUCT_MEDIA_VOLUME_QWEN;
        if (result != D_OK && (result != D_ERR_NOT_READY || voice_busy)) return result;
    }
#endif
    d_local_prompt_get_snapshot(&prompt);
    if (prompt.ready) {
        remaining = product_ui_media_volume_remaining(started);
        if (!remaining) return D_ERR_TIMEOUT;
        *touched |= PRODUCT_MEDIA_VOLUME_PROMPT;
        result = d_local_prompt_apply_volume(percent, remaining);
        if (result != D_OK) return result;
    }
    if (!product_ui_media_volume_remaining(started)) return D_ERR_TIMEOUT;
    return *touched ? D_OK : D_ERR_NOT_READY;
}

/**
 * @brief 在单独恢复预算内把触及的来源恢复到此前确认音量。
 * @param[in] confirmed 此前设备音量已确认的百分比。
 * @param[in] touched 需要恢复的来源位掩码。
 * @return 仍恢复失败的来源位掩码；零表示全部已触及来源恢复成功。
 */
static uint8_t product_ui_media_restore_volume(uint8_t confirmed, uint8_t touched)
{
    uint32_t started = (uint32_t)timer_get_ms(), remaining;
    uint8_t failed = 0U;
    if (touched & PRODUCT_MEDIA_VOLUME_MUSIC) {
        remaining = product_ui_media_volume_remaining(started);
        if (!remaining || music_service_apply_volume(confirmed, remaining) != D_OK)
            failed |= PRODUCT_MEDIA_VOLUME_MUSIC;
    }
#if D_FEATURE_QWEN
    if (touched & PRODUCT_MEDIA_VOLUME_QWEN) {
        if (!product_ui_media_volume_remaining(started) || qwen_service_apply_device_volume(confirmed) != D_OK)
            failed |= PRODUCT_MEDIA_VOLUME_QWEN;
    }
#endif
    if (touched & PRODUCT_MEDIA_VOLUME_PROMPT) {
        remaining = product_ui_media_volume_remaining(started);
        if (!remaining || d_local_prompt_apply_volume(confirmed, remaining) != D_OK)
            failed |= PRODUCT_MEDIA_VOLUME_PROMPT;
    }
    return failed;
}

/**
 * @brief 保留音量恢复失败故障及票据，关闭音乐／提示音准入并请求语音停止。
 * @param[in] transaction 故障所属的非零音量事务编号。
 * @param[in] failed 恢复失败的来源位掩码，与既有故障累积。
 */
static void product_ui_media_volume_failed(uint32_t transaction, uint8_t failed)
{
    spin_lock(&s_media_lock);
    s_media_volume.has_fault = 1U;
    s_media_volume.fault_mask |= failed;
    s_media_volume.hold_ticket = transaction;
    s_media_volume.is_recovery_pending = 0U;
    spin_unlock(&s_media_lock);
    (void)music_service_hold_begin(MUSIC_HOLD_VOLUME, transaction);
    d_local_prompt_foreground_set(PRODUCT_MEDIA_FOREGROUND_VOLUME_FAULT, 1U);
#if D_FEATURE_QWEN
    (void)qwen_service_session_stop();
#endif
}

/**
 * @brief 作为设备音量后端串行应用目标，失败恢复，成功后保留待确认的故障恢复意图。
 * @param[in] percent 目标设备音量百分比。
 * @param[in] context 后端上下文，当前未使用。
 * @return 准备／恢复竞争返回 BUSY；其余返回实际音量应用结果。
 * @note 设备音量所有者在回调返回后才提交确认；播放准入还须检查该所有者的在途状态，不能只检查本协调器。
 */
static d_err_t product_ui_media_volume(uint8_t percent, void *context)
{
    d_err_t result;
    uint32_t transaction, held_ticket = 0U;
    uint8_t touched, failed, confirmed = d_device_volume_get();
    (void)context;
    /* 防止形成音乐→提示音→音量→音乐的循环等待，
     * 也不能让新事务与恢复的最终释放竞争。 */
    spin_lock(&s_media_lock);
    if (s_music_prepare_in_progress || s_media_volume.is_recovering) {
        spin_unlock(&s_media_lock);
        return D_ERR_BUSY;
    }
    s_media_volume.is_applying = 1U;
    s_media_volume.is_recovery_pending = 0U;
    if (++s_media_volume.transaction == 0U) ++s_media_volume.transaction;
    transaction = s_media_volume.transaction;
    spin_unlock(&s_media_lock);
    result = product_ui_media_apply_volume(percent, &touched);
    if (result != D_OK) {
        failed = product_ui_media_restore_volume(confirmed, touched);
        if (failed) product_ui_media_volume_failed(transaction, failed);
    } else {
        spin_lock(&s_media_lock);
        if (s_media_volume.has_fault && (touched & s_media_volume.fault_mask) == s_media_volume.fault_mask) {
            s_media_volume.is_recovery_pending = 1U;
            s_media_volume.recovery_percent = percent;
            s_media_volume.recovery_transaction = transaction;
            held_ticket = s_media_volume.hold_ticket;
        }
        spin_unlock(&s_media_lock);
        if (held_ticket) (void)music_service_hold_begin(MUSIC_HOLD_VOLUME, held_ticket);
    }
    spin_lock(&s_media_lock);
    s_media_volume.is_applying = 0U;
    spin_unlock(&s_media_lock);
    /* 设备音量所有者在本回调返回后才提交确认；
     * 播放准入还检查它的在途状态，不能只看本协调器。 */
    return result;
}

/**
 * @brief 等待设备音量确认及音频空闲后按事务编号解除保留故障。
 * @note 故障恢复与执行状态独立；仅覆盖全部故障来源且对应最新确认事务时可恢复播放准入。
 */
static void product_ui_media_reconcile_volume(void)
{
    d_device_volume_snapshot_t volume;
    uint32_t transaction, held_ticket;
    d_err_t result;
    uint8_t recover;
    d_device_volume_get_snapshot(&volume);
    if (volume.is_in_progress || volume.is_pending || !volume.is_confirmed || product_ui_media_voice_busy()) return;
    spin_lock(&s_media_lock);
    recover = s_media_volume.has_fault && s_media_volume.is_recovery_pending && !s_media_volume.is_applying && !s_media_volume.is_recovering &&
        volume.volume_percent == s_media_volume.recovery_percent && s_media_volume.recovery_transaction == s_media_volume.transaction;
    transaction = s_media_volume.transaction;
    held_ticket = s_media_volume.hold_ticket;
    if (recover) s_media_volume.is_recovering = 1U;
    spin_unlock(&s_media_lock);
    if (!recover) return;
    result = d_local_prompt_wait_stopped(0U);
    if (result == D_OK) result = music_service_hold_release(MUSIC_HOLD_VOLUME, held_ticket);
    if (result == D_OK) d_local_prompt_foreground_set(PRODUCT_MEDIA_FOREGROUND_VOLUME_FAULT, 0U);
    spin_lock(&s_media_lock);
    if (result == D_OK && transaction == s_media_volume.transaction) {
        s_media_volume.has_fault = 0U;
        s_media_volume.fault_mask = 0U;
        s_media_volume.is_recovery_pending = 0U;
    }
    s_media_volume.is_recovering = 0U;
    spin_unlock(&s_media_lock);
}

/**
 * @brief 取得某维护客户端独立占用位，推进代次并锁外请求音乐收敛。
 * @param[in] owner OTA、电源或外部客户端的独立位。
 */
static void product_ui_media_maintenance_claim(uint8_t owner)
{
    uint32_t generation;
    spin_lock(&s_media_lock);
    s_media_maintenance.owners |= owner;
    s_media_maintenance.release_requested &= (uint8_t)~owner;
    if (++s_media_maintenance.generation == 0U) ++s_media_maintenance.generation;
    generation = s_media_maintenance.generation;
    spin_unlock(&s_media_lock);
    (void)music_service_hold_begin(MUSIC_HOLD_MAINTENANCE, generation);
}

/**
 * @brief 为 OTA 固定客户端取得维护占用，发起音乐收敛。
 */
void product_ui_media_maintenance_begin(void)
{
    product_ui_media_maintenance_claim(PRODUCT_MEDIA_MAINT_OTA);
}

/**
 * @brief 限时轮询音乐持有完成，等待时每次让出一个 OS tick。
 * @param[in] timeout_ms 等待上限毫秒，零只检查一次。
 * @return 已就绪返回 D_OK，后端失败原样返回，时间耗尽返回 TIMEOUT。
 */
static d_err_t product_ui_media_wait_music(uint32_t timeout_ms)
{
    uint32_t started = (uint32_t)timer_get_ms();
    d_err_t result;
    uint8_t ready;
    do {
        result = music_service_hold_poll(&ready);
        if (result != D_OK || ready) return result;
        if (!timeout_ms) break;
        os_time_dly(1);
    } while ((uint32_t)((uint32_t)timer_get_ms() - started) < timeout_ms);
    return D_ERR_TIMEOUT;
}

/**
 * @brief 只在 OTA 已持有维护占用时等待音乐收敛。
 */
d_err_t product_ui_media_maintenance_wait(uint32_t timeout_ms)
{
    uint8_t held;
    spin_lock(&s_media_lock);
    held = (s_media_maintenance.owners & PRODUCT_MEDIA_MAINT_OTA) != 0U;
    spin_unlock(&s_media_lock);
    /* OTA 可能在取得媒体占用前失败；部分开始的回退
     * 不能等待与其无关的正在播放所有者。 */
    return held ? product_ui_media_wait_music(timeout_ms) : D_OK;
}

/**
 * @brief 协调各维护客户端释放，仍有其他客户端时只解除已请求释放的位。
 * @note 最后一个客户端必须等音乐 hold 真实解除；锁外返回后仍核对代次。
 */
static void product_ui_media_reconcile_maintenance(void)
{
    uint32_t generation;
    uint8_t pending;
    d_err_t result;
    spin_lock(&s_media_lock);
    generation = s_media_maintenance.generation;
    pending = s_media_maintenance.release_requested & s_media_maintenance.owners;
    if (!pending || (s_media_maintenance.owners & (uint8_t)~pending)) {
        /* 解除一个客户端不能顺带释放其他客户端的音乐占用。 */
        s_media_maintenance.owners &= (uint8_t)~pending;
        s_media_maintenance.release_requested &= (uint8_t)~pending;
        spin_unlock(&s_media_lock);
        return;
    }
    spin_unlock(&s_media_lock);
    result = music_service_hold_release(MUSIC_HOLD_MAINTENANCE, generation);
    if (result != D_OK && result != D_ERR_NOT_FOUND) return;
    spin_lock(&s_media_lock);
    if (generation == s_media_maintenance.generation) {
        s_media_maintenance.owners &= (uint8_t)~pending;
        s_media_maintenance.release_requested &= (uint8_t)~pending;
    }
    spin_unlock(&s_media_lock);
}

/**
 * @brief 保存指定客户端释放意图，首次显式取消可重试一次音乐停止。
 * @param[in] owner 要解除的独立维护客户端位。
 * @return 该客户端已解除返回 D_OK，仍持有返回 BUSY。
 * @note 空闲回调只协调完成，不连续发出无限 STOP 重试。
 */
static d_err_t product_ui_media_maintenance_drop(uint8_t owner)
{
    uint32_t generation;
    uint8_t retry, active;
    spin_lock(&s_media_lock);
    if (!(s_media_maintenance.owners & owner)) {
        spin_unlock(&s_media_lock);
        return D_OK;
    }
    generation = s_media_maintenance.generation;
    retry = !(s_media_maintenance.release_requested & owner);
    s_media_maintenance.release_requested |= owner;
    spin_unlock(&s_media_lock);
    /* 一次显式取消允许重试失败停止；空闲回调只协调完成，
     * 不连续发出无界 STOP 重试。 */
    if (retry) (void)music_service_hold_begin(MUSIC_HOLD_MAINTENANCE, generation);
    product_ui_media_reconcile_releases();
    spin_lock(&s_media_lock);
    active = (s_media_maintenance.owners & owner) != 0U;
    spin_unlock(&s_media_lock);
    return active ? D_ERR_BUSY : D_OK;
}

/**
 * @brief 持久记录 OTA 释放意图并尝试完成占用解除。
 */
d_err_t product_ui_media_maintenance_release(void)
{
    return product_ui_media_maintenance_drop(PRODUCT_MEDIA_MAINT_OTA);
}


#if D_FEATURE_POWER_CONTROL
/**
 * @brief 为电源独立客户端取得维护占用。
 * @return 记录占用后返回 D_OK，停止完成另行等待。
 */
static d_err_t product_ui_media_power_begin(void)
{
    product_ui_media_maintenance_claim(PRODUCT_MEDIA_MAINT_POWER);
    return D_OK;
}

/**
 * @brief 先限时等待音乐收敛，再使用语音关机所有者的等待合同。
 * @param[in] timeout_ms 音乐等待上限；后续语音等待使用其自己的接口期限。
 * @return 首个等待失败原样返回；所有启用来源完成为 D_OK。
 */
static d_err_t product_ui_media_power_wait(uint32_t timeout_ms)
{
    d_err_t result = product_ui_media_wait_music(timeout_ms);
    if (result != D_OK) return result;
#if D_FEATURE_QWEN
    return qwen_service_shutdown_wait();
#else
    return D_OK;
#endif
}
/**
 * @brief 提交电源客户端释放意图，由既有音乐空闲路径继续收敛。
 * @return 释放意图已经保存后返回 D_OK，不代表占用立即解除。
 */
static d_err_t product_ui_media_power_cancel(void)
{
    (void)product_ui_media_maintenance_drop(PRODUCT_MEDIA_MAINT_POWER);
    /* 已受理意图继续受到门禁保护，由音乐 worker 的空闲回调完成，
     * 即使电源任务已经返回也不丢失。 */
    return D_OK;
}
static const power_ctrl_prepare_ops_t s_power_peer = {
    product_ui_media_power_begin, product_ui_media_power_wait, product_ui_media_power_cancel
};
#endif

/**
 * @brief 串行绑定音乐配置、提示音交接、语音及电源协作回调。
 */
d_err_t product_ui_media_runtime_bind(void)
{
    static const music_config_t config = {
        .maximum_tracks = 200U,
        .repeat_all = 1U,
        .formats = MUSIC_FORMAT_MP3 | MUSIC_FORMAT_WAV,
        .permit = product_ui_media_permit,
        .prepare = product_ui_media_prepare,
        .released = product_ui_media_released
    };
    d_err_t result = music_service_configure(&config);
    if (result != D_OK) return result;
    /* 可能较慢的媒体卡扫描之前必须先同步初始音量。 */
    (void)music_service_hold_begin(MUSIC_HOLD_STARTUP, 1U);
    result = d_local_prompt_register_foreground_probe(product_ui_media_foreground_busy);
    if (result != D_OK) return result;
    result = d_local_prompt_register_alarm_handoff(&s_alarm_peer);
    if (result != D_OK) return result;
#if D_FEATURE_QWEN
    result = qwen_service_bind_audio_peer(&s_voice_peer);
    if (result != D_OK) return result;
#endif
#if D_FEATURE_POWER_CONTROL
    result = power_ctrl_bind_prepare(&s_power_peer);
#endif
    return result;
}

/**
 * @brief 备用系统定时器只在有挂起意图时推进释放及恢复。
 * @param[in] context 定时器上下文，当前未使用。
 * @note 不等待、不调用 SDK 解码器；音乐 worker 失败时仍能在无页面状态继续协调。
 */
static void product_ui_media_reconcile_tick(void *context)
{
    uint8_t pending;
    (void)context;
    spin_lock(&s_media_lock);
    pending = s_media_startup.is_release_pending || s_voice.is_release_requested || s_alert.is_release_requested ||
        s_media_maintenance.release_requested || s_media_volume.is_recovery_pending;
    spin_unlock(&s_media_lock);
    if (pending) product_ui_media_reconcile_releases();
}

/**
 * @brief 串行注册设备音量后端，建立无需页面的释放与恢复推进路径。
 */
d_err_t product_ui_media_runtime_start(void)
{
    static const d_device_volume_backend_t backend = {product_ui_media_volume, NULL};
    music_snapshot_t music;
    uint8_t timer_created = 0U;
    d_err_t result;
    /* 启动串行。可选 worker 失败时，无页面也必须继续产品恢复；
     * 既有 sys_timer 回调只协调所有者完成，不等待也不使用 SDK 解码器。 */
    music_service_get_snapshot(&music);
    if (!music.ready && !s_media_startup.reconcile_timer) {
        s_media_startup.reconcile_timer = sys_timer_add_to_task("sys_timer", NULL,
            product_ui_media_reconcile_tick, PRODUCT_MEDIA_RECONCILE_MS);
        if (!s_media_startup.reconcile_timer) return D_ERR_NO_MEMORY;
        timer_created = 1U;
    }
    result = d_device_volume_register_backend(&backend);
    if (result != D_OK && timer_created) {
        sys_timer_del(s_media_startup.reconcile_timer);
        s_media_startup.reconcile_timer = 0U;
    }
    spin_lock(&s_media_lock);
    s_media_startup.is_ready = result == D_OK;
    s_media_startup.is_release_pending = result == D_OK;
    spin_unlock(&s_media_lock);
    product_ui_media_reconcile_releases();
    return result;
}

/**
 * @brief 查询产品媒体协调器已启动且没有保留的音量故障。
 */
uint8_t product_ui_media_runtime_ready(void)
{
    uint8_t ready;
    spin_lock(&s_media_lock);
    ready = s_media_startup.is_ready && !s_media_volume.has_fault;
    spin_unlock(&s_media_lock);
    return ready;
}
#endif
