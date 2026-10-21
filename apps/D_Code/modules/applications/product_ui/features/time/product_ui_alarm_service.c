/**
 * @file product_ui_alarm_service.c
 * @brief Single alarm owner with asynchronous load/save and nonblocking sound requests.
 * @author D_Code
 * @date 2026-09-21
 */
#include "config/app_config.h"
#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)
#include "product_ui_alarm_service.h"
#include "product_ui_alarm_port.h"
#include "product_ui_clock.h"
#include "product_ui_clock_port.h"
#include "product_ui_time_domain_internal.h"
#include "system/spinlock.h"
#include "os/os_api.h"
#include "framework/d_log.h"
#include <string.h>

static DEFINE_SPINLOCK(s_alarm_lock);
static struct {
    product_ui_alarm_model_t model;
    product_ui_alarm_snapshot_t status;
    OS_SEM wake;
    uint32_t sequence;
    uint32_t failed_revision;
    uint32_t submitted_event;
    uint32_t audio_attempt;
    uint32_t canceled_attempt;
    uint16_t timer;
    int worker_pid;
    uint8_t active_slot;
    uint8_t attempted;
    uint8_t retry;
    uint8_t retry_load;
    uint8_t stop_requested;
    uint8_t worker_stopped;
    uint8_t has_record;
    uint8_t cancel_requested;
} s_alarm;

/* These buffers belong only to the storage worker, never its 4KB stack. */
static product_ui_alarm_saved_t s_alarm_save_snapshot;
static uint8_t s_alarm_encoded[PRODUCT_UI_ALARM_RECORD_SIZE];
static uint8_t s_alarm_readback[PRODUCT_UI_ALARM_RECORD_SIZE];

static void product_ui_alarm_next_request(void)
{
    if (++s_alarm.status.request_id == 0U) ++s_alarm.status.request_id;
}

static void product_ui_alarm_load(void)
{
    uint8_t slot;
    uint8_t has_record = 0U;
    uint8_t protected_record = 0U;
    uint8_t unreadable = 0U;
    uint8_t absent_slots = 0U;
    d_err_t load_error = D_ERR_NOT_FOUND;
    for (slot = 0U; slot < 2U; slot++) {
        uint32_t sequence = 0U;
        int length = product_ui_alarm_storage_read(slot, s_alarm_encoded, sizeof(s_alarm_encoded));
        d_err_t result;
        if (length == PRODUCT_UI_ALARM_ERR_VERSION) result = PRODUCT_UI_ALARM_ERR_VERSION;
        else if (length == D_ERR_NOT_FOUND) { absent_slots++; continue; }
        else if (length == D_ERR_NOT_READY) { unreadable = 1U; load_error = D_ERR_IO; continue; }
        else result = length == PRODUCT_UI_ALARM_RECORD_SIZE ?
            product_ui_alarm_decode(s_alarm_encoded, &s_alarm_save_snapshot, &sequence) : D_ERR_IO;
        if (result == PRODUCT_UI_ALARM_ERR_VERSION) { protected_record = 1U; continue; }
        if (result != D_OK) { load_error = result; continue; }
        if (!has_record || product_ui_alarm_sequence_after(sequence, s_alarm.sequence)) {
            spin_lock(&s_alarm_lock);
            s_alarm.model.saved = s_alarm_save_snapshot;
            spin_unlock(&s_alarm_lock);
            s_alarm.sequence = sequence;
            s_alarm.active_slot = slot;
            has_record = 1U;
        }
    }
    spin_lock(&s_alarm_lock);
    if (has_record) s_alarm_save_snapshot = s_alarm.model.saved;
    product_ui_alarm_model_init(&s_alarm.model, has_record && !protected_record ? &s_alarm_save_snapshot : NULL);
    s_alarm.has_record = has_record && !protected_record;
    s_alarm.status.persisted_revision = 1U;
    /* Lack of a valid record is not proof of first use: both reads must say absent. */
    if (absent_slots == 2U && product_ui_alarm_model_seed_defaults(&s_alarm.model) == D_OK)
        product_ui_alarm_next_request();
    s_alarm.status.storage_protected = protected_record;
    s_alarm.status.load_error = protected_record ? PRODUCT_UI_ALARM_ERR_VERSION : has_record ? D_OK : load_error;
    s_alarm.status.loaded = !unreadable;
    s_alarm.status.storage_error = protected_record ? PRODUCT_UI_ALARM_ERR_VERSION : unreadable ? D_ERR_IO : D_OK;
    s_alarm.retry_load = 0U;
    load_error = s_alarm.status.load_error;
    spin_unlock(&s_alarm_lock);
    LOGI("ALARM_STORE", "load absent=%u valid=%u protected=%u unreadable=%u error=%d presets=%u",
        absent_slots, has_record, protected_record, unreadable, load_error, absent_slots == 2U);
}

static d_err_t product_ui_alarm_write(uint8_t slot, uint32_t sequence)
{
    product_ui_alarm_encode(&s_alarm_save_snapshot, sequence, s_alarm_encoded);
    if (product_ui_alarm_storage_write(slot, s_alarm_encoded, sizeof(s_alarm_encoded)) != PRODUCT_UI_ALARM_RECORD_SIZE ||
        product_ui_alarm_storage_read(slot, s_alarm_readback, sizeof(s_alarm_readback)) != PRODUCT_UI_ALARM_RECORD_SIZE ||
        memcmp(s_alarm_encoded, s_alarm_readback, sizeof(s_alarm_encoded)) != 0) return D_ERR_IO;
    return D_OK;
}

static void product_ui_alarm_save_worker(void *context)
{
    (void)context;
    product_ui_alarm_load();
    for (;;) {
        uint32_t revision, request;
        uint8_t has_work, reload;
        d_err_t result;
        spin_lock(&s_alarm_lock);
        revision = s_alarm.model.revision;
        reload = s_alarm.retry_load && !s_alarm.status.loaded;
        has_work = s_alarm.status.loaded && !s_alarm.status.storage_protected &&
            revision != s_alarm.status.persisted_revision &&
            (revision != s_alarm.failed_revision || s_alarm.retry);
        if (!has_work && !reload && s_alarm.stop_requested) {
            s_alarm.worker_stopped = 1U;
            s_alarm.status.storage_ready = 0U;
            spin_unlock(&s_alarm_lock); return;
        }
        request = s_alarm.status.request_id;
        if (has_work) {
            s_alarm_save_snapshot = s_alarm.model.saved;
            s_alarm.status.saving = 1U;
            s_alarm.retry = 0U;
        }
        spin_unlock(&s_alarm_lock);
        if (reload) { product_ui_alarm_load(); continue; }
        if (!has_work) { (void)os_sem_pend(&s_alarm.wake, 10); continue; }
        result = product_ui_alarm_write((uint8_t)(1U - s_alarm.active_slot), s_alarm.sequence + 1U);
        spin_lock(&s_alarm_lock);
        if (result == D_OK) {
            s_alarm.active_slot = (uint8_t)(1U - s_alarm.active_slot);
            s_alarm.sequence++;
            s_alarm.status.persisted_revision = revision;
            s_alarm.failed_revision = 0U;
            s_alarm.has_record = 1U;
        } else s_alarm.failed_revision = revision;
        s_alarm.status.completed_id = request;
        s_alarm.status.storage_error = result;
        s_alarm.status.saving = 0U;
        spin_unlock(&s_alarm_lock);
    }
}

static void product_ui_alarm_tick(void *context)
{
    product_ui_clock_snapshot_t clock;
    product_ui_alarm_audio_snapshot_t audio;
    uint32_t previous_revision;
    uint32_t request_event = 0U, attempt = 0U;
    uint32_t cancel_attempt;
    uint16_t ringing, stopping;
    uint8_t volume = 0U, cancel = 0U, send_cancel = 0U;
    (void)context;
    product_ui_clock_get_snapshot(&clock);
    product_ui_alarm_audio_snapshot(&audio);
    spin_lock(&s_alarm_lock);
    if (!s_alarm.status.loaded || !s_alarm.status.timer_ready || s_alarm.status.storage_protected) {
        spin_unlock(&s_alarm_lock); return;
    }
    previous_revision = s_alarm.model.revision;
    if (!audio.ready) s_alarm.status.audio_error = D_ERR_NOT_READY;
    else if (!s_alarm.submitted_event ||
        (audio.event_id == s_alarm.submitted_event && audio.attempt == s_alarm.audio_attempt))
        s_alarm.status.audio_error = audio.error;
    s_alarm.status.audio_owned = audio.owned;
    s_alarm.status.audio_playing = audio.playing;
    if (!s_alarm.status.quiescing) product_ui_alarm_model_advance(&s_alarm.model, &clock);
    stopping = product_ui_alarm_model_stop_mask(&s_alarm.model);
    if (stopping && !audio.playing && !audio.pending && !audio.owned)
        product_ui_alarm_model_audio_stopped(&s_alarm.model);
    ringing = product_ui_alarm_model_ring_mask(&s_alarm.model);
    stopping = product_ui_alarm_model_stop_mask(&s_alarm.model);
    cancel = s_alarm.status.quiescing || stopping || !ringing;
    if (!cancel && audio.playing && audio.event_id == s_alarm.model.group_generation && audio.attempt == s_alarm.audio_attempt)
        product_ui_alarm_model_audio_started(&s_alarm.model, s_alarm.model.group_generation);
    if (!cancel && audio.ready && s_alarm.submitted_event != s_alarm.model.group_generation) {
        request_event = s_alarm.model.group_generation;
        s_alarm.submitted_event = request_event;
        if (++s_alarm.audio_attempt == 0U) ++s_alarm.audio_attempt;
        attempt = s_alarm.audio_attempt;
        volume = s_alarm.model.saved.volume;
        s_alarm.cancel_requested = 0U;
        s_alarm.status.audio_error = D_OK;
    }
    if (cancel) s_alarm.submitted_event = 0U;
    cancel_attempt = s_alarm.audio_attempt;
    if (cancel && (!s_alarm.cancel_requested || s_alarm.canceled_attempt != cancel_attempt)) {
        s_alarm.cancel_requested = 1U;
        s_alarm.canceled_attempt = cancel_attempt;
        send_cancel = 1U;
    }
    if (previous_revision != s_alarm.model.revision) product_ui_alarm_next_request();
    spin_unlock(&s_alarm_lock);
    if (send_cancel) product_ui_alarm_audio_cancel(cancel_attempt);
    if (request_event) {
        d_err_t result = product_ui_alarm_audio_request(request_event, attempt, volume);
        if (result != D_OK) {
            spin_lock(&s_alarm_lock);
            if (attempt == s_alarm.audio_attempt && request_event == s_alarm.model.group_generation)
                s_alarm.status.audio_error = result;
            spin_unlock(&s_alarm_lock);
        }
    }
}

void product_ui_alarm_get_snapshot(product_ui_alarm_snapshot_t *snapshot)
{
    uint8_t index;
    if (snapshot == NULL) return;
    spin_lock(&s_alarm_lock);
    *snapshot = s_alarm.status;
    snapshot->clock = s_alarm.model.clock;
    snapshot->revision = s_alarm.model.revision;
    snapshot->group_generation = s_alarm.model.group_generation;
    snapshot->volume = s_alarm.model.saved.volume;
    snapshot->has_record = s_alarm.has_record;
    snapshot->ring_mask = product_ui_alarm_model_ring_mask(&s_alarm.model);
    snapshot->stop_mask = product_ui_alarm_model_stop_mask(&s_alarm.model);
    snapshot->dirty = s_alarm.model.revision != s_alarm.status.persisted_revision;
    for (index = 0U; index < PRODUCT_UI_ALARM_COUNT; index++) {
        const product_ui_alarm_record_t *record = &s_alarm.model.saved.records[index];
        int64_t due;
        if (!record->rule.id) continue;
        snapshot->count++;
        snapshot->enabled_count += record->rule.enabled != 0U;
        snapshot->missed_count += record->notice != 0U;
        due = record->state == PRODUCT_UI_ALARM_SNOOZED ? record->snooze_utc :
            record->rule.enabled ? record->next_regular_utc : 0;
        if (due && snapshot->clock.valid && (!snapshot->next_utc || due < snapshot->next_utc ||
            (due == snapshot->next_utc && record->rule.id < snapshot->next_id))) {
            snapshot->next_utc = due;
            snapshot->next_id = record->rule.id;
        }
    }
    spin_unlock(&s_alarm_lock);
}

d_err_t product_ui_alarm_get_record(uint32_t id, product_ui_alarm_record_t *record)
{
    int index;
    if (record == NULL || !id) return D_ERR_INVALID_ARG;
    spin_lock(&s_alarm_lock);
    if (!s_alarm.status.loaded) { spin_unlock(&s_alarm_lock); return D_ERR_NOT_READY; }
    index = product_ui_alarm_model_find(&s_alarm.model, id);
    if (index >= 0) *record = s_alarm.model.saved.records[index];
    spin_unlock(&s_alarm_lock);
    return index >= 0 ? D_OK : D_ERR_NOT_FOUND;
}

d_err_t product_ui_alarm_get_at(uint8_t index, product_ui_alarm_record_t *record)
{
    uint8_t slot, count = 0U;
    d_err_t result = D_ERR_NOT_FOUND;
    if (record == NULL || index >= PRODUCT_UI_ALARM_COUNT) return D_ERR_INVALID_ARG;
    spin_lock(&s_alarm_lock);
    if (!s_alarm.status.loaded) { spin_unlock(&s_alarm_lock); return D_ERR_NOT_READY; }
    for (slot = 0U; slot < PRODUCT_UI_ALARM_COUNT; slot++) {
        if (!s_alarm.model.saved.records[slot].rule.id) continue;
        if (count++ == index) { *record = s_alarm.model.saved.records[slot]; result = D_OK; break; }
    }
    spin_unlock(&s_alarm_lock);
    return result;
}

d_err_t product_ui_alarm_submit(const product_ui_alarm_command_t *command, uint32_t *request, uint32_t *affected_id)
{
    product_ui_clock_snapshot_t clock;
    d_err_t result;
    uint32_t before, group_before, cancel_attempt;
    uint8_t wake;
    if (request != NULL) *request = 0U;
    if (affected_id != NULL) *affected_id = 0U;
    if (command == NULL) return D_ERR_INVALID_ARG;
    product_ui_clock_get_snapshot(&clock);
    spin_lock(&s_alarm_lock);
    if (s_alarm.status.storage_protected) { spin_unlock(&s_alarm_lock); return PRODUCT_UI_ALARM_ERR_VERSION; }
    if (!s_alarm.status.loaded || s_alarm.status.quiescing) {
        spin_unlock(&s_alarm_lock); return D_ERR_NOT_READY;
    }
    before = s_alarm.model.revision;
    group_before = s_alarm.model.group_generation;
    if (s_alarm.status.timer_ready) product_ui_alarm_model_advance(&s_alarm.model, &clock);
    result = product_ui_alarm_model_submit(&s_alarm.model, command, affected_id);
    if (result == D_OK && command->action == PRODUCT_UI_ALARM_VOLUME && !s_alarm.has_record &&
        s_alarm.model.revision == before) {
        if (++s_alarm.model.revision == 0U) ++s_alarm.model.revision;
    }
    if (before != s_alarm.model.revision) product_ui_alarm_next_request();
    if (request != NULL) *request = s_alarm.status.request_id;
    wake = s_alarm.status.storage_ready;
    cancel_attempt = group_before != s_alarm.model.group_generation ? s_alarm.audio_attempt : 0U;
    if (cancel_attempt) { s_alarm.cancel_requested = 1U; s_alarm.canceled_attempt = cancel_attempt; }
    spin_unlock(&s_alarm_lock);
    if (cancel_attempt) product_ui_alarm_audio_cancel(cancel_attempt);
    if (wake) (void)os_sem_post(&s_alarm.wake);
    return result;
}

d_err_t product_ui_alarm_retry_save(uint32_t *request)
{
    spin_lock(&s_alarm_lock);
    if (s_alarm.status.storage_protected) { spin_unlock(&s_alarm_lock); return PRODUCT_UI_ALARM_ERR_VERSION; }
    if (!s_alarm.status.storage_ready || s_alarm.status.quiescing) {
        spin_unlock(&s_alarm_lock); return D_ERR_NOT_READY;
    }
    if (!s_alarm.status.loaded) s_alarm.retry_load = 1U;
    else {
        if (!s_alarm.has_record && s_alarm.model.revision == s_alarm.status.persisted_revision) {
            if (++s_alarm.model.revision == 0U) ++s_alarm.model.revision;
        }
        s_alarm.retry = 1U;
    }
    product_ui_alarm_next_request();
    if (request != NULL) *request = s_alarm.status.request_id;
    spin_unlock(&s_alarm_lock);
    (void)os_sem_post(&s_alarm.wake);
    return D_OK;
}

d_err_t product_ui_alarm_retry_audio(uint32_t group_generation)
{
    uint32_t cancel_attempt = 0U;
    spin_lock(&s_alarm_lock);
    if (s_alarm.status.quiescing || group_generation != s_alarm.model.group_generation) {
        spin_unlock(&s_alarm_lock); return D_ERR_BUSY;
    }
    if (product_ui_alarm_model_stop_mask(&s_alarm.model) ||
        (!product_ui_alarm_model_ring_mask(&s_alarm.model) && s_alarm.status.audio_owned))
        cancel_attempt = s_alarm.audio_attempt;
    else if (product_ui_alarm_model_ring_mask(&s_alarm.model)) s_alarm.submitted_event = 0U;
    else { spin_unlock(&s_alarm_lock); return D_ERR_BUSY; }
    spin_unlock(&s_alarm_lock);
    if (cancel_attempt) product_ui_alarm_audio_cancel(cancel_attempt);
    return D_OK;
}

d_err_t product_ui_alarm_preview(uint8_t volume)
{
    if (volume < 1U || volume > 100U) return D_ERR_INVALID_ARG;
    return product_ui_alarm_audio_preview(volume);
}

void product_ui_alarm_stop_preview(void) { product_ui_alarm_audio_preview_cancel(); }
d_err_t product_ui_alarm_preview_status(uint8_t *active) { return product_ui_alarm_audio_preview_status(active); }

static product_ui_time_result_e product_ui_alarm_domain_result(d_err_t result)
{
    if (result == D_OK) return PRODUCT_UI_TIME_OK;
    if (result == D_ERR_FULL) return PRODUCT_UI_TIME_ERR_CAPACITY;
    if (result == D_ERR_NOT_FOUND) return PRODUCT_UI_TIME_ERR_NOT_FOUND;
    if (result == D_ERR_BUSY) return PRODUCT_UI_TIME_ERR_CONFLICT;
    return result == D_ERR_INVALID_ARG ? PRODUCT_UI_TIME_ERR_INVALID_ARGUMENT : PRODUCT_UI_TIME_ERR_UNAVAILABLE;
}

static product_ui_time_result_e product_ui_alarm_domain_get(uint8_t index, product_ui_alarm_t *alarm)
{
    product_ui_alarm_record_t record;
    d_err_t result;
    if (alarm == NULL) return PRODUCT_UI_TIME_ERR_INVALID_ARGUMENT;
    result = product_ui_alarm_get_at(index, &record);
    if (result == D_OK) *alarm = record.rule;
    return product_ui_alarm_domain_result(result);
}

static product_ui_time_result_e product_ui_alarm_domain_copy(product_ui_alarm_t *alarms,
    product_ui_time_alarm_summary_t *summary)
{
    uint8_t index;
    int64_t next = 0;
    if (summary == NULL) return PRODUCT_UI_TIME_ERR_INVALID_ARGUMENT;
    memset(summary, 0, sizeof(*summary));
    spin_lock(&s_alarm_lock);
    summary->generation = s_alarm.model.revision;
    summary->runtime_available = s_alarm.status.loaded && s_alarm.status.timer_ready &&
        !s_alarm.status.storage_protected && !s_alarm.status.quiescing && s_alarm.model.clock.valid;
    for (index = 0U; index < PRODUCT_UI_ALARM_COUNT && s_alarm.status.loaded; index++) {
        const product_ui_alarm_record_t *record = &s_alarm.model.saved.records[index];
        int64_t due;
        if (!record->rule.id) continue;
        if (alarms != NULL) alarms[summary->count] = record->rule;
        summary->count++;
        due = record->state == PRODUCT_UI_ALARM_SNOOZED ? record->snooze_utc :
            record->rule.enabled ? record->next_regular_utc : 0;
        if (due && summary->runtime_available && (!next || due < next ||
            (due == next && record->rule.id < summary->next_id))) {
            next = due; summary->next_id = record->rule.id;
        }
    }
    spin_unlock(&s_alarm_lock);
    if (next && product_ui_time_utc_to_beijing(next, &summary->next_at) == PRODUCT_UI_TIME_OK) summary->next_valid = 1U;
    return PRODUCT_UI_TIME_OK;
}

static product_ui_time_result_e product_ui_alarm_domain_execute(
    const product_ui_time_domain_command_t *command, uint32_t *affected_id)
{
    product_ui_alarm_command_t translated = {0};
    product_ui_alarm_record_t record;
    uint32_t id;
    d_err_t result;
    if (command == NULL) return PRODUCT_UI_TIME_ERR_INVALID_ARGUMENT;
    if (command->type == PRODUCT_UI_TIME_COMMAND_UPSERT_ALARM) {
        translated.action = PRODUCT_UI_ALARM_EDIT;
        translated.rule = command->data.alarm;
        translated.label_kind = 255U;
        id = translated.rule.id;
    } else if (command->type == PRODUCT_UI_TIME_COMMAND_REMOVE_ALARM) {
        translated.action = PRODUCT_UI_ALARM_DELETE;
        id = command->data.id.id;
    } else if (command->type == PRODUCT_UI_TIME_COMMAND_SET_ALARM_ENABLED) {
        translated.action = PRODUCT_UI_ALARM_ENABLE;
        translated.enabled = command->data.alarm_enabled.enabled;
        id = command->data.alarm_enabled.id;
    } else return PRODUCT_UI_TIME_ERR_INVALID_ARGUMENT;
    translated.id = id;
    if (id) {
        result = product_ui_alarm_get_record(id, &record);
        if (result != D_OK) return product_ui_alarm_domain_result(result);
        translated.expected_revision = record.rule_revision;
    }
    return product_ui_alarm_domain_result(product_ui_alarm_submit(&translated, NULL, affected_id));
}

static const product_ui_time_alarm_backend_t s_alarm_backend = {
    product_ui_alarm_domain_copy, product_ui_alarm_domain_get, product_ui_alarm_domain_execute,
};

d_err_t product_ui_alarm_init(void)
{
    uint16_t timer;
    d_err_t clock_result;
    uint8_t storage_ready;
    spin_lock(&s_alarm_lock);
    if (s_alarm.attempted) {
        d_err_t result = s_alarm.status.timer_error;
        spin_unlock(&s_alarm_lock); return result;
    }
    s_alarm.attempted = 1U;
    product_ui_alarm_model_init(&s_alarm.model, NULL);
    s_alarm.status.persisted_revision = 1U;
    s_alarm.status.load_error = D_ERR_NOT_READY;
    s_alarm.status.storage_error = D_ERR_NOT_READY;
    s_alarm.status.timer_error = D_ERR_NOT_READY;
    spin_unlock(&s_alarm_lock);
    product_ui_time_domain_bind_alarms(&s_alarm_backend);
    clock_result = product_ui_clock_init();
    storage_ready = os_sem_create(&s_alarm.wake, 0) == OS_NO_ERR &&
        thread_fork("alarm_store", 12, 1024, 0, &s_alarm.worker_pid, product_ui_alarm_save_worker, NULL) == OS_NO_ERR;
    timer = product_ui_alarm_timer_start(product_ui_alarm_tick);
    spin_lock(&s_alarm_lock);
    s_alarm.timer = timer;
    s_alarm.status.timer_ready = timer != 0U && clock_result == D_OK;
    s_alarm.status.timer_error = s_alarm.status.timer_ready ? D_OK : D_ERR_IO;
    s_alarm.status.storage_ready = storage_ready;
    if (!storage_ready) {
        s_alarm.status.storage_error = D_ERR_IO;
        s_alarm.status.load_error = D_ERR_IO;
        /* Session-only alarms are usable; this failed worker can never overwrite VM. */
        s_alarm.status.loaded = 1U;
    }
    spin_unlock(&s_alarm_lock);
    return timer && clock_result == D_OK ? D_OK : D_ERR_IO;
}

d_err_t product_ui_alarm_maintenance_begin(void)
{
    uint32_t attempt;
    uint8_t wake;
    product_ui_alarm_tick(NULL);
    spin_lock(&s_alarm_lock);
    if (!s_alarm.status.loaded || s_alarm.status.quiescing ||
        product_ui_alarm_model_ring_mask(&s_alarm.model)) {
        spin_unlock(&s_alarm_lock);
        return D_ERR_BUSY;
    }
    s_alarm.status.quiescing = 1U;
    attempt = s_alarm.audio_attempt;
    wake = s_alarm.status.storage_ready;
    spin_unlock(&s_alarm_lock);
    product_ui_clock_quiesce_begin();
    product_ui_alarm_audio_cancel(attempt);
    if (wake) (void)os_sem_post(&s_alarm.wake);
    return D_OK;
}

void product_ui_alarm_quiesce_begin(void)
{
    product_ui_alarm_command_t stop = {.action = PRODUCT_UI_ALARM_STOP_GROUP};
    uint32_t attempt;
    uint8_t wake;
    product_ui_clock_quiesce_begin();
    product_ui_alarm_tick(NULL);
    spin_lock(&s_alarm_lock);
    if (!s_alarm.status.quiescing) {
        stop.group_generation = s_alarm.model.group_generation;
        if (product_ui_alarm_model_ring_mask(&s_alarm.model)) {
            (void)product_ui_alarm_model_submit(&s_alarm.model, &stop, NULL);
            product_ui_alarm_next_request();
        }
        s_alarm.status.quiescing = 1U;
    }
    attempt = s_alarm.audio_attempt;
    wake = s_alarm.status.storage_ready;
    spin_unlock(&s_alarm_lock);
    product_ui_alarm_audio_cancel(attempt);
    if (wake) (void)os_sem_post(&s_alarm.wake);
}

void product_ui_alarm_quiesce_cancel(void)
{
    product_ui_clock_quiesce_cancel();
    spin_lock(&s_alarm_lock);
    s_alarm.status.quiescing = 0U;
    spin_unlock(&s_alarm_lock);
    product_ui_alarm_tick(NULL);
}

d_err_t product_ui_alarm_wait_idle(uint32_t timeout_ms)
{
    uint32_t started = product_ui_clock_port_ms();
    for (;;) {
        product_ui_alarm_snapshot_t snapshot;
        product_ui_alarm_audio_snapshot_t audio;
        product_ui_alarm_tick(NULL);
        product_ui_alarm_get_snapshot(&snapshot);
        product_ui_alarm_audio_snapshot(&audio);
        if (!snapshot.dirty && !snapshot.saving && !snapshot.stop_mask &&
            !audio.playing && !audio.pending && !audio.owned) return D_OK;
        if (snapshot.dirty && (!snapshot.storage_ready || snapshot.storage_protected)) return D_ERR_IO;
        if (snapshot.dirty && !snapshot.saving && snapshot.storage_error != D_OK) return snapshot.storage_error;
        if ((audio.owned || audio.playing) && audio.error != D_OK) return audio.error;
        if ((uint32_t)(product_ui_clock_port_ms() - started) >= timeout_ms) return D_ERR_TIMEOUT;
        os_time_dly(1);
    }
}

d_err_t product_ui_alarm_service_stop(uint32_t timeout_ms)
{
    uint32_t started = product_ui_clock_port_ms();
    uint16_t timer;
    d_err_t result;
    product_ui_alarm_quiesce_begin();
    result = product_ui_alarm_wait_idle(timeout_ms);
    if (result != D_OK) return result;
    spin_lock(&s_alarm_lock);
    timer = s_alarm.timer;
    s_alarm.timer = 0U;
    s_alarm.status.timer_ready = 0U;
    s_alarm.stop_requested = 1U;
    spin_unlock(&s_alarm_lock);
    if (timer) product_ui_alarm_timer_stop(timer);
    for (;;) {
        uint8_t stopped;
        spin_lock(&s_alarm_lock);
        stopped = !s_alarm.status.storage_ready || s_alarm.worker_stopped;
        spin_unlock(&s_alarm_lock);
        if (stopped) return D_OK;
        if ((uint32_t)(product_ui_clock_port_ms() - started) >= timeout_ms) return D_ERR_TIMEOUT;
        os_time_dly(1);
    }
}
#endif
