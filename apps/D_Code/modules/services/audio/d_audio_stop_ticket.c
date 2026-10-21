/** @file d_audio_stop_ticket.c @brief Allocation-free stop acknowledgement bookkeeping. */
#include "d_audio_stop_ticket.h"
#include <stddef.h>

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

void d_audio_stop_ticket_submitted(d_audio_stop_ticket_t *ticket)
{
    if (ticket != NULL) ticket->submitted = 1U;
}

void d_audio_stop_ticket_complete(d_audio_stop_ticket_t *ticket, uint32_t id)
{
    if (ticket != NULL && id != 0U && id == ticket->requested)
        ticket->completed = id;
}

uint8_t d_audio_stop_ticket_ready(const d_audio_stop_ticket_t *ticket)
{
    return ticket != NULL && ticket->pending && ticket->submitted &&
           ticket->requested == ticket->completed;
}

void d_audio_stop_ticket_finish(d_audio_stop_ticket_t *ticket)
{
    if (d_audio_stop_ticket_ready(ticket)) ticket->pending = 0U;
}
