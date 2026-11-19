/**
 * @file qwen_config.h
 * @brief 配置 Qwen 多模态会话及小乐唤醒的公开选项，私有接入实现由静态库管理。
 */

#ifndef D_QWEN_CONFIG_H
#define D_QWEN_CONFIG_H

#if D_FEATURE_QWEN
/* SDK WebSocket 的 INFO 日志包含接入 URL；保留警告及错误诊断。 */
#define D_QWEN_PRIVATE_ACCESS_LOGS 1

/* HAL 诊断仅在显式设为 1 时启用；正式会话默认关闭。 */
#ifndef D_QWEN_HAL_ACCEPT_TEST_ENABLE
#define D_QWEN_HAL_ACCEPT_TEST_ENABLE        0
#endif
#if (D_QWEN_HAL_ACCEPT_TEST_ENABLE != 0) && (D_QWEN_HAL_ACCEPT_TEST_ENABLE != 1)
#error "D_QWEN_HAL_ACCEPT_TEST_ENABLE must be 0 or 1"
#endif

/* 全托管独立保存签发凭据。半托管仍使用原 VM16/31/32/33。 */
#define D_QWEN_FULL_CREDENTIAL_STORE 1
#if D_QWEN_FULL_CREDENTIAL_STORE != 0 && D_QWEN_FULL_CREDENTIAL_STORE != 1
#error "D_QWEN_FULL_CREDENTIAL_STORE must be 0 or 1"
#endif

#define D_QWEN_AUTH_MODE_FULL_MANAGED       1U
#define D_QWEN_AUTH_MODE_SEMI_MANAGED       2U
#define D_QWEN_AUTH_MODE                    D_QWEN_AUTH_MODE_SEMI_MANAGED
// 半托管每次只使用一种激活码来源，读取失败时不回退到其他来源。
#define D_QWEN_ACTIVATION_SOURCE_UI         1U // 在设备界面输入激活码。
#define D_QWEN_ACTIVATION_SOURCE_FACTORY    2U // 使用原厂厂测工具写入 VM 的激活码。
#define D_QWEN_ACTIVATION_SOURCE_STATIC     3U // 使用编译期固定码，仅供单台样机测试。
#define D_QWEN_ACTIVATION_SOURCE_PROGRAMMER 4U // 使用强制下载工具写入 Flash 尾部的认证码。
#define D_QWEN_ACTIVATION_SOURCE            D_QWEN_ACTIVATION_SOURCE_PROGRAMMER // 当前选择烧录器来源。
#define D_QWEN_FACTORY_TEST_BUILD           0

#if D_QWEN_FACTORY_TEST_BUILD != 0 && D_QWEN_FACTORY_TEST_BUILD != 1
#error "D_QWEN_FACTORY_TEST_BUILD must be 0 or 1"
#endif
#if D_QWEN_ACTIVATION_SOURCE < 1U || D_QWEN_ACTIVATION_SOURCE > 4U
#error "D_QWEN_ACTIVATION_SOURCE must be UI, FACTORY, STATIC or PROGRAMMER"
#endif
#if D_QWEN_FACTORY_TEST_BUILD && ((D_QWEN_AUTH_MODE != D_QWEN_AUTH_MODE_SEMI_MANAGED) || \
    (D_QWEN_ACTIVATION_SOURCE != D_QWEN_ACTIVATION_SOURCE_FACTORY))
#error "Qwen factory image requires semi-managed FACTORY source"
#endif
#if D_QWEN_FACTORY_TEST_BUILD
#define PRODUCT_TEST_ENABLE
#endif




#define D_QWEN_HTTP_TLS_VERIFY_OPTIONAL     0U
#define D_QWEN_HTTP_TLS_VERIFY_REQUIRED     1U
/* 产品默认校验服务端证书。OPTIONAL 仅用于明确选择的诊断，
 * 还须启用下方许可开关；发布版本不得使用。 */
#define D_QWEN_HTTP_TLS_VERIFY_MODE_FULL_MANAGED D_QWEN_HTTP_TLS_VERIFY_REQUIRED
#define D_QWEN_HTTP_TLS_VERIFY_MODE_SEMI_MANAGED D_QWEN_HTTP_TLS_VERIFY_REQUIRED
#define D_QWEN_HTTP_ALLOW_UNVERIFIED_DIAGNOSTIC 0
#include "config/certs/ota_isrg_roots.h"
/* 共用公开 ISRG 根证书，不受 OTA 是否启用影响。 */
#define D_QWEN_HTTP_CA_PEM_SEMI_MANAGED     D_OTA_ISRG_ROOTS_PEM

#if (D_QWEN_AUTH_MODE == D_QWEN_AUTH_MODE_FULL_MANAGED)
#define D_QWEN_HTTP_TLS_VERIFY_MODE         D_QWEN_HTTP_TLS_VERIFY_MODE_FULL_MANAGED
#else
#define D_QWEN_HTTP_TLS_VERIFY_MODE         D_QWEN_HTTP_TLS_VERIFY_MODE_SEMI_MANAGED
#define D_QWEN_HTTP_CA_PEM                  D_QWEN_HTTP_CA_PEM_SEMI_MANAGED
#endif

#if ((D_QWEN_HTTP_TLS_VERIFY_MODE != D_QWEN_HTTP_TLS_VERIFY_OPTIONAL) && \
     (D_QWEN_HTTP_TLS_VERIFY_MODE != D_QWEN_HTTP_TLS_VERIFY_REQUIRED))
#error "D_QWEN_HTTP_TLS_VERIFY_MODE 无效：请选择 OPTIONAL 或 REQUIRED"
#endif

#if (D_QWEN_HTTP_TLS_VERIFY_MODE == D_QWEN_HTTP_TLS_VERIFY_OPTIONAL) && !D_QWEN_HTTP_ALLOW_UNVERIFIED_DIAGNOSTIC
#error "Unverified HTTPS requires an explicit diagnostic opt-in"
#endif

#define D_QWEN_DEVICE_NAME                  "AC791N_DEVICE" // 未使用出厂名且关闭 MAC 命名时使用此固定名。
/* 出厂命名启用后，合法旧版单码仍沿用MAC/固定名策略；读取、校验或格式错误会报错。 */
#define D_QWEN_DEVICE_NAME_USE_FACTORY      1 // 1：使用出厂设备名；0：使用下方MAC/固定名策略。
#if D_QWEN_DEVICE_NAME_USE_FACTORY != 0 && D_QWEN_DEVICE_NAME_USE_FACTORY != 1
#error "D_QWEN_DEVICE_NAME_USE_FACTORY must be 0 or 1"
#endif
#define D_QWEN_DEVICE_NAME_USE_BT_MAC       1 // 未启用出厂名或为合法旧版单码时：0用固定名，1由本机蓝牙MAC生成。
#if D_QWEN_DEVICE_NAME_USE_BT_MAC != 0 && D_QWEN_DEVICE_NAME_USE_BT_MAC != 1
#error "D_QWEN_DEVICE_NAME_USE_BT_MAC must be 0 or 1"
#endif
#define D_QWEN_WSS_DIAG_ENABLE             1 // 临时诊断回答截断：记录 WSS 指定字段；实机定位完成后关闭。
#if D_QWEN_WSS_DIAG_ENABLE != 0 && D_QWEN_WSS_DIAG_ENABLE != 1
#error "D_QWEN_WSS_DIAG_ENABLE must be 0 or 1"
#endif

#define D_QWEN_DIALOG_MODE_PUSH2TALK        0U
#define D_QWEN_DIALOG_MODE_DUPLEX           1U
#define D_QWEN_DIALOG_MODE                  D_QWEN_DIALOG_MODE_DUPLEX

/* Product UI 等待按钮、唤醒或按键启动；旧界面仍按原策略自动开始。 */
#define D_QWEN_SESSION_START_AUTO           0U
#define D_QWEN_SESSION_START_EXPLICIT       1U
#if D_UI_TARGET == D_UI_PRODUCT
#define D_QWEN_SESSION_START_POLICY         D_QWEN_SESSION_START_EXPLICIT
#else
#define D_QWEN_SESSION_START_POLICY         D_QWEN_SESSION_START_AUTO
#endif

#if ((D_QWEN_DIALOG_MODE != D_QWEN_DIALOG_MODE_PUSH2TALK) && \
     (D_QWEN_DIALOG_MODE != D_QWEN_DIALOG_MODE_DUPLEX))
#error "D_QWEN_DIALOG_MODE 无效：请选择 PUSH2TALK 或 DUPLEX"
#endif

#define D_QWEN_VOICE_ID                     "longanhuan"
#define D_QWEN_AUDIO_FORMAT_PCM             0U
#define D_QWEN_AUDIO_FORMAT_RAW_OPUS        1U
#define D_QWEN_DS_AUDIO_FORMAT              D_QWEN_AUDIO_FORMAT_RAW_OPUS
#define D_QWEN_US_AUDIO_FORMAT              D_QWEN_AUDIO_FORMAT_RAW_OPUS

#if ((D_QWEN_DS_AUDIO_FORMAT != D_QWEN_AUDIO_FORMAT_PCM) && \
     (D_QWEN_DS_AUDIO_FORMAT != D_QWEN_AUDIO_FORMAT_RAW_OPUS))
#error "D_QWEN_DS_AUDIO_FORMAT 无效：请选择 PCM 或 RAW_OPUS"
#endif

#if ((D_QWEN_US_AUDIO_FORMAT != D_QWEN_AUDIO_FORMAT_PCM) && \
     (D_QWEN_US_AUDIO_FORMAT != D_QWEN_AUDIO_FORMAT_RAW_OPUS))
#error "D_QWEN_US_AUDIO_FORMAT 无效：请选择 PCM 或 RAW_OPUS"
#endif

#if (D_QWEN_US_AUDIO_FORMAT == D_QWEN_AUDIO_FORMAT_RAW_OPUS)
#define AUDIO_TYPE_OPUS
#endif

#if (D_QWEN_DS_AUDIO_FORMAT == D_QWEN_AUDIO_FORMAT_RAW_OPUS)
#define D_QWEN_US_SAMPLE_RATE               16000
#define D_QWEN_DS_SAMPLE_RATE               16000
#define D_QWEN_RECORD_VOLUME                45U
#define D_QWEN_PLAY_VOLUME                  65U
#define D_QWEN_AUDIO_PROC_ENABLE            1U
#define D_QWEN_VAD_START_MS                 300U
#define D_QWEN_VAD_STOP_MS                  800U
#define D_QWEN_DNS_GAIN_FLOOR               0.05f
#define D_QWEN_DNS_OVER_DRIVE               1.5f
#define D_QWEN_ES_MIN_SUPPRESS              6.0f
#else
#define D_QWEN_US_SAMPLE_RATE               16000
#define D_QWEN_DS_SAMPLE_RATE               8000
#define D_QWEN_RECORD_VOLUME                100U
#define D_QWEN_PLAY_VOLUME                  80U
#define D_QWEN_AUDIO_PROC_ENABLE            0U
#endif

#define D_QWEN_DS_OPUS_ENABLE_CBR           1U
#define D_QWEN_DS_OPUS_FRAME_MS             60U
#define D_QWEN_DS_OPUS_BIT_RATE_KBPS        32U
#define D_QWEN_DS_OPUS_PKT_LEN              \
    ((D_QWEN_DS_OPUS_BIT_RATE_KBPS * D_QWEN_DS_OPUS_FRAME_MS) / 8U)

#if (D_QWEN_DS_AUDIO_FORMAT == D_QWEN_AUDIO_FORMAT_RAW_OPUS)
#if (((D_QWEN_DS_OPUS_BIT_RATE_KBPS * D_QWEN_DS_OPUS_FRAME_MS) % 8U) != 0U)
#error "Qwen OPUS 码率与帧长组合无效"
#endif
#if (D_QWEN_DS_OPUS_PKT_LEN == 0U)
#error "D_QWEN_DS_OPUS_PKT_LEN 不能为 0"
#endif
#ifndef CONFIG_OPUS_DEC_ENABLE
#define CONFIG_OPUS_DEC_ENABLE
#endif
#endif

#if (D_QWEN_US_AUDIO_FORMAT == D_QWEN_AUDIO_FORMAT_RAW_OPUS)
#define D_QWEN_RECORDER_RB_SIZE             (16U * 1024U)
#ifndef CONFIG_OPUS_ENC_ENABLE
#define CONFIG_OPUS_ENC_ENABLE
#endif
#else
#define D_QWEN_RECORDER_RB_SIZE             (32U * 1024U)
#endif

#define D_QWEN_HTTP_TIMEOUT_MS              8000
/* 静态库 ABI 的固定容量；修改容量须由私有工程重新编库。 */
#define D_QWEN_HTTP_BUF_SIZE                4096U
#define D_QWEN_IDLE_TIMEOUT_MS              15000U
#define D_QWEN_IDLE_GOODBYE_TIMEOUT_MS      8000U

#endif /* D_FEATURE_QWEN */

#if D_FEATURE_XIAOLE_WAKEUP
#define D_XIAOLE_REF_ADC_CHANNEL            0U
#define D_XIAOLE_MIC_ADC_CHANNEL            CONFIG_AISP_MIC0_ADC_CHANNEL
#define D_XIAOLE_MIC_GAIN                   CONFIG_AISP_MIC_ADC_GAIN
#define D_XIAOLE_SAMPLE_RATE                16000U
#define D_XIAOLE_MPO_DP                     6
#define D_XIAOLE_START_DELAY_TICKS          20
#endif

#endif /* D_QWEN_CONFIG_H */
