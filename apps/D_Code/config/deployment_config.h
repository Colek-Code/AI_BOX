/**
 * @file deployment_config.h
 * @brief Network, cloud, BLE, AI Chat and BRTC deployment parameters.
 *
 * Feature selection belongs in app_config.h. Edit this file only when the
 * selected deployment needs different credentials, endpoints or identities.
 */

#ifndef D_DEPLOYMENT_CONFIG_H
#define D_DEPLOYMENT_CONFIG_H

/* Product identity exists independently of the selected cloud provider. */
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
#define D_TCP_SERVER_IP             "192.168.1.100"
#define D_TCP_SERVER_PORT           8080
#define SERVER_TCP_IP               D_TCP_SERVER_IP
#define SERVER_TCP_PORT             D_TCP_SERVER_PORT
#endif

#if D_FEATURE_AI_CHAT
/* 1 = Qwen text API, 2 = Doubao text API. */
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

#if D_DEVICE_CLOUD_ENABLE
#define D_PROVISION_AUTH_FACTORY_SECRET   0
#define D_PROVISION_AUTH_DEVICE_CERT      1
#define D_PROVISION_AUTH_MODE             D_PROVISION_AUTH_FACTORY_SECRET

#define D_CLOUD_BASE_URL                  "https://your-cloud.example.com"
#define D_WS_DEFAULT_ENDPOINT             "wss://your-cloud.example.com/device/ws"

#define D_DEFAULT_DEVICE_ID               "your_device_id"
#define D_DEFAULT_PRODUCT_KEY             "your_product_key"
#define D_DEFAULT_DEVICE_SN               "your_device_sn"
#define D_DEFAULT_FACTORY_SECRET          ""

#define D_DEFAULT_CLAIM_TICKET            "your_claim_ticket"
#define D_DEFAULT_TENANT_ID               "your_tenant_id"
#define D_SIGN_ALGORITHM                  "SHA256withRSA"

#define D_WS_RECV_BUF_SIZE                2048
#define D_WS_HEARTBEAT_INTERVAL_S         30
#define D_WS_HEARTBEAT_TIMEOUT_S          120
#define D_WS_RECONNECT_INTERVAL_S         5
#define D_WS_RECONNECT_MAX_S              60
#endif

#if D_FEATURE_BRTC
#define D_BRTC_USERSERVER_URL       "http://your_brtc_server_ip:8936/api/v1/aiagent"
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
