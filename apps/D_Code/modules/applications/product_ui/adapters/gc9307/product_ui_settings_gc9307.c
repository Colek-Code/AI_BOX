/** @file product_ui_settings_gc9307.c @brief 将设备音量、亮度及安全调暗投影到 UI 设置域。 */

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

/* 设置适配专用的手动背光恢复亮度；安全调暗前保存，开启时恢复。
 * 超时灭屏路径不使用此值。 */
static uint8_t s_bl_switch_restore_percent = 80U;
static uint32_t s_volume_revision;

/**
 * @brief 按设备音量代次更新请求编号、完成编号、能力及音量状态投影。
 */
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

/**
 * @brief 把亮度限定为设置允许的可见区间。
 * @param[in] percent 待限定的亮度百分比。
 * @return 钳制后的百分比，至少为非零最低亮度。
 */
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

/**
 * @brief 验证单字节设置载荷，受理音量或同步应用安全调暗／亮度。
 * @param[in] command 音量、背光开关或亮度命令。
 * @param[in] payload 调用期间借用的单字节目标值。
 * @param[in] payload_size 必须等于一个 uint8_t。
 * @param[in] user_data 注册上下文，当前未使用。
 * @return 操作及投影成功返回 0；载荷、能力或底层操作失败返回 -1。
 * @note 音量成功只表示请求受理；手动背光关闭只调到最低亮度。亮度唤醒失败时恢复灭屏，并按实际读回发布投影。
 */
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
        /* 手动开关只安全调暗，不调用物理灭屏或零亮度接口；
         * 超时灭屏所有者的锁定状态独立于本分支。 */
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
                /* 恢复失败但仍亮着时，开关继续投影为开启。 */
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
            /* 手动关闭成功指到达最低可见亮度，不代表物理黑屏。 */
            ok = ((bl_err == D_OK) &&
                  (snapshot.brightness_percent ==
                   PRODUCT_UI_BRIGHTNESS_MIN_PERCENT)) ?
                     1U :
                     0U;
            if (ok != 0U) {
                snapshot.brightness_percent =
                    PRODUCT_UI_BRIGHTNESS_MIN_PERCENT;
                /* 开关外观为关闭，滑条在最低亮度仍可操作。 */
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
        /* 连续亮度命令不调成黑屏；物理零亮度由灭屏所有者管理，
         * 不能通过亮度滑条命令进入。 */
        if (value < PRODUCT_UI_BRIGHTNESS_MIN_PERCENT) {
            value = PRODUCT_UI_BRIGHTNESS_MIN_PERCENT;
        }
        was_off = d_screen_power_is_off();
        if (was_off != 0U) {
            /* 只清除灭屏状态后应用一次目标，避免先恢复旧非零亮度造成闪亮。 */
            d_screen_power_clear_off();
        }
        if (d_display_set_brightness(value) != D_OK) {
            if (was_off != 0U) {
                /* 亮度唤醒失败时恢复灭屏状态及黑屏。 */
                d_screen_power_force_off();
            }
            snapshot.brightness_percent = d_display_get_brightness();
            /* 与安全调暗开关一致：最低可见亮度外观为关闭，更高为开启；
             * 最低值仍为非零 PWM，不能仅用大于零判断。 */
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
            /* 最低亮度与安全调暗关闭使用相同的开关外观。 */
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

/**
 * @brief 从显示软件状态建立设置投影并注册命令；失败撤销本次命令绑定。
 */
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
    /* 显示先于设置初始化，当前读取显示适配器的软件状态。 */
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
    /* 连接域由 product_ui_register 独立初始化。 */
    return 0;
}

#endif /* Product UI 的 GC9307 配置。 */
