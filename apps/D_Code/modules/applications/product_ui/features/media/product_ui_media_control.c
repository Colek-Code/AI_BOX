/**
 * @file product_ui_media_control.c
 * @brief Media command binding and control-center projection.
 */

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

#include "modules/applications/product_ui/features/media/product_ui_media_control.h"
#include "modules/applications/product_ui/runtime/service_registry/product_ui_service_registry.h"
#include <stddef.h>

/*
 * No on-board audio player is wired yet. Keep this file as the single
 * registration point for PREVIOUS / TOGGLE_PLAY_PAUSE / NEXT.
 *
 * When a real backend lands:
 *  1. Implement the media command callback against that backend.
 *  2. On READY always keep PRODUCT_UI_MEDIA_CAP_TRANSPORT via
 *     product_ui_media_publish_valid_mask(); only toggle MEDIA_STATE_PLAYING.
 *  3. Pause must not clear capability / transport valid bits.
 *  4. Call product_ui_service_register_command + publish READY here.
 *
 * Until then MEDIA stays UNAVAILABLE. Control center shows "未接入",
 * transport stays disabled and dimmed, and clicks are swallowed (no toast,
 * no close, no fake play state).
 */

int product_ui_media_control_try_register(void)
{
    /* Backend not present: do not register command or claim READY. */
    return 0;
}

void product_ui_media_cc_project(
    const product_ui_media_service_view_t *service,
    product_ui_media_cc_view_t *out)
{
    uint8_t ready;

    if (out == NULL) {
        return;
    }
    out->transport_enabled = 0U;
    out->show_pause_icon = 0U;
    out->is_unavailable = 1U;
    if (service == NULL) {
        return;
    }
    ready = (uint8_t)((service->service_ready != 0U) &&
                      (service->command_available != 0U));
    if (ready == 0U) {
        return;
    }
    out->is_unavailable = 0U;
    out->transport_enabled = 1U;
    out->show_pause_icon =
        (uint8_t)((service->valid_mask & PRODUCT_UI_MEDIA_STATE_PLAYING) != 0U);
}

void product_ui_media_publish_valid_mask(
    uint8_t is_playing, uint32_t *valid_mask)
{
    if (valid_mask == NULL) {
        return;
    }
    *valid_mask = PRODUCT_UI_MEDIA_CAP_TRANSPORT;
    if (is_playing != 0U) {
        *valid_mask |= PRODUCT_UI_MEDIA_STATE_PLAYING;
    }
}

#endif /* D_LVGL_ENABLE && (D_UI_TARGET == D_UI_PRODUCT) */
