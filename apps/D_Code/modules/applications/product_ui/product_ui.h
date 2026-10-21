/**
 * @file product_ui.h
 * @brief Public entry for the GC9307 product UI framework.
 */

#ifndef __PRODUCT_UI_H__
#define __PRODUCT_UI_H__

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

#include "modules/applications/product_ui/shared/theme/product_ui_theme.h"
#include "modules/applications/product_ui/assets/product_ui_icons.h"
#include "modules/applications/product_ui/runtime/status/product_ui_status_model.h"
#include "lvgl.h"
#include <stdint.h>

typedef enum {
    PRODUCT_UI_PAGE_ROLE_AUTO,
    PRODUCT_UI_PAGE_ROLE_HOME,
    PRODUCT_UI_PAGE_ROLE_CONTENT,
    PRODUCT_UI_PAGE_ROLE_SETTINGS,
} product_ui_page_role_e;

typedef enum {
    PRODUCT_UI_PAGE_THEME_AUTO,
    PRODUCT_UI_PAGE_THEME_LIGHT,
    PRODUCT_UI_PAGE_THEME_DARK,
    PRODUCT_UI_PAGE_THEME_TRANSPARENT,
} product_ui_page_theme_e;

typedef struct {
    const char *title;
    const char *back_title;
    lv_event_cb_t nav_back_cb;
    void *nav_back_user_data;
    const char *nav_action_symbol;
    const lv_image_dsc_t *nav_action_image;
    const char *nav_action_text;
    uint8_t nav_action_read_only;
    uint8_t nav_action_disabled;
    lv_event_cb_t nav_action_cb;
    void *nav_action_user_data;
    uint8_t show_back;
    product_ui_page_role_e role;
    product_ui_page_theme_e theme;
} product_ui_page_spec_t;

typedef struct {
    const char *item_id;
    const char *title;
    product_ui_icon_id_t icon_id;
    product_ui_color_e accent;
} product_ui_list_item_t;

int product_ui_register(void);
int product_ui_prepare_pages(void);
int product_ui_navigate_to(const char *module_id);
int product_ui_navigation_back(void);
void product_ui_navigation_reset(void);
int product_ui_open_module(const char *module_id);
product_ui_status_result_e product_ui_status_update(
    const product_ui_status_t *status);

#endif
#endif /* __PRODUCT_UI_H__ */
