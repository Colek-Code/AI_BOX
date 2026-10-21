/**
 * @file    rtc_audio_service.c
 * @brief   RTC 音频服务 — 最小验证实现
 *
 * 封装 SDK audio_input.c 提供的音频采集/播放能力，
 * 实现本地 MIC→OPUS→DAC 环回测试，验证音频底层可用。
 *
 * 依赖：audio_input.c (apps/common/LLM/audio/)
 */

#include "config/app_config.h"

#if D_FEATURE_RTC_AUDIO

#include "system/includes.h"
#include "os/os_api.h"
#include "framework/d_event_bus.h"
#include "framework/d_error.h"
#include "framework/d_log.h"
#include "rtc_audio_service.h"
#include "audio_input.h"

#define TAG "RTC_AUD"

//*********************************************************************************//
//                              内部状态                                            //
//*********************************************************************************//

static volatile int s_initialized = 0;
static volatile int s_loopback_running = 0;
static volatile int s_loopback_stop_req = 0;

//*********************************************************************************//
//                              环回测试任务                                        //
//*********************************************************************************//

/*
 * 环回缓冲区：与 AUDIO_RECORD_VOICE_UPLORD_LEN 对齐
 * OPUS: 60ms@32kbps = 240B/帧 (SDK 支持 16/32/64kbps)
 * PCM:  1280B/块
 */
#ifdef AUDIO_TYPE_OPUS
#define RTC_LOOPBACK_BUF_SIZE   RTC_AUDIO_OPUS_FRAME_BYTES
#else
#define RTC_LOOPBACK_BUF_SIZE   1280
#endif
static u8 s_loopback_buf[RTC_LOOPBACK_BUF_SIZE];

static void rtc_audio_loopback_task(void *arg)
{
    int rlen;

    LOGI(TAG, "环回测试任务启动");
    s_loopback_running = 1;
    d_event_post(EVT_RTC_AUDIO_STARTED, NULL, 0);

    while (!s_loopback_stop_req) {
        /* 从编码器 cbuffer 读取音频数据（PCM 或 OPUS，取决于 audio_input.h 宏） */
        rlen = _device_get_voice_data(s_loopback_buf, RTC_LOOPBACK_BUF_SIZE);

        if (rlen > 0) {
            /* 写入解码器 cbuffer → DAC 输出 */
            _device_write_voice_data(s_loopback_buf, rlen);
        }
    }

    LOGI(TAG, "环回测试任务退出");
    s_loopback_running = 0;
    s_loopback_stop_req = 0;
    d_event_post(EVT_RTC_AUDIO_STOPPED, NULL, 0);
}

//*********************************************************************************//
//                              公开 API                                            //
//*********************************************************************************//

d_err_t rtc_audio_service_init(void)
{
    if (s_initialized) {
        LOGW(TAG, "已初始化，跳过");
        return D_OK;
    }

    LOGI(TAG, "初始化音频: %dHz/%dbit/%dch",
         RTC_AUDIO_SAMPLE_RATE, RTC_AUDIO_BIT_DEPTH, RTC_AUDIO_CHANNELS);

    /* 调用 SDK audio_input.c 初始化 Audio Server + cbuffer */
    audio_stream_init(RTC_AUDIO_SAMPLE_RATE, RTC_AUDIO_BIT_DEPTH, RTC_AUDIO_CHANNELS);

    s_initialized = 1;
    LOGI(TAG, "音频服务初始化完成");
    return D_OK;
}

d_err_t rtc_audio_start(void)
{
    if (!s_initialized) {
        LOGE(TAG, "未初始化");
        return D_ERR_NOT_READY;
    }

    LOGI(TAG, "启动录音+播放");
    start_audio_stream();
    return D_OK;
}

d_err_t rtc_audio_stop(void)
{
    if (!s_initialized) {
        return D_ERR_NOT_READY;
    }

    LOGI(TAG, "停止录音+播放");
    stop_audio_stream();
    return D_OK;
}

d_err_t rtc_audio_loopback_start(void)
{
    int ret;

    if (!s_initialized) {
        LOGE(TAG, "未初始化，无法启动环回");
        return D_ERR_NOT_READY;
    }

    if (s_loopback_running) {
        LOGW(TAG, "环回已在运行");
        return D_ERR_BUSY;
    }

    LOGI(TAG, "启动本地环回测试（MIC → cbuffer → DAC）");

    /*
     * D++ 环回延迟优化：先开 player(DAC)，再开 recorder(MIC)
     *
     * 消息通过 __audio_task 队列串行处理：
     *   1) player 消息 → audio_player_net_init() → DAC 初始化 ~1.7s
     *   2) recorder 消息 → audio_recoder_init() → MIC 开始采集
     *
     * 这样录音只在 DAC 就绪后才开始，避免 1.7s 数据积压。
     * 配合 AUDIO_ATTR_NO_WAIT_READY，AUDIO_DEC_START 不会因空 buffer 阻塞。
     */
    _device_net_audio_play(true);      /* D++ 先开解码器（DAC 初始化 → 就绪） */
    _device_net_audio_recorder(true);  /* D++ 再开编码器（MIC→pcm_cbuff_w） */

    /* 清空残留数据，确保环回从零开始 */
    _device_wbuf_clear();

    /* 创建环回任务：读编码数据 → 写解码缓冲 */
    s_loopback_stop_req = 0;
    ret = thread_fork("rtc_loopback", 8, 1024, 0, NULL, rtc_audio_loopback_task, NULL);
    if (ret != OS_NO_ERR) {
        stop_audio_stream();
        LOGE(TAG, "rtc_loopback 创建失败: %d", ret);
        return D_ERR_IO;
    }

    return D_OK;
}

void rtc_audio_loopback_stop(void)
{
    if (!s_loopback_running) {
        return;
    }

    LOGI(TAG, "停止环回测试");
    s_loopback_stop_req = 1;

    /* 等待任务退出 */
    int timeout = 50; /* 50 × 20ms = 1s */
    while (s_loopback_running && timeout-- > 0) {
        os_time_dly(2); /* 20ms */
    }

    if (s_loopback_running) {
        LOGW(TAG, "环回任务退出超时");
    }

    stop_audio_stream();
}

int rtc_audio_loopback_is_running(void)
{
    return s_loopback_running;
}

#endif /* D_FEATURE_RTC_AUDIO */
