/**
 * @file product_ui_camera_controller.h
 * @brief Page intent and screen holds; hardware state belongs to camera_preview.
 * @author D_Code
 * @date 2026-09-11
 */
#ifndef __PRODUCT_UI_CAMERA_CONTROLLER_H__
#define __PRODUCT_UI_CAMERA_CONTROLLER_H__
#include "modules/services/camera/camera_preview.h"

typedef struct {
    camera_preview_snapshot_t snapshot;
    uint32_t session;
    uint32_t shutdown_epoch;
    const char *status;
    const char *button;
    uint8_t is_attached;
    uint8_t has_start_intent;
    uint8_t has_screen_hold;
    uint8_t was_screen_off;
    uint8_t can_retry;
} product_ui_camera_controller_t;

void product_ui_camera_enter(product_ui_camera_controller_t *controller);
void product_ui_camera_update(product_ui_camera_controller_t *controller);
void product_ui_camera_retry(product_ui_camera_controller_t *controller);
void product_ui_camera_leave(product_ui_camera_controller_t *controller);

#endif
