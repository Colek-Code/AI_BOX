/**
 * @file d_config_report.c
 * @brief Prints the resolved board, UI and feature selection at startup.
 */

#include "app_config.h"
#include "d_config_report.h"

#include "framework/d_log.h"

#define TAG "CONFIG"

static const char *d_config_board_name(void)
{
#if D_BOARD_TARGET == D_BOARD_COPY_GC9307_HYN
    return "Copy GC9307/HYN";
#else
    return "Copy EMI ILI948x";
#endif
}

static const char *d_config_ui_name(void)
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

void d_config_report_print(void)
{
    LOGI(TAG, "Board: %s", d_config_board_name());
    LOGI(TAG, "UI: %s", d_config_ui_name());
    LOGI(TAG, "Features: key=%d power=%d wifi=%d ble=%d ws=%d ota=%d",
         D_FEATURE_KEY_SERVICE,
         D_FEATURE_POWER_CONTROL,
         D_FEATURE_WIFI,
         D_FEATURE_BLE_PROVISION,
         D_DEVICE_CLOUD_ENABLE,
         D_FEATURE_OTA);
    LOGI(TAG, "AI/audio: chat=%d qwen=%d xiaole=%d brtc=%d rtc=%d music=%d",
         D_FEATURE_AI_CHAT,
         D_FEATURE_QWEN,
         D_FEATURE_XIAOLE_WAKEUP,
         D_FEATURE_BRTC,
         D_FEATURE_RTC_AUDIO,
         D_FEATURE_LOCAL_MUSIC);
#if D_FEATURE_QWEN
    LOGI(TAG, "Qwen auth=%s device_cloud=%d",
         D_QWEN_AUTH_MODE == D_QWEN_AUTH_MODE_FULL_MANAGED ? "full-managed" : "semi-managed",
         D_DEVICE_CLOUD_ENABLE);
#endif
}
