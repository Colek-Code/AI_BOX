/**
 * @file product_ui_status_model.c
 * @brief Deterministic status validation without UI dependencies.
 */

#include "modules/applications/product_ui/runtime/status/product_ui_status_model.h"
#include <stddef.h>

product_ui_status_result_e product_ui_status_normalize(
    const product_ui_status_t *input, product_ui_status_t *output)
{
    product_ui_status_result_e result = PRODUCT_UI_STATUS_OK;
    uint8_t supported_mask;

    if ((input == NULL) || (output == NULL)) {
        return PRODUCT_UI_STATUS_INVALID_ARGUMENT;
    }
    *output = *input;
    supported_mask = PRODUCT_UI_STATUS_CAP_TIME |
                     PRODUCT_UI_STATUS_CAP_WIFI |
                     PRODUCT_UI_STATUS_CAP_BLUETOOTH |
                     PRODUCT_UI_STATUS_CAP_BATTERY;
    if (((output->capability_mask | output->valid_mask) &
         ~supported_mask) != 0U) {
        result = PRODUCT_UI_STATUS_PARTIAL;
    }
    output->capability_mask &= supported_mask;
    if ((output->valid_mask & ~output->capability_mask) != 0U) {
        result = PRODUCT_UI_STATUS_PARTIAL;
    }
    output->valid_mask &= output->capability_mask;

    if ((output->valid_mask & PRODUCT_UI_STATUS_VALID_TIME) &&
        ((output->hour > 23U) || (output->minute > 59U))) {
        output->valid_mask &= (uint8_t)~PRODUCT_UI_STATUS_VALID_TIME;
        output->hour = 0U;
        output->minute = 0U;
        result = PRODUCT_UI_STATUS_PARTIAL;
    }
    if ((output->valid_mask & PRODUCT_UI_STATUS_VALID_WIFI) &&
        ((output->wifi_level > 4U) ||
         (output->wifi_state > PRODUCT_UI_LINK_CONNECTED))) {
        output->valid_mask &= (uint8_t)~PRODUCT_UI_STATUS_VALID_WIFI;
        output->wifi_level = 0U;
        output->wifi_state = PRODUCT_UI_LINK_UNKNOWN;
        result = PRODUCT_UI_STATUS_PARTIAL;
    }
    if ((output->valid_mask & PRODUCT_UI_STATUS_VALID_BLUETOOTH) &&
        (output->bluetooth_state > PRODUCT_UI_LINK_CONNECTED)) {
        output->valid_mask &= (uint8_t)~PRODUCT_UI_STATUS_VALID_BLUETOOTH;
        output->bluetooth_state = PRODUCT_UI_LINK_UNKNOWN;
        result = PRODUCT_UI_STATUS_PARTIAL;
    }
    if ((output->valid_mask & PRODUCT_UI_STATUS_VALID_BATTERY) &&
        (output->battery_percent > 100U)) {
        output->valid_mask &= (uint8_t)~PRODUCT_UI_STATUS_VALID_BATTERY;
        output->battery_percent = 0U;
        output->is_charging = 0U;
        result = PRODUCT_UI_STATUS_PARTIAL;
    }
    output->is_charging = output->is_charging ? 1U : 0U;
    return result;
}
