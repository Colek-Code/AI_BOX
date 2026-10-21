/**
 * @file qwen_config.h
 * @brief Qwen multimodal-agent and XiaoLe wakeup parameters.
 */

#ifndef D_QWEN_CONFIG_H
#define D_QWEN_CONFIG_H

#if D_FEATURE_QWEN

/* HAL 诊断仅在显式设为 1 时启用；正式会话默认关闭。 */
#ifndef D_QWEN_HAL_ACCEPT_TEST_ENABLE
#define D_QWEN_HAL_ACCEPT_TEST_ENABLE        0
#endif
#if (D_QWEN_HAL_ACCEPT_TEST_ENABLE != 0) && (D_QWEN_HAL_ACCEPT_TEST_ENABLE != 1)
#error "D_QWEN_HAL_ACCEPT_TEST_ENABLE must be 0 or 1"
#endif

#define D_QWEN_AUTH_MODE_FULL_MANAGED       1U
#define D_QWEN_AUTH_MODE_SEMI_MANAGED       2U
#define D_QWEN_AUTH_MODE                    D_QWEN_AUTH_MODE_FULL_MANAGED
#define D_QWEN_WORKSPACE_ID                 "your_qwen_workspace_id"
#define D_QWEN_APP_ID                       ""
#define D_QWEN_APP_SECRET                   ""

#if (D_QWEN_AUTH_MODE == D_QWEN_AUTH_MODE_FULL_MANAGED)
#define D_QWEN_API_KEY                      ""
#endif

#define D_QWEN_HTTP_BASE_URL_FULL_MANAGED   "https://bailian.multimodalagent.aliyuncs.com"
#define D_QWEN_HTTP_HOST_FULL_MANAGED       "bailian.multimodalagent.aliyuncs.com"
#define D_QWEN_HTTP_PORT_FULL_MANAGED       "443"
#define D_QWEN_REGISTER_API_FULL_MANAGED    "/api/device/v1/register"
#define D_QWEN_TOKEN_API_FULL_MANAGED       "/api/token/v1/getToken"

#define D_QWEN_HTTP_BASE_URL_SEMI_MANAGED   "https://your_cloud_host.example.com"
#define D_QWEN_HTTP_HOST_SEMI_MANAGED       "your_cloud_host.example.com"
#define D_QWEN_HTTP_PORT_SEMI_MANAGED       "443"
#define D_QWEN_REGISTER_API_SEMI_MANAGED    "/device/open/aliyun/rtos/register"
#define D_QWEN_TOKEN_API_SEMI_MANAGED       "/device/open/aliyun/rtos/getToken"

#define D_QWEN_HTTP_TLS_VERIFY_OPTIONAL     0U
#define D_QWEN_HTTP_TLS_VERIFY_REQUIRED     1U
/* Product defaults verify the server. OPTIONAL is only an explicit diagnostic
 * choice and also requires the opt-in below; never use it for a release. */
#define D_QWEN_HTTP_TLS_VERIFY_MODE_FULL_MANAGED D_QWEN_HTTP_TLS_VERIFY_REQUIRED
#define D_QWEN_HTTP_TLS_VERIFY_MODE_SEMI_MANAGED D_QWEN_HTTP_TLS_VERIFY_REQUIRED
#define D_QWEN_HTTP_ALLOW_UNVERIFIED_DIAGNOSTIC 0
#include "config/certs/digicert_global_root_g2.h"
/* Trust anchor for the configured semi-managed backend. When changing the
 * backend, supply its CA here; the hostname is still checked independently. */
#define D_QWEN_HTTP_CA_PEM_SEMI_MANAGED     D_DIGICERT_GLOBAL_ROOT_G2_PEM

#if (D_QWEN_AUTH_MODE == D_QWEN_AUTH_MODE_FULL_MANAGED)
#define D_QWEN_HTTP_BASE_URL                D_QWEN_HTTP_BASE_URL_FULL_MANAGED
#define D_QWEN_HTTP_HOST                    D_QWEN_HTTP_HOST_FULL_MANAGED
#define D_QWEN_HTTP_PORT                    D_QWEN_HTTP_PORT_FULL_MANAGED
#define D_QWEN_REGISTER_API                 D_QWEN_REGISTER_API_FULL_MANAGED
#define D_QWEN_TOKEN_API                    D_QWEN_TOKEN_API_FULL_MANAGED
#define D_QWEN_HTTP_TLS_VERIFY_MODE         D_QWEN_HTTP_TLS_VERIFY_MODE_FULL_MANAGED
#else
#define D_QWEN_HTTP_BASE_URL                D_QWEN_HTTP_BASE_URL_SEMI_MANAGED
#define D_QWEN_HTTP_HOST                    D_QWEN_HTTP_HOST_SEMI_MANAGED
#define D_QWEN_HTTP_PORT                    D_QWEN_HTTP_PORT_SEMI_MANAGED
#define D_QWEN_REGISTER_API                 D_QWEN_REGISTER_API_SEMI_MANAGED
#define D_QWEN_TOKEN_API                    D_QWEN_TOKEN_API_SEMI_MANAGED
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

#define D_QWEN_DEVICE_NAME                  "AC791N_DEV"
#define D_QWEN_DEVICE_NAME_USE_BT_MAC       0
#define D_QWEN_DIALOG_MODE_PUSH2TALK        0U
#define D_QWEN_DIALOG_MODE_DUPLEX           1U
#define D_QWEN_DIALOG_MODE                  D_QWEN_DIALOG_MODE_DUPLEX

/* Product UI waits for a button/wakeup/key; legacy roots retain automatic start. */
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
#define D_QWEN_HTTP_BUF_SIZE                (4 * 1024)
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
