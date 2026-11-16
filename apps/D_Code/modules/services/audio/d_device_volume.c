/**
 * @file d_device_volume.c
 * @brief 在锁内维护音量元数据，在锁外串行调用音量后端。
 */
#include "config/app_config.h"
#include "d_device_volume.h"
#include "framework/d_log.h"
#include "os/os_api.h"
#include "system/os/os_cpu.h"
#include "system/timer.h"
#include "system/spinlock.h"
#include <string.h>

static DEFINE_SPINLOCK(s_volume_lock);

#ifndef D_DEVICE_VOLUME_DEFAULT
#ifdef D_QWEN_PLAY_VOLUME
#define D_DEVICE_VOLUME_DEFAULT D_QWEN_PLAY_VOLUME
#else
#define D_DEVICE_VOLUME_DEFAULT 80U
#endif
#endif
#define TAG "DEVICE_VOLUME"
#define D_DEVICE_VOLUME_TASK_PRIORITY 12
#define D_DEVICE_VOLUME_TASK_STACK_WORDS 1024
#define D_DEVICE_VOLUME_POLL_TICKS 10

typedef enum {
    D_VOLUME_SET,
    D_VOLUME_ADJUST,
    D_VOLUME_MUTE,
} d_volume_action_e;

typedef struct {
    uint32_t id;
    uint32_t generation;
    uint8_t percent;
    uint8_t restore;
} d_volume_request_t;

typedef struct {
    d_device_volume_snapshot_t state;
    d_device_volume_backend_t backend;
    d_device_volume_backend_t alert_backend;
    d_device_volume_alert_snapshot_t alert;
    uint32_t alert_started_ms;
    d_volume_request_t desired;
    d_volume_request_t pending;
    d_volume_request_t active;
    uint32_t started_ms;
    uint32_t next_id;
    uint8_t is_initialized;
    uint8_t is_detaching;
    uint8_t is_service_attempted;
    uint8_t is_service_starting;
#if D_FEATURE_QWEN || D_LOCAL_PROMPT_ENABLE
    OS_SEM wake;
    int task_pid;
#endif
} d_device_volume_context_t;

static d_device_volume_context_t s_volume;

/**
 * @brief 将有符号音量限制到设备百分比范围。
 * @param[in] percent 待限制音量。
 * @return 0～100 的音量。
 */
static uint8_t d_device_volume_clamp(int percent)
{
    if (percent < 0) {
        return D_DEVICE_VOLUME_MIN;
    }
    if (percent > (int)D_DEVICE_VOLUME_MAX) {
        return D_DEVICE_VOLUME_MAX;
    }
    return (uint8_t)percent;
}

/**
 * @brief 在自旋锁内一次性建立默认音量缓存。
 */
static void d_device_volume_ensure_init(void)
{
    spin_lock(&s_volume_lock);
    if (!s_volume.is_initialized) {
        s_volume.state.volume_percent = d_device_volume_clamp(D_DEVICE_VOLUME_DEFAULT);
        s_volume.state.restore_percent = s_volume.state.volume_percent;
        s_volume.state.target_percent = s_volume.state.volume_percent;
        s_volume.state.service_error = D_ERR_NOT_READY;
        s_volume.desired.percent = s_volume.state.volume_percent;
        s_volume.desired.restore = s_volume.state.restore_percent;
        s_volume.is_initialized = 1U;
        s_volume.state.revision = 1U;
    }
    spin_unlock(&s_volume_lock);
}

/**
 * @brief 更新在途媒体回调的超时标记。
 * @param[in] now 当前毫秒计时值。
 * @note 调用者必须已持有 s_volume_lock；仅更新元数据。
 */
static void d_device_volume_deadline_locked(uint32_t now)
{
    uint8_t expired = s_volume.state.is_in_progress &&
        (uint32_t)(now - s_volume.started_ms) >= D_DEVICE_VOLUME_TIMEOUT_MS;
    if (expired != s_volume.state.is_timed_out) {
        s_volume.state.is_timed_out = expired;
        s_volume.state.revision++;
    }
}

/**
 * @brief 从最新期望音量构造带请求编号及后端代次的新请求。
 * @param[in] action 绝对设置、相对调整或静音动作。
 * @param[in] value 动作对应的音量、步长或静音值。
 * @param[out] request 接收新请求。
 * @note 调用者必须持有 s_volume_lock。
 */
static void d_device_volume_desired_locked(d_volume_action_e action, int value,
                                           d_volume_request_t *request)
{
    *request = s_volume.desired;
    if (action == D_VOLUME_SET) {
        request->percent = d_device_volume_clamp(value);
    } else if (action == D_VOLUME_ADJUST) {
        request->percent = d_device_volume_clamp((int)request->percent + value);
    } else if (action == D_VOLUME_MUTE) {
        if (value) {
            request->percent = 0U;
        } else if (request->restore != 0U) {
            request->percent = request->restore;
        } else {
            request->percent = d_device_volume_clamp(D_DEVICE_VOLUME_DEFAULT);
        }
    }
    if (request->percent != 0U) {
        request->restore = request->percent;
    }
    if (++s_volume.next_id == 0U) {
        ++s_volume.next_id;
    }
    request->id = s_volume.next_id;
    request->generation = s_volume.state.backend_generation;
}

/**
 * @brief 记录当前执行请求与超时起点。
 * @param[in] request 即将执行的音量请求。
 * @note 调用者必须持有 s_volume_lock。
 */
static void d_device_volume_begin_locked(const d_volume_request_t *request)
{
    s_volume.active = *request;
    s_volume.state.is_in_progress = 1U;
    s_volume.state.is_timed_out = 0U;
    s_volume.started_ms = (uint32_t)timer_get_ms();
    s_volume.state.revision++;
}

/**
 * @brief 核对请求编号与代次后发布音量执行结果。
 * @param[in] request 已执行请求的副本。
 * @param[in] result 后端执行结果。
 * @note 函数自行加锁；旧请求不覆盖当前执行状态。
 */
static void d_device_volume_complete(const d_volume_request_t *request, d_err_t result)
{
    spin_lock(&s_volume_lock);
    if (!s_volume.state.is_in_progress || s_volume.active.id != request->id ||
        s_volume.active.generation != request->generation) {
        spin_unlock(&s_volume_lock);
        return;
    }
    if (request->generation != s_volume.state.backend_generation) {
        result = D_ERR_NOT_READY;
    }
    if (result == D_OK) {
        s_volume.state.volume_percent = request->percent;
        s_volume.state.restore_percent = request->restore;
    }
    s_volume.state.is_confirmed = result == D_OK;
    s_volume.state.completed_id = request->id;
    if (request->id == s_volume.state.request_id) {
        s_volume.state.result = result;
    }
    s_volume.state.is_in_progress = 0U;
    s_volume.state.is_timed_out = 0U;
    if (!s_volume.state.is_pending) {
        s_volume.desired.percent = s_volume.state.volume_percent;
        s_volume.desired.restore = s_volume.state.restore_percent;
        s_volume.state.target_percent = s_volume.desired.percent;
    }
    s_volume.state.revision++;
    spin_unlock(&s_volume_lock);
    LOGI(TAG, "completed id=%u volume=%u result=%d", (unsigned)request->id,
         (unsigned)request->percent, result);
}

/**
 * @brief 在单个待执行槽中受理音量请求并唤醒工作任务。
 * @param[in] action 音量动作。
 * @param[in] value 动作参数。
 * @param[out] request_id 可选输出，失败为 0，受理后为非零编号。
 * @return D_OK 表示已受理；未就绪或静默、超时等阻塞条件返回对应错误。
 * @note 唤醒失败时请求仍保留，工作任务轮询可继续消费。
 */
static d_err_t d_device_volume_submit(d_volume_action_e action, int value,
                                      uint32_t *request_id)
{
    d_volume_request_t request;
    uint32_t replaced;
    if (request_id != NULL) {
        *request_id = 0U;
    }
    d_device_volume_ensure_init();
    spin_lock(&s_volume_lock);
    d_device_volume_deadline_locked((uint32_t)timer_get_ms());
    if (!s_volume.state.is_service_ready || !s_volume.state.is_backend_ready ||
        s_volume.is_detaching) {
        spin_unlock(&s_volume_lock);
        return D_ERR_NOT_READY;
    }
    if (s_volume.state.is_quiescing || s_volume.state.is_timed_out) {
        spin_unlock(&s_volume_lock);
        return D_ERR_BUSY;
    }
    d_device_volume_desired_locked(action, value, &request);
    replaced = s_volume.state.is_pending ? s_volume.pending.id : 0U;
    if (replaced != 0U) {
        s_volume.state.superseded_id = replaced;
    }
    s_volume.pending = request;
    s_volume.desired = request;
    s_volume.state.is_pending = 1U;
    s_volume.state.request_id = request.id;
    s_volume.state.target_percent = request.percent;
    s_volume.state.result = D_OK;
    s_volume.state.revision++;
    spin_unlock(&s_volume_lock);
    if (request_id != NULL) {
        *request_id = request.id;
    }
#if D_FEATURE_QWEN || D_LOCAL_PROMPT_ENABLE
    /* 轮询兜底唤醒失败；替换待执行槽不需要再次唤醒。 */
    if (!replaced && os_sem_post(&s_volume.wake) != OS_NO_ERR) {
        LOGW(TAG, "wake failed; retained request=%u", (unsigned)request.id);
    }
#endif
    LOGI(TAG, "accepted id=%u target=%u replaced=%u", (unsigned)request.id,
         (unsigned)request.percent, (unsigned)replaced);
    return D_OK;
}

/**
 * @brief 校验范围并受理绝对音量；完整合同见 d_device_volume.h。
 */
d_err_t d_device_volume_request_set(uint8_t percent, uint32_t *request_id)
{
    if (request_id != NULL) {
        *request_id = 0U;
    }
    if (percent > D_DEVICE_VOLUME_MAX) {
        return D_ERR_INVALID_ARG;
    }
    return d_device_volume_submit(D_VOLUME_SET, percent, request_id);
}

/**
 * @brief 受理相对音量请求；完整合同见 d_device_volume.h。
 */
d_err_t d_device_volume_request_adjust(int8_t step, uint32_t *request_id)
{
    return d_device_volume_submit(D_VOLUME_ADJUST, step, request_id);
}

/**
 * @brief 校验布尔值并受理静音请求；完整合同见 d_device_volume.h。
 */
d_err_t d_device_volume_request_mute(uint8_t is_mute, uint32_t *request_id)
{
    if (request_id != NULL) {
        *request_id = 0U;
    }
    if (is_mute > 1U) {
        return D_ERR_INVALID_ARG;
    }
    return d_device_volume_submit(D_VOLUME_MUTE, is_mute, request_id);
}

#if D_FEATURE_QWEN || D_LOCAL_PROMPT_ENABLE
/**
 * @brief 优先执行一版闹铃音量操作或恢复媒体音量。
 * @return 执行过一项工作返回 1，无可执行工作返回 0。
 * @note 后端在锁外调用；版本或代次变化时不发布旧结果。释放失败仍保留占用。
 */
static uint8_t d_device_volume_process_alert(void)
{
    d_device_volume_alert_snapshot_t work;
    d_device_volume_backend_t backend;
    d_volume_request_t media;
    d_err_t result;
    spin_lock(&s_volume_lock);
    if (s_volume.alert.revision == s_volume.alert.completed_revision || s_volume.alert.busy ||
        s_volume.state.is_in_progress || s_volume.is_detaching) {
        spin_unlock(&s_volume_lock); return 0U;
    }
    work = s_volume.alert;
    backend = work.requested ? s_volume.alert_backend : s_volume.backend;
    media = s_volume.desired;
    s_volume.alert.busy = 1U;
    s_volume.alert_started_ms = (uint32_t)timer_get_ms();
    spin_unlock(&s_volume_lock);
    result = backend.set_volume ? backend.set_volume(work.requested ? work.percent : media.percent, backend.context) :
        work.requested ? D_ERR_NOT_READY : D_OK;
    spin_lock(&s_volume_lock);
    if ((uint32_t)((uint32_t)timer_get_ms() - s_volume.alert_started_ms) >= D_DEVICE_VOLUME_TIMEOUT_MS) result = D_ERR_TIMEOUT;
    s_volume.alert.busy = 0U;
    if (work.revision == s_volume.alert.revision && work.token == s_volume.alert.token &&
        (work.requested || media.id == s_volume.desired.id)) {
        s_volume.alert.error = result;
        s_volume.alert.completed_revision = work.revision;
        s_volume.alert.held = work.requested || result != D_OK;
        if (!work.requested && result == D_OK) {
            s_volume.state.volume_percent = media.percent;
            s_volume.state.restore_percent = media.restore;
            s_volume.state.revision++;
        }
    }
    spin_unlock(&s_volume_lock);
    return 1U;
}

/**
 * @brief 持续优先处理闹铃占用，再消费媒体音量请求。
 * @param[in] context 任务兼容参数，本实现不使用。
 * @note 后端回调在锁外执行；无工作时限时等待信号量。
 */
static void d_device_volume_worker(void *context)
{
    d_volume_request_t request;
    d_device_volume_backend_t backend;
    uint8_t has_work;
    (void)context;
    for (;;) {
        if (d_device_volume_process_alert()) continue;
        spin_lock(&s_volume_lock);
        has_work = s_volume.state.is_pending && !s_volume.state.is_in_progress &&
            s_volume.state.is_backend_ready && !s_volume.is_detaching && !s_volume.alert.held;
        if (has_work) {
            request = s_volume.pending;
            backend = s_volume.backend;
            s_volume.state.is_pending = 0U;
            d_device_volume_begin_locked(&request);
        }
        spin_unlock(&s_volume_lock);
        if (has_work) {
            /* 连续繁忙时也消费本项唤醒计数，避免积累无效通知。 */
            (void)os_sem_accept(&s_volume.wake);
            d_device_volume_complete(&request,
                backend.set_volume(request.percent, backend.context));
        } else {
            (void)os_sem_pend(&s_volume.wake, D_DEVICE_VOLUME_POLL_TICKS);
        }
    }
}
#endif

/**
 * @brief 建立一次性持久音量工作任务；完整合同见 d_device_volume.h。
 */
d_err_t d_device_volume_service_init(void)
{
    d_err_t result = D_ERR_NOT_READY;
    d_device_volume_ensure_init();
    spin_lock(&s_volume_lock);
    if (s_volume.is_service_attempted) {
        result = s_volume.is_service_starting ? D_ERR_BUSY : s_volume.state.service_error;
        spin_unlock(&s_volume_lock);
        return result;
    }
    s_volume.is_service_attempted = 1U;
    s_volume.is_service_starting = 1U;
    spin_unlock(&s_volume_lock);
#if D_FEATURE_QWEN || D_LOCAL_PROMPT_ENABLE
    result = D_ERR_IO;
    if (os_sem_create(&s_volume.wake, 0) == OS_NO_ERR &&
        thread_fork("device_volume", D_DEVICE_VOLUME_TASK_PRIORITY,
                    D_DEVICE_VOLUME_TASK_STACK_WORDS, 0, &s_volume.task_pid,
                    d_device_volume_worker, NULL) == OS_NO_ERR) {
        result = D_OK;
    }
    /* 静态唤醒对象持续保留，超时调用者不能释放它。 */
#endif
    spin_lock(&s_volume_lock);
    s_volume.state.service_error = result;
    s_volume.state.is_service_ready = result == D_OK;
    s_volume.is_service_starting = 0U;
    s_volume.state.revision++;
    spin_unlock(&s_volume_lock);
    return result;
}

/**
 * @brief 复制当前状态及准入视图；完整合同见 d_device_volume.h。
 */
void d_device_volume_get_snapshot(d_device_volume_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }
    d_device_volume_ensure_init();
    spin_lock(&s_volume_lock);
    d_device_volume_deadline_locked((uint32_t)timer_get_ms());
    *snapshot = s_volume.state;
    snapshot->can_request = snapshot->is_service_ready && snapshot->is_backend_ready &&
        !snapshot->is_quiescing && !snapshot->is_timed_out && !s_volume.is_detaching;
    spin_unlock(&s_volume_lock);
}

/**
 * @brief 在没有异步或闹铃占用时同步执行一次音量变更。
 * @param[in] action 音量动作。
 * @param[in] value 动作参数。
 * @return 占用返回 D_ERR_BUSY，否则返回后端结果；无后端时按成功更新缓存。
 */
static d_err_t d_device_volume_run_sync(d_volume_action_e action, int value)
{
    d_volume_request_t request;
    d_device_volume_backend_t backend;
    d_err_t result;
    d_device_volume_ensure_init();
    spin_lock(&s_volume_lock);
    if (s_volume.state.is_in_progress || s_volume.state.is_pending ||
        s_volume.state.is_quiescing || s_volume.is_detaching || s_volume.alert.held || s_volume.alert.busy) {
        spin_unlock(&s_volume_lock);
        return D_ERR_BUSY;
    }
    d_device_volume_desired_locked(action, value, &request);
    s_volume.desired = request;
    s_volume.state.target_percent = request.percent;
    s_volume.state.request_id = request.id;
    backend = s_volume.backend;
    d_device_volume_begin_locked(&request);
    spin_unlock(&s_volume_lock);
    result = backend.set_volume ? backend.set_volume(request.percent, backend.context) : D_OK;
    d_device_volume_complete(&request, result);
    return result;
}

/**
 * @brief 同步默认音量缓存；完整合同见 d_device_volume.h。
 */
d_err_t d_device_volume_init(void)
{
    return d_device_volume_run_sync(D_VOLUME_ADJUST, 0);
}

/**
 * @brief 读取已确认音量；完整合同见 d_device_volume.h。
 */
uint8_t d_device_volume_get(void)
{
    d_device_volume_snapshot_t snapshot;
    d_device_volume_get_snapshot(&snapshot);
    return snapshot.volume_percent;
}

/**
 * @brief 同步设置音量；完整合同见 d_device_volume.h。
 */
d_err_t d_device_volume_set(uint8_t percent)
{
    return d_device_volume_run_sync(D_VOLUME_SET, percent);
}

/**
 * @brief 同步调整音量；完整合同见 d_device_volume.h。
 */
d_err_t d_device_volume_adjust(int8_t step)
{
    return d_device_volume_run_sync(D_VOLUME_ADJUST, step);
}

/**
 * @brief 同步更新静音状态；完整合同见 d_device_volume.h。
 */
d_err_t d_device_volume_set_mute(uint8_t is_mute)
{
    return d_device_volume_run_sync(D_VOLUME_MUTE, is_mute != 0U);
}

/**
 * @brief 比较当前媒体后端的回调与上下文身份。
 * @param[in] backend 已验证非空的待比较后端。
 * @return 两项均相同时返回非零，否则返回 0。
 * @note 调用者在持有 s_volume_lock 时使用。
 */
static uint8_t d_device_volume_backend_matches(const d_device_volume_backend_t *backend)
{
    return s_volume.backend.set_volume == backend->set_volume &&
        s_volume.backend.context == backend->context;
}

/**
 * @brief 注册媒体后端并同步缓存；完整合同见 d_device_volume.h。
 */
d_err_t d_device_volume_register_backend(const d_device_volume_backend_t *backend)
{
    d_err_t result;
    uint8_t is_new;
    uint8_t percent;
    if (backend == NULL || backend->set_volume == NULL) {
        return D_ERR_INVALID_ARG;
    }
    d_device_volume_ensure_init();
    spin_lock(&s_volume_lock);
    if (s_volume.state.is_in_progress || s_volume.state.is_pending ||
        s_volume.state.is_quiescing || s_volume.is_detaching || s_volume.alert.held || s_volume.alert.busy ||
        (s_volume.backend.set_volume && !d_device_volume_backend_matches(backend))) {
        spin_unlock(&s_volume_lock);
        return D_ERR_BUSY;
    }
    is_new = s_volume.backend.set_volume == NULL;
    if (is_new) {
        s_volume.state.backend_generation++;
    }
    s_volume.backend = *backend;
    s_volume.state.is_backend_ready = 0U;
    percent = s_volume.state.volume_percent;
    d_device_volume_begin_locked(&s_volume.desired);
    spin_unlock(&s_volume_lock);
    result = backend->set_volume(percent, backend->context);
    spin_lock(&s_volume_lock);
    if (result != D_OK && is_new) {
        memset(&s_volume.backend, 0, sizeof(s_volume.backend));
        s_volume.is_detaching = 0U;
    }
    /* 锁外同步期间可能先进入注销流程，发布就绪前需再次核对。 */
    if (result == D_OK && s_volume.is_detaching) result = D_ERR_BUSY;
    s_volume.state.is_backend_ready = result == D_OK && !s_volume.is_detaching;
    s_volume.state.is_confirmed = result == D_OK;
    s_volume.state.is_in_progress = 0U;
    s_volume.state.is_timed_out = 0U;
    s_volume.state.revision++;
    spin_unlock(&s_volume_lock);
    return result;
}

/**
 * @brief 解除匹配后端并拒绝新工作；完整合同见 d_device_volume.h。
 */
d_err_t d_device_volume_unregister_backend(const d_device_volume_backend_t *backend)
{
    if (backend == NULL || backend->set_volume == NULL) {
        return D_ERR_INVALID_ARG;
    }
    d_device_volume_ensure_init();
    spin_lock(&s_volume_lock);
    if (s_volume.backend.set_volume && !d_device_volume_backend_matches(backend)) {
        spin_unlock(&s_volume_lock);
        return D_ERR_NOT_FOUND;
    }
    if (s_volume.alert.held || s_volume.alert.busy) { spin_unlock(&s_volume_lock); return D_ERR_BUSY; }
    s_volume.is_detaching = 1U;
    s_volume.state.is_backend_ready = 0U;
    if (s_volume.state.is_pending) {
        s_volume.state.is_pending = 0U;
        s_volume.state.result = D_ERR_NOT_READY;
        s_volume.state.superseded_id = s_volume.pending.id;
    }
    s_volume.state.revision++;
    if (s_volume.state.is_in_progress) {
        spin_unlock(&s_volume_lock);
        return D_ERR_BUSY;
    }
    if (s_volume.backend.set_volume != NULL) {
        s_volume.state.backend_generation++;
    }
    memset(&s_volume.backend, 0, sizeof(s_volume.backend));
    s_volume.desired.percent = s_volume.state.volume_percent;
    s_volume.desired.restore = s_volume.state.restore_percent;
    s_volume.state.target_percent = s_volume.state.volume_percent;
    s_volume.is_detaching = 0U;
    spin_unlock(&s_volume_lock);
    return D_OK;
}

/**
 * @brief 进入静默准入状态；完整合同见 d_device_volume.h。
 */
void d_device_volume_quiesce_begin(void)
{
    d_device_volume_ensure_init();
    spin_lock(&s_volume_lock);
    s_volume.state.is_quiescing = 1U;
    s_volume.state.revision++;
    spin_unlock(&s_volume_lock);
}

/**
 * @brief 解除静默准入状态；完整合同见 d_device_volume.h。
 */
void d_device_volume_quiesce_cancel(void)
{
    d_device_volume_ensure_init();
    spin_lock(&s_volume_lock);
    s_volume.state.is_quiescing = 0U;
    s_volume.state.revision++;
    spin_unlock(&s_volume_lock);
}

/**
 * @brief 限时等待所有音量占用空闲；完整合同见 d_device_volume.h。
 */
d_err_t d_device_volume_wait_idle(uint32_t timeout_ms)
{
    d_device_volume_snapshot_t snapshot;
    uint32_t started = (uint32_t)timer_get_ms();
    for (;;) {
        d_device_volume_alert_snapshot_t alert;
        d_device_volume_get_snapshot(&snapshot);
        d_device_volume_get_alert_snapshot(&alert);
        if (!snapshot.is_in_progress && !snapshot.is_pending && !alert.held && !alert.busy) {
            return D_OK;
        }
        if ((uint32_t)((uint32_t)timer_get_ms() - started) >= timeout_ms) {
            return D_ERR_TIMEOUT;
        }
        os_time_dly(1);
    }
}

/**
 * @brief 注册静态闹铃后端；完整合同见 d_device_volume.h。
 */
d_err_t d_device_volume_register_alert_backend(const d_device_volume_backend_t *backend)
{
    if (backend == NULL || backend->set_volume == NULL) return D_ERR_INVALID_ARG;
    d_device_volume_ensure_init();
    spin_lock(&s_volume_lock);
    if (s_volume.alert.busy || (s_volume.alert_backend.set_volume &&
        (s_volume.alert_backend.set_volume != backend->set_volume || s_volume.alert_backend.context != backend->context))) {
        spin_unlock(&s_volume_lock); return D_ERR_BUSY;
    }
    s_volume.alert_backend = *backend;
    spin_unlock(&s_volume_lock);
    return D_OK;
}

/**
 * @brief 受理闹铃音量占用；完整合同见 d_device_volume.h。
 */
d_err_t d_device_volume_alert_begin(uint32_t token, uint8_t percent)
{
    if (!token || percent < 1U || percent > 100U) return D_ERR_INVALID_ARG;
    d_device_volume_ensure_init();
    spin_lock(&s_volume_lock);
    if (!s_volume.state.is_service_ready || !s_volume.alert_backend.set_volume) {
        spin_unlock(&s_volume_lock); return D_ERR_NOT_READY;
    }
    /* 注销已占有转换流程，工作任务不能处理新闹铃占用；
     * 此时再接受占用会反过来阻塞注销完成。 */
    if (s_volume.state.is_quiescing || s_volume.is_detaching || (s_volume.alert.held && token != s_volume.alert.token &&
        (uint32_t)(token - s_volume.alert.token) >= 0x80000000U)) {
        spin_unlock(&s_volume_lock); return D_ERR_BUSY;
    }
    if (s_volume.alert.requested && s_volume.alert.token == token && s_volume.alert.percent == percent &&
        s_volume.alert.error == D_OK) { spin_unlock(&s_volume_lock); return D_OK; }
    s_volume.alert.token = token;
    s_volume.alert.percent = percent;
    s_volume.alert.requested = s_volume.alert.held = 1U;
    s_volume.alert.error = D_OK;
    if (++s_volume.alert.revision == 0U) ++s_volume.alert.revision;
    spin_unlock(&s_volume_lock);
#if D_FEATURE_QWEN || D_LOCAL_PROMPT_ENABLE
    (void)os_sem_post(&s_volume.wake);
#endif
    return D_OK;
}

/**
 * @brief 受理匹配代次的占用释放；完整合同见 d_device_volume.h。
 */
d_err_t d_device_volume_alert_end(uint32_t token)
{
    d_device_volume_ensure_init();
    spin_lock(&s_volume_lock);
    if (token != s_volume.alert.token) { spin_unlock(&s_volume_lock); return D_ERR_BUSY; }
    if (!s_volume.alert.requested && (!s_volume.alert.held || s_volume.alert.error == D_OK)) {
        spin_unlock(&s_volume_lock); return D_OK;
    }
    s_volume.alert.requested = 0U;
    s_volume.alert.error = D_OK;
    if (++s_volume.alert.revision == 0U) ++s_volume.alert.revision;
    spin_unlock(&s_volume_lock);
#if D_FEATURE_QWEN || D_LOCAL_PROMPT_ENABLE
    (void)os_sem_post(&s_volume.wake);
#endif
    return D_OK;
}

/**
 * @brief 复制闹铃音量状态及超时视图；完整合同见 d_device_volume.h。
 */
void d_device_volume_get_alert_snapshot(d_device_volume_alert_snapshot_t *snapshot)
{
    if (snapshot == NULL) return;
    d_device_volume_ensure_init();
    spin_lock(&s_volume_lock);
    *snapshot = s_volume.alert;
    snapshot->ready = s_volume.state.is_service_ready && s_volume.alert_backend.set_volume != NULL;
    if (snapshot->busy && (uint32_t)((uint32_t)timer_get_ms() - s_volume.alert_started_ms) >= D_DEVICE_VOLUME_TIMEOUT_MS)
        snapshot->error = D_ERR_TIMEOUT;
    spin_unlock(&s_volume_lock);
}
