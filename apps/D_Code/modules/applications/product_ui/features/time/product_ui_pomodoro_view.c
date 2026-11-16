/** @file product_ui_pomodoro_view.c @brief 三个有限番茄钟视图及稳定动作身份，结构替换失败保留旧视图。 */
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
/* 显示缓存不放入 UI 栈上的事务候选视图，避免扩大候选对象。 */
static struct {
    product_ui_label_cache_t phase, time, status, primary, values[7];
} s_text;
static product_ui_pomo_action_fn s_action;
static uint32_t s_generation;
static uint32_t s_pressed_generation;
static lv_obj_t *s_pressed;
static uint8_t s_armed;
static uint8_t s_repeated;
static lv_obj_t *s_slider;
static uint8_t s_slider_original;
static uint8_t s_slider_field;

/**
 * @brief 把时长预览值更新到对应字段标签及其缓存。
 * @param[in] field 调用链保证在时长字段范围内的下标。
 * @param[in] value 待显示分钟数。
 */
static void product_ui_pomodoro_duration_text(uint8_t field, uint8_t value)
{
    char text[8];
    lv_snprintf(text, sizeof(text), "%u", value);
    (void)product_ui_label_sync(s_view.values[field], text, &s_text.values[field]);
}

/**
 * @brief 撤销动作资格，并把活跃滑条及数值文字恢复到按下原值。
 */
static void product_ui_pomodoro_cancel_input(void)
{
    s_armed = 0U;
    if (s_slider != NULL) {
        lv_slider_set_value(s_slider, s_slider_original, LV_ANIM_OFF);
        product_ui_pomodoro_duration_text(s_slider_field, s_slider_original);
        s_slider = NULL;
    }
}

/**
 * @brief 滑动只预览文字，同对象同代次的完整松手才提交时长动作。
 * @param[in] event 滑条事件，用户数据编码时长字段。
 * @note 草稿由控制器拥有；松手后先撤销预览，下轮刷新绑定控制器受理值。重置显式发送 PRESS_LOST 收敛 LVGL 拖动。
 */
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
        /* 草稿只由控制器拥有，后续刷新绑定被受理的松手值。 */
        product_ui_pomodoro_cancel_input();
    } else if (code == LV_EVENT_INDEV_RESET) {
        if (s_pressed == target) {
            /* LVGL 滑条在 PRESS_LOST 才清除拖动及滚动链状态，INDEV_RESET 本身不会收尾。 */
            s_armed = 0U;
            lv_obj_send_event(target, LV_EVENT_PRESS_LOST, NULL);
            /* 重置可能在输入分发之外执行，LVGL 无法据此推断指针滚动轴，须恢复纵向滚动链。 */
            lv_obj_add_flag(target, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
        }
    } else if (code == LV_EVENT_PRESS_LOST || code == LV_EVENT_DEFOCUSED) {
        if (s_pressed == target) product_ui_pomodoro_cancel_input();
    }
}

/**
 * @brief 把阶段映射成静态名称。
 */
const char *product_ui_pomodoro_phase_name(product_ui_pomodoro_phase_e phase)
{
    return phase == PRODUCT_UI_POMODORO_FOCUS ? "专注" :
        phase == PRODUCT_UI_POMODORO_SHORT_BREAK ? "短休息" : "长休息";
}

/**
 * @brief 校验按钮对象、模型代次及使能，区分长按重复增减与单次点击。
 * @param[in] event 按钮事件，用户数据低字节为动作，高字节为字段。
 */
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

/**
 * @brief 清除默认样式及滚动、手势和按压锁定标志，便于显式配置。
 * @param[in,out] object 调用链保证非空的 LVGL 对象。
 */
static void product_ui_pomodoro_clean(lv_obj_t *object)
{
    lv_obj_remove_style_all(object);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE |
        LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_ON_FOCUS);
}

/**
 * @brief 创建主题标签并复制本次文字。
 * @param[in,out] parent 标签父对象。
 * @param[in] text 本次复制的借用文字。
 * @param[in] style 标签主题样式。
 * @return 成功返回父对象拥有的标签，失败返回 NULL。
 */
static lv_obj_t *product_ui_pomodoro_label(lv_obj_t *parent, const char *text, product_ui_style_e style)
{
    lv_obj_t *label = lv_label_create(parent);
    if (label == NULL) return NULL;
    lv_obj_add_style(label, product_ui_style(style), 0);
    lv_label_set_text(label, text);
    return label;
}

/**
 * @brief 创建带稳定动作身份及共用输入校验的主题按钮。
 * @param[in,out] parent 按钮父对象。
 * @param[in] text 本次复制的按钮文字。
 * @param[in] identity 低字节动作、高字节字段的整数身份。
 * @return 成功返回父对象拥有的按钮；创建失败返回 NULL，标签失败时删除局部按钮。
 */
static lv_obj_t *product_ui_pomodoro_button(lv_obj_t *parent, const char *text, uintptr_t identity)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_t *label;
    product_ui_pomo_action_e action = (product_ui_pomo_action_e)(identity & 255U);
    if (button == NULL) return NULL;
    product_ui_pomodoro_clean(button);
    lv_obj_add_event_cb(button, product_ui_pomodoro_input, LV_EVENT_ALL, (void *)identity);
    label = product_ui_pomodoro_label(button, text, PRODUCT_UI_STYLE_BODY);
    if (label == NULL) { lv_obj_delete(button); return NULL; }
    product_ui_button_style(button,
        action == PRODUCT_UI_POMO_PRIMARY || action == PRODUCT_UI_POMO_SAVE ? PRODUCT_UI_BUTTON_PRIMARY :
        action == PRODUCT_UI_POMO_END || action == PRODUCT_UI_POMO_CLEAR ? PRODUCT_UI_BUTTON_DANGER :
        PRODUCT_UI_BUTTON_SECONDARY);
    lv_obj_set_size(button, 80, PRODUCT_UI_TOUCH_MIN);
    lv_obj_center(label);
    return button;
}

/**
 * @brief 创建共用按压校验的窄导航按钮。
 */
lv_obj_t *product_ui_pomodoro_view_nav_button(lv_obj_t *parent, const char *text,
                                             product_ui_pomo_action_e action)
{
    lv_obj_t *button = product_ui_pomodoro_button(parent, text, action);
    if (button != NULL) lv_obj_set_width(button, 48);
    return button;
}

/**
 * @brief 滚动开始时取消旧手势，并为滚动阶段请求组合帧。
 * @param[in] event 视图根的滚动事件。
 */
static void product_ui_pomodoro_scroll(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_SCROLL_BEGIN) product_ui_pomodoro_cancel_input();
    d_screen_request_composed_frame();
}

/**
 * @brief 在候选根创建计时阶段、倒计时、状态及三项计时操作。
 * @param[in,out] view 持有候选根并接收子对象引用的视图。
 * @return 完整创建返回 0，任一对象失败返回 -1。
 */
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
    lv_obj_set_size(skip, 144, 36); lv_obj_set_pos(skip, 0, 122);
    lv_obj_set_size(end, 144, 36); lv_obj_set_pos(end, 152, 122);
    return 0;
}

/**
 * @brief 创建指定时长字段的范围滑条及释放提交回调。
 * @param[in,out] parent 滑条父容器。
 * @param[in] field 调用方保证为 0 至 2 的时长字段。
 * @param[in] y 该字段布局的纵向起点。
 * @return 成功返回父树拥有的滑条，失败返回 NULL。
 */
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

/**
 * @brief 在候选根创建七项设置控件、时长滑条及保存入口。
 * @param[in,out] view 候选视图，接收标签及滑条引用。
 * @return 完整创建返回 0，任一对象失败返回 -1。
 */
static int product_ui_pomodoro_settings_build(product_ui_pomodoro_render_t *view)
{
    static const char *titles[] = {"专注分钟", "短休息分钟", "长休息分钟", "长休息间隔",
        "自动开始专注", "自动开始休息", "完成提示音"};
    unsigned index;
    lv_obj_t *save;
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
    view->status = product_ui_pomodoro_label(view->root, "修改后请保存设置", PRODUCT_UI_STYLE_CAPTION);
    if (!save || !view->status) return -1;
    lv_obj_set_size(save, 296, 36); lv_obj_set_pos(save, 0, 420);
    lv_obj_set_pos(view->status, 0, 392);
    return 0;
}

/**
 * @brief 在候选根创建七行完成统计、未归日说明及清空入口。
 * @param[in,out] view 候选视图，接收统计控件引用。
 * @return 完整创建返回 0，任一对象失败返回 -1。
 */
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

/**
 * @brief 仅当前视图根删除时清除当前控件、手势及显示缓存。
 * @param[in] event 视图根删除事件。
 * @note 替换后的旧根删除不会清除已经提交的新候选视图。
 */
static void product_ui_pomodoro_deleted(lv_event_t *event)
{
    if (lv_event_get_current_target(event) == s_view.root) {
        s_slider = NULL;
        memset(&s_text, 0, sizeof(s_text));
        memset(&s_view, 0, sizeof(s_view)); s_pressed = NULL; s_armed = 0U;
    }
}

/**
 * @brief 在候选树完整创建后才替换当前子视图，失败保留原视图。
 */
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
    memset(&s_text, 0, sizeof(s_text));
    s_action = action;
    s_generation = snapshot->model.generation;
    lv_obj_add_event_cb(candidate.root, product_ui_pomodoro_deleted, LV_EVENT_DELETE, NULL);
    if (old) lv_obj_delete(old);
    product_ui_pomodoro_view_refresh(snapshot, &snapshot->model.saved.settings);
    d_screen_request_composed_frame();
    return 0;
}

/**
 * @brief 使用共用可重试文字缓存更新标签。
 * @param[in,out] label 借用的目标标签。
 * @param[in] text 本次目标文字。
 * @param[in,out] cache 与该标签绑定的持久缓存。
 */
static void product_ui_pomodoro_text(lv_obj_t *label, const char *text,
                                    product_ui_label_cache_t *cache)
{
    (void)product_ui_label_sync(label, text, cache);
}

/**
 * @brief 区分原记录读取、在途保存、失败保留及确认记录，选择状态文字。
 * @param[in] snapshot 调用链保证非空的服务快照。
 * @return 静态状态文字的借用指针。
 */
static const char *product_ui_pomodoro_save_status(const product_ui_pomodoro_snapshot_t *snapshot)
{
    if (!snapshot->storage_ready) return "保存不可用，本次修改未保存";
    if (snapshot->load_error == D_ERR_IO || snapshot->load_error == D_ERR_NOT_READY)
        return snapshot->saving ? "正在读取原记录" : "原记录读取失败，请点保存设置重试";
    if (snapshot->saving) return "正在保存";
    if (snapshot->persisted_revision != snapshot->model.saved_revision)
        return snapshot->storage_error != D_OK ? "已应用，保存失败，可重试" : "等待保存";
    if (snapshot->load_error == D_ERR_INVALID_ARG) return "原记录损坏，已用默认值";
    return snapshot->has_record ? "已保存" : "默认设置，修改后请保存";
}

/**
 * @brief 为受支持的近期统计显示求前一公历日期。
 * @param[in] date 调用链保证有效且前一天仍在日期支持区间内的日期。
 * @return 前一天的 YYYYMMDD 日期键。
 */
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

/**
 * @brief 有可信今天时显示近七天，否则按记录日期倒序展示最近记录。
 * @param[in] snapshot 借用的服务快照及完成统计。
 * @note 未归日统计及保存状态单独显示，不把无可信日期的完成记录归到伪造今天。
 */
static void product_ui_pomodoro_stats_refresh(const product_ui_pomodoro_snapshot_t *snapshot)
{
    uint32_t date = snapshot->today;
    uint32_t ceiling = UINT32_MAX;
    unsigned row, index;
    char text[128];
    product_ui_pomodoro_text(s_view.phase, date ? "近7天 · 完整专注" : "最近记录 · 未校时", &s_text.phase);
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
        product_ui_pomodoro_text(s_view.values[row], text, &s_text.values[row]);
        if (snapshot->today) date = product_ui_pomodoro_previous_date(date);
    }
    lv_snprintf(text, sizeof(text), "未归档：%u次  %u分钟\n%s", (unsigned)snapshot->model.saved.undated_count,
        (unsigned)snapshot->model.saved.undated_minutes,
        snapshot->load_error == D_ERR_NOT_FOUND && !snapshot->has_record && snapshot->storage_ready &&
        snapshot->persisted_revision == snapshot->model.saved_revision ? "暂无已保存记录" :
        product_ui_pomodoro_save_status(snapshot));
    product_ui_pomodoro_text(s_view.status, text, &s_text.status);
}

/**
 * @brief 按服务快照及控制器草稿刷新已建视图，模型代次变化撤销旧输入。
 */
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
            product_ui_pomodoro_text(s_view.values[index], text, &s_text.values[index]);
        }
        product_ui_pomodoro_text(s_view.status,
            memcmp(draft, &snapshot->model.saved.settings, sizeof(*draft)) != 0 ?
            "修改尚未保存" : product_ui_pomodoro_save_status(snapshot), &s_text.status);
    } else if (s_view.mode == PRODUCT_UI_POMO_STATS) {
        product_ui_pomodoro_stats_refresh(snapshot);
    } else {
        uint32_t seconds = (snapshot->model.remaining_ms + 999U) / 1000U;
        lv_snprintf(text, sizeof(text), "%s · 本轮完成 %u/%u", product_ui_pomodoro_phase_name(snapshot->model.phase),
            snapshot->model.completed_in_cycle, snapshot->model.cycle_interval);
        product_ui_pomodoro_text(s_view.phase, text, &s_text.phase);
        lv_snprintf(text, sizeof(text), "%02u:%02u", (unsigned)(seconds / 60U), (unsigned)(seconds % 60U));
        product_ui_pomodoro_text(s_view.time, text, &s_text.time);
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
            snapshot->model.state == PRODUCT_UI_POMODORO_PAUSED ? "已暂停" : "准备好后开始", &s_text.status);
        product_ui_pomodoro_text(s_view.primary_label, snapshot->model.state == PRODUCT_UI_POMODORO_RUNNING ? "暂停" :
            snapshot->model.state == PRODUCT_UI_POMODORO_PAUSED ? "继续" : "开始", &s_text.primary);
        if (!snapshot->timer_ready || snapshot->quiescing) lv_obj_add_state(s_view.primary, LV_STATE_DISABLED);
        else lv_obj_remove_state(s_view.primary, LV_STATE_DISABLED);
    }
}

/**
 * @brief 取消预览手势、删除当前视图树并撤销动作回调及文字缓存。
 */
void product_ui_pomodoro_view_destroy(void)
{
    product_ui_pomodoro_cancel_input();
    if (s_view.root) lv_obj_delete(s_view.root);
    memset(&s_text, 0, sizeof(s_text));
    memset(&s_view, 0, sizeof(s_view)); s_action = NULL; s_armed = 0U; s_pressed = NULL;
}
#endif
