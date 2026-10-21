/**
 * @file product_ui_overlay_content.c
 * @brief Control center: JXY dark solid layout (320x240, 2/3×3/4 adapt).
 */

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

#include "modules/applications/product_ui/shell/control_center/product_ui_overlay_internal.h"
#include "modules/applications/product_ui/shell/control_center/product_ui_control_widgets.h"
#include "modules/applications/product_ui/shared/widgets/product_ui_components.h"
#include "modules/applications/product_ui/product_ui_runtime.h"
#include "modules/applications/product_ui/debug/product_ui_trace.h"
#include "modules/applications/product_ui/shared/theme/product_ui_theme.h"
#include "modules/applications/product_ui/product_ui.h"
#include "modules/applications/product_ui/runtime/service_registry/product_ui_service_registry.h"
#include "modules/applications/product_ui/features/media/product_ui_media_control.h"
#include "modules/applications/product_ui/features/settings/product_ui_settings_service.h"
#include "modules/applications/product_ui/runtime/connectivity/product_ui_connection_service.h"
#include "modules/applications/product_ui/debug/product_ui_visual_capture.h"
#include <string.h>

/* Layout: product_ui_theme.h PRODUCT_UI_CC_* (sole geometry source). */

static const product_ui_quick_card_spec_t s_quick_specs[] = {
    {PRODUCT_UI_CC_QUICK_SIZE, PRODUCT_UI_QUICK_TILE_FOCUS, "专注"},
    {PRODUCT_UI_CC_QUICK_SIZE, PRODUCT_UI_QUICK_TILE_ALARM, "闹钟"},
    {PRODUCT_UI_CC_QUICK_SIZE, PRODUCT_UI_QUICK_TILE_ROTATION, "自动旋转"},
    {PRODUCT_UI_CC_QUICK_SIZE, PRODUCT_UI_QUICK_TILE_SETTINGS, "设置"},
};

typedef struct {
    product_ui_service_snapshot_t settings;
    product_ui_service_snapshot_t media;
    product_ui_service_snapshot_t alarm;
    product_ui_service_snapshot_t system;
} product_ui_control_services_t;

static void product_ui_control_set_enabled(
    product_ui_control_center_content_t *content, lv_obj_t *control,
    uint8_t is_enabled);
static void product_ui_media_action_cb(lv_event_t *event);

static void product_ui_control_clean(lv_obj_t *obj)
{
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE |
                      LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_style_layout(obj, LV_LAYOUT_NONE, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
}

static int product_ui_control_record_interactive(
    product_ui_control_center_content_t *content, lv_obj_t *obj)
{
    if ((obj == NULL) ||
        (content->interactive_count >= PRODUCT_UI_CONTROL_INTERACTIVE_MAX)) {
        return -1;
    }
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    content->interactive[content->interactive_count++] = obj;
    return 0;
}

static int product_ui_control_record_passive(
    product_ui_control_center_content_t *content, lv_obj_t *obj)
{
    if ((obj == NULL) ||
        (content->passive_hit_count >= PRODUCT_UI_CONTROL_PASSIVE_HIT_MAX)) {
        return -1;
    }
    content->passive_hit[content->passive_hit_count++] = obj;
    return 0;
}

static void product_ui_control_center_content_refresh_force(
    product_ui_control_center_content_t *content)
{
    if (content == NULL) {
        return;
    }
    /* Bypass revision cache; re-project authoritative service snapshots. */
    content->refresh_initialized = 0U;
    content->wifi_cache_initialized = 0U;
    product_ui_control_center_content_refresh(content);
}

static void product_ui_slider_release_cb(lv_event_t *event)
{
    product_ui_control_center_content_t *content;
    lv_obj_t *slider;
    product_ui_settings_command_e command;
    uint8_t index;
    int result;

    slider = lv_event_get_target(event);
    content = lv_event_get_user_data(event);
    if ((slider == NULL) || (content == NULL) || !content->is_fully_open) {
        return;
    }
    if (lv_obj_has_state(slider, LV_STATE_DISABLED)) {
        return;
    }
    for (index = 0; index < 2U; index++) {
        if (content->sliders[index].root == slider) {
            break;
        }
    }
    if (index >= 2U) {
        return;
    }
    if (!content->sliders[index].is_dragging) {
        product_ui_control_center_content_refresh_force(content);
        return;
    }
    content->sliders[index].is_dragging = 0U;
    if (lv_event_get_code(event) == LV_EVENT_PRESS_LOST ||
        lv_event_get_code(event) == LV_EVENT_INDEV_RESET) {
        product_ui_control_center_content_refresh_force(content);
        return;
    }
    if (index == 0U) {
        command = PRODUCT_UI_SETTINGS_SET_BRIGHTNESS;
    } else {
        command = PRODUCT_UI_SETTINGS_SET_VOLUME;
    }
    result = product_ui_settings_service_command(
        command, (uint8_t)lv_slider_get_value(slider));
    if (result != 0) {
        /* LVGL already moved the knob; restore from authoritative snapshot. */
        product_ui_control_center_content_refresh_force(content);
        PRODUCT_UI_TRACE("slider_command name=%s result=%d\n", index == 0U ? "brightness" : "volume", result);
        product_ui_toast_show("设置失败", 1200U);
        return;
    }
    PRODUCT_UI_TRACE("slider_change name=%s value=%d\n", index == 0U ? "brightness" : "volume",
                     lv_slider_get_value(slider));
    product_ui_control_center_content_refresh_force(content);
}

static int product_ui_layout_create(
    lv_obj_t *panel, product_ui_control_center_content_t *content)
{
    content->layout = lv_obj_create(panel);
    if (content->layout == NULL) {
        return -1;
    }
    product_ui_control_clean(content->layout);
    lv_obj_clear_flag(content->layout, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(content->layout, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_size(content->layout, PRODUCT_UI_SCREEN_W, PRODUCT_UI_SCREEN_H);
    lv_obj_set_pos(content->layout, 0, -PRODUCT_UI_SCREEN_H);
    lv_obj_set_style_bg_opa(content->layout, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(content->layout, LV_OBJ_FLAG_HIDDEN);
    return 0;
}

static int product_ui_frame_create(product_ui_control_center_content_t *content)
{
    content->frame = lv_obj_create(content->layout);
    if (content->frame == NULL) {
        return -1;
    }
    product_ui_control_clean(content->frame);
    lv_obj_set_size(content->frame, PRODUCT_UI_SCREEN_W, PRODUCT_UI_SCREEN_H);
    lv_obj_set_pos(content->frame, 0, 0);
    lv_obj_set_style_bg_color(
        content->frame, product_ui_color(PRODUCT_UI_COLOR_CONTROL_SCRIM), 0);
    lv_obj_set_style_bg_opa(content->frame, LV_OPA_COVER, 0);
    lv_obj_clear_flag(content->frame, LV_OBJ_FLAG_CLICKABLE |
                      LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE |
                      LV_OBJ_FLAG_EVENT_BUBBLE);
    return 0;
}

static int product_ui_media_create(product_ui_control_center_content_t *content)
{
    lv_obj_t *transport;

    memset(&content->media, 0, sizeof(content->media));
    content->media_card = product_ui_media_card_create(
        content->layout, PRODUCT_UI_CC_MEDIA_W, PRODUCT_UI_CC_MEDIA_H,
        &content->media);
    if (content->media_card == NULL) {
        return -1;
    }
    lv_obj_set_pos(content->media_card, PRODUCT_UI_CC_MEDIA_X,
                   PRODUCT_UI_CC_MEDIA_Y);

    /* Parent = media card → coordinates local, cannot leave music shell. */
    transport = product_ui_media_transport_create(content->media_card,
                                                  &content->media);
    if (transport == NULL) {
        return -1;
    }
    product_ui_media_transport_bind(&content->media, product_ui_media_action_cb,
                                    content);
    if ((product_ui_control_record_interactive(
             content, content->media.previous) != 0) ||
        (product_ui_control_record_interactive(
             content, content->media.play) != 0) ||
        (product_ui_control_record_interactive(
             content, content->media.next) != 0)) {
        return -1;
    }
    return 0;
}

static void product_ui_media_action_cb(lv_event_t *event)
{
    product_ui_control_center_content_t *content;
    lv_obj_t *target;
    uint16_t command;

    content = lv_event_get_user_data(event);
    target = lv_event_get_target(event);
    if ((content == NULL) || (target == NULL) || !content->is_fully_open) {
        return;
    }
    /* Unavailable: swallow — no toast, no close, no fake play. */
    if (lv_obj_has_state(target, LV_STATE_DISABLED) ||
        !lv_obj_has_flag(target, LV_OBJ_FLAG_CLICKABLE)) {
        return;
    }
    command = (uint16_t)(uintptr_t)lv_obj_get_user_data(target);
    if (!product_ui_service_command_available(PRODUCT_UI_SERVICE_MEDIA) ||
        !product_ui_service_is_ready(PRODUCT_UI_SERVICE_MEDIA)) {
        PRODUCT_UI_TRACE("media_action command=%u result=unavailable\n",
                         command);
        return;
    }
    if (product_ui_service_command(PRODUCT_UI_SERVICE_MEDIA,
                                   command, NULL, 0U) != 0) {
        product_ui_control_center_content_refresh_force(content);
        return;
    }
    product_ui_control_center_content_refresh_force(content);
    PRODUCT_UI_TRACE("media_action command=%u result=ok\n", command);
}

static int product_ui_control_settings_snapshot(
    product_ui_settings_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return -1;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    if (!product_ui_service_is_ready(PRODUCT_UI_SERVICE_SETTINGS) ||
        !product_ui_service_command_available(PRODUCT_UI_SERVICE_SETTINGS)) {
        return -1;
    }
    return product_ui_settings_service_snapshot(snapshot);
}

static uint8_t product_ui_slider_cap_ok(
    const product_ui_settings_snapshot_t *snapshot, int snapshot_result,
    uint32_t cap)
{
    if ((snapshot_result != 0) || (snapshot == NULL)) {
        return 0U;
    }
    if (((snapshot->capability_mask & cap) == 0U) ||
        ((snapshot->valid_mask & cap) == 0U)) {
        return 0U;
    }
    return 1U;
}

static void product_ui_slider_show_unavailable(product_ui_slider_view_t *view)
{
    if ((view == NULL) || (view->root == NULL)) {
        return;
    }
    view->is_dragging = 0U;
    /* Keep solid dark shell visible; mute fill/icon only. */
    lv_obj_clear_flag(view->root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_state(view->root, LV_STATE_DISABLED);
    lv_obj_clear_flag(view->root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_opa(view->root, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(view->root, LV_OPA_COVER, LV_PART_MAIN);
    product_ui_slider_apply_value(view, 0);
    if (view->fill != NULL) {
        lv_obj_add_flag(view->fill, LV_OBJ_FLAG_HIDDEN);
    }
    if (view->icon != NULL) {
        lv_obj_set_style_image_recolor(
            view->icon,
            product_ui_color(PRODUCT_UI_COLOR_CONTROL_TEXT_MUTED), 0);
        lv_obj_set_style_image_recolor_opa(view->icon, LV_OPA_COVER, 0);
        lv_obj_set_style_opa(view->icon, LV_OPA_50, 0);
    }
    if (view->value_label != NULL) {
        lv_obj_add_flag(view->value_label, LV_OBJ_FLAG_HIDDEN);
    }
}

static int product_ui_sliders_create(
    product_ui_control_center_content_t *content)
{
    product_ui_settings_snapshot_t snapshot;
    lv_obj_t *brightness;
    lv_obj_t *volume;
    int snapshot_result;
    uint8_t bri_ok;
    uint8_t vol_ok;

    snapshot_result = product_ui_control_settings_snapshot(&snapshot);
    bri_ok = product_ui_slider_cap_ok(&snapshot, snapshot_result,
                                      PRODUCT_UI_SETTING_CAP_BRIGHTNESS);
    vol_ok = product_ui_slider_cap_ok(&snapshot, snapshot_result,
                                      PRODUCT_UI_SETTING_CAP_VOLUME);

    brightness = product_ui_vertical_slider_create(
        content->layout, PRODUCT_UI_CC_SLIDER_W, PRODUCT_UI_CC_SLIDER_H,
        PRODUCT_UI_ICON_CC_BRIGHTNESS,
        bri_ok ? snapshot.brightness_percent : 0,
        &content->sliders[0]);
    volume = product_ui_vertical_slider_create(
        content->layout, PRODUCT_UI_CC_SLIDER_W, PRODUCT_UI_CC_SLIDER_H,
        PRODUCT_UI_ICON_CC_VOLUME,
        vol_ok ? product_ui_settings_volume_display(&snapshot) : 0,
        &content->sliders[1]);
    if ((brightness == NULL) || (volume == NULL)) {
        return -1;
    }
    lv_obj_set_pos(brightness, PRODUCT_UI_CC_SLIDER_0_X, PRODUCT_UI_CC_SLIDER_Y);
    lv_obj_set_pos(volume, PRODUCT_UI_CC_SLIDER_1_X, PRODUCT_UI_CC_SLIDER_Y);
#if defined(PRODUCT_UI_BRIGHTNESS_MIN_PERCENT)
    lv_slider_set_range(brightness,
                        (int32_t)PRODUCT_UI_BRIGHTNESS_MIN_PERCENT, 100);
#endif
    if ((product_ui_control_record_interactive(content, brightness) != 0) ||
        (product_ui_control_record_interactive(content, volume) != 0)) {
        return -1;
    }
    lv_obj_add_event_cb(brightness, product_ui_slider_release_cb,
                        LV_EVENT_RELEASED, content);
    lv_obj_add_event_cb(volume, product_ui_slider_release_cb,
                        LV_EVENT_RELEASED, content);
    lv_obj_add_event_cb(brightness, product_ui_slider_release_cb,
                        LV_EVENT_PRESS_LOST, content);
    lv_obj_add_event_cb(volume, product_ui_slider_release_cb,
                        LV_EVENT_PRESS_LOST, content);
    lv_obj_add_event_cb(brightness, product_ui_slider_release_cb,
                        LV_EVENT_INDEV_RESET, content);
    lv_obj_add_event_cb(volume, product_ui_slider_release_cb,
                        LV_EVENT_INDEV_RESET, content);

    if (bri_ok) {
        product_ui_slider_apply_value(
            &content->sliders[0], lv_slider_get_value(brightness));
    } else {
        product_ui_slider_show_unavailable(&content->sliders[0]);
    }
    if (vol_ok) {
        product_ui_slider_apply_value(
            &content->sliders[1], lv_slider_get_value(volume));
    } else {
        product_ui_slider_show_unavailable(&content->sliders[1]);
    }
    return 0;
}

static int product_ui_info_cards_create(
    product_ui_control_center_content_t *content)
{
    if (product_ui_info_card_create(
            content->layout, PRODUCT_UI_CC_WIFI_W, PRODUCT_UI_CC_WIFI_H,
            PRODUCT_UI_ICON_CC_WIFI, "WLAN", &content->wifi_card) == NULL) {
        return -1;
    }
    lv_obj_set_pos(content->wifi_card.root, PRODUCT_UI_CC_WIFI_X,
                   PRODUCT_UI_CC_WIFI_Y);
    if (product_ui_control_record_passive(content, content->wifi_card.root) !=
        0) {
        return -1;
    }

    if (product_ui_info_card_create(
            content->layout, PRODUCT_UI_CC_FOCUS_W, PRODUCT_UI_CC_FOCUS_H,
            PRODUCT_UI_ICON_CC_FOCUS, "专注模式",
            &content->focus_card) == NULL) {
        return -1;
    }
    lv_obj_set_pos(content->focus_card.root, PRODUCT_UI_CC_FOCUS_X,
                   PRODUCT_UI_CC_FOCUS_Y);
    if (product_ui_control_record_passive(content, content->focus_card.root) !=
        0) {
        return -1;
    }
    product_ui_info_card_set_state(&content->focus_card,
                                   PRODUCT_UI_CONTROL_UNAVAILABLE, "未接入");
    return 0;
}

static uint8_t product_ui_control_services_update(
    product_ui_control_center_content_t *content,
    product_ui_control_services_t *services)
{
    if ((content == NULL) || (services == NULL)) {
        return 0U;
    }
    (void)product_ui_service_snapshot(PRODUCT_UI_SERVICE_SETTINGS,
                                      &services->settings);
    (void)product_ui_service_snapshot(PRODUCT_UI_SERVICE_MEDIA,
                                      &services->media);
    (void)product_ui_service_snapshot(PRODUCT_UI_SERVICE_ALARM,
                                      &services->alarm);
    (void)product_ui_service_snapshot(PRODUCT_UI_SERVICE_SYSTEM,
                                      &services->system);
    if (content->refresh_initialized &&
        (content->settings_revision == services->settings.revision) &&
        (content->media_revision == services->media.revision) &&
        (content->alarm_revision == services->alarm.revision) &&
        (content->system_revision == services->system.revision)) {
        return 0U;
    }
    content->settings_revision = services->settings.revision;
    content->media_revision = services->media.revision;
    content->alarm_revision = services->alarm.revision;
    content->system_revision = services->system.revision;
    content->refresh_initialized = 1U;
    return 1U;
}

static void product_ui_control_volume_feedback(
    product_ui_control_center_content_t *content,
    const product_ui_settings_snapshot_t *snapshot)
{
    if (snapshot == NULL || snapshot->volume_request_id == 0U) return;
    if (snapshot->volume_state != PRODUCT_UI_VOLUME_TIMEOUT &&
        !(snapshot->volume_state == PRODUCT_UI_VOLUME_ERROR &&
          snapshot->volume_completed_id == snapshot->volume_request_id)) return;
    if (content->volume_feedback_request == snapshot->volume_request_id &&
        content->volume_feedback_state == snapshot->volume_state) return;
    content->volume_feedback_request = snapshot->volume_request_id;
    content->volume_feedback_state = (uint8_t)snapshot->volume_state;
    product_ui_toast_show(product_ui_settings_volume_status(snapshot), 1800U);
}

static void product_ui_control_sliders_refresh(
    product_ui_control_center_content_t *content,
    const product_ui_settings_snapshot_t *snapshot,
    uint8_t settings_enabled)
{
    static const uint32_t slider_caps[] = {
        PRODUCT_UI_SETTING_CAP_BRIGHTNESS,
        PRODUCT_UI_SETTING_CAP_VOLUME,
    };
    uint8_t slider_values[2];
    uint8_t index;
    uint8_t cap_ok;

    if (content == NULL) {
        return;
    }
    slider_values[0] = 0U;
    slider_values[1] = 0U;
    for (index = 0; index < 2U; index++) {
        if (content->sliders[index].root == NULL) {
            continue;
        }
        cap_ok = 0U;
        if ((settings_enabled != 0U) && (snapshot != NULL) &&
            ((snapshot->capability_mask & slider_caps[index]) != 0U) &&
            ((snapshot->valid_mask & slider_caps[index]) != 0U)) {
            cap_ok = 1U;
            if (index == 0U) {
                slider_values[0] = snapshot->brightness_percent;
#if defined(PRODUCT_UI_BRIGHTNESS_MIN_PERCENT)
                if (slider_values[0] < PRODUCT_UI_BRIGHTNESS_MIN_PERCENT) {
                    slider_values[0] = PRODUCT_UI_BRIGHTNESS_MIN_PERCENT;
                }
#endif
            } else {
                slider_values[1] = product_ui_settings_volume_display(snapshot);
            }
        }
        if (cap_ok) {
            if (index == 1U && !product_ui_settings_volume_can_adjust(snapshot))
                content->sliders[index].is_dragging = 0U;
            lv_obj_clear_flag(content->sliders[index].root, LV_OBJ_FLAG_HIDDEN);
            product_ui_control_set_enabled(
                content, content->sliders[index].root,
                index == 0U || product_ui_settings_volume_can_adjust(snapshot));
            lv_obj_set_style_opa(content->sliders[index].root, LV_OPA_COVER,
                                 0);
            if (content->sliders[index].icon != NULL) {
                lv_obj_set_style_opa(content->sliders[index].icon,
                                     LV_OPA_COVER, 0);
                lv_obj_set_style_image_recolor_opa(
                    content->sliders[index].icon, LV_OPA_COVER, 0);
            }
            if (content->sliders[index].fill != NULL) {
                lv_obj_clear_flag(content->sliders[index].fill,
                                  LV_OBJ_FLAG_HIDDEN);
            }
            product_ui_slider_apply_value(&content->sliders[index],
                                          (int32_t)slider_values[index]);
            if (index == 1U && !content->sliders[index].is_dragging) {
                lv_obj_t *value_label = content->sliders[index].value_label;
                if (value_label != NULL && snapshot->volume_state == PRODUCT_UI_VOLUME_APPLYING)
                    lv_label_set_text_static(value_label, "...");
                else if (value_label != NULL &&
                         (snapshot->volume_state == PRODUCT_UI_VOLUME_TIMEOUT ||
                          snapshot->volume_state == PRODUCT_UI_VOLUME_ERROR))
                    lv_label_set_text_static(value_label, "!");
            }
        } else {
            product_ui_slider_show_unavailable(&content->sliders[index]);
        }
    }
    product_ui_control_volume_feedback(content, snapshot);
}

static void product_ui_control_media_refresh(
    product_ui_control_center_content_t *content,
    const product_ui_service_snapshot_t *media_service)
{
    product_ui_media_service_view_t service_view;
    product_ui_media_cc_view_t view;

    if ((content == NULL) || (media_service == NULL)) {
        return;
    }
    /* Registry snapshot: state + valid_mask only (no capability_mask field). */
    service_view.service_ready =
        (uint8_t)(media_service->state == PRODUCT_UI_SERVICE_READY);
    service_view.command_available =
        (uint8_t)product_ui_service_command_available(PRODUCT_UI_SERVICE_MEDIA);
    service_view.valid_mask = media_service->valid_mask;
    product_ui_media_cc_project(&service_view, &view);

    if (view.is_unavailable == 0U) {
        if (view.show_pause_icon != 0U) {
            product_ui_media_card_set_text(&content->media, "本地音乐", "播放中");
        } else {
            product_ui_media_card_set_text(&content->media, "本地音乐", "未播放");
        }
        product_ui_media_card_set_playing(&content->media, view.show_pause_icon);
    } else {
        /* Unavailable: "未接入", no fake play glyph state. */
        product_ui_media_card_set_text(&content->media, "音乐", "未接入");
        product_ui_media_card_set_playing(&content->media, 0U);
    }
    /* Keep hits in overlay tree; disabled swallows click (no toast/close). */
    product_ui_control_set_enabled(content, content->media.previous,
                                   view.transport_enabled);
    product_ui_control_set_enabled(content, content->media.play,
                                   view.transport_enabled);
    product_ui_control_set_enabled(content, content->media.next,
                                   view.transport_enabled);
    /* Unavailable transport: whole strip ~55% opacity; restore on READY. */
    if (content->media.transport != NULL) {
        lv_obj_set_style_opa(content->media.transport,
                             (view.transport_enabled != 0U) ? LV_OPA_COVER :
                                                              LV_OPA_50,
                             0);
    }
}

static void product_ui_control_runtime_quick_refresh(
    product_ui_control_center_content_t *content,
    const product_ui_control_services_t *services)
{
    uint8_t alarm_ready;
    uint8_t system_ready;
    product_ui_control_state_e alarm_state;
    product_ui_control_state_e system_state;

    if ((content == NULL) || (services == NULL)) {
        return;
    }
    /* Focus is permanently unavailable; stay in hit-test, never clickable. */
    product_ui_quick_card_set_state(&content->quick_cards[0],
                                    PRODUCT_UI_CONTROL_UNAVAILABLE);
    product_ui_control_set_enabled(content, content->quick_cards[0].root, 0U);

    /*
     * Enable from capability bit; ACTIVE chrome from state bit.
     * READY + CAP + OFF (no STATE) must remain clickable.
     */
    /* Opening the list remains useful when timing or storage is unavailable. */
    alarm_ready = product_ui_module_available("alarm");
    system_ready =
        (uint8_t)(product_ui_service_command_available(
                      PRODUCT_UI_SERVICE_SYSTEM) &&
                  ((services->system.valid_mask &
                    PRODUCT_UI_SYSTEM_CAP_ROTATION) != 0U));
    if (alarm_ready == 0U) {
        alarm_state = PRODUCT_UI_CONTROL_UNAVAILABLE;
    } else if ((services->alarm.valid_mask &
                PRODUCT_UI_ALARM_STATE_ACTIVE) != 0U) {
        alarm_state = PRODUCT_UI_CONTROL_ACTIVE;
    } else {
        alarm_state = PRODUCT_UI_CONTROL_INACTIVE;
    }
    if (system_ready == 0U) {
        system_state = PRODUCT_UI_CONTROL_UNAVAILABLE;
    } else if ((services->system.valid_mask &
                PRODUCT_UI_SYSTEM_STATE_ROTATION_ON) != 0U) {
        system_state = PRODUCT_UI_CONTROL_ACTIVE;
    } else {
        system_state = PRODUCT_UI_CONTROL_INACTIVE;
    }
    product_ui_quick_card_set_state(&content->quick_cards[1], alarm_state);
    product_ui_control_set_enabled(content, content->quick_cards[1].root,
                                   alarm_ready);
    product_ui_quick_card_set_state(&content->quick_cards[2], system_state);
    product_ui_control_set_enabled(content, content->quick_cards[2].root,
                                   system_ready);
    /* Settings is navigation — never latched ACTIVE blue; always clickable. */
    product_ui_quick_card_set_state(&content->quick_cards[3],
                                    PRODUCT_UI_CONTROL_INACTIVE);
    product_ui_control_set_enabled(content, content->quick_cards[3].root, 1U);
}

static uint8_t product_ui_control_wifi_changed(
    product_ui_control_center_content_t *content)
{
    product_ui_connection_snapshot_t conn = {0};
    product_ui_cc_wifi_cache_t next;

    if (content == NULL) {
        return 0U;
    }
    /* Producer sync runs in product_ui_process — view only consumes snapshot. */
    memset(&next, 0, sizeof(next));
    if (product_ui_connection_service_snapshot(&conn) == 0) {
        next.service_version = conn.service_version;
        next.capability_mask = conn.wifi_capable;
        next.valid_mask = conn.wifi_capable;
        next.has_saved_credentials = conn.has_saved_credentials;
        next.conn_state = conn.conn_state;
        next.conn_error = conn.conn_error;
        next.ready = conn.ready;
    }
    if ((content->wifi_cache_initialized != 0U) &&
        (content->wifi_cache.ready == next.ready) &&
        (content->wifi_cache.service_version == next.service_version) &&
        (content->wifi_cache.capability_mask == next.capability_mask) &&
        (content->wifi_cache.valid_mask == next.valid_mask) &&
        (content->wifi_cache.has_saved_credentials ==
         next.has_saved_credentials) &&
        (content->wifi_cache.conn_state == next.conn_state) &&
        (content->wifi_cache.conn_error == next.conn_error)) {
        return 0U;
    }
    content->wifi_cache = next;
    content->wifi_cache_initialized = 1U;
    return 1U;
}

static void product_ui_control_wifi_refresh(
    product_ui_control_center_content_t *content)
{
    product_ui_control_state_e state;
    const char *sub;
    const product_ui_cc_wifi_cache_t *wifi;

    if (content == NULL) {
        return;
    }
    /*
     * CC projection from Product UI connection snapshot only.
     * Do not reuse status-bar LINK_OFF folding (IDLE with no creds → 等待配网).
     */
    wifi = &content->wifi_cache;
    if ((wifi->ready == 0U) || (wifi->capability_mask == 0U)) {
        state = PRODUCT_UI_CONTROL_UNAVAILABLE;
        sub = "不可用";
    } else if (wifi->conn_state == (uint8_t)PRODUCT_UI_CONN_STATE_CONNECTED) {
        state = PRODUCT_UI_CONTROL_ACTIVE;
        sub = "已连接";
    } else if ((wifi->conn_state ==
                (uint8_t)PRODUCT_UI_CONN_STATE_CONNECTING) ||
               (wifi->conn_state ==
                (uint8_t)PRODUCT_UI_CONN_STATE_ASSOCIATED)) {
        state = PRODUCT_UI_CONTROL_INACTIVE;
        sub = "连接中";
    } else if ((wifi->conn_state == (uint8_t)PRODUCT_UI_CONN_STATE_IDLE) &&
               (wifi->has_saved_credentials == 0U)) {
        state = PRODUCT_UI_CONTROL_INACTIVE;
        sub = "等待配网";
    } else if ((wifi->conn_state ==
                (uint8_t)PRODUCT_UI_CONN_STATE_DISCONNECTED) &&
               (wifi->conn_error == (uint8_t)PRODUCT_UI_CONN_ERR_NONE)) {
        state = PRODUCT_UI_CONTROL_INACTIVE;
        sub = "未连接";
    } else if ((wifi->conn_state == (uint8_t)PRODUCT_UI_CONN_STATE_ERROR) ||
               ((wifi->conn_state ==
                 (uint8_t)PRODUCT_UI_CONN_STATE_DISCONNECTED) &&
                (wifi->conn_error != (uint8_t)PRODUCT_UI_CONN_ERR_NONE))) {
        state = PRODUCT_UI_CONTROL_INACTIVE;
        sub = "连接失败";
    } else {
        state = PRODUCT_UI_CONTROL_INACTIVE;
        sub = "未连接";
    }
    product_ui_info_card_set_state(&content->wifi_card, state, sub);
}

static int product_ui_quick_find_index(
    const product_ui_control_center_content_t *content, const lv_obj_t *target)
{
    uint8_t index;

    for (index = 0; index < PRODUCT_UI_CONTROL_QUICK_COUNT; index++) {
        if (content->quick_cards[index].root == target) {
            return index;
        }
    }
    return -1;
}

static void product_ui_control_set_enabled(
    product_ui_control_center_content_t *content, lv_obj_t *control,
    uint8_t is_enabled)
{
    if ((content == NULL) || (control == NULL)) {
        return;
    }
    if (is_enabled) {
        lv_obj_remove_state(control, LV_STATE_DISABLED);
        if (content->is_fully_open) {
            lv_obj_add_flag(control, LV_OBJ_FLAG_CLICKABLE);
        } else {
            lv_obj_clear_flag(control, LV_OBJ_FLAG_CLICKABLE);
        }
    } else {
        lv_obj_add_state(control, LV_STATE_DISABLED);
        lv_obj_clear_flag(control, LV_OBJ_FLAG_CLICKABLE);
    }
}

static void product_ui_quick_action_cb(lv_event_t *event)
{
    product_ui_control_center_content_t *content = lv_event_get_user_data(event);
    product_ui_quick_card_view_t *view;
    int index;

    if (content == NULL || !content->is_fully_open) {
        return;
    }
    index = product_ui_quick_find_index(content, lv_event_get_target(event));
    if (index < 0) {
        return;
    }
    view = &content->quick_cards[index];
    if (lv_obj_has_state(view->root, LV_STATE_DISABLED) ||
        !lv_obj_has_flag(view->root, LV_OBJ_FLAG_CLICKABLE)) {
        return;
    }
    if (index == 3) {
        PRODUCT_UI_TRACE("quick_action=%s state=momentary\n",
                         view->semantic_name);
        if (product_ui_open_module("product_settings") != 0) {
            product_ui_toast_show("设置打开失败", 1200U);
        }
        return;
    }
    if (index == 0) {
        return;
    }
    if (index == 1) {
        if (product_ui_open_module("alarm") != 0) product_ui_toast_show("闹钟打开失败", 1200U);
        return;
    } else if (index == 2) {
        if (!product_ui_service_is_ready(PRODUCT_UI_SERVICE_SYSTEM) ||
            !product_ui_service_command_available(PRODUCT_UI_SERVICE_SYSTEM)) {
            return;
        }
        if (product_ui_service_command(
                PRODUCT_UI_SERVICE_SYSTEM,
                (uint16_t)PRODUCT_UI_SYSTEM_CMD_TOGGLE_ROTATION, NULL,
                0U) != 0) {
            product_ui_control_center_content_refresh_force(content);
            return;
        }
    } else {
        return;
    }
    product_ui_control_center_content_refresh_force(content);
    PRODUCT_UI_TRACE("quick_action=%s state=%u\n",
                     view->semantic_name, view->state);
}

static int product_ui_quick_create(product_ui_control_center_content_t *content)
{
    static const int16_t s_quick_x[4] = {
        PRODUCT_UI_CC_QUICK0_X, PRODUCT_UI_CC_QUICK1_X,
        PRODUCT_UI_CC_QUICK0_X, PRODUCT_UI_CC_QUICK1_X,
    };
    static const int16_t s_quick_y[4] = {
        PRODUCT_UI_CC_QUICK_Y0, PRODUCT_UI_CC_QUICK_Y0,
        PRODUCT_UI_CC_QUICK_Y1, PRODUCT_UI_CC_QUICK_Y1,
    };
    lv_obj_t *button;
    uint8_t index;

    for (index = 0; index < PRODUCT_UI_CONTROL_QUICK_COUNT; index++) {
        button = product_ui_quick_card_create(
            content->layout, &s_quick_specs[index],
            &content->quick_cards[index]);
        if (button == NULL) {
            return -1;
        }
        lv_obj_set_pos(button, s_quick_x[index], s_quick_y[index]);
        if (product_ui_control_record_interactive(content, button) != 0) {
            return -1;
        }
        lv_obj_add_event_cb(button, product_ui_quick_action_cb,
                            LV_EVENT_CLICKED, content);
    }
    product_ui_quick_card_set_state(&content->quick_cards[0],
                                    PRODUCT_UI_CONTROL_UNAVAILABLE);
    product_ui_quick_card_set_state(
        &content->quick_cards[1],
        product_ui_service_is_ready(PRODUCT_UI_SERVICE_ALARM) ?
            PRODUCT_UI_CONTROL_INACTIVE : PRODUCT_UI_CONTROL_UNAVAILABLE);
    product_ui_quick_card_set_state(
        &content->quick_cards[2],
        product_ui_service_is_ready(PRODUCT_UI_SERVICE_SYSTEM) ?
            PRODUCT_UI_CONTROL_INACTIVE : PRODUCT_UI_CONTROL_UNAVAILABLE);
    product_ui_quick_card_set_state(&content->quick_cards[3],
                                    PRODUCT_UI_CONTROL_INACTIVE);
    return 0;
}

static uint8_t product_ui_control_point_in_obj(const lv_obj_t *obj,
                                              int16_t x, int16_t y)
{
    lv_area_t area;

    if ((obj == NULL) || lv_obj_has_flag((lv_obj_t *)obj, LV_OBJ_FLAG_HIDDEN)) {
        return 0U;
    }
    lv_obj_get_coords((lv_obj_t *)obj, &area);
    if ((x >= area.x1) && (x <= area.x2) &&
        (y >= area.y1) && (y <= area.y2)) {
        return 1U;
    }
    return 0U;
}

uint8_t product_ui_control_center_content_hit_controls(
    const product_ui_control_center_content_t *content, int16_t x, int16_t y)
{
    uint8_t index;

    if ((content == NULL) || (content->is_fully_open == 0U)) {
        return 0U;
    }
    if (product_ui_control_point_in_obj(content->media_card, x, y) != 0U) {
        return 1U;
    }
    if (product_ui_control_point_in_obj(content->media.transport, x, y) !=
        0U) {
        return 1U;
    }
    for (index = 0; index < content->passive_hit_count; index++) {
        if (product_ui_control_point_in_obj(content->passive_hit[index],
                                            x, y) != 0U) {
            return 1U;
        }
    }
    for (index = 0; index < content->interactive_count; index++) {
        if (product_ui_control_point_in_obj(content->interactive[index],
                                            x, y) != 0U) {
            return 1U;
        }
    }
    return 0U;
}

void product_ui_control_center_content_refresh(
    product_ui_control_center_content_t *content)
{
    product_ui_settings_snapshot_t snapshot = {0};
    product_ui_control_services_t services = {0};
    uint8_t settings_enabled;
    uint8_t service_changed;
    uint8_t wifi_changed;
    int settings_snap_rc;

    if (content == NULL) {
        return;
    }
    service_changed = product_ui_control_services_update(content, &services);
    wifi_changed = product_ui_control_wifi_changed(content);

    if ((service_changed == 0U) && (wifi_changed == 0U)) {
        return;
    }
    if (service_changed != 0U) {
        settings_enabled = 0U;
        if (services.settings.state == PRODUCT_UI_SERVICE_READY) {
            settings_snap_rc = product_ui_control_settings_snapshot(&snapshot);
            settings_enabled = (uint8_t)(settings_snap_rc == 0);
        }
        product_ui_control_sliders_refresh(content, &snapshot, settings_enabled);
        product_ui_control_media_refresh(content, &services.media);
        product_ui_control_runtime_quick_refresh(content, &services);
    }
    if (wifi_changed != 0U) {
        product_ui_control_wifi_refresh(content);
    }
}

int product_ui_control_center_content_create(
    lv_obj_t *panel, product_ui_control_center_content_t *content)
{
    if ((panel == NULL) || (content == NULL)) {
        return -1;
    }
    memset(content, 0, sizeof(*content));
    if ((product_ui_layout_create(panel, content) != 0) ||
        (product_ui_frame_create(content) != 0) ||
        (product_ui_info_cards_create(content) != 0) ||
        (product_ui_media_create(content) != 0) ||
        (product_ui_sliders_create(content) != 0) ||
        (product_ui_quick_create(content) != 0)) {
        /* Tear down partial layout; do not leave half-built interactive set. */
        if (content->layout != NULL) {
            lv_obj_delete(content->layout);
        }
        memset(content, 0, sizeof(*content));
        return -1;
    }
    product_ui_control_center_content_refresh(content);
    product_ui_control_center_content_set_progress(content, 0);
    PRODUCT_UI_TRACE("panel_content mode=cc_solid interactive=%u passive=%u\n",
                     content->interactive_count, content->passive_hit_count);
    return 0;
}

void product_ui_control_center_content_set_progress(
    product_ui_control_center_content_t *content, uint16_t progress_permille)
{
    int32_t y;
    uint8_t index;
    uint8_t is_interactive;
    uint8_t was_open;

    if ((content == NULL) || (content->layout == NULL)) {
        return;
    }
    if (progress_permille > 1000U) {
        progress_permille = 1000U;
    }
    y = -((int32_t)PRODUCT_UI_SCREEN_H) +
        (((int32_t)PRODUCT_UI_SCREEN_H * (int32_t)progress_permille) / 1000);
    lv_obj_set_y(content->layout, y);

    was_open = content->is_fully_open;
    if (progress_permille == 0U) {
        lv_obj_add_flag(content->layout, LV_OBJ_FLAG_HIDDEN);
        content->is_open_visual = 0U;
        /* Force full service projection on next fully-open. */
        content->refresh_initialized = 0U;
    } else {
        lv_obj_clear_flag(content->layout, LV_OBJ_FLAG_HIDDEN);
        content->is_open_visual = 1U;
    }

    is_interactive = progress_permille == 1000U;
    content->is_fully_open = is_interactive;
    if (!is_interactive) {
        for (index = 0U; index < 2U; index++) {
            if (content->sliders[index].is_dragging) {
                content->sliders[index].is_dragging = 0U;
                product_ui_slider_apply_value(&content->sliders[index],
                                               content->sliders[index].confirmed_value);
            }
        }
    }
    if (content->layout != NULL) {
        lv_obj_clear_flag(content->layout, LV_OBJ_FLAG_CLICKABLE);
    }
    /* Refresh after the opening gesture finishes. Changing DISABLED states
     * here can reset LVGL's active input before it delivers RELEASED. */
    for (index = 0; index < content->interactive_count; index++) {
        if (is_interactive &&
            !lv_obj_has_state(content->interactive[index], LV_STATE_DISABLED)) {
            lv_obj_add_flag(content->interactive[index], LV_OBJ_FLAG_CLICKABLE);
        } else {
            lv_obj_clear_flag(content->interactive[index],
                              LV_OBJ_FLAG_CLICKABLE);
        }
    }
    if (is_interactive && (was_open == 0U)) {
        product_ui_visual_capture_request("CC-04");
        PRODUCT_UI_TRACE("panel_content_open y=%d interactive=1\n", (int)y);
    } else if ((progress_permille == 0U) && (was_open != 0U)) {
        PRODUCT_UI_TRACE("panel_content_closed\n");
    }
}

#endif /* D_LVGL_ENABLE && (D_UI_TARGET == D_UI_PRODUCT) */
