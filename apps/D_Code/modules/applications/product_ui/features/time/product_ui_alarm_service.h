/** @file product_ui_alarm_service.h @brief Unique alarm runtime and asynchronous persistence receipts. */
#ifndef PRODUCT_UI_ALARM_SERVICE_H
#define PRODUCT_UI_ALARM_SERVICE_H
#include "product_ui_alarm_model.h"

typedef struct {
    product_ui_clock_snapshot_t clock;
    uint32_t revision;
    uint32_t group_generation;
    uint32_t request_id;
    uint32_t completed_id;
    uint32_t persisted_revision;
    uint32_t next_id;
    int64_t next_utc;
    uint16_t ring_mask;
    uint16_t stop_mask;
    d_err_t timer_error;
    d_err_t storage_error;
    d_err_t load_error;
    d_err_t audio_error;
    uint8_t loaded;
    uint8_t timer_ready;
    uint8_t storage_ready;
    uint8_t storage_protected;
    uint8_t saving;
    uint8_t dirty;
    uint8_t quiescing;
    uint8_t audio_owned;
    uint8_t audio_playing;
    uint8_t count;
    uint8_t enabled_count;
    uint8_t missed_count;
    uint8_t volume;
    uint8_t has_record;
} product_ui_alarm_snapshot_t;

d_err_t product_ui_alarm_init(void);
void product_ui_alarm_get_snapshot(product_ui_alarm_snapshot_t *snapshot);
d_err_t product_ui_alarm_get_record(uint32_t id, product_ui_alarm_record_t *record);
d_err_t product_ui_alarm_get_at(uint8_t index, product_ui_alarm_record_t *record);
d_err_t product_ui_alarm_submit(const product_ui_alarm_command_t *command, uint32_t *request, uint32_t *affected_id);
d_err_t product_ui_alarm_retry_save(uint32_t *request);
d_err_t product_ui_alarm_retry_audio(uint32_t group_generation);
d_err_t product_ui_alarm_preview(uint8_t volume);
void product_ui_alarm_stop_preview(void);
d_err_t product_ui_alarm_preview_status(uint8_t *active);
void product_ui_alarm_quiesce_begin(void);
/** Atomic maintenance admission: a ringing instance is never stopped here. */
d_err_t product_ui_alarm_maintenance_begin(void);
void product_ui_alarm_quiesce_cancel(void);
d_err_t product_ui_alarm_wait_idle(uint32_t timeout_ms);
d_err_t product_ui_alarm_service_stop(uint32_t timeout_ms);
#endif
