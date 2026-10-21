/** @file product_ui_pomodoro_view.c @brief LVGL objects only; structural replacements preserve a usable old view. */
#include "config/app_config.h"
#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)
#include "product_ui_pomodoro_view.h"
#include "platform/screen/transition/d_page_transition.h"
#include "modules/applications/product_ui/debug/product_ui_trace.h"
#include <string.h>

typedef struct {
    lv_obj_t *root;
    lv_obj_t *phase;
    lv_obj_t *time;
    lv_obj_t *status;
    lv_obj_t *primary;
    lv_obj_t *primary_label;
    lv_obj_t *values[7];
    lv_obj_t *sliders[3];
    product_ui_pomo_mode_e mode;
} product_ui_pomodoro_render_t;
static product_ui_pomodoro_render_t s_view;
static product_ui_pomo_action_fn s_action;
static uint32_t s_generation;
static uint32_t s_pressed_generation;
static lv_obj_t *s_pressed;
static uint8_t s_armed;
static uint8_t s_repeated;
static lv_obj_t *s_slider;
static uint8_t s_slider_original;
static uint8_t s_slider_field;

static void product_ui_pomodoro_duration_text(uint8_t field, uint8_t value)
{
    char text[8];
    lv_snprintf(text, sizeof(text), "%u", value);
    if (s_view.values[field] && strcmp(lv_label_get_text(s_view.values[field]), text) != 0)
        lv_label_set_text(s_view.values[field], text);
}

static void product_ui_pomodoro_cancel_input(void)
{
    s_armed = 0U;
    if (s_slider != NULL) {
        lv_slider_set_value(s_slider, s_slider_original, LV_ANIM_OFF);
        product_ui_pomodoro_duration_text(s_slider_field, s_slider_original);
        s_slider = NULL;
    }
}

static void product_ui_pomodoro_slider_input(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t *target = lv_event_get_current_target(event);
    uint8_t field = (uint8_t)(uintptr_t)lv_event_get_user_data(event);
    if (code == LV_EVENT_PRESSED) {
        product_ui_pomodoro_cancel_input();
        s_slider = target; s_slider_field = field;
        s_slider_original = (uint8_t)lv_slider_get_value(target);
        s_pressed = target; s_pressed_generation = s_generation; s_armed = 1U;
    } else if (code == LV_EVENT_VALUE_CHANGED) {
        if (s_armed && s_slider == target)
            product_ui_pomodoro_duration_text(field, (uint8_t)lv_slider_get_value(target));
    } else if (code == LV_EVENT_RELEASED) {
        if (s_armed && s_slider == target && s_pressed_generation == s_generation && s_action != NULL)
            s_action(PRODUCT_UI_POMO_SET_DURATION, field, (uint8_t)lv_slider_get_value(target), s_generation);
        /* Only the controller owns the draft; refresh binds the accepted release. */
        product_ui_pomodoro_cancel_input();
    } else if (code == LV_EVENT_INDEV_RESET) {
        if (s_pressed == target) {
            /* LVGL's slider clears its drag/scroll-chain state on PRESS_LOST, not INDEV_RESET. */
            s_armed = 0U;
            lv_obj_send_event(target, LV_EVENT_PRESS_LOST, NULL);
            /* Reset may run outside indev dispatch, so LVGL cannot infer the pointer's scroll axis. */
            lv_obj_add_flag(target, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
        }
    } else if (code == LV_EVENT_PRESS_LOST || code == LV_EVENT_DEFOCUSED) {
        if (s_pressed == target) product_ui_pomodoro_cancel_input();
    }
}

const char *product_ui_pomodoro_phase_name(product_ui_pomodoro_phase_e phase)
{
    return phase == PRODUCT_UI_POMODORO_FOCUS ? "专注" :
        phase == PRODUCT_UI_POMODORO_SHORT_BREAK ? "短休息" : "长休息";
}

static void product_ui_pomodoro_input(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t *target = lv_event_get_current_target(event);
    uintptr_t identity = (uintptr_t)lv_event_get_user_data(event);
    product_ui_pomo_action_e action = (product_ui_pomo_action_e)(identity & 255U);
    uint8_t valid;
    if (code == LV_EVENT_PRESSED || code == LV_EVENT_CLICKED || code == LV_EVENT_PRESS_LOST ||
        code == LV_EVENT_DEFOCUSED || code == LV_EVENT_INDEV_RESET) {
        PRODUCT_UI_TRACE("pomodoro input=%u action=%u armed=%u same=%u gen=%u/%u\n",
            (unsigned)code, (unsigned)identity, s_armed, s_pressed == target,
            (unsigned)s_pressed_generation, (unsigned)s_generation);
    }
    if (code == LV_EVENT_PRESSED) {
        product_ui_pomodoro_cancel_input();
        s_pressed = target; s_pressed_generation = s_generation; s_armed = 1U; s_repeated = 0U;
    } else if (code == LV_EVENT_PRESS_LOST || code == LV_EVENT_INDEV_RESET ||
               code == LV_EVENT_DEFOCUSED || code == LV_EVENT_SCROLL_BEGIN) {
        if (s_pressed == target) s_armed = 0U;
    } else if (code == LV_EVENT_LONG_PRESSED || code == LV_EVENT_LONG_PRESSED_REPEAT) {
        valid = s_armed && s_pressed == target && s_pressed_generation == s_generation &&
            !lv_obj_has_state(target, LV_STATE_DISABLED);
        if (valid && s_action != NULL &&
            (action == PRODUCT_UI_POMO_DECREASE || action == PRODUCT_UI_POMO_INCREASE)) {
            s_repeated = 1U;
            s_action(action, (uint8_t)(identity >> 8), 0U, s_generation);
        }
    } else if (code == LV_EVENT_CLICKED) {
        valid = s_armed && !s_repeated && s_pressed == target && s_pressed_generation == s_generation &&
            !lv_obj_has_state(target, LV_STATE_DISABLED);
        s_armed = 0U;
        if (valid && s_action != NULL) s_action(action, (uint8_t)(identity >> 8), 0U, s_generation);
    }
}

static void product_ui_pomodoro_clean(lv_obj_t *object)
{
    lv_obj_remove_style_all(object);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE |
        LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_ON_FOCUS);
}

static lv_obj_t *product_ui_pomodoro_label(lv_obj_t *parent, const char *text, product_ui_style_e style)
{
    lv_obj_t *label = lv_label_create(parent);
    if (label == NULL) return NULL;
    lv_obj_add_style(label, product_ui_style(style), 0);
    lv_label_set_text(label, text);
    return label;
}

static lv_obj_t *product_ui_pomodoro_button(lv_obj_t *parent, const char *text, uintptr_t identity)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_t *label;
    if (button == NULL) return NULL;
    product_ui_pomodoro_clean(button);
    lv_obj_set_size(button, 80, PRODUCT_UI_TOUCH_MIN);
    lv_obj_add_style(button, product_ui_style(PRODUCT_UI_STYLE_INTERACTIVE), 0);
    lv_obj_add_style(button, product_ui_style(PRODUCT_UI_STYLE_PRESSED), LV_STATE_PRESSED);
    lv_obj_add_style(button, product_ui_style(PRODUCT_UI_STYLE_DISABLED), LV_STATE_DISABLED);
    lv_obj_add_event_cb(button, product_ui_pomodoro_input, LV_EVENT_ALL, (void *)identity);
    label = product_ui_pomodoro_label(button, text, PRODUCT_UI_STYLE_BODY);
    if (label == NULL) { lv_obj_delete(button); return NULL; }
    lv_obj_center(label);
    return button;
}

lv_obj_t *product_ui_pomodoro_view_nav_button(lv_obj_t *parent, const char *text,
                                             product_ui_pomo_action_e action)
{
    lv_obj_t *button = product_ui_pomodoro_button(parent, text, action);
    if (button != NULL) lv_obj_set_width(button, 48);
    return button;
}

static void product_ui_pomodoro_scroll(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_SCROLL_BEGIN) product_ui_pomodoro_cancel_input();
    d_screen_request_composed_frame();
}

static int product_ui_pomodoro_timer_build(product_ui_pomodoro_render_t *view)
{
    lv_obj_t *skip;
    lv_obj_t *end;
    view->phase = product_ui_pomodoro_label(view->root, "", PRODUCT_UI_STYLE_BODY);
    view->time = product_ui_pomodoro_label(view->root, "25:00", PRODUCT_UI_STYLE_TITLE);
    view->status = product_ui_pomodoro_label(view->root, "", PRODUCT_UI_STYLE_CAPTION);
    view->primary = product_ui_pomodoro_button(view->root, "开始", PRODUCT_UI_POMO_PRIMARY);
    skip = product_ui_pomodoro_button(view->root, "跳过", PRODUCT_UI_POMO_SKIP);
    end = product_ui_pomodoro_button(view->root, "结束本轮", PRODUCT_UI_POMO_END);
    if (!view->phase || !view->time || !view->status || !view->primary || !skip || !end) return -1;
    view->primary_label = lv_obj_get_child(view->primary, 0);
    lv_obj_align(view->phase, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_text_font(view->time, product_ui_font(PRODUCT_UI_FONT_DISPLAY_NUMBER), 0);
    lv_obj_align(view->time, LV_ALIGN_TOP_MID, 0, 23);
    lv_obj_set_width(view->status, 296);
    lv_obj_set_style_text_align(view->status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(view->status, LV_ALIGN_TOP_MID, 0, 62);
    lv_obj_set_size(view->primary, 296, 36);
    lv_obj_set_pos(view->primary, 0, 82);
    lv_obj_set_style_bg_color(view->primary, product_ui_color(PRODUCT_UI_COLOR_ACCENT), 0);
    lv_obj_set_style_text_color(view->primary_label, product_ui_color(PRODUCT_UI_COLOR_ON_PRIMARY), 0);
    lv_obj_set_size(skip, 144, 36); lv_obj_set_pos(skip, 0, 122);
    lv_obj_set_size(end, 144, 36); lv_obj_set_pos(end, 152, 122);
    return 0;
}

static lv_obj_t *product_ui_pomodoro_duration_slider(lv_obj_t *parent, uint8_t field, int32_t y)
{
    static const uint8_t maxima[] = {120, 30, 60};
    lv_obj_t *slider = lv_slider_create(parent);
    if (slider == NULL) return NULL;
    product_ui_pomodoro_clean(slider);
    lv_obj_add_flag(slider, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_set_size(slider, 184, 8);
    lv_obj_set_pos(slider, 56, y + 40);
    lv_obj_set_style_bg_color(slider, product_ui_color(PRODUCT_UI_COLOR_SURFACE_ALT), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, product_ui_color(PRODUCT_UI_COLOR_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, product_ui_color(PRODUCT_UI_COLOR_ACCENT), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_border_color(slider, product_ui_color(PRODUCT_UI_COLOR_SURFACE), LV_PART_KNOB);
    lv_obj_set_style_border_width(slider, 2, LV_PART_KNOB);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 6, LV_PART_KNOB);
    lv_obj_set_ext_click_area(slider, PRODUCT_UI_SLIDER_TOUCH_EXTEND);
    lv_slider_set_range(slider, 1, maxima[field]);
    lv_obj_add_event_cb(slider, product_ui_pomodoro_slider_input, LV_EVENT_ALL, (void *)(uintptr_t)field);
    return slider;
}

static int product_ui_pomodoro_settings_build(product_ui_pomodoro_render_t *view)
{
    static const char *titles[] = {"专注分钟", "短休息分钟", "长休息分钟", "长休息间隔",
        "自动开始专注", "自动开始休息", "完成提示音"};
    unsigned index;
    lv_obj_t *save;
    lv_obj_t *retry;
    for (index = 0U; index < 7U; index++) {
        int32_t y = index < 3U ? (int32_t)index * 72 : 216 + (int32_t)(index - 3U) * 44;
        lv_obj_t *title = product_ui_pomodoro_label(view->root, titles[index], PRODUCT_UI_STYLE_BODY);
        lv_obj_t *control;
        if (title == NULL) return -1;
        lv_obj_set_pos(title, 0, y + (index < 3U ? 4 : 10));
        if (index < 4U) {
            control = product_ui_pomodoro_button(view->root, "-", PRODUCT_UI_POMO_DECREASE | (index << 8));
            if (!control) return -1;
            lv_obj_set_size(control, 36, 36);
            lv_obj_set_pos(control, index < 3U ? 0 : 158, y + (index < 3U ? 26 : 0));
            view->values[index] = product_ui_pomodoro_label(view->root, "", PRODUCT_UI_STYLE_BODY);
            if (!view->values[index]) return -1;
            lv_obj_set_pos(view->values[index], index < 3U ? 242 : 197, y + (index < 3U ? 4 : 10));
            lv_obj_set_width(view->values[index], 54);
            lv_obj_set_style_text_align(view->values[index], index < 3U ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_CENTER, 0);
            control = product_ui_pomodoro_button(view->root, "+", PRODUCT_UI_POMO_INCREASE | (index << 8));
            if (!control) return -1;
            lv_obj_set_size(control, 36, 36); lv_obj_set_pos(control, 260, y + (index < 3U ? 26 : 0));
            if (index < 3U) {
                view->sliders[index] = product_ui_pomodoro_duration_slider(view->root, (uint8_t)index, y);
                if (!view->sliders[index]) return -1;
            }
        } else {
            control = product_ui_pomodoro_button(view->root, "关闭", PRODUCT_UI_POMO_TOGGLE | (index << 8));
            if (!control) return -1;
            lv_obj_set_pos(control, 216, y);
            view->values[index] = lv_obj_get_child(control, 0);
        }
    }
    save = product_ui_pomodoro_button(view->root, "保存设置", PRODUCT_UI_POMO_SAVE);
    retry = product_ui_pomodoro_button(view->root, "重试保存", PRODUCT_UI_POMO_RETRY);
    view->status = product_ui_pomodoro_label(view->root, "返回不保存修改", PRODUCT_UI_STYLE_CAPTION);
    if (!save || !retry || !view->status) return -1;
    lv_obj_set_size(save, 144, 36); lv_obj_set_pos(save, 0, 420);
    lv_obj_set_size(retry, 144, 36); lv_obj_set_pos(retry, 152, 420);
    lv_obj_set_pos(view->status, 0, 392);
    return 0;
}

static int product_ui_pomodoro_stats_build(product_ui_pomodoro_render_t *view)
{
    unsigned index;
    lv_obj_t *clear;
    view->phase = product_ui_pomodoro_label(view->root, "近7天", PRODUCT_UI_STYLE_BODY);
    if (!view->phase) return -1;
    for (index = 0U; index < 7U; index++) {
        view->values[index] = product_ui_pomodoro_label(view->root, "", PRODUCT_UI_STYLE_BODY);
        if (!view->values[index]) return -1;
        lv_obj_set_pos(view->values[index], 0, 32 + (int32_t)index * 36);
    }
    view->status = product_ui_pomodoro_label(view->root, "", PRODUCT_UI_STYLE_CAPTION);
    clear = product_ui_pomodoro_button(view->root, "清空统计", PRODUCT_UI_POMO_CLEAR);
    if (!view->status || !clear) return -1;
    lv_obj_set_pos(view->status, 0, 287);
    lv_obj_set_width(view->status, 296);
    lv_obj_set_size(clear, 296, 36); lv_obj_set_pos(clear, 0, 330);
    return 0;
}

static void product_ui_pomodoro_deleted(lv_event_t *event)
{
    if (lv_event_get_current_target(event) == s_view.root) {
        s_slider = NULL;
        memset(&s_view, 0, sizeof(s_view)); s_pressed = NULL; s_armed = 0U;
    }
}

int product_ui_pomodoro_view_show(lv_obj_t *parent, product_ui_pomo_mode_e mode,
    const product_ui_pomodoro_snapshot_t *snapshot, product_ui_pomo_action_fn action)
{
    product_ui_pomodoro_render_t candidate = {0};
    lv_obj_t *old;
    int result;
    if (!parent || !snapshot || !action || mode > PRODUCT_UI_POMO_STATS) return -1;
    candidate.mode = mode;
    candidate.root = lv_obj_create(parent);
    if (!candidate.root) return -1;
    product_ui_pomodoro_clean(candidate.root);
    lv_obj_set_size(candidate.root, 296, 158);
    if (mode != PRODUCT_UI_POMO_TIMER) {
        lv_obj_add_flag(candidate.root, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(candidate.root, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(candidate.root, LV_SCROLLBAR_MODE_AUTO);
        lv_obj_add_event_cb(candidate.root, product_ui_pomodoro_scroll, LV_EVENT_SCROLL_BEGIN, NULL);
        lv_obj_add_event_cb(candidate.root, product_ui_pomodoro_scroll, LV_EVENT_SCROLL, NULL);
        lv_obj_add_event_cb(candidate.root, product_ui_pomodoro_scroll, LV_EVENT_SCROLL_END, NULL);
    }
    result = mode == PRODUCT_UI_POMO_TIMER ? product_ui_pomodoro_timer_build(&candidate) :
        mode == PRODUCT_UI_POMO_SETTINGS ? product_ui_pomodoro_settings_build(&candidate) :
        product_ui_pomodoro_stats_build(&candidate);
    if (result != 0) { lv_obj_delete(candidate.root); return -1; }
    product_ui_pomodoro_cancel_input(); s_pressed = NULL;
    old = s_view.root;
    s_view = candidate;
    s_action = action;
    s_generation = snapshot->model.generation;
    lv_obj_add_event_cb(candidate.root, product_ui_pomodoro_deleted, LV_EVENT_DELETE, NULL);
    if (old) lv_obj_delete(old);
    product_ui_pomodoro_view_refresh(snapshot, &snapshot->model.saved.settings);
    d_screen_request_composed_frame();
    return 0;
}

static void product_ui_pomodoro_text(lv_obj_t *label, const char *text)
{
    if (label && strcmp(lv_label_get_text(label), text) != 0) lv_label_set_text(label, text);
}

static const char *product_ui_pomodoro_save_status(const product_ui_pomodoro_snapshot_t *snapshot)
{
    if (!snapshot->storage_ready) return "保存不可用，本次修改未保存";
    if (snapshot->load_error == D_ERR_IO || snapshot->load_error == D_ERR_NOT_READY)
        return snapshot->saving ? "正在读取原记录" : "原记录读取失败，本次未保存，请点重试保存";
    if (snapshot->saving) return "正在保存";
    if (snapshot->persisted_revision != snapshot->model.saved_revision)
        return snapshot->storage_error != D_OK ? "已应用，保存失败，可重试" : "等待保存";
    if (snapshot->load_error == D_ERR_INVALID_ARG) return "原记录损坏，已用默认值";
    return snapshot->has_record ? "已保存" : "默认设置，返回不保存修改";
}

static uint32_t product_ui_pomodoro_previous_date(uint32_t date)
{
    uint32_t month;
    uint32_t year;
    if (date % 100U > 1U) return date - 1U;
    month = (date / 100U) % 100U;
    year = date / 10000U;
    if (--month == 0U) { month = 12U; year--; }
    date = year * 10000U + month * 100U + 31U;
    while (date % 100U > 0U && !product_ui_pomodoro_date_valid(date)) date--;
    return date;
}

static void product_ui_pomodoro_stats_refresh(const product_ui_pomodoro_snapshot_t *snapshot)
{
    uint32_t date = snapshot->today;
    uint32_t ceiling = UINT32_MAX;
    unsigned row, index;
    char text[128];
    product_ui_pomodoro_text(s_view.phase, date ? "近7天 · 完整专注" : "最近记录 · 未校时");
    for (row = 0U; row < 7U; row++) {
        const product_ui_pomodoro_day_t *day = NULL;
        if (!snapshot->today) {
            date = 0U;
            for (index = 0U; index < 7U; index++) {
                uint32_t candidate = snapshot->model.saved.days[index].date;
                if (candidate < ceiling && candidate > date) date = candidate;
            }
            ceiling = date;
        }
        for (index = 0U; index < 7U; index++)
            if (date && snapshot->model.saved.days[index].date == date) day = &snapshot->model.saved.days[index];
        if (date) lv_snprintf(text, sizeof(text), "%04u-%02u-%02u   %u次  %u分", (unsigned)(date / 10000U),
            (unsigned)((date / 100U) % 100U), (unsigned)(date % 100U),
            (unsigned)(day ? day->count : 0U), (unsigned)(day ? day->minutes : 0U));
        else lv_snprintf(text, sizeof(text), "暂无记录");
        product_ui_pomodoro_text(s_view.values[row], text);
        if (snapshot->today) date = product_ui_pomodoro_previous_date(date);
    }
    lv_snprintf(text, sizeof(text), "未归档：%u次  %u分钟\n%s", (unsigned)snapshot->model.saved.undated_count,
        (unsigned)snapshot->model.saved.undated_minutes,
        snapshot->load_error == D_ERR_NOT_FOUND && !snapshot->has_record && snapshot->storage_ready &&
        snapshot->persisted_revision == snapshot->model.saved_revision ? "暂无已保存记录" :
        product_ui_pomodoro_save_status(snapshot));
    product_ui_pomodoro_text(s_view.status, text);
}

void product_ui_pomodoro_view_refresh(const product_ui_pomodoro_snapshot_t *snapshot,
    const product_ui_pomodoro_settings_t *draft)
{
    char text[128];
    unsigned index;
    if (!s_view.root || !snapshot || !draft) return;
    if (s_generation != snapshot->model.generation) product_ui_pomodoro_cancel_input();
    s_generation = snapshot->model.generation;
    if (s_view.mode == PRODUCT_UI_POMO_SETTINGS) {
        uint8_t values[] = {draft->focus_minutes, draft->short_minutes, draft->long_minutes,
            draft->interval, draft->auto_focus, draft->auto_break, draft->sound};
        for (index = 0U; index < 7U; index++) {
            if (index < 3U) {
                if (s_slider == s_view.sliders[index]) continue;
                lv_slider_set_value(s_view.sliders[index], values[index], LV_ANIM_OFF);
            }
            if (index < 4U) lv_snprintf(text, sizeof(text), "%u", values[index]);
            else lv_snprintf(text, sizeof(text), "%s", values[index] ? "开启" : "关闭");
            product_ui_pomodoro_text(s_view.values[index], text);
        }
        product_ui_pomodoro_text(s_view.status,
            memcmp(draft, &snapshot->model.saved.settings, sizeof(*draft)) != 0 ?
            "修改未保存，返回将放弃" : product_ui_pomodoro_save_status(snapshot));
    } else if (s_view.mode == PRODUCT_UI_POMO_STATS) {
        product_ui_pomodoro_stats_refresh(snapshot);
    } else {
        uint32_t seconds = (snapshot->model.remaining_ms + 999U) / 1000U;
        lv_snprintf(text, sizeof(text), "%s · 本轮完成 %u/%u", product_ui_pomodoro_phase_name(snapshot->model.phase),
            snapshot->model.completed_in_cycle, snapshot->model.cycle_interval);
        product_ui_pomodoro_text(s_view.phase, text);
        lv_snprintf(text, sizeof(text), "%02u:%02u", (unsigned)(seconds / 60U), (unsigned)(seconds % 60U));
        product_ui_pomodoro_text(s_view.time, text);
        product_ui_pomodoro_text(s_view.status, !snapshot->timer_ready ? "计时服务不可用" :
             snapshot->quiescing ? "正在关机" :
             !snapshot->storage_ready ? "保存不可用，记录未保存" :
             snapshot->load_error == D_ERR_IO || snapshot->load_error == D_ERR_NOT_READY ?
                 "记录读取失败，请在设置重试" :
            snapshot->persisted_revision != snapshot->model.saved_revision && snapshot->storage_error != D_OK ?
                "统计未保存，请在设置重试" :
            snapshot->audio_error != D_OK ? "声音不可用，完成后显示提醒" :
            !snapshot->today ? "未校时 · 完成记录暂不归档" :
            snapshot->model.state == PRODUCT_UI_POMODORO_RUNNING ? "专注当下，离开页面仍会计时" :
            snapshot->model.state == PRODUCT_UI_POMODORO_PAUSED ? "已暂停" : "准备好后开始");
        product_ui_pomodoro_text(s_view.primary_label, snapshot->model.state == PRODUCT_UI_POMODORO_RUNNING ? "暂停" :
            snapshot->model.state == PRODUCT_UI_POMODORO_PAUSED ? "继续" : "开始");
        if (!snapshot->timer_ready || snapshot->quiescing) lv_obj_add_state(s_view.primary, LV_STATE_DISABLED);
        else lv_obj_remove_state(s_view.primary, LV_STATE_DISABLED);
    }
}

void product_ui_pomodoro_view_destroy(void)
{
    product_ui_pomodoro_cancel_input();
    if (s_view.root) lv_obj_delete(s_view.root);
    memset(&s_view, 0, sizeof(s_view)); s_action = NULL; s_armed = 0U; s_pressed = NULL;
}
#endif
