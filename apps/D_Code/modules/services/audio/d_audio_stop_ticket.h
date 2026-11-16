/** @file d_audio_stop_ticket.h @brief 音频停止占用票据：区分提交与完成确认，不分配动态内存。 */
#ifndef D_AUDIO_STOP_TICKET_H
#define D_AUDIO_STOP_TICKET_H
#include <stdint.h>
typedef struct {
    volatile uint32_t requested;
    volatile uint32_t completed;
    uint8_t pending;
    uint8_t submitted;
} d_audio_stop_ticket_t;
/**
 * @brief 开始或复用当前停止请求的占用票据。
 * @param[in,out] ticket 所有者维护的票据。
 * @return 当前非零请求编号；ticket 为空时返回 0。
 * @note 超时不撤销票据；开始和结束由同一所有者协调，本接口不加锁。
 */
uint32_t d_audio_stop_ticket_begin(d_audio_stop_ticket_t *ticket);
/**
 * @brief 标记停止命令已提交。
 * @param[in,out] ticket 待标记票据；为空时不处理。
 */
void d_audio_stop_ticket_submitted(d_audio_stop_ticket_t *ticket);
/**
 * @brief 为仍匹配的非零请求编号记录完成确认。
 * @param[in,out] ticket 待确认票据；为空时不处理。
 * @param[in] id 回调携带的停止请求编号。
 */
void d_audio_stop_ticket_complete(d_audio_stop_ticket_t *ticket, uint32_t id);
/**
 * @brief 检查停止命令已提交且已收到当前请求的完成确认。
 * @param[in] ticket 待检查票据；允许为空。
 * @return 条件全部满足时返回非零，否则返回 0。
 */
uint8_t d_audio_stop_ticket_ready(const d_audio_stop_ticket_t *ticket);
/**
 * @brief 仅在停止完成确认齐备时解除 pending 状态。
 * @param[in,out] ticket 所有者持有的票据；允许为空。
 */
void d_audio_stop_ticket_finish(d_audio_stop_ticket_t *ticket);
#endif
