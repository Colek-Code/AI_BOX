/**
 * @file deployment_config.h
 * @brief 配置网络、云端、BLE、AI Chat 和 BRTC 的部署参数。
 * @note 功能选择由 app_config.h 管理；仅在当前部署需要调整凭据、地址或身份时修改本文件。
 */

#ifndef D_DEPLOYMENT_CONFIG_H
#define D_DEPLOYMENT_CONFIG_H

/* 产品身份独立于所选云服务商。 */
#define D_FIRMWARE_VERSION                "1.1.0"
#ifndef D_FIRMWARE_BUILD_ID
#define D_FIRMWARE_BUILD_ID               "development"
#endif

#if D_FEATURE_WIFI
#define D_WIFI_SSID                 ""
#define D_WIFI_PWD                  ""
#define D_WIFI_AP_SSID              ""
#define D_WIFI_AP_PWD               "CHANGE_ME"
#define STA_SSID                    ""
#define STA_PWD                     ""
#endif

#ifdef D_TCP_CLIENT_TEST
#define D_TCP_SERVER_IP             "10.17.40.160"
#define D_TCP_SERVER_PORT           8080
#define SERVER_TCP_IP               D_TCP_SERVER_IP
#define SERVER_TCP_PORT             D_TCP_SERVER_PORT
#endif

#if D_FEATURE_AI_CHAT
/* 1 = Qwen 文本接口；2 = 豆包文本接口。 */
#define D_AI_PLATFORM               2

#if (D_AI_PLATFORM == 1)
#define D_AI_API_KEY                ""
#define D_AI_HOST                   "dashscope.aliyuncs.com"
#define D_AI_PATH                   "/api/v1/services/aigc/text-generation/generation"
#define D_AI_MODEL                  "qwen-turbo"
#else
#define D_AI_API_KEY                ""
#define D_AI_HOST                   "ark.cn-beijing.volces.com"
#define D_AI_PATH                   "/api/v3/responses"
#define D_AI_MODEL                  "doubao-seed-1-8-251228"
#endif

#define D_AI_TIMEOUT_MS             15000
#define D_AI_MAX_RETRIES            2
#define D_AI_REPLY_BUF_SIZE         2048
#endif


#if D_FEATURE_BRTC
#define D_BRTC_USERSERVER_URL       "http://10.122.170.160:8936/api/v1/aiagent"
#define D_BRTC_APP_ID               ""
#define D_BRTC_USER_ID              "D_Code_001"

#define D_BRTC_WORKFLOW             "VoiceChat"
#define D_BRTC_LLM                  "LLMRacing"
#define D_BRTC_LANG                 "zh"
#define D_BRTC_AUDIO_IN_CODEC       "pcm"
#define D_BRTC_AUDIO_OUT_CODEC      "pcm"
#define D_BRTC_AUDIO_IN_FREQ        16000
#define D_BRTC_AUDIO_IN_CH          1
#define D_BRTC_VOICE_INTERRUPT_EN   true
#define D_BRTC_VOICE_INTERRUPT_LVL  80
#define D_BRTC_REGION               REGION_MAINLAND
#define D_BRTC_LICENSE_KEY          ""

#define D_BRTC_HTTP_TIMEOUT_MS      5000
#define D_BRTC_HTTP_MAX_RETRIES     3
#define D_BRTC_IDLE_TIMEOUT_MS      (5 * 60 * 1000)

#define D_BRTC_ENABLE_G722
#define D_BRTC_ASR_LONG_AUDIO_ENABLE

#ifdef D_BRTC_ASR_LONG_AUDIO_ENABLE
#define D_BRTC_ASR_LONG_AUDIO_MODE "true"
#else
#define D_BRTC_ASR_LONG_AUDIO_MODE "false"
#endif

#ifdef D_BRTC_ENABLE_G722
#define D_BRTC_HTTP_CONFIG_STR  "{\"llm\":\"" D_BRTC_LLM "\",\"llm_token\":\"no\"," \
    "\"rtc_ac\":\"g722\",\"lang\":\"" D_BRTC_LANG "\"," \
    "\"asr_long_audio_mode\":" D_BRTC_ASR_LONG_AUDIO_MODE "}"
#else
#define D_BRTC_HTTP_CONFIG_STR  "{\"llm\":\"" D_BRTC_LLM "\",\"llm_token\":\"no\"," \
    "\"rtc_ac\":\"pcmu\",\"lang\":\"" D_BRTC_LANG "\"," \
    "\"asr_long_audio_mode\":" D_BRTC_ASR_LONG_AUDIO_MODE "}"
#endif
#endif

#if D_FEATURE_BLE_PROVISION
#define D_BLE_DEVICE_NAME_PREFIX    "TUDA_AI"
#define D_BLE_PRODUCT_UUID          "tuda-ai-ble-001"
#define D_BLE_BATCH_NUM             "batch-001"
#endif

#endif /* D_DEPLOYMENT_CONFIG_H */
