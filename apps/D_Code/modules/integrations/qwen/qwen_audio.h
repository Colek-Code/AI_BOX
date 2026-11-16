/** @file qwen_audio.h @brief Qwen 录音预缓冲、下行播放与受检共享音频交接接口。 */
#ifndef QWEN_AUDIO_H
#define QWEN_AUDIO_H

#include "framework/d_error.h"

#include <stdint.h>

/**
 * @brief 初始化共享音频、静态缓冲及录音和播放工作任务。
 * @return 完成返回 D_OK；音量或队列、任务创建失败返回对应错误。
 * @note 初始化不代表已启动采集或播放；失败阶段按实现清理已建队列或线程。
 */
d_err_t qwen_audio_init(void);
/**
 * @brief 初始化前绑定硬件准备时复核的非阻塞准入探针。
 * @param[in] admitted 可空探针，非空时在后续音频准备过程中持续有效。
 */
void qwen_audio_bind_admission(uint8_t (*admitted)(void));
/**
 * @brief 读取本模块已完成初始化的标记。
 * @return 已初始化返回非零。
 */
uint8_t qwen_audio_is_initialized(void);
/**
 * @brief 检查播放意图、尾部、硬件视图或本地缓冲是否仍活动。
 * @return 任一活动返回 1，否则为 0。
 * @note 不作为底层 STOP 完成确认，资源交接使用 stop_all_wait。
 */
uint8_t qwen_audio_is_player_active(void);
/**
 * @brief 读取播放器启停推进的当前代次。
 * @return 当前播放代次，局部结束事件携带同一值。
 */
uint32_t qwen_audio_player_generation(void);
/**
 * @brief 检查运行播放器距上次进展是否达到给定期限。
 * @param[in] timeout_ms 停滞毫秒阈值，0 禁用判断。
 * @return 满足停滞条件返回 1，否则为 0。
 * @note 没有初始化、没有运行或尚无进展起点时不报停滞。
 */
uint8_t qwen_audio_is_player_stalled(uint32_t timeout_ms);
/**
 * @brief 投递退出并等待两个生产任务，再删除队列和清空本模块状态。
 * @note 调用者应先通过 stop_all_wait 确认共享底层停止；本入口无返回值，不提供独立底层 STOP 成功证明。
 */
void qwen_audio_deinit(void);
/**
 * @brief 提交固定服务提示音播放器准备，并有限等待硬件及预热。
 * @note 可阻塞；等待就绪超时只记录日志，无返回值。仅后台准备路径调用。
 */
void qwen_audio_prepare_player(void);
/**
 * @brief 提交预采集准备并有限等待 PREBUFFERING 或 FEEDING。
 * @return 观察到准备完成返回 D_OK；超过内部期限返回 D_ERR_TIMEOUT。
 * @note 命令投递失败仅日志，因此返回值为观察到的准备状态，不是逐次命令回执。
 */
d_err_t qwen_audio_prepare_recorder(void);
/**
 * @brief 非阻塞投递开始向 SDK 送录音数据的命令。
 */
void qwen_audio_start_recorder(void);
/**
 * @brief 先禁止继续喂 SDK，再投递录音停止意图。
 * @note 不等待共享编码器关闭完成。
 */
void qwen_audio_stop_recorder(void);
/**
 * @brief 非阻塞投递普通下行播放启动命令。
 */
void qwen_audio_start_player(void);
/**
 * @brief 投递流结束标记，让已缓存音频按尾部排空。
 * @note 流结束不等于本地播放已结束。
 */
void qwen_audio_finish_player(void);
/**
 * @brief 非阻塞投递播放器停止命令。
 */
void qwen_audio_stop_player(void);
/**
 * @brief 同时请求停止录音与播放，不等待底层收尾。
 */
void qwen_audio_stop_all(void);
/**
 * @brief 排空两个 Qwen 生产队列，再等待共享音频 FIFO 屏障和实际 STOP。
 * @param[in] timeout_ms 两层队列与实际停止共用的非零毫秒总期限。
 * @return 成功或未初始化返回 D_OK；零期限、队列满、SDK STOP 错误或超时返回对应错误。
 * @note 仅 Qwen 后台生命周期线程调用，不在 LVGL 或音频所有者任务等待。失败保留停止票据，超时保留下层屏障；重试加入原请求，成功才可交出音频占用。
 */
d_err_t qwen_audio_stop_all_wait(uint32_t timeout_ms);
/**
 * @brief 同步设置共享播放音量，成功后更新本地确认值。
 * @param[in] volume_percent 请求百分比，超过 100 按 100 处理。
 * @return 硬件设置成功返回 D_OK，失败返回 D_ERR_IO。
 */
d_err_t qwen_audio_set_player_volume(uint8_t volume_percent);
/**
 * @brief 在本地确认音量基础上同步调整并截断到 0～100。
 * @param[in] step_percent 有符号百分比步长。
 * @return 实际设置结果。
 */
d_err_t qwen_audio_adjust_player_volume(int8_t step_percent);
/**
 * @brief 同步静音或恢复上次非零音量。
 * @param[in] is_mute 非零静音，0 恢复；无恢复值时采用编译默认。
 * @return 实际设置结果。
 */
d_err_t qwen_audio_set_player_mute(uint8_t is_mute);
/**
 * @brief 读取最近一次硬件设置成功后的缓存音量。
 * @return 当前音量百分比。
 */
uint8_t qwen_audio_get_player_volume(void);

#endif
