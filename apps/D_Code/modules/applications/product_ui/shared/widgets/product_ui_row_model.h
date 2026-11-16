/** @file product_ui_row_model.h @brief 不依赖 LVGL 的列表行交互与尾部样式语义校验。 */

#ifndef __PRODUCT_UI_ROW_MODEL_H__
#define __PRODUCT_UI_ROW_MODEL_H__

#include <stdint.h>

typedef enum {
    PRODUCT_UI_ROW_INTERACTION_NONE,
    PRODUCT_UI_ROW_INTERACTION_NAVIGATE,
    PRODUCT_UI_ROW_INTERACTION_TOGGLE,
    PRODUCT_UI_ROW_INTERACTION_ACTION,
} product_ui_row_interaction_e;

typedef enum {
    PRODUCT_UI_ROW_TRAILING_NONE,
    PRODUCT_UI_ROW_TRAILING_CHEVRON,
    PRODUCT_UI_ROW_TRAILING_SWITCH,
    PRODUCT_UI_ROW_TRAILING_STATUS,
} product_ui_row_trailing_e;

/**
 * @brief 检查列表行交互、尾部外观和回调／状态文本是否一致。
 * @param[in] interaction 行的交互类型。
 * @param[in] trailing 行尾展示类型。
 * @param[in] has_callback 是否存在交互回调。
 * @param[in] has_status_text 是否存在状态文本。
 * @return 满足规则返回 1；超出上界、缺少交互回调或尾部不匹配返回 0。
 * @note 导航须配箭头，切换须配开关；纯展示行不配箭头或开关，状态尾部须有状态文本。
 */
uint8_t product_ui_row_semantics_valid(
    product_ui_row_interaction_e interaction,
    product_ui_row_trailing_e trailing,
    uint8_t has_callback, uint8_t has_status_text);

#endif /* __PRODUCT_UI_ROW_MODEL_H__ */
