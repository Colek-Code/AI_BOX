/**
 * @file product_ui_row_model.h
 * @brief LVGL-free list-row interaction semantics.
 */

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

uint8_t product_ui_row_semantics_valid(
    product_ui_row_interaction_e interaction,
    product_ui_row_trailing_e trailing,
    uint8_t has_callback, uint8_t has_status_text);

#endif /* __PRODUCT_UI_ROW_MODEL_H__ */
