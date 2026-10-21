/** @file product_ui_pomodoro_view.h @brief Three bounded presentations and stable action identities. */
#ifndef PRODUCT_UI_POMODORO_VIEW_H
#define PRODUCT_UI_POMODORO_VIEW_H
#include "modules/applications/product_ui/shared/widgets/product_ui_components.h"
#include "product_ui_pomodoro_service.h"
typedef enum { PRODUCT_UI_POMO_TIMER, PRODUCT_UI_POMO_SETTINGS, PRODUCT_UI_POMO_STATS } product_ui_pomo_mode_e;
typedef enum {
    PRODUCT_UI_POMO_NONE, PRODUCT_UI_POMO_PRIMARY, PRODUCT_UI_POMO_SKIP, PRODUCT_UI_POMO_END,
    PRODUCT_UI_POMO_SETTINGS_OPEN, PRODUCT_UI_POMO_STATS_OPEN, PRODUCT_UI_POMO_BACK,
    PRODUCT_UI_POMO_SAVE, PRODUCT_UI_POMO_CLEAR, PRODUCT_UI_POMO_RETRY,
    PRODUCT_UI_POMO_DECREASE, PRODUCT_UI_POMO_INCREASE, PRODUCT_UI_POMO_TOGGLE,
    PRODUCT_UI_POMO_SET_DURATION,
} product_ui_pomo_action_e;
typedef void (*product_ui_pomo_action_fn)(product_ui_pomo_action_e action, uint8_t field,
    uint8_t value, uint32_t generation);
int product_ui_pomodoro_view_show(lv_obj_t *parent, product_ui_pomo_mode_e mode,
    const product_ui_pomodoro_snapshot_t *snapshot, product_ui_pomo_action_fn action);
void product_ui_pomodoro_view_refresh(const product_ui_pomodoro_snapshot_t *snapshot,
    const product_ui_pomodoro_settings_t *draft);
void product_ui_pomodoro_view_destroy(void);
lv_obj_t *product_ui_pomodoro_view_nav_button(lv_obj_t *parent, const char *text,
    product_ui_pomo_action_e action);
const char *product_ui_pomodoro_phase_name(product_ui_pomodoro_phase_e phase);
#endif
