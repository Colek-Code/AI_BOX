/** @file product_ui_alarm_audio.c @brief 通过共享本地提示音所有者发起闹钟及试听请求。 */
#include "config/app_config.h"
#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)
#include "modules/applications/product_ui/features/time/product_ui_alarm_port.h"
#include "modules/applications/product_ui/features/time/product_ui_clock_port.h"
#include "modules/services/audio/d_local_prompt.h"
#include "system/spinlock.h"
#include <stddef.h>

static uint32_t s_preview_generation;
static DEFINE_SPINLOCK(s_preview_lock);

/**
 * @brief 复制本地提示音闹钟来源的事件、代次及资源状态。
 */
void product_ui_alarm_audio_snapshot(product_ui_alarm_audio_snapshot_t *snapshot)
{
    d_local_prompt_source_snapshot_t source;
    if (snapshot == NULL) return;
    d_local_prompt_get_source_snapshot(D_LOCAL_PROMPT_ALARM, &source);
    *snapshot = (product_ui_alarm_audio_snapshot_t){source.event_id, source.generation, source.error,
        source.ready, source.pending, source.playing, source.owned};
}

/**
 * @brief 把闹钟事件、尝试代次及音量交给本地提示音受理。
 */
d_err_t product_ui_alarm_audio_request(uint32_t event_id, uint32_t attempt, uint8_t volume)
{
    d_local_prompt_request_t request = {D_LOCAL_PROMPT_ALARM, event_id, attempt,
        product_ui_clock_port_ms(), volume};
    return d_local_prompt_request_source(&request);
}

/**
 * @brief 请求取消不晚于指定尝试代次的闹钟提示，零代次不操作。
 */
void product_ui_alarm_audio_cancel(uint32_t through_attempt)
{
    /* 尝试代次零表示尚未发出请求，不能借此取消较新的请求。 */
    if (through_attempt) d_local_prompt_cancel_source(D_LOCAL_PROMPT_ALARM, through_attempt);
}

/**
 * @brief 在自旋锁内分配非零试听代次，再向提示音服务提交。
 */
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

/**
 * @brief 借用当前试听代次，取消此前已发出的试听。
 */
void product_ui_alarm_audio_preview_cancel(void)
{
    uint32_t generation;
    spin_lock(&s_preview_lock); generation = s_preview_generation; spin_unlock(&s_preview_lock);
    if (generation) d_local_prompt_cancel_source(D_LOCAL_PROMPT_PREVIEW, generation);
}

/**
 * @brief 投影试听是否待处理、播放或仍占有资源，并报告来源错误。
 */
d_err_t product_ui_alarm_audio_preview_status(uint8_t *active)
{
    d_local_prompt_source_snapshot_t snapshot;
    if (active == NULL) return D_ERR_INVALID_ARG;
    d_local_prompt_get_source_snapshot(D_LOCAL_PROMPT_PREVIEW, &snapshot);
    *active = snapshot.pending || snapshot.playing || snapshot.owned;
    return snapshot.ready ? snapshot.error : D_ERR_NOT_READY;
}
#endif
