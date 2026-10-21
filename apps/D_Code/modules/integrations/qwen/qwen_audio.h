#ifndef QWEN_AUDIO_H
#define QWEN_AUDIO_H

#include "framework/d_error.h"

#include <stdint.h>

d_err_t qwen_audio_init(void);
uint8_t qwen_audio_is_initialized(void);
uint8_t qwen_audio_is_player_active(void);
uint32_t qwen_audio_player_generation(void);
uint8_t qwen_audio_is_player_stalled(uint32_t timeout_ms);
void qwen_audio_deinit(void);
void qwen_audio_prepare_player(void);
d_err_t qwen_audio_prepare_recorder(void);
void qwen_audio_start_recorder(void);
void qwen_audio_stop_recorder(void);
void qwen_audio_start_player(void);
void qwen_audio_finish_player(void);
void qwen_audio_stop_player(void);
void qwen_audio_stop_all(void);
/**
 * @brief 排空 Qwen 生产任务，再等待共享音频队列和 SDK 停止完成。
 * @param timeout_ms 两层队列及实际停止共用的总时限，必须大于零，单位毫秒。
 * @return D_OK 表示 MMI 数据访问停止，且下层编码器/解码器 STOP 成功；队列提交失败
 *         返回 D_ERR_FULL，SDK 停止失败返回 D_ERR_IO，超时返回 D_ERR_TIMEOUT；零时限
 *         返回 D_ERR_INVALID_ARG。模块未初始化时直接返回 D_OK。
 * @note 仅由 Qwen 后台生命周期线程调用，不能在 LVGL 回调中等待。
 *       失败时保留停止票据，下一次继续确认同一请求。
 *       超时保留下层屏障，失败保留未释放的资源；只有成功才可交出音频占用。
 */
d_err_t qwen_audio_stop_all_wait(uint32_t timeout_ms);
d_err_t qwen_audio_set_player_volume(uint8_t volume_percent);
d_err_t qwen_audio_adjust_player_volume(int8_t step_percent);
d_err_t qwen_audio_set_player_mute(uint8_t is_mute);
uint8_t qwen_audio_get_player_volume(void);

#endif
