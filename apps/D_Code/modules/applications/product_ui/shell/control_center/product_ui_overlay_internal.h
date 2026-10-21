/**
 * @file product_ui_overlay_internal.h
 * @brief Internal control-center content contract (JXY dark solid layout).
 */

#ifndef __PRODUCT_UI_OVERLAY_INTERNAL_H__
#define __PRODUCT_UI_OVERLAY_INTERNAL_H__

#include "lvgl.h"
#include "modules/applications/product_ui/shell/control_center/product_ui_control_widgets.h"
#include "modules/applications/product_ui/runtime/status/product_ui_status_model.h"

/* 3 media + 2 sliders + 4 quick = 9 */
#define PRODUCT_UI_CONTROL_INTERACTIVE_MAX    9
#define PRODUCT_UI_CONTROL_QUICK_COUNT         4
#define PRODUCT_UI_CONTROL_PASSIVE_HIT_MAX    2

typedef struct {
    /* Independent CC WLAN projection (not status-bar LINK_OFF mapping). */
    uint32_t service_version;
    uint8_t capability_mask;
    uint8_t valid_mask;
    uint8_t has_saved_credentials;
    uint8_t conn_state;
    uint8_t conn_error;
    uint8_t ready;
} product_ui_cc_wifi_cache_t;

typedef struct {
    /*
     * layout: slide host — must stay non-CLICKABLE (empty taps close overlay).
     * frame: solid black full-screen sheet.
     * interactive[]: media×3 + sliders×2 + quick×4; CLICKABLE only at 1000.
     * passive_hit[]: Wi-Fi / focus info cards (geometry hit only).
     */
    lv_obj_t *layout;
    lv_obj_t *frame;
    lv_obj_t *media_card;
    lv_obj_t *interactive[PRODUCT_UI_CONTROL_INTERACTIVE_MAX];
    lv_obj_t *passive_hit[PRODUCT_UI_CONTROL_PASSIVE_HIT_MAX];
    product_ui_media_controls_t media;
    product_ui_slider_view_t sliders[2];
    product_ui_quick_card_view_t quick_cards[PRODUCT_UI_CONTROL_QUICK_COUNT];
    product_ui_info_card_view_t wifi_card;
    product_ui_info_card_view_t focus_card;
    product_ui_cc_wifi_cache_t wifi_cache;
    uint8_t wifi_cache_initialized;
    uint8_t interactive_count;
    uint8_t passive_hit_count;
    uint8_t is_open_visual;
    uint8_t is_fully_open;
    uint32_t settings_revision;
    uint32_t volume_feedback_request;
    uint8_t volume_feedback_state;
    uint32_t media_revision;
    uint32_t alarm_revision;
    uint32_t system_revision;
    uint8_t refresh_initialized;
} product_ui_control_center_content_t;

int product_ui_control_center_content_create(
    lv_obj_t *panel, product_ui_control_center_content_t *content);
void product_ui_control_center_content_refresh(
    product_ui_control_center_content_t *content);
void product_ui_control_center_content_set_progress(
    product_ui_control_center_content_t *content, uint16_t progress_permille);
uint8_t product_ui_control_center_content_hit_controls(
    const product_ui_control_center_content_t *content, int16_t x, int16_t y);

#endif /* __PRODUCT_UI_OVERLAY_INTERNAL_H__ */
