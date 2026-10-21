/** @file product_ui_motion.h @brief Bounded UI motion and one terminal-state path. */
#ifndef PRODUCT_UI_MOTION_H
#define PRODUCT_UI_MOTION_H
#include "config/app_config.h"
#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)
#include "lvgl.h"
#include <stdint.h>
#define PRODUCT_UI_MOTION_PRESS_MS   (2U * D_PRODUCT_UI_FRAME_PERIOD_MS)
#define PRODUCT_UI_MOTION_RELEASE_MS (3U * D_PRODUCT_UI_FRAME_PERIOD_MS)
#define PRODUCT_UI_MOTION_ENTER_MS   (4U * D_PRODUCT_UI_FRAME_PERIOD_MS)
#define PRODUCT_UI_MOTION_SNAP_MS    (6U * D_PRODUCT_UI_FRAME_PERIOD_MS)

typedef void (*product_ui_motion_apply_t)(void *context, int32_t value);
typedef void (*product_ui_motion_complete_t)(void *context);
typedef struct {
    lv_obj_t *owner;
    void *context;
    product_ui_motion_apply_t apply;
    product_ui_motion_complete_t complete;
} product_ui_motion_binding_t;
typedef struct product_ui_motion {
    struct product_ui_motion *next;
    product_ui_motion_binding_t binding;
    int32_t current;
    int32_t target;
    uint32_t generation;
    uint8_t is_active;
    uint8_t is_bound;
} product_ui_motion_t;

void product_ui_motion_bind(product_ui_motion_t *motion, const product_ui_motion_binding_t *binding);
void product_ui_motion_dispose(product_ui_motion_t *motion);
void product_ui_motion_set(product_ui_motion_t *motion, int32_t value);
void product_ui_motion_start(product_ui_motion_t *motion, int32_t target, uint32_t duration_ms);
void product_ui_motion_cancel(product_ui_motion_t *motion, uint8_t finish);
uint8_t product_ui_motion_is_reduced(void);
void product_ui_motion_apply_tree(lv_obj_t *root);
void product_ui_motion_cancel_tree(lv_obj_t *root);
void product_ui_motion_process(uint32_t elapsed_ms);
#endif
#endif
