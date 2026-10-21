/**
 * @file runtime_config.h
 * @brief Input, power, logging and generic audio runtime parameters.
 */

#ifndef D_RUNTIME_CONFIG_H
#define D_RUNTIME_CONFIG_H

#define D_KEY_PTT                         4
#define D_KEY_VOL_UP                      5
#define D_KEY_POWER_KEY                   KEY_POWER

#define D_QWEN_WIFI_RECONFIG_KEY          D_KEY_PTT
#define D_QWEN_WIFI_RECONFIG_ACTION       KEY_EVENT_TRIPLE_CLICK

#define D_POWER_CTRL_KEY                  D_KEY_POWER_KEY
#define D_POWER_CTRL_LONG_PRESS_MS        5000U
#define D_POWER_CTRL_LONG_START_OFFSET_MS 1000U
#define D_POWER_CTRL_DEFAULT_MODE         0U

#define D_LOG_LEVEL                       LOG_LEVEL_DEBUG
#define D_AUDIO_SAMPLE_RATE               16000

/* Product UI display/animation cadence; input polling remains independent. */
#define D_PRODUCT_UI_FRAME_PERIOD_MS       34U

#define D_RTC_AUDIO_CODEC_PCM             0
#define D_RTC_AUDIO_CODEC_OPUS            1
#define D_RTC_AUDIO_CODEC                 D_RTC_AUDIO_CODEC_PCM

#if (D_RTC_AUDIO_CODEC != D_RTC_AUDIO_CODEC_PCM) && \
    (D_RTC_AUDIO_CODEC != D_RTC_AUDIO_CODEC_OPUS)
#error "D_RTC_AUDIO_CODEC 无效：请选择 PCM 或 OPUS"
#endif

#endif /* D_RUNTIME_CONFIG_H */
