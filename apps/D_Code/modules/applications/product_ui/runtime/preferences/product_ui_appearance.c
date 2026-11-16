/** @file product_ui_appearance.c @brief 外观偏好的后台存储事务；保存成功须经完整记录读回确认。 */
#include "product_ui_appearance.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)
#include "product_ui_appearance_storage.h"
#include "os/os_api.h"
#include "system/includes.h"
#include <string.h>

#define PRODUCT_UI_APPEARANCE_TASK_PRIORITY 12
#define PRODUCT_UI_APPEARANCE_STACK_WORDS   1024
#define PRODUCT_UI_APPEARANCE_POLL_TICKS    10
#define PRODUCT_UI_ROTATION_FLIPPED        1U
#define PRODUCT_UI_ROTATION_OVERRIDE       2U

typedef struct {
    OS_SEM wake;
    product_ui_appearance_snapshot_t state;
    uint32_t accepted_ms;
    int task_pid;
    uint8_t is_init_attempted;
    uint8_t is_worker_started;
    uint8_t is_stop_requested;
    uint8_t is_worker_stopped;
} product_ui_appearance_context_t;

static product_ui_appearance_context_t s_appearance;

/**
 * @brief 计算记录前六字节的 CRC16，排除尾部校验字段。
 * @param[in] record 至少可读六字节的记录缓冲区。
 * @return 按多项式 0x1021、初值 0xffff 得到的 16 位校验值。
 */
static uint16_t product_ui_appearance_crc(const uint8_t *record)
{
    uint16_t crc = 0xffffU;
    uint8_t index;
    uint8_t bit;
    for (index = 0U; index < 6U; index++) {
        crc ^= (uint16_t)record[index] << 8;
        for (bit = 0U; bit < 8U; bit++) {
            crc = (uint16_t)((crc << 1) ^ ((crc & 0x8000U) ? 0x1021U : 0U));
        }
    }
    return crc;
}

/**
 * @brief 校验八字节记录的魔数、受支持版本、标志范围及 CRC。
 * @param[in] record 至少可读 PRODUCT_UI_APPEARANCE_RECORD_SIZE 字节的记录。
 * @return 全部字段合法且校验一致为非零，否则为零。
 */
static uint8_t product_ui_appearance_record_valid(const uint8_t *record)
{
    uint16_t stored_crc = (uint16_t)record[6] | ((uint16_t)record[7] << 8);
    uint8_t flags_valid = record[2] == 1U ? record[5] == 0U :
        (record[2] == 2U && (record[5] == 0U || record[5] == 2U || record[5] == 3U));
    return record[0] == 'P' && record[1] == 'U' && flags_valid &&
           record[3] < PRODUCT_UI_WALLPAPER_COUNT && record[4] <= 1U &&
           stored_crc == product_ui_appearance_crc(record);
}

/**
 * @brief 编码 V2 外观记录并追加小端 CRC16。
 * @param[out] record 至少八字节的调用方缓冲区。
 * @param[in] wallpaper 已校验的壁纸编号。
 * @param[in] reduce_motion 已校验的减少动态效果开关。
 * @param[in] rotation_flags 方向覆盖与上下翻转位组合。
 */
static void product_ui_appearance_encode(uint8_t *record, uint8_t wallpaper,
                                          uint8_t reduce_motion, uint8_t rotation_flags)
{
    uint16_t crc;
    record[0] = 'P';
    record[1] = 'U';
    record[2] = 2U;
    record[3] = wallpaper;
    record[4] = reduce_motion;
    record[5] = rotation_flags;
    crc = product_ui_appearance_crc(record);
    record[6] = (uint8_t)crc;
    record[7] = (uint8_t)(crc >> 8);
}

/**
 * @brief 推进非零状态代次，供快照消费者识别变化。
 * @note 调用方在共享状态的临界区内执行。
 */
static void product_ui_appearance_changed(void)
{
    s_appearance.state.revision++;
    if (s_appearance.state.revision == 0U) s_appearance.state.revision = 1U;
}

/**
 * @brief 写入整条记录并逐字节读回确认。
 * @param[in] wallpaper 目标壁纸编号。
 * @param[in] reduce_motion 目标减少动态效果开关。
 * @param[in] rotation_flags 目标方向标志。
 * @return 完整写入且读回相等返回 D_OK，短写、短读或内容不符返回 D_ERR_IO。
 * @note 在 worker 中执行阻塞存储，不在共享状态临界区内调用。
 */
static d_err_t product_ui_appearance_save(uint8_t wallpaper, uint8_t reduce_motion, uint8_t rotation_flags)
{
    uint8_t record[PRODUCT_UI_APPEARANCE_RECORD_SIZE];
    uint8_t verified[PRODUCT_UI_APPEARANCE_RECORD_SIZE];
    product_ui_appearance_encode(record, wallpaper, reduce_motion, rotation_flags);
    if (product_ui_appearance_storage_write(record, sizeof(record)) != (int)sizeof(record)) {
        return D_ERR_IO;
    }
    if (product_ui_appearance_storage_read(verified, sizeof(verified)) != (int)sizeof(verified) ||
        memcmp(record, verified, sizeof(record)) != 0) {
        return D_ERR_IO;
    }
    return D_OK;
}

/**
 * @brief 串行取出已受理偏好，在存储完成后发布请求结果。
 * @param[in] context 任务入口上下文；当前未使用。
 * @note 临界区只保护状态交接，存储在临界区外执行；显示确认未到达时保留待处理事务。
 */
static void product_ui_appearance_worker(void *context)
{
    (void)context;
    for (;;) {
        uint8_t has_work;
        uint8_t wallpaper;
        uint8_t reduce_motion;
        uint8_t rotation_flags;
        uint16_t rotation_degrees;
        d_err_t result;

        OS_ENTER_CRITICAL();
        has_work = s_appearance.state.is_pending && !s_appearance.state.is_rotation_waiting;
        if (!s_appearance.state.is_pending && s_appearance.is_stop_requested) {
            s_appearance.is_worker_stopped = 1U;
            s_appearance.state.is_service_ready = 0U;
            product_ui_appearance_changed();
            OS_EXIT_CRITICAL();
            return;
        }
        wallpaper = s_appearance.state.target_wallpaper_id;
        reduce_motion = s_appearance.state.target_reduce_motion;
        rotation_degrees = s_appearance.state.target_rotation_degrees;
        rotation_flags = s_appearance.state.target_rotation_override ?
            PRODUCT_UI_ROTATION_OVERRIDE | (rotation_degrees == 180U ? PRODUCT_UI_ROTATION_FLIPPED : 0U) : 0U;
        if (has_work) {
            s_appearance.state.is_pending = 0U;
            s_appearance.state.is_in_progress = 1U;
            product_ui_appearance_changed();
        }
        OS_EXIT_CRITICAL();
        if (!has_work) {
            (void)os_sem_pend(&s_appearance.wake, PRODUCT_UI_APPEARANCE_POLL_TICKS);
            continue;
        }
        (void)os_sem_accept(&s_appearance.wake);
        result = product_ui_appearance_save(wallpaper, reduce_motion, rotation_flags);
        OS_ENTER_CRITICAL();
        if (result == D_OK) {
            s_appearance.state.wallpaper_id = wallpaper;
            s_appearance.state.reduce_motion = reduce_motion;
            s_appearance.state.has_confirmed_record = 1U;
            s_appearance.state.persisted_rotation_degrees = rotation_degrees;
            s_appearance.state.record_error = D_OK;
        }
        s_appearance.state.result = result;
        s_appearance.state.completed_id = s_appearance.state.request_id;
        s_appearance.state.is_in_progress = 0U;
        product_ui_appearance_changed();
        OS_EXIT_CRITICAL();
    }
}

/**
 * @brief 读取已有外观记录并启动后台保存任务；启动过程不写 VM。
 */
d_err_t product_ui_appearance_init(void)
{
    uint8_t record[PRODUCT_UI_APPEARANCE_RECORD_SIZE];
    product_ui_appearance_snapshot_t snapshot;
    d_err_t result = D_ERR_IO;
    int record_length;
    OS_ENTER_CRITICAL();
    if (s_appearance.is_init_attempted) {
        OS_EXIT_CRITICAL();
        product_ui_appearance_get_snapshot(&snapshot);
        if (!snapshot.is_initialized) return D_ERR_BUSY;
        return snapshot.is_service_ready ? D_OK :
            (snapshot.init_error != D_OK ? snapshot.init_error : D_ERR_NOT_READY);
    }
    s_appearance.is_init_attempted = 1U;
    s_appearance.state.revision = 1U;
    s_appearance.state.init_error = D_ERR_BUSY;
    s_appearance.state.rotation_degrees = D_SCREEN_DEFAULT_ROTATION_DEGREES;
    s_appearance.state.persisted_rotation_degrees = D_SCREEN_DEFAULT_ROTATION_DEGREES;
    OS_EXIT_CRITICAL();
    record_length = product_ui_appearance_storage_read(record, sizeof(record));
    s_appearance.state.record_error = record_length <= 0 ? D_ERR_NOT_FOUND : D_ERR_IO;
    if (record_length == (int)sizeof(record) && record[0] == 'P' && record[1] == 'U' && record[2] > 2U) {
        s_appearance.state.is_record_protected = 1U;
        s_appearance.state.record_error = D_ERR_NOT_READY;
    }
    if (record_length == (int)sizeof(record) &&
        product_ui_appearance_record_valid(record)) {
        OS_ENTER_CRITICAL();
        s_appearance.state.wallpaper_id = record[3];
        s_appearance.state.reduce_motion = record[4];
        s_appearance.state.has_confirmed_record = 1U;
        s_appearance.state.record_error = D_OK;
        if (record[2] == 2U && (record[5] & PRODUCT_UI_ROTATION_OVERRIDE)) {
            s_appearance.state.has_rotation_override = 1U;
            s_appearance.state.rotation_degrees = (record[5] & PRODUCT_UI_ROTATION_FLIPPED) ? 180U : 0U;
            s_appearance.state.persisted_rotation_degrees = s_appearance.state.rotation_degrees;
        }
        OS_EXIT_CRITICAL();
    }
    if (os_sem_create(&s_appearance.wake, 0) == OS_NO_ERR &&
        thread_fork("ui_appearance", PRODUCT_UI_APPEARANCE_TASK_PRIORITY,
                    PRODUCT_UI_APPEARANCE_STACK_WORDS, 0, &s_appearance.task_pid,
                    product_ui_appearance_worker, NULL) == OS_NO_ERR) {
        result = D_OK;
    }
    OS_ENTER_CRITICAL();
    s_appearance.is_worker_started = result == D_OK;
    s_appearance.state.is_service_ready = result == D_OK;
    s_appearance.state.is_initialized = 1U;
    s_appearance.state.init_error = result;
    s_appearance.state.target_wallpaper_id = s_appearance.state.wallpaper_id;
    s_appearance.state.target_reduce_motion = s_appearance.state.reduce_motion;
    s_appearance.state.target_rotation_degrees = s_appearance.state.rotation_degrees;
    s_appearance.state.target_rotation_override = s_appearance.state.has_rotation_override;
    product_ui_appearance_changed();
    OS_EXIT_CRITICAL();
    return result;
}

/**
 * @brief 复制受临界区保护的外观状态，并按受理时间计算超时观察值。
 */
void product_ui_appearance_get_snapshot(product_ui_appearance_snapshot_t *snapshot)
{
    uint32_t accepted_ms;
    if (snapshot == NULL) return;
    OS_ENTER_CRITICAL();
    *snapshot = s_appearance.state;
    accepted_ms = s_appearance.accepted_ms;
    snapshot->is_initializing = s_appearance.is_init_attempted && !snapshot->is_initialized;
    if (!s_appearance.is_init_attempted) snapshot->init_error = D_ERR_NOT_READY;
    OS_EXIT_CRITICAL();
    snapshot->is_timed_out = (snapshot->is_pending || snapshot->is_in_progress) &&
        (uint32_t)(product_ui_appearance_clock_ms() - accepted_ms) >= PRODUCT_UI_APPEARANCE_TIMEOUT_MS;
}

/**
 * @brief 验证壁纸或动态效果目标，并原子预留单槽保存请求。
 * @param[in] value 壁纸编号或动态效果开关，含义由 is_motion 指定。
 * @param[in] is_motion 非零更新动态效果，零更新壁纸。
 * @param[out] request_id 可为 NULL；失败置零，成功写入对应请求或完成编号。
 * @return 受理或已有一致的确认记录返回 D_OK；否则返回参数、就绪或占用错误。
 * @note 静态待处理标志保留工作，丢失一次信号量通知不丢弃请求。
 */
static d_err_t product_ui_appearance_submit(uint8_t value, uint8_t is_motion,
                                             uint32_t *request_id)
{
    product_ui_appearance_snapshot_t *state = &s_appearance.state;
    uint8_t wallpaper;
    uint8_t reduce_motion;
    uint32_t accepted_ms = product_ui_appearance_clock_ms();
    if (request_id != NULL) *request_id = 0U;
    if ((is_motion && value > 1U) || (!is_motion && value >= PRODUCT_UI_WALLPAPER_COUNT)) {
        return D_ERR_INVALID_ARG;
    }
    OS_ENTER_CRITICAL();
    if (!state->is_service_ready || state->is_record_protected) {
        OS_EXIT_CRITICAL();
        return D_ERR_NOT_READY;
    }
    if (state->is_quiescing || state->is_pending || state->is_in_progress) {
        OS_EXIT_CRITICAL();
        return D_ERR_BUSY;
    }
    wallpaper = is_motion ? state->wallpaper_id : value;
    reduce_motion = is_motion ? value : state->reduce_motion;
    if (state->has_confirmed_record && state->result == D_OK &&
        wallpaper == state->wallpaper_id && reduce_motion == state->reduce_motion) {
        if (request_id != NULL) *request_id = state->completed_id;
        OS_EXIT_CRITICAL();
        return D_OK;
    }
    state->target_wallpaper_id = wallpaper;
    state->target_reduce_motion = reduce_motion;
    state->target_rotation_degrees = state->rotation_degrees;
    state->target_rotation_override = state->has_rotation_override;
    state->request_id++;
    if (state->request_id == 0U) state->request_id = 1U;
    state->is_pending = 1U;
    state->result = D_OK;
    s_appearance.accepted_ms = accepted_ms;
    product_ui_appearance_changed();
    if (request_id != NULL) *request_id = state->request_id;
    OS_EXIT_CRITICAL();
    /* 静态待处理标志保留请求，漏掉一次信号量通知不会丢弃工作。 */
    (void)os_sem_post(&s_appearance.wake);
    return D_OK;
}

/**
 * @brief 受理壁纸偏好保存请求。
 */
d_err_t product_ui_appearance_request_wallpaper(uint8_t id, uint32_t *request_id)
{
    return product_ui_appearance_submit(id, 0U, request_id);
}

/**
 * @brief 受理减少动态效果偏好的保存请求。
 */
d_err_t product_ui_appearance_request_reduce_motion(uint8_t enabled, uint32_t *request_id)
{
    return product_ui_appearance_submit(enabled, 1U, request_id);
}

/**
 * @brief 预留外观事务队列，等待匹配的显示翻转结果后再决定保存。
 */
d_err_t product_ui_appearance_request_rotation(uint16_t degrees, uint32_t *request_id)
{
    product_ui_appearance_snapshot_t *state = &s_appearance.state;
    uint32_t now = product_ui_appearance_clock_ms();
    if (request_id) *request_id = 0U;
    if (degrees != 0U && degrees != 180U) return D_ERR_INVALID_ARG;
    OS_ENTER_CRITICAL();
    if (!state->is_service_ready || state->is_record_protected) {
        OS_EXIT_CRITICAL();
        return D_ERR_NOT_READY;
    }
    if (state->is_quiescing || state->is_pending || state->is_in_progress) {
        OS_EXIT_CRITICAL();
        return D_ERR_BUSY;
    }
    state->target_wallpaper_id = state->wallpaper_id;
    state->target_reduce_motion = state->reduce_motion;
    state->target_rotation_degrees = degrees;
    state->target_rotation_override = 1U;
    if (++state->request_id == 0U) state->request_id = 1U;
    state->is_pending = 1U;
    state->is_rotation_waiting = 1U;
    state->result = D_OK;
    s_appearance.accepted_ms = now;
    product_ui_appearance_changed();
    if (request_id) *request_id = state->request_id;
    OS_EXIT_CRITICAL();
    return D_OK;
}

/**
 * @brief 接收显示翻转完成确认，成功释放后台保存，失败结束本次事务。
 */
void product_ui_appearance_rotation_applied(uint32_t request_id, d_err_t result)
{
    product_ui_appearance_snapshot_t *state = &s_appearance.state;
    OS_ENTER_CRITICAL();
    if (!request_id || request_id != state->request_id || !state->is_rotation_waiting) {
        OS_EXIT_CRITICAL();
        return;
    }
    state->is_rotation_waiting = 0U;
    if (result == D_OK) {
        state->rotation_degrees = state->target_rotation_degrees;
        state->has_rotation_override = 1U;
    } else {
        state->is_pending = 0U;
        state->result = result;
        state->completed_id = request_id;
        state->target_rotation_degrees = state->rotation_degrees;
        state->target_rotation_override = state->has_rotation_override;
    }
    product_ui_appearance_changed();
    OS_EXIT_CRITICAL();
    if (result == D_OK) (void)os_sem_post(&s_appearance.wake);
}

/**
 * @brief 关闭新外观请求入口，保留已受理事务继续完成。
 */
void product_ui_appearance_quiesce_begin(void)
{
    OS_ENTER_CRITICAL();
    if (!s_appearance.state.is_quiescing) {
        s_appearance.state.is_quiescing = 1U;
        product_ui_appearance_changed();
    }
    OS_EXIT_CRITICAL();
}

/**
 * @brief 未进入服务停止时解除维护静默，重新允许外观请求。
 */
void product_ui_appearance_quiesce_cancel(void)
{
    OS_ENTER_CRITICAL();
    if (s_appearance.state.is_quiescing && !s_appearance.is_stop_requested) {
        s_appearance.state.is_quiescing = 0U;
        product_ui_appearance_changed();
    }
    OS_EXIT_CRITICAL();
}

/**
 * @brief 阻塞等待初始化及已受理外观事务退出。
 */
d_err_t product_ui_appearance_wait_idle(uint32_t timeout_ms)
{
    uint32_t started_ms = product_ui_appearance_clock_ms();
    product_ui_appearance_snapshot_t snapshot;
    for (;;) {
        product_ui_appearance_get_snapshot(&snapshot);
        if (!snapshot.is_initializing && !snapshot.is_pending && !snapshot.is_in_progress) return D_OK;
        if ((uint32_t)(product_ui_appearance_clock_ms() - started_ms) >= timeout_ms) return D_ERR_TIMEOUT;
        os_time_dly(1);
    }
}

/**
 * @brief 先禁止新请求并排空事务，再要求后台任务退出。
 */
d_err_t product_ui_appearance_service_stop(uint32_t timeout_ms)
{
    uint32_t started_ms = product_ui_appearance_clock_ms();
    uint8_t is_stopped;
    product_ui_appearance_quiesce_begin();
    if (product_ui_appearance_wait_idle(timeout_ms) != D_OK) return D_ERR_TIMEOUT;
    OS_ENTER_CRITICAL();
    if (!s_appearance.is_worker_started) {
        OS_EXIT_CRITICAL();
        return D_OK;
    }
    s_appearance.is_stop_requested = 1U;
    OS_EXIT_CRITICAL();
    (void)os_sem_post(&s_appearance.wake);
    for (;;) {
        OS_ENTER_CRITICAL();
        is_stopped = s_appearance.is_worker_stopped;
        OS_EXIT_CRITICAL();
        if (is_stopped) return D_OK;
        if ((uint32_t)(product_ui_appearance_clock_ms() - started_ms) >= timeout_ms) return D_ERR_TIMEOUT;
        os_time_dly(1);
    }
}
#endif
