/**
 * @file product_ui_service_registry.h
 * @brief Product UI service registry without LVGL dependencies.
 */

#ifndef PRODUCT_UI_SERVICE_REGISTRY_H
#define PRODUCT_UI_SERVICE_REGISTRY_H

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

#include <stdint.h>

typedef enum {
    PRODUCT_UI_SERVICE_SYSTEM,
    PRODUCT_UI_SERVICE_CONNECTION,
    PRODUCT_UI_SERVICE_MEDIA,
    PRODUCT_UI_SERVICE_STORAGE,
    PRODUCT_UI_SERVICE_TIME,
    PRODUCT_UI_SERVICE_EVENT,
    PRODUCT_UI_SERVICE_ALARM,
    PRODUCT_UI_SERVICE_POMODORO,
    PRODUCT_UI_SERVICE_SETTINGS,
    PRODUCT_UI_SERVICE_COUNT,
} product_ui_service_id_e;

typedef enum {
    PRODUCT_UI_SERVICE_UNKNOWN,
    PRODUCT_UI_SERVICE_UNAVAILABLE,
    PRODUCT_UI_SERVICE_READY,
    PRODUCT_UI_SERVICE_DEGRADED,
    PRODUCT_UI_SERVICE_ERROR,
} product_ui_service_state_e;

typedef struct {
    product_ui_service_state_e state;
    uint32_t valid_mask;
    uint32_t revision;
    int32_t error_code;
} product_ui_service_snapshot_t;

typedef int (*product_ui_service_command_cb_t)(uint16_t command,
                                               const void *payload,
                                               uint16_t payload_size,
                                               void *user_data);

/* PRODUCT_UI_SERVICE_MEDIA commands (named; no bare 0/1/2 at call sites). */
typedef enum {
    PRODUCT_UI_MEDIA_CMD_PREVIOUS = 0,
    PRODUCT_UI_MEDIA_CMD_TOGGLE_PLAY_PAUSE,
    PRODUCT_UI_MEDIA_CMD_NEXT,
} product_ui_media_cmd_e;

/* PRODUCT_UI_SERVICE_SYSTEM (named; unavailable until registered). */
typedef enum {
    PRODUCT_UI_SYSTEM_CMD_TOGGLE_ROTATION = 0,
} product_ui_system_cmd_e;

/*
 * Service snapshot valid_mask bits (runtime flags, not LVGL).
 * Capability bits and state bits are separate:
 * - CAP_* enables the control when the service is READY + callback registered
 * - STATE_* drives active/selected chrome only
 * Media: CAP_TRANSPORT may stay set while paused; STATE_PLAYING only while playing.
 */
#define PRODUCT_UI_MEDIA_CAP_TRANSPORT          (1U << 0)
#define PRODUCT_UI_MEDIA_STATE_PLAYING          (1U << 1)
#define PRODUCT_UI_ALARM_CAP_OPEN               (1U << 0)
#define PRODUCT_UI_ALARM_STATE_ACTIVE           (1U << 1)
#define PRODUCT_UI_SYSTEM_CAP_ROTATION          (1U << 0)
#define PRODUCT_UI_SYSTEM_STATE_ROTATION_ON     (1U << 1)

int product_ui_service_registry_init(void);
int product_ui_service_publish(product_ui_service_id_e service,
                               product_ui_service_state_e state,
                               uint32_t valid_mask, int32_t error_code);
int product_ui_service_data_changed(product_ui_service_id_e service);
int product_ui_service_snapshot(product_ui_service_id_e service,
                                product_ui_service_snapshot_t *snapshot);
uint8_t product_ui_service_is_ready(product_ui_service_id_e service);
uint8_t product_ui_service_command_available(product_ui_service_id_e service);
int product_ui_service_register_command(product_ui_service_id_e service,
                                        product_ui_service_command_cb_t callback,
                                        void *user_data);
int product_ui_service_command(product_ui_service_id_e service,
                               uint16_t command, const void *payload,
                               uint16_t payload_size);

#endif
#endif /* PRODUCT_UI_SERVICE_REGISTRY_H */
