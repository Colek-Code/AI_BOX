/** @file product_ui_settings_device_pages.c @brief 显示、动效及声音详情页，手势取消与服务完成状态分开。 */

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
    product_ui_label_cache_t status_text;
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

/**
 * @brief 只同步亮度滑条与本地百分比文字，不提交硬件命令。
 * @param[in] brightness_percent 真实服务亮度，钳制为可见区间。
 */
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
    /* 仅更新值；业务回调绑定于松手阶段，不会递归提交亮度命令。 */
    lv_slider_set_value(s_display_brightness_control, value, LV_ANIM_OFF);
    lv_obj_send_event(s_display_brightness_control, LV_EVENT_VALUE_CHANGED,
                      NULL);
}

/**
 * @brief 提交背光安全调暗／恢复，成功同步亮度，失败恢复开关外观。
 * @param[in] event 背光开关 VALUE_CHANGED 事件。
 */
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
        /* 安全调暗关闭发布最低可见亮度，滑条同步真实值。 */
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

/**
 * @brief 跟踪亮度完整拖动，只在有效松手时提交，丢失或重置时恢复真实快照。
 * @param[in] event 亮度控件的按下、松手、丢失或输入重置事件。
 * @note RESET 使用事件对应输入处理真实松手，并发送 PRESS_LOST 收敛滑条；不把旧拖动在恢复后继续提交。
 */
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
    if (lv_event_get_code(event) == LV_EVENT_INDEV_RESET) {
        product_ui_control_wait_release(event);
        s_brightness_dragging = 0U;
        /* 重置会解除输入设备归属，之后不会再有 RELEASED 替滑条收尾。 */
        lv_obj_send_event(control, LV_EVENT_PRESS_LOST, NULL);
        lv_obj_add_flag(control, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
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
        /* 开关外观跟随快照，最低可见亮度显示为未选中。 */
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

/**
 * @brief 跟踪音量拖动，只在有效松手时受理目标，取消或失败恢复服务显示值。
 * @param[in] event 音量控件的按下、松手、丢失或输入重置事件。
 * @note 成功只表示请求受理；应用与超时状态由后续快照投影，RESET 不允许旧拖动复活。
 */
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
    if (lv_event_get_code(event) == LV_EVENT_INDEV_RESET) {
        product_ui_control_wait_release(event);
        s_volume_dragging = 0U;
        lv_obj_send_event(control, LV_EVENT_PRESS_LOST, NULL);
        lv_obj_add_flag(control, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
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

/**
 * @brief 创建开关行并在实际子开关上绑定值变化回调。
 * @param[in,out] content 父内容容器。
 * @param[in] toggle_spec 本次借用的文字、初值及持久回调规格。
 * @return 成功返回页面树拥有的开关子对象，参数、创建或结构失败返回 NULL。
 */
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

/**
 * @brief 创建设置滑条行，设定有效范围后绑定拖动与取消回调。
 * @param[in,out] content 滑条行父容器。
 * @param[in] title 本次创建使用的借用标题。
 * @param[in] value 初始百分比。
 * @param[in] callback 持久有效的按下、松手、丢失及重置处理器。
 * @param[in] min_value 有效区间下限；小于 max_value 时应用此范围。
 * @param[in] max_value 有效区间上限。
 * @param[out] out_control 可为空，预先清零，成功时返回借用的滑条对象。
 * @return 成功返回 0，行或子控件获取失败返回 -1，部分对象留给页面树清理。
 * @note VALUE_CHANGED 只更新百分比文字；业务提交在有效 RELEASED 后执行，避免拖动过程连续写 PWM／音量。
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
        /* 此时尚未注册业务回调，发送值变化只刷新文字，
         * 不会提交亮度或音量命令。 */
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

/**
 * @brief 按目标或确认外观同步开关、保存使能及可重试状态文字。
 * @param[in] snapshot 调用链保证非空的设置快照。
 */
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
        (void)product_ui_label_sync(s_appearance_status,
            product_ui_settings_appearance_status(snapshot), &s_display_refresh.status_text);
    }
}

/**
 * @brief 受理减少动态效果保存，并马上以真实快照纠正本地开关投影。
 * @param[in] event 减少动态效果开关的值变化事件。
 */
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

/**
 * @brief 按实际能力创建背光、亮度及动态效果控件或对应不可用反馈。
 * @param[in,out] content 已有显示详情内容容器。
 * @return 完整呈现或成功反馈返回 0，任一控件／反馈创建失败返回 -1。
 */
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
        /* 开关关闭只是最低可见亮度，滑条仍保持可操作。 */
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
    memset(&s_display_refresh.status_text, 0, sizeof(s_display_refresh.status_text));
    s_appearance_status = lv_label_create(content);
    if (s_reduce_motion_control == NULL || s_appearance_status == NULL) return -1;
    lv_obj_add_style(s_appearance_status, product_ui_style(PRODUCT_UI_STYLE_CAPTION), 0);
    product_ui_settings_reduce_motion_sync(&snapshot);
    return 0;
}


/**
 * @brief 建立显示详情框架，重置拖动及刷新状态后创建能力对应内容。
 */
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

/**
 * @brief 按真实音量能力创建滑条和状态文字，否则创建不可用反馈。
 * @param[in,out] content 已有声音详情内容容器。
 * @return 完整呈现或成功反馈返回 0，创建失败返回 -1。
 */
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
    memset(&s_sound_refresh.status_text, 0, sizeof(s_sound_refresh.status_text));
    s_sound_volume_status = lv_label_create(content);
    if (s_sound_volume_status == NULL) {
        return -1;
    }
    lv_obj_add_style(s_sound_volume_status, product_ui_style(PRODUCT_UI_STYLE_CAPTION), 0);
    (void)product_ui_label_sync(s_sound_volume_status, product_ui_settings_volume_status(&snapshot), &s_sound_refresh.status_text);
    if (!product_ui_settings_volume_can_adjust(&snapshot)) {
        lv_obj_add_state(s_sound_volume_control, LV_STATE_DISABLED);
    }
    return 0;
}


/**
 * @brief 建立声音详情框架，重置拖动及刷新状态后创建音量内容。
 */
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




/**
 * @brief 推进公共详情周期并用有限累积决定本轮是否刷新。
 * @param[in,out] refresh 对应页面独立刷新状态。
 * @param[in] elapsed_ms 本轮页面毫秒数。
 * @return 达到 200 毫秒刷新点返回 1，其余累计并返回 0。
 */
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

/**
 * @brief 能力变化时重建内容，稳定能力时同步未拖动的亮度及外观状态。
 * @param[in] elapsed_ms 本轮页面毫秒数。
 * @note 重建失败清除部分控件并保留 render_pending；拖动期间不以后台值覆盖用户当前预览。
 */
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

/**
 * @brief 按音量能力重建或同步滑条、使能及状态文字。
 * @param[in] elapsed_ms 本轮页面毫秒数。
 * @note 准入关闭会撤销拖动；内容失败保留重试，文字失败由共用缓存处理。
 */
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
            (void)product_ui_label_sync(s_sound_volume_status,
                product_ui_settings_volume_status(&snapshot), &s_sound_refresh.status_text);
        }
    }
    s_sound_refresh.read_result = result;
    if (s_sound_refresh.render_pending) {
        s_sound_volume_control = NULL;
        s_sound_volume_status = NULL;
        lv_obj_clean(content);
    }
}


/**
 * @brief 撤销显示控件引用、拖动及刷新缓存，并解除框架状态。
 * @note 页面管理器删除 LVGL 对象，外观服务在途事务不因页面销毁而取消。
 */
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

/**
 * @brief 撤销音量控件引用、拖动及刷新缓存，并解除框架状态。
 * @note 设备音量请求仍由其后台所有者完成。
 */
static void product_ui_settings_sound_destroy(void)
{
    s_volume_dragging = 0U;
    s_sound_volume_control = NULL;
    s_sound_volume_status = NULL;
    memset(&s_sound_refresh, 0, sizeof(s_sound_refresh));
    product_ui_page_view_reset(&s_sound_page_view);
}


/**
 * @brief 用静态页面描述、生命周期回调及模块入口装配一个设置详情页。
 * @param symbol 私有符号前缀，用于静态描述及生成的注册函数。
 * @param page_id 模块标识。
 * @param display_name 页面显示名称。
 * @param create_fn 创建回调，沿用页面管理器返回合同。
 * @param update_fn UI 周期回调。
 * @param destroy_fn 页面状态清理回调；对象删除仍由页面管理器拥有。
 * @note 生成的 symbol_register_page 接收页面类型并原样返回注册结果。
 */
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

/**
 * @brief 注册静态显示详情页面描述。
 * @param[in] page_type 装配分配的页面类型。
 * @return 原样返回页面管理器注册结果。
 */
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
