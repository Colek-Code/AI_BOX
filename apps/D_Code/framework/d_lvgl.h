/**
 * @file d_lvgl.h
 * @brief Bounded LVGL worker startup with an exact-once terminal snapshot.
 */

#ifndef D_LVGL_H
#define D_LVGL_H

#include "config/app_config.h"
#include "d_error.h"

#ifdef D_LVGL_ENABLE

#include "lvgl.h"

typedef enum {
    D_LVGL_INIT_NOT_STARTED = 0,
    D_LVGL_INIT_STARTING,
    D_LVGL_INIT_LATE_PENDING,
    D_LVGL_INIT_READY,
    D_LVGL_INIT_FAILED,
} d_lvgl_init_state_e;

typedef d_err_t (*d_lvgl_bootstrap_fn_t)(void);
typedef void (*d_lvgl_update_fn_t)(uint32_t elapsed_ms);
typedef void (*d_lvgl_cycle_complete_fn_t)(uint32_t update_ms, uint32_t render_ms);
typedef void (*d_lvgl_init_complete_cb_t)(d_err_t result, void *context);

typedef struct {
    d_lvgl_bootstrap_fn_t bootstrap;
    d_lvgl_update_fn_t update;
    d_lvgl_cycle_complete_fn_t cycle_complete_cb;
    /* Retained for the firmware lifetime; use NULL or a static font descriptor. */
    const lv_font_t *font_fallback_tail;
    d_lvgl_init_complete_cb_t complete_cb;
    /* Must remain valid until the asynchronous completion callback has run. */
    void *complete_context;
} d_lvgl_init_spec_t;

typedef struct {
    d_lvgl_init_state_e state;
    d_err_t result;
    uint8_t is_result_published;
} d_lvgl_init_snapshot_t;

d_err_t d_lvgl_init(const d_lvgl_init_spec_t *spec);
d_err_t d_lvgl_get_init_snapshot(d_lvgl_init_snapshot_t *snapshot);

#endif /* D_LVGL_ENABLE */
#endif /* D_LVGL_H */
