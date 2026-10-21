/** @file product_ui_pomodoro_service.h @brief Single runtime owner and confirmed persistence. */
#ifndef PRODUCT_UI_POMODORO_SERVICE_H
#define PRODUCT_UI_POMODORO_SERVICE_H
#include "product_ui_pomodoro_model.h"

typedef struct {
    product_ui_pomodoro_model_t model;
    uint32_t today;
    uint32_t request_id;
    uint32_t completed_id;
    uint32_t persisted_revision;
    uint32_t save_started_ms;
    d_err_t timer_error;
    d_err_t storage_error;
    d_err_t audio_error;
    /* IO/NOT_READY: writes blocked until retry reads both slots. INVALID_ARG:
     * readable but invalid records; defaults used. NOT_FOUND: proven empty. */
    d_err_t load_error;
    uint8_t timer_ready;
    uint8_t storage_ready;
    uint8_t saving;
    uint8_t quiescing;
    uint8_t has_record;
} product_ui_pomodoro_snapshot_t;

d_err_t product_ui_pomodoro_init(void);
void product_ui_pomodoro_get_snapshot(product_ui_pomodoro_snapshot_t *snapshot);
d_err_t product_ui_pomodoro_execute(product_ui_pomodoro_command_e command, uint32_t generation);
d_err_t product_ui_pomodoro_save_settings(const product_ui_pomodoro_settings_t *settings, uint32_t *request);
d_err_t product_ui_pomodoro_clear_stats(uint32_t *request);
d_err_t product_ui_pomodoro_retry_save(uint32_t *request);
void product_ui_pomodoro_acknowledge(uint32_t event_id);
void product_ui_pomodoro_quiesce_begin(void);
void product_ui_pomodoro_quiesce_cancel(void);
d_err_t product_ui_pomodoro_wait_idle(uint32_t timeout_ms);
/** Explicit host/service teardown; leaves static synchronization alive on timeout. */
d_err_t product_ui_pomodoro_service_stop(uint32_t timeout_ms);
#endif
