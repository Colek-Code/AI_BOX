/** @file d_audio_stop_ticket.h @brief One in-flight stop command and its completion. */
#ifndef D_AUDIO_STOP_TICKET_H
#define D_AUDIO_STOP_TICKET_H
#include <stdint.h>
typedef struct {
    volatile uint32_t requested;
    volatile uint32_t completed;
    uint8_t pending;
    uint8_t submitted;
} d_audio_stop_ticket_t;
/** A timed-out operation retains its ticket; only the owner starts/finishes it. */
uint32_t d_audio_stop_ticket_begin(d_audio_stop_ticket_t *ticket);
void d_audio_stop_ticket_submitted(d_audio_stop_ticket_t *ticket);
void d_audio_stop_ticket_complete(d_audio_stop_ticket_t *ticket, uint32_t id);
uint8_t d_audio_stop_ticket_ready(const d_audio_stop_ticket_t *ticket);
void d_audio_stop_ticket_finish(d_audio_stop_ticket_t *ticket);
#endif
