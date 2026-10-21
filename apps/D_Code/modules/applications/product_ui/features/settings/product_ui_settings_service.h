/**
 * @file product_ui_settings_service.h
 * @brief Product UI settings capabilities and bounded commands.
 */

#ifndef __PRODUCT_UI_SETTINGS_SERVICE_H__
#define __PRODUCT_UI_SETTINGS_SERVICE_H__

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

#include <stdint.h>
#include "modules/applications/product_ui/runtime/preferences/product_ui_appearance.h"

#define PRODUCT_UI_SETTING_CAP_BACKLIGHT_SWITCH (1U << 0)
#define PRODUCT_UI_SETTING_CAP_BRIGHTNESS       (1U << 1)
#define PRODUCT_UI_SETTING_CAP_VOLUME           (1U << 2)
#define PRODUCT_UI_SETTING_CAP_WALLPAPER        (1U << 3)
#define PRODUCT_UI_SETTING_CAP_TIME             (1U << 4)
#define PRODUCT_UI_SETTING_CAP_WIFI             (1U << 5)
#define PRODUCT_UI_SETTING_CAP_BLUETOOTH        (1U << 6)
#define PRODUCT_UI_SETTING_CAP_REDUCE_MOTION    (1U << 7)

/*
 * Continuous brightness must stay visible; 0 is reserved for backlight off.
 * Keep in sync with the minimum visible brightness in each display adapter.
 */
#define PRODUCT_UI_BRIGHTNESS_MIN_PERCENT       5U
#define PRODUCT_UI_BRIGHTNESS_MAX_PERCENT       100U

typedef enum {
    PRODUCT_UI_VOLUME_READY,
    PRODUCT_UI_VOLUME_APPLYING,
    PRODUCT_UI_VOLUME_ERROR,
    PRODUCT_UI_VOLUME_TIMEOUT,
    PRODUCT_UI_VOLUME_UNAVAILABLE,
    PRODUCT_UI_VOLUME_QUIESCING,
} product_ui_volume_state_e;

typedef struct {
    uint32_t capability_mask;
    uint32_t valid_mask;
    uint32_t revision;
    uint8_t backlight_enabled;
    uint8_t brightness_percent;
    uint8_t volume_percent;
    uint8_t wallpaper_id;
    uint8_t volume_target_percent;
    product_ui_volume_state_e volume_state;
    int32_t volume_error;
    uint32_t volume_request_id;
    uint32_t volume_completed_id;
    product_ui_appearance_snapshot_t appearance;
} product_ui_settings_snapshot_t;

typedef enum {
    PRODUCT_UI_SETTINGS_SET_BACKLIGHT,
    PRODUCT_UI_SETTINGS_SET_BRIGHTNESS,
    PRODUCT_UI_SETTINGS_SET_VOLUME,
    PRODUCT_UI_SETTINGS_SET_WALLPAPER,
    PRODUCT_UI_SETTINGS_SET_REDUCE_MOTION,
} product_ui_settings_command_e;

int product_ui_settings_service_init(void);
uint8_t product_ui_settings_volume_display(const product_ui_settings_snapshot_t *snapshot);
uint8_t product_ui_settings_volume_can_adjust(const product_ui_settings_snapshot_t *snapshot);
const char *product_ui_settings_volume_status(const product_ui_settings_snapshot_t *snapshot);
uint8_t product_ui_settings_appearance_can_save(const product_ui_settings_snapshot_t *snapshot);
const char *product_ui_settings_appearance_status(const product_ui_settings_snapshot_t *snapshot);
int product_ui_settings_service_snapshot(product_ui_settings_snapshot_t *snapshot);
int product_ui_settings_service_publish(
    const product_ui_settings_snapshot_t *snapshot);
int product_ui_settings_service_command(product_ui_settings_command_e command,
                                        uint8_t value);

#endif
#endif /* __PRODUCT_UI_SETTINGS_SERVICE_H__ */
