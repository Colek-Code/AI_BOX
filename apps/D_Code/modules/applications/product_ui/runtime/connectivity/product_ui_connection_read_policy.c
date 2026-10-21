/**
 * @file product_ui_connection_read_policy.c
 * @brief Pure CONNECTION status-read decisions (no platform/wifi deps).
 */

#include "modules/applications/product_ui/runtime/connectivity/product_ui_connection_read_policy.h"

product_ui_connection_sync_action_e product_ui_connection_sync_action(
    uint8_t wifi_on,
    uint8_t have_valid_snapshot,
    uint32_t current_version,
    uint32_t seen_version)
{
    if (wifi_on == 0U) {
        return PRODUCT_UI_CONN_SYNC_UNAVAILABLE;
    }
    if ((have_valid_snapshot != 0U) && (current_version == seen_version)) {
        return PRODUCT_UI_CONN_SYNC_SKIP_READ;
    }
    return PRODUCT_UI_CONN_SYNC_FULL_READ;
}

product_ui_connection_stable_select_e product_ui_connection_stable_select(
    uint32_t v0_before,
    uint32_t v0_after,
    uint32_t v1_before,
    uint32_t v1_after)
{
    if (v0_before == v0_after) {
        return PRODUCT_UI_CONN_STABLE_USE_FIRST;
    }
    if (v1_before == v1_after) {
        return PRODUCT_UI_CONN_STABLE_USE_SECOND;
    }
    return PRODUCT_UI_CONN_STABLE_ABORT;
}
