/** @file product_ui_appearance.c @brief Bounded, confirmed appearance saves. */
#include "product_ui_appearance.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)
#include "product_ui_appearance_storage.h"
#include "os/os_api.h"
#include "system/includes.h"
#include <string.h>

#define PRODUCT_UI_APPEARANCE_TASK_PRIORITY 12
#define PRODUCT_UI_APPEARANCE_STACK_WORDS   1024
#define PRODUCT_UI_APPEARANCE_POLL_TICKS    10

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

static uint8_t product_ui_appearance_record_valid(const uint8_t *record)
{
    uint16_t stored_crc = (uint16_t)record[6] | ((uint16_t)record[7] << 8);
    return record[0] == 'P' && record[1] == 'U' && record[2] == 1U &&
           record[3] < PRODUCT_UI_WALLPAPER_COUNT && record[4] <= 1U &&
           record[5] == 0U && stored_crc == product_ui_appearance_crc(record);
}

static void product_ui_appearance_encode(uint8_t *record, uint8_t wallpaper,
                                          uint8_t reduce_motion)
{
    uint16_t crc;
    record[0] = 'P';
    record[1] = 'U';
    record[2] = 1U;
    record[3] = wallpaper;
    record[4] = reduce_motion;
    record[5] = 0U;
    crc = product_ui_appearance_crc(record);
    record[6] = (uint8_t)crc;
    record[7] = (uint8_t)(crc >> 8);
}

static void product_ui_appearance_changed(void)
{
    s_appearance.state.revision++;
    if (s_appearance.state.revision == 0U) s_appearance.state.revision = 1U;
}

static d_err_t product_ui_appearance_save(uint8_t wallpaper, uint8_t reduce_motion)
{
    uint8_t record[PRODUCT_UI_APPEARANCE_RECORD_SIZE];
    uint8_t verified[PRODUCT_UI_APPEARANCE_RECORD_SIZE];
    product_ui_appearance_encode(record, wallpaper, reduce_motion);
    if (product_ui_appearance_storage_write(record, sizeof(record)) != (int)sizeof(record)) {
        return D_ERR_IO;
    }
    if (product_ui_appearance_storage_read(verified, sizeof(verified)) != (int)sizeof(verified) ||
        memcmp(record, verified, sizeof(record)) != 0) {
        return D_ERR_IO;
    }
    return D_OK;
}

static void product_ui_appearance_worker(void *context)
{
    (void)context;
    for (;;) {
        uint8_t has_work;
        uint8_t wallpaper;
        uint8_t reduce_motion;
        d_err_t result;

        OS_ENTER_CRITICAL();
        has_work = s_appearance.state.is_pending;
        if (!has_work && s_appearance.is_stop_requested) {
            s_appearance.is_worker_stopped = 1U;
            s_appearance.state.is_service_ready = 0U;
            product_ui_appearance_changed();
            OS_EXIT_CRITICAL();
            return;
        }
        wallpaper = s_appearance.state.target_wallpaper_id;
        reduce_motion = s_appearance.state.target_reduce_motion;
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
        result = product_ui_appearance_save(wallpaper, reduce_motion);
        OS_ENTER_CRITICAL();
        if (result == D_OK) {
            s_appearance.state.wallpaper_id = wallpaper;
            s_appearance.state.reduce_motion = reduce_motion;
            s_appearance.state.has_confirmed_record = 1U;
        }
        s_appearance.state.result = result;
        s_appearance.state.completed_id = s_appearance.state.request_id;
        s_appearance.state.is_in_progress = 0U;
        product_ui_appearance_changed();
        OS_EXIT_CRITICAL();
    }
}

d_err_t product_ui_appearance_init(void)
{
    uint8_t record[PRODUCT_UI_APPEARANCE_RECORD_SIZE];
    product_ui_appearance_snapshot_t snapshot;
    d_err_t result = D_ERR_IO;
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
    OS_EXIT_CRITICAL();
    if (product_ui_appearance_storage_read(record, sizeof(record)) == (int)sizeof(record) &&
        product_ui_appearance_record_valid(record)) {
        OS_ENTER_CRITICAL();
        s_appearance.state.wallpaper_id = record[3];
        s_appearance.state.reduce_motion = record[4];
        s_appearance.state.has_confirmed_record = 1U;
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
    product_ui_appearance_changed();
    OS_EXIT_CRITICAL();
    return result;
}

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
    if (!state->is_service_ready) {
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
    state->request_id++;
    if (state->request_id == 0U) state->request_id = 1U;
    state->is_pending = 1U;
    state->result = D_OK;
    s_appearance.accepted_ms = accepted_ms;
    product_ui_appearance_changed();
    if (request_id != NULL) *request_id = state->request_id;
    OS_EXIT_CRITICAL();
    /* The static pending job survives a missed semaphore notification. */
    (void)os_sem_post(&s_appearance.wake);
    return D_OK;
}

d_err_t product_ui_appearance_request_wallpaper(uint8_t id, uint32_t *request_id)
{
    return product_ui_appearance_submit(id, 0U, request_id);
}

d_err_t product_ui_appearance_request_reduce_motion(uint8_t enabled, uint32_t *request_id)
{
    return product_ui_appearance_submit(enabled, 1U, request_id);
}

void product_ui_appearance_quiesce_begin(void)
{
    OS_ENTER_CRITICAL();
    if (!s_appearance.state.is_quiescing) {
        s_appearance.state.is_quiescing = 1U;
        product_ui_appearance_changed();
    }
    OS_EXIT_CRITICAL();
}

void product_ui_appearance_quiesce_cancel(void)
{
    OS_ENTER_CRITICAL();
    if (s_appearance.state.is_quiescing && !s_appearance.is_stop_requested) {
        s_appearance.state.is_quiescing = 0U;
        product_ui_appearance_changed();
    }
    OS_EXIT_CRITICAL();
}

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
