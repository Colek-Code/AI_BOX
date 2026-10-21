/** @file d_local_prompt.h @brief Shared Pomodoro/preview/alarm audio with checked voice handoff. */
#ifndef D_LOCAL_PROMPT_H
#define D_LOCAL_PROMPT_H
#include "framework/d_error.h"
#include <stdint.h>
typedef enum {
    D_LOCAL_PROMPT_POMODORO = 0,
    D_LOCAL_PROMPT_PREVIEW,
    D_LOCAL_PROMPT_ALARM,
    D_LOCAL_PROMPT_SOURCE_COUNT,
} d_local_prompt_source_e;

typedef struct {
    d_local_prompt_source_e source;
    uint32_t event_id;
    uint32_t generation;
    uint32_t created_ms;
    uint8_t volume;
} d_local_prompt_request_t;

typedef struct {
    uint32_t event_id;
    uint32_t generation;
    d_err_t error;
    uint8_t ready;
    uint8_t pending;
    uint8_t playing; /* START acknowledged; cleared only after checked STOP. */
    uint8_t owned;   /* Also covers OPEN/START in flight and retained failures. */
} d_local_prompt_source_snapshot_t;

typedef struct {
    d_err_t (*begin)(uint32_t token);
    d_err_t (*poll)(uint32_t token, uint8_t *ready);
    void (*release)(uint32_t token);
} d_local_prompt_handoff_t;

/** All callbacks are nonblocking, static-lifetime and invoked without the prompt lock. */
d_err_t d_local_prompt_register_alarm_handoff(const d_local_prompt_handoff_t *handoff);
/** Pomodoro aliases generation to event_id for its legacy sequence. Other
 * sources have independent event identities and monotonically newer generations. */
d_err_t d_local_prompt_request_source(const d_local_prompt_request_t *request);
void d_local_prompt_cancel_source(d_local_prompt_source_e source, uint32_t through_generation);
void d_local_prompt_get_source_snapshot(d_local_prompt_source_e source, d_local_prompt_source_snapshot_t *snapshot);
uint8_t d_local_prompt_alarm_requested(void);
typedef struct {
    d_err_t error;
    uint32_t event_id;
    uint32_t generation;
    uint8_t ready;
    uint8_t playing;
    uint8_t pending;
    uint8_t foreground;
    uint8_t quiescing;
} d_local_prompt_snapshot_t;
d_err_t d_local_prompt_init(void);
/** Integration supplies a static-lifetime, nonblocking snapshot probe; service has no upward dependency. */
d_err_t d_local_prompt_register_foreground_probe(uint8_t (*probe)(void));
void d_local_prompt_get_snapshot(d_local_prompt_snapshot_t *snapshot);
/** Latest event wins; age measured on the device monotonic clock. */
void d_local_prompt_request(uint32_t event_id, uint32_t created_ms);
void d_local_prompt_cancel(uint32_t event_id);
/** Nonblocking intent; wait belongs to a background audio owner. */
void d_local_prompt_foreground_begin(void);
void d_local_prompt_foreground_end(void);
d_err_t d_local_prompt_wait_stopped(uint32_t timeout_ms);
/** Device-volume worker only, never the UI or the prompt worker. */
d_err_t d_local_prompt_apply_volume(uint8_t percent, uint32_t timeout_ms);
void d_local_prompt_quiesce_begin(void);
void d_local_prompt_quiesce_cancel(void);
#endif
