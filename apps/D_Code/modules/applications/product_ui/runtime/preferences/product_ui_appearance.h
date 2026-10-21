/** @file product_ui_appearance.h @brief Persistent Product UI appearance transactions. */
#ifndef PRODUCT_UI_APPEARANCE_H
#define PRODUCT_UI_APPEARANCE_H

#include "config/app_config.h"
#include "framework/d_error.h"
#include <stdint.h>

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)
#define PRODUCT_UI_APPEARANCE_VM_ID       26U
#define PRODUCT_UI_APPEARANCE_RECORD_SIZE 8U
#define PRODUCT_UI_APPEARANCE_TIMEOUT_MS  2000U
#define PRODUCT_UI_WALLPAPER_COUNT        3U

typedef struct {
    uint32_t revision;
    uint32_t request_id;
    uint32_t completed_id;
    d_err_t result;
    d_err_t init_error;
    uint8_t wallpaper_id;
    uint8_t reduce_motion;
    uint8_t target_wallpaper_id;
    uint8_t target_reduce_motion;
    uint8_t is_initialized;
    uint8_t is_initializing;
    uint8_t is_service_ready;
    uint8_t has_confirmed_record;
    uint8_t is_pending;
    uint8_t is_in_progress;
    uint8_t is_quiescing;
    uint8_t is_timed_out;
} product_ui_appearance_snapshot_t;

d_err_t product_ui_appearance_init(void);
void product_ui_appearance_get_snapshot(product_ui_appearance_snapshot_t *snapshot);
d_err_t product_ui_appearance_request_wallpaper(uint8_t id, uint32_t *request_id);
d_err_t product_ui_appearance_request_reduce_motion(uint8_t enabled, uint32_t *request_id);
void product_ui_appearance_quiesce_begin(void);
void product_ui_appearance_quiesce_cancel(void);
d_err_t product_ui_appearance_wait_idle(uint32_t timeout_ms);
/* Optional service teardown; objects stay valid if a storage call returns late. */
d_err_t product_ui_appearance_service_stop(uint32_t timeout_ms);
#endif
#endif
