/**
 * @file product_ui_color_icons.h
 * @brief Color UI icons from ui_pack (LVGL ARGB8888). Generated — do not edit.
 */

#ifndef __PRODUCT_UI_COLOR_ICONS_H__
#define __PRODUCT_UI_COLOR_ICONS_H__

#include "lvgl.h"

#define PRODUCT_UI_HOME_ICON_CANVAS_SIZE 60
#define PRODUCT_UI_HOME_ICON_OPTICAL_SIZE 56

typedef enum {
    PRODUCT_UI_COLOR_ICON_MUSIC,
    PRODUCT_UI_COLOR_ICON_CALENDAR,
    PRODUCT_UI_COLOR_ICON_CALCULATOR,
    PRODUCT_UI_COLOR_ICON_GALLERY,
    PRODUCT_UI_COLOR_ICON_ALARM,
    PRODUCT_UI_COLOR_ICON_POMODORO,
    PRODUCT_UI_COLOR_ICON_SCHEDULE,
    PRODUCT_UI_COLOR_ICON_SETTINGS,
    PRODUCT_UI_COLOR_ICON_EMPTY_SLOT,
    PRODUCT_UI_COLOR_ICON_AI_CHAT,
    PRODUCT_UI_COLOR_ICON_CAMERA,
    PRODUCT_UI_COLOR_ICON_COUNT
} product_ui_color_icon_id_t;

const lv_image_dsc_t *product_ui_color_icon_get(
    product_ui_color_icon_id_t icon_id);

#endif /* __PRODUCT_UI_COLOR_ICONS_H__ */
