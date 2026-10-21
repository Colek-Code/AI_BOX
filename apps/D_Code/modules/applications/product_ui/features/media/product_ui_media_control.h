/**
 * @file product_ui_media_control.h
 * @brief Media command binding and control-center projection.
 *
 * Registers PRODUCT_UI_SERVICE_MEDIA only when a real audio/media backend
 * is ready. Without registration the service stays UNAVAILABLE and UI
 * shows toast — never invents play state.
 */

#ifndef PRODUCT_UI_MEDIA_CONTROL_H
#define PRODUCT_UI_MEDIA_CONTROL_H

#include "config/app_config.h"
#include <stdint.h>

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

/**
 * Attempt to bind media commands. Returns 0 if registered or intentionally
 * skipped (no backend); non-zero only on hard failure.
 */
int product_ui_media_control_try_register(void);

typedef struct {
    uint8_t service_ready;
    uint8_t command_available;
    uint32_t valid_mask;
} product_ui_media_service_view_t;

typedef struct {
    uint8_t transport_enabled;
    uint8_t show_pause_icon;
    uint8_t is_unavailable;
} product_ui_media_cc_view_t;

void product_ui_media_cc_project(
    const product_ui_media_service_view_t *service,
    product_ui_media_cc_view_t *out);
void product_ui_media_publish_valid_mask(
    uint8_t is_playing, uint32_t *valid_mask);

#endif
#endif /* PRODUCT_UI_MEDIA_CONTROL_H */
