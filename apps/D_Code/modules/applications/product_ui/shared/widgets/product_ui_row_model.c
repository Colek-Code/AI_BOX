/**
 * @file product_ui_row_model.c
 * @brief Deterministic validation for row interaction and trailing style.
 */

#include "modules/applications/product_ui/shared/widgets/product_ui_row_model.h"

uint8_t product_ui_row_semantics_valid(
    product_ui_row_interaction_e interaction,
    product_ui_row_trailing_e trailing,
    uint8_t has_callback, uint8_t has_status_text)
{
    if ((interaction > PRODUCT_UI_ROW_INTERACTION_ACTION) ||
        (trailing > PRODUCT_UI_ROW_TRAILING_STATUS)) {
        return 0U;
    }
    if ((interaction != PRODUCT_UI_ROW_INTERACTION_NONE) && !has_callback) {
        return 0U;
    }
    if ((interaction == PRODUCT_UI_ROW_INTERACTION_NAVIGATE) &&
        (trailing != PRODUCT_UI_ROW_TRAILING_CHEVRON)) {
        return 0U;
    }
    if ((interaction == PRODUCT_UI_ROW_INTERACTION_TOGGLE) &&
        (trailing != PRODUCT_UI_ROW_TRAILING_SWITCH)) {
        return 0U;
    }
    if ((interaction == PRODUCT_UI_ROW_INTERACTION_NONE) &&
        ((trailing == PRODUCT_UI_ROW_TRAILING_CHEVRON) ||
         (trailing == PRODUCT_UI_ROW_TRAILING_SWITCH))) {
        return 0U;
    }
    if ((trailing == PRODUCT_UI_ROW_TRAILING_STATUS) && !has_status_text) {
        return 0U;
    }
    return 1U;
}
