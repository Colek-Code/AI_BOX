/**
 * @file product_ui_icons.h
 * @brief Product UI embedded icon declarations.
 */

#ifndef __PRODUCT_UI_ICONS_H__
#define __PRODUCT_UI_ICONS_H__

#include "lvgl.h"

typedef enum {
    PRODUCT_UI_ICON_MUSIC,
    PRODUCT_UI_ICON_CALENDAR,
    PRODUCT_UI_ICON_CALCULATOR,
    PRODUCT_UI_ICON_GALLERY,
    PRODUCT_UI_ICON_ALARM,
    PRODUCT_UI_ICON_TIMER,
    PRODUCT_UI_ICON_SCHEDULE,
    PRODUCT_UI_ICON_SETTINGS,
    PRODUCT_UI_ICON_WIFI,
    PRODUCT_UI_ICON_BLUETOOTH,
    PRODUCT_UI_ICON_ROTATION,
    PRODUCT_UI_ICON_BRIGHTNESS,
    PRODUCT_UI_ICON_VOLUME,
    /* Control-center only (A4 white glyphs; not for home/list). */
    PRODUCT_UI_ICON_CC_WIFI,
    PRODUCT_UI_ICON_CC_FOCUS,
    PRODUCT_UI_ICON_CC_MEDIA,
    PRODUCT_UI_ICON_CC_ALARM,
    PRODUCT_UI_ICON_CC_ROTATION,
    PRODUCT_UI_ICON_CC_SETTINGS,
    PRODUCT_UI_ICON_CC_BRIGHTNESS,
    PRODUCT_UI_ICON_CC_VOLUME,
    PRODUCT_UI_ICON_CC_PREVIOUS,
    PRODUCT_UI_ICON_CC_PLAY,
    PRODUCT_UI_ICON_CC_PAUSE,
    PRODUCT_UI_ICON_CC_NEXT,
    PRODUCT_UI_ICON_AI_CHAT,
    PRODUCT_UI_ICON_CAMERA,
    PRODUCT_UI_ICON_COUNT
} product_ui_icon_id_t;

const lv_image_dsc_t *product_ui_icon_get(product_ui_icon_id_t icon_id);
const lv_image_dsc_t *product_ui_icon_get_sized(product_ui_icon_id_t icon_id,
                                               uint16_t size);

#endif /* __PRODUCT_UI_ICONS_H__ */
