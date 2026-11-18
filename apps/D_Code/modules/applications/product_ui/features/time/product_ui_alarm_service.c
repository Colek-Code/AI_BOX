/**
 * @file product_ui_alarm_service.c
 * @brief 唯一闹钟运行时，异步存储完成确认与非阻塞音频请求分开。
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

/* 这些大缓冲仅由存储 worker 持有，不放进它的 4 KB 栈。 */
static product_ui_alarm_saved_t s_alarm_save_snapshot;
static uint8_t s_alarm_encoded[PRODUCT_UI_ALARM_RECORD_SIZE];
static uint8_t s_alarm_readback[PRODUCT_UI_ALARM_RECORD_SIZE];

#if D_FEATURE_AI_ALARM
static product_ui_alarm_receipt_t s_receipt;
static product_ui_alarm_t s_receipt_rule;
static uint32_t s_receipt_sequence;
static uint8_t s_receipt_delete;

/**
 * @brief 在持有闹钟锁时按编号、规则版本及精确规则内容检查跟踪身份。
 * @param[in] saved 待比较的活动或冻结保存值。
 * @return 编辑仍对应同一记录或跟踪删除目标确实不存在时返回非零，否则为零。
 */
static uint8_t product_ui_alarm_receipt_matches(const product_ui_alarm_saved_t *saved)
{
    uint8_t slot;
    for (slot = 0U; slot < PRODUCT_UI_ALARM_COUNT; slot++) {
        const product_ui_alarm_record_t *record = &saved->records[slot];
        if (record->rule.id != s_receipt.id) continue;
        return !s_receipt_delete && record->rule_revision == s_receipt.rule_revision &&
            memcmp(&record->rule, &s_receipt_rule, sizeof(s_receipt_rule)) == 0;
    }
    return s_receipt_delete;
}

/**
 * @brief 按实际冻结写入规则更新跟踪回执，活动记录已改变时标记取代。
 * @param[in] result 本次完整写入及读回确认结果。
 * @note 调用方持有闹钟锁；不以全局保存序号推断某条规则已保存。
 */
static void product_ui_alarm_receipt_written(d_err_t result)
{
    if (s_receipt.state != PRODUCT_UI_ALARM_RECEIPT_PENDING &&
        s_receipt.state != PRODUCT_UI_ALARM_RECEIPT_FAILED) return;
    if (product_ui_alarm_receipt_matches(&s_alarm_save_snapshot)) {
        s_receipt.state = result == D_OK ? PRODUCT_UI_ALARM_RECEIPT_SAVED : PRODUCT_UI_ALARM_RECEIPT_FAILED;
        s_receipt.error = result;
    } else if (!product_ui_alarm_receipt_matches(&s_alarm.model.saved)) {
        s_receipt.state = PRODUCT_UI_ALARM_RECEIPT_SUPERSEDED;
        s_receipt.error = D_ERR_BUSY;
    }
}
#endif

/**
 * @brief 在持有闹钟锁时推进非零保存请求编号。
 */
static void product_ui_alarm_next_request(void)
{
    if (++s_alarm.status.request_id == 0U) ++s_alarm.status.request_id;
}

/**
 * @brief 由唯一存储 worker 读取两槽、选择记录并建立模型及加载分类。
 * @note 两槽均明确NOT_FOUND才种入默认规则，已知版本不兼容独立保护。
 *       其他未取得完整记录的读取保持未加载，显式重载成功前不允许变更；
 *       完整读出但CRC/内容无效仍按原A/B降级规则处理，不自动修复写入。
 */
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
        /* 未取得完整记录不能当作已确认损坏；另一有效槽不授权覆盖它。 */
        else if (length != PRODUCT_UI_ALARM_RECORD_SIZE) { unreadable = 1U; load_error = D_ERR_IO; continue; }
        else result = product_ui_alarm_decode(s_alarm_encoded, &s_alarm_save_snapshot, &sequence);
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
    /* 没有有效记录不证明首次使用，只有两次槽读取均明确缺失才种入默认规则。 */
    if (absent_slots == 2U && product_ui_alarm_model_seed_defaults(&s_alarm.model) == D_OK)
        product_ui_alarm_next_request();
    s_alarm.status.storage_protected = protected_record;
    s_alarm.status.load_error = protected_record ? PRODUCT_UI_ALARM_ERR_VERSION : unreadable ? D_ERR_IO : has_record ? D_OK : load_error;
    s_alarm.status.loaded = !unreadable;
    s_alarm.status.storage_error = protected_record ? PRODUCT_UI_ALARM_ERR_VERSION : unreadable ? D_ERR_IO : D_OK;
    s_alarm.retry_load = 0U;
    load_error = s_alarm.status.load_error;
    spin_unlock(&s_alarm_lock);
    LOGI("ALARM_STORE", "load absent=%u valid=%u protected=%u unreadable=%u error=%d presets=%u",
        absent_slots, has_record, protected_record, unreadable, load_error, absent_slots == 2U);
}

/**
 * @brief 编码冻结保存副本到另一槽，完整写入并逐字节读回确认。
 * @param[in] slot 本次目标 A/B 槽。
 * @param[in] sequence 目标记录环形序号。
 * @return 完整写读且相同返回 D_OK，其余为 IO。
 * @note 编码及读回大缓冲仅由此 worker 静态持有，不压入任务小栈。
 */
static d_err_t product_ui_alarm_write(uint8_t slot, uint32_t sequence)
{
    product_ui_alarm_encode(&s_alarm_save_snapshot, sequence, s_alarm_encoded);
    if (product_ui_alarm_storage_write(slot, s_alarm_encoded, sizeof(s_alarm_encoded)) != PRODUCT_UI_ALARM_RECORD_SIZE ||
        product_ui_alarm_storage_read(slot, s_alarm_readback, sizeof(s_alarm_readback)) != PRODUCT_UI_ALARM_RECORD_SIZE ||
        memcmp(s_alarm_encoded, s_alarm_readback, sizeof(s_alarm_encoded)) != 0) return D_ERR_IO;
    return D_OK;
}

/**
 * @brief 串行初始加载／显式重读，冻结模型后保存并发布请求及规则观察回执。
 * @param[in] context worker 上下文，当前未使用。
 * @note IO 在闹钟锁外执行；相同失败内容代次等待显式重试，完成只确认冻结的模型代次，不覆盖活动数据。
 */
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
#if D_FEATURE_AI_ALARM
        product_ui_alarm_receipt_written(result);
#endif
        spin_unlock(&s_alarm_lock);
    }
}

/**
 * @brief 锁外取时钟及音频值，锁内推进模型，锁外按尝试代次提交声音或取消。
 * @param[in] context 独立定时器上下文，当前未使用。
 * @note 只有 event_id 和 attempt 匹配的真实音频启动才驱动 RINGING；资源全释放才驱动 audio_stopped。
 */
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

/**
 * @brief 在闹钟锁内汇聚值快照、有效记录计数和最早可用下次执行点。
 * @param[out] snapshot 调用链保证非空的输出快照。
 */
static void product_ui_alarm_snapshot_locked(product_ui_alarm_snapshot_t *snapshot)
{
    uint8_t index;
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
}

/**
 * @brief 在闹钟自旋锁内复制阶段、计数、保存及音频投影。
 */
void product_ui_alarm_get_snapshot(product_ui_alarm_snapshot_t *snapshot)
{
    if (snapshot == NULL) return;
    spin_lock(&s_alarm_lock);
    product_ui_alarm_snapshot_locked(snapshot);
    spin_unlock(&s_alarm_lock);
}

#if D_FEATURE_AI_ALARM
/**
 * @brief 在同一闹钟锁下复制状态及全部十六槽记录。
 */
void product_ui_alarm_copy_set(product_ui_alarm_snapshot_t *snapshot,
    product_ui_alarm_record_t records[PRODUCT_UI_ALARM_COUNT])
{
    if (snapshot == NULL || records == NULL) return;
    spin_lock(&s_alarm_lock);
    product_ui_alarm_snapshot_locked(snapshot);
    memcpy(records, s_alarm.model.saved.records, sizeof(s_alarm.model.saved.records));
    spin_unlock(&s_alarm_lock);
}

/**
 * @brief 读取本地跟踪回执，并把已被活动规则取代的失败操作标记为 SUPERSEDED。
 */
void product_ui_alarm_get_receipt(product_ui_alarm_receipt_t *receipt)
{
    if (receipt == NULL) return;
    spin_lock(&s_alarm_lock);
    if (s_receipt.state == PRODUCT_UI_ALARM_RECEIPT_FAILED &&
        !product_ui_alarm_receipt_matches(&s_alarm.model.saved)) {
        s_receipt.state = PRODUCT_UI_ALARM_RECEIPT_SUPERSEDED;
        s_receipt.error = D_ERR_BUSY;
    }
    *receipt = s_receipt;
    spin_unlock(&s_alarm_lock);
}

/**
 * @brief 为匹配且仍未被取代的 FAILED 跟踪操作受理一次保存重试。
 */
d_err_t product_ui_alarm_retry_tracked(uint32_t operation)
{
    spin_lock(&s_alarm_lock);
    if (!operation || operation != s_receipt.operation) {
        spin_unlock(&s_alarm_lock); return D_ERR_NOT_FOUND;
    }
    if (s_receipt.state == PRODUCT_UI_ALARM_RECEIPT_SAVED) {
        spin_unlock(&s_alarm_lock); return D_OK;
    }
    if (!product_ui_alarm_receipt_matches(&s_alarm.model.saved)) {
        s_receipt.state = PRODUCT_UI_ALARM_RECEIPT_SUPERSEDED;
        s_receipt.error = D_ERR_BUSY;
    }
    if (s_receipt.state != PRODUCT_UI_ALARM_RECEIPT_FAILED) {
        spin_unlock(&s_alarm_lock); return D_ERR_BUSY;
    }
    if (!s_alarm.status.storage_ready || s_alarm.status.storage_protected || s_alarm.status.quiescing) {
        spin_unlock(&s_alarm_lock); return D_ERR_NOT_READY;
    }
    s_receipt.state = PRODUCT_UI_ALARM_RECEIPT_PENDING;
    s_alarm.retry = 1U;
    product_ui_alarm_next_request();
    spin_unlock(&s_alarm_lock);
    (void)os_sem_post(&s_alarm.wake);
    return D_OK;
}
#endif

/**
 * @brief 按稳定非零规则编号读取一个完整记录。
 */
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

/**
 * @brief 按有效槽顺序读取逻辑下标对应的完整记录。
 */
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

/**
 * @brief 在时钟与规则版本准入后同步变更模型，登记保存及可选跟踪身份。
 * @param[in] command 本次借用的命令。
 * @param[out] request 可为空，预清零后返回当前请求编号。
 * @param[out] affected_id 可为空；receipt 非空时调用方必须同时提供有效输出。
 * @param[in] clock_revision 跟踪变更使用的预期时钟代次，零不检查。
 * @param[out] receipt 可为空；非空启用精确规则跟踪准入及受理输出。
 * @return 原样返回实际准入或模型结果。
 * @note 锁外唤醒保存及取消旧音频尝试；命令失败前的时钟结算仍可能推进实例及保存请求。
 */
static d_err_t product_ui_alarm_submit_internal(const product_ui_alarm_command_t *command,
    uint32_t *request, uint32_t *affected_id, uint32_t clock_revision, product_ui_alarm_receipt_t *receipt)
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
#if D_FEATURE_AI_ALARM
    if (receipt != NULL) {
        if (!s_alarm.status.storage_ready || !s_alarm.status.timer_ready ||
            (s_alarm.status.load_error != D_OK && s_alarm.status.load_error != D_ERR_NOT_FOUND)) {
            spin_unlock(&s_alarm_lock); return D_ERR_NOT_READY;
        }
        if (s_receipt.state == PRODUCT_UI_ALARM_RECEIPT_PENDING ||
            (s_receipt.state == PRODUCT_UI_ALARM_RECEIPT_FAILED &&
             product_ui_alarm_receipt_matches(&s_alarm.model.saved))) {
            spin_unlock(&s_alarm_lock); return D_ERR_BUSY;
        }
        if (s_alarm.status.storage_error != D_OK && s_alarm.model.revision != s_alarm.status.persisted_revision) {
            spin_unlock(&s_alarm_lock); return D_ERR_IO;
        }
        if (clock_revision && (!clock.valid || clock.revision != clock_revision)) {
            spin_unlock(&s_alarm_lock); return D_ERR_BUSY;
        }
    }
#else
    (void)clock_revision; (void)receipt;
#endif
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
#if D_FEATURE_AI_ALARM
    if (result == D_OK && receipt != NULL) {
        int slot;
        memset(&s_receipt, 0, sizeof(s_receipt));
        if (++s_receipt_sequence == 0U) ++s_receipt_sequence;
        s_receipt.operation = s_receipt_sequence;
        s_receipt.id = *affected_id;
        s_receipt.revision = s_alarm.model.revision;
        s_receipt_delete = command->action == PRODUCT_UI_ALARM_DELETE;
        slot = product_ui_alarm_model_find(&s_alarm.model, s_receipt.id);
        if (slot >= 0) {
            s_receipt_rule = s_alarm.model.saved.records[slot].rule;
            s_receipt.rule_revision = s_alarm.model.saved.records[slot].rule_revision;
        }
        s_receipt.state = PRODUCT_UI_ALARM_RECEIPT_PENDING;
        *receipt = s_receipt;
    }
#endif
    wake = s_alarm.status.storage_ready;
    cancel_attempt = group_before != s_alarm.model.group_generation ? s_alarm.audio_attempt : 0U;
    if (cancel_attempt) { s_alarm.cancel_requested = 1U; s_alarm.canceled_attempt = cancel_attempt; }
    spin_unlock(&s_alarm_lock);
    if (cancel_attempt) product_ui_alarm_audio_cancel(cancel_attempt);
    if (wake) (void)os_sem_post(&s_alarm.wake);
    return result;
}

/**
 * @brief 同步提交模型变更，变化后登记后台保存并按组变化请求音频取消。
 */
d_err_t product_ui_alarm_submit(const product_ui_alarm_command_t *command, uint32_t *request, uint32_t *affected_id)
{
    return product_ui_alarm_submit_internal(command, request, affected_id, 0U, NULL);
}

#if D_FEATURE_AI_ALARM
/**
 * @brief 在 AI 闹钟功能开启时受理一个带本地持久化观察回执的变更。
 */
d_err_t product_ui_alarm_submit_tracked(const product_ui_alarm_command_t *command,
    uint32_t clock_revision, product_ui_alarm_receipt_t *receipt)
{
    uint32_t id;
    if (command == NULL || receipt == NULL ||
        (command->action != PRODUCT_UI_ALARM_EDIT && command->action != PRODUCT_UI_ALARM_DELETE &&
         !(command->action == PRODUCT_UI_ALARM_ENABLE && command->enabled == 0U)))
        return D_ERR_INVALID_ARG;
    memset(receipt, 0, sizeof(*receipt));
    return product_ui_alarm_submit_internal(command, NULL, &id, clock_revision, receipt);
}
#endif

/**
 * @brief 显式重新加载未完成初始读取，或重试当前脏模型保存。
 */
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

/**
 * @brief 按当前组代次重试停止或允许下一轮重新提交闹铃请求。
 */
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

/**
 * @brief 受理共享提示音试听，音量必须在一至一百。
 */
d_err_t product_ui_alarm_preview(uint8_t volume)
{
    if (volume < 1U || volume > 100U) return D_ERR_INVALID_ARG;
    return product_ui_alarm_audio_preview(volume);
}

/**
 * @brief 请求取消当前试听，不等待其资源释放。
 */
void product_ui_alarm_stop_preview(void) { product_ui_alarm_audio_preview_cancel(); }
/**
 * @brief 读取试听来源是否仍待处理、播放或占有资源。
 */
d_err_t product_ui_alarm_preview_status(uint8_t *active) { return product_ui_alarm_audio_preview_status(active); }

/**
 * @brief 将闹钟错误显式映射为时间域结果。
 * @param[in] result 闹钟服务结果。
 * @return 对应成功、容量、缺失、冲突、参数或不可用结果。
 */
static product_ui_time_result_e product_ui_alarm_domain_result(d_err_t result)
{
    if (result == D_OK) return PRODUCT_UI_TIME_OK;
    if (result == D_ERR_FULL) return PRODUCT_UI_TIME_ERR_CAPACITY;
    if (result == D_ERR_NOT_FOUND) return PRODUCT_UI_TIME_ERR_NOT_FOUND;
    if (result == D_ERR_BUSY) return PRODUCT_UI_TIME_ERR_CONFLICT;
    return result == D_ERR_INVALID_ARG ? PRODUCT_UI_TIME_ERR_INVALID_ARGUMENT : PRODUCT_UI_TIME_ERR_UNAVAILABLE;
}

/**
 * @brief 按时间域逻辑下标复制闹钟规则，内部实例状态不暴露给桥接。
 * @param[in] index 有效记录逻辑下标。
 * @param[out] alarm 成功接收规则副本，失败不写。
 * @return 返回映射后的时间域结果。
 */
static product_ui_time_result_e product_ui_alarm_domain_get(uint8_t index, product_ui_alarm_t *alarm)
{
    product_ui_alarm_record_t record;
    d_err_t result;
    if (alarm == NULL) return PRODUCT_UI_TIME_ERR_INVALID_ARGUMENT;
    result = product_ui_alarm_get_at(index, &record);
    if (result == D_OK) *alarm = record.rule;
    return product_ui_alarm_domain_result(result);
}

/**
 * @brief 在同一闹钟锁下复制可选规则数组及摘要，再锁外转换下次北京时间。
 * @param[out] alarms 可为空；非空时须至少 ALARM_COUNT 项。
 * @param[out] summary 必填，接收计数、运行资格、代次及下次执行值。
 * @return 成功返回 TIME_OK，空摘要返回 INVALID_ARGUMENT。
 */
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

/**
 * @brief 把时间域闹钟命令翻译为服务命令，先读规则版本再提交以拒绝并发旧值。
 * @param[in] command 时间域的新增更新、删除或使能命令。
 * @param[out] affected_id 可为空，接收服务所涉及的规则编号。
 * @return 返回显式映射的时间域结果，非法类型或空命令为 INVALID_ARGUMENT。
 */
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

/**
 * @brief 一次性绑定时间域桥接、启动软件时钟、保存 worker 及独立调度。
 */
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
        /* 允许本次会话规则使用；未成功建立的 worker 不会覆盖 VM。 */
        s_alarm.status.loaded = 1U;
    }
    spin_unlock(&s_alarm_lock);
    return timer && clock_result == D_OK ? D_OK : D_ERR_IO;
}

/**
 * @brief 在同一锁内检查已加载、未静默且无响铃组后取得维护准入。
 */
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

/**
 * @brief 冻结时钟校正、结算实例后关闭新操作，响铃组转入停止意图。
 */
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

/**
 * @brief 解除时钟校正及闹钟静默，并立即推进一次真实状态。
 */
void product_ui_alarm_quiesce_cancel(void)
{
    product_ui_clock_quiesce_cancel();
    spin_lock(&s_alarm_lock);
    s_alarm.status.quiescing = 0U;
    spin_unlock(&s_alarm_lock);
    product_ui_alarm_tick(NULL);
}

/**
 * @brief 限时等待当前已知脏数据、STOPPING 实例及闹铃音频资源归还。
 */
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

/**
 * @brief 先静默并排空，再停止计时器并要求保存 worker 退出。
 */
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
