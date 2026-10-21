/**
 * @file product_ui_settings_gc9307.c
 * @brief GC9307 Product UI settings adapter outside the UI service layer.
 */

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT) && \
    (D_BOARD_IS_GC9307)

#include "modules/applications/product_ui/adapters/gc9307/product_ui_gc9307_adapter.h"
#include "platform/screen/d_display.h"
#include "platform/screen/power/d_screen_power.h"
#include "modules/applications/product_ui/runtime/service_registry/product_ui_service_registry.h"
#include "modules/applications/product_ui/features/settings/product_ui_settings_service.h"
#include "modules/applications/product_ui/runtime/status/product_ui_status_model.h"
#include "framework/d_error.h"
#include "modules/services/audio/d_device_volume.h"
#include <string.h>

/*
 * Manual backlight switch restore level (settings adapter only).
 * Safe-dim off saves pre-dim brightness here; on restores it.
 * Never used by the timeout screen-power path.
 */
static uint8_t s_bl_switch_restore_percent = 80U;
static uint32_t s_volume_revision;

void product_ui_settings_gc9307_process(void)
{
    d_device_volume_snapshot_t volume;
    product_ui_settings_snapshot_t snapshot;
    d_device_volume_get_snapshot(&volume);
    if (volume.revision == s_volume_revision ||
        product_ui_settings_service_snapshot(&snapshot) != 0) return;
    snapshot.volume_percent = volume.volume_percent;
    snapshot.volume_target_percent = volume.target_percent;
    snapshot.volume_request_id = volume.request_id;
    snapshot.volume_completed_id = volume.completed_id;
    snapshot.volume_error = volume.result;
    if (!volume.is_service_ready || !volume.is_backend_ready) {
        snapshot.capability_mask &= ~PRODUCT_UI_SETTING_CAP_VOLUME;
        snapshot.valid_mask &= ~PRODUCT_UI_SETTING_CAP_VOLUME;
        snapshot.volume_state = PRODUCT_UI_VOLUME_UNAVAILABLE;
        if (!volume.is_service_ready) snapshot.volume_error = volume.service_error;
    } else {
        snapshot.capability_mask |= PRODUCT_UI_SETTING_CAP_VOLUME;
        snapshot.valid_mask |= PRODUCT_UI_SETTING_CAP_VOLUME;
        if (volume.is_quiescing) snapshot.volume_state = PRODUCT_UI_VOLUME_QUIESCING;
        else if (volume.is_timed_out) snapshot.volume_state = PRODUCT_UI_VOLUME_TIMEOUT;
        else if (volume.is_pending || volume.is_in_progress)
            snapshot.volume_state = PRODUCT_UI_VOLUME_APPLYING;
        else if (volume.result != D_OK) snapshot.volume_state = PRODUCT_UI_VOLUME_ERROR;
        else snapshot.volume_state = PRODUCT_UI_VOLUME_READY;
    }
    if (product_ui_settings_service_publish(&snapshot) == 0)
        s_volume_revision = volume.revision;
}

static uint8_t product_ui_settings_gc9307_clamp_visible(uint8_t percent)
{
    if (percent < PRODUCT_UI_BRIGHTNESS_MIN_PERCENT) {
        return PRODUCT_UI_BRIGHTNESS_MIN_PERCENT;
    }
    if (percent > PRODUCT_UI_BRIGHTNESS_MAX_PERCENT) {
        return PRODUCT_UI_BRIGHTNESS_MAX_PERCENT;
    }
    return percent;
}

static int product_ui_settings_gc9307_command(
    uint16_t command, const void *payload, uint16_t payload_size,
    void *user_data)
{
    product_ui_settings_snapshot_t snapshot;
    uint8_t value;

    (void)user_data;
    if ((payload == NULL) || (payload_size != sizeof(value))) {
        return -1;
    }
    value = *(const uint8_t *)payload;

    if (product_ui_settings_service_snapshot(&snapshot) != 0) {
        return -1;
    }

    switch (command) {
    case PRODUCT_UI_SETTINGS_SET_VOLUME: {
        uint32_t request_id;
        d_err_t result = d_device_volume_request_set(value, &request_id);
        product_ui_settings_gc9307_process();
        return result == D_OK ? 0 : -1;
    }
    case PRODUCT_UI_SETTINGS_SET_BACKLIGHT: {
        d_err_t bl_err;
        uint8_t current;
        uint8_t target;
        uint8_t ok;

        if ((snapshot.capability_mask &
             PRODUCT_UI_SETTING_CAP_BACKLIGHT_SWITCH) == 0U) {
            return -1;
        }
        value = value ? 1U : 0U;
        current = d_display_get_brightness();
        /*
         * Manual switch is safe-dim only: never force_off / backlight(0) /
         * set_brightness(0). Timeout lock stays independent of this branch.
         */
        if (value != 0U) {
            target = product_ui_settings_gc9307_clamp_visible(
                s_bl_switch_restore_percent);
            bl_err = d_display_set_brightness(target);
            snapshot.brightness_percent = d_display_get_brightness();
            ok = ((bl_err == D_OK) &&
                  (snapshot.brightness_percent >=
                   PRODUCT_UI_BRIGHTNESS_MIN_PERCENT) &&
                  (snapshot.brightness_percent <=
                   PRODUCT_UI_BRIGHTNESS_MAX_PERCENT)) ?
                     1U :
                     0U;
            snapshot.backlight_enabled = (ok != 0U) ? 1U : 0U;
            if ((ok == 0U) &&
                (snapshot.brightness_percent >
                 PRODUCT_UI_BRIGHTNESS_MIN_PERCENT)) {
                /* Still lit after failed restore — keep switch on. */
                snapshot.backlight_enabled = 1U;
            }
        } else {
            if (current > PRODUCT_UI_BRIGHTNESS_MIN_PERCENT) {
                s_bl_switch_restore_percent =
                    product_ui_settings_gc9307_clamp_visible(current);
            }
            bl_err = d_display_set_brightness(
                PRODUCT_UI_BRIGHTNESS_MIN_PERCENT);
            snapshot.brightness_percent = d_display_get_brightness();
            /* Off success = at MIN dim, never physical black. */
            ok = ((bl_err == D_OK) &&
                  (snapshot.brightness_percent ==
                   PRODUCT_UI_BRIGHTNESS_MIN_PERCENT)) ?
                     1U :
                     0U;
            if (ok != 0U) {
                snapshot.brightness_percent =
                    PRODUCT_UI_BRIGHTNESS_MIN_PERCENT;
                /* Switch off visual; slider remains operable at MIN. */
                snapshot.backlight_enabled = 0U;
            } else {
                if (snapshot.brightness_percent <
                    PRODUCT_UI_BRIGHTNESS_MIN_PERCENT) {
                    snapshot.brightness_percent =
                        PRODUCT_UI_BRIGHTNESS_MIN_PERCENT;
                }
                snapshot.backlight_enabled =
                    (snapshot.brightness_percent >
                     PRODUCT_UI_BRIGHTNESS_MIN_PERCENT) ?
                        1U :
                        0U;
            }
        }
        snapshot.valid_mask |= PRODUCT_UI_SETTING_CAP_BACKLIGHT_SWITCH;
        if ((snapshot.capability_mask & PRODUCT_UI_SETTING_CAP_BRIGHTNESS) !=
            0U) {
            snapshot.valid_mask |= PRODUCT_UI_SETTING_CAP_BRIGHTNESS;
        }
        if (ok == 0U) {
            (void)product_ui_settings_service_publish(&snapshot);
            return -1;
        }
        return product_ui_settings_service_publish(&snapshot);
    }

    case PRODUCT_UI_SETTINGS_SET_BRIGHTNESS: {
        uint8_t was_off;

        if ((snapshot.capability_mask & PRODUCT_UI_SETTING_CAP_BRIGHTNESS) ==
            0U) {
            return -1;
        }
        if (value > PRODUCT_UI_BRIGHTNESS_MAX_PERCENT) {
            return -1;
        }
        /*
         * Continuous brightness never blacks out; 0 is only via backlight off
         * / screen-power owner, not the brightness slider command.
         */
        if (value < PRODUCT_UI_BRIGHTNESS_MIN_PERCENT) {
            value = PRODUCT_UI_BRIGHTNESS_MIN_PERCENT;
        }
        was_off = d_screen_power_is_off();
        if (was_off != 0U) {
            /* Clear off state only; one apply avoids a last-nonzero flash. */
            d_screen_power_clear_off();
        }
        if (d_display_set_brightness(value) != D_OK) {
            if (was_off != 0U) {
                /* Restore off + dark after failed wake-by-brightness. */
                d_screen_power_force_off();
            }
            snapshot.brightness_percent = d_display_get_brightness();
            /*
             * Align with safe-dim switch: MIN dim is "off" visual; above MIN
             * is on. Never use bare >0 (MIN is still non-zero PWM).
             */
            snapshot.backlight_enabled =
                (snapshot.brightness_percent >
                 PRODUCT_UI_BRIGHTNESS_MIN_PERCENT) ?
                    1U :
                    0U;
            snapshot.valid_mask |= PRODUCT_UI_SETTING_CAP_BRIGHTNESS |
                                   PRODUCT_UI_SETTING_CAP_BACKLIGHT_SWITCH;
            (void)product_ui_settings_service_publish(&snapshot);
            return -1;
        }
        snapshot.brightness_percent = d_display_get_brightness();
        if (snapshot.brightness_percent > PRODUCT_UI_BRIGHTNESS_MIN_PERCENT) {
            s_bl_switch_restore_percent =
                product_ui_settings_gc9307_clamp_visible(
                    snapshot.brightness_percent);
            snapshot.backlight_enabled = 1U;
        } else {
            /* At MIN: same as safe-dim off for switch visual. */
            snapshot.brightness_percent = PRODUCT_UI_BRIGHTNESS_MIN_PERCENT;
            snapshot.backlight_enabled = 0U;
        }
        snapshot.valid_mask |= PRODUCT_UI_SETTING_CAP_BRIGHTNESS |
                               PRODUCT_UI_SETTING_CAP_BACKLIGHT_SWITCH;
        return product_ui_settings_service_publish(&snapshot);
    }

    default:
        return -1;
    }
}

int product_ui_settings_gc9307_init(void)
{
    product_ui_settings_snapshot_t snapshot;
    uint8_t brightness;

    memset(&snapshot, 0, sizeof(snapshot));
    s_volume_revision = 0U;
    snapshot.capability_mask =
        PRODUCT_UI_SETTING_CAP_BACKLIGHT_SWITCH |
        PRODUCT_UI_SETTING_CAP_BRIGHTNESS | PRODUCT_UI_SETTING_CAP_TIME;
    snapshot.valid_mask = snapshot.capability_mask;
    /* d_display_init runs before settings init; trust adapter software state. */
    brightness = d_display_get_brightness();
    if (brightness < PRODUCT_UI_BRIGHTNESS_MIN_PERCENT) {
        brightness = PRODUCT_UI_BRIGHTNESS_MIN_PERCENT;
    }
    snapshot.brightness_percent = brightness;
    snapshot.backlight_enabled =
        (brightness > PRODUCT_UI_BRIGHTNESS_MIN_PERCENT) ? 1U : 0U;
    if (snapshot.backlight_enabled != 0U) {
        s_bl_switch_restore_percent =
            product_ui_settings_gc9307_clamp_visible(brightness);
    }
#if D_FEATURE_WIFI
    snapshot.capability_mask |= PRODUCT_UI_SETTING_CAP_WIFI;
    snapshot.valid_mask |= PRODUCT_UI_SETTING_CAP_WIFI;
#endif
    if (product_ui_service_register_command(
            PRODUCT_UI_SERVICE_SETTINGS,
            product_ui_settings_gc9307_command, NULL) != 0) {
        return -1;
    }
    if (product_ui_settings_service_publish(&snapshot) != 0) {
        product_ui_service_register_command(PRODUCT_UI_SERVICE_SETTINGS,
                                            NULL, NULL);
        return -1;
    }
    if (product_ui_service_publish(
            PRODUCT_UI_SERVICE_SETTINGS, PRODUCT_UI_SERVICE_READY,
            snapshot.valid_mask, 0) != 0) {
        product_ui_service_register_command(PRODUCT_UI_SERVICE_SETTINGS,
                                            NULL, NULL);
        return -1;
    }
    product_ui_settings_gc9307_process();
    /* Connection is initialized independently in product_ui_register(). */
    return 0;
}

#endif /* Product UI GC9307 profile */
