/**
 * @file product_ui_alarm_view.c
 * @brief 有限闹钟列表、编辑及音量视图，结构替换与不可变输入身份分开。
 * @author D_Code
 * @date 2026-09-22
 */
#include "config/app_config.h"
#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)
#include "product_ui_alarm_view.h"
#include "product_ui_time_domain_internal.h"
#include "platform/screen/transition/d_page_transition.h"
#include <string.h>

typedef struct {
    lv_obj_t *root, *status, *empty, *hour, *minute, *enabled, *volume, *volume_text, *preview, *retry, *label;
    lv_obj_t *repeats, *labels, *repeat_text;
    product_ui_alarm_mode_e mode;
    product_ui_alarm_repeat_e routine_kind;
} product_ui_alarm_render_t;
typedef struct {
    product_ui_list_item_t item;
    char title[48], subtitle[96];
    lv_obj_t *row;
    uint32_t id, revision, pressed_id, pressed_revision;
    uint8_t enabled, armed, toggle;
} product_ui_alarm_row_t;
static product_ui_alarm_render_t s_view;
/* 候选创建不能清空当前仍可用视图的显示缓存。 */
static struct {
    product_ui_label_cache_t status, repeat, enabled, volume, preview;
} s_text;
static product_ui_virtual_list_t s_list;
static product_ui_alarm_row_t s_rows[PRODUCT_UI_VIRTUAL_LIST_SLOT_COUNT];
static uint32_t s_order[PRODUCT_UI_ALARM_COUNT];
static uint16_t s_order_minutes[PRODUCT_UI_ALARM_COUNT];
static uint8_t s_order_count;
static product_ui_alarm_snapshot_t s_snapshot;
static product_ui_alarm_ui_action_fn s_action;
static product_ui_time_roller_t s_hour_binding, s_minute_binding;
static uint32_t s_generation = 1U, s_pressed_generation, s_seen_revision;
static lv_obj_t *s_pressed, *s_slider;
static uint8_t s_armed, s_slider_original;
static uint32_t s_matrix_pressed;
static lv_point_t s_matrix_origin;

static const char *s_label_map[] = {"闹钟", "起床", "午休", "\n", "吃药", "学习", "运动", ""};

/* 作息控件由一个矩阵承载；事务式准备设置页时，
 * 为每项创建独立对象及样式会超出堆预算。 */
enum {
    PRODUCT_UI_ROUTINE_LEGEND = PRODUCT_UI_ALARM_REPEAT_COUNT,
    PRODUCT_UI_ROUTINE_WEEK_TITLE = 7,
    PRODUCT_UI_ROUTINE_FIRST_DAY = 8,
    PRODUCT_UI_ROUTINE_SECOND_TITLE = 15,
    PRODUCT_UI_ROUTINE_SECOND_DAY = 16,
    PRODUCT_UI_ROUTINE_PHASE_TEXT = 23,
    PRODUCT_UI_ROUTINE_PHASE_FIRST = 24,
    PRODUCT_UI_ROUTINE_PHASE_SECOND = 25,
    PRODUCT_UI_ROUTINE_RESET = 26,
};
static char s_routine_phase_text[112] = "请选择本周属于哪一周";
#define PRODUCT_UI_ROUTINE_CHOICES "仅一次", "每天", "\n", "双休", "单休", "\n", "大小周", "自选星期", "\n",
#define PRODUCT_UI_ROUTINE_DAYS "一", "二", "三", "四", "五", "六", "日", "\n",
static const char *s_routine_simple_map[] = {PRODUCT_UI_ROUTINE_CHOICES
    "确定仅更新草稿，返回取消修改", ""};
static const char *s_routine_week_map[] = {PRODUCT_UI_ROUTINE_CHOICES
    "选中＝响铃，未选中＝休息", "\n", "每周响铃日", "\n", PRODUCT_UI_ROUTINE_DAYS
    "恢复默认星期", "\n", "确定仅更新草稿，返回取消修改", ""};
static const char *s_routine_custom_map[] = {PRODUCT_UI_ROUTINE_CHOICES
    "选中＝响铃，未选中＝休息", "\n", "每周响铃日", "\n", PRODUCT_UI_ROUTINE_DAYS
    "确定仅更新草稿，返回取消修改", ""};
static const char *s_routine_fortnight_map[] = {PRODUCT_UI_ROUTINE_CHOICES
    "选中＝响铃，未选中＝休息", "\n", "第 1 周", "\n", PRODUCT_UI_ROUTINE_DAYS
    "第 2 周", "\n", PRODUCT_UI_ROUTINE_DAYS
    s_routine_phase_text, "\n", "本周第 1 周", "本周第 2 周", "\n",
    "恢复默认星期", "\n", "确定仅更新草稿，返回取消修改", ""};
#undef PRODUCT_UI_ROUTINE_CHOICES
#undef PRODUCT_UI_ROUTINE_DAYS

/* 复用行使用同一几何；只读共用样式避免旧编辑器与替换列表共存时，
 * 给每个子对象额外分配本地样式。 */
static const lv_style_prop_t s_row_geometry_props[] = {LV_STYLE_X, LV_STYLE_Y, LV_STYLE_WIDTH, LV_STYLE_HEIGHT};
static const lv_style_const_prop_t s_row_title_geometry[] = {
    {&s_row_geometry_props[0], {.num = PRODUCT_UI_SPACE_MD}}, {&s_row_geometry_props[1], {.num = 3}},
    {&s_row_geometry_props[2], {.num = 218}}, {&s_row_geometry_props[3], {.num = 18}},
    LV_STYLE_CONST_PROPS_END
};
static const lv_style_const_prop_t s_row_subtitle_geometry[] = {
    {&s_row_geometry_props[0], {.num = PRODUCT_UI_SPACE_MD}}, {&s_row_geometry_props[1], {.num = 25}},
    {&s_row_geometry_props[2], {.num = 232}}, {&s_row_geometry_props[3], {.num = 16}},
    LV_STYLE_CONST_PROPS_END
};
static const lv_style_const_prop_t s_row_toggle_geometry[] = {
    {&s_row_geometry_props[0], {.num = 296 - PRODUCT_UI_SPACE_MD - 32}}, {&s_row_geometry_props[1], {.num = 13}},
    {&s_row_geometry_props[2], {.num = 32}}, {&s_row_geometry_props[3], {.num = 18}},
    LV_STYLE_CONST_PROPS_END
};
static LV_STYLE_CONST_INIT(s_row_title_style, (void *)s_row_title_geometry);
static LV_STYLE_CONST_INIT(s_row_subtitle_style, (void *)s_row_subtitle_geometry);
static LV_STYLE_CONST_INIT(s_row_toggle_style, (void *)s_row_toggle_geometry);

static const lv_style_prop_t s_matrix_spacing_props[] = {LV_STYLE_PAD_ROW, LV_STYLE_PAD_COLUMN};
static const lv_style_const_prop_t s_matrix_spacing[] = {
    {&s_matrix_spacing_props[0], {.num = 8}}, {&s_matrix_spacing_props[1], {.num = 4}}, LV_STYLE_CONST_PROPS_END
};
static LV_STYLE_CONST_INIT(s_matrix_spacing_style, (void *)s_matrix_spacing);

/**
 * @brief 用当前视图代次组织完整动作值并同步交给控制器排队。
 * @param[in] action 页面动作。
 * @param[in] value 动作携带数值。
 * @param[in] id 有记录身份时的稳定规则编号，否则为零。
 * @param[in] revision 按下时观察到的规则版本，否则为零。
 */
static void product_ui_alarm_emit(product_ui_alarm_ui_action_e action, uint16_t value, uint32_t id, uint32_t revision)
{
    product_ui_alarm_ui_event_t event = {action, id, revision, s_generation, value};
    if (s_action != NULL) s_action(&event);
}

/**
 * @brief 推进非零视图代次，取消滚轮／行手势并恢复活跃音量滑条原值。
 */
void product_ui_alarm_view_cancel_input(void)
{
    uint8_t index;
    s_armed = 0U;
    if (++s_generation == 0U) ++s_generation;
    product_ui_time_roller_cancel(&s_hour_binding);
    product_ui_time_roller_cancel(&s_minute_binding);
    if (s_slider != NULL) lv_slider_set_value(s_slider, s_slider_original, LV_ANIM_OFF);
    s_slider = NULL;
    for (index = 0U; index < PRODUCT_UI_VIRTUAL_LIST_SLOT_COUNT; index++) s_rows[index].armed = 0U;
}

/**
 * @brief 读取当前 UI 视图的非零输入代次。
 */
uint32_t product_ui_alarm_view_generation(void) { return s_generation; }

/**
 * @brief 以对象、视图代次和使能状态校验完整按钮点击。
 * @param[in] event 按钮事件，用户数据低字节编码动作，高位编码数值。
 */
static void product_ui_alarm_button_event(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t *object = lv_event_get_current_target(event);
    uintptr_t identity = (uintptr_t)lv_event_get_user_data(event);
    if (code == LV_EVENT_PRESSED) {
        s_pressed = object; s_pressed_generation = s_generation; s_armed = 1U;
    } else if (code == LV_EVENT_PRESS_LOST || code == LV_EVENT_INDEV_RESET || code == LV_EVENT_DEFOCUSED) {
        if (s_pressed == object) s_armed = 0U;
    } else if (code == LV_EVENT_CLICKED) {
        uint8_t accepted = s_armed && s_pressed == object && s_pressed_generation == s_generation &&
            !lv_obj_has_state(object, LV_STATE_DISABLED);
        s_armed = 0U;
        if (accepted) product_ui_alarm_emit((product_ui_alarm_ui_action_e)(identity & 255U),
            (uint16_t)(identity >> 8), 0U, 0U);
    }
}

/**
 * @brief 创建具有动作／数值身份、静态借用文字及共用按压校验的主题按钮。
 * @param[in,out] parent 按钮父容器。
 * @param[in] text 覆盖按钮寿命的不可变文字。
 * @param[in] action 页面动作编号。
 * @param[in] value 动作携带的数值。
 * @return 成功返回父树拥有的按钮，失败返回 NULL。
 */
static lv_obj_t *product_ui_alarm_button(lv_obj_t *parent, const char *text, product_ui_alarm_ui_action_e action, uint16_t value)
{
    const product_ui_time_button_spec_t spec = {
        .text = text,
        .role = action == PRODUCT_UI_ALARM_UI_SAVE ? PRODUCT_UI_BUTTON_PRIMARY :
            action == PRODUCT_UI_ALARM_UI_DELETE ? PRODUCT_UI_BUTTON_DANGER : PRODUCT_UI_BUTTON_SECONDARY,
        .callback = product_ui_alarm_button_event,
        .context = (void *)(uintptr_t)((uint32_t)action | ((uint32_t)value << 8)),
    };
    return product_ui_time_button_create(parent, &spec);
}

/**
 * @brief 创建有稳定动作身份及按压校验的窄导航按钮。
 */
lv_obj_t *product_ui_alarm_view_nav_button(lv_obj_t *parent, const char *text, product_ui_alarm_ui_action_e action)
{
    lv_obj_t *button = product_ui_alarm_button(parent, text, action, 0U);
    if (button != NULL) lv_obj_set_width(button, 52);
    return button;
}

/**
 * @brief 把小时或分钟滚轮的有效松手结果转换为页面动作。
 * @param[in] binding 持久滚轮绑定，field 为零表示小时，非零为分钟。
 * @param[in] selected 已选择的时分选项下标。
 */
static void product_ui_alarm_roller_changed(product_ui_time_roller_t *binding, uint16_t selected)
{ product_ui_alarm_emit(binding->field ? PRODUCT_UI_ALARM_UI_MINUTE : PRODUCT_UI_ALARM_UI_HOUR, selected, 0U, 0U); }

/**
 * @brief 只处理本对象滚动阶段，开始时取消输入并请求组合帧。
 * @param[in] event 列表或编辑根的滚动事件。
 */
static void product_ui_alarm_scroll_event(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code != LV_EVENT_SCROLL_BEGIN && code != LV_EVENT_SCROLL && code != LV_EVENT_SCROLL_END) return;
    if (lv_event_get_target(event) != lv_event_get_current_target(event)) return;
    if (lv_event_get_code(event) == LV_EVENT_SCROLL_BEGIN) product_ui_alarm_view_cancel_input();
    d_screen_request_composed_frame();
}

/**
 * @brief 按计划、作息模板及掩码写入有限重复说明。
 * @param[in] rule 已验证规则。
 * @param[out] text 调用方输出缓冲，须容纳至少“每周”及终止符。
 * @param[in] size 输出总容量。
 */
static void product_ui_alarm_repeat_text(const product_ui_alarm_t *rule, char *text, uint16_t size)
{
    static const char *const days[] = {"日", "一", "二", "三", "四", "五", "六"};
    uint8_t index;
    if (rule->schedule == PRODUCT_UI_ALARM_SCHEDULE_ONCE) lv_snprintf(text, size, "仅一次");
    else if (rule->schedule == PRODUCT_UI_ALARM_SCHEDULE_FORTNIGHT)
        lv_snprintf(text, size, "%s", rule->repeat_mask == 126U && rule->alternate_mask == 62U ? "大小周" : "自定义作息·双周");
    else if (rule->routine_template == PRODUCT_UI_ALARM_ROUTINE_DOUBLE_REST)
        lv_snprintf(text, size, "%s", rule->repeat_mask == 62U ? "双休·周一至周五" : "自定义作息·每周");
    else if (rule->routine_template == PRODUCT_UI_ALARM_ROUTINE_SINGLE_REST)
        lv_snprintf(text, size, "%s", rule->repeat_mask == 126U ? "单休·周一至周六" : "自定义作息·每周");
    else if (rule->repeat_mask == 127U) lv_snprintf(text, size, "每天");
    else if (rule->repeat_mask == 62U) lv_snprintf(text, size, "周一至周五");
    else {
        lv_snprintf(text, size, "每周");
        for (index = 1U; index <= 7U; index++) if (rule->repeat_mask & (1U << (index % 7U)))
            strncat(text, days[index % 7U], size - strlen(text) - 1U);
    }
}

/**
 * @brief 读取当前规则版本并在固定槽格式化标题、下次／错过状态及开关。
 * @param[in] item 排序后的逻辑行下标。
 * @param[in] slot 固定复用槽下标。
 * @param[out] spec 接收借用槽内持久文字的行规格。
 * @param[in] context 列表绑定上下文，当前未使用。
 * @return 成功返回 0，槽／逻辑下标或规则读取失败返回 -1。
 * @note 编号或规则版本变化撤销旧槽按压，虚拟列表拥有带校验的动作分发。
 */
static int product_ui_alarm_row_bind(uint16_t item, uint8_t slot, product_ui_row_spec_t *spec, void *context)
{
    product_ui_alarm_record_t record;
    product_ui_datetime_t next;
    product_ui_alarm_row_t *row;
    char repeat[40];
    int64_t due;
    (void)context;
    if (slot >= PRODUCT_UI_VIRTUAL_LIST_SLOT_COUNT || item >= s_order_count ||
        product_ui_alarm_get_record(s_order[item], &record) != D_OK) return -1;
    row = &s_rows[slot];
    if (row->id != record.rule.id || row->revision != record.rule_revision) row->armed = 0U;
    row->id = record.rule.id; row->revision = record.rule_revision; row->enabled = record.rule.enabled;
    lv_snprintf(row->title, sizeof(row->title), "%02u:%02u  %s", record.rule.hour, record.rule.minute, record.rule.label);
    product_ui_alarm_repeat_text(&record.rule, repeat, sizeof(repeat));
    due = record.state == PRODUCT_UI_ALARM_SNOOZED ? record.snooze_utc : record.next_regular_utc;
    if (record.notice) lv_snprintf(row->subtitle, sizeof(row->subtitle), "%s · %s", repeat,
        record.state == PRODUCT_UI_ALARM_TIME_CONFLICT ? "时间不一致，请重新设置" : "上次提醒已错过");
    else if (!s_snapshot.clock.valid && record.rule.enabled)
        lv_snprintf(row->subtitle, sizeof(row->subtitle), "%s · 等待校时", repeat);
    else if (due && product_ui_time_utc_to_beijing(due, &next) == PRODUCT_UI_TIME_OK)
        lv_snprintf(row->subtitle, sizeof(row->subtitle), "%s · %s %04u/%02u/%02u", repeat,
            record.state == PRODUCT_UI_ALARM_SNOOZED ? "稍后" : "下次", next.date.year, next.date.month, next.date.day);
    else lv_snprintf(row->subtitle, sizeof(row->subtitle), "%s · %s", repeat,
        record.rule.enabled ? "无下次提醒" : "已关闭");
    if (record.state == PRODUCT_UI_ALARM_SNOOZED && due &&
        product_ui_time_utc_to_beijing(due, &next) == PRODUCT_UI_TIME_OK) {
        size_t used = strlen(row->subtitle);
        lv_snprintf(row->subtitle + used, sizeof(row->subtitle) - used, " %02u:%02u", next.time.hour, next.time.minute);
    }
    row->item = (product_ui_list_item_t){.item_id = "alarm_item", .title = row->title,
        .icon_id = PRODUCT_UI_ICON_ALARM, .accent = PRODUCT_UI_COLOR_DANGER};
    *spec = (product_ui_row_spec_t){.item_key = row->id, .item = &row->item, .subtitle = row->subtitle,
        .interaction = PRODUCT_UI_ROW_INTERACTION_ACTION, .trailing = PRODUCT_UI_ROW_TRAILING_SWITCH,
        /* PRODUCT_UI_VIRTUAL_ACTION_INJECTED: 虚拟列表拥有带校验的动作分发。 */
        .is_enabled = 1U, .is_checked = record.rule.enabled};
    return 0;
}

/**
 * @brief 依据当前输入点是否位于行右侧六十像素区判断开关动作。
 * @param[in] row 有效行对象。
 * @return 在切换区返回非零，无输入设备或不在区内返回零。
 */
static uint8_t product_ui_alarm_row_toggle_zone(lv_obj_t *row)
{
    lv_indev_t *input = lv_indev_active();
    lv_point_t point;
    lv_area_t area;
    if (input == NULL) return 0U;
    lv_indev_get_point(input, &point); lv_obj_get_coords(row, &area);
    return point.x >= area.x2 - 60;
}

/**
 * @brief 按下时冻结规则编号、版本和命中区，失去输入资格时撤销行操作。
 * @param[in] event 行全部事件，用户数据借用该复用槽状态。
 */
static void product_ui_alarm_row_input(lv_event_t *event)
{
    product_ui_alarm_row_t *row = lv_event_get_user_data(event);
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) {
        row->pressed_id = row->id; row->pressed_revision = row->revision;
        row->toggle = product_ui_alarm_row_toggle_zone(row->row); row->armed = 1U;
    } else if (code == LV_EVENT_PRESS_LOST || code == LV_EVENT_INDEV_RESET || code == LV_EVENT_DEFOCUSED)
        row->armed = 0U;
}

/**
 * @brief 找到与当前逻辑行匹配且仍装备的槽，复核身份及命中区后分发编辑／切换。
 * @param[in] item 当前排序逻辑下标。
 * @param[in] context 列表动作上下文，当前未使用。
 */
static void product_ui_alarm_row_action(uint16_t item, void *context)
{
    uint8_t index;
    (void)context;
    if (item >= s_order_count) return;
    for (index = 0U; index < PRODUCT_UI_VIRTUAL_LIST_SLOT_COUNT; index++) {
        product_ui_alarm_row_t *row = &s_rows[index];
        if (!row->row || !row->armed || row->id != s_order[item]) continue;
        row->armed = 0U;
        if (row->pressed_id != row->id || row->pressed_revision != row->revision ||
            row->toggle != product_ui_alarm_row_toggle_zone(row->row)) return;
        product_ui_alarm_emit(row->toggle ? PRODUCT_UI_ALARM_UI_TOGGLE : PRODUCT_UI_ALARM_UI_EDIT,
            !row->enabled, row->id, row->revision);
        return;
    }
}

/**
 * @brief 仅为新建立的复用行附着槽状态输入监听，避免重复注册。
 */
static void product_ui_alarm_bind_rows(void)
{
    uint8_t index;
    for (index = 0U; index < PRODUCT_UI_VIRTUAL_LIST_SLOT_COUNT; index++) {
        lv_obj_t *row = s_list.slots[index].row;
        if (row != NULL && row != s_rows[index].row) {
            s_rows[index].row = row;
            lv_obj_add_event_cb(row, product_ui_alarm_row_input, LV_EVENT_ALL, &s_rows[index]);
        }
    }
}

/**
 * @brief 按时分升序、同一时分按编号升序构建规则顺序表。
 * @return 全部读取成功返回 0，任一逻辑记录读取失败返回 -1，部分顺序不提交为完成呈现。
 */
static int product_ui_alarm_build_order(void)
{
    uint8_t index, position;
    product_ui_alarm_record_t record;
    s_order_count = 0U;
    for (index = 0U; index < s_snapshot.count; index++) {
        uint16_t minutes;
        if (product_ui_alarm_get_at(index, &record) != D_OK) return -1;
        minutes = (uint16_t)(record.rule.hour * 60U + record.rule.minute);
        position = s_order_count;
        while (position && (s_order_minutes[position - 1U] > minutes ||
            (s_order_minutes[position - 1U] == minutes && s_order[position - 1U] > record.rule.id))) {
            s_order[position] = s_order[position - 1U];
            s_order_minutes[position] = s_order_minutes[position - 1U];
            position--;
        }
        s_order[position] = record.rule.id; s_order_minutes[position] = minutes;
        s_order_count++;
    }
    return 0;
}

/**
 * @brief 把持久槽缓冲重新绑定到只读标签，更新开关投影。
 * @param[in,out] row 本模块固定三子对象布局的行。
 * @param[in] spec 借用槽内文字的行规格，指针须覆盖该行寿命。
 * @return 子对象和规格完整返回 0，否则返回 -1。
 */
static int product_ui_alarm_row_rebind(lv_obj_t *row, const product_ui_row_spec_t *spec)
{
    lv_obj_t *title = lv_obj_get_child(row, 0), *subtitle = lv_obj_get_child(row, 1), *toggle = lv_obj_get_child(row, 2);
    if (!title || !subtitle || !toggle || !spec || !spec->item) return -1;
    /* 六个槽缓冲覆盖行对象寿命；即使指针未变，槽内格式化字节也可能变化，
     * 因此每次重绑都重新设置静态文本。 */
    lv_label_set_text_static(title, spec->item->title);
    lv_label_set_text_static(subtitle, spec->subtitle);
    if (spec->is_checked) lv_obj_add_state(toggle, LV_STATE_CHECKED);
    else lv_obj_remove_state(toggle, LV_STATE_CHECKED);
    return 0;
}

/**
 * @brief 创建使用静态共用几何样式及持久槽文字的轻量闹钟行。
 * @param[in,out] parent 新行父容器。
 * @param[in] spec 带列表动作回调及持久文字的有效规格。
 * @return 成功返回父树拥有的行，失败删除局部行并返回 NULL。
 */
static lv_obj_t *product_ui_alarm_row_create(lv_obj_t *parent, const product_ui_row_spec_t *spec)
{
    lv_obj_t *row = lv_button_create(parent), *title, *subtitle, *toggle;
    if (!row) return NULL;
    product_ui_time_control_clean(row);
    lv_obj_add_flag(row, LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_CHAIN_VER);
    lv_obj_add_style(row, product_ui_style(PRODUCT_UI_STYLE_INTERACTIVE), 0);
    lv_obj_add_style(row, product_ui_style(PRODUCT_UI_STYLE_PRESSED), LV_STATE_PRESSED);
    lv_obj_set_style_radius(row, PRODUCT_UI_RADIUS_LG, 0);
    lv_obj_set_size(row, 296, PRODUCT_UI_ROW_DOUBLE_H);
    title = product_ui_time_label(row, "", PRODUCT_UI_STYLE_BODY);
    subtitle = product_ui_time_label(row, "", PRODUCT_UI_STYLE_CAPTION);
    toggle = lv_switch_create(row);
    if (!title || !subtitle || !toggle) { lv_obj_delete(row); return NULL; }
    lv_obj_set_style_bg_color(toggle, product_ui_color(PRODUCT_UI_COLOR_ACCENT), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_style(title, &s_row_title_style, 0);
    lv_obj_add_style(subtitle, &s_row_subtitle_style, 0);
    lv_obj_add_style(toggle, &s_row_toggle_style, 0);
    lv_label_set_long_mode(title, LV_LABEL_LONG_CLIP);
    lv_label_set_long_mode(subtitle, LV_LABEL_LONG_CLIP);
    lv_obj_remove_flag(toggle, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(row, spec->action_cb, LV_EVENT_ALL, spec->user_data);
    if (product_ui_alarm_row_rebind(row, spec) != 0) { lv_obj_delete(row); return NULL; }
    return row;
}

/**
 * @brief 排序后创建固定复用列表、校时状态及空提示，并绑定行身份监听。
 * @param[in,out] view 候选根及输出控件引用。
 * @return 完整创建／刷新为 0，任一步失败为 -1，候选树由上层清理。
 */
static int product_ui_alarm_list_build(product_ui_alarm_render_t *view)
{
    product_ui_virtual_list_spec_t spec = {.row_height = PRODUCT_UI_ROW_DOUBLE_H,
        .bind_cb = product_ui_alarm_row_bind, .action_cb = product_ui_alarm_row_action,
        .row_create_cb = product_ui_alarm_row_create, .row_rebind_cb = product_ui_alarm_row_rebind};
    lv_obj_t *header;
    lv_obj_t *time;
    if (product_ui_alarm_build_order() != 0) return -1;
    memset(s_rows, 0, sizeof(s_rows));
    spec.item_count = s_order_count;
    if (product_ui_virtual_list_create(view->root, &s_list, &spec) != 0) return -1;
    header = lv_obj_create(s_list.header);
    if (header == NULL) return -1;
    product_ui_time_control_clean(header);
    lv_obj_set_size(header, LV_PCT(100), 40);
    view->status = product_ui_time_label(header, "", PRODUCT_UI_STYLE_CAPTION);
    time = product_ui_alarm_button(header, "校时", PRODUCT_UI_ALARM_UI_TIME, 0U);
    view->empty = product_ui_time_label(s_list.header, "暂无闹钟，点击新建", PRODUCT_UI_STYLE_BODY);
    if (!view->status || !time || !view->empty) return -1;
    lv_obj_set_width(view->status, 220); lv_label_set_long_mode(view->status, LV_LABEL_LONG_WRAP);
    lv_obj_set_size(time, 64, 36); lv_obj_align(time, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_width(view->empty, 288); lv_label_set_long_mode(view->empty, LV_LABEL_LONG_WRAP);
    lv_obj_add_event_cb(s_list.root, product_ui_alarm_scroll_event, LV_EVENT_ALL, NULL);
    lv_obj_update_flag(view->empty, LV_OBJ_FLAG_HIDDEN, s_order_count != 0U);
    if (product_ui_virtual_list_refresh(&s_list) != 0) return -1;
    product_ui_alarm_bind_rows();
    return 0;
}

/**
 * @brief 把作息矩阵下标解释为模式、两周星期、相位或恢复默认动作。
 * @param[in] index 经矩阵输入校验的按钮下标。
 */
static void product_ui_alarm_routine_action(uint32_t index)
{
    product_ui_alarm_repeat_e kind = s_view.routine_kind;
    if (index < PRODUCT_UI_ALARM_REPEAT_COUNT)
        product_ui_alarm_emit(PRODUCT_UI_ALARM_UI_REPEAT, (uint16_t)index, 0U, 0U);
    else if (kind < PRODUCT_UI_ALARM_REPEAT_DOUBLE) return;
    else if (index >= PRODUCT_UI_ROUTINE_FIRST_DAY && index < PRODUCT_UI_ROUTINE_FIRST_DAY + 7U)
        product_ui_alarm_emit(PRODUCT_UI_ALARM_UI_WEEKDAY, (uint16_t)((index - PRODUCT_UI_ROUTINE_FIRST_DAY + 1U) % 7U), 0U, 0U);
    else if (kind == PRODUCT_UI_ALARM_REPEAT_FORTNIGHT) {
        if (index >= PRODUCT_UI_ROUTINE_SECOND_DAY && index < PRODUCT_UI_ROUTINE_SECOND_DAY + 7U)
            product_ui_alarm_emit(PRODUCT_UI_ALARM_UI_ALTERNATE_DAY, (uint16_t)((index - PRODUCT_UI_ROUTINE_SECOND_DAY + 1U) % 7U), 0U, 0U);
        else if (index == PRODUCT_UI_ROUTINE_PHASE_FIRST || index == PRODUCT_UI_ROUTINE_PHASE_SECOND)
            product_ui_alarm_emit(PRODUCT_UI_ALARM_UI_PHASE, (uint16_t)(index - PRODUCT_UI_ROUTINE_PHASE_FIRST), 0U, 0U);
        else if (index == PRODUCT_UI_ROUTINE_RESET) product_ui_alarm_emit(PRODUCT_UI_ALARM_UI_DEFAULT_DAYS, 0U, 0U, 0U);
    } else if (kind != PRODUCT_UI_ALARM_REPEAT_CUSTOM) {
        if (index == 15U) product_ui_alarm_emit(PRODUCT_UI_ALARM_UI_DEFAULT_DAYS, 0U, 0U, 0U);
    }
}

/**
 * @brief 冻结矩阵按钮及原点，移动、换项或旧视图代次均撤销点击资格。
 * @param[in] event 矩阵全部事件，用户数据编码矩阵动作域。
 */
static void product_ui_alarm_matrix_input(lv_event_t *event)
{
    lv_obj_t *object = lv_event_get_current_target(event);
    lv_event_code_t code = lv_event_get_code(event);
    product_ui_alarm_ui_action_e action = (product_ui_alarm_ui_action_e)(uintptr_t)lv_event_get_user_data(event);
    uint32_t index = lv_buttonmatrix_get_selected_button(object);
    lv_indev_t *input = lv_indev_active();
    if (code == LV_EVENT_PRESSED) {
        s_pressed = object; s_pressed_generation = s_generation; s_matrix_pressed = index;
        s_armed = index != LV_BUTTONMATRIX_BUTTON_NONE &&
            !lv_buttonmatrix_has_button_ctrl(object, index, LV_BUTTONMATRIX_CTRL_DISABLED) && input != NULL;
        if (input) lv_indev_get_point(input, &s_matrix_origin);
    } else if (code == LV_EVENT_PRESSING && s_pressed == object && input) {
        lv_point_t point;
        lv_indev_get_point(input, &point);
        if (index != s_matrix_pressed || LV_ABS(point.x - s_matrix_origin.x) > 8 ||
            LV_ABS(point.y - s_matrix_origin.y) > 8) s_armed = 0U;
    } else if (code == LV_EVENT_PRESS_LOST || code == LV_EVENT_DEFOCUSED || code == LV_EVENT_INDEV_RESET) {
        if (s_pressed == object) s_armed = 0U;
    } else if (code == LV_EVENT_CLICKED) {
        uint8_t accepted = s_armed && s_pressed == object && s_pressed_generation == s_generation && index == s_matrix_pressed;
        s_armed = 0U;
        if (accepted && action == PRODUCT_UI_ALARM_UI_ROUTINE) product_ui_alarm_routine_action(index);
        else if (accepted) product_ui_alarm_emit(action,
            action == PRODUCT_UI_ALARM_UI_WEEKDAY || action == PRODUCT_UI_ALARM_UI_ALTERNATE_DAY ? (index + 1U) % 7U : index, 0U, 0U);
    }
}

/**
 * @brief 创建借用静态布局的矩阵并安装动作身份校验及共用样式。
 * @param[in,out] parent 矩阵父容器。
 * @param[in] action 矩阵动作域。
 * @param[in] map 须覆盖矩阵寿命的布局指针及文字数组。
 * @param[in] height 明确的矩阵高度。
 * @return 成功返回父树拥有的矩阵，创建失败返回 NULL。
 */
static lv_obj_t *product_ui_alarm_matrix(lv_obj_t *parent, product_ui_alarm_ui_action_e action,
    const char **map, int32_t height)
{
    int32_t y = action == PRODUCT_UI_ALARM_UI_LABEL ? 228 : 0;
    lv_obj_t *matrix;
    matrix = lv_buttonmatrix_create(parent);
    if (!matrix) return NULL;
    product_ui_time_control_clean(matrix);
    lv_obj_add_flag(matrix, LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_CHAIN_VER);
    lv_obj_set_size(matrix, 296, height); lv_obj_set_pos(matrix, 0, y);
    lv_buttonmatrix_set_map(matrix, map);
    lv_buttonmatrix_set_button_ctrl_all(matrix, LV_BUTTONMATRIX_CTRL_NO_REPEAT);
    lv_obj_add_style(matrix, &s_matrix_spacing_style, 0);
    lv_obj_add_style(matrix, product_ui_style(PRODUCT_UI_STYLE_BODY), LV_PART_ITEMS);
    lv_obj_add_style(matrix, product_ui_style(PRODUCT_UI_STYLE_INTERACTIVE), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(matrix, product_ui_color(PRODUCT_UI_COLOR_SURFACE_ALT), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(matrix, product_ui_color(PRODUCT_UI_COLOR_ACCENT), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(matrix, product_ui_color(PRODUCT_UI_COLOR_ON_PRIMARY), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_add_style(matrix, product_ui_style(PRODUCT_UI_STYLE_DISABLED), LV_PART_ITEMS | LV_STATE_DISABLED);
    lv_obj_add_event_cb(matrix, product_ui_alarm_matrix_input, LV_EVENT_ALL, (void *)(uintptr_t)action);
    return matrix;
}

/**
 * @brief 在候选根创建时分滚轮、作息入口、标签及启用／删除控件。
 * @param[in,out] view 候选根及输出控件引用。
 * @return 完整创建为 0，任一步失败为 -1。
 */
static int product_ui_alarm_editor_build(product_ui_alarm_render_t *view)
{
    lv_obj_t *label, *button;
    label = product_ui_time_label(view->root, "小时", PRODUCT_UI_STYLE_CAPTION);
    if (label == NULL) return -1;
    lv_obj_set_pos(label, 42, 0); lv_obj_set_width(label, 88); lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    label = product_ui_time_label(view->root, "分钟", PRODUCT_UI_STYLE_CAPTION);
    if (label == NULL) return -1;
    lv_obj_set_pos(label, 164, 0); lv_obj_set_width(label, 88); lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    s_hour_binding.field = 0U; s_hour_binding.changed = product_ui_alarm_roller_changed;
    s_minute_binding.field = 1U; s_minute_binding.changed = product_ui_alarm_roller_changed;
    view->hour = product_ui_time_roller_create(view->root, product_ui_time_hours, &s_hour_binding);
    view->minute = product_ui_time_roller_create(view->root, product_ui_time_minutes, &s_minute_binding);
    if (!view->hour || !view->minute) return -1;
    lv_obj_set_pos(view->hour, 42, 24); lv_obj_set_pos(view->minute, 164, 24);
    view->repeats = product_ui_alarm_button(view->root, "重复与作息", PRODUCT_UI_ALARM_UI_ROUTINE, 0U);
    view->repeat_text = product_ui_time_label(view->root, "", PRODUCT_UI_STYLE_CAPTION);
    if (!view->repeats || !view->repeat_text) return -1;
    lv_obj_set_size(view->repeats, 296, 36); lv_obj_set_pos(view->repeats, 0, 122);
    lv_obj_set_pos(view->repeat_text, 0, 170); lv_obj_set_width(view->repeat_text, 296);
    view->label = product_ui_time_label(view->root, "标签", PRODUCT_UI_STYLE_BODY);
    if (!view->label) return -1;
    lv_obj_set_pos(view->label, 0, 202);
    view->labels = product_ui_alarm_matrix(view->root, PRODUCT_UI_ALARM_UI_LABEL, s_label_map, 80);
    if (!view->labels) return -1;
    view->enabled = product_ui_alarm_button(view->root, "保存后开启", PRODUCT_UI_ALARM_UI_ENABLED, 0U);
    view->status = product_ui_time_label(view->root, "", PRODUCT_UI_STYLE_CAPTION);
    if (!view->enabled || !view->status) return -1;
    lv_obj_set_size(view->enabled, 296, 36); lv_obj_set_pos(view->enabled, 0, 320);
    lv_obj_set_width(view->status, 296); lv_label_set_long_mode(view->status, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(view->status, 0, 366);
    button = product_ui_alarm_button(view->root, "删除闹钟", PRODUCT_UI_ALARM_UI_DELETE, 0U);
    if (button == NULL) return -1;
    view->retry = button;
    lv_obj_set_size(button, 296, 36); lv_obj_set_pos(button, 0, 414);
    return 0;
}

/**
 * @brief 按作息种类选择静态矩阵布局，一次性创建后先禁用全部项等待能力绑定。
 * @param[in,out] view 含候选根及作息种类的视图。
 * @return 创建成功为 0，失败为 -1。
 */
static int product_ui_alarm_routine_build(product_ui_alarm_render_t *view)
{
    const char **map;
    uint8_t rows;
    if (view->routine_kind < PRODUCT_UI_ALARM_REPEAT_DOUBLE) { map = s_routine_simple_map; rows = 4U; }
    else if (view->routine_kind == PRODUCT_UI_ALARM_REPEAT_FORTNIGHT) { map = s_routine_fortnight_map; rows = 12U; }
    else if (view->routine_kind == PRODUCT_UI_ALARM_REPEAT_CUSTOM) { map = s_routine_custom_map; rows = 7U; }
    else { map = s_routine_week_map; rows = 8U; }
    view->repeats = product_ui_alarm_matrix(view->root, PRODUCT_UI_ALARM_UI_ROUTINE, map, rows * 36U + (rows - 1U) * 8U);
    if (!view->repeats) return -1;
    lv_buttonmatrix_set_button_ctrl_all(view->repeats, LV_BUTTONMATRIX_CTRL_NO_REPEAT | LV_BUTTONMATRIX_CTRL_DISABLED);
    return 0;
}

/**
 * @brief 音量仅在同代次有效松手时提交，丢失或重置恢复原值及滚动链。
 * @param[in] event 音量滑条全部事件。
 */
static void product_ui_alarm_slider_event(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t *object = lv_event_get_current_target(event);
    if (code == LV_EVENT_PRESSED) {
        s_slider = object; s_slider_original = (uint8_t)lv_slider_get_value(object);
        s_pressed = object;
        s_pressed_generation = s_generation; s_armed = 1U;
    } else if (code == LV_EVENT_RELEASED) {
        uint8_t accepted = s_armed && s_slider == object && s_pressed_generation == s_generation;
        s_armed = 0U; s_slider = NULL;
        if (accepted) product_ui_alarm_emit(PRODUCT_UI_ALARM_UI_VOLUME, (uint16_t)lv_slider_get_value(object), 0U, 0U);
    } else if (code == LV_EVENT_PRESS_LOST || code == LV_EVENT_INDEV_RESET || code == LV_EVENT_DEFOCUSED) {
        if (s_slider == object) {
            s_armed = 0U;
            if (code == LV_EVENT_INDEV_RESET) lv_obj_send_event(object, LV_EVENT_PRESS_LOST, NULL);
            lv_slider_set_value(object, s_slider_original, LV_ANIM_OFF);
            lv_obj_add_flag(object, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
            s_slider = NULL;
        }
    }
}

/**
 * @brief 在候选根创建独立闹钟音量、试听、校时及保存重试入口。
 * @param[in,out] view 候选根及输出控件引用。
 * @return 完整创建为 0，任一步失败为 -1。
 */
static int product_ui_alarm_settings_build(product_ui_alarm_render_t *view)
{
    lv_obj_t *label, *button;
    label = product_ui_time_label(view->root, "闹钟音量", PRODUCT_UI_STYLE_BODY);
    view->volume_text = product_ui_time_label(view->root, "60%", PRODUCT_UI_STYLE_BODY);
    view->volume = lv_slider_create(view->root);
    if (!label || !view->volume_text || !view->volume) return -1;
    lv_obj_set_pos(label, 0, 0); lv_obj_align(view->volume_text, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_size(view->volume, 176, 10); lv_obj_set_pos(view->volume, 60, 48);
    lv_slider_set_range(view->volume, 1, 100);
    lv_obj_set_ext_click_area(view->volume, PRODUCT_UI_SLIDER_TOUCH_EXTEND);
    lv_obj_remove_flag(view->volume, LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(view->volume, product_ui_alarm_slider_event, LV_EVENT_ALL, NULL);
    button = product_ui_alarm_button(view->root, "-", PRODUCT_UI_ALARM_UI_VOLUME_DOWN, 0U);
    if (!button) return -1;
    lv_obj_set_size(button, 44, 36); lv_obj_set_pos(button, 0, 34);
    button = product_ui_alarm_button(view->root, "+", PRODUCT_UI_ALARM_UI_VOLUME_UP, 0U);
    if (!button) return -1;
    lv_obj_set_size(button, 44, 36); lv_obj_set_pos(button, 252, 34);
    view->preview = product_ui_alarm_button(view->root, "试听", PRODUCT_UI_ALARM_UI_PREVIEW, 0U);
    if (!view->preview) return -1;
    lv_obj_set_size(view->preview, 296, 36); lv_obj_set_pos(view->preview, 0, 88);
    label = product_ui_time_label(view->root, "媒体静音不影响闹钟\n声音不可用时仍会显示提醒", PRODUCT_UI_STYLE_CAPTION);
    view->status = product_ui_time_label(view->root, "", PRODUCT_UI_STYLE_CAPTION);
    button = product_ui_alarm_button(view->root, "日期与时间", PRODUCT_UI_ALARM_UI_TIME, 0U);
    view->retry = product_ui_alarm_button(view->root, "重试保存", PRODUCT_UI_ALARM_UI_RETRY_SAVE, 0U);
    if (!label || !view->status || !button || !view->retry) return -1;
    lv_obj_set_pos(label, 0, 138); lv_obj_set_width(label, 296);
    lv_obj_set_pos(view->status, 0, 186); lv_obj_set_width(view->status, 296);
    lv_label_set_long_mode(view->status, LV_LABEL_LONG_WRAP);
    lv_obj_set_size(button, 296, 36); lv_obj_set_pos(button, 0, 238);
    lv_obj_set_size(view->retry, 296, 36); lv_obj_set_pos(view->retry, 0, 286);
    return 0;
}

/**
 * @brief 仅当前根删除时清空控件及显示缓存，旧候选删除不抹掉新视图。
 * @param[in] event 视图根删除事件。
 */
static void product_ui_alarm_view_deleted(lv_event_t *event)
{
    if (lv_event_get_current_target(event) != s_view.root) return;
    memset(&s_text, 0, sizeof(s_text));
    memset(&s_view, 0, sizeof(s_view));
    s_pressed = s_slider = NULL; s_armed = 0U;
}

/**
 * @brief 必要时先完整构建候选树，再替换当前闹钟子视图。
 */
int product_ui_alarm_view_show(lv_obj_t *parent, product_ui_alarm_mode_e mode,
    const product_ui_alarm_draft_t *draft, product_ui_alarm_ui_action_fn action)
{
    product_ui_alarm_render_t candidate = {0};
    lv_obj_t *old;
    int result;
    if (parent == NULL || draft == NULL || action == NULL || (unsigned)mode >= PRODUCT_UI_ALARM_MODE_COUNT) return -1;
    if (s_view.root && s_view.mode == mode &&
        (mode != PRODUCT_UI_ALARM_ROUTINE || s_view.routine_kind == draft->routine.kind)) return 0;
    product_ui_alarm_get_snapshot(&s_snapshot);
    candidate.mode = mode;
    candidate.routine_kind = draft->routine.kind;
    candidate.root = lv_obj_create(parent);
    if (!candidate.root) return -1;
    product_ui_time_control_clean(candidate.root);
    lv_obj_set_size(candidate.root, 296, 158);
    if (mode != PRODUCT_UI_ALARM_LIST) {
        lv_obj_add_flag(candidate.root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_PRESS_LOCK);
        lv_obj_set_scroll_dir(candidate.root, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(candidate.root, LV_SCROLLBAR_MODE_AUTO);
        lv_obj_add_event_cb(candidate.root, product_ui_alarm_scroll_event, LV_EVENT_ALL, NULL);
    }
    result = mode == PRODUCT_UI_ALARM_LIST ? product_ui_alarm_list_build(&candidate) :
        mode == PRODUCT_UI_ALARM_EDITOR ? product_ui_alarm_editor_build(&candidate) :
        mode == PRODUCT_UI_ALARM_ROUTINE ? product_ui_alarm_routine_build(&candidate) : product_ui_alarm_settings_build(&candidate);
    if (result != 0) { lv_obj_delete(candidate.root); return -1; }
    product_ui_alarm_view_cancel_input();
    old = s_view.root;
    s_view = candidate; s_action = action; s_pressed = s_slider = NULL;
    memset(&s_text, 0, sizeof(s_text));
    lv_obj_add_event_cb(candidate.root, product_ui_alarm_view_deleted, LV_EVENT_DELETE, NULL);
    if (old) lv_obj_delete(old);
    s_seen_revision = 0U;
    product_ui_alarm_view_refresh(&s_snapshot, draft);
    d_screen_request_composed_frame();
    return 0;
}

/**
 * @brief 区分版本保护、未加载、会话数据、在途保存及确认记录的状态文字。
 * @param[in] snapshot 调用链保证非空的服务值。
 * @return 静态状态说明的借用指针。
 */
static const char *product_ui_alarm_save_text(const product_ui_alarm_snapshot_t *snapshot)
{
    if (snapshot->storage_protected) return "存储版本不兼容，无法保存";
    if (!snapshot->loaded) return "读取失败时可在设置中重试";
    if (!snapshot->storage_ready) return "保存不可用，仅本次有效";
    if (!snapshot->has_record && snapshot->load_error != D_OK && snapshot->load_error != D_ERR_NOT_FOUND)
        return "记录读取失败，未生成预置，可重试保存";
    if (snapshot->saving) return "正在保存";
    if (snapshot->dirty) return snapshot->storage_error != D_OK ? "已应用，保存失败，请重试" : "等待保存";
    return snapshot->has_record ? "已保存" : "默认设置，尚无保存记录";
}

/**
 * @brief 经共用缓存更新标签，失败保留其重试语义。
 * @param[in,out] label 借用的目标标签。
 * @param[in] text 本次目标文字。
 * @param[in,out] cache 覆盖标签寿命的文本缓存。
 */
static void product_ui_alarm_set_text(lv_obj_t *label, const char *text,
                                    product_ui_label_cache_t *cache)
{
    (void)product_ui_label_sync(label, text, cache);
}

/**
 * @brief 投影普通按钮的选中与使能状态，空对象忽略。
 * @param[in,out] button 借用按钮。
 * @param[in] checked 非零选中。
 * @param[in] enabled 非零使能。
 */
static void product_ui_alarm_check_button(lv_obj_t *button, uint8_t checked, uint8_t enabled)
{
    if (!button) return;
    if (enabled) lv_obj_remove_state(button, LV_STATE_DISABLED);
    else lv_obj_add_state(button, LV_STATE_DISABLED);
    if (checked) lv_obj_add_state(button, LV_STATE_CHECKED);
    else lv_obj_remove_state(button, LV_STATE_CHECKED);
}

/**
 * @brief 只在差异存在时修改指定矩阵项的选中及禁用控制位。
 * @param[in,out] matrix 借用矩阵，NULL 时忽略。
 * @param[in] index 调用方保证存在的矩阵项下标。
 * @param[in] checked 非零选中。
 * @param[in] enabled 非零可操作。
 */
static void product_ui_alarm_matrix_bind(lv_obj_t *matrix, uint8_t index, uint8_t checked, uint8_t enabled)
{
    if (!matrix) return;
    if (lv_buttonmatrix_has_button_ctrl(matrix, index, LV_BUTTONMATRIX_CTRL_CHECKED) != (checked != 0U)) {
        if (checked) lv_buttonmatrix_set_button_ctrl(matrix, index, LV_BUTTONMATRIX_CTRL_CHECKED);
        else lv_buttonmatrix_clear_button_ctrl(matrix, index, LV_BUTTONMATRIX_CTRL_CHECKED);
    }
    if (lv_buttonmatrix_has_button_ctrl(matrix, index, LV_BUTTONMATRIX_CTRL_DISABLED) == (enabled != 0U)) {
        if (enabled) lv_buttonmatrix_clear_button_ctrl(matrix, index, LV_BUTTONMATRIX_CTRL_DISABLED);
        else lv_buttonmatrix_set_button_ctrl(matrix, index, LV_BUTTONMATRIX_CTRL_DISABLED);
    }
}

/**
 * @brief 投影两周星期及可信本周相位，未校时仍保留既有周期的星期编辑。
 * @param[in] snapshot 借用的时钟及服务值。
 * @param[in] routine 控制器持有的作息子草稿。
 * @note 共享相位说明缓冲静态存活；改字节后显式使矩阵失效，不把栈文字交给 map。
 */
static void product_ui_alarm_routine_refresh(const product_ui_alarm_snapshot_t *snapshot,
    const product_ui_alarm_routine_draft_t *routine)
{
    product_ui_datetime_t now;
    product_ui_date_t monday;
    uint8_t index, week = 0U, has_week = 0U;
    char text[128];
    for (index = 0U; index < PRODUCT_UI_ALARM_REPEAT_COUNT; index++)
        product_ui_alarm_matrix_bind(s_view.repeats, index, routine->kind == index, 1U);
    if (routine->kind < PRODUCT_UI_ALARM_REPEAT_DOUBLE) return;
    for (index = 0U; index < 7U; index++) {
        uint8_t bit = (uint8_t)(1U << ((index + 1U) % 7U));
        product_ui_alarm_matrix_bind(s_view.repeats, PRODUCT_UI_ROUTINE_FIRST_DAY + index, (routine->rule.repeat_mask & bit) != 0U, 1U);
        if (routine->kind == PRODUCT_UI_ALARM_REPEAT_FORTNIGHT)
            product_ui_alarm_matrix_bind(s_view.repeats, PRODUCT_UI_ROUTINE_SECOND_DAY + index, (routine->rule.alternate_mask & bit) != 0U, 1U);
    }
    if (routine->kind != PRODUCT_UI_ALARM_REPEAT_FORTNIGHT) {
        if (routine->kind != PRODUCT_UI_ALARM_REPEAT_CUSTOM) product_ui_alarm_matrix_bind(s_view.repeats, 15U, 0U, 1U);
        return;
    }
    product_ui_alarm_matrix_bind(s_view.repeats, PRODUCT_UI_ROUTINE_RESET, 0U, 1U);
    if (snapshot->clock.valid && product_ui_time_utc_to_beijing(snapshot->clock.utc_seconds, &now) == PRODUCT_UI_TIME_OK &&
        product_ui_alarm_anchor_for_week(&now.date, 0U, &monday) == D_OK) {
        has_week = routine->phase_confirmed && product_ui_alarm_week_index(&routine->rule, &now.date, &week) == D_OK;
        lv_snprintf(text, sizeof(text), "本周起始 %04u/%02u/%02u\n%s", monday.year, monday.month, monday.day,
            has_week ? "已确认周期，可重新选择" : "请选择本周属于哪一周");
    } else lv_snprintf(text, sizeof(text), "%s", routine->phase_confirmed ?
        "未校时，已保留原周期\n可调整响铃日" : "未校时，请到设置中校时");
    if (strcmp(s_routine_phase_text, text)) {
        lv_snprintf(s_routine_phase_text, sizeof(s_routine_phase_text), "%s", text);
        lv_obj_invalidate(s_view.repeats);
    }
    for (index = 0U; index < 2U; index++)
        product_ui_alarm_matrix_bind(s_view.repeats, PRODUCT_UI_ROUTINE_PHASE_FIRST + index, has_week && week == index, snapshot->clock.valid);
}

/**
 * @brief 按服务值及控制器草稿更新当前视图，保留输入预览及失败重试条件。
 */
void product_ui_alarm_view_refresh(const product_ui_alarm_snapshot_t *snapshot, const product_ui_alarm_draft_t *draft)
{
    uint8_t index;
    char text[128];
    uint8_t changed;
    if (!s_view.root || !snapshot || !draft) return;
    changed = snapshot->revision != s_seen_revision || snapshot->clock.valid != s_snapshot.clock.valid ||
        snapshot->clock.revision != s_snapshot.clock.revision;
    s_snapshot = *snapshot;
    if (s_view.mode == PRODUCT_UI_ALARM_LIST) {
        if (changed) {
            product_ui_alarm_view_cancel_input();
            if (product_ui_alarm_build_order() == 0) {
                (void)product_ui_virtual_list_set_count(&s_list, s_order_count);
                if (product_ui_virtual_list_refresh(&s_list) == 0) s_seen_revision = snapshot->revision;
                product_ui_alarm_bind_rows();
            }
        }
        if (!snapshot->loaded) lv_snprintf(text, sizeof(text), "%s", snapshot->load_error ? "读取失败，请在设置中重试" : "正在读取闹钟");
        else if (!snapshot->timer_ready) lv_snprintf(text, sizeof(text), "计时不可用，可查看和编辑");
        else if (!snapshot->clock.valid) lv_snprintf(text, sizeof(text), "未校时 · 每周规则可先保存");
        else lv_snprintf(text, sizeof(text), "%s · %u 条已开启", snapshot->clock.source == PRODUCT_UI_CLOCK_MANUAL ? "手动时间" : "网络时间", snapshot->enabled_count);
        if (snapshot->storage_error || (!snapshot->has_record && snapshot->load_error != D_OK && snapshot->load_error != D_ERR_NOT_FOUND))
            lv_snprintf(text, sizeof(text), "%s", product_ui_alarm_save_text(snapshot));
        product_ui_alarm_set_text(s_view.status, text, &s_text.status);
        lv_obj_update_flag(s_view.empty, LV_OBJ_FLAG_HIDDEN, s_order_count != 0U || !snapshot->loaded);
    } else if (s_view.mode == PRODUCT_UI_ALARM_EDITOR) {
        product_ui_time_roller_bind(&s_hour_binding, s_view.hour, draft->record.rule.hour, s_generation);
        product_ui_time_roller_bind(&s_minute_binding, s_view.minute, draft->record.rule.minute, s_generation);
        product_ui_alarm_repeat_text(&draft->record.rule, text, sizeof(text));
        product_ui_alarm_set_text(s_view.repeat_text, text, &s_text.repeat);
        for (index = 0U; index < 6U; index++) product_ui_alarm_matrix_bind(s_view.labels, index, draft->record.label_kind == index, 1U);
        product_ui_alarm_set_text(lv_obj_get_child(s_view.enabled, 0), draft->record.rule.enabled ? "提醒：已开启" : "提醒：已关闭", &s_text.enabled);
        product_ui_alarm_check_button(s_view.enabled, draft->record.rule.enabled, 1U);
        product_ui_alarm_check_button(s_view.retry, 0U, draft->record.rule.id != 0U);
        product_ui_alarm_set_text(s_view.status, draft->has_conflict ? "记录已变化，请再次保存确认" : !snapshot->clock.valid && draft->record.rule.schedule == PRODUCT_UI_ALARM_SCHEDULE_ONCE ?
            "仅一次闹钟需要先校时\n请到设置中校时" : draft->dirty ? "草稿尚未保存" : product_ui_alarm_save_text(snapshot), &s_text.status);
    } else if (s_view.mode == PRODUCT_UI_ALARM_ROUTINE) {
        product_ui_alarm_routine_refresh(snapshot, &draft->routine);
    } else if (s_view.mode == PRODUCT_UI_ALARM_SETTINGS) {
        if (!s_slider) lv_slider_set_value(s_view.volume, draft->volume, LV_ANIM_OFF);
        lv_snprintf(text, sizeof(text), "%u%%", s_slider ? (unsigned)lv_slider_get_value(s_view.volume) : draft->volume);
        product_ui_alarm_set_text(s_view.volume_text, text, &s_text.volume);
        product_ui_alarm_set_text(lv_obj_get_child(s_view.preview, 0), draft->preview_active ? "停止试听" : "试听提示音", &s_text.preview);
        product_ui_alarm_set_text(s_view.status, draft->dirty ? "音量尚未保存" : product_ui_alarm_save_text(snapshot), &s_text.status);
        product_ui_alarm_check_button(s_view.retry, 0U, !snapshot->storage_protected &&
            (snapshot->storage_error != D_OK || (!snapshot->has_record && snapshot->load_error != D_OK && snapshot->load_error != D_ERR_NOT_FOUND)));
    }
}

/**
 * @brief 列表模式下推进共用虚拟列表重试，并为新复用槽附着记录身份监听。
 */
void product_ui_alarm_view_update(uint32_t elapsed_ms)
{
    if (s_view.mode == PRODUCT_UI_ALARM_LIST && s_view.root) {
        product_ui_virtual_list_update(&s_list, elapsed_ms);
        product_ui_alarm_bind_rows();
    }
}

/**
 * @brief 读取当前列表或编辑子树的独立纵向偏移。
 */
int32_t product_ui_alarm_view_scroll(void)
{
    lv_obj_t *root = s_view.mode == PRODUCT_UI_ALARM_LIST ? s_list.root : s_view.root;
    return root ? lv_obj_get_scroll_y(root) : 0;
}

/**
 * @brief 布局完成后立即恢复当前列表或编辑子树视口。
 */
void product_ui_alarm_view_restore_scroll(int32_t y)
{
    lv_obj_t *root = s_view.mode == PRODUCT_UI_ALARM_LIST ? s_list.root : s_view.root;
    if (root) { lv_obj_update_layout(root); lv_obj_scroll_to_y(root, y, LV_ANIM_OFF); }
}

/**
 * @brief 撤销全部手势，删除当前视图树并清空控件、行副本及动作回调。
 */
void product_ui_alarm_view_destroy(void)
{
    product_ui_alarm_view_cancel_input();
    if (s_view.root) lv_obj_delete(s_view.root);
    memset(&s_view, 0, sizeof(s_view));
    memset(&s_text, 0, sizeof(s_text));
    memset(s_rows, 0, sizeof(s_rows));
    s_action = NULL;
}
#endif
