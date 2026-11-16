/**
 * @file product_ui_icons.h
 * @brief Product UI 静态 A4 图标声明；枚举顺序与生成表共同构成资源编号合同。
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
    /* 共用单色 A4 控件图标；首页彩色图稿使用独立资源。 */
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
    PRODUCT_UI_ICON_MEDIA_LIST,
    PRODUCT_UI_ICON_COUNT
} product_ui_icon_id_t;

/**
 * @brief 按编号借用静态 A4 图标描述符。
 * @param[in] icon_id 图标编号。
 * @return 合法编号返回静态只读图标；负值或达到 COUNT 返回 NULL。
 * @note 描述符及像素不由调用方释放，默认尺寸由生成清单决定。
 */
const lv_image_dsc_t *product_ui_icon_get(product_ui_icon_id_t icon_id);
/**
 * @brief 按编号及已生成的原生尺寸借用静态 A4 图标。
 * @param[in] icon_id 图标编号，须在零至 COUNT 之前。
 * @param[in] size 资源清单声明并生成的原生画布尺寸。
 * @return 合法编号且尺寸表存在该图标时返回静态描述符；非法编号、尺寸不支持或未生成条目时返回 NULL。
 * @note 不执行运行时缩放，不转移描述符或像素所有权。
 */
const lv_image_dsc_t *product_ui_icon_get_sized(product_ui_icon_id_t icon_id,
                                               uint16_t size);

#endif /* __PRODUCT_UI_ICONS_H__ */
