/** @file product_ui_alarm_audio.c @brief Typed requests to the existing shared local-prompt owner. */
#include "config/app_config.h"
#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)
#include "modules/applications/product_ui/features/time/product_ui_alarm_port.h"
#include "modules/applications/product_ui/features/time/product_ui_clock_port.h"
#include "modules/services/audio/d_local_prompt.h"
#include "system/spinlock.h"
#include <stddef.h>

static uint32_t s_preview_generation;
static DEFINE_SPINLOCK(s_preview_lock);

void product_ui_alarm_audio_snapshot(product_ui_alarm_audio_snapshot_t *snapshot)
{
    d_local_prompt_source_snapshot_t source;
    if (snapshot == NULL) return;
    d_local_prompt_get_source_snapshot(D_LOCAL_PROMPT_ALARM, &source);
    *snapshot = (product_ui_alarm_audio_snapshot_t){source.event_id, source.generation, source.error,
        source.ready, source.pending, source.playing, source.owned};
}

d_err_t product_ui_alarm_audio_request(uint32_t event_id, uint32_t attempt, uint8_t volume)
{
    d_local_prompt_request_t request = {D_LOCAL_PROMPT_ALARM, event_id, attempt,
        product_ui_clock_port_ms(), volume};
    return d_local_prompt_request_source(&request);
}

void product_ui_alarm_audio_cancel(uint32_t through_attempt)
{
    /* Attempt zero means no issued request, never "cancel a newer request". */
    if (through_attempt) d_local_prompt_cancel_source(D_LOCAL_PROMPT_ALARM, through_attempt);
}

d_err_t product_ui_alarm_audio_preview(uint8_t volume)
{
    d_local_prompt_request_t request = {0};
    spin_lock(&s_preview_lock);
    if (++s_preview_generation == 0U) ++s_preview_generation;
    request.event_id = request.generation = s_preview_generation;
    spin_unlock(&s_preview_lock);
    request.source = D_LOCAL_PROMPT_PREVIEW;
    request.created_ms = product_ui_clock_port_ms();
    request.volume = volume;
    return d_local_prompt_request_source(&request);
}

void product_ui_alarm_audio_preview_cancel(void)
{
    uint32_t generation;
    spin_lock(&s_preview_lock); generation = s_preview_generation; spin_unlock(&s_preview_lock);
    if (generation) d_local_prompt_cancel_source(D_LOCAL_PROMPT_PREVIEW, generation);
}

d_err_t product_ui_alarm_audio_preview_status(uint8_t *active)
{
    d_local_prompt_source_snapshot_t snapshot;
    if (active == NULL) return D_ERR_INVALID_ARG;
    d_local_prompt_get_source_snapshot(D_LOCAL_PROMPT_PREVIEW, &snapshot);
    *active = snapshot.pending || snapshot.playing || snapshot.owned;
    return snapshot.ready ? snapshot.error : D_ERR_NOT_READY;
}
#endif
