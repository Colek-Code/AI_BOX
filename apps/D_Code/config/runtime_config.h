/**
 * @file runtime_config.h
 * @brief 配置输入、电源、日志和通用音频运行参数，功能选择由 app_config.h 管理。
 */

#ifndef D_RUNTIME_CONFIG_H
#define D_RUNTIME_CONFIG_H

/* SDK 按键名在消费者包含 key_event.h 后展开。
 * 此处 KEY_VOLUME_DEC 对应 36k 的按住说话键，不绑定实体音量减动作。 */
#define D_KEY_PTT                         KEY_VOLUME_DEC
#define D_KEY_VOL_UP                      KEY_VOLUME_INC /* Reserved; unbound. */
#define D_KEY_POWER_KEY                   KEY_POWER

#define D_QWEN_WIFI_RECONFIG_KEY          D_KEY_PTT
#define D_QWEN_WIFI_RECONFIG_ACTION       KEY_EVENT_TRIPLE_CLICK

#define D_POWER_CTRL_KEY                  D_KEY_POWER_KEY
#define D_POWER_CTRL_LONG_PRESS_MS        5000U
#define D_POWER_CTRL_LONG_START_OFFSET_MS 1000U
#define D_POWER_CTRL_DEFAULT_MODE         0U

#define D_LOG_LEVEL                       LOG_LEVEL_DEBUG
#define D_AUDIO_SAMPLE_RATE               16000

/* Product UI 的显示及动画周期；输入轮询周期独立配置。 */
#define D_PRODUCT_UI_FRAME_PERIOD_MS       34U

/* 前台 UI 与 Qwen 控制/TLS（优先级 10）共享调度时间；传屏任务必须能在
 * 两个控制任务再次运行前完成。音频（11）与 local_music（12）保留原优先级。 */
#define D_PRODUCT_UI_TASK_PRIORITY         10U
#define D_PRODUCT_UI_FLUSH_TASK_PRIORITY   11U
#if (D_PRODUCT_UI_TASK_PRIORITY < 1U) || (D_PRODUCT_UI_TASK_PRIORITY > 30U) || \
    (D_PRODUCT_UI_FLUSH_TASK_PRIORITY <= D_PRODUCT_UI_TASK_PRIORITY) || \
    (D_PRODUCT_UI_FLUSH_TASK_PRIORITY > 30U)
#error "Product UI priorities must be 1..30, with flush above the UI worker"
#endif

#define D_RTC_AUDIO_CODEC_PCM             0
#define D_RTC_AUDIO_CODEC_OPUS            1
#define D_RTC_AUDIO_CODEC                 D_RTC_AUDIO_CODEC_PCM

#if (D_RTC_AUDIO_CODEC != D_RTC_AUDIO_CODEC_PCM) && \
    (D_RTC_AUDIO_CODEC != D_RTC_AUDIO_CODEC_OPUS)
#error "D_RTC_AUDIO_CODEC 无效：请选择 PCM 或 OPUS"
#endif

#endif /* D_RUNTIME_CONFIG_H */
