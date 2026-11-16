/** @file product_ui_calendar_view.c @brief 日历控件、持久按钮文字、输入身份及日程呈现；控制器拥有查询。 */
#include "config/app_config.h"
#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)
#include "modules/applications/product_ui/features/time/product_ui_calendar_view.h"
#include "modules/applications/product_ui/features/time/product_ui_time_domain_internal.h"
#include "modules/applications/product_ui/shared/widgets/product_ui_components.h"
#include "platform/screen/transition/d_page_transition.h"
#include <string.h>

#define PRODUCT_UI_CALENDAR_GRID_HEIGHT (6 * (PRODUCT_UI_TOUCH_MIN + 2))
#define PRODUCT_UI_CALENDAR_LABEL_BYTES 16U

typedef struct {
    lv_obj_t *root;
    lv_obj_t *grid;
    lv_obj_t *grid_scroll;
    lv_obj_t *month_title;
    lv_obj_t *status;
    lv_obj_t *previous;
    lv_obj_t *next;
    lv_obj_t *year_down;
    lv_obj_t *year_up;
    lv_obj_t *decade_down;
    lv_obj_t *decade_up;
    lv_obj_t *month_down;
    lv_obj_t *month_up;
    lv_obj_t *picker_year;
    lv_obj_t *picker_month;
} product_ui_calendar_widgets_t;

typedef struct {
    lv_obj_t *parent;
    product_ui_calendar_widgets_t widgets;
    product_ui_calendar_presentation_t state;
    product_ui_calendar_action_cb_t callback;
    char cells[PRODUCT_UI_CALENDAR_CELL_COUNT][PRODUCT_UI_CALENDAR_LABEL_BYTES];
    const char *map[48]; /**< 四十二个按钮、五个行分隔及终止项。 */
    product_ui_virtual_list_t events;
    product_ui_calendar_event_t event_slots[PRODUCT_UI_VIRTUAL_LIST_SLOT_COUNT];
    product_ui_list_item_t event_rows[PRODUCT_UI_VIRTUAL_LIST_SLOT_COUNT];
    uint8_t event_order[PRODUCT_UI_TIME_DOMAIN_MAX_EVENTS];
    uint8_t event_count;
    uint32_t event_signature;
    uint32_t seen_event_revision;
    product_ui_date_t seen_selected_date;
    uint8_t is_event_cache_valid;
    uint32_t generation;
    uint32_t pressed_generation;
    lv_obj_t *pressed_object;
    product_ui_date_t pressed_date;
    uint16_t pressed_index;
    uint8_t is_armed;
    uint8_t is_binding;
} product_ui_calendar_view_t;

static product_ui_calendar_view_t s_view;
static product_ui_row_text_cache_t s_event_text[PRODUCT_UI_VIRTUAL_LIST_SLOT_COUNT];

static const char *const s_calendar_terms[] = {
    "", "小寒", "大寒", "立春", "雨水", "惊蛰", "春分", "清明", "谷雨",
    "立夏", "小满", "芒种", "夏至", "小暑", "大暑", "立秋", "处暑",
    "白露", "秋分", "寒露", "霜降", "立冬", "小雪", "大雪", "冬至",
};
static const char *const s_calendar_festivals[] = {
    "元旦", "妇女节", "劳动节", "儿童节", "教师节", "国庆节", "春节",
    "元宵", "端午", "七夕", "中秋", "重阳", "腊八", "除夕", "清明",
};
static const char *const s_calendar_lunar_months[] = {
    "", "正月", "二月", "三月", "四月", "五月", "六月", "七月",
    "八月", "九月", "十月", "冬月", "腊月",
};
static const char *const s_calendar_lunar_days[] = {
    "", "初一", "初二", "初三", "初四", "初五", "初六", "初七", "初八", "初九", "初十",
    "十一", "十二", "十三", "十四", "十五", "十六", "十七", "十八", "十九", "二十",
    "廿一", "廿二", "廿三", "廿四", "廿五", "廿六", "廿七", "廿八", "廿九", "三十",
};
static const char *const s_calendar_weekdays[] = {"周一", "周二", "周三", "周四", "周五", "周六", "周日"};

/**
 * @brief 逐字段比较两个有效日期。
 * @param[in] a 调用链保证非空的左日期。
 * @param[in] b 调用链保证非空的右日期。
 * @return 年月日相同返回非零，否则为零。
 */
static uint8_t product_ui_calendar_dates_equal(const product_ui_date_t *a,
                                              const product_ui_date_t *b)
{
    return a->year == b->year && a->month == b->month && a->day == b->day;
}

/**
 * @brief 根据投影添加或删除对象禁用状态。
 * @param[in,out] object 调用链保证有效的对象。
 * @param[in] disabled 非零禁用，零使能。
 */
static void product_ui_calendar_disable(lv_obj_t *object, uint8_t disabled)
{
    if (disabled) {
        lv_obj_add_state(object, LV_STATE_DISABLED);
    } else {
        lv_obj_remove_state(object, LV_STATE_DISABLED);
    }
}

/**
 * @brief 撤销当前按压所有者并推进输入代次，使旧点击无效。
 */
void product_ui_calendar_view_cancel_press(void)
{
    s_view.is_armed = 0U;
    s_view.pressed_object = NULL;
    s_view.generation++;
}

/**
 * @brief 滚动撤销按压，用户开始浏览时通知控制器停止自动跟随今天。
 * @param[in] event 容器或列表的滚动事件。
 * @note 程序绑定阶段不发布用户浏览动作；滚动及结束请求组合帧。
 */
static void product_ui_calendar_scroll_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);

    product_ui_calendar_view_cancel_press();
    if (code == LV_EVENT_SCROLL_BEGIN && !s_view.is_binding && s_view.callback != NULL) {
        s_view.callback(PRODUCT_UI_CALENDAR_ACTION_BROWSE, NULL);
    }
    if ((code == LV_EVENT_SCROLL) || (code == LV_EVENT_SCROLL_END)) {
        d_screen_request_composed_frame();
    }
}

/**
 * @brief 按对象、输入代次及按钮矩阵日期身份校验完整点击，再提交页面动作。
 * @param[in] event 按钮或日期矩阵事件，用户数据编码动作。
 * @note 只有当前按压所有者的丢失／失焦能撤销资格；按下时捕获日期，松手时矩阵下标必须仍一致。
 */
static void product_ui_calendar_input_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t *object = lv_event_get_current_target(event);
    product_ui_calendar_action_e action =
        (product_ui_calendar_action_e)(uintptr_t)lv_event_get_user_data(event);
    uint32_t index;

    if ((code == LV_EVENT_PRESS_LOST) || (code == LV_EVENT_DEFOCUSED) ||
        (code == LV_EVENT_INDEV_RESET)) {
        /* LVGL 可能先给新按钮 PRESSED，再让旧按钮失焦；
         * 只有当前按压所有者能撤销本轮输入。 */
        if (object == s_view.pressed_object) {
            product_ui_calendar_view_cancel_press();
        }
        return;
    }
    if (code == LV_EVENT_PRESSED) {
        product_ui_calendar_view_cancel_press();
        if (lv_obj_has_state(object, LV_STATE_DISABLED)) {
            return;
        }
        if (action == PRODUCT_UI_CALENDAR_ACTION_SELECT) {
            index = lv_buttonmatrix_get_selected_button(object);
            if ((index >= PRODUCT_UI_CALENDAR_CELL_COUNT) ||
                (s_view.state.month->cells[index].state != PRODUCT_UI_CALENDAR_CELL_CURRENT)) {
                return;
            }
            s_view.pressed_index = (uint16_t)index;
            s_view.pressed_date = s_view.state.month->cells[index].day.solar;
        }
        s_view.pressed_object = object;
        s_view.pressed_generation = s_view.generation;
        s_view.is_armed = 1U;
    } else if (code == LV_EVENT_CLICKED) {
        uint8_t is_valid = s_view.is_armed && s_view.pressed_object == object &&
            s_view.pressed_generation == s_view.generation;
        s_view.is_armed = 0U;
        if (action == PRODUCT_UI_CALENDAR_ACTION_SELECT) {
            index = lv_buttonmatrix_get_selected_button(object);
            is_valid = is_valid && index == s_view.pressed_index;
        }
        if (is_valid && s_view.callback != NULL) {
            s_view.callback(action, action == PRODUCT_UI_CALENDAR_ACTION_SELECT ?
                &s_view.pressed_date : NULL);
        }
    }
}

/**
 * @brief 创建主题文字标签并复制本次文本，关闭点击及手势冒泡。
 * @param[in,out] parent 标签父对象。
 * @param[in] text 本次复制的借用文字。
 * @return 成功返回父对象拥有的标签，失败返回 NULL。
 */
static lv_obj_t *product_ui_calendar_label(lv_obj_t *parent, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);

    if (label != NULL) {
        lv_obj_add_style(label, product_ui_style(PRODUCT_UI_STYLE_BODY), 0);
        lv_label_set_text(label, text);
        lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    }
    return label;
}

/**
 * @brief 创建主题按钮及文字，使用具名动作和共用输入身份校验。
 * @param[in,out] parent 按钮父对象。
 * @param[in] text 本次复制的按钮文字。
 * @param[in] action 页面动作编号。
 * @return 成功返回按钮，失败返回 NULL；标签失败时删除局部按钮。
 */
static lv_obj_t *product_ui_calendar_button(lv_obj_t *parent, const char *text,
                                           product_ui_calendar_action_e action)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_t *label;

    if (button == NULL) {
        return NULL;
    }
    lv_obj_set_size(button, PRODUCT_UI_TOUCH_MIN, PRODUCT_UI_TOUCH_MIN);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    label = product_ui_calendar_label(button, text);
    if (label == NULL) {
        lv_obj_delete(button);
        return NULL;
    }
    product_ui_button_style(button, action == PRODUCT_UI_CALENDAR_ACTION_APPLY ?
        PRODUCT_UI_BUTTON_PRIMARY : PRODUCT_UI_BUTTON_SECONDARY);
    lv_obj_set_size(button, PRODUCT_UI_TOUCH_MIN, PRODUCT_UI_TOUCH_MIN);
    lv_obj_center(label);
    lv_obj_add_event_cb(button, product_ui_calendar_input_cb, LV_EVENT_ALL, (void *)(uintptr_t)action);
    return button;
}

/**
 * @brief 配置视图根纵向布局并绑定滚动取消与组合帧回调。
 * @param[in,out] root 新创建的视图根。
 */
static void product_ui_calendar_container(lv_obj_t *root)
{
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(root, 4, 0);
    lv_obj_set_scroll_dir(root, LV_DIR_VER);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(root, product_ui_calendar_scroll_cb, LV_EVENT_SCROLL_BEGIN, NULL);
    lv_obj_add_event_cb(root, product_ui_calendar_scroll_cb, LV_EVENT_SCROLL, NULL);
    lv_obj_add_event_cb(root, product_ui_calendar_scroll_cb, LV_EVENT_SCROLL_END, NULL);
}

/**
 * @brief 创建固定高度的水平分散布局行。
 * @param[in,out] parent 行父对象。
 * @param[in] height 行高度像素数。
 * @return 成功返回父对象拥有的行，失败返回 NULL。
 */
static lv_obj_t *product_ui_calendar_row(lv_obj_t *parent, int32_t height)
{
    lv_obj_t *row = lv_obj_create(parent);

    if (row != NULL) {
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, lv_pct(100), height);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    }
    return row;
}

/**
 * @brief 在 LVGL 绘制任务创建阶段按今天标志设置独立日期边框。
 * @param[in] event 网格 DRAW_TASK_ADDED 事件。
 * @note 选中填充与今天边框语义分开；仅处理日期单元的边框描述，不修改其他绘制任务。
 */
static void product_ui_calendar_grid_draw_cb(lv_event_t *event)
{
    lv_draw_task_t *task = lv_event_get_draw_task(event);
    lv_draw_border_dsc_t *border;
    lv_obj_t *grid = lv_event_get_current_target(event);

    if (task == NULL || task->type != LV_DRAW_TASK_TYPE_BORDER || task->draw_dsc == NULL) {
        return;
    }
    border = task->draw_dsc;
    if (border->base.part != LV_PART_ITEMS || border->base.id1 >= PRODUCT_UI_CALENDAR_CELL_COUNT) {
        return;
    }
    /* 选中填充表示当前选择，独立边框表示今天；
     * LVGL 9.0 在任务创建阶段提供此描述及按钮编号。 */
    border->opa = lv_buttonmatrix_has_button_ctrl(grid, border->base.id1,
        LV_BUTTONMATRIX_CTRL_CUSTOM_1) ? LV_OPA_COVER : LV_OPA_TRANSP;
}

/**
 * @brief 在候选根创建月份导航、周标题及可滚动四十二格矩阵。
 * @param[in,out] widgets 含候选根并接收子控件引用的描述。
 * @return 完整创建返回 0，任一对象失败返回 -1，候选根由上层统一删除。
 */
static int product_ui_calendar_month_create(product_ui_calendar_widgets_t *widgets)
{
    static const char *const week[] = {"一", "二", "三", "四", "五", "六", "日"};
    lv_obj_t *row = product_ui_calendar_row(widgets->root, PRODUCT_UI_TOUCH_MIN);
    lv_obj_t *title_button;
    lv_obj_t *week_row;
    uint8_t index;

    if (row == NULL) {
        return -1;
    }
    lv_obj_clear_flag(widgets->root, LV_OBJ_FLAG_SCROLLABLE);
    widgets->previous = product_ui_calendar_button(row, LV_SYMBOL_LEFT, PRODUCT_UI_CALENDAR_ACTION_PREVIOUS);
    title_button = product_ui_calendar_button(row, "", PRODUCT_UI_CALENDAR_ACTION_PICKER);
    widgets->next = product_ui_calendar_button(row, LV_SYMBOL_RIGHT, PRODUCT_UI_CALENDAR_ACTION_NEXT);
    if ((widgets->previous == NULL) || (title_button == NULL) || (widgets->next == NULL)) {
        return -1;
    }
    lv_obj_set_width(title_button, 200);
    widgets->month_title = lv_obj_get_child(title_button, 0);
    widgets->status = product_ui_calendar_label(widgets->root, "");
    week_row = product_ui_calendar_row(widgets->root, 18);
    if ((widgets->status == NULL) || (week_row == NULL)) {
        return -1;
    }
    lv_obj_set_style_text_color(widgets->status, product_ui_color(PRODUCT_UI_COLOR_TEXT_MUTED), 0);
    lv_obj_set_style_text_font(widgets->status, product_ui_font(PRODUCT_UI_FONT_CAPTION), 0);
    for (index = 0U; index < 7U; index++) {
        lv_obj_t *label = product_ui_calendar_label(week_row, week[index]);
        if (label == NULL) {
            return -1;
        }
        lv_obj_set_width(label, 40);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(label, product_ui_color(PRODUCT_UI_COLOR_TEXT_MUTED), 0);
    }
    widgets->grid_scroll = lv_obj_create(widgets->root);
    if (widgets->grid_scroll == NULL) {
        return -1;
    }
    lv_obj_remove_style_all(widgets->grid_scroll);
    lv_obj_set_size(widgets->grid_scroll, lv_pct(100), 0);
    lv_obj_set_flex_grow(widgets->grid_scroll, 1U);
    lv_obj_set_scroll_dir(widgets->grid_scroll, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(widgets->grid_scroll, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_clear_flag(widgets->grid_scroll, LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_add_event_cb(widgets->grid_scroll, product_ui_calendar_scroll_cb, LV_EVENT_SCROLL_BEGIN, NULL);
    lv_obj_add_event_cb(widgets->grid_scroll, product_ui_calendar_scroll_cb, LV_EVENT_SCROLL, NULL);
    lv_obj_add_event_cb(widgets->grid_scroll, product_ui_calendar_scroll_cb, LV_EVENT_SCROLL_END, NULL);
    widgets->grid = lv_buttonmatrix_create(widgets->grid_scroll);
    if (widgets->grid == NULL) {
        return -1;
    }
    lv_obj_remove_style_all(widgets->grid);
    lv_obj_set_size(widgets->grid, lv_pct(100), PRODUCT_UI_CALENDAR_GRID_HEIGHT);
    lv_obj_set_style_pad_row(widgets->grid, 2, 0);
    lv_obj_set_style_pad_column(widgets->grid, 2, 0);
    lv_obj_set_style_bg_color(widgets->grid, product_ui_color(PRODUCT_UI_COLOR_SURFACE), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(widgets->grid, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_radius(widgets->grid, PRODUCT_UI_RADIUS_MD, LV_PART_ITEMS);
    lv_obj_set_style_border_width(widgets->grid, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_color(widgets->grid, product_ui_color(PRODUCT_UI_COLOR_ACTION), LV_PART_ITEMS);
    lv_obj_set_style_border_opa(widgets->grid, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_text_font(widgets->grid, product_ui_font(PRODUCT_UI_FONT_CAPTION), LV_PART_ITEMS);
    lv_obj_set_style_text_color(widgets->grid, product_ui_color(PRODUCT_UI_COLOR_TEXT), LV_PART_ITEMS);
    lv_obj_set_style_text_color(widgets->grid, product_ui_color(PRODUCT_UI_COLOR_TEXT_MUTED),
                               LV_PART_ITEMS | LV_STATE_DISABLED);
    lv_obj_set_style_bg_opa(widgets->grid, LV_OPA_TRANSP, LV_PART_ITEMS | LV_STATE_DISABLED);
    lv_obj_set_style_bg_color(widgets->grid, product_ui_color(PRODUCT_UI_COLOR_PRIMARY_SOFT),
                             LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(widgets->grid, product_ui_color(PRODUCT_UI_COLOR_ACTION),
                               LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(widgets->grid, product_ui_color(PRODUCT_UI_COLOR_PRIMARY_SOFT),
                             LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_clear_flag(widgets->grid, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(widgets->grid, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
    lv_obj_add_event_cb(widgets->grid, product_ui_calendar_grid_draw_cb, LV_EVENT_DRAW_TASK_ADDED, NULL);
    lv_obj_add_event_cb(widgets->grid, product_ui_calendar_input_cb, LV_EVENT_ALL,
                        (void *)(uintptr_t)PRODUCT_UI_CALENDAR_ACTION_SELECT);
    return 0;
}

/**
 * @brief 按节日优先、其次节气、最后农历月日选择格内短标签。
 * @param[in] day 调用链保证字段及表下标合法的日信息。
 * @return 静态文字的借用指针。
 */
static const char *product_ui_calendar_short_label(const product_ui_calendar_day_t *day)
{
    uint8_t index;

    for (index = 0U; index < 15U; index++) {
        if ((day->festivals & (1U << index)) != 0U) {
            return s_calendar_festivals[index];
        }
    }
    if (day->term != 0U) {
        return s_calendar_terms[day->term];
    }
    return day->lunar.day == 1U ? s_calendar_lunar_months[day->lunar.month] :
                                s_calendar_lunar_days[day->lunar.day];
}

/**
 * @brief 把控制器月模型绑定到持久矩阵文字，设置选中、今天及范围边缘能力。
 * @param[in] state 借用的已验证呈现状态及持续有效月模型。
 * @note 负滚动目标表示显露选中行，非负目标精确恢复；矩阵 map 借用本模块静态文字，不指向栈缓冲。
 */
static void product_ui_calendar_month_bind(const product_ui_calendar_presentation_t *state)
{
    product_ui_calendar_widgets_t *widgets = &s_view.widgets;
    uint8_t index;
    uint8_t map_index = 0U;
    int32_t scroll_y = state->month_scroll;
    uint8_t selected_row = 0U;

    lv_label_set_text(widgets->month_title, "选择年月");
    lv_label_set_text(widgets->status, "未校时 · 浏览模式");
    lv_obj_update_flag(widgets->status, LV_OBJ_FLAG_HIDDEN, state->has_today);
    product_ui_calendar_disable(widgets->previous,
        state->month->year == PRODUCT_UI_CALENDAR_MIN_YEAR && state->month->month == 1U);
    product_ui_calendar_disable(widgets->next,
        state->month->year == PRODUCT_UI_CALENDAR_MAX_YEAR && state->month->month == 12U);
    for (index = 0U; index < PRODUCT_UI_CALENDAR_CELL_COUNT; index++) {
        const product_ui_calendar_cell_t *cell = &state->month->cells[index];
        const char *short_label = cell->state == PRODUCT_UI_CALENDAR_CELL_OUT_OF_RANGE ? "" :
                                  product_ui_calendar_short_label(&cell->day);
        const char *leap = cell->day.lunar.is_leap && cell->day.lunar.day == 1U &&
                          cell->day.festivals == 0U && cell->day.term == 0U ? "闰" : "";
        lv_snprintf(s_view.cells[index], sizeof(s_view.cells[index]), "%u\n%s%s",
                    cell->day.solar.day, leap, short_label);
        s_view.map[map_index++] = s_view.cells[index];
        if (index % 7U == 6U && index != 41U) {
            s_view.map[map_index++] = "\n";
        }
    }
    s_view.map[map_index] = "";
    lv_buttonmatrix_set_map(widgets->grid, s_view.map);
    lv_buttonmatrix_set_button_ctrl_all(widgets->grid, LV_BUTTONMATRIX_CTRL_NO_REPEAT);
    lv_buttonmatrix_clear_button_ctrl_all(widgets->grid,
        LV_BUTTONMATRIX_CTRL_DISABLED | LV_BUTTONMATRIX_CTRL_CHECKED | LV_BUTTONMATRIX_CTRL_CUSTOM_1);
    for (index = 0U; index < PRODUCT_UI_CALENDAR_CELL_COUNT; index++) {
        const product_ui_calendar_cell_t *cell = &state->month->cells[index];
        if (cell->state != PRODUCT_UI_CALENDAR_CELL_CURRENT) {
            lv_buttonmatrix_set_button_ctrl(widgets->grid, index, LV_BUTTONMATRIX_CTRL_DISABLED);
        } else if (product_ui_calendar_dates_equal(&state->selected, &cell->day.solar)) {
            lv_buttonmatrix_set_button_ctrl(widgets->grid, index, LV_BUTTONMATRIX_CTRL_CHECKED);
            selected_row = index / 7U;
        }
        if (state->has_today && cell->state == PRODUCT_UI_CALENDAR_CELL_CURRENT &&
            product_ui_calendar_dates_equal(&state->today, &cell->day.solar)) {
            lv_buttonmatrix_set_button_ctrl(widgets->grid, index, LV_BUTTONMATRIX_CTRL_CUSTOM_1);
        }
    }
    lv_obj_update_layout(widgets->root);
    if (scroll_y < 0) {
        scroll_y = lv_obj_get_y(widgets->grid) +
            (selected_row + 1) * (PRODUCT_UI_TOUCH_MIN + 2) - lv_obj_get_height(widgets->grid_scroll) + 4;
        if (scroll_y < 0) {
            scroll_y = 0;
        }
    }
    lv_obj_scroll_to_y(widgets->grid_scroll, scroll_y, LV_ANIM_OFF);
}

/**
 * @brief 在候选根创建年、十年、月及确定操作。
 * @param[in,out] widgets 候选根及其输出控件引用。
 * @return 完整创建返回 0，任一步失败返回 -1。
 */
static int product_ui_calendar_picker_create(product_ui_calendar_widgets_t *widgets)
{
    lv_obj_t *year_row;
    lv_obj_t *month_row;
    lv_obj_t *apply;

    if (product_ui_calendar_label(widgets->root, "选择浏览年月") == NULL) {
        return -1;
    }
    year_row = product_ui_calendar_row(widgets->root, PRODUCT_UI_TOUCH_MIN);
    month_row = product_ui_calendar_row(widgets->root, PRODUCT_UI_TOUCH_MIN);
    if ((year_row == NULL) || (month_row == NULL)) {
        return -1;
    }
    widgets->decade_down = product_ui_calendar_button(year_row, "-10", PRODUCT_UI_CALENDAR_ACTION_DECADE_DOWN);
    widgets->year_down = product_ui_calendar_button(year_row, "-", PRODUCT_UI_CALENDAR_ACTION_YEAR_DOWN);
    widgets->picker_year = product_ui_calendar_label(year_row, "");
    widgets->year_up = product_ui_calendar_button(year_row, "+", PRODUCT_UI_CALENDAR_ACTION_YEAR_UP);
    widgets->decade_up = product_ui_calendar_button(year_row, "+10", PRODUCT_UI_CALENDAR_ACTION_DECADE_UP);
    widgets->month_down = product_ui_calendar_button(month_row, "-", PRODUCT_UI_CALENDAR_ACTION_MONTH_DOWN);
    widgets->picker_month = product_ui_calendar_label(month_row, "");
    widgets->month_up = product_ui_calendar_button(month_row, "+", PRODUCT_UI_CALENDAR_ACTION_MONTH_UP);
    if ((widgets->decade_down == NULL) || (widgets->decade_up == NULL) ||
        (widgets->year_down == NULL) || (widgets->picker_year == NULL) ||
        (widgets->year_up == NULL) || (widgets->month_down == NULL) ||
        (widgets->picker_month == NULL) || (widgets->month_up == NULL)) {
        return -1;
    }
    apply = product_ui_calendar_button(widgets->root, "确定", PRODUCT_UI_CALENDAR_ACTION_APPLY);
    if (apply == NULL) {
        return -1;
    }
    lv_obj_set_width(apply, lv_pct(100));
    return 0;
}

/**
 * @brief 显示控制器草稿年月，并在查询上下界禁用相应增减操作。
 * @param[in] state 借用的年月选择器状态。
 */
static void product_ui_calendar_picker_bind(const product_ui_calendar_presentation_t *state)
{
    char value[20];

    lv_snprintf(value, sizeof(value), "%u年", state->picker_year);
    lv_label_set_text(s_view.widgets.picker_year, value);
    lv_snprintf(value, sizeof(value), "%u月", state->picker_month);
    lv_label_set_text(s_view.widgets.picker_month, value);
    product_ui_calendar_disable(s_view.widgets.year_down, state->picker_year == PRODUCT_UI_CALENDAR_MIN_YEAR);
    product_ui_calendar_disable(s_view.widgets.year_up, state->picker_year == PRODUCT_UI_CALENDAR_MAX_YEAR);
    product_ui_calendar_disable(s_view.widgets.decade_down, state->picker_year == PRODUCT_UI_CALENDAR_MIN_YEAR);
    product_ui_calendar_disable(s_view.widgets.decade_up, state->picker_year == PRODUCT_UI_CALENDAR_MAX_YEAR);
    product_ui_calendar_disable(s_view.widgets.month_down, state->picker_month == 1U);
    product_ui_calendar_disable(s_view.widgets.month_up, state->picker_month == 12U);
}

/**
 * @brief 在局部数组中筛选所选日期并按开始时间排序，版本稳定后提交顺序。
 * @param[in] date 筛选的开始日期；跨天事件仍按开始日期归属。
 * @param[out] out_signature 成功时接收选中日完整事件值的内容摘要，失败不写。
 * @return 成功返回 TIME_OK；参数、摘要容量、记录读取或末次代次复核失败返回对应错误。
 * @note 失败或重入变化不得把部分顺序发布给可见行绑定器；日程接口沿用 UI 串行合同。
 */
static product_ui_time_result_e product_ui_calendar_event_order(
    const product_ui_date_t *date, uint32_t *out_signature)
{
    product_ui_calendar_event_t event;
    product_ui_calendar_event_t previous;
    product_ui_time_domain_summary_t summary;
    uint8_t order[PRODUCT_UI_TIME_DOMAIN_MAX_EVENTS] = {0};
    uint8_t count = 0U;
    uint8_t source;
    uint32_t revision;
    product_ui_time_result_e result;
    uint32_t signature = 2166136261UL;

    if (date == NULL || out_signature == NULL) return PRODUCT_UI_TIME_ERR_INVALID_ARGUMENT;
    revision = product_ui_time_domain_get_event_revision();
    result = product_ui_time_domain_get_summary(&summary);
    if (result != PRODUCT_UI_TIME_OK) return result;
    if (summary.event_count > PRODUCT_UI_TIME_DOMAIN_MAX_EVENTS) return PRODUCT_UI_TIME_ERR_CAPACITY;
    for (source = 0U; source < summary.event_count; source++) {
        uint8_t insert;
        uint16_t byte;
        const uint8_t *bytes = (const uint8_t *)&event;

        result = product_ui_time_domain_get_event(source, &event);
        if (result != PRODUCT_UI_TIME_OK) return result;
        if (!product_ui_calendar_dates_equal(date, &event.start.date)) {
            continue;
        }
        for (byte = 0U; byte < sizeof(event); byte++) {
            signature = (signature ^ bytes[byte]) * 16777619UL;
        }
        insert = count;
        while (insert > 0U) {
            result = product_ui_time_domain_get_event(order[insert - 1U], &previous);
            if (result != PRODUCT_UI_TIME_OK) return result;
            if (product_ui_time_compare_datetime(&previous.start, &event.start) <= 0) {
                break;
            }
            order[insert] = order[insert - 1U];
            insert--;
        }
        order[insert] = source;
        count++;
    }
    /* 先在局部完成构建；不完整查询或重入日程变更
     * 不得把部分排序发布给可见行绑定器。 */
    if (revision != product_ui_time_domain_get_event_revision()) return PRODUCT_UI_TIME_ERR_CONFLICT;
    memcpy(s_view.event_order, order, (size_t)count * sizeof(order[0]));
    s_view.event_count = count;
    *out_signature = signature;
    return PRODUCT_UI_TIME_OK;
}

/**
 * @brief 仅在日程代次仍匹配且列表完整呈现时提交日期／代次缓存键。
 * @param[in] revision 查询开始时观察到的日程专用代次。
 * @param[in] date 对应的选中日期。
 * @note 先使旧键失效，再检查 render_pending 和 is_refreshing，避免失败行被缓存命中压制。
 */
static void product_ui_calendar_event_cache_commit(
    uint32_t revision, const product_ui_date_t *date)
{
    s_view.is_event_cache_valid = 0U;
    if (s_view.events.render_pending || s_view.events.is_refreshing ||
        revision != product_ui_time_domain_get_event_revision()) return;
    s_view.seen_event_revision = revision;
    s_view.seen_selected_date = *date;
    s_view.is_event_cache_valid = 1U;
}

/**
 * @brief 按排序逻辑下标复制事件到固定槽，输出只读日程行规格。
 * @param[in] index 当天排序列表的逻辑下标。
 * @param[in] slot 固定复用槽下标。
 * @param[out] spec 接收借用槽内事件标题的行规格。
 * @param[in] context 列表上下文，当前未使用。
 * @return 成功返回 0，参数、槽／逻辑下标或事件读取失败返回 -1。
 */
static int product_ui_calendar_event_bind(uint16_t index, uint8_t slot,
                                         product_ui_row_spec_t *spec, void *context)
{
    product_ui_calendar_event_t *event;
    product_ui_list_item_t *row;
    (void)context;

    if (spec == NULL || slot >= PRODUCT_UI_VIRTUAL_LIST_SLOT_COUNT || index >= s_view.event_count) {
        return -1;
    }
    event = &s_view.event_slots[slot];
    row = &s_view.event_rows[slot];
    if (product_ui_time_domain_get_event(s_view.event_order[index], event) != PRODUCT_UI_TIME_OK) {
        return -1;
    }
    *row = (product_ui_list_item_t){.item_id = "calendar_event", .title = event->title[0] ? event->title : "未命名日程",
        .icon_id = PRODUCT_UI_ICON_SCHEDULE, .accent = PRODUCT_UI_COLOR_WARNING};
    *spec = (product_ui_row_spec_t){.item = row, .item_key = event->id, .appearance = PRODUCT_UI_ROW_APPEARANCE_INSET,
        .subtitle = event->completed ? "已完成" : "待处理", .is_enabled = 1U,
        .interaction = PRODUCT_UI_ROW_INTERACTION_NONE, .trailing = PRODUCT_UI_ROW_TRAILING_NONE};
    return 0;
}

/**
 * @brief 创建选中日期详情及当天有限日程列表，完整刷新后尝试提交缓存键。
 * @param[in,out] widgets 候选视图根及输出控件引用。
 * @param[in] state 借用的所选日期及详情滚动目标。
 * @return 完整创建返回 0，日期查询、排序、任一对象或行刷新失败返回 -1。
 */
static int product_ui_calendar_detail_create(product_ui_calendar_widgets_t *widgets,
                                             const product_ui_calendar_presentation_t *state)
{
    product_ui_calendar_day_t day;
    product_ui_virtual_list_spec_t list_spec = {.row_height = PRODUCT_UI_ROW_DOUBLE_H,
        .bind_cb = product_ui_calendar_event_bind, .text_caches = s_event_text};
    lv_obj_t *header;
    char text[160];
    size_t used = 0U;
    uint8_t index;
    uint32_t revision = product_ui_time_domain_get_event_revision();

    s_view.is_event_cache_valid = 0U;

    if (product_ui_calendar_get_day(&state->selected, &day) != PRODUCT_UI_CALENDAR_OK) {
        return -1;
    }
    if (product_ui_calendar_event_order(&state->selected, &s_view.event_signature) != PRODUCT_UI_TIME_OK) {
        return -1;
    }
    list_spec.item_count = s_view.event_count;
    lv_obj_clear_flag(widgets->root, LV_OBJ_FLAG_SCROLLABLE);
    if (product_ui_virtual_list_create(widgets->root, &s_view.events, &list_spec) != 0) {
        return -1;
    }
    header = s_view.events.header;
    lv_snprintf(text, sizeof(text), "%u年%u月%u日  %s", day.solar.year,
                day.solar.month, day.solar.day, s_calendar_weekdays[day.weekday]);
    if (product_ui_calendar_label(header, text) == NULL) {
        return -1;
    }
    lv_snprintf(text, sizeof(text), "农历%u年 %s%s%s", day.lunar.year,
                day.lunar.is_leap ? "闰" : "", s_calendar_lunar_months[day.lunar.month],
                s_calendar_lunar_days[day.lunar.day]);
    if (product_ui_calendar_label(header, text) == NULL) {
        return -1;
    }
    text[0] = '\0';
    for (index = 0U; index < 15U; index++) {
        if (day.festivals & (1U << index)) {
            used += (size_t)lv_snprintf(text + used, sizeof(text) - used, "%s%s",
                                        used ? " · " : "", s_calendar_festivals[index]);
        }
    }
    if (product_ui_calendar_label(header, used ? text : "当日无节日") == NULL) {
        return -1;
    }
    lv_snprintf(text, sizeof(text), "节气：%s", day.term ? s_calendar_terms[day.term] : "无");
    if (product_ui_calendar_label(header, text) == NULL ||
        product_ui_calendar_label(header, "当日日程") == NULL) {
        return -1;
    }
    widgets->status = product_ui_calendar_label(header, s_view.event_count ? "" : "当天没有日程");
    if (widgets->status == NULL || product_ui_virtual_list_refresh(&s_view.events) != 0) {
        return -1;
    }
    if (state->detail_scroll > 0) {
        lv_obj_update_layout(s_view.events.root);
        lv_obj_scroll_to_y(s_view.events.root, state->detail_scroll, LV_ANIM_OFF);
        if (product_ui_virtual_list_refresh(&s_view.events) != 0) {
            return -1;
        }
    }
    lv_obj_add_event_cb(s_view.events.root, product_ui_calendar_scroll_cb, LV_EVENT_SCROLL, NULL);
    lv_obj_add_event_cb(s_view.events.root, product_ui_calendar_scroll_cb, LV_EVENT_SCROLL_END, NULL);
    product_ui_calendar_event_cache_commit(revision, &state->selected);
    return 0;
}

/**
 * @brief 在无现存视图的串行创建阶段建立父容器及动作回调借用。
 */
void product_ui_calendar_view_init(lv_obj_t *parent, product_ui_calendar_action_cb_t callback)
{
    memset(&s_view, 0, sizeof(s_view));
    s_view.parent = parent;
    s_view.callback = callback;
}

/**
 * @brief 模式变化时先构建完整候选子树再替换旧树，同模式只重新绑定。
 */
int product_ui_calendar_view_show(const product_ui_calendar_presentation_t *state)
{
    product_ui_calendar_widgets_t next = {0};
    int result;

    if (state == NULL || s_view.parent == NULL || state->month == NULL) {
        return -1;
    }
    product_ui_calendar_view_cancel_press();
    if (s_view.widgets.root == NULL || state->mode != s_view.state.mode) {
        next.root = lv_obj_create(s_view.parent);
        if (next.root == NULL) {
            return -1;
        }
        product_ui_calendar_container(next.root);
        lv_obj_update_layout(next.root);
        if (state->mode == PRODUCT_UI_CALENDAR_MONTH) {
            result = product_ui_calendar_month_create(&next);
        } else if (state->mode == PRODUCT_UI_CALENDAR_PICKER) {
            result = product_ui_calendar_picker_create(&next);
        } else {
            result = product_ui_calendar_detail_create(&next, state);
        }
        if (result != 0) {
            lv_obj_delete(next.root);
            return -1;
        }
        if (s_view.widgets.root != NULL) {
            lv_obj_delete(s_view.widgets.root);
        }
        s_view.widgets = next;
        if (state->mode != PRODUCT_UI_CALENDAR_DETAIL) memset(s_event_text, 0, sizeof(s_event_text));
    }
    s_view.state = *state;
    s_view.is_binding = 1U;
    if (state->mode == PRODUCT_UI_CALENDAR_MONTH) {
        product_ui_calendar_month_bind(state);
    } else if (state->mode == PRODUCT_UI_CALENDAR_PICKER) {
        product_ui_calendar_picker_bind(state);
    }
    s_view.is_binding = 0U;
    d_screen_request_composed_frame();
    return 0;
}

/**
 * @brief 读取当前显示视图对应的独立滚动位置。
 */
int32_t product_ui_calendar_view_scroll(void)
{
    if (s_view.state.mode == PRODUCT_UI_CALENDAR_DETAIL) {
        return s_view.events.root != NULL ? lv_obj_get_scroll_y(s_view.events.root) : 0;
    }
    return s_view.widgets.grid_scroll != NULL ? lv_obj_get_scroll_y(s_view.widgets.grid_scroll) : 0;
}

/**
 * @brief 以日程专用代次和选中日期检查详情缓存，再重试有限行绑定。
 */
void product_ui_calendar_view_update(uint32_t elapsed_ms)
{
    uint32_t signature;
    uint32_t revision;
    product_ui_date_t date;

    if (s_view.widgets.root == NULL || s_view.state.mode != PRODUCT_UI_CALENDAR_DETAIL) {
        return;
    }
    revision = product_ui_time_domain_get_event_revision();
    date = s_view.state.selected;
    if (s_view.is_event_cache_valid && revision == s_view.seen_event_revision &&
        product_ui_calendar_dates_equal(&date, &s_view.seen_selected_date)) {
        /* 排序缓存命中也不能压制先前失败的可见行绑定重试。 */
        product_ui_virtual_list_update(&s_view.events, elapsed_ms);
        return;
    }
    if (product_ui_calendar_event_order(&date, &signature) != PRODUCT_UI_TIME_OK ||
        revision != product_ui_time_domain_get_event_revision()) {
        s_view.is_event_cache_valid = 0U;
        product_ui_virtual_list_update(&s_view.events, elapsed_ms);
        return;
    }
    if (!s_view.is_event_cache_valid ||
        !product_ui_calendar_dates_equal(&date, &s_view.seen_selected_date) ||
        signature != s_view.event_signature) {
        s_view.is_event_cache_valid = 0U;
        product_ui_calendar_view_cancel_press();
        lv_label_set_text(s_view.widgets.status, s_view.event_count ? "" : "当天没有日程");
        if (product_ui_virtual_list_set_count(&s_view.events, s_view.event_count) == 0) {
            s_view.event_signature = signature;
            product_ui_calendar_event_cache_commit(revision, &date);
        }
        d_screen_request_composed_frame();
    } else {
        /* 其他日期的日程变化可以只更新缓存键，保留当前可见内容。 */
        product_ui_calendar_event_cache_commit(revision, &date);
    }
    product_ui_virtual_list_update(&s_view.events, elapsed_ms);
}

/**
 * @brief 撤销按压、删除本视图子树并清空回调、父引用及文本缓存。
 */
void product_ui_calendar_view_reset(void)
{
    product_ui_calendar_view_cancel_press();
    if (s_view.widgets.root != NULL) {
        lv_obj_delete(s_view.widgets.root);
    }
    memset(s_event_text, 0, sizeof(s_event_text));
    memset(&s_view, 0, sizeof(s_view));
}

#endif
