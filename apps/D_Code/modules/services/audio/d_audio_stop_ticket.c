/** @file d_audio_stop_ticket.c @brief 音频停止占用票据的无动态分配实现。 */
#include "d_audio_stop_ticket.h"
#include <stddef.h>

/**
 * @brief 产生非零请求编号并复用尚未结束的请求；完整合同见 d_audio_stop_ticket.h。
 */
uint32_t d_audio_stop_ticket_begin(d_audio_stop_ticket_t *ticket)
{
    if (ticket == NULL) return 0U;
    if (!ticket->pending) {
        ticket->requested++;
        if (!ticket->requested) ticket->requested++;
        ticket->submitted = 0U;
        ticket->pending = 1U;
    }
    return ticket->requested;
}

/**
 * @brief 记录提交状态；完整合同见 d_audio_stop_ticket.h。
 */
void d_audio_stop_ticket_submitted(d_audio_stop_ticket_t *ticket)
{
    if (ticket != NULL) ticket->submitted = 1U;
}

/**
 * @brief 过滤旧请求的完成确认；完整合同见 d_audio_stop_ticket.h。
 */
void d_audio_stop_ticket_complete(d_audio_stop_ticket_t *ticket, uint32_t id)
{
    if (ticket != NULL && id != 0U && id == ticket->requested)
        ticket->completed = id;
}

/**
 * @brief 检查提交和完成编号是否一致；完整合同见 d_audio_stop_ticket.h。
 */
uint8_t d_audio_stop_ticket_ready(const d_audio_stop_ticket_t *ticket)
{
    return ticket != NULL && ticket->pending && ticket->submitted &&
           ticket->requested == ticket->completed;
}

/**
 * @brief 在确认完成后解除占用；完整合同见 d_audio_stop_ticket.h。
 */
void d_audio_stop_ticket_finish(d_audio_stop_ticket_t *ticket)
{
    if (d_audio_stop_ticket_ready(ticket)) ticket->pending = 0U;
}
