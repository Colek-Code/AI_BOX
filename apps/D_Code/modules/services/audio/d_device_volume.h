/**
 * @file d_device_volume.h
 * @brief Unified device playback volume control.
 */
#ifndef __D_DEVICE_VOLUME_H__
#define __D_DEVICE_VOLUME_H__

#include "framework/d_error.h"

#include <stdint.h>

#define D_DEVICE_VOLUME_MIN      0U
#define D_DEVICE_VOLUME_MAX      100U
#define D_DEVICE_VOLUME_TIMEOUT_MS 2000U

typedef struct {
    uint32_t revision;
    uint32_t request_id;
    uint32_t completed_id;
    uint32_t superseded_id;
    uint32_t backend_generation;
    d_err_t service_error;
    d_err_t result;
    uint8_t volume_percent;
    uint8_t target_percent;
    uint8_t restore_percent;
    uint8_t is_service_ready;
    uint8_t is_backend_ready;
    uint8_t is_pending;
    uint8_t is_in_progress;
    uint8_t is_timed_out;
    uint8_t is_confirmed;
    uint8_t is_quiescing;
    uint8_t can_request;
} d_device_volume_snapshot_t;

typedef d_err_t (*d_device_volume_backend_set_fn_t)(uint8_t volume_percent,
                                                     void *context);

typedef struct {
    d_device_volume_backend_set_fn_t set_volume;
    void *context;
} d_device_volume_backend_t;

typedef struct {
    uint32_t token;
    uint32_t revision;
    uint32_t completed_revision;
    d_err_t error;
    uint8_t ready;
    uint8_t requested;
    uint8_t held;
    uint8_t busy;
    uint8_t percent;
} d_device_volume_alert_snapshot_t;

/** Static-lifetime alert backend; callbacks run only on the existing volume worker. */
d_err_t d_device_volume_register_alert_backend(const d_device_volume_backend_t *backend);
/** Nonblocking lease: media writes wait until the alert has stopped and released it. */
d_err_t d_device_volume_alert_begin(uint32_t token, uint8_t percent);
d_err_t d_device_volume_alert_end(uint32_t token);
void d_device_volume_get_alert_snapshot(d_device_volume_alert_snapshot_t *snapshot);

d_err_t d_device_volume_init(void);

/** Start the optional persistent worker without resetting the cached setting. */
d_err_t d_device_volume_service_init(void);
/** Acceptance may replace pending work; only completed_id proves execution. */
d_err_t d_device_volume_request_set(uint8_t percent, uint32_t *request_id);
d_err_t d_device_volume_request_adjust(int8_t step, uint32_t *request_id);
d_err_t d_device_volume_request_mute(uint8_t is_mute, uint32_t *request_id);
/** Bounded snapshot, including deadlines while the backend is blocked. */
void d_device_volume_get_snapshot(d_device_volume_snapshot_t *snapshot);
/** Reject new requests; already accepted work continues. */
void d_device_volume_quiesce_begin(void);
void d_device_volume_quiesce_cancel(void);
/** Background/shutdown only; a failed but returned operation is already idle. */
d_err_t d_device_volume_wait_idle(uint32_t timeout_ms);

/**
 * @brief Register the single active playback-volume backend.
 *
 * Re-registering the same callback/context pair is idempotent and resynchronizes
 * the current volume. A different backend is rejected with D_ERR_BUSY.
 * @param backend Backend descriptor copied by the service; its callback and
 *                context must remain valid until the backend is unregistered.
 * @return D_OK on registration and synchronization success, otherwise d_err_t.
 */
d_err_t d_device_volume_register_backend(const d_device_volume_backend_t *backend);

/**
 * @brief Unregister the matching playback-volume backend.
 *
 * Releasing an already-empty backend slot is idempotent. A different active
 * callback/context pair is never removed. The caller must retry D_ERR_BUSY
 * before destroying resources used by the backend callback.
 * @param backend Expected callback/context pair.
 * @return D_OK when detached or already empty, D_ERR_NOT_FOUND for a different
 *         active backend, D_ERR_BUSY while an update callback is in progress,
 *         otherwise D_ERR_INVALID_ARG.
 */
d_err_t d_device_volume_unregister_backend(const d_device_volume_backend_t *backend);
uint8_t d_device_volume_get(void);
d_err_t d_device_volume_set(uint8_t volume_percent);
d_err_t d_device_volume_adjust(int8_t step_percent);
d_err_t d_device_volume_set_mute(uint8_t is_mute);

#endif /* __D_DEVICE_VOLUME_H__ */
