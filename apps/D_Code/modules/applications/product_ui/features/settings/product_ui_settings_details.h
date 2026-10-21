/**
 * @file product_ui_settings_details.h
 * @brief Hidden Product UI settings detail page interfaces.
 */

#ifndef __PRODUCT_UI_SETTINGS_DETAILS_H__
#define __PRODUCT_UI_SETTINGS_DETAILS_H__

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

#include "lvgl.h"

int product_ui_settings_wifi_page_create(lv_obj_t *parent);
int product_ui_settings_bluetooth_page_create(lv_obj_t *parent);
int product_ui_settings_display_page_create(lv_obj_t *parent);
int product_ui_settings_sound_page_create(lv_obj_t *parent);
int product_ui_settings_wallpaper_page_create(lv_obj_t *parent);
int product_ui_settings_time_page_create(lv_obj_t *parent);

#endif
#endif /* __PRODUCT_UI_SETTINGS_DETAILS_H__ */
