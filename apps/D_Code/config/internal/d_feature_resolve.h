/**
 * @file d_feature_resolve.h
 * @brief 将应用功能解析为杰理 SDK 依赖宏。
 * @note 开发者配置 board_config.h 与 app_config.h；硬件归属单独由 d_board_resolve.h 管理。
 */

#ifndef D_FEATURE_RESOLVE_H
#define D_FEATURE_RESOLVE_H

/* Wi-Fi 功能依赖网络协议栈。 */
#if D_FEATURE_WIFI
#define CONFIG_WIFI_ENABLE
#define CONFIG_NET_ENABLE
#endif

/* BLE 配网使用仅 BLE 的控制器配置。 */
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

/* 可选的文本及相机应用。 */
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

/* ISP0 沿用原连接器上的 iic0；产品预览为触摸保留独立总线。 */
#if D_CAMERA_PREVIEW_ENABLE
#define D_HYN_IIC_DEVICE_NAME "iic1"
#else
#define D_HYN_IIC_DEVICE_NAME "iic0"
#endif

/* 没有云语音代理时，产品提醒仍须能播放声音。 */
#if D_UI_TARGET == D_UI_PRODUCT
#define D_LOCAL_PROMPT_ENABLE 1
#define CONFIG_AUDIO_ENABLE
#else
#define D_LOCAL_PROMPT_ENABLE 0
#endif

/* 语音代理所需的 SDK 基础依赖。 */
#if D_FEATURE_BRTC
/* 两个 BRTC 适配文件继续使用供应商接口要求的旧条件宏。 */
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

/* 本地音乐依赖音频服务及本地音乐模式。 */
#if D_FEATURE_LOCAL_MUSIC && (D_UI_TARGET == D_UI_PRODUCT)
#define D_PRODUCT_MUSIC_ENABLE 1
#else
#define D_PRODUCT_MUSIC_ENABLE 0
#endif
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

/* 即使关闭 Qwen，HTTPS OTA 仍自行依赖 TLS 及可信时间启动链路。 */
#if D_FEATURE_OTA
#define CONFIG_NET_ENABLE
#define CONFIG_MBEDTLS_ENABLE
#define CONFIG_HTTP_CLIENT_ENABLE
#endif

/* 独立网络测试仍可通过编译参数显式启用。 */
#if D_FEATURE_WEBSOCKET
#define CONFIG_NET_ENABLE
#define CONFIG_MBEDTLS_ENABLE
#endif

#if defined(D_TCP_CLIENT_TEST) || defined(D_TCP_SERVER_TEST) || \
    defined(D_UDP_TEST) || defined(D_HTTP_TEST) || defined(D_MQTT_TEST)
#define CONFIG_NET_ENABLE
#endif

#endif /* D_FEATURE_RESOLVE_H */
