/** @file product_ui_settings_time_page.c @brief 本次开机的软件时间编辑及显式确认，不写 RTC。 */
#include "config/app_config.h"
#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)
#include "product_ui_settings_details.h"
#include "modules/applications/product_ui/features/time/product_ui_time_controls.h"
#include "modules/applications/product_ui/features/time/product_ui_clock.h"
#include "modules/applications/product_ui/features/time/product_ui_time_domain_internal.h"
#include "modules/applications/product_ui/product_ui_runtime.h"
#include "platform/screen/transition/d_page_transition.h"
#include "framework/d_ui_module.h"
#include "framework/d_page_manager.h"
#include <string.h>

enum { TIME_NONE, TIME_YEAR, TIME_MONTH, TIME_DAY, TIME_HOUR, TIME_MINUTE, TIME_SAVE, TIME_AUTO, TIME_BACK };
static struct {
    product_ui_page_view_t page;
    product_ui_datetime_t draft;
    product_ui_clock_snapshot_t clock;
    product_ui_time_roller_t bindings[5];
    lv_obj_t *root, *status, *rollers[5], *auto_button, *pressed;
    uint32_t generation, pressed_generation, confirm_revision;
    uint16_t value;
    uint8_t pending, dirty, armed, confirmed, day_count;
} s_time;

/**
 * @brief 撤销当前手势资格、推进非零输入代次并取消全部滚轮绑定。
 */
static void product_ui_time_cancel_input(void)
{
    uint8_t index;
    s_time.armed = 0U;
    if (++s_time.generation == 0U) ++s_time.generation;
    for (index = 0U; index < 5U; index++) product_ui_time_roller_cancel(&s_time.bindings[index]);
}

/**
 * @brief 仅在单槽为空时保存一个页面动作，避免事件回调中直接修改时间。
 * @param[in] action 页面动作编号。
 * @param[in] value 动作携带的滚轮选项或零。
 */
static void product_ui_time_queue(uint8_t action, uint16_t value)
{ if (s_time.pending == TIME_NONE) { s_time.pending = action; s_time.value = value; } }

/**
 * @brief 把滚轮字段及选项序号排入页面动作槽。
 * @param[in] binding 借用的滚轮绑定，其 field 指明目标日期时间字段。
 * @param[in] selected 当前选项下标。
 */
static void product_ui_time_changed(product_ui_time_roller_t *binding, uint16_t selected)
{ product_ui_time_queue(binding->field, selected); }

/**
 * @brief 把应用手动时间动作排入页面周期。
 * @param[in] event 点击事件，当前不读取载荷。
 */
static void product_ui_time_save_cb(lv_event_t *event) { (void)event; product_ui_time_queue(TIME_SAVE, 0U); }
/**
 * @brief 把返回动作排入页面周期，以便检查草稿及确认。
 * @param[in] event 点击事件，当前不读取载荷。
 */
static void product_ui_time_back_cb(lv_event_t *event) { (void)event; product_ui_time_queue(TIME_BACK, 0U); }

/**
 * @brief 校验同对象、同代次的完整按压后排入恢复自动校时动作。
 * @param[in] event 自动校时按钮事件。
 */
static void product_ui_time_auto_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t *target = lv_event_get_current_target(event);
    if (code == LV_EVENT_PRESSED) {
        s_time.pressed = target; s_time.pressed_generation = s_time.generation; s_time.armed = 1U;
    } else if (code == LV_EVENT_PRESS_LOST || code == LV_EVENT_INDEV_RESET || code == LV_EVENT_DEFOCUSED) s_time.armed = 0U;
    else if (code == LV_EVENT_CLICKED) {
        uint8_t accepted = s_time.armed && s_time.pressed == target && s_time.pressed_generation == s_time.generation;
        s_time.armed = 0U;
        if (accepted) product_ui_time_queue(TIME_AUTO, 0U);
    }
}

/**
 * @brief 滚动开始撤销旧输入资格，并请求组合帧避免滚动残留。
 * @param[in] event 只处理目标与当前目标相同的滚动阶段事件。
 */
static void product_ui_time_scrolled(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code != LV_EVENT_SCROLL_BEGIN && code != LV_EVENT_SCROLL && code != LV_EVENT_SCROLL_END) return;
    if (lv_event_get_target(event) != lv_event_get_current_target(event)) return;
    if (lv_event_get_code(event) == LV_EVENT_SCROLL_BEGIN) product_ui_time_cancel_input();
    d_screen_request_composed_frame();
}

/**
 * @brief 按草稿年月调整日选项，重绑滚轮并显示真实时钟来源及草稿状态。
 */
static void product_ui_time_form_refresh(void)
{
    uint16_t values[5] = {s_time.draft.date.year - PRODUCT_UI_CLOCK_MIN_YEAR, s_time.draft.date.month - 1U,
        s_time.draft.date.day - 1U, s_time.draft.time.hour, s_time.draft.time.minute};
    uint8_t index;
    uint8_t days = product_ui_time_days_in_month(s_time.draft.date.year, s_time.draft.date.month);
    char text[128];
    if (!s_time.root) return;
    if (s_time.day_count != days) {
        product_ui_time_roller_cancel(&s_time.bindings[2]);
        lv_roller_set_options(s_time.rollers[2], product_ui_time_day_options(days), LV_ROLLER_MODE_NORMAL);
        s_time.day_count = days;
    }
    for (index = 0U; index < 5U; index++) product_ui_time_roller_bind(&s_time.bindings[index], s_time.rollers[index], values[index], s_time.generation);
    if (!s_time.clock.valid) lv_snprintf(text, sizeof(text), "未校时 · %s", s_time.dirty ? "草稿尚未应用" : "请联网或设置下方时间");
    else {
        product_ui_datetime_t now;
        (void)product_ui_time_utc_to_beijing(s_time.clock.utc_seconds, &now);
        lv_snprintf(text, sizeof(text), "%s · %04u/%02u/%02u %02u:%02u:%02u\n%s",
            s_time.clock.source == PRODUCT_UI_CLOCK_MANUAL ? "手动" : "网络", now.date.year, now.date.month, now.date.day,
            now.time.hour, now.time.minute, now.time.second, s_time.dirty ? "草稿尚未应用" : "北京时间 UTC+8");
    }
    if (strcmp(lv_label_get_text(s_time.status), text)) lv_label_set_text(s_time.status, text);
}

/**
 * @brief 确认接受且页面仍存在时标记已确认，并重排原操作。
 * @param[in] accepted 非零表示用户接受。
 * @param[in] context 用整数值编码的原动作编号，不是动态对象指针。
 */
static void product_ui_time_confirmed(uint8_t accepted, void *context)
{
    if (accepted && s_time.root) { s_time.confirmed = 1U; product_ui_time_queue((uint8_t)(uintptr_t)context, 0U); }
}

/**
 * @brief 消耗既有确认，或创建确认框并捕获本次时钟代次。
 * @param[in] action 返回、恢复自动或应用手动时间动作。
 * @return 已确认返回 1；等待新确认或创建失败返回 0。
 * @note 紧凑布局拒绝时保留原模态可用，页面仍拥有确认及动作的业务身份。
 */
static uint8_t product_ui_time_confirm(uint8_t action)
{
    lv_obj_t *dialog;
    const char *title = action == TIME_BACK ? "放弃修改" : action == TIME_AUTO ? "恢复自动校时" : "设置手动时间";
    const char *message = action == TIME_BACK ? "未应用的日期与时间将被放弃。" : action == TIME_AUTO ?
        "将等待可信网络时间，重新校准本机时钟。" : "仅本次开机有效，重启需要再次校时。确认后从新时间安排后续闹钟，秒数设为 00。";
    if (s_time.confirmed) { s_time.confirmed = 0U; return 1U; }
    product_ui_time_cancel_input(); s_time.confirm_revision = s_time.clock.revision;
    if (action == TIME_BACK)
        dialog = product_ui_danger_dialog_show(title, message, product_ui_time_confirmed, (void *)(uintptr_t)action);
    else dialog = product_ui_confirm_dialog_show(title, message, product_ui_time_confirmed, (void *)(uintptr_t)action);
    /* 设置页选择共用紧凑布局；布局拒绝时原模态仍可使用，
     * 确认及动作的业务所有权仍由本页持有。 */
    if (dialog != NULL)
        (void)product_ui_dialog_compact(dialog);
    else
        product_ui_toast_show("无法显示确认，请重试", 1600U);
    return 0U;
}

/**
 * @brief 在页面周期消费动作，编辑草稿或按确认时的时钟代次提交校正。
 * @param[in] action 页面动作编号。
 * @param[in] value 滚轮选项序号，年月日按字段映射。
 * @note 年月变化时钳制日期到当月最大值；校时失败保留草稿，不把确认后的外部时钟变化覆盖掉。
 */
static void product_ui_time_apply(uint8_t action, uint16_t value)
{
    d_err_t result = D_OK;
    uint8_t days;
    if (action == TIME_BACK) {
        if (!s_time.dirty || product_ui_time_confirm(action)) (void)product_ui_navigation_back();
        return;
    }
    if (action == TIME_SAVE || action == TIME_AUTO) {
        if (!product_ui_time_confirm(action)) return;
        result = action == TIME_SAVE ? product_ui_clock_set_manual(&s_time.draft, s_time.confirm_revision) :
            product_ui_clock_use_automatic(s_time.confirm_revision);
        if (result == D_OK) { s_time.dirty = 0U; product_ui_toast_show(action == TIME_SAVE ? "本次开机使用手动时间" : "已恢复自动校时", 1800U); }
        else product_ui_toast_show(result == D_ERR_BUSY ? "时间来源已变化，请重新确认" : "校时失败，请重试", 2000U);
        return;
    }
    switch (action) {
    case TIME_YEAR: s_time.draft.date.year = PRODUCT_UI_CLOCK_MIN_YEAR + value; break;
    case TIME_MONTH: s_time.draft.date.month = (uint8_t)(value + 1U); break;
    case TIME_DAY: s_time.draft.date.day = (uint8_t)(value + 1U); break;
    case TIME_HOUR: s_time.draft.time.hour = (uint8_t)value; break;
    case TIME_MINUTE: s_time.draft.time.minute = (uint8_t)value; break;
    default: return;
    }
    days = product_ui_time_days_in_month(s_time.draft.date.year, s_time.draft.date.month);
    if (s_time.draft.date.day > days) s_time.draft.date.day = days;
    s_time.dirty = 1U;
}

/**
 * @brief 创建时间详情框架、五个滚轮及自动校时按钮，以有效时钟或默认日期初始化草稿。
 */
int product_ui_settings_time_page_create(lv_obj_t *parent)
{
    static const product_ui_page_spec_t spec = {.title = "日期与时间", .back_title = "返回", .show_back = 1U,
        .role = PRODUCT_UI_PAGE_ROLE_SETTINGS,
        .nav_back_cb = product_ui_time_back_cb, .nav_action_text = "应用", .nav_action_cb = product_ui_time_save_cb};
    static const char *const captions[5] = {"年", "月", "日", "小时", "分钟"};
    const char *options[5] = {product_ui_time_years, product_ui_time_months, product_ui_time_day_options(31U), product_ui_time_hours, product_ui_time_minutes};
    uint8_t index;
    lv_obj_t *label;
    if (!parent) return -1;
    memset(&s_time, 0, sizeof(s_time));
    s_time.generation = 1U;
    product_ui_clock_get_snapshot(&s_time.clock);
    s_time.draft.date = (product_ui_date_t){PRODUCT_UI_CLOCK_MIN_YEAR, 1U, 1U};
    if (s_time.clock.valid) (void)product_ui_time_utc_to_beijing(s_time.clock.utc_seconds, &s_time.draft);
    s_time.draft.time.second = 0U;
    if (product_ui_page_frame_create_ex(parent, &spec, &s_time.page) != 0) goto fail;
    lv_obj_set_style_pad_ver(s_time.page.content, 4, 0);
    lv_obj_remove_flag(s_time.page.content, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    s_time.root = lv_obj_create(s_time.page.content);
    if (!s_time.root) goto fail;
    product_ui_time_control_clean(s_time.root); lv_obj_set_size(s_time.root, 296, 158);
    lv_obj_add_flag(s_time.root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_PRESS_LOCK); lv_obj_set_scroll_dir(s_time.root, LV_DIR_VER);
    lv_obj_add_event_cb(s_time.root, product_ui_time_scrolled, LV_EVENT_ALL, NULL);
    s_time.status = product_ui_time_label(s_time.root, "", PRODUCT_UI_STYLE_CAPTION);
    if (!s_time.status) goto fail;
    lv_obj_set_width(s_time.status, 296); lv_label_set_long_mode(s_time.status, LV_LABEL_LONG_WRAP);
    for (index = 0U; index < 5U; index++) {
        int32_t x = index < 3U ? 4 + 100 * (int32_t)index : 42 + 122 * ((int32_t)index - 3);
        int32_t y = index < 3U ? 48 : 190;
        label = product_ui_time_label(s_time.root, captions[index], PRODUCT_UI_STYLE_CAPTION);
        if (!label) goto fail;
        lv_obj_set_pos(label, x, y); lv_obj_set_width(label, 88); lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        s_time.bindings[index].field = index + TIME_YEAR; s_time.bindings[index].changed = product_ui_time_changed;
        s_time.rollers[index] = product_ui_time_roller_create(s_time.root, options[index], &s_time.bindings[index]);
        if (!s_time.rollers[index]) goto fail;
        lv_obj_set_pos(s_time.rollers[index], x, y + 24);
    }
    s_time.auto_button = product_ui_time_button_create(s_time.root, &(product_ui_time_button_spec_t){
        .text = "恢复自动校时", .role = PRODUCT_UI_BUTTON_SECONDARY, .callback = product_ui_time_auto_cb});
    label = product_ui_time_label(s_time.root, "手动范围 2024—2099\n手动时间仅本次开机有效\n万年历查询范围不受影响", PRODUCT_UI_STYLE_CAPTION);
    if (!s_time.auto_button || !label) goto fail;
    lv_obj_set_size(s_time.auto_button, 296, 36); lv_obj_set_pos(s_time.auto_button, 0, 334);
    lv_obj_set_pos(label, 0, 382); lv_obj_set_width(label, 296); lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    product_ui_time_form_refresh();
    return 0;
fail:
    if (s_time.root) lv_obj_delete(s_time.root);
    s_time.root = NULL; product_ui_page_view_reset(&s_time.page);
    return -1;
}

/**
 * @brief 推进公共 UI，读取时钟并消费单个排队动作，再刷新草稿投影。
 * @param[in] elapsed_ms 本轮页面毫秒数。
 */
static void product_ui_settings_time_page_update(uint32_t elapsed_ms)
{
    uint8_t action;
    uint16_t value;
    product_ui_process(elapsed_ms);
    if (!s_time.root) return;
    product_ui_clock_get_snapshot(&s_time.clock);
    action = s_time.pending; value = s_time.value; s_time.pending = TIME_NONE;
    if (action != TIME_NONE) product_ui_time_apply(action, value);
    if (!s_time.root) return;
    product_ui_clock_get_snapshot(&s_time.clock);
    product_ui_time_form_refresh();
}

/**
 * @brief 取消输入代次及滚轮，删除编辑子树并清空页面状态。
 */
static void product_ui_settings_time_page_destroy(void)
{
    product_ui_time_cancel_input();
    if (s_time.root) lv_obj_delete(s_time.root);
    product_ui_page_view_reset(&s_time.page);
    memset(&s_time, 0, sizeof(s_time));
}

static const page_ops_t s_time_ops = {.create = product_ui_settings_time_page_create,
    .destroy = product_ui_settings_time_page_destroy, .update = product_ui_settings_time_page_update, .flags = PAGE_FLAG_NO_GESTURE};
static page_desc_t s_time_desc = {.name = "日期与时间", .ops = &s_time_ops};
/**
 * @brief 注册静态时间详情页面描述。
 * @param[in] page_type 模块装配分配的页面类型。
 * @return 原样返回页面管理器注册结果。
 */
static int product_ui_settings_time_register_page(int page_type)
{ s_time_desc.page_type = page_type; return page_manager_register(&s_time_desc); }
D_UI_MODULE_DEFINE(product_settings_time, "日期与时间", LV_SYMBOL_SETTINGS, product_ui_settings_time_register_page, NULL);
#endif
