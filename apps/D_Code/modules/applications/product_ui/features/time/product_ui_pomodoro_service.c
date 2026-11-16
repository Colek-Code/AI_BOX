/** @file product_ui_pomodoro_service.c @brief 独立计时及保存所有者；持久化完成确认不覆盖仍在更新的活动数据。 */
#include "config/app_config.h"
#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)
#include "product_ui_pomodoro_service.h"
#include "product_ui_pomodoro_port.h"
#include "modules/services/audio/d_local_prompt.h"
#include "os/os_api.h"
#include "system/includes.h"
#include <string.h>

typedef struct {
    product_ui_pomodoro_snapshot_t state;
    OS_SEM wake;
    uint32_t sequence;
    uint32_t failed_revision;
    int worker_pid;
    uint16_t timer;
    uint8_t active_slot;
    uint8_t attempted;
    uint8_t initialized;
    uint8_t retry;
    uint8_t stop_requested;
    uint8_t worker_stopped;
    uint8_t load_blocked;
    uint8_t settings_changed;
    uint8_t stats_cleared;
} product_ui_pomodoro_context_t;
static product_ui_pomodoro_context_t s_pomodoro;

typedef struct {
    product_ui_pomodoro_saved_t saved;
    uint32_t sequence;
    d_err_t error;
    uint8_t slot;
    uint8_t has_record;
    uint8_t blocked;
} product_ui_pomodoro_load_t;

/**
 * @brief 读取两槽并选择最新有效记录，任一不可判读或较新版本均保护整个初始加载。
 * @param[out] loaded 接收记录、序号、槽位及加载错误分类。
 * @note 可读旧槽不能证明另一个不可读槽无新数据；保护时以会话数据启动，留给显式重试合并。
 */
static void product_ui_pomodoro_read_saved(product_ui_pomodoro_load_t *loaded)
{
    product_ui_pomodoro_saved_t candidate;
    uint8_t record[PRODUCT_UI_POMODORO_RECORD_SIZE];
    uint32_t sequence;
    uint8_t slot, damaged = 0U;
    memset(loaded, 0, sizeof(*loaded));
    loaded->error = D_ERR_NOT_FOUND;
    for (slot = 0U; slot < 2U; slot++) {
        int result = product_ui_pomodoro_storage_read(slot, record, sizeof(record));
        if (result != (int)sizeof(record)) {
            if (result != D_ERR_NOT_FOUND) {
                loaded->blocked = 1U;
                loaded->error = result == D_ERR_NOT_READY ? D_ERR_NOT_READY : D_ERR_IO;
            }
            continue;
        }
        /* 本固件不能用默认值覆盖较新的记录格式。 */
        if (memcmp(record, "POMO", 4U) == 0 && (record[4] != 1U || record[5] != 0U)) {
            loaded->blocked = 1U;
            loaded->error = D_ERR_NOT_READY;
            continue;
        }
        if (product_ui_pomodoro_decode(record, &candidate, &sequence) != D_OK) {
            damaged = 1U;
            continue;
        }
        if (!loaded->has_record || product_ui_pomodoro_sequence_after(sequence, loaded->sequence)) {
            loaded->saved = candidate;
            loaded->sequence = sequence;
            loaded->slot = slot;
            loaded->has_record = 1U;
        }
    }
    /* 可读旧槽不能证明不可读槽中没有新数据；先使用会话数据，
     * 以便显式重试后合并且不重复计数。 */
    if (loaded->blocked) loaded->has_record = 0U;
    else loaded->error = loaded->has_record ? D_OK : damaged ? D_ERR_INVALID_ARG : D_ERR_NOT_FOUND;
}

/**
 * @brief 在持有服务临界区时推进非零保存请求编号。
 */
static void product_ui_pomodoro_dirty_locked(void)
{
    s_pomodoro.state.request_id++;
    if (s_pomodoro.state.request_id == 0U) s_pomodoro.state.request_id = 1U;
}

/**
 * @brief 独立结算计时及统计，在临界区外按新事件提交或取消提示音。
 * @param[in] context 计时器上下文，当前未使用。
 */
static void product_ui_pomodoro_tick(void *context)
{
    uint32_t date = product_ui_pomodoro_today();
    uint32_t previous_event;
    uint32_t previous_saved;
    uint32_t event_id = 0U;
    uint32_t event_ms = 0U;
    uint8_t sound = 0U;
    (void)context;
    OS_ENTER_CRITICAL();
    if (!s_pomodoro.initialized || s_pomodoro.state.quiescing) { OS_EXIT_CRITICAL(); return; }
    previous_event = s_pomodoro.state.model.event_id;
    previous_saved = s_pomodoro.state.model.saved_revision;
    s_pomodoro.state.today = date;
    product_ui_pomodoro_model_advance(&s_pomodoro.state.model, product_ui_pomodoro_clock_ms(), date);
    if (previous_saved != s_pomodoro.state.model.saved_revision) product_ui_pomodoro_dirty_locked();
    if (previous_event != s_pomodoro.state.model.event_id) {
        event_id = s_pomodoro.state.model.event_id;
        event_ms = s_pomodoro.state.model.event_ms;
        sound = s_pomodoro.state.model.saved.settings.sound;
    }
    OS_EXIT_CRITICAL();
    if (event_id != 0U) {
        if (sound) d_local_prompt_request(event_id, event_ms);
        else d_local_prompt_cancel(event_id);
    }
}

/**
 * @brief 编码到另一保存槽，完整写入并逐字节读回确认。
 * @param[in] saved 本次冻结的保存值副本。
 * @param[in] slot 目标 A/B 槽。
 * @param[in] sequence 本次记录序号。
 * @return 完整写入且读回一致为 D_OK，短写、短读或内容不符为 IO。
 */
static d_err_t product_ui_pomodoro_write(const product_ui_pomodoro_saved_t *saved,
                                         uint8_t slot, uint32_t sequence)
{
    uint8_t record[PRODUCT_UI_POMODORO_RECORD_SIZE];
    uint8_t verified[PRODUCT_UI_POMODORO_RECORD_SIZE];
    product_ui_pomodoro_encode(saved, sequence, record);
    if (product_ui_pomodoro_storage_write(slot, record, sizeof(record)) != (int)sizeof(record) ||
        product_ui_pomodoro_storage_read(slot, verified, sizeof(verified)) != (int)sizeof(verified) ||
        memcmp(record, verified, sizeof(record)) != 0) return D_ERR_IO;
    return D_OK;
}

/**
 * @brief 锁外重新读取初始记录，锁内只合并一次并保留本次设置／清空意图。
 * @param[in] request 本次重试读取所属的请求编号。
 * @note 保存关闭声音的恢复设置也在锁外执行取消，不将 IO 完成覆盖为旧活动阶段。
 */
static void product_ui_pomodoro_recover_load(uint32_t request)
{
    product_ui_pomodoro_load_t loaded;
    uint8_t session_dirty;
    uint8_t cancel_sound = 0U;
    uint32_t event_id = 0U;
    product_ui_pomodoro_read_saved(&loaded);
    OS_ENTER_CRITICAL();
    session_dirty = s_pomodoro.state.persisted_revision != s_pomodoro.state.model.saved_revision;
    if (!loaded.blocked) {
        if (loaded.has_record) {
            product_ui_pomodoro_model_recover(&s_pomodoro.state.model, &loaded.saved,
                s_pomodoro.settings_changed, s_pomodoro.stats_cleared);
        }
        s_pomodoro.sequence = loaded.has_record ? loaded.sequence : 0U;
        s_pomodoro.active_slot = loaded.has_record ? loaded.slot : 0U;
        s_pomodoro.state.has_record = loaded.has_record;
        s_pomodoro.load_blocked = 0U;
        s_pomodoro.settings_changed = 0U;
        s_pomodoro.stats_cleared = 0U;
        cancel_sound = !s_pomodoro.state.model.saved.settings.sound;
        event_id = s_pomodoro.state.model.event_id;
        if (!session_dirty) s_pomodoro.state.persisted_revision = s_pomodoro.state.model.saved_revision;
    }
    s_pomodoro.state.load_error = loaded.error;
    s_pomodoro.state.storage_error = loaded.blocked ? loaded.error : D_OK;
    if (loaded.blocked || !session_dirty) s_pomodoro.state.completed_id = request;
    s_pomodoro.state.saving = 0U;
    OS_EXIT_CRITICAL();
    /* 恢复已保存的关闭声音设置也遵循显式保存时的取消合同；
     * 始终在服务临界区外操作音频所有者。 */
    if (cancel_sound && event_id != 0U) d_local_prompt_cancel(event_id);
}

/**
 * @brief 串行恢复初始加载或冻结保存副本，按请求编号确认完成。
 * @param[in] context 任务上下文，当前未使用。
 * @note IO 在临界区外执行；保存成功只确认相应 saved_revision，不覆盖同时更新的活动数据。相同失败代次等显式重试。
 */
static void product_ui_pomodoro_save_worker(void *context)
{
    (void)context;
    for (;;) {
        product_ui_pomodoro_saved_t saved;
        uint32_t revision;
        uint32_t request;
        uint8_t has_work;
        d_err_t result;
        OS_ENTER_CRITICAL();
        if (s_pomodoro.initialized && s_pomodoro.load_blocked && s_pomodoro.retry) {
            request = s_pomodoro.state.request_id;
            s_pomodoro.retry = 0U;
            s_pomodoro.state.saving = 1U;
            s_pomodoro.state.save_started_ms = product_ui_pomodoro_clock_ms();
            OS_EXIT_CRITICAL();
            product_ui_pomodoro_recover_load(request);
            continue;
        }
        revision = s_pomodoro.state.model.saved_revision;
        has_work = s_pomodoro.initialized && !s_pomodoro.load_blocked &&
            revision != s_pomodoro.state.persisted_revision &&
            (s_pomodoro.failed_revision != revision || s_pomodoro.retry);
        if (!has_work && s_pomodoro.stop_requested) {
            s_pomodoro.worker_stopped = 1U;
            s_pomodoro.state.storage_ready = 0U;
            OS_EXIT_CRITICAL();
            return;
        }
        request = s_pomodoro.state.request_id;
        if (has_work) {
            saved = s_pomodoro.state.model.saved;
            s_pomodoro.state.saving = 1U;
            s_pomodoro.state.save_started_ms = product_ui_pomodoro_clock_ms();
            s_pomodoro.retry = 0U;
        }
        OS_EXIT_CRITICAL();
        if (!has_work) { (void)os_sem_pend(&s_pomodoro.wake, 10); continue; }
        (void)os_sem_accept(&s_pomodoro.wake);
        result = product_ui_pomodoro_write(&saved, (uint8_t)(1U - s_pomodoro.active_slot), s_pomodoro.sequence + 1U);
        OS_ENTER_CRITICAL();
        if (result == D_OK) {
            s_pomodoro.active_slot = (uint8_t)(1U - s_pomodoro.active_slot);
            s_pomodoro.sequence++;
            s_pomodoro.state.persisted_revision = revision;
            s_pomodoro.state.has_record = 1U;
            s_pomodoro.state.load_error = D_OK;
            s_pomodoro.failed_revision = 0U;
        } else {
            s_pomodoro.failed_revision = revision;
        }
        s_pomodoro.state.completed_id = request;
        s_pomodoro.state.storage_error = result;
        s_pomodoro.state.saving = 0U;
        OS_EXIT_CRITICAL();
    }
}

/**
 * @brief 初始化前读取 A/B 记录及保护状态，建立不触发默认写入的活动模型。
 */
static void product_ui_pomodoro_load(void)
{
    product_ui_pomodoro_load_t loaded;
    product_ui_pomodoro_read_saved(&loaded);
    s_pomodoro.load_blocked = loaded.blocked;
    s_pomodoro.state.has_record = loaded.has_record;
    s_pomodoro.sequence = loaded.has_record ? loaded.sequence : 0U;
    s_pomodoro.active_slot = loaded.has_record ? loaded.slot : 0U;
    product_ui_pomodoro_model_init(&s_pomodoro.state.model, loaded.has_record ? &loaded.saved : NULL);
    s_pomodoro.state.load_error = loaded.error;
    /* 空记录／默认值启动不是写入请求。 */
    s_pomodoro.state.persisted_revision = s_pomodoro.state.model.saved_revision;
}

/**
 * @brief 一次性读取 A/B 记录，建立后台保存及独立计时。
 */
d_err_t product_ui_pomodoro_init(void)
{
    OS_ENTER_CRITICAL();
    if (s_pomodoro.attempted) {
        d_err_t result = s_pomodoro.initialized ? s_pomodoro.state.timer_error : D_ERR_BUSY;
        OS_EXIT_CRITICAL();
        return result;
    }
    s_pomodoro.attempted = 1U;
    OS_EXIT_CRITICAL();
    product_ui_pomodoro_load();
    s_pomodoro.state.storage_error = D_ERR_IO;
    if (os_sem_create(&s_pomodoro.wake, 0) == OS_NO_ERR &&
        thread_fork("pomo_store", 12, 1024, 0, &s_pomodoro.worker_pid,
                    product_ui_pomodoro_save_worker, NULL) == OS_NO_ERR) {
        s_pomodoro.state.storage_ready = 1U;
        s_pomodoro.state.storage_error = s_pomodoro.load_blocked ? s_pomodoro.state.load_error : D_OK;
    }
    s_pomodoro.timer = product_ui_pomodoro_timer_start(product_ui_pomodoro_tick);
    OS_ENTER_CRITICAL();
    s_pomodoro.state.timer_ready = s_pomodoro.timer != 0U;
    s_pomodoro.state.timer_error = s_pomodoro.timer != 0U ? D_OK : D_ERR_IO;
    s_pomodoro.initialized = 1U;
    OS_EXIT_CRITICAL();
    return s_pomodoro.state.timer_error;
}

/**
 * @brief 复制服务值快照并附带共享提示音当前错误。
 */
void product_ui_pomodoro_get_snapshot(product_ui_pomodoro_snapshot_t *snapshot)
{
    d_local_prompt_snapshot_t audio;
    if (snapshot == NULL) return;
    d_local_prompt_get_snapshot(&audio);
    OS_ENTER_CRITICAL();
    *snapshot = s_pomodoro.state;
    OS_EXIT_CRITICAL();
    snapshot->audio_error = audio.ready ? audio.error : D_ERR_NOT_READY;
}

/**
 * @brief 先结算单调时间，再按操作代次执行计时命令。
 */
d_err_t product_ui_pomodoro_execute(product_ui_pomodoro_command_e command, uint32_t generation)
{
    d_err_t result;
    uint32_t canceled;
    product_ui_pomodoro_tick(NULL);
    OS_ENTER_CRITICAL();
    if (!s_pomodoro.initialized || !s_pomodoro.state.timer_ready || s_pomodoro.state.quiescing) {
        OS_EXIT_CRITICAL(); return D_ERR_NOT_READY;
    }
    result = product_ui_pomodoro_model_command(&s_pomodoro.state.model, command,
                                               generation, product_ui_pomodoro_clock_ms());
    canceled = s_pomodoro.state.model.acknowledged_event;
    OS_EXIT_CRITICAL();
    if (result == D_OK && (command == PRODUCT_UI_POMODORO_SKIP || command == PRODUCT_UI_POMODORO_END))
        d_local_prompt_cancel(canceled);
    return result;
}

/**
 * @brief 在服务临界区内登记保存请求，再报告存储或初始加载准入。
 * @param[out] request 可为空，接收已推进的请求编号，即使之后报告不可保存。
 * @return 可保存为 D_OK，无 worker 为 NOT_READY，加载保护时返回 load_error。
 */
static d_err_t product_ui_pomodoro_request_locked(uint32_t *request)
{
    product_ui_pomodoro_dirty_locked();
    if (request != NULL) *request = s_pomodoro.state.request_id;
    if (!s_pomodoro.state.storage_ready) return D_ERR_NOT_READY;
    return s_pomodoro.load_blocked ? s_pomodoro.state.load_error : D_OK;
}

/**
 * @brief 立即更新活动设置并生成后台保存请求。
 */
d_err_t product_ui_pomodoro_save_settings(const product_ui_pomodoro_settings_t *settings, uint32_t *request)
{
    d_err_t result;
    uint32_t event;
    if (request != NULL) *request = 0U;
    if (!product_ui_pomodoro_settings_valid(settings)) return D_ERR_INVALID_ARG;
    OS_ENTER_CRITICAL();
    if (!s_pomodoro.initialized || s_pomodoro.state.quiescing) { OS_EXIT_CRITICAL(); return D_ERR_NOT_READY; }
    result = product_ui_pomodoro_model_settings(&s_pomodoro.state.model, settings);
    if (result == D_OK && s_pomodoro.load_blocked) s_pomodoro.settings_changed = 1U;
    if (result == D_OK) result = product_ui_pomodoro_request_locked(request);
    event = s_pomodoro.state.model.event_id;
    OS_EXIT_CRITICAL();
    if (!settings->sound) d_local_prompt_cancel(event);
    return result;
}

/**
 * @brief 立即清空活动统计并生成保存请求，不改变当前阶段。
 */
d_err_t product_ui_pomodoro_clear_stats(uint32_t *request)
{
    d_err_t result;
    if (request != NULL) *request = 0U;
    product_ui_pomodoro_tick(NULL);
    OS_ENTER_CRITICAL();
    if (!s_pomodoro.initialized || s_pomodoro.state.quiescing) { OS_EXIT_CRITICAL(); return D_ERR_NOT_READY; }
    product_ui_pomodoro_model_clear_stats(&s_pomodoro.state.model);
    if (s_pomodoro.load_blocked) s_pomodoro.stats_cleared = 1U;
    result = product_ui_pomodoro_request_locked(request);
    OS_EXIT_CRITICAL();
    return result;
}

/**
 * @brief 显式重试受保护的首次读取或失败保存，同步返回受理情况。
 */
d_err_t product_ui_pomodoro_retry_save(uint32_t *request)
{
    d_err_t result;
    if (request != NULL) *request = 0U;
    OS_ENTER_CRITICAL();
    if (!s_pomodoro.initialized || s_pomodoro.state.quiescing) { OS_EXIT_CRITICAL(); return D_ERR_NOT_READY; }
    if (s_pomodoro.state.saving || (!s_pomodoro.load_blocked &&
        s_pomodoro.state.persisted_revision == s_pomodoro.state.model.saved_revision)) {
        if (request != NULL) *request = s_pomodoro.state.request_id;
        OS_EXIT_CRITICAL();
        return D_OK;
    }
    s_pomodoro.retry = 1U;
    result = product_ui_pomodoro_request_locked(request);
    if (s_pomodoro.state.storage_ready) result = D_OK; /* 只确认重试受理，尚未恢复或保存。 */
    OS_EXIT_CRITICAL();
    return result;
}

/**
 * @brief 匹配当前事件时确认其提示，并取消不晚于指定事件的声音。
 */
void product_ui_pomodoro_acknowledge(uint32_t event_id)
{
    OS_ENTER_CRITICAL();
    if (event_id == s_pomodoro.state.model.event_id)
        s_pomodoro.state.model.acknowledged_event = event_id;
    OS_EXIT_CRITICAL();
    if (event_id != 0U) d_local_prompt_cancel(event_id);
}

/**
 * @brief 先结算当前计时，再暂停运行阶段、关闭新操作并静默共享提示音。
 */
void product_ui_pomodoro_quiesce_begin(void)
{
    product_ui_pomodoro_tick(NULL);
    OS_ENTER_CRITICAL();
    s_pomodoro.state.quiescing = 1U;
    if (s_pomodoro.state.model.state == PRODUCT_UI_POMODORO_RUNNING)
        (void)product_ui_pomodoro_model_command(&s_pomodoro.state.model, PRODUCT_UI_POMODORO_PAUSE,
            s_pomodoro.state.model.generation, product_ui_pomodoro_clock_ms());
    OS_EXIT_CRITICAL();
    d_local_prompt_quiesce_begin();
}

/**
 * @brief 解除业务及提示音静默，已暂停阶段仍需用户显式继续。
 */
void product_ui_pomodoro_quiesce_cancel(void)
{
    OS_ENTER_CRITICAL();
    s_pomodoro.state.quiescing = 0U;
    OS_EXIT_CRITICAL();
    d_local_prompt_quiesce_cancel();
}

/**
 * @brief 限时等待脏保存数据及在途 IO 收敛。
 */
d_err_t product_ui_pomodoro_wait_idle(uint32_t timeout_ms)
{
    uint32_t start = product_ui_pomodoro_clock_ms();
    for (;;) {
        uint8_t dirty;
        uint8_t saving;
        d_err_t error;
        OS_ENTER_CRITICAL();
        dirty = s_pomodoro.state.persisted_revision != s_pomodoro.state.model.saved_revision;
        saving = s_pomodoro.state.saving;
        error = s_pomodoro.state.storage_error;
        OS_EXIT_CRITICAL();
        if (!dirty && !saving) return D_OK;
        if (!saving && error != D_OK) return error;
        if ((uint32_t)(product_ui_pomodoro_clock_ms() - start) >= timeout_ms) return D_ERR_TIMEOUT;
        os_time_dly(1);
    }
}

/**
 * @brief 静默并停止计时器，要求保存 worker 在工作结束后退出。
 */
d_err_t product_ui_pomodoro_service_stop(uint32_t timeout_ms)
{
    uint32_t start = product_ui_pomodoro_clock_ms();
    product_ui_pomodoro_quiesce_begin();
    product_ui_pomodoro_timer_stop(s_pomodoro.timer);
    s_pomodoro.timer = 0U;
    OS_ENTER_CRITICAL();
    s_pomodoro.state.timer_ready = 0U;
    s_pomodoro.stop_requested = 1U;
    OS_EXIT_CRITICAL();
    for (;;) {
        uint8_t done;
        OS_ENTER_CRITICAL();
        done = !s_pomodoro.state.storage_ready || s_pomodoro.worker_stopped;
        OS_EXIT_CRITICAL();
        if (done) return product_ui_pomodoro_wait_idle(0U);
        if ((uint32_t)(product_ui_pomodoro_clock_ms() - start) >= timeout_ms) return D_ERR_TIMEOUT;
        os_time_dly(1);
    }
}
#endif
