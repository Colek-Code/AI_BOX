/** @file product_ui_app_slot.h
 *  @brief Home artwork, caption and touch ownership.
 */
#ifndef __PRODUCT_UI_APP_SLOT_H__
#define __PRODUCT_UI_APP_SLOT_H__

#include "config/app_config.h"
#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)
#include "lvgl.h"
#include "modules/applications/product_ui/shared/motion/product_ui_motion.h"

typedef struct {
    const lv_image_dsc_t *image;
    const char *title;
    uint8_t is_enabled;
} product_ui_app_slot_spec_t;

typedef struct {
    lv_obj_t *root;
    lv_obj_t *icon;
    lv_obj_t *title;
    product_ui_motion_t press_motion;
    uint8_t is_enabled;
    uint8_t is_armed;
} product_ui_app_slot_t;

int product_ui_image_fit(lv_obj_t *image, const lv_image_dsc_t *source,
                         int32_t width, int32_t height);
int product_ui_app_slot_create(lv_obj_t *parent, product_ui_app_slot_t *slot,
                               lv_event_cb_t action, void *user_data);
int product_ui_app_slot_bind(product_ui_app_slot_t *slot,
                             const product_ui_app_slot_spec_t *spec);
void product_ui_app_slot_cancel(product_ui_app_slot_t *slot);
void product_ui_app_slot_dispose(product_ui_app_slot_t *slot);

#endif
#endif
