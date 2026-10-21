/**
 * @file product_ui_settings_service.c
 * @brief Honest Product UI settings adapter for the active hardware profile.
 */

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

#include "modules/applications/product_ui/features/settings/product_ui_settings_service.h"
#include "modules/applications/product_ui/runtime/service_registry/product_ui_service_registry.h"
#include "os/os_api.h"
#include <string.h>

#define PRODUCT_UI_SETTING_CAP_ALL \
    (PRODUCT_UI_SETTING_CAP_BACKLIGHT_SWITCH | \
     PRODUCT_UI_SETTING_CAP_BRIGHTNESS | \
     PRODUCT_UI_SETTING_CAP_VOLUME | \
     PRODUCT_UI_SETTING_CAP_WALLPAPER | \
     PRODUCT_UI_SETTING_CAP_TIME | \
     PRODUCT_UI_SETTING_CAP_WIFI | \
     PRODUCT_UI_SETTING_CAP_BLUETOOTH | PRODUCT_UI_SETTING_CAP_REDUCE_MOTION)

static OS_MUTEX s_settings_mutex;
static uint8_t s_settings_mutex_ready;
static product_ui_settings_snapshot_t s_settings;

static void product_ui_settings_project_appearance(product_ui_settings_snapshot_t *snapshot)
{
    const uint32_t mask = PRODUCT_UI_SETTING_CAP_WALLPAPER | PRODUCT_UI_SETTING_CAP_REDUCE_MOTION;
    product_ui_appearance_get_snapshot(&snapshot->appearance);
    snapshot->wallpaper_id = snapshot->appearance.wallpaper_id;
    snapshot->capability_mask &= ~mask;
    snapshot->valid_mask &= ~mask;
    if (snapshot->appearance.is_initialized) {
        snapshot->capability_mask |= mask;
        snapshot->valid_mask |= mask;
    }
}

static uint8_t product_ui_settings_store_locked(product_ui_settings_snapshot_t *next)
{
    next->revision = s_settings.revision;
    if (memcmp(&s_settings, next, sizeof(*next)) == 0) return 0U;
    next->revision++;
    if (next->revision == 0U) next->revision = 1U;
    s_settings = *next;
    return 1U;
}

static uint8_t product_ui_settings_snapshot_valid(
    const product_ui_settings_snapshot_t *snapshot)
{
    if ((snapshot == NULL) ||
        ((snapshot->capability_mask & ~PRODUCT_UI_SETTING_CAP_ALL) != 0U) ||
        ((snapshot->valid_mask & ~snapshot->capability_mask) != 0U) ||
        (snapshot->backlight_enabled > 1U) ||
        (snapshot->brightness_percent > 100U) ||
        (snapshot->volume_percent > 100U) ||
        (snapshot->volume_target_percent > 100U) ||
        (snapshot->volume_state < PRODUCT_UI_VOLUME_READY) ||
        (snapshot->volume_state > PRODUCT_UI_VOLUME_QUIESCING)) {
        return 0U;
    }
    return 1U;
}

int product_ui_settings_service_init(void)
{
    if (!s_settings_mutex_ready) {
        if (os_mutex_create(&s_settings_mutex) != OS_NO_ERR) {
            return -1;
        }
        s_settings_mutex_ready = 1;
    }
    os_mutex_pend(&s_settings_mutex, 0);
    memset(&s_settings, 0, sizeof(s_settings));
    s_settings.revision = 1U;
    os_mutex_post(&s_settings_mutex);
    return 0;
}

int product_ui_settings_service_publish(
    const product_ui_settings_snapshot_t *snapshot)
{
    product_ui_settings_snapshot_t next;
    uint8_t is_changed;

    if (!s_settings_mutex_ready ||
        !product_ui_settings_snapshot_valid(snapshot)) {
        return -1;
    }
    next = *snapshot;
    os_mutex_pend(&s_settings_mutex, 0);
    /* Appearance is a projection, never a field owned by hardware publishers. */
    product_ui_settings_project_appearance(&next);
    is_changed = product_ui_settings_store_locked(&next);
    os_mutex_post(&s_settings_mutex);
    if (is_changed) (void)product_ui_service_data_changed(PRODUCT_UI_SERVICE_SETTINGS);
    return 0;
}

int product_ui_settings_service_snapshot(product_ui_settings_snapshot_t *snapshot)
{
    product_ui_settings_snapshot_t next;
    uint8_t is_changed;
    if (!s_settings_mutex_ready || (snapshot == NULL)) {
        return -1;
    }
    os_mutex_pend(&s_settings_mutex, 0);
    next = s_settings;
    product_ui_settings_project_appearance(&next);
    is_changed = product_ui_settings_store_locked(&next);
    *snapshot = s_settings;
    os_mutex_post(&s_settings_mutex);
    if (is_changed) (void)product_ui_service_data_changed(PRODUCT_UI_SERVICE_SETTINGS);
    return 0;
}

uint8_t product_ui_settings_volume_display(const product_ui_settings_snapshot_t *snapshot)
{
    if (snapshot == NULL) return 0U;
    if (snapshot->volume_state == PRODUCT_UI_VOLUME_APPLYING ||
        snapshot->volume_state == PRODUCT_UI_VOLUME_TIMEOUT)
        return snapshot->volume_target_percent;
    return snapshot->volume_percent;
}

uint8_t product_ui_settings_volume_can_adjust(const product_ui_settings_snapshot_t *snapshot)
{
    return snapshot != NULL &&
        (snapshot->capability_mask & snapshot->valid_mask & PRODUCT_UI_SETTING_CAP_VOLUME) &&
        snapshot->volume_state != PRODUCT_UI_VOLUME_TIMEOUT &&
        snapshot->volume_state != PRODUCT_UI_VOLUME_QUIESCING &&
        snapshot->volume_state != PRODUCT_UI_VOLUME_UNAVAILABLE;
}

const char *product_ui_settings_volume_status(const product_ui_settings_snapshot_t *snapshot)
{
    if (snapshot == NULL) return "音量控制不可用";
    switch (snapshot->volume_state) {
    case PRODUCT_UI_VOLUME_APPLYING: return "正在调整音量";
    case PRODUCT_UI_VOLUME_ERROR: return "音量调整失败";
    case PRODUCT_UI_VOLUME_TIMEOUT: return "音量设置超时";
    case PRODUCT_UI_VOLUME_UNAVAILABLE: return "声音服务未就绪";
    case PRODUCT_UI_VOLUME_QUIESCING: return "正在关机";
    default: return "设备音量";
    }
}

uint8_t product_ui_settings_appearance_can_save(const product_ui_settings_snapshot_t *snapshot)
{
    if (snapshot == NULL) return 0U;
    return snapshot->appearance.is_service_ready && !snapshot->appearance.is_quiescing &&
        !snapshot->appearance.is_pending && !snapshot->appearance.is_in_progress;
}

const char *product_ui_settings_appearance_status(const product_ui_settings_snapshot_t *snapshot)
{
    if (snapshot == NULL || !snapshot->appearance.is_service_ready) return "外观保存不可用";
    if (snapshot->appearance.is_quiescing) return "正在关机";
    if (snapshot->appearance.is_timed_out) return "保存超时，仍在处理";
    if (snapshot->appearance.is_pending || snapshot->appearance.is_in_progress) return "正在保存";
    if (snapshot->appearance.result != D_OK) return "保存失败，请重试";
    return snapshot->appearance.has_confirmed_record ? "已保存" : "默认外观";
}

int product_ui_settings_service_command(product_ui_settings_command_e command,
                                        uint8_t value)
{
    if (((unsigned)command > PRODUCT_UI_SETTINGS_SET_REDUCE_MOTION) ||
        (((command == PRODUCT_UI_SETTINGS_SET_BRIGHTNESS) ||
          (command == PRODUCT_UI_SETTINGS_SET_VOLUME)) && (value > 100U))) {
        return -1;
    }
    if (command == PRODUCT_UI_SETTINGS_SET_WALLPAPER) {
        return product_ui_appearance_request_wallpaper(value, NULL);
    }
    if (command == PRODUCT_UI_SETTINGS_SET_REDUCE_MOTION) {
        return product_ui_appearance_request_reduce_motion(value, NULL);
    }
    return product_ui_service_command(
        PRODUCT_UI_SERVICE_SETTINGS, (uint16_t)command,
        &value, sizeof(value));
}

#endif /* D_LVGL_ENABLE && (D_UI_TARGET == D_UI_PRODUCT) */
