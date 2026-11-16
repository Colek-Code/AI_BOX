/**
 * @file    rtc_audio_service.h
 * @brief   共享音频底层的 MIC→编码缓冲→解码→DAC 环回诊断接口。
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
//                              公共接口                                            //
//*********************************************************************************//

/**
 * @brief 一次性调用共享 audio_stream_init 建立采集与播放对象。
 * @return 当前封装在调用后返回 D_OK；不接收底层初始化错误反馈。
 * @note 启动阶段调用；本封装的 initialized 不证明实机音频通路已通过。
 */
d_err_t rtc_audio_service_init(void);

/**
 * @brief 提交共享音频流录音与播放启动请求。
 * @return 已初始化时返回 D_OK；未初始化返回 D_ERR_NOT_READY。
 * @note 不在本封装等待底层启动完成。
 */
d_err_t rtc_audio_start(void);

/**
 * @brief 提交共享音频流停止请求。
 * @return 已初始化时返回 D_OK；未初始化返回 D_ERR_NOT_READY。
 * @note 返回不等同底层 STOP 完成确认。
 */
d_err_t rtc_audio_stop(void);

/**
 * @brief 按先播放后录音顺序提交启动，并创建本地环回任务。
 * @return 任务创建成功返回 D_OK；未初始化、已报告运行或创建失败返回对应错误。
 * @note 环回读取编码缓冲并写解码缓冲；当前 running 标志由任务启动后置位，本接口不保证并发重复调用安全。
 */
d_err_t rtc_audio_loopback_start(void);

/**
 * @brief 请求环回任务退出，有限等待后提交共享音频停止。
 * @note 最长轮询约一秒，超时只记录日志；返回不能证明任务或底层资源已释放。
 */
void rtc_audio_loopback_stop(void);

/**
 * @brief 读取环回任务发布的运行标志。
 * @return 任务报告运行返回非零，否则返回 0。
 */
int rtc_audio_loopback_is_running(void);

#endif /* RTC_AUDIO_SERVICE_H */
