/**
 * @file d_service_startup.c
 * @brief Starts required and optional D_Code services in a visible order.
 */

#include "config/app_config.h"
#include "d_service_startup.h"

#include "framework/d_error.h"
#include "framework/d_log.h"

#if D_FEATURE_KEY_SERVICE
#include "modules/services/key/d_key_service.h"
#endif
#if D_FEATURE_POWER_CONTROL
#include "modules/services/power/power_ctrl.h"
#endif
#if D_FEATURE_WIFI
#include "modules/services/wifi/wifi_service.h"
#endif
#if D_FEATURE_OTA
#include "modules/services/ota/ota_service.h"
#endif
#if D_FEATURE_BLE_PROVISION
#include "modules/services/ble_provisioning/ble_service.h"
#include "modules/services/ble_provisioning/d_ble_net_cfg.h"
#endif
#if D_FEATURE_AI_CHAT
#include "modules/applications/ai_chat/ai_service.h"
#endif
#if D_FEATURE_LOCAL_MUSIC
#include "modules/services/local_music/music_service.h"
#endif
#if D_DEVICE_CLOUD_ENABLE
#include "modules/services/provisioning/device_credentials.h"
#include "modules/services/provisioning/provision_service.h"
#include "modules/services/websocket/ws_service.h"
#endif
#if D_FEATURE_RTC_AUDIO
#include "modules/diagnostics/audio/rtc_loopback/rtc_audio_service.h"
#endif
#if D_FEATURE_BRTC
#include "modules/integrations/baidu_rtc/brtc_service.h"
#endif
#if D_FEATURE_QWEN
#include "modules/integrations/qwen/qwen_service.h"
#endif
#if D_FEATURE_QWEN || D_LOCAL_PROMPT_ENABLE
#include "modules/services/audio/d_device_volume.h"
#endif
#if D_LOCAL_PROMPT_ENABLE
#include "modules/services/audio/d_local_prompt.h"
#endif
#if D_FEATURE_XIAOLE_WAKEUP
#include "modules/integrations/xiaole_wakeup/xiaole_wakeup.h"
#endif

#define TAG "SERVICE_LIST"

#if D_CAMERA_PREVIEW_ENABLE
#include "modules/services/camera/camera_preview.h"
#endif

#if defined(D_LVGL_ENABLE) && ((D_UI_TARGET == D_UI_PRODUCT) || (D_UI_TARGET == D_UI_CIPHER_PANEL))
#include "modules/applications/qwen_subtitle/qwen_subtitle_text.h"
#endif

typedef d_err_t (*d_service_init_fn_t)(void);

typedef enum {
    D_SERVICE_POLICY_REQUIRED = 0,
    D_SERVICE_POLICY_OPTIONAL,
} d_service_policy_e;

typedef struct {
    const char *name;
    d_service_init_fn_t init;
    d_service_policy_e policy;
} d_service_entry_t;

#if D_FEATURE_BLE_PROVISION
static d_err_t d_service_start_ble(void)
{
    d_err_t err = ble_service_init();

    if (err == D_OK) {
        bt_ble_module_init();
    }
    return err;
}
#endif

#if D_FEATURE_AI_CHAT
static d_err_t d_service_start_ai_chat(void)
{
    static const ai_service_config_t config = {
        .api_key = D_AI_API_KEY,
        .api_host = D_AI_HOST,
        .api_path = D_AI_PATH,
        .model = D_AI_MODEL,
        .timeout_ms = D_AI_TIMEOUT_MS,
        .max_retries = D_AI_MAX_RETRIES,
    };

    return ai_service_init(&config);
}
#endif

#if D_DEVICE_CLOUD_ENABLE
static d_err_t d_service_start_websocket(void)
{
    d_err_t err = ws_service_init();

#if D_FEATURE_AI_CHAT
    if (err == D_OK) {
        err = ai_service_ws_bridge_init();
    }
#endif
    return err;
}
#endif

#if D_FEATURE_RTC_AUDIO
static d_err_t d_service_start_rtc_audio(void)
{
    d_err_t err = rtc_audio_service_init();

    if (err != D_OK) {
        return err;
    }

    err = rtc_audio_loopback_start();
    if (err != D_OK) {
        LOGE(TAG, "RTC audio loopback start failed: %d", err);
    }
    /* The service itself is ready even when the optional loopback test fails. */
    return D_OK;
}
#endif

/* Base device controls are required; feature services may start in degraded mode. */
static const d_service_entry_t g_services[] = {
#if D_CAMERA_PREVIEW_ENABLE
    { "Camera preview", camera_preview_init, D_SERVICE_POLICY_OPTIONAL },
#endif
#if D_FEATURE_KEY_SERVICE
    { "Key", d_key_service_init, D_SERVICE_POLICY_REQUIRED },
#endif
#if D_FEATURE_POWER_CONTROL
    { "Power control", power_ctrl_init, D_SERVICE_POLICY_REQUIRED },
#endif
#if D_FEATURE_WIFI
    { "WiFi", wifi_service_init, D_SERVICE_POLICY_OPTIONAL },
#endif
#if D_FEATURE_OTA
    { "HTTP OTA", ota_service_init, D_SERVICE_POLICY_OPTIONAL },
#endif
#if D_FEATURE_BLE_PROVISION
    { "BLE provisioning", d_service_start_ble, D_SERVICE_POLICY_OPTIONAL },
#endif
#if D_FEATURE_AI_CHAT
    { "AI chat", d_service_start_ai_chat, D_SERVICE_POLICY_OPTIONAL },
#endif
#if D_FEATURE_LOCAL_MUSIC
    { "Local music", music_service_init, D_SERVICE_POLICY_OPTIONAL },
#endif
#if D_DEVICE_CLOUD_ENABLE
    { "Device credentials", cred_init, D_SERVICE_POLICY_OPTIONAL },
    { "Provisioning", provision_service_init, D_SERVICE_POLICY_OPTIONAL },
#if D_FEATURE_AI_CHAT
    { "WebSocket + AI bridge", d_service_start_websocket, D_SERVICE_POLICY_OPTIONAL },
#else
    { "WebSocket", d_service_start_websocket, D_SERVICE_POLICY_OPTIONAL },
#endif
#endif
#if D_FEATURE_RTC_AUDIO
    { "RTC audio", d_service_start_rtc_audio, D_SERVICE_POLICY_OPTIONAL },
#endif
#if D_FEATURE_BRTC
    { "BRTC", brtc_service_init, D_SERVICE_POLICY_OPTIONAL },
#endif
#if D_FEATURE_QWEN || D_LOCAL_PROMPT_ENABLE
    { "Device volume", d_device_volume_service_init, D_SERVICE_POLICY_OPTIONAL },
#endif
#if D_LOCAL_PROMPT_ENABLE
    { "Local prompt", d_local_prompt_init, D_SERVICE_POLICY_OPTIONAL },
#endif
#if D_FEATURE_QWEN
#if defined(D_LVGL_ENABLE) && ((D_UI_TARGET == D_UI_PRODUCT) || (D_UI_TARGET == D_UI_CIPHER_PANEL))
    { "Qwen subtitles", qwen_subtitle_init, D_SERVICE_POLICY_OPTIONAL },
#endif
    { "Qwen", qwen_service_init, D_SERVICE_POLICY_OPTIONAL },
#endif
#if D_FEATURE_XIAOLE_WAKEUP
    { "XiaoLe wakeup", xiaole_wakeup_init, D_SERVICE_POLICY_OPTIONAL },
#endif
    { 0, 0, D_SERVICE_POLICY_OPTIONAL },
};

d_service_start_result_t d_service_start_all(void)
{
    const d_service_entry_t *entry = g_services;
    d_service_start_result_t result = {
        .started_count = 0U,
        .required_failure_count = 0U,
        .optional_failure_count = 0U,
        .first_required_error = D_OK,
        .first_optional_error = D_OK,
    };

    while (entry->init != 0) {
        d_err_t err = entry->init();

        if (err == D_OK) {
            result.started_count++;
            LOGI(TAG, "  OK %s", entry->name);
        } else if (entry->policy == D_SERVICE_POLICY_REQUIRED) {
            if (result.required_failure_count == 0U) {
                result.first_required_error = err;
            }
            result.required_failure_count++;
            LOGE(TAG, "required service %s init failed: %d", entry->name, err);
        } else {
            if (result.optional_failure_count == 0U) {
                result.first_optional_error = err;
            }
            result.optional_failure_count++;
            LOGW(TAG, "optional service %s unavailable: %d", entry->name, err);
        }
        entry++;
    }

    return result;
}
