/**
 * @file power_ctrl.h
 * @brief Power on/off management module — 5-second long-press soft shutdown
 * @author D_Code
 * @date 2026-06-04
 */

#ifndef POWER_CTRL_H
#define POWER_CTRL_H

#include "framework/d_error.h"

#include <stdint.h>

typedef enum {
    POWER_CTRL_MODE_SOFT = 0,
    POWER_CTRL_MODE_HARD = 1,
} power_ctrl_mode_e;

typedef d_err_t (*power_ctrl_prepare_shutdown_cb_t)(void *user_data);
typedef d_err_t (*power_ctrl_shutdown_backend_cb_t)(void *user_data);

/** @brief Initialize the power control module and register the dedicated power key handler.
 * @return D_OK on success, error code on key registration failure.
 */
d_err_t power_ctrl_init(void);

/** @brief Deinitialize the power control module and unregister the power key handler. */
void power_ctrl_deinit(void);

/**
 * @brief Return nonzero while shutdown preparation or power-off is in progress.
 * @note Clears when shutdown is aborted; does not report a key merely being held.
 */
uint8_t power_ctrl_is_shutting_down(void);

/**
 * @brief Register a callback invoked before shutdown (e.g. flush state, close WSS).
 * @param callback  Returns D_OK or D_ERR_NOT_READY to allow shutdown to proceed.
 * @param user_data Opaque pointer forwarded to the callback.
 */
void power_ctrl_set_prepare_shutdown_callback(power_ctrl_prepare_shutdown_cb_t callback, void *user_data);

/**
 * @brief Register a hard-shutdown backend (reserved for future true power-off support).
 * @param callback  Hard shutdown implementation.
 * @param user_data Opaque pointer forwarded to the callback.
 */
void power_ctrl_set_hard_shutdown_callback(power_ctrl_shutdown_backend_cb_t callback, void *user_data);

/**
 * @brief Execute a shutdown in the specified mode, calling the prepare callback first.
 * @param mode  POWER_CTRL_MODE_SOFT or POWER_CTRL_MODE_HARD.
 * @return D_OK on success, error code otherwise.
 */
d_err_t power_ctrl_request_shutdown(power_ctrl_mode_e mode);

/** @brief Execute a shutdown using the compiled-in default mode (D_POWER_CTRL_DEFAULT_MODE). */
d_err_t power_ctrl_request_default_shutdown(void);

#endif
