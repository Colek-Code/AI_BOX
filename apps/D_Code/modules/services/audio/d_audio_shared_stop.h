/** @file d_audio_shared_stop.h @brief 共享 Qwen SDK 音频所有者的受检排空接口。 */
#ifndef D_AUDIO_SHARED_STOP_H
#define D_AUDIO_SHARED_STOP_H
#include "framework/d_error.h"
#include <stdint.h>

/**
 * @brief 在生产者队列已确认停止后，等待底层音频 FIFO 屏障及编码、解码 STOP 结果。
 * @param[in] timeout_ms 非零等待毫秒数。
 * @return D_OK 表示受检停止成功；零期限、未就绪、投递失败、STOP 错误或超时返回相应错误。
 * @note 仅 Qwen 生命周期后台任务调用；UI 或音频所有者任务不能在此等待。超时保留唯一在途屏障，重试加入同一操作；失败不转移资源所有权。实现位于 apps/common/LLM/audio/audio_input.c。
 */
d_err_t d_audio_shared_stop_wait(uint32_t timeout_ms);
#endif
