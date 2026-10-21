/**
 * @file    rtc_audio_service.h
 * @brief   RTC 音频服务 — 最小验证（MIC→cbuffer→DAC 环回）
 * @note    封装 SDK audio_input.c，提供 D_Code 风格接口
 *          后续扩展 WebSocket 音频传输时在此基础上增加
 */

#ifndef RTC_AUDIO_SERVICE_H
#define RTC_AUDIO_SERVICE_H

#include "framework/d_error.h"

//*********************************************************************************//
//                              配置参数                                            //
//*********************************************************************************//

#define RTC_AUDIO_SAMPLE_RATE       16000   /* 采样率 16kHz */
#define RTC_AUDIO_BIT_DEPTH         16      /* 量化深度 16bit */
#define RTC_AUDIO_CHANNELS          1       /* 单声道 */
/* OPUS 参数：仅 OPUS 模式有效，loopback 和后续 WS 音频传输共用。 */
#if D_RTC_AUDIO_CODEC == D_RTC_AUDIO_CODEC_OPUS
#define RTC_AUDIO_OPUS_BITRATE      32000   /* OPUS 码率 32kbps (SDK 支持 16000/32000/64000) */
#define RTC_AUDIO_OPUS_FRAME_MS     60      /* OPUS 帧时长 60ms */
#define RTC_AUDIO_OPUS_FRAME_BYTES  240     /* OPUS CBR 帧字节 = 32000*60/8000 */
#endif

//*********************************************************************************//
//                              API                                                 //
//*********************************************************************************//

/**
 * @brief 初始化 RTC 音频服务
 * @note  调用 audio_stream_init() 初始化 Audio Server + cbuffer
 *        应在 app_main Phase 4 中调用
 * @return D_OK 成功, D_ERR_IO 失败
 */
d_err_t rtc_audio_service_init(void);

/**
 * @brief 启动录音 + 播放（同时启动编码器和解码器）
 * @return D_OK 成功
 */
d_err_t rtc_audio_start(void);

/**
 * @brief 停止录音 + 播放
 * @return D_OK 成功
 */
d_err_t rtc_audio_stop(void);

/**
 * @brief 启动本地环回测试（MIC → encode → cbuffer → decode → DAC）
 * @note  会创建一个后台任务，循环读取编码数据并写入解码缓冲
 *        对 MIC 说话应能从扬声器听到回放
 * @return D_OK 成功, D_ERR_BUSY 已在运行
 */
d_err_t rtc_audio_loopback_start(void);

/**
 * @brief 停止环回测试
 */
void rtc_audio_loopback_stop(void);

/**
 * @brief 查询环回测试是否正在运行
 * @return 1=运行中, 0=未运行
 */
int rtc_audio_loopback_is_running(void);

#endif /* RTC_AUDIO_SERVICE_H */
