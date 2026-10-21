/**
 * @file product_ui_control_widgets.h
 * @brief Private control-center widget constructors (JXY dark solid chrome).
 */

#ifndef __PRODUCT_UI_CONTROL_WIDGETS_H__
#define __PRODUCT_UI_CONTROL_WIDGETS_H__

#include "modules/applications/product_ui/product_ui.h"

typedef struct {
    lv_obj_t *title;
    lv_obj_t *subtitle;
    /* In-card transport (child of media card shell). */
    lv_obj_t *transport;
    lv_obj_t *previous;
    lv_obj_t *play;
    lv_obj_t *next;
    lv_obj_t *play_glyph;
    uint8_t is_playing;
} product_ui_media_controls_t;

typedef struct {
    lv_obj_t *root;
    lv_obj_t *fill;
    lv_obj_t *value_label;
    lv_obj_t *icon;
    int32_t confirmed_value;
    uint8_t is_dragging;
} product_ui_slider_view_t;

typedef enum {
    PRODUCT_UI_CONTROL_UNAVAILABLE,
    PRODUCT_UI_CONTROL_INACTIVE,
    PRODUCT_UI_CONTROL_ACTIVE,
} product_ui_control_state_e;

typedef enum {
    PRODUCT_UI_QUICK_TILE_FOCUS = 0,
    PRODUCT_UI_QUICK_TILE_ALARM,
    PRODUCT_UI_QUICK_TILE_ROTATION,
    PRODUCT_UI_QUICK_TILE_SETTINGS,
    PRODUCT_UI_QUICK_TILE_COUNT
} product_ui_quick_tile_e;

typedef struct {
    int16_t diameter;
    product_ui_quick_tile_e tile_id;
    const char *semantic_name;
} product_ui_quick_card_spec_t;

typedef struct {
    lv_obj_t *root;
    lv_obj_t *icon;
    product_ui_quick_tile_e tile_id;
    const char *semantic_name;
    product_ui_control_state_e state;
} product_ui_quick_card_view_t;

/* Passive info cards (Wi-Fi / focus): no callbacks, block scrim hit only. */
typedef struct {
    lv_obj_t *root;
    lv_obj_t *icon;
    lv_obj_t *title;
    lv_obj_t *subtitle;
    product_ui_control_state_e state;
} product_ui_info_card_view_t;

/* Media info card only (cover + title + subtitle). No transport buttons. */
lv_obj_t *product_ui_media_card_create(
    lv_obj_t *parent, int16_t width, int16_t height,
    product_ui_media_controls_t *controls);

/*
 * In-card transport: three non-overlapping 36x36 targets at card-local y=75.
 * Visual disc 21 + glyph 12 centered; no ext_click_area.
 */
lv_obj_t *product_ui_media_transport_create(
    lv_obj_t *parent, product_ui_media_controls_t *controls);

/* Controller binds CLICKED; widgets do not call services. */
void product_ui_media_transport_bind(
    product_ui_media_controls_t *controls, lv_event_cb_t cb, void *user_data);

void product_ui_media_card_set_text(product_ui_media_controls_t *controls,
                                    const char *title,
                                    const char *subtitle);
void product_ui_media_card_set_playing(product_ui_media_controls_t *controls,
                                       uint8_t is_playing);

lv_obj_t *product_ui_info_card_create(
    lv_obj_t *parent, int16_t width, int16_t height,
    product_ui_icon_id_t icon_id, const char *title,
    product_ui_info_card_view_t *view);
void product_ui_info_card_set_state(product_ui_info_card_view_t *view,
                                    product_ui_control_state_e state,
                                    const char *subtitle);

lv_obj_t *product_ui_vertical_slider_create(
    lv_obj_t *parent, int16_t width, int16_t height,
    product_ui_icon_id_t icon_id, int32_t value,
    product_ui_slider_view_t *view);
void product_ui_slider_apply_value(product_ui_slider_view_t *view,
                                   int32_t value);

lv_obj_t *product_ui_quick_card_create(
    lv_obj_t *parent, const product_ui_quick_card_spec_t *spec,
    product_ui_quick_card_view_t *view);
void product_ui_quick_card_set_state(product_ui_quick_card_view_t *view,
                                     product_ui_control_state_e state);

#endif /* __PRODUCT_UI_CONTROL_WIDGETS_H__ */
