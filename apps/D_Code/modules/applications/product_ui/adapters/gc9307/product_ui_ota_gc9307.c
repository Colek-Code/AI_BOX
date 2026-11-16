/** @file product_ui_ota_gc9307.c @brief 维护协调器按所有者依次收敛，不嵌套持有它们的锁。 */
#include "config/app_config.h"
#if D_FEATURE_OTA && defined(D_LVGL_ENABLE) && D_UI_TARGET == D_UI_PRODUCT
#include "product_ui_ota_gc9307.h"
#if D_PRODUCT_MUSIC_ENABLE
#include "modules/applications/product_ui/features/media/product_ui_media_runtime.h"
#endif
#include "modules/services/ota/ota_service.h"
#include "system/includes.h"
#include "os/os_api.h"
#if D_FEATURE_WEBSOCKET
#include "modules/services/websocket/ws_service.h"
static uint8_t s_websocket_held;
#endif
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

/**
 * @brief 依次取得维护占用并关闭新业务入口，标记已成功取得的所有者。
 * @return 全部受理返回 D_OK；正在关机或任一所有者拒绝时返回对应错误。
 * @note 失败可能已持有前序资源；由 OTA 调用 release 按已取得标志恢复，不能把失败解释为毫无副作用。
 */
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
#if D_PRODUCT_MUSIC_ENABLE
    product_ui_media_maintenance_begin();
#endif
#if D_FEATURE_WEBSOCKET
    result = ws_service_maintenance_begin(WS_HOLD_OTA);
    if (result != D_OK) return result;
    s_websocket_held = 1U;
#endif
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

/**
 * @brief 从统一起点计算当前排空阶段可用的剩余毫秒数。
 * @param[in] started 开始维护等待时的回绕毫秒计数。
 * @param[in] timeout 整个等待过程的总毫秒预算。
 * @return 已超时返回零，否则为剩余毫秒数。
 */
static uint32_t product_ui_ota_remaining(uint32_t started, uint32_t timeout)
{
    uint32_t elapsed = (uint32_t)timer_get_ms() - started;
    return elapsed >= timeout ? 0U : timeout - elapsed;
}

/**
 * @brief 用同一总预算依次等待媒体、传输、会话、相机、音量和持久化空闲。
 * @param[in] timeout 整个排空过程的总等待上限，单位毫秒。
 * @return 所有等待完成返回 D_OK；首个错误或超时原样返回，维护占用仍保留。
 */
static d_err_t product_ui_ota_wait(uint32_t timeout)
{
    uint32_t started = (uint32_t)timer_get_ms();
    d_err_t result;
#if D_PRODUCT_MUSIC_ENABLE
    result = product_ui_media_maintenance_wait(product_ui_ota_remaining(started, timeout));
    if (result != D_OK) return result;
#endif
#if D_FEATURE_WEBSOCKET
    {
        uint8_t ready = 0U;
        while (!ready) {
            result = ws_service_maintenance_poll(&ready);
            if (result != D_OK) return result;
            if (ready) break;
            if (!product_ui_ota_remaining(started, timeout)) return D_ERR_TIMEOUT;
            os_time_dly(1);
        }
    }
#endif
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

/**
 * @brief 先确认在途工作已结束，再按占用标志恢复各所有者的受理能力。
 * @return 全部释放返回 D_OK；在途资源未结束返回 D_ERR_BUSY，其他释放失败原样返回。
 * @note 失败保存可在恢复后重试；仍在写入或音频停止中的所有者必须保留，不将超时当作资源已经释放。
 */
static d_err_t product_ui_ota_release(void)
{
#if D_PRODUCT_MUSIC_ENABLE
    if (product_ui_media_maintenance_wait(0U) != D_OK) return D_ERR_BUSY;
#endif
#if D_FEATURE_WEBSOCKET
    /* 释放其他所有者前先检查：未结束的传输仍须保留 OTA 占用。 */
    if (s_websocket_held) {
        uint8_t ready;
        d_err_t result = ws_service_maintenance_poll(&ready);
        if (result != D_OK || !ready) return D_ERR_BUSY;
    }
#endif
    if (s_devices_held) {
        product_ui_alarm_snapshot_t alarm;
        product_ui_pomodoro_snapshot_t pomodoro;
        product_ui_appearance_snapshot_t appearance;
        product_ui_alarm_get_snapshot(&alarm);
        product_ui_pomodoro_get_snapshot(&pomodoro);
        product_ui_appearance_get_snapshot(&appearance);
        /* 保存失败可在恢复后重试；在途写入或音频停止必须继续保留其所有者至真正完成。 */
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
#if D_FEATURE_WEBSOCKET
    if (s_websocket_held) {
        d_err_t result = ws_service_maintenance_release(WS_HOLD_OTA);
        if (result != D_OK) return result;
        s_websocket_held = 0U;
    }
#endif
#if D_PRODUCT_MUSIC_ENABLE
    return product_ui_media_maintenance_release();
#else
    return D_OK;
#endif
}

/**
 * @brief 注册由静态描述承载的产品维护协调器。
 */
d_err_t product_ui_ota_register_backend(void)
{
    static const ota_maintenance_backend_t backend = {
        product_ui_ota_begin, product_ui_ota_wait, product_ui_ota_release
    };
    return ota_service_register_maintenance(&backend);
}
#endif
