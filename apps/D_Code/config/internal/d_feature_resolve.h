/**
 * @file d_feature_resolve.h
 * @brief Resolve application features into JieLi SDK dependency macros.
 *
 * Application developers configure board_config.h and app_config.h. Hardware
 * ownership is kept separately in d_board_resolve.h.
 */

#ifndef D_FEATURE_RESOLVE_H
#define D_FEATURE_RESOLVE_H

/* WiFi -> network stack. */
#if D_FEATURE_WIFI
#define CONFIG_WIFI_ENABLE
#define CONFIG_NET_ENABLE
#endif

/* Device platform -> network, TLS and HTTP client. Qwen resolves its own below. */
#if D_DEVICE_CLOUD_ENABLE
#define CONFIG_NET_ENABLE
#define CONFIG_MBEDTLS_ENABLE
#define CONFIG_HTTP_CLIENT_ENABLE
#endif

/* BLE provisioning -> BLE-only controller profile. */
#if D_FEATURE_BLE_PROVISION
#define CONFIG_BT_ENABLE
#define TCFG_USER_BLE_ENABLE              1
#define TCFG_USER_BT_CLASSIC_ENABLE       0
#define TCFG_BLE_SECURITY_EN              0
#define CONFIG_POWER_ON_ENABLE_BLE        0
#define BT_NET_CFG_EN                     1
#define BT_NET_CFG_DUI_EN                 0
#define BT_NET_CFG_TURING_EN              0
#define BT_NET_CFG_TENCENT_EN             0
#define BT_NET_CFG_QYAI_EN                0
#define TRANS_DATA_EN                     0
#define XIAOMI_EN                         0
#define BT_NET_CENTRAL_EN                 0
#define BT_NET_HID_EN                     0
#define TRANS_MULTI_BLE_EN                0
#define APP_NONCONN_24G                   0
#define CONFIG_BLE_MESH_ENABLE            0
#endif

/* Optional text and camera applications. */
#if D_FEATURE_AI_CHAT
#define CONFIG_NET_ENABLE
#define CONFIG_MBEDTLS_ENABLE
#define CONFIG_HTTP_CLIENT_ENABLE
#endif

#if D_FEATURE_CAMERA
#define D_VIDEO_ENABLE
#if (D_UI_TARGET == D_UI_PRODUCT) && (D_BOARD_TARGET == D_BOARD_COPY_GC9307_HYN)
#define D_CAMERA_PREVIEW_ENABLE 1
#else
#define D_CAMERA_DEMO
#endif
#endif

#ifndef D_CAMERA_PREVIEW_ENABLE
#define D_CAMERA_PREVIEW_ENABLE 0
#endif

/* ISP0 uses iic0 on the original connector; Product preview gives touch its own bus. */
#if D_CAMERA_PREVIEW_ENABLE
#define D_HYN_IIC_DEVICE_NAME "iic1"
#else
#define D_HYN_IIC_DEVICE_NAME "iic0"
#endif

/* Product reminders remain audible without a cloud voice agent. */
#if D_UI_TARGET == D_UI_PRODUCT
#define D_LOCAL_PROMPT_ENABLE 1
#define CONFIG_AUDIO_ENABLE
#else
#define D_LOCAL_PROMPT_ENABLE 0
#endif

/* Voice agents -> required SDK infrastructure. */
#if D_FEATURE_BRTC
/* Two BRTC port files retain the vendor-facing legacy guard. */
#define D_MODULE_BRTC_AGENT_ENABLE
#define CONFIG_NET_ENABLE
#define CONFIG_AUDIO_ENABLE
#define CONFIG_HTTP_CLIENT_ENABLE
#endif

#if D_FEATURE_QWEN
#define CONFIG_NET_ENABLE
#define CONFIG_AUDIO_ENABLE
#define CONFIG_MBEDTLS_ENABLE
#define CONFIG_HTTP_CLIENT_ENABLE
#endif

#if D_FEATURE_XIAOLE_WAKEUP
#define CONFIG_AUDIO_ENABLE
#endif

#if D_FEATURE_RTC_AUDIO
#define CONFIG_AUDIO_ENABLE
#define CONFIG_VAD_ENC_ENABLE
#if D_RTC_AUDIO_CODEC == D_RTC_AUDIO_CODEC_OPUS
#define CONFIG_OPUS_ENC_ENABLE
#define CONFIG_OPUS_DEC_ENABLE
#endif
#endif

/* Local music -> audio server and local-music mode. */
#if D_FEATURE_LOCAL_MUSIC
#define CONFIG_AUDIO_ENABLE
#define CONFIG_LOCAL_MUSIC_MODE_ENABLE
#endif
#ifdef D_AUDIO_DEMO
#define CONFIG_AUDIO_ENABLE
#endif
#ifdef D_LOCAL_MUSIC_DEMO
#define CONFIG_LOCAL_MUSIC_MODE_ENABLE
#endif

/* OTA -> network and HTTP client. */
#if D_FEATURE_OTA
#define CONFIG_NET_ENABLE
#define CONFIG_HTTP_CLIENT_ENABLE
#endif

/* Standalone network tests may still be enabled through compiler flags. */
#if defined(D_TCP_CLIENT_TEST) || defined(D_TCP_SERVER_TEST) || \
    defined(D_UDP_TEST) || defined(D_HTTP_TEST) || defined(D_MQTT_TEST)
#define CONFIG_NET_ENABLE
#endif

#endif /* D_FEATURE_RESOLVE_H */
