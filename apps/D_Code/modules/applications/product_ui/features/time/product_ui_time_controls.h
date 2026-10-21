/** @file product_ui_time_controls.h @brief Shared clock/alarm controls with release-only roller commits. */
#ifndef PRODUCT_UI_TIME_CONTROLS_H
#define PRODUCT_UI_TIME_CONTROLS_H
#include "modules/applications/product_ui/shared/widgets/product_ui_components.h"

typedef struct product_ui_time_roller product_ui_time_roller_t;
struct product_ui_time_roller {
    lv_obj_t *object;
    void (*changed)(product_ui_time_roller_t *binding, uint16_t selected);
    uint32_t generation;
    uint32_t pressed_generation;
    uint16_t original;
    uint8_t field;
    uint8_t armed;
};

extern const char product_ui_time_hours[];
extern const char product_ui_time_minutes[];
extern const char product_ui_time_months[];
extern const char product_ui_time_years[];
const char *product_ui_time_day_options(uint8_t count);
void product_ui_time_control_clean(lv_obj_t *object);
/* Creation text is immutable and must outlive the control (normally a literal).
 * Use lv_label_set_text for later formatted/temporary strings. */
lv_obj_t *product_ui_time_label(lv_obj_t *parent, const char *text, product_ui_style_e style);
lv_obj_t *product_ui_time_button(lv_obj_t *parent, const char *text, product_ui_action_cb_t callback, void *context);
lv_obj_t *product_ui_time_roller_create(lv_obj_t *parent, const char *options, product_ui_time_roller_t *binding);
void product_ui_time_roller_bind(product_ui_time_roller_t *binding, lv_obj_t *object, uint16_t selected, uint32_t generation);
void product_ui_time_roller_cancel(product_ui_time_roller_t *binding);
#endif
