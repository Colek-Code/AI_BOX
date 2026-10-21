/** Product maintenance coordinator. Owners drain themselves without nested locks. */
#include "config/app_config.h"
#if D_FEATURE_OTA && defined(D_LVGL_ENABLE) && D_UI_TARGET == D_UI_PRODUCT
#include "product_ui_ota_gc9307.h"
#include "modules/services/ota/ota_service.h"
#include "system/includes.h"
#include "os/os_api.h"
#include "modules/services/wifi/wifi_service.h"
#include "modules/services/audio/d_device_volume.h"
#include "modules/services/audio/d_local_prompt.h"
#include "modules/applications/product_ui/features/time/product_ui_alarm_service.h"
#include "modules/applications/product_ui/features/time/product_ui_pomodoro_service.h"
#include "modules/applications/product_ui/runtime/preferences/product_ui_appearance.h"
#if D_FEATURE_QWEN
#include "modules/integrations/qwen/qwen_service.h"
#endif
#if D_CAMERA_PREVIEW_ENABLE
#include "modules/services/camera/camera_preview.h"
#endif
#if D_FEATURE_POWER_CONTROL
#include "modules/services/power/power_ctrl.h"
#endif

static uint8_t s_alarm_held, s_wifi_held, s_qwen_held, s_devices_held;

static d_err_t product_ui_ota_begin(void)
{
    d_err_t result;
#if D_FEATURE_POWER_CONTROL
    if (power_ctrl_is_shutting_down()) return D_ERR_BUSY;
#endif
    result = product_ui_alarm_maintenance_begin();
    if (result != D_OK) return result;
    s_alarm_held = 1U;
    result = wifi_service_configuration_hold_begin();
    if (result != D_OK) return result;
    s_wifi_held = 1U;
    product_ui_pomodoro_quiesce_begin();
    d_local_prompt_quiesce_begin();
    d_device_volume_quiesce_begin();
    product_ui_appearance_quiesce_begin();
#if D_CAMERA_PREVIEW_ENABLE
    camera_preview_shutdown_begin();
#endif
    s_devices_held = 1U;
#if D_FEATURE_QWEN
    result = qwen_service_maintenance_begin();
    if (result != D_OK) return result;
    s_qwen_held = 1U;
#endif
    return D_OK;
}

static uint32_t product_ui_ota_remaining(uint32_t started, uint32_t timeout)
{
    uint32_t elapsed = (uint32_t)timer_get_ms() - started;
    return elapsed >= timeout ? 0U : timeout - elapsed;
}

static d_err_t product_ui_ota_wait(uint32_t timeout)
{
    uint32_t started = (uint32_t)timer_get_ms();
    d_err_t result;
#if D_FEATURE_QWEN
    uint8_t ready = 0U;
    while (!ready) {
        result = qwen_service_maintenance_poll(&ready);
        if (result != D_OK) return result;
        if (ready) break;
        if (!product_ui_ota_remaining(started, timeout)) return D_ERR_TIMEOUT;
        os_time_dly(1);
    }
#endif
#if D_CAMERA_PREVIEW_ENABLE
    result = camera_preview_wait_stopped(product_ui_ota_remaining(started, timeout));
    if (result != D_OK) return result;
#endif
    result = d_device_volume_wait_idle(product_ui_ota_remaining(started, timeout));
    if (result != D_OK) return result;
    result = product_ui_alarm_wait_idle(product_ui_ota_remaining(started, timeout));
    if (result != D_OK) return result;
    result = product_ui_pomodoro_wait_idle(product_ui_ota_remaining(started, timeout));
    if (result != D_OK) return result;
    result = d_local_prompt_wait_stopped(product_ui_ota_remaining(started, timeout));
    if (result != D_OK) return result;
    return product_ui_appearance_wait_idle(product_ui_ota_remaining(started, timeout));
}

static d_err_t product_ui_ota_release(void)
{
    if (s_devices_held) {
        product_ui_alarm_snapshot_t alarm;
        product_ui_pomodoro_snapshot_t pomodoro;
        product_ui_appearance_snapshot_t appearance;
        product_ui_alarm_get_snapshot(&alarm);
        product_ui_pomodoro_get_snapshot(&pomodoro);
        product_ui_appearance_get_snapshot(&appearance);
        /* A failed save can be retried after restoration, but an in-flight
         * write or audio stop must retain its owner until it completes. */
        if (alarm.saving || pomodoro.saving || appearance.is_in_progress ||
            appearance.is_pending || d_device_volume_wait_idle(0U) != D_OK ||
            d_local_prompt_wait_stopped(0U) != D_OK) return D_ERR_BUSY;
#if D_CAMERA_PREVIEW_ENABLE
        if (camera_preview_wait_stopped(0U) != D_OK) return D_ERR_BUSY;
#endif
    }
#if D_FEATURE_QWEN
    if (s_qwen_held) {
        d_err_t result = qwen_service_maintenance_release();
        if (result != D_OK) return result;
        s_qwen_held = 0U;
    }
#endif
    if (s_devices_held) {
#if D_CAMERA_PREVIEW_ENABLE
        camera_preview_shutdown_abort();
#endif
        product_ui_appearance_quiesce_cancel();
        d_device_volume_quiesce_cancel();
        d_local_prompt_quiesce_cancel();
        product_ui_pomodoro_quiesce_cancel();
        s_devices_held = 0U;
    }
    if (s_alarm_held) { product_ui_alarm_quiesce_cancel(); s_alarm_held = 0U; }
    if (s_wifi_held) { wifi_service_configuration_hold_end(); s_wifi_held = 0U; }
    return D_OK;
}

d_err_t product_ui_ota_register_backend(void)
{
    static const ota_maintenance_backend_t backend = {
        product_ui_ota_begin, product_ui_ota_wait, product_ui_ota_release
    };
    return ota_service_register_maintenance(&backend);
}
#endif
