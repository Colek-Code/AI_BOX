/**
 * @file d_config_report.c
 * @brief 在启动阶段报告解析后的板型、界面及功能组合，属于配置诊断，不改变配置。
 */

#include "app_config.h"
#include "d_config_report.h"

#include "framework/d_log.h"

#define TAG "CONFIG"

/**
 * @brief 取得编译期板型对应的诊断名称。
 * @return 静态字符串，不转移所有权。
 */
static const char *d_config_board_name(void)
{
#if D_BOARD_TARGET == D_BOARD_COPY_GC9307_HYN
    return "Copy GC9307/HYN";
#else
    return "Copy EMI ILI948x";
#endif
}

/**
 * @brief 取得编译期根界面对应的诊断名称。
 * @return 静态字符串，不转移所有权。
 */
const char *d_config_ui_name(void)
{
#if D_UI_TARGET == D_UI_PRODUCT
    return "Product UI";
#elif D_UI_TARGET == D_UI_TABLET_SHELL
    return "TabletShell";
#elif D_UI_TARGET == D_UI_SCREEN_TEST
    return "ScreenTest";
#elif D_UI_TARGET == D_UI_CIPHER_PANEL
    return "CipherPanel";
#elif D_UI_TARGET == D_UI_LAUNCHER_DEMO
    return "LauncherDemo";
#else
    return "LightSpotDemo";
#endif
}

/**
 * @brief 输出当前已解析配置；接口合同见 d_config_report.h。
 */
void d_config_report_print(void)
{
    LOGI(TAG, "Board: %s", d_config_board_name());
    LOGI(TAG, "UI: %s", d_config_ui_name());
    LOGI(TAG, "Screen default orientation=%d (saved user direction takes precedence)",
         D_SCREEN_DEFAULT_ROTATION_DEGREES);
    LOGI(TAG, "Generic WebSocket=%d (explicit start)", D_FEATURE_WEBSOCKET);
    LOGI(TAG, "AI alarm=%d mutations=%d", D_FEATURE_AI_ALARM, D_AI_ALARM_MUTATIONS);
#if D_FEATURE_QWEN
    LOGI(TAG, "Qwen WSS diagnostic=%d", D_QWEN_WSS_DIAG_ENABLE);
#endif
    LOGI(TAG, "Features: key=%d power=%d wifi=%d ble=%d ota=%d",
         D_FEATURE_KEY_SERVICE,
         D_FEATURE_POWER_CONTROL,
         D_FEATURE_WIFI,
         D_FEATURE_BLE_PROVISION,
         D_FEATURE_OTA);
    LOGI(TAG, "AI/audio: chat=%d qwen=%d xiaole=%d brtc=%d rtc=%d music=%d",
         D_FEATURE_AI_CHAT,
         D_FEATURE_QWEN,
         D_FEATURE_XIAOLE_WAKEUP,
         D_FEATURE_BRTC,
         D_FEATURE_RTC_AUDIO,
         D_FEATURE_LOCAL_MUSIC);
#if D_FEATURE_QWEN
    LOGI(TAG, "Qwen auth=%s",
         D_QWEN_AUTH_MODE == D_QWEN_AUTH_MODE_FULL_MANAGED ? "full-managed" : "semi-managed");
#endif
}
