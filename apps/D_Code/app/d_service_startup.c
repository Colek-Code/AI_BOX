/**
 * @file d_service_startup.c
 * @brief 按显式清单装配必需与可选服务，属于启动层，不接管服务状态机。
 */

#include "config/app_config.h"
#include "d_service_startup.h"
#if D_PRODUCT_MUSIC_ENABLE
#include "modules/applications/product_ui/features/media/product_ui_media_runtime.h"
#endif
#if D_FEATURE_WEBSOCKET
#include "modules/services/websocket/ws_service.h"
#endif

#include "framework/d_error.h"
#include "framework/d_log.h"

#if D_FEATURE_KEY_SERVICE
#include "modules/services/key/d_key_service.h"
#include "modules/applications/device_control/product_key_bindings.h"
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
#if D_FEATURE_RTC_AUDIO
#include "modules/diagnostics/audio/rtc_loopback/rtc_audio_service.h"
#endif
#if D_FEATURE_BRTC
#include "modules/integrations/baidu_rtc/brtc_service.h"
#endif
#if D_FEATURE_QWEN
#include "modules/integrations/qwen/qwen_service.h"
#if D_FEATURE_AI_ALARM
#include "modules/applications/product_ui/features/time/product_ui_alarm_tool.h"
#endif
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
/**
 * @brief 初始化 BLE 服务，成功后才启动 SDK BLE 模块。
 * @return BLE 服务初始化结果；失败时不调用 bt_ble_module_init。
 */
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
/**
 * @brief 用静态部署配置初始化文本 AI 服务。
 * @return ai_service_init 返回的初始化结果；配置生命周期为整个程序。
 */
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

#if D_FEATURE_RTC_AUDIO
/**
 * @brief 初始化 RTC 音频服务，并尝试启动可选回环测试。
 * @return 服务初始化失败时返回原错误；服务就绪后即使回环失败也返回 D_OK。
 */
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
    /* 可选回环测试失败不影响音频服务已初始化的事实。 */
    return D_OK;
}
#endif

/* 基础设备控制属于必需能力；可选功能失败允许降级启动。 */
#if D_FEATURE_LOCAL_MUSIC && !D_PRODUCT_MUSIC_ENABLE
/**
 * @brief 为未启用产品媒体协调的独立音乐配置提供固定准入。
 * @return 始终返回 D_OK；不表示产品音频优先级检查已执行。
 */
static d_err_t d_service_music_standalone_permit(void) { return D_OK; }
/**
 * @brief 配置独立音乐的曲库上限、循环和格式，再初始化音乐服务。
 * @return 配置失败时返回其错误；否则返回 music_service_init 的结果。
 */
static d_err_t d_service_start_standalone_music(void)
{
    static const music_config_t config = {200U, 1U, MUSIC_FORMAT_MP3 | MUSIC_FORMAT_WAV,
        d_service_music_standalone_permit, NULL, NULL};
    d_err_t result = music_service_configure(&config);
    return result == D_OK ? music_service_init() : result;
}
#endif

static const d_service_entry_t g_services[] = {
#if D_PRODUCT_MUSIC_ENABLE
    { "Media policy", product_ui_media_runtime_bind, D_SERVICE_POLICY_REQUIRED },
    { "Device volume", d_device_volume_service_init, D_SERVICE_POLICY_OPTIONAL },
#endif
#if D_FEATURE_WEBSOCKET
    { "WebSocket", ws_service_init, D_SERVICE_POLICY_OPTIONAL },
#endif
#if D_CAMERA_PREVIEW_ENABLE
    { "Camera preview", camera_preview_init, D_SERVICE_POLICY_OPTIONAL },
#endif
#if D_FEATURE_KEY_SERVICE
    { "Key", d_key_service_init, D_SERVICE_POLICY_REQUIRED },
    { "Product keys", product_key_bindings_init, D_SERVICE_POLICY_REQUIRED },
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
#if D_PRODUCT_MUSIC_ENABLE
    { "Local music", music_service_init, D_SERVICE_POLICY_OPTIONAL },
#else
    { "Local music", d_service_start_standalone_music, D_SERVICE_POLICY_OPTIONAL },
#endif
#endif
#if D_FEATURE_RTC_AUDIO
    { "RTC audio", d_service_start_rtc_audio, D_SERVICE_POLICY_OPTIONAL },
#endif
#if D_FEATURE_BRTC
    { "BRTC", brtc_service_init, D_SERVICE_POLICY_OPTIONAL },
#endif
#if (D_FEATURE_QWEN || D_LOCAL_PROMPT_ENABLE) && !D_PRODUCT_MUSIC_ENABLE
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
#if D_PRODUCT_MUSIC_ENABLE
    { "Media ready", product_ui_media_runtime_start, D_SERVICE_POLICY_OPTIONAL },
#endif
    { 0, 0, D_SERVICE_POLICY_OPTIONAL },
};

/**
 * @brief 遍历启动清单并汇总结果；完整接口合同见 d_service_startup.h。
 */
d_service_start_result_t d_service_start_all(void)
{
#if D_FEATURE_AI_ALARM
    qwen_local_tool_bind(product_ui_alarm_tool_port());
#endif
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
