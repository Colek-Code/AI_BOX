/** @file d_local_prompt_decoder.h @brief 由 local_prompt 任务独占的 PCM 提示音解码适配。 */
#ifndef D_LOCAL_PROMPT_DECODER_H
#define D_LOCAL_PROMPT_DECODER_H
#include "framework/d_error.h"
#include <stdint.h>
/**
 * @brief 以单次模式打开并启动提示音解码。
 * @param[in] generation 非零播放代次。
 * @param[in] volume 0～100 的播放音量。
 * @return D_OK 表示启动命令成功；其余为占用、内存或解码错误。
 * @note 失败时可能保留打开的服务，调用方须继续通过 stop 确认收尾。
 */
d_err_t d_local_prompt_decoder_start(uint32_t generation, uint8_t volume);
/**
 * @brief 以循环模式打开并启动闹铃提示音。
 * @param[in] generation 非零播放代次。
 * @param[in] volume 0～100 的播放音量。
 * @return D_OK 表示启动命令成功；其余为占用、内存或解码错误。
 * @note 循环由虚拟 PCM 长度实现；失败后的服务收尾由调用方执行 stop。
 */
d_err_t d_local_prompt_decoder_start_alarm(uint32_t generation, uint8_t volume);
/**
 * @brief 在 OPEN 后、START 前再次核实播放所有者。
 * @param[in] generation 非零播放代次。
 * @param[in] volume 0～100 的音量。
 * @param[in] repeating 非零表示以带静音间隔的循环 PCM 播放。
 * @param[in] permit_start 可选启动准入回调；传入当前代次。
 * @return D_OK 表示 START 成功；检查回调拒绝时透传其错误，其余返回占用、内存或 I/O 错误。
 * @note 由 local_prompt 任务串行调用；准入失败或 START 失败时可能保留已打开的服务，调用方必须检查 stop 的结果。
 */
d_err_t d_local_prompt_decoder_start_checked(uint32_t generation, uint8_t volume, uint8_t repeating,
    d_err_t (*permit_start)(uint32_t generation));
/**
 * @brief 停止已打开的解码器并关闭服务。
 * @return D_OK 表示服务为空或已关闭；D_ERR_IO 表示 STOP 失败且服务仍保留。
 * @note STOP 失败时不宣称释放，调用方保留所有权并重试。
 */
d_err_t d_local_prompt_decoder_stop(void);
/**
 * @brief 设置当前解码器音量。
 * @param[in] volume 0～100 的音量。
 * @return 越界返回 D_ERR_INVALID_ARG；未打开或设置成功返回 D_OK；请求失败返回 D_ERR_IO。
 */
d_err_t d_local_prompt_decoder_volume(uint8_t volume);
/**
 * @brief 读取当前播放是否收到结束或错误事件。
 * @return 非零表示已收到终止事件，0 表示尚未收到。
 */
uint8_t d_local_prompt_decoder_finished(void);
/**
 * @brief 读取当前播放的事件结果。
 * @return 初始及正常结束为 D_OK；错误事件置为 D_ERR_IO。
 */
d_err_t d_local_prompt_decoder_result(void);
#endif
