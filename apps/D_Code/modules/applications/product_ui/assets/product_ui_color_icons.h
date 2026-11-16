/**
 * @file product_ui_color_icons.h
 * @brief 由 ui_pack 生成的 LVGL ARGB8888 彩色图标；请修改生成器。
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

/**
 * @brief 按彩色图标编号借用静态图像描述符。
 * @param[in] icon_id 合法枚举编号；须不为负且小于 PRODUCT_UI_COLOR_ICON_COUNT。
 * @return 有效编号对应的静态只读描述符；达到或超过 COUNT 返回 NULL。
 * @note 实现只检查上界；调用方须保证不传负值。返回值不由调用方释放。
 */
const lv_image_dsc_t *product_ui_color_icon_get(
    product_ui_color_icon_id_t icon_id);

#endif /* __PRODUCT_UI_COLOR_ICONS_H__ */
