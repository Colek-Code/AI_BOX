/** @file d_local_prompt.c @brief Prioritized audio owner with confirmed stop and volume handoff. */
#include "config/app_config.h"
#if D_LOCAL_PROMPT_ENABLE
#include "d_local_prompt.h"
#include "d_local_prompt_decoder.h"
#include "d_device_volume.h"
#include "system/includes.h"
#include "os/os_api.h"
#include "system/spinlock.h"
#include <string.h>

/* Public requests may arrive from another core; callbacks/IO never run under this lock. */
static DEFINE_SPINLOCK(s_prompt_lock);

#define D_LOCAL_PROMPT_WAIT_MS 30000U
#define D_LOCAL_PROMPT_STOP_MS 1500U
#define D_LOCAL_PROMPT_ALARM_MS 300000U
#define D_LOCAL_PROMPT_HANDOFF_MS 10000U
typedef struct {
    d_local_prompt_snapshot_t state;
    uint32_t created_ms;
    uint32_t started_ms;
    uint32_t canceled_event;
    uint32_t playing_event;
    uint32_t volume_request;
    uint32_t volume_completed;
    d_err_t volume_error;
    d_err_t media_volume_error;
    int task_pid;
    uint8_t volume;
    uint8_t target_volume;
    uint8_t attempted;
    uint8_t stop_requested;
    uint8_t stop_failed;
    uint8_t volume_expired;
    uint8_t volume_is_alert;
    uint8_t alert_volume;
    uint8_t alert_backend_ready;
    uint8_t active_source;
    uint8_t decoder_owned;
    uint8_t decoder_started;
} d_local_prompt_context_t;
static d_local_prompt_context_t s_prompt;
static uint8_t (*s_foreground_probe)(void);
typedef struct {
    d_local_prompt_request_t request;
    uint32_t canceled_generation;
    d_err_t error;
    uint8_t wanted;
    uint8_t pending;
} d_local_prompt_slot_t;
static d_local_prompt_slot_t s_sources[D_LOCAL_PROMPT_SOURCE_COUNT];
static const d_local_prompt_handoff_t *s_alarm_handoff;
static struct {
    uint32_t token;
    uint32_t next_token;
    d_local_prompt_request_t request;
    d_err_t error;
    uint8_t handoff_sent;
    uint8_t volume_sent;
    uint8_t release_sent;
    uint8_t start_issued;
    uint8_t releasing;
} s_alert_owner;
static d_err_t d_local_prompt_apply_volume_kind(uint8_t percent, uint32_t timeout_ms, uint8_t alert);
static void d_local_prompt_process_alert(void);

static uint8_t d_local_prompt_after(uint32_t left, uint32_t right)
{
    return left != right && (uint32_t)(left - right) < 0x80000000U;
}

static void d_local_prompt_wake(void)
{
    /* A full queue does not lose intent: the 20 ms taskq timeout reconciles it. */
    if (s_prompt.state.ready) (void)os_taskq_post("local_prompt", 1, 1);
}

static uint8_t d_local_prompt_foreground_busy(void)
{
    uint8_t (*probe)(void);
    spin_lock(&s_prompt_lock);
    probe = s_foreground_probe;
    spin_unlock(&s_prompt_lock);
    return probe != NULL ? probe() : 0U;
}

d_err_t d_local_prompt_register_foreground_probe(uint8_t (*probe)(void))
{
    if (probe == NULL) return D_ERR_INVALID_ARG;
    spin_lock(&s_prompt_lock);
    if (s_foreground_probe != NULL && s_foreground_probe != probe) { spin_unlock(&s_prompt_lock); return D_ERR_BUSY; }
    s_foreground_probe = probe;
    spin_unlock(&s_prompt_lock);
    return D_OK;
}

static d_err_t d_local_prompt_start_permitted(uint32_t generation)
{
    uint32_t now = (uint32_t)timer_get_ms();
    d_err_t result = D_OK;
    spin_lock(&s_prompt_lock);
    if (generation != s_prompt.state.generation || !s_prompt.decoder_owned ||
        s_prompt.stop_requested || s_prompt.state.quiescing) result = D_ERR_BUSY;
    else if (s_prompt.active_source == D_LOCAL_PROMPT_POMODORO) {
        if (s_prompt.state.foreground || s_prompt.playing_event != s_prompt.state.event_id) result = D_ERR_BUSY;
        else if ((uint32_t)(now - s_prompt.created_ms) >= D_LOCAL_PROMPT_WAIT_MS) result = D_ERR_TIMEOUT;
    } else {
        d_local_prompt_request_t *request = &s_alert_owner.request;
        d_local_prompt_slot_t *slot = &s_sources[s_prompt.active_source];
        if (!s_alert_owner.token || !slot->wanted || slot->request.generation != request->generation ||
            (s_prompt.active_source == D_LOCAL_PROMPT_PREVIEW && s_prompt.state.foreground)) result = D_ERR_BUSY;
        else if (s_alert_owner.error != D_OK) result = s_alert_owner.error;
        else if ((uint32_t)(now - request->created_ms) >=
            (request->source == D_LOCAL_PROMPT_ALARM ? D_LOCAL_PROMPT_HANDOFF_MS : 2000U)) result = D_ERR_TIMEOUT;
    }
    spin_unlock(&s_prompt_lock);
    return result;
}

static void d_local_prompt_start_completed(uint32_t generation, d_err_t result)
{
    uint8_t started = result == D_OK;
    d_err_t permitted = d_local_prompt_start_permitted(generation);
    uint32_t now = (uint32_t)timer_get_ms();
    uint8_t canceled;
    if (result == D_OK) result = permitted;
    canceled = result == D_ERR_BUSY && permitted == D_ERR_BUSY;
    spin_lock(&s_prompt_lock);
    s_prompt.decoder_started = started;
    if (started) s_prompt.started_ms = now;
    if (result != D_OK) s_prompt.stop_requested = 1U;
    if (s_prompt.active_source == D_LOCAL_PROMPT_POMODORO) s_prompt.state.error = canceled ? D_OK : result;
    else if (result != D_OK && !canceled) {
        s_alert_owner.error = result;
        s_sources[s_prompt.active_source].error = result;
        s_sources[s_prompt.active_source].pending = 0U;
    }
    spin_unlock(&s_prompt_lock);
}

static void d_local_prompt_process_stop(void)
{
    uint8_t stop;
    d_err_t result;
    spin_lock(&s_prompt_lock);
    stop = s_prompt.decoder_owned && !s_prompt.stop_failed &&
        (s_prompt.stop_requested || (s_prompt.state.foreground && s_prompt.active_source != D_LOCAL_PROMPT_ALARM) ||
         s_prompt.state.quiescing || (uint32_t)((uint32_t)timer_get_ms() - s_prompt.started_ms) >=
         (s_prompt.active_source == D_LOCAL_PROMPT_ALARM ? D_LOCAL_PROMPT_ALARM_MS : D_LOCAL_PROMPT_STOP_MS));
    spin_unlock(&s_prompt_lock);
    if (!stop && !d_local_prompt_decoder_finished()) return;
    spin_lock(&s_prompt_lock);
    stop = s_prompt.decoder_owned && !s_prompt.stop_failed;
    spin_unlock(&s_prompt_lock);
    if (!stop) return;
    result = d_local_prompt_decoder_stop();
    spin_lock(&s_prompt_lock);
    /* Successful cleanup proves silence, not successful playback. */
    if (s_prompt.active_source == D_LOCAL_PROMPT_POMODORO) {
        if (result != D_OK) s_prompt.state.error = result;
        else if (d_local_prompt_decoder_result() != D_OK) s_prompt.state.error = d_local_prompt_decoder_result();
    } else if (result != D_OK || d_local_prompt_decoder_result() != D_OK) {
        s_alert_owner.error = result != D_OK ? result : d_local_prompt_decoder_result();
        s_sources[s_prompt.active_source].error = s_alert_owner.error;
    }
    s_prompt.stop_failed = result != D_OK;
    if (result == D_OK) {
        s_prompt.decoder_owned = 0U;
        s_prompt.decoder_started = 0U;
        s_prompt.stop_requested = 0U;
        s_prompt.playing_event = 0U;
        if (s_prompt.active_source == D_LOCAL_PROMPT_PREVIEW &&
            s_sources[D_LOCAL_PROMPT_PREVIEW].request.generation == s_alert_owner.request.generation)
            s_sources[D_LOCAL_PROMPT_PREVIEW].wanted = 0U;
    }
    spin_unlock(&s_prompt_lock);
}

static void d_local_prompt_process_volume(void)
{
    uint32_t request;
    uint8_t volume;
    uint8_t alert;
    uint8_t apply_decoder;
    d_err_t result;
    spin_lock(&s_prompt_lock);
    request = s_prompt.volume_request;
    volume = s_prompt.target_volume;
    alert = s_prompt.volume_is_alert;
    apply_decoder = s_prompt.decoder_owned &&
        (alert ? s_prompt.active_source != D_LOCAL_PROMPT_POMODORO : s_prompt.active_source == D_LOCAL_PROMPT_POMODORO);
    if (request == s_prompt.volume_completed) { spin_unlock(&s_prompt_lock); return; }
    spin_unlock(&s_prompt_lock);
    result = apply_decoder ? d_local_prompt_decoder_volume(volume) : D_OK;
    spin_lock(&s_prompt_lock);
    if (s_prompt.volume_expired) result = D_ERR_TIMEOUT;
    if (result == D_OK) {
        if (alert) s_prompt.alert_volume = volume;
        else s_prompt.volume = volume;
    } else {
        if (apply_decoder) s_prompt.stop_requested = 1U;
        if (alert) s_alert_owner.error = result;
        else s_prompt.state.error = result;
    }
    s_prompt.volume_error = result;
    if (!alert) s_prompt.media_volume_error = result;
    s_prompt.volume_completed = request;
    spin_unlock(&s_prompt_lock);
}

static void d_local_prompt_process_start(void)
{
    uint32_t generation;
    uint8_t volume;
    uint8_t busy = d_local_prompt_foreground_busy();
    d_err_t result;
    spin_lock(&s_prompt_lock);
    if (s_prompt.state.pending &&
        (uint32_t)((uint32_t)timer_get_ms() - s_prompt.created_ms) >= D_LOCAL_PROMPT_WAIT_MS)
        s_prompt.state.pending = 0U;
    if (!s_prompt.state.pending || s_prompt.decoder_owned || s_prompt.state.foreground ||
        s_prompt.state.quiescing || busy || s_prompt.media_volume_error != D_OK ||
        s_prompt.volume_request != s_prompt.volume_completed || s_alert_owner.token ||
        s_sources[D_LOCAL_PROMPT_ALARM].wanted || s_sources[D_LOCAL_PROMPT_PREVIEW].wanted) {
        spin_unlock(&s_prompt_lock); return;
    }
    volume = s_prompt.volume;
    s_prompt.state.pending = 0U;
    if (volume == 0U) { spin_unlock(&s_prompt_lock); return; }
    s_prompt.state.generation++;
    if (s_prompt.state.generation == 0U) s_prompt.state.generation = 1U;
    generation = s_prompt.state.generation;
    s_prompt.decoder_owned = 1U; /* OPEN/START in flight is also an owned resource. */
    s_prompt.decoder_started = 0U;
    s_prompt.playing_event = s_prompt.state.event_id;
    s_prompt.active_source = D_LOCAL_PROMPT_POMODORO;
    s_prompt.stop_requested = 0U;
    s_prompt.stop_failed = 0U;
    spin_unlock(&s_prompt_lock);
    result = d_local_prompt_decoder_start_checked(generation, volume, 0U, d_local_prompt_start_permitted);
    d_local_prompt_start_completed(generation, result);
    d_local_prompt_process_stop();
}

static void d_local_prompt_alert_error(uint32_t token, d_err_t result)
{
    spin_lock(&s_prompt_lock);
    if (s_alert_owner.token == token) {
        s_alert_owner.error = result;
        s_sources[s_alert_owner.request.source].error = result;
        s_sources[s_alert_owner.request.source].pending = 0U;
    }
    spin_unlock(&s_prompt_lock);
}

static void d_local_prompt_alert_release(void)
{
    d_device_volume_alert_snapshot_t volume;
    const d_local_prompt_handoff_t *handoff;
    uint32_t token, next_token;
    uint8_t volume_sent, release_sent, handoff_sent;
    d_err_t result;
    spin_lock(&s_prompt_lock);
    token = s_alert_owner.token;
    if (!token || s_prompt.decoder_owned) { spin_unlock(&s_prompt_lock); return; }
    volume_sent = s_alert_owner.volume_sent;
    release_sent = s_alert_owner.release_sent;
    handoff_sent = s_alert_owner.handoff_sent;
    handoff = s_alarm_handoff;
    spin_unlock(&s_prompt_lock);
    if (volume_sent) {
        if (!release_sent) {
            result = d_device_volume_alert_end(token);
            if (result != D_OK) { d_local_prompt_alert_error(token, result); return; }
            spin_lock(&s_prompt_lock);
            if (s_alert_owner.token == token) s_alert_owner.release_sent = 1U;
            spin_unlock(&s_prompt_lock);
        }
        d_device_volume_get_alert_snapshot(&volume);
        if (volume.held || volume.busy) {
            if (volume.error != D_OK) d_local_prompt_alert_error(token, volume.error);
            return;
        }
    }
    spin_lock(&s_prompt_lock);
    next_token = s_alert_owner.next_token;
    /* Physical audio and volume are already stopped. Keep the observable owner
     * until the voice callback returns, while allowing it to restore standby. */
    s_alert_owner.releasing = 1U;
    spin_unlock(&s_prompt_lock);
    if (handoff_sent && handoff != NULL) handoff->release(token);
    spin_lock(&s_prompt_lock);
    memset(&s_alert_owner, 0, sizeof(s_alert_owner));
    s_alert_owner.next_token = next_token;
    spin_unlock(&s_prompt_lock);
}

static uint8_t d_local_prompt_alert_select_locked(void)
{
    d_local_prompt_source_e source;
    d_local_prompt_slot_t *slot;
    if (s_alert_owner.token) return 1U;
    source = s_sources[D_LOCAL_PROMPT_ALARM].wanted ? D_LOCAL_PROMPT_ALARM : D_LOCAL_PROMPT_PREVIEW;
    slot = &s_sources[source];
    if (!slot->wanted || !slot->pending || slot->error != D_OK || s_prompt.state.quiescing) return 0U;
    if (s_prompt.decoder_owned) {
        s_prompt.stop_requested = 1U;
        if (s_prompt.stop_failed) slot->error = D_ERR_IO;
        return 0U;
    }
    if (++s_alert_owner.next_token == 0U) ++s_alert_owner.next_token;
    s_alert_owner.token = s_alert_owner.next_token;
    s_alert_owner.request = slot->request;
    return 1U;
}

static void d_local_prompt_process_alert(void)
{
    d_local_prompt_request_t request;
    d_device_volume_alert_snapshot_t volume;
    const d_local_prompt_handoff_t *handoff;
    uint32_t token, generation;
    uint8_t release, handoff_sent, volume_sent, started;
    uint8_t ready = 0U;
    d_err_t error, result;
    spin_lock(&s_prompt_lock);
    if (s_sources[D_LOCAL_PROMPT_ALARM].wanted && s_sources[D_LOCAL_PROMPT_ALARM].pending &&
        (uint32_t)((uint32_t)timer_get_ms() - s_sources[D_LOCAL_PROMPT_ALARM].request.created_ms) >= D_LOCAL_PROMPT_HANDOFF_MS) {
        s_sources[D_LOCAL_PROMPT_ALARM].error = D_ERR_TIMEOUT;
        s_sources[D_LOCAL_PROMPT_ALARM].pending = 0U;
        if (s_alert_owner.token && s_alert_owner.request.source == D_LOCAL_PROMPT_ALARM)
            s_alert_owner.error = D_ERR_TIMEOUT;
    }
    if (!d_local_prompt_alert_select_locked()) { spin_unlock(&s_prompt_lock); return; }
    token = s_alert_owner.token; request = s_alert_owner.request;
    release = s_prompt.state.quiescing || !s_sources[request.source].wanted ||
        request.event_id != s_sources[request.source].request.event_id ||
        request.generation != s_sources[request.source].request.generation ||
        (request.source == D_LOCAL_PROMPT_PREVIEW && s_sources[D_LOCAL_PROMPT_ALARM].wanted);
    if (release && s_prompt.decoder_owned) s_prompt.stop_requested = 1U;
    handoff_sent = s_alert_owner.handoff_sent; volume_sent = s_alert_owner.volume_sent;
    started = s_alert_owner.start_issued; error = s_alert_owner.error; handoff = s_alarm_handoff;
    spin_unlock(&s_prompt_lock);
    if (release) { d_local_prompt_alert_release(); return; }
    if (error != D_OK || started) return;
    if ((uint32_t)((uint32_t)timer_get_ms() - request.created_ms) >=
        (request.source == D_LOCAL_PROMPT_ALARM ? D_LOCAL_PROMPT_HANDOFF_MS : 2000U)) {
        d_local_prompt_alert_error(token, D_ERR_TIMEOUT);
        if (request.source == D_LOCAL_PROMPT_PREVIEW) d_local_prompt_cancel_source(request.source, request.generation);
        return;
    }
#if D_FEATURE_QWEN
    if (request.source == D_LOCAL_PROMPT_ALARM) {
        if (handoff == NULL) return;
        if (!handoff_sent) {
            result = handoff->begin(token);
            if (result == D_ERR_NOT_READY) return;
            if (result != D_OK) { d_local_prompt_alert_error(token, result); return; }
            spin_lock(&s_prompt_lock); s_alert_owner.handoff_sent = 1U; spin_unlock(&s_prompt_lock);
        }
        result = handoff->poll(token, &ready);
        if (result != D_OK) { d_local_prompt_alert_error(token, result); return; }
        if (!ready) return;
    }
#else
    (void)handoff; (void)handoff_sent; (void)ready;
#endif
    if (!volume_sent) {
        result = d_device_volume_alert_begin(token, request.volume);
        if (result == D_ERR_NOT_READY) return;
        if (result != D_OK) { d_local_prompt_alert_error(token, result); return; }
        spin_lock(&s_prompt_lock); s_alert_owner.volume_sent = 1U; spin_unlock(&s_prompt_lock);
    }
    d_device_volume_get_alert_snapshot(&volume);
    if (volume.error != D_OK) { d_local_prompt_alert_error(token, volume.error); return; }
    if (volume.token != token || !volume.requested || volume.busy || volume.revision != volume.completed_revision) return;
    spin_lock(&s_prompt_lock);
    if (s_prompt.state.quiescing || !s_sources[request.source].wanted ||
        s_sources[request.source].request.generation != request.generation) { spin_unlock(&s_prompt_lock); return; }
    s_sources[request.source].pending = 0U;
    s_prompt.decoder_owned = 1U;
    s_prompt.decoder_started = 0U;
    s_prompt.active_source = (uint8_t)request.source;
    s_prompt.playing_event = 0U;
    if (++s_prompt.state.generation == 0U) ++s_prompt.state.generation;
    generation = s_prompt.state.generation;
    s_prompt.stop_requested = s_prompt.stop_failed = 0U;
    s_alert_owner.start_issued = 1U;
    spin_unlock(&s_prompt_lock);
    result = d_local_prompt_decoder_start_checked(generation, request.volume,
        request.source == D_LOCAL_PROMPT_ALARM, d_local_prompt_start_permitted);
    d_local_prompt_start_completed(generation, result);
    d_local_prompt_process_stop();
}

static void d_local_prompt_worker(void *context)
{
    int messages[16];
    (void)context;
    for (;;) {
        /* SDK dispatches server callbacks inside this queue receive. */
        (void)__os_taskq_pend(messages, 16, 2);
        d_local_prompt_process_stop();
        d_local_prompt_process_volume();
        d_local_prompt_process_alert();
        d_local_prompt_process_start();
    }
}

#if !D_FEATURE_QWEN
static d_err_t d_local_prompt_volume_backend(uint8_t percent, void *context)
{
    (void)context;
    return d_local_prompt_apply_volume(percent, D_DEVICE_VOLUME_TIMEOUT_MS);
}
static const d_device_volume_backend_t s_prompt_volume_backend = {d_local_prompt_volume_backend, NULL};
#endif

static d_err_t d_local_prompt_alert_volume_backend(uint8_t percent, void *context)
{
    (void)context;
    return d_local_prompt_apply_volume_kind(percent, D_DEVICE_VOLUME_TIMEOUT_MS, 1U);
}
static const d_device_volume_backend_t s_alert_volume_backend = {d_local_prompt_alert_volume_backend, NULL};

d_err_t d_local_prompt_init(void)
{
    d_err_t result;
    spin_lock(&s_prompt_lock);
    if (s_prompt.attempted) {
        result = s_prompt.state.ready ? D_OK : D_ERR_NOT_READY;
        spin_unlock(&s_prompt_lock); return result;
    }
    s_prompt.attempted = 1U;
    spin_unlock(&s_prompt_lock);
    s_prompt.volume = d_device_volume_get();
    s_prompt.target_volume = s_prompt.volume;
    result = thread_fork("local_prompt", 12, 1024, 128, &s_prompt.task_pid,
                         d_local_prompt_worker, NULL) == OS_NO_ERR ? D_OK : D_ERR_IO;
    spin_lock(&s_prompt_lock);
    s_prompt.state.ready = result == D_OK;
    s_prompt.state.error = result;
    spin_unlock(&s_prompt_lock);
    if (result == D_OK) {
        d_err_t alert_result = d_device_volume_register_alert_backend(&s_alert_volume_backend);
        spin_lock(&s_prompt_lock);
        s_prompt.alert_backend_ready = alert_result == D_OK;
        spin_unlock(&s_prompt_lock);
    }
#if !D_FEATURE_QWEN
    if (result == D_OK) {
        result = d_device_volume_register_backend(&s_prompt_volume_backend);
        spin_lock(&s_prompt_lock);
        s_prompt.state.error = result;
        s_prompt.state.ready = result == D_OK;
        spin_unlock(&s_prompt_lock);
    }
#endif
    return result;
}

void d_local_prompt_get_snapshot(d_local_prompt_snapshot_t *snapshot)
{
    if (snapshot == NULL) return;
    spin_lock(&s_prompt_lock);
    *snapshot = s_prompt.state;
    snapshot->playing = s_prompt.decoder_owned && s_prompt.decoder_started && s_prompt.active_source == D_LOCAL_PROMPT_POMODORO;
    spin_unlock(&s_prompt_lock);
}

static d_err_t d_local_prompt_request_pomodoro(uint32_t event_id, uint32_t created_ms)
{
    spin_lock(&s_prompt_lock);
    if (!event_id) { spin_unlock(&s_prompt_lock); return D_ERR_INVALID_ARG; }
    if (!s_prompt.state.ready || s_prompt.state.quiescing) { spin_unlock(&s_prompt_lock); return D_ERR_NOT_READY; }
    if (!d_local_prompt_after(event_id, s_prompt.canceled_event) ||
        (event_id != s_prompt.state.event_id && !d_local_prompt_after(event_id, s_prompt.state.event_id))) {
        spin_unlock(&s_prompt_lock); return D_ERR_BUSY;
    }
    if (event_id != s_prompt.state.event_id) {
        s_prompt.state.event_id = event_id;
        s_prompt.created_ms = created_ms;
        s_prompt.state.pending = 1U;
        if (s_prompt.active_source == D_LOCAL_PROMPT_POMODORO) {
            s_prompt.stop_requested = s_prompt.decoder_owned;
            s_prompt.stop_failed = 0U;
        }
    }
    spin_unlock(&s_prompt_lock);
    d_local_prompt_wake();
    return D_OK;
}

void d_local_prompt_request(uint32_t event_id, uint32_t created_ms)
{ (void)d_local_prompt_request_pomodoro(event_id, created_ms); }

void d_local_prompt_cancel(uint32_t event_id)
{
    spin_lock(&s_prompt_lock);
    if (event_id == 0U) event_id = s_prompt.state.event_id;
    if (d_local_prompt_after(event_id, s_prompt.canceled_event)) s_prompt.canceled_event = event_id;
    if (!d_local_prompt_after(s_prompt.state.event_id, event_id)) s_prompt.state.pending = 0U;
    if (s_prompt.active_source == D_LOCAL_PROMPT_POMODORO && !d_local_prompt_after(s_prompt.playing_event, event_id)) {
        s_prompt.stop_requested = 1U;
        s_prompt.stop_failed = 0U;
    }
    spin_unlock(&s_prompt_lock);
    d_local_prompt_wake();
}

void d_local_prompt_foreground_begin(void)
{
    spin_lock(&s_prompt_lock);
    s_prompt.state.foreground = 1U;
    s_sources[D_LOCAL_PROMPT_PREVIEW].wanted = s_sources[D_LOCAL_PROMPT_PREVIEW].pending = 0U;
    if (s_prompt.active_source != D_LOCAL_PROMPT_ALARM) {
        s_prompt.stop_requested = 1U;
        s_prompt.stop_failed = 0U;
    }
    spin_unlock(&s_prompt_lock);
    d_local_prompt_wake();
}

void d_local_prompt_foreground_end(void)
{
    spin_lock(&s_prompt_lock);
    s_prompt.state.foreground = 0U;
    spin_unlock(&s_prompt_lock);
    d_local_prompt_wake();
}

d_err_t d_local_prompt_wait_stopped(uint32_t timeout_ms)
{
    uint32_t started = (uint32_t)timer_get_ms();
    for (;;) {
        uint8_t playing;
        uint8_t failed;
        uint8_t alarm;
        spin_lock(&s_prompt_lock);
        playing = s_prompt.decoder_owned || s_alert_owner.token != 0U;
        failed = s_prompt.stop_failed || (s_alert_owner.token && s_alert_owner.error != D_OK);
        alarm = !s_prompt.state.quiescing && s_sources[D_LOCAL_PROMPT_ALARM].wanted;
        spin_unlock(&s_prompt_lock);
        if (alarm) return D_ERR_BUSY;
        if (!playing) return D_OK;
        if (failed) return D_ERR_IO;
        if ((uint32_t)((uint32_t)timer_get_ms() - started) >= timeout_ms) return D_ERR_TIMEOUT;
        os_time_dly(1);
    }
}

static d_err_t d_local_prompt_apply_volume_kind(uint8_t percent, uint32_t timeout_ms, uint8_t alert)
{
    uint32_t request;
    uint32_t started = (uint32_t)timer_get_ms();
    if (percent > 100U) return D_ERR_INVALID_ARG;
    spin_lock(&s_prompt_lock);
    if (!s_prompt.state.ready) { spin_unlock(&s_prompt_lock); return D_ERR_NOT_READY; }
    if (s_prompt.volume_request != s_prompt.volume_completed) { spin_unlock(&s_prompt_lock); return D_ERR_BUSY; }
    s_prompt.target_volume = percent;
    s_prompt.volume_is_alert = alert;
    s_prompt.volume_expired = 0U;
    s_prompt.volume_request++;
    request = s_prompt.volume_request;
    spin_unlock(&s_prompt_lock);
    d_local_prompt_wake();
    for (;;) {
        uint8_t complete;
        d_err_t result;
        spin_lock(&s_prompt_lock);
        complete = s_prompt.volume_completed == request;
        result = s_prompt.volume_error;
        spin_unlock(&s_prompt_lock);
        if (complete) return result;
        if ((uint32_t)((uint32_t)timer_get_ms() - started) >= timeout_ms) {
            spin_lock(&s_prompt_lock);
            if (s_prompt.volume_completed == request) {
                result = s_prompt.volume_error;
            } else {
                s_prompt.volume_expired = 1U;
                s_prompt.volume_error = D_ERR_TIMEOUT;
                if (alert) s_alert_owner.error = D_ERR_TIMEOUT;
                else { s_prompt.state.error = D_ERR_TIMEOUT; s_prompt.media_volume_error = D_ERR_TIMEOUT; }
                s_prompt.stop_requested = 1U;
                result = D_ERR_TIMEOUT;
            }
            spin_unlock(&s_prompt_lock);
            d_local_prompt_wake();
            return result;
        }
        os_time_dly(1);
    }
}

d_err_t d_local_prompt_apply_volume(uint8_t percent, uint32_t timeout_ms)
{ return d_local_prompt_apply_volume_kind(percent, timeout_ms, 0U); }

void d_local_prompt_quiesce_begin(void)
{
    spin_lock(&s_prompt_lock);
    s_prompt.state.quiescing = 1U;
    s_prompt.state.pending = 0U;
    s_sources[D_LOCAL_PROMPT_ALARM].wanted = s_sources[D_LOCAL_PROMPT_ALARM].pending = 0U;
    s_sources[D_LOCAL_PROMPT_PREVIEW].wanted = s_sources[D_LOCAL_PROMPT_PREVIEW].pending = 0U;
    s_alert_owner.release_sent = 0U;
    s_prompt.stop_requested = 1U;
    s_prompt.stop_failed = 0U;
    spin_unlock(&s_prompt_lock);
    d_local_prompt_wake();
}

void d_local_prompt_quiesce_cancel(void)
{
    spin_lock(&s_prompt_lock);
    s_prompt.state.quiescing = 0U;
    spin_unlock(&s_prompt_lock);
}

d_err_t d_local_prompt_register_alarm_handoff(const d_local_prompt_handoff_t *handoff)
{
    if (handoff == NULL || handoff->begin == NULL || handoff->poll == NULL || handoff->release == NULL)
        return D_ERR_INVALID_ARG;
    spin_lock(&s_prompt_lock);
    if (s_alarm_handoff != NULL && s_alarm_handoff != handoff) { spin_unlock(&s_prompt_lock); return D_ERR_BUSY; }
    s_alarm_handoff = handoff;
    spin_unlock(&s_prompt_lock);
    d_local_prompt_wake();
    return D_OK;
}

uint8_t d_local_prompt_alarm_requested(void)
{
    uint8_t requested;
    spin_lock(&s_prompt_lock);
    requested = s_sources[D_LOCAL_PROMPT_ALARM].wanted ||
        (s_alert_owner.token && !s_alert_owner.releasing && s_alert_owner.request.source == D_LOCAL_PROMPT_ALARM);
    spin_unlock(&s_prompt_lock);
    return requested;
}

d_err_t d_local_prompt_request_source(const d_local_prompt_request_t *request)
{
    d_local_prompt_slot_t *slot;
    d_device_volume_alert_snapshot_t volume;
    if (request == NULL || request->source < D_LOCAL_PROMPT_POMODORO || request->source >= D_LOCAL_PROMPT_SOURCE_COUNT ||
        !request->event_id || !request->generation) return D_ERR_INVALID_ARG;
    if (request->source == D_LOCAL_PROMPT_POMODORO) {
        if (request->generation != request->event_id) return D_ERR_INVALID_ARG;
        return d_local_prompt_request_pomodoro(request->event_id, request->created_ms);
    }
    if (request->volume < 1U || request->volume > 100U) return D_ERR_INVALID_ARG;
    if (request->source == D_LOCAL_PROMPT_PREVIEW && d_local_prompt_foreground_busy()) return D_ERR_BUSY;
    d_device_volume_get_alert_snapshot(&volume);
    if (!volume.ready) return D_ERR_NOT_READY;
    spin_lock(&s_prompt_lock);
    slot = &s_sources[request->source];
    if (!s_prompt.state.ready || !s_prompt.alert_backend_ready || s_prompt.state.quiescing) {
        spin_unlock(&s_prompt_lock); return D_ERR_NOT_READY;
    }
    if (request->source == D_LOCAL_PROMPT_PREVIEW && (s_prompt.state.foreground || s_sources[D_LOCAL_PROMPT_ALARM].wanted ||
        (s_alert_owner.token && s_alert_owner.request.source == D_LOCAL_PROMPT_ALARM))) {
        spin_unlock(&s_prompt_lock); return D_ERR_BUSY;
    }
    if (!d_local_prompt_after(request->generation, slot->canceled_generation) ||
        (slot->request.generation && request->generation != slot->request.generation &&
         !d_local_prompt_after(request->generation, slot->request.generation))) {
        spin_unlock(&s_prompt_lock); return D_ERR_BUSY;
    }
    if (request->generation == slot->request.generation) {
        d_err_t result = request->event_id == slot->request.event_id ? D_OK : D_ERR_INVALID_ARG;
        spin_unlock(&s_prompt_lock); return result;
    }
    slot->request = *request;
    slot->wanted = slot->pending = 1U;
    slot->error = D_OK;
    if (s_prompt.decoder_owned) s_prompt.stop_requested = 1U;
    s_prompt.stop_failed = 0U;
    s_alert_owner.release_sent = 0U;
    spin_unlock(&s_prompt_lock);
    d_local_prompt_wake();
    return D_OK;
}

void d_local_prompt_cancel_source(d_local_prompt_source_e source, uint32_t through_generation)
{
    d_local_prompt_slot_t *slot;
    if (source < D_LOCAL_PROMPT_POMODORO || source >= D_LOCAL_PROMPT_SOURCE_COUNT) return;
    if (source == D_LOCAL_PROMPT_POMODORO) { d_local_prompt_cancel(through_generation); return; }
    spin_lock(&s_prompt_lock);
    slot = &s_sources[source];
    if (!through_generation) through_generation = slot->request.generation;
    if (d_local_prompt_after(through_generation, slot->canceled_generation)) slot->canceled_generation = through_generation;
    if (!d_local_prompt_after(slot->request.generation, through_generation)) slot->wanted = slot->pending = 0U;
    if (s_alert_owner.token && s_alert_owner.request.source == source &&
        !d_local_prompt_after(s_alert_owner.request.generation, through_generation)) {
        s_prompt.stop_requested = 1U;
        s_prompt.stop_failed = 0U;
        s_alert_owner.release_sent = 0U;
        s_alert_owner.error = D_OK;
    }
    spin_unlock(&s_prompt_lock);
    d_local_prompt_wake();
}

void d_local_prompt_get_source_snapshot(d_local_prompt_source_e source, d_local_prompt_source_snapshot_t *snapshot)
{
    d_device_volume_alert_snapshot_t volume;
    uint32_t now = (uint32_t)timer_get_ms();
    if (snapshot == NULL) return;
    memset(snapshot, 0, sizeof(*snapshot));
    if (source < D_LOCAL_PROMPT_POMODORO || source >= D_LOCAL_PROMPT_SOURCE_COUNT) return;
    d_device_volume_get_alert_snapshot(&volume);
    spin_lock(&s_prompt_lock);
    snapshot->ready = s_prompt.state.ready;
    if (source == D_LOCAL_PROMPT_POMODORO) {
        snapshot->event_id = snapshot->generation = s_prompt.state.event_id;
        snapshot->error = s_prompt.state.error;
        snapshot->pending = s_prompt.state.pending;
        snapshot->owned = s_prompt.decoder_owned && s_prompt.active_source == source;
        snapshot->playing = snapshot->owned && s_prompt.decoder_started;
    } else {
        snapshot->event_id = s_sources[source].request.event_id;
        snapshot->generation = s_sources[source].request.generation;
        snapshot->error = s_sources[source].error;
        snapshot->pending = s_sources[source].pending;
        snapshot->owned = s_alert_owner.token && s_alert_owner.request.source == source;
        snapshot->playing = s_prompt.decoder_owned && s_prompt.decoder_started && s_prompt.active_source == source;
        snapshot->ready = snapshot->ready && s_prompt.alert_backend_ready && volume.ready;
        if (s_sources[source].wanted && s_alert_owner.token && s_alert_owner.error != D_OK)
            snapshot->error = s_alert_owner.error;
        if (snapshot->owned) {
            if (s_alert_owner.error != D_OK) snapshot->error = s_alert_owner.error;
            if (snapshot->playing) {
                snapshot->event_id = s_alert_owner.request.event_id;
                snapshot->generation = s_alert_owner.request.generation;
            }
            if (snapshot->error == D_OK && s_prompt.decoder_owned && !s_prompt.decoder_started && s_alert_owner.start_issued &&
                (uint32_t)(now - s_alert_owner.request.created_ms) >=
                (source == D_LOCAL_PROMPT_ALARM ? D_LOCAL_PROMPT_HANDOFF_MS : 2000U)) snapshot->error = D_ERR_TIMEOUT;
        }
    }
    spin_unlock(&s_prompt_lock);
}
#endif
