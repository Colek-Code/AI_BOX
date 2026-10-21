/**
 * @file product_ui_components.h
 * @brief Shared Product UI component contracts.
 */

#ifndef __PRODUCT_UI_COMPONENTS_H__
#define __PRODUCT_UI_COMPONENTS_H__

#include "modules/applications/product_ui/product_ui.h"
#include "modules/applications/product_ui/shared/widgets/product_ui_row_model.h"
#include "modules/applications/product_ui/shared/widgets/product_ui_app_slot.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

typedef void (*product_ui_action_cb_t)(lv_event_t *event);

typedef enum {
    PRODUCT_UI_FEEDBACK_EMPTY,
    PRODUCT_UI_FEEDBACK_LOADING,
    PRODUCT_UI_FEEDBACK_ERROR,
} product_ui_feedback_state_e;

typedef struct {
    const product_ui_list_item_t *item;
    const char *subtitle;
    const char *status_text;
    product_ui_action_cb_t action_cb;
    void *user_data;
    product_ui_row_interaction_e interaction;
    product_ui_row_trailing_e trailing;
    uint8_t is_enabled;
    uint8_t is_checked;
    uint8_t is_single_line;
    uint32_t item_key;
} product_ui_row_spec_t;

/* Action callbacks stay attached to the row; rebind updates its displayed data and availability. */
int product_ui_list_row_rebind(lv_obj_t *row,
                               const product_ui_row_spec_t *spec);
int product_ui_list_row_status_update(lv_obj_t *row, const char *text);

typedef struct {
    product_ui_feedback_state_e state;
    const char *title;
    const char *message;
    const char *action_label;
    product_ui_action_cb_t action_cb;
    void *user_data;
} product_ui_feedback_spec_t;

#define PRODUCT_UI_VIRTUAL_LIST_SLOT_COUNT    6U

typedef struct product_ui_virtual_list product_ui_virtual_list_t;
typedef int (*product_ui_virtual_list_bind_cb_t)(
    uint16_t item_index, uint8_t slot_index,
    product_ui_row_spec_t *spec, void *user_data);
typedef void (*product_ui_virtual_list_action_cb_t)(
    uint16_t item_index, void *user_data);
/* Optional presentation owner; both callbacks are required together. Custom
 * creation attaches spec->action_cb to LV_EVENT_ALL when it is non-NULL. The
 * list still owns lifetime, binding identity, viewport and click cancellation. */
typedef lv_obj_t *(*product_ui_virtual_row_create_cb_t)(lv_obj_t *parent, const product_ui_row_spec_t *spec);
typedef int (*product_ui_virtual_row_rebind_cb_t)(lv_obj_t *row, const product_ui_row_spec_t *spec);

typedef struct {
    uint16_t item_count;
    uint16_t row_height;
    product_ui_virtual_list_bind_cb_t bind_cb;
    product_ui_virtual_list_action_cb_t action_cb;
    void *user_data;
    product_ui_virtual_row_create_cb_t row_create_cb;
    product_ui_virtual_row_rebind_cb_t row_rebind_cb;
} product_ui_virtual_list_spec_t;

typedef struct {
    lv_obj_t *row;
    product_ui_virtual_list_t *owner;
    uint16_t item_index;
    uint32_t binding_version;
    uint32_t item_key;
    uint32_t pressed_version;
    uint8_t is_armed;
    product_ui_row_interaction_e interaction;
} product_ui_virtual_list_slot_t;

struct product_ui_virtual_list {
    lv_obj_t *root;
    lv_obj_t *extent;
    lv_obj_t *header;
    product_ui_virtual_list_slot_t slots[PRODUCT_UI_VIRTUAL_LIST_SLOT_COUNT];
    product_ui_virtual_list_bind_cb_t bind_cb;
    product_ui_virtual_list_action_cb_t action_cb;
    void *user_data;
    product_ui_virtual_row_create_cb_t row_create_cb;
    product_ui_virtual_row_rebind_cb_t row_rebind_cb;
    uint16_t item_count;
    uint16_t first_bound;
    uint16_t row_height;
    uint16_t row_pitch;
    int32_t header_height;
    uint8_t is_refreshing;
    uint8_t render_pending;
    uint16_t retry_elapsed_ms;
};

typedef void (*product_ui_confirm_cb_t)(uint8_t is_confirmed, void *user_data);

#define PRODUCT_UI_WIFI_BAR_COUNT 4

typedef struct product_ui_status_view {
    lv_obj_t *root;
    lv_obj_t *time;
    lv_obj_t *wifi; /* signal container; bars driven by wifi_level */
    lv_obj_t *wifi_bar[PRODUCT_UI_WIFI_BAR_COUNT];
    lv_obj_t *bluetooth;
    /* Host is the flex unit; body/tip/fill live inside so tip cannot overlap peers. */
    lv_obj_t *battery_host;
    lv_obj_t *battery;
    lv_obj_t *battery_fill;
    lv_obj_t *battery_tip;
    lv_obj_t *battery_charge;
    lv_obj_t *battery_text;
    struct product_ui_status_view *next;
    uint8_t is_light;
    uint8_t hide_connectivity;
    uint8_t is_registered;
} product_ui_status_view_t;

typedef struct {
    lv_obj_t *root;
    lv_obj_t *status;
    lv_obj_t *nav;
    lv_obj_t *title;
    product_ui_motion_t title_motion;
    lv_obj_t *nav_action;
    lv_obj_t *nav_action_label;
    lv_obj_t *content;
    uint8_t is_nav_action_armed;
    uint8_t is_nav_action_read_only;
    product_ui_status_view_t status_view;
} product_ui_page_view_t;

lv_obj_t *product_ui_list_row_create(lv_obj_t *parent,
                                     const product_ui_row_spec_t *spec);
lv_obj_t *product_ui_section_create(lv_obj_t *parent, const char *title);
lv_obj_t *product_ui_empty_state_create(lv_obj_t *parent, const char *message);
int product_ui_virtual_list_create(lv_obj_t *parent,
                                      product_ui_virtual_list_t *list,
                                      const product_ui_virtual_list_spec_t *spec);
int product_ui_virtual_list_refresh(product_ui_virtual_list_t *list);
int product_ui_virtual_list_set_count(product_ui_virtual_list_t *list, uint16_t count);
void product_ui_virtual_list_update(product_ui_virtual_list_t *list, uint32_t elapsed_ms);
uint16_t product_ui_virtual_list_first_visible(
    const product_ui_virtual_list_t *list);
void product_ui_virtual_list_scroll_to(product_ui_virtual_list_t *list,
                                       uint16_t item_index);
lv_obj_t *product_ui_feedback_create(lv_obj_t *parent,
                                     product_ui_feedback_state_e state,
                                     const char *title,
                                     const char *message);
lv_obj_t *product_ui_feedback_create_ex(
    lv_obj_t *parent, const product_ui_feedback_spec_t *spec);
void product_ui_toast_show(const char *message, uint16_t duration_ms);
void product_ui_feedback_clear(void);
uint8_t product_ui_feedback_is_modal(void);
lv_obj_t *product_ui_confirm_dialog_show(const char *title,
                                         const char *message,
                                         product_ui_confirm_cb_t callback,
                                         void *user_data);
lv_obj_t *product_ui_toggle_row_create(lv_obj_t *parent, const char *title,
                                       const char *subtitle, uint8_t is_checked);
lv_obj_t *product_ui_setting_row_get_control(lv_obj_t *row);
lv_obj_t *product_ui_slider_row_create(lv_obj_t *parent, const char *title,
                                       int32_t value);

int product_ui_control_center_init(void);
void product_ui_control_center_show(uint8_t visible);
void product_ui_control_center_refresh(void);
void product_ui_control_center_close_for_page_transition(void);
void product_ui_control_center_bind_pull(lv_obj_t *obj);
/* UI-thread modal ownership; repeated acquisition/release by one owner is harmless. */
void product_ui_control_center_hold_pull(uint8_t *is_held);
void product_ui_control_center_release_pull(uint8_t *is_held);
uint8_t product_ui_control_center_is_busy(void);
uint8_t product_ui_control_center_is_open(void);
lv_obj_t *product_ui_control_status_create(lv_obj_t *parent,
                                           product_ui_status_view_t *view);

int product_ui_page_frame_create_ex(lv_obj_t *parent,
                                    const product_ui_page_spec_t *spec,
                                    product_ui_page_view_t *view);
void product_ui_page_view_reset(product_ui_page_view_t *view);
void product_ui_page_enter(lv_obj_t *root, uint8_t is_back);
/* Changing the displayed operation cancels the current press. */
void product_ui_page_action_update(product_ui_page_view_t *view,
                                    const char *text, uint8_t is_enabled,
                                    uint8_t is_read_only);

void product_ui_status_bar_refresh(const product_ui_status_t *status);

#endif
#endif /* __PRODUCT_UI_COMPONENTS_H__ */
