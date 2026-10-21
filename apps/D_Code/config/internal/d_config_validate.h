#ifndef D_CONFIG_VALIDATE_H
#define D_CONFIG_VALIDATE_H

#include "config/board_config.h"

#if !defined(D_BOARD_TARGET)
#error "请在 board_config.h 中设置 D_BOARD_TARGET"
#endif

#if (D_BOARD_TARGET != D_BOARD_COPY_GC9307_HYN) && \
    (D_BOARD_TARGET != D_BOARD_COPY_EMI_ILI948X)
#error "D_BOARD_TARGET 无效，请使用 board_config.h 中列出的板型"
#endif

#if !defined(D_UI_TARGET)
#error "请在 app_config.h 中设置 D_UI_TARGET"
#endif

#if !defined(D_FEATURE_KEY_SERVICE) || \
    !defined(D_FEATURE_POWER_CONTROL) || \
    !defined(D_FEATURE_WIFI) || \
    !defined(D_FEATURE_BLE_PROVISION) || \
    !defined(D_FEATURE_WEBSOCKET) || \
    !defined(D_FEATURE_AI_CHAT) || \
    !defined(D_FEATURE_CAMERA) || \
    !defined(D_FEATURE_GALLERY) || \
    !defined(D_FEATURE_LOCAL_MUSIC) || \
    !defined(D_FEATURE_QWEN) || \
    !defined(D_FEATURE_XIAOLE_WAKEUP) || \
    !defined(D_FEATURE_BRTC) || \
    !defined(D_FEATURE_RTC_AUDIO) || \
    !defined(D_FEATURE_OTA) || \
    !defined(D_DEBUG_BOARD_LOG) || \
    !defined(D_DEBUG_UI_PERFORMANCE) || \
    !defined(D_DEBUG_VISUAL_CAPTURE)
#error "app_config.h 配置项不完整，请恢复缺少的 D_FEATURE_* 或 D_DEBUG_* 行"
#endif

#if (D_UI_TARGET != D_UI_PRODUCT) && \
    (D_UI_TARGET != D_UI_TABLET_SHELL) && \
    (D_UI_TARGET != D_UI_SCREEN_TEST) && \
    (D_UI_TARGET != D_UI_CIPHER_PANEL) && \
    (D_UI_TARGET != D_UI_LAUNCHER_DEMO) && \
    (D_UI_TARGET != D_UI_LIGHT_SPOT_DEMO)
#error "D_UI_TARGET 无效，请使用 app_config.h 中列出的界面"
#endif

#define D_CONFIG_BOOL_IS_VALID(value) (((value) == 0) || ((value) == 1))

#if !D_CONFIG_BOOL_IS_VALID(D_FEATURE_KEY_SERVICE) || \
    !D_CONFIG_BOOL_IS_VALID(D_FEATURE_POWER_CONTROL) || \
    !D_CONFIG_BOOL_IS_VALID(D_FEATURE_WIFI) || \
    !D_CONFIG_BOOL_IS_VALID(D_FEATURE_BLE_PROVISION) || \
    !D_CONFIG_BOOL_IS_VALID(D_FEATURE_WEBSOCKET) || \
    !D_CONFIG_BOOL_IS_VALID(D_FEATURE_AI_CHAT) || \
    !D_CONFIG_BOOL_IS_VALID(D_FEATURE_CAMERA) || \
    !D_CONFIG_BOOL_IS_VALID(D_FEATURE_GALLERY) || \
    !D_CONFIG_BOOL_IS_VALID(D_FEATURE_LOCAL_MUSIC) || \
    !D_CONFIG_BOOL_IS_VALID(D_FEATURE_QWEN) || \
    !D_CONFIG_BOOL_IS_VALID(D_FEATURE_XIAOLE_WAKEUP) || \
    !D_CONFIG_BOOL_IS_VALID(D_FEATURE_BRTC) || \
    !D_CONFIG_BOOL_IS_VALID(D_FEATURE_RTC_AUDIO) || \
    !D_CONFIG_BOOL_IS_VALID(D_FEATURE_OTA) || \
    !D_CONFIG_BOOL_IS_VALID(D_DEBUG_BOARD_LOG) || \
    !D_CONFIG_BOOL_IS_VALID(D_DEBUG_UI_PERFORMANCE) || \
    !D_CONFIG_BOOL_IS_VALID(D_DEBUG_VISUAL_CAPTURE)
#error "D_FEATURE_* 和 D_DEBUG_* 开关只能填写 0 或 1"
#endif

#if !D_CONFIG_BOOL_IS_VALID(D_OTA_KEEP_VM) || !D_CONFIG_BOOL_IS_VALID(D_OTA_DOUBLE_BANK)
#error "OTA layout and VM policy must be 0 or 1"
#endif
#if !defined(D_OTA_BOOT_VERSION) || (D_OTA_BOOT_VERSION < 0) || (D_OTA_BOOT_VERSION > 65535)
#error "D_OTA_BOOT_VERSION must be within 0..65535"
#endif
#if D_FEATURE_OTA && (!D_OTA_DOUBLE_BANK || D_UI_TARGET != D_UI_PRODUCT || !D_FEATURE_WIFI)
#error "OTA requires the GC9307 Product UI, WiFi and its dual-bank baseline"
#endif

#if ((D_UI_TARGET == D_UI_PRODUCT) || \
     (D_UI_TARGET == D_UI_TABLET_SHELL) || \
     (D_UI_TARGET == D_UI_SCREEN_TEST)) && \
    (D_BOARD_TARGET != D_BOARD_COPY_GC9307_HYN)
#error "Product/TabletShell/ScreenTest 只能搭配 GC9307/HYN 板型"
#endif

#if ((D_UI_TARGET == D_UI_CIPHER_PANEL) || \
     (D_UI_TARGET == D_UI_LAUNCHER_DEMO) || \
     (D_UI_TARGET == D_UI_LIGHT_SPOT_DEMO)) && \
    (D_BOARD_TARGET != D_BOARD_COPY_EMI_ILI948X)
#error "Cipher/Launcher/LightSpot 只能搭配 EMI/ILI948x 板型"
#endif

#if (D_BOARD_TARGET == D_BOARD_COPY_GC9307_HYN) && \
    (D_FEATURE_AI_CHAT || (D_FEATURE_CAMERA && D_UI_TARGET != D_UI_PRODUCT) || D_FEATURE_GALLERY || \
     D_FEATURE_LOCAL_MUSIC || D_FEATURE_BRTC || D_FEATURE_RTC_AUDIO)
#error "GC9307/HYN 当前不支持旧版 AI/相机/相册/本地音乐/BRTC/RTC Audio，请关闭对应开关或改用 EMI 板型"
#endif

#if (D_FEATURE_QWEN + D_FEATURE_BRTC + D_FEATURE_RTC_AUDIO) > 1
#error "Qwen、BRTC 和 RTC Audio 只能启用一个"
#endif

#if D_FEATURE_XIAOLE_WAKEUP && !D_FEATURE_QWEN
#error "启用小乐唤醒时必须同时启用 Qwen"
#endif

#if D_FEATURE_POWER_CONTROL && !D_FEATURE_KEY_SERVICE
#error "启用 Power Control 时必须同时启用 Key Service"
#endif

#if D_FEATURE_QWEN && !D_FEATURE_KEY_SERVICE
#error "启用 Qwen 时必须同时启用 Key Service"
#endif

#if (D_FEATURE_BLE_PROVISION || D_FEATURE_WEBSOCKET || \
     D_FEATURE_AI_CHAT || D_FEATURE_QWEN || D_FEATURE_BRTC || \
     D_FEATURE_OTA) && \
    !D_FEATURE_WIFI
#error "当前启用的网络业务需要同时启用 WiFi"
#endif

#if (D_DEBUG_UI_PERFORMANCE || D_DEBUG_VISUAL_CAPTURE) && \
    (D_UI_TARGET != D_UI_PRODUCT)
#error "UI性能统计和视觉采集只适用于 Product UI"
#endif

#undef D_CONFIG_BOOL_IS_VALID

#endif /* D_CONFIG_VALIDATE_H */
