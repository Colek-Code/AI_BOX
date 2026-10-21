/** @file product_ui_alarm_port.h @brief VM, scheduler and shared sound boundaries. */
#ifndef PRODUCT_UI_ALARM_PORT_H
#define PRODUCT_UI_ALARM_PORT_H
#include "framework/d_error.h"
#include <stdint.h>
#define PRODUCT_UI_ALARM_VM_A 29U
#define PRODUCT_UI_ALARM_VM_B 30U

typedef struct {
    uint32_t event_id;
    uint32_t attempt;
    d_err_t error;
    uint8_t ready;
    uint8_t pending;
    uint8_t playing;
    uint8_t owned;
} product_ui_alarm_audio_snapshot_t;

int product_ui_alarm_storage_read(uint8_t slot, uint8_t *record, uint16_t size);
int product_ui_alarm_storage_write(uint8_t slot, const uint8_t *record, uint16_t size);
uint16_t product_ui_alarm_timer_start(void (*callback)(void *));
void product_ui_alarm_timer_stop(uint16_t timer);
void product_ui_alarm_audio_snapshot(product_ui_alarm_audio_snapshot_t *snapshot);
d_err_t product_ui_alarm_audio_request(uint32_t event_id, uint32_t attempt, uint8_t volume);
/** Reject late submissions through this attempt as well as stopping an active one. */
void product_ui_alarm_audio_cancel(uint32_t through_attempt);
d_err_t product_ui_alarm_audio_preview(uint8_t volume);
void product_ui_alarm_audio_preview_cancel(void);
d_err_t product_ui_alarm_audio_preview_status(uint8_t *active);
#endif
