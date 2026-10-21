/** @file product_ui_calendar_view.h
 * @brief LVGL calendar presentation; controller owns dates and queued actions.
 */
#ifndef __PRODUCT_UI_CALENDAR_VIEW_H__
#define __PRODUCT_UI_CALENDAR_VIEW_H__

#include "config/app_config.h"
#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)
#include "lvgl.h"
#include "modules/applications/product_ui/features/time/product_ui_calendar_model.h"

typedef enum {
    PRODUCT_UI_CALENDAR_MONTH,
    PRODUCT_UI_CALENDAR_DETAIL,
    PRODUCT_UI_CALENDAR_PICKER,
} product_ui_calendar_mode_e;

typedef enum {
    PRODUCT_UI_CALENDAR_ACTION_NONE,
    PRODUCT_UI_CALENDAR_ACTION_PREVIOUS,
    PRODUCT_UI_CALENDAR_ACTION_NEXT,
    PRODUCT_UI_CALENDAR_ACTION_PICKER,
    PRODUCT_UI_CALENDAR_ACTION_SELECT,
    PRODUCT_UI_CALENDAR_ACTION_YEAR_DOWN,
    PRODUCT_UI_CALENDAR_ACTION_YEAR_UP,
    PRODUCT_UI_CALENDAR_ACTION_MONTH_DOWN,
    PRODUCT_UI_CALENDAR_ACTION_MONTH_UP,
    PRODUCT_UI_CALENDAR_ACTION_APPLY,
    PRODUCT_UI_CALENDAR_ACTION_TODAY,
    PRODUCT_UI_CALENDAR_ACTION_BACK,
    PRODUCT_UI_CALENDAR_ACTION_BROWSE,
    PRODUCT_UI_CALENDAR_ACTION_DECADE_DOWN,
    PRODUCT_UI_CALENDAR_ACTION_DECADE_UP,
} product_ui_calendar_action_e;

typedef struct {
    product_ui_calendar_mode_e mode;
    const product_ui_calendar_month_t *month;
    product_ui_date_t selected;
    product_ui_date_t today;
    uint16_t picker_year;
    uint8_t picker_month;
    uint8_t has_today;
    int32_t month_scroll; /* -1 reveals selected date; otherwise restore exact offset. */
    int32_t detail_scroll; /* Independent viewport for a temporary settings visit. */
} product_ui_calendar_presentation_t;

typedef void (*product_ui_calendar_action_cb_t)(
    product_ui_calendar_action_e action, const product_ui_date_t *date);

void product_ui_calendar_view_init(lv_obj_t *parent, product_ui_calendar_action_cb_t callback);
/** Build a replacement first; retain the old usable subtree if creation fails. */
int product_ui_calendar_view_show(const product_ui_calendar_presentation_t *state);
void product_ui_calendar_view_cancel_press(void);
/** Return the currently displayed month-grid or detail-list scroll offset. */
int32_t product_ui_calendar_view_scroll(void);
void product_ui_calendar_view_update(uint32_t elapsed_ms);
void product_ui_calendar_view_reset(void);

#endif
#endif
