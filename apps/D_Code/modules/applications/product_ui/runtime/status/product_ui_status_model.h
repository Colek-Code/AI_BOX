/**
 * @file product_ui_status_model.h
 * @brief LVGL-free Product UI status validation contract.
 */

#ifndef __PRODUCT_UI_STATUS_MODEL_H__
#define __PRODUCT_UI_STATUS_MODEL_H__

#include <stdint.h>

typedef enum {
    PRODUCT_UI_LINK_UNKNOWN,
    PRODUCT_UI_LINK_OFF,
    PRODUCT_UI_LINK_ON,
    PRODUCT_UI_LINK_CONNECTED,
} product_ui_link_state_e;

#define PRODUCT_UI_STATUS_CAP_TIME       (1U << 0)
#define PRODUCT_UI_STATUS_CAP_WIFI       (1U << 1)
#define PRODUCT_UI_STATUS_CAP_BLUETOOTH  (1U << 2)
#define PRODUCT_UI_STATUS_CAP_BATTERY    (1U << 3)

#define PRODUCT_UI_STATUS_VALID_TIME       PRODUCT_UI_STATUS_CAP_TIME
#define PRODUCT_UI_STATUS_VALID_WIFI       PRODUCT_UI_STATUS_CAP_WIFI
#define PRODUCT_UI_STATUS_VALID_BLUETOOTH  PRODUCT_UI_STATUS_CAP_BLUETOOTH
#define PRODUCT_UI_STATUS_VALID_BATTERY    PRODUCT_UI_STATUS_CAP_BATTERY

typedef struct {
    uint8_t capability_mask;
    uint8_t valid_mask;
    uint8_t hour;
    uint8_t minute;
    uint8_t wifi_level;
    product_ui_link_state_e wifi_state;
    product_ui_link_state_e bluetooth_state;
    uint8_t battery_percent;
    uint8_t is_charging;
} product_ui_status_t;

typedef enum {
    PRODUCT_UI_STATUS_OK = 0,
    PRODUCT_UI_STATUS_PARTIAL = 1,
    PRODUCT_UI_STATUS_INVALID_ARGUMENT = -1,
} product_ui_status_result_e;

product_ui_status_result_e product_ui_status_normalize(
    const product_ui_status_t *input, product_ui_status_t *output);

#endif /* __PRODUCT_UI_STATUS_MODEL_H__ */
