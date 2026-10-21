/**
 * @file product_ui_settings_device_pages.c
 * @brief Display, motion and sound detail pages.
 */

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

#include "modules/applications/product_ui/features/settings/product_ui_settings_details.h"
#include "modules/applications/product_ui/features/settings/product_ui_settings_details_common.h"
#include "framework/d_ui_module.h"
#include "framework/d_page_manager.h"
#include <string.h>

typedef struct {
    uint32_t elapsed_ms;
    int read_result;
    uint8_t render_pending;
} product_ui_settings_refresh_t;

static product_ui_settings_refresh_t s_display_refresh;
static product_ui_settings_refresh_t s_sound_refresh;

static product_ui_page_view_t s_display_page_view;
static product_ui_page_view_t s_sound_page_view;
static lv_obj_t *s_display_backlight_control;
static lv_obj_t *s_display_brightness_control;
static lv_obj_t *s_reduce_motion_control;
static lv_obj_t *s_appearance_status;
static uint8_t s_brightness_dragging;
static uint8_t s_volume_dragging;
static lv_obj_t *s_sound_volume_control;
static lv_obj_t *s_sound_volume_status;

typedef struct {
    const char *title;
    const char *subtitle;
    uint8_t checked;
    lv_event_cb_t callback;
} product_ui_settings_toggle_spec_t;

static void product_ui_settings_display_sync_brightness_slider(
    uint8_t brightness_percent)
{
    int32_t value;

    if (s_display_brightness_control == NULL) {
        return;
    }
    value = (int32_t)brightness_percent;
    if (value < (int32_t)PRODUCT_UI_BRIGHTNESS_MIN_PERCENT) {
        value = (int32_t)PRODUCT_UI_BRIGHTNESS_MIN_PERCENT;
    }
    if (value > (int32_t)PRODUCT_UI_BRIGHTNESS_MAX_PERCENT) {
        value = (int32_t)PRODUCT_UI_BRIGHTNESS_MAX_PERCENT;
    }
    /* Value only: apply cb is on RELEASED, so no recursive SET_BRIGHTNESS. */
    lv_slider_set_value(s_display_brightness_control, value, LV_ANIM_OFF);
    lv_obj_send_event(s_display_brightness_control, LV_EVENT_VALUE_CHANGED,
                      NULL);
}

static void product_ui_settings_backlight_cb(lv_event_t *event)
{
    lv_obj_t *control = lv_event_get_target(event);
    product_ui_settings_snapshot_t snapshot;
    uint8_t enabled;

    if (control == NULL) {
        return;
    }
    enabled = lv_obj_has_state(control, LV_STATE_CHECKED) ? 1U : 0U;
    if (product_ui_settings_service_command(
            PRODUCT_UI_SETTINGS_SET_BACKLIGHT, enabled) == 0) {
        /* Safe-dim off publishes brightness=MIN; keep slider in sync. */
        if (product_ui_settings_detail_snapshot(&snapshot) == 0) {
            product_ui_settings_display_sync_brightness_slider(
                snapshot.brightness_percent);
        }
        return;
    }
    if (enabled) {
        lv_obj_clear_state(control, LV_STATE_CHECKED);
    } else {
        lv_obj_add_state(control, LV_STATE_CHECKED);
    }
    product_ui_toast_show("背光控制失败", 1200U);
}

static void product_ui_settings_brightness_cb(lv_event_t *event)
{
    lv_obj_t *control = lv_event_get_target(event);
    product_ui_settings_snapshot_t snapshot;
    int32_t slider_value;

    if (control == NULL) {
        return;
    }
    if (lv_event_get_code(event) == LV_EVENT_PRESSED) {
        s_brightness_dragging = 1U;
        return;
    }
    if (!s_brightness_dragging || lv_event_get_code(event) == LV_EVENT_PRESS_LOST ||
        lv_event_get_code(event) == LV_EVENT_INDEV_RESET) {
        s_brightness_dragging = 0U;
        if (product_ui_settings_detail_snapshot(&snapshot) == 0) {
            product_ui_settings_display_sync_brightness_slider(snapshot.brightness_percent);
        }
        return;
    }
    s_brightness_dragging = 0U;
    slider_value = lv_slider_get_value(control);
    if (slider_value < (int32_t)PRODUCT_UI_BRIGHTNESS_MIN_PERCENT) {
        slider_value = (int32_t)PRODUCT_UI_BRIGHTNESS_MIN_PERCENT;
    }
    if (slider_value > (int32_t)PRODUCT_UI_BRIGHTNESS_MAX_PERCENT) {
        slider_value = (int32_t)PRODUCT_UI_BRIGHTNESS_MAX_PERCENT;
    }
    if (product_ui_settings_service_command(
            PRODUCT_UI_SETTINGS_SET_BRIGHTNESS,
            (uint8_t)slider_value) == 0) {
        /* Switch visual follows snapshot (MIN dim => unchecked). */
        if ((s_display_backlight_control != NULL) &&
            (product_ui_settings_detail_snapshot(&snapshot) == 0)) {
            if (snapshot.backlight_enabled != 0U) {
                lv_obj_add_state(s_display_backlight_control, LV_STATE_CHECKED);
            } else {
                lv_obj_clear_state(s_display_backlight_control,
                                   LV_STATE_CHECKED);
            }
            product_ui_settings_display_sync_brightness_slider(
                snapshot.brightness_percent);
        }
        return;
    }
    if (product_ui_settings_detail_snapshot(&snapshot) == 0) {
        int32_t revert = (int32_t)snapshot.brightness_percent;

        if (revert < (int32_t)PRODUCT_UI_BRIGHTNESS_MIN_PERCENT) {
            revert = (int32_t)PRODUCT_UI_BRIGHTNESS_MIN_PERCENT;
        }
        if (revert > (int32_t)PRODUCT_UI_BRIGHTNESS_MAX_PERCENT) {
            revert = (int32_t)PRODUCT_UI_BRIGHTNESS_MAX_PERCENT;
        }
        lv_slider_set_value(control, revert, LV_ANIM_OFF);
        lv_obj_send_event(control, LV_EVENT_VALUE_CHANGED, NULL);
    }
    product_ui_toast_show("亮度控制失败", 1200U);
}

static void product_ui_settings_volume_cb(lv_event_t *event)
{
    lv_obj_t *control = lv_event_get_target(event);
    product_ui_settings_snapshot_t snapshot;

    if (control == NULL) {
        return;
    }
    if (lv_event_get_code(event) == LV_EVENT_PRESSED) {
        s_volume_dragging = 1U;
        return;
    }
    if (!s_volume_dragging || lv_event_get_code(event) == LV_EVENT_PRESS_LOST ||
        lv_event_get_code(event) == LV_EVENT_INDEV_RESET) {
        s_volume_dragging = 0U;
        if (product_ui_settings_detail_snapshot(&snapshot) == 0) {
            lv_slider_set_value(control, product_ui_settings_volume_display(&snapshot), LV_ANIM_OFF);
            lv_obj_send_event(control, LV_EVENT_VALUE_CHANGED, NULL);
        }
        return;
    }
    s_volume_dragging = 0U;
    if (product_ui_settings_service_command(
            PRODUCT_UI_SETTINGS_SET_VOLUME,
            (uint8_t)lv_slider_get_value(control)) == 0) {
        return;
    }
    if (product_ui_settings_detail_snapshot(&snapshot) == 0) {
        lv_slider_set_value(control, product_ui_settings_volume_display(&snapshot), LV_ANIM_OFF);
        lv_obj_send_event(control, LV_EVENT_VALUE_CHANGED, NULL);
    }
    product_ui_toast_show("音量控制失败", 1200U);
}

static lv_obj_t *product_ui_settings_toggle_bind(
    lv_obj_t *content, const product_ui_settings_toggle_spec_t *toggle_spec)
{
    lv_obj_t *row;
    lv_obj_t *control;

    if ((content == NULL) || (toggle_spec == NULL)) {
        return NULL;
    }
    row = product_ui_toggle_row_create(content, toggle_spec->title,
                                       toggle_spec->subtitle,
                                       toggle_spec->checked);
    if (row == NULL) {
        return NULL;
    }
    control = product_ui_setting_row_get_control(row);
    if (control == NULL) {
        return NULL;
    }
    lv_obj_add_event_cb(control, toggle_spec->callback,
                        LV_EVENT_VALUE_CHANGED, NULL);
    return control;
}

/*
 * Bind a settings slider.
 * - Percent subtitle still updates on LV_EVENT_VALUE_CHANGED (row create).
 * - Hardware/service apply uses apply_code (RELEASED), matching control-center
 *   and avoiding PWM/volume flood while dragging (LVGL v9 VALUE_CHANGED/press).
 * - PRESS_LOST cancels the preview and restores the service snapshot.
 */
static int product_ui_settings_slider_bind(lv_obj_t *content,
                                           const char *title,
                                           uint8_t value,
                                           lv_event_cb_t callback,
                                           int32_t min_value,
                                           int32_t max_value,
                                           lv_obj_t **out_control)
{
    lv_obj_t *row;
    lv_obj_t *control;
    int32_t clamped;

    if (out_control != NULL) {
        *out_control = NULL;
    }
    row = product_ui_slider_row_create(content, title, value);
    if (row == NULL) {
        return -1;
    }
    control = product_ui_setting_row_get_control(row);
    if (control == NULL) {
        return -1;
    }
    if (min_value < max_value) {
        lv_slider_set_range(control, min_value, max_value);
        clamped = (int32_t)value;
        if (clamped < min_value) {
            clamped = min_value;
        }
        if (clamped > max_value) {
            clamped = max_value;
        }
        lv_slider_set_value(control, clamped, LV_ANIM_OFF);
        /*
         * Label-only refresh: apply callbacks are not registered yet, so this
         * does not issue SET_BRIGHTNESS/SET_VOLUME.
         */
        lv_obj_send_event(control, LV_EVENT_VALUE_CHANGED, NULL);
    }
    lv_obj_add_event_cb(control, callback, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(control, callback, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(control, callback, LV_EVENT_PRESS_LOST, NULL);
    lv_obj_add_event_cb(control, callback, LV_EVENT_INDEV_RESET, NULL);
    if (out_control != NULL) {
        *out_control = control;
    }
    return 0;
}

static void product_ui_settings_reduce_motion_sync(const product_ui_settings_snapshot_t *snapshot)
{
    uint8_t enabled;
    if (s_reduce_motion_control == NULL) return;
    enabled = snapshot->appearance.is_pending || snapshot->appearance.is_in_progress ?
        snapshot->appearance.target_reduce_motion : snapshot->appearance.reduce_motion;
    if (enabled) lv_obj_add_state(s_reduce_motion_control, LV_STATE_CHECKED);
    else lv_obj_clear_state(s_reduce_motion_control, LV_STATE_CHECKED);
    if (product_ui_settings_appearance_can_save(snapshot))
        lv_obj_remove_state(s_reduce_motion_control, LV_STATE_DISABLED);
    else lv_obj_add_state(s_reduce_motion_control, LV_STATE_DISABLED);
    if (s_appearance_status != NULL) {
        lv_label_set_text_static(s_appearance_status, product_ui_settings_appearance_status(snapshot));
    }
}

static void product_ui_settings_reduce_motion_cb(lv_event_t *event)
{
    product_ui_settings_snapshot_t snapshot;
    lv_obj_t *control = lv_event_get_target(event);
    uint8_t enabled = lv_obj_has_state(control, LV_STATE_CHECKED);
    if (product_ui_settings_service_command(PRODUCT_UI_SETTINGS_SET_REDUCE_MOTION, enabled) != D_OK) {
        product_ui_toast_show("暂时无法保存外观", 1400U);
    }
    if (product_ui_settings_service_snapshot(&snapshot) == 0) product_ui_settings_reduce_motion_sync(&snapshot);
}

static int product_ui_settings_display_content_create(lv_obj_t *content)
{
    product_ui_settings_snapshot_t snapshot;
    int result;

    s_display_refresh.read_result = product_ui_settings_detail_snapshot(&snapshot);
    if (s_display_refresh.read_result != 0) {
        return product_ui_settings_detail_feedback(
            content, PRODUCT_UI_FEEDBACK_ERROR, "显示设置不可用",
            "无法读取背光状态");
    }
    if (product_ui_settings_detail_value_ready(
            &snapshot, PRODUCT_UI_SETTING_CAP_BACKLIGHT_SWITCH)) {
        const product_ui_settings_toggle_spec_t toggle_spec = {
            .title = "屏幕背光",
            .subtitle = "开启或关闭屏幕背光",
            .checked = snapshot.backlight_enabled,
            .callback = product_ui_settings_backlight_cb,
        };

        s_display_backlight_control =
            product_ui_settings_toggle_bind(content, &toggle_spec);
        if (s_display_backlight_control == NULL) {
            return -1;
        }
    } else if (product_ui_settings_detail_feedback(
                   content, PRODUCT_UI_FEEDBACK_ERROR, "背光开关不可用",
                   "未提供有效的背光开关状态") != 0) {
        return -1;
    }

    if (product_ui_settings_detail_value_ready(
            &snapshot, PRODUCT_UI_SETTING_CAP_BRIGHTNESS)) {
        /* Slider stays enabled when switch is off (safe-dim at MIN). */
        result = product_ui_settings_slider_bind(
            content, "连续亮度", snapshot.brightness_percent,
            product_ui_settings_brightness_cb,
            (int32_t)PRODUCT_UI_BRIGHTNESS_MIN_PERCENT,
            (int32_t)PRODUCT_UI_BRIGHTNESS_MAX_PERCENT,
            &s_display_brightness_control);
    } else {
        result = product_ui_settings_detail_feedback(
            content, PRODUCT_UI_FEEDBACK_ERROR, "连续亮度不可用",
            "当前设备仅支持背光开关");
    }
    if (result != 0 || !snapshot.appearance.is_initialized) return result;
    {
        const product_ui_settings_toggle_spec_t motion = {
            .title = "减少动态效果", .subtitle = "关闭自动缓动与滚动惯性",
            .checked = snapshot.appearance.reduce_motion,
            .callback = product_ui_settings_reduce_motion_cb,
        };
        s_reduce_motion_control = product_ui_settings_toggle_bind(content, &motion);
    }
    s_appearance_status = lv_label_create(content);
    if (s_reduce_motion_control == NULL || s_appearance_status == NULL) return -1;
    lv_obj_add_style(s_appearance_status, product_ui_style(PRODUCT_UI_STYLE_CAPTION), 0);
    product_ui_settings_reduce_motion_sync(&snapshot);
    return 0;
}


int product_ui_settings_display_page_create(lv_obj_t *parent)
{
    lv_obj_t *content = product_ui_settings_detail_frame_create(
        parent, "显示与亮度", &s_display_page_view);

    s_display_backlight_control = NULL;
    s_display_brightness_control = NULL;
    s_reduce_motion_control = NULL;
    s_appearance_status = NULL;
    s_brightness_dragging = 0U;
    memset(&s_display_refresh, 0, sizeof(s_display_refresh));
    if (content == NULL) {
        return -1;
    }
    return product_ui_settings_display_content_create(content);
}

static int product_ui_settings_sound_content_create(lv_obj_t *content)
{
    product_ui_settings_snapshot_t snapshot;

    s_sound_refresh.read_result = product_ui_settings_detail_snapshot(&snapshot);
    if ((s_sound_refresh.read_result != 0) ||
        !product_ui_settings_detail_value_ready(
            &snapshot, PRODUCT_UI_SETTING_CAP_VOLUME)) {
        return product_ui_settings_detail_feedback(
            content, PRODUCT_UI_FEEDBACK_ERROR, "声音不可用",
            product_ui_settings_volume_status(s_sound_refresh.read_result == 0 ? &snapshot : NULL));
    }
    if (product_ui_settings_slider_bind(content, "音量",
            product_ui_settings_volume_display(&snapshot),
            product_ui_settings_volume_cb, 0, 100, &s_sound_volume_control) != 0) {
        return -1;
    }
    s_sound_volume_status = lv_label_create(content);
    if (s_sound_volume_status == NULL) {
        return -1;
    }
    lv_obj_add_style(s_sound_volume_status, product_ui_style(PRODUCT_UI_STYLE_CAPTION), 0);
    lv_label_set_text_static(s_sound_volume_status, product_ui_settings_volume_status(&snapshot));
    if (!product_ui_settings_volume_can_adjust(&snapshot)) {
        lv_obj_add_state(s_sound_volume_control, LV_STATE_DISABLED);
    }
    return 0;
}


int product_ui_settings_sound_page_create(lv_obj_t *parent)
{
    lv_obj_t *content = product_ui_settings_detail_frame_create(
        parent, "声音", &s_sound_page_view);

    s_sound_volume_control = NULL;
    s_sound_volume_status = NULL;
    s_volume_dragging = 0U;
    memset(&s_sound_refresh, 0, sizeof(s_sound_refresh));
    if (content == NULL) {
        return -1;
    }
    return product_ui_settings_sound_content_create(content);
}




static uint8_t product_ui_settings_refresh_due(product_ui_settings_refresh_t *refresh,
                                               uint32_t elapsed_ms)
{
    product_ui_settings_detail_page_update(elapsed_ms);
    if (elapsed_ms < 200U && refresh->elapsed_ms < 200U - elapsed_ms) {
        refresh->elapsed_ms += elapsed_ms;
        return 0U;
    }
    refresh->elapsed_ms = 0U;
    return 1U;
}

static void product_ui_settings_display_update(uint32_t elapsed_ms)
{
    product_ui_settings_snapshot_t snapshot = {0};
    lv_obj_t *content = s_display_page_view.content;
    uint8_t can_backlight;
    uint8_t can_brightness;
    uint8_t can_motion;
    int result;

    if (content == NULL || !product_ui_settings_refresh_due(&s_display_refresh, elapsed_ms)) {
        return;
    }
    result = product_ui_settings_detail_snapshot(&snapshot);
    can_backlight = result == 0 && product_ui_settings_detail_value_ready(
        &snapshot, PRODUCT_UI_SETTING_CAP_BACKLIGHT_SWITCH);
    can_brightness = result == 0 && product_ui_settings_detail_value_ready(
        &snapshot, PRODUCT_UI_SETTING_CAP_BRIGHTNESS);
    can_motion = result == 0 && snapshot.appearance.is_initialized;
    if (s_display_refresh.render_pending || result != s_display_refresh.read_result ||
        can_backlight != (s_display_backlight_control != NULL) ||
        can_brightness != (s_display_brightness_control != NULL) ||
        can_motion != (s_reduce_motion_control != NULL)) {
        s_brightness_dragging = 0U;
        s_display_backlight_control = NULL;
        s_display_brightness_control = NULL;
        s_reduce_motion_control = NULL;
        s_appearance_status = NULL;
        lv_obj_clean(content);
        s_display_refresh.render_pending = product_ui_settings_display_content_create(content) != 0;
    } else {
        if (can_backlight) {
            if (snapshot.backlight_enabled) {
                lv_obj_add_state(s_display_backlight_control, LV_STATE_CHECKED);
            } else {
                lv_obj_clear_state(s_display_backlight_control, LV_STATE_CHECKED);
            }
        }
        if (can_brightness && !s_brightness_dragging &&
            lv_slider_get_value(s_display_brightness_control) != snapshot.brightness_percent) {
            product_ui_settings_display_sync_brightness_slider(snapshot.brightness_percent);
        }
        if (can_motion) product_ui_settings_reduce_motion_sync(&snapshot);
    }
    s_display_refresh.read_result = result;
    if (s_display_refresh.render_pending) {
        s_display_backlight_control = NULL;
        s_display_brightness_control = NULL;
        s_reduce_motion_control = NULL;
        s_appearance_status = NULL;
        lv_obj_clean(content);
    }
}

static void product_ui_settings_sound_update(uint32_t elapsed_ms)
{
    product_ui_settings_snapshot_t snapshot = {0};
    lv_obj_t *content = s_sound_page_view.content;
    uint8_t can_volume;
    int result;

    if (content == NULL || !product_ui_settings_refresh_due(&s_sound_refresh, elapsed_ms)) {
        return;
    }
    result = product_ui_settings_detail_snapshot(&snapshot);
    can_volume = result == 0 && product_ui_settings_detail_value_ready(
        &snapshot, PRODUCT_UI_SETTING_CAP_VOLUME);
    if (s_sound_refresh.render_pending || result != s_sound_refresh.read_result ||
        can_volume != (s_sound_volume_control != NULL)) {
        s_volume_dragging = 0U;
        s_sound_volume_control = NULL;
        s_sound_volume_status = NULL;
        lv_obj_clean(content);
        s_sound_refresh.render_pending = product_ui_settings_sound_content_create(content) != 0;
    } else if (can_volume) {
        uint8_t can_adjust = product_ui_settings_volume_can_adjust(&snapshot);
        if (!can_adjust) {
            s_volume_dragging = 0U;
        }
        if (!s_volume_dragging && lv_slider_get_value(s_sound_volume_control) !=
            product_ui_settings_volume_display(&snapshot)) {
            lv_slider_set_value(s_sound_volume_control,
                product_ui_settings_volume_display(&snapshot), LV_ANIM_OFF);
            lv_obj_send_event(s_sound_volume_control, LV_EVENT_VALUE_CHANGED, NULL);
        }
        if (can_adjust) {
            lv_obj_remove_state(s_sound_volume_control, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(s_sound_volume_control, LV_STATE_DISABLED);
        }
        if (s_sound_volume_status != NULL) {
            lv_label_set_text_static(s_sound_volume_status,
                product_ui_settings_volume_status(&snapshot));
        }
    }
    s_sound_refresh.read_result = result;
    if (s_sound_refresh.render_pending) {
        s_sound_volume_control = NULL;
        s_sound_volume_status = NULL;
        lv_obj_clean(content);
    }
}


static void product_ui_settings_display_destroy(void)
{
    s_display_backlight_control = NULL;
    s_display_brightness_control = NULL;
    s_reduce_motion_control = NULL;
    s_appearance_status = NULL;
    s_brightness_dragging = 0U;
    memset(&s_display_refresh, 0, sizeof(s_display_refresh));
    product_ui_page_view_reset(&s_display_page_view);
}

static void product_ui_settings_sound_destroy(void)
{
    s_volume_dragging = 0U;
    s_sound_volume_control = NULL;
    s_sound_volume_status = NULL;
    memset(&s_sound_refresh, 0, sizeof(s_sound_refresh));
    product_ui_page_view_reset(&s_sound_page_view);
}


#define PRODUCT_UI_SETTINGS_DEFINE_PAGE(symbol, page_id, display_name, create_fn, update_fn, destroy_fn) \
    static const page_ops_t s_##symbol##_ops = { \
        .create = create_fn, \
        .destroy = destroy_fn, \
        .update = update_fn, \
        .flags = PAGE_FLAG_NO_GESTURE, \
    }; \
    static page_desc_t s_##symbol##_desc = { \
        .name = display_name, \
        .ops = &s_##symbol##_ops, \
    }; \
    static int symbol##_register_page(int page_type) \
    { \
        s_##symbol##_desc.page_type = page_type; \
        return page_manager_register(&s_##symbol##_desc); \
    } \
    D_UI_MODULE_DEFINE(page_id, display_name, LV_SYMBOL_SETTINGS, \
                    symbol##_register_page, NULL)

static const page_ops_t s_settings_display_ops = {
    .create = product_ui_settings_display_page_create,
    .destroy = product_ui_settings_display_destroy,
    .update = product_ui_settings_display_update,
    .flags = PAGE_FLAG_NO_GESTURE,
};

static page_desc_t s_settings_display_desc = {
    .name = "显示详情",
    .ops = &s_settings_display_ops,
};

static int settings_display_register_page(int page_type)
{
    s_settings_display_desc.page_type = page_type;
    return page_manager_register(&s_settings_display_desc);
}

D_UI_MODULE_DEFINE(product_settings_display, "显示详情", LV_SYMBOL_SETTINGS,
                settings_display_register_page, NULL);

PRODUCT_UI_SETTINGS_DEFINE_PAGE(settings_sound,
                                product_settings_sound,
                                "声音详情",
                                product_ui_settings_sound_page_create,
                                product_ui_settings_sound_update,
                                product_ui_settings_sound_destroy);

#endif /* D_LVGL_ENABLE && (D_UI_TARGET == D_UI_PRODUCT) */
