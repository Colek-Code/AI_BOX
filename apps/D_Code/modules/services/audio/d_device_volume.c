/**
 * @file d_device_volume.c
 * @brief Device volume ownership and serialized, bounded backend requests.
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

/* _locked helpers touch metadata only; backend calls always run outside this lock. */
static void d_device_volume_deadline_locked(uint32_t now)
{
    uint8_t expired = s_volume.state.is_in_progress &&
        (uint32_t)(now - s_volume.started_ms) >= D_DEVICE_VOLUME_TIMEOUT_MS;
    if (expired != s_volume.state.is_timed_out) {
        s_volume.state.is_timed_out = expired;
        s_volume.state.revision++;
    }
}

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

static void d_device_volume_begin_locked(const d_volume_request_t *request)
{
    s_volume.active = *request;
    s_volume.state.is_in_progress = 1U;
    s_volume.state.is_timed_out = 0U;
    s_volume.started_ms = (uint32_t)timer_get_ms();
    s_volume.state.revision++;
}

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
    /* Polling covers a failed post; replacing the pending slot needs no new wake. */
    if (!replaced && os_sem_post(&s_volume.wake) != OS_NO_ERR) {
        LOGW(TAG, "wake failed; retained request=%u", (unsigned)request.id);
    }
#endif
    LOGI(TAG, "accepted id=%u target=%u replaced=%u", (unsigned)request.id,
         (unsigned)request.percent, (unsigned)replaced);
    return D_OK;
}

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

d_err_t d_device_volume_request_adjust(int8_t step, uint32_t *request_id)
{
    return d_device_volume_submit(D_VOLUME_ADJUST, step, request_id);
}

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
            /* Consume this job's wake even when a continuously busy worker never pends. */
            (void)os_sem_accept(&s_volume.wake);
            d_device_volume_complete(&request,
                backend.set_volume(request.percent, backend.context));
        } else {
            (void)os_sem_pend(&s_volume.wake, D_DEVICE_VOLUME_POLL_TICKS);
        }
    }
}
#endif

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
    /* The static wake object is retained; no timed-out caller can free it. */
#endif
    spin_lock(&s_volume_lock);
    s_volume.state.service_error = result;
    s_volume.state.is_service_ready = result == D_OK;
    s_volume.is_service_starting = 0U;
    s_volume.state.revision++;
    spin_unlock(&s_volume_lock);
    return result;
}

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

d_err_t d_device_volume_init(void)
{
    return d_device_volume_run_sync(D_VOLUME_ADJUST, 0);
}

uint8_t d_device_volume_get(void)
{
    d_device_volume_snapshot_t snapshot;
    d_device_volume_get_snapshot(&snapshot);
    return snapshot.volume_percent;
}

d_err_t d_device_volume_set(uint8_t percent)
{
    return d_device_volume_run_sync(D_VOLUME_SET, percent);
}

d_err_t d_device_volume_adjust(int8_t step)
{
    return d_device_volume_run_sync(D_VOLUME_ADJUST, step);
}

d_err_t d_device_volume_set_mute(uint8_t is_mute)
{
    return d_device_volume_run_sync(D_VOLUME_MUTE, is_mute != 0U);
}

static uint8_t d_device_volume_backend_matches(const d_device_volume_backend_t *backend)
{
    return s_volume.backend.set_volume == backend->set_volume &&
        s_volume.backend.context == backend->context;
}

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
    s_volume.state.is_backend_ready = result == D_OK && !s_volume.is_detaching;
    s_volume.state.is_confirmed = result == D_OK;
    s_volume.state.is_in_progress = 0U;
    s_volume.state.is_timed_out = 0U;
    s_volume.state.revision++;
    spin_unlock(&s_volume_lock);
    return result;
}

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

void d_device_volume_quiesce_begin(void)
{
    d_device_volume_ensure_init();
    spin_lock(&s_volume_lock);
    s_volume.state.is_quiescing = 1U;
    s_volume.state.revision++;
    spin_unlock(&s_volume_lock);
}

void d_device_volume_quiesce_cancel(void)
{
    d_device_volume_ensure_init();
    spin_lock(&s_volume_lock);
    s_volume.state.is_quiescing = 0U;
    s_volume.state.revision++;
    spin_unlock(&s_volume_lock);
}

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

d_err_t d_device_volume_alert_begin(uint32_t token, uint8_t percent)
{
    if (!token || percent < 1U || percent > 100U) return D_ERR_INVALID_ARG;
    d_device_volume_ensure_init();
    spin_lock(&s_volume_lock);
    if (!s_volume.state.is_service_ready || !s_volume.alert_backend.set_volume) {
        spin_unlock(&s_volume_lock); return D_ERR_NOT_READY;
    }
    /* Detach already owns the transition: its worker cannot process a new
     * alert lease, and the lease would in turn prevent detach from completing. */
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
