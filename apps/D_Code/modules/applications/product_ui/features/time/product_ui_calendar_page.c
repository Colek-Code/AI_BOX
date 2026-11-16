/** @file product_ui_calendar_page.c @brief 日历会话控制器，结构变更只在页面更新阶段执行。 */
#include "config/app_config.h"
#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)
#include "modules/applications/product_ui/features/time/product_ui_calendar_page.h"
#include "modules/applications/product_ui/features/time/product_ui_calendar_view.h"
#include "modules/applications/product_ui/shared/widgets/product_ui_components.h"
#include "modules/applications/product_ui/product_ui_runtime.h"
#include "framework/d_ui_module.h"
#include "framework/d_page_manager.h"
#include <string.h>

#define PRODUCT_UI_CALENDAR_REFRESH_MS 500U

typedef struct {
    product_ui_calendar_presentation_t presentation;
    product_ui_date_t pending_date;
    product_ui_calendar_action_e pending;
    uint32_t elapsed_ms;
    uint8_t is_initialized;
    uint8_t has_user_interacted;
    uint8_t follow_today;
    uint8_t holds_pull;
} product_ui_calendar_controller_t;

static product_ui_calendar_controller_t s_calendar;
static product_ui_calendar_month_t s_calendar_month;
static product_ui_page_view_t s_calendar_page;

/**
 * @brief 逐字段比较两个已知有效日期是否相同。
 * @param[in] a 调用链保证非空的左日期。
 * @param[in] b 调用链保证非空的右日期。
 * @return 年月日完全相同返回非零，否则为零。
 */
static uint8_t product_ui_calendar_same_date(const product_ui_date_t *a,
                                             const product_ui_date_t *b)
{
    return a->year == b->year && a->month == b->month && a->day == b->day;
}

/**
 * @brief 浏览动作立即停止自动跟随，其余动作及可选日期进入单槽队列。
 * @param[in] action 视图产生的页面动作。
 * @param[in] date 可为空的日期，存在时复制到队列状态。
 * @note 事件回调不切换结构子树；已有待处理动作时保留先受理的一个。
 */
static void product_ui_calendar_queue(product_ui_calendar_action_e action,
                                       const product_ui_date_t *date)
{
    if (action == PRODUCT_UI_CALENDAR_ACTION_BROWSE) {
        s_calendar.has_user_interacted = 1U;
        s_calendar.follow_today = 0U;
        return;
    }
    if (s_calendar.pending != PRODUCT_UI_CALENDAR_ACTION_NONE) {
        return;
    }
    if (date != NULL) {
        s_calendar.pending_date = *date;
    }
    s_calendar.pending = action;
}

/**
 * @brief 点击导航返回时预约页面返回动作。
 * @param[in] event 导航按钮事件，只处理 CLICKED。
 */
static void product_ui_calendar_back_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        product_ui_calendar_queue(PRODUCT_UI_CALENDAR_ACTION_BACK, NULL);
    }
}

/**
 * @brief 点击今天操作时预约回到可信今天。
 * @param[in] event 导航按钮事件，只处理 CLICKED。
 */
static void product_ui_calendar_today_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        product_ui_calendar_queue(PRODUCT_UI_CALENDAR_ACTION_TODAY, NULL);
    }
}

/**
 * @brief 成功构建当前视图后同步标题、今天能力及选择器下拉占用。
 * @return 成功返回 0，视图替换失败返回 -1，保留旧可用视图。
 */
static int product_ui_calendar_present(void)
{
    product_ui_calendar_presentation_t *state = &s_calendar.presentation;
    char month_title[32];

    if (product_ui_calendar_view_show(state) != 0) {
        return -1;
    }
    lv_snprintf(month_title, sizeof(month_title), "%u年%u月", s_calendar_month.year, s_calendar_month.month);
    lv_label_set_text(s_calendar_page.title, state->mode == PRODUCT_UI_CALENDAR_DETAIL ?
        "日期详情" : state->mode == PRODUCT_UI_CALENDAR_PICKER ? "选择年月" : month_title);
    product_ui_page_action_update(&s_calendar_page, "今天",
        state->has_today && state->mode != PRODUCT_UI_CALENDAR_PICKER, 0U);
    if (state->mode == PRODUCT_UI_CALENDAR_PICKER) {
        product_ui_control_center_hold_pull(&s_calendar.holds_pull);
    } else {
        product_ui_control_center_release_pull(&s_calendar.holds_pull);
    }
    return 0;
}

/**
 * @brief 从时间域核实今天，只有未自主浏览或明确跟随时才移动查询日期。
 * @param[in] is_new_visit 非零表示从首页开始的新日历访问。
 * @return 今天可用性／日期或新访问需要刷新时返回 1，内容未变返回 0。
 * @note 无可信今天时只撤销 has_today，不把浏览锚点当作当前日期；自主查询不被后台校时强制跳走。
 */
static uint8_t product_ui_calendar_update_clock(uint8_t is_new_visit)
{
    product_ui_time_domain_summary_t summary;
    product_ui_calendar_day_t day;
    product_ui_calendar_presentation_t *state = &s_calendar.presentation;
    product_ui_date_t old_today = state->today;
    uint8_t had_today = state->has_today;

    state->has_today = product_ui_time_domain_get_summary(&summary) == PRODUCT_UI_TIME_OK &&
        summary.wall_time_valid &&
        product_ui_calendar_get_day(&summary.wall_time.date, &day) == PRODUCT_UI_CALENDAR_OK;
    if (state->has_today) {
        state->today = summary.wall_time.date;
    }
    if (!is_new_visit && had_today == state->has_today &&
        (!state->has_today || product_ui_calendar_same_date(&old_today, &state->today))) {
        return 0U;
    }
    if (state->has_today && (!s_calendar.has_user_interacted || s_calendar.follow_today)) {
        state->selected = state->today;
        state->month_scroll = -1;
        (void)product_ui_calendar_build_month(state->today.year, state->today.month, &s_calendar_month);
        s_calendar.follow_today = 1U;
    } else if (state->mode == PRODUCT_UI_CALENDAR_MONTH && s_calendar_page.root != NULL) {
        state->month_scroll = product_ui_calendar_view_scroll();
    }
    return 1U;
}

/**
 * @brief 在日历支持上下界内移动一个月，并选择目标月第一天。
 * @param[in] direction 负值前一月，非负值后一月。
 */
static void product_ui_calendar_shift_month(int direction)
{
    uint16_t year = s_calendar_month.year;
    uint8_t month = s_calendar_month.month;

    if (direction < 0) {
        if (year == PRODUCT_UI_CALENDAR_MIN_YEAR && month == 1U) {
            return;
        }
        if (month == 1U) { year--; month = 12U; }
        else { month--; }
    } else {
        if (year == PRODUCT_UI_CALENDAR_MAX_YEAR && month == 12U) {
            return;
        }
        if (month == 12U) { year++; month = 1U; }
        else { month++; }
    }
    (void)product_ui_calendar_build_month(year, month, &s_calendar_month);
    s_calendar.presentation.selected = (product_ui_date_t){year, month, 1U};
    s_calendar.presentation.month_scroll = 0;
}

/**
 * @brief 在控制器副本上处理月份、日期详情、年月选择及今天动作。
 * @param[in] action 已从单槽取出的动作。
 * @note 此函数更新查询状态和月模型，不直接提交视图；上层呈现失败时恢复前一控制器状态。
 */
static void product_ui_calendar_apply_action(product_ui_calendar_action_e action)
{
    product_ui_calendar_presentation_t *state = &s_calendar.presentation;

    if (action != PRODUCT_UI_CALENDAR_ACTION_BACK) {
        s_calendar.has_user_interacted = 1U;
        s_calendar.follow_today = 0U;
    }
    switch (action) {
    case PRODUCT_UI_CALENDAR_ACTION_PREVIOUS:
    case PRODUCT_UI_CALENDAR_ACTION_NEXT:
        product_ui_calendar_shift_month(action == PRODUCT_UI_CALENDAR_ACTION_PREVIOUS ? -1 : 1);
        break;
    case PRODUCT_UI_CALENDAR_ACTION_SELECT:
        state->month_scroll = product_ui_calendar_view_scroll();
        state->selected = s_calendar.pending_date;
        state->detail_scroll = 0;
        state->mode = PRODUCT_UI_CALENDAR_DETAIL;
        break;
    case PRODUCT_UI_CALENDAR_ACTION_PICKER:
        state->month_scroll = product_ui_calendar_view_scroll();
        state->picker_year = s_calendar_month.year;
        state->picker_month = s_calendar_month.month;
        state->mode = PRODUCT_UI_CALENDAR_PICKER;
        break;
    case PRODUCT_UI_CALENDAR_ACTION_YEAR_DOWN:
        if (state->picker_year > PRODUCT_UI_CALENDAR_MIN_YEAR) { state->picker_year--; }
        break;
    case PRODUCT_UI_CALENDAR_ACTION_YEAR_UP:
        if (state->picker_year < PRODUCT_UI_CALENDAR_MAX_YEAR) { state->picker_year++; }
        break;
    case PRODUCT_UI_CALENDAR_ACTION_DECADE_DOWN:
        state->picker_year = state->picker_year - PRODUCT_UI_CALENDAR_MIN_YEAR >= 10U ?
            state->picker_year - 10U : PRODUCT_UI_CALENDAR_MIN_YEAR;
        break;
    case PRODUCT_UI_CALENDAR_ACTION_DECADE_UP:
        state->picker_year = PRODUCT_UI_CALENDAR_MAX_YEAR - state->picker_year >= 10U ?
            state->picker_year + 10U : PRODUCT_UI_CALENDAR_MAX_YEAR;
        break;
    case PRODUCT_UI_CALENDAR_ACTION_MONTH_DOWN:
        if (state->picker_month > 1U) { state->picker_month--; }
        break;
    case PRODUCT_UI_CALENDAR_ACTION_MONTH_UP:
        if (state->picker_month < 12U) { state->picker_month++; }
        break;
    case PRODUCT_UI_CALENDAR_ACTION_APPLY:
        (void)product_ui_calendar_build_month(state->picker_year, state->picker_month, &s_calendar_month);
        state->selected = (product_ui_date_t){state->picker_year, state->picker_month, 1U};
        state->month_scroll = 0;
        state->mode = PRODUCT_UI_CALENDAR_MONTH;
        break;
    case PRODUCT_UI_CALENDAR_ACTION_TODAY:
        if (state->has_today) {
            (void)product_ui_calendar_build_month(state->today.year, state->today.month, &s_calendar_month);
            state->selected = state->today;
            state->month_scroll = -1;
            state->mode = PRODUCT_UI_CALENDAR_MONTH;
            s_calendar.follow_today = 1U;
        }
        break;
    case PRODUCT_UI_CALENDAR_ACTION_BACK:
        state->mode = PRODUCT_UI_CALENDAR_MONTH;
        break;
    default:
        break;
    }
}

/**
 * @brief 消费单个动作，成功呈现后保留新查询，创建失败恢复原控制器及月模型。
 * @note 月视图返回交统一导航；切换前撤销旧视图按压资格，避免旧控件操作新查询。
 */
static void product_ui_calendar_process_action(void)
{
    product_ui_calendar_action_e action = s_calendar.pending;
    product_ui_calendar_controller_t before = s_calendar;
    uint16_t previous_year = s_calendar_month.year;
    uint8_t previous_month = s_calendar_month.month;

    s_calendar.pending = PRODUCT_UI_CALENDAR_ACTION_NONE;
    if (action == PRODUCT_UI_CALENDAR_ACTION_NONE) {
        return;
    }
    if (action == PRODUCT_UI_CALENDAR_ACTION_BACK &&
        s_calendar.presentation.mode == PRODUCT_UI_CALENDAR_MONTH) {
        (void)product_ui_navigation_back();
        return;
    }
    product_ui_calendar_view_cancel_press();
    product_ui_calendar_apply_action(action);
    if (product_ui_calendar_present() != 0) {
        s_calendar = before;
        s_calendar.pending = PRODUCT_UI_CALENDAR_ACTION_NONE;
        (void)product_ui_calendar_build_month(previous_year, previous_month, &s_calendar_month);
        product_ui_toast_show("页面创建失败，请重试", 1600U);
    }
}

/**
 * @brief 创建日历页面及当前查询视图，首次无可信今天时只使用浏览锚点。
 */
int product_ui_calendar_page_create(lv_obj_t *parent)
{
    static const product_ui_page_spec_t spec = {
        .title = "万年历", .back_title = "返回", .show_back = 1U,
        .nav_back_cb = product_ui_calendar_back_cb,
        .nav_action_text = "今天", .nav_action_cb = product_ui_calendar_today_cb,
    };
    uint8_t is_new_visit;

    if (parent == NULL) {
        return -1;
    }
    /* 准备目标页面时，页面管理器的当前页仍是来源页；
     * 从临时设置页返回须保留原查询。 */
    is_new_visit = !s_calendar.is_initialized || page_manager_get_current() == PAGE_TYPE_MAIN;
    if (!s_calendar.is_initialized) {
        s_calendar.presentation.selected = (product_ui_date_t){PRODUCT_UI_CALENDAR_BROWSE_ANCHOR_YEAR, 1U, 1U};
        s_calendar.presentation.month = &s_calendar_month;
        (void)product_ui_calendar_build_month(PRODUCT_UI_CALENDAR_BROWSE_ANCHOR_YEAR, 1U, &s_calendar_month);
        s_calendar.is_initialized = 1U;
    }
    s_calendar.pending = PRODUCT_UI_CALENDAR_ACTION_NONE;
    s_calendar.elapsed_ms = 0U;
    /* 首页进入日历属于新访问；详情／选择器返回不重建本页，
     * 其选择日期及视口继续由控制器持有。 */
    if (is_new_visit) {
        s_calendar.presentation.mode = PRODUCT_UI_CALENDAR_MONTH;
        s_calendar.presentation.detail_scroll = 0;
        s_calendar.has_user_interacted = 0U;
        s_calendar.follow_today = 0U;
    }
    (void)product_ui_calendar_update_clock(is_new_visit);
    if (product_ui_page_frame_create_ex(parent, &spec, &s_calendar_page) != 0) {
        product_ui_page_view_reset(&s_calendar_page);
        return -1;
    }
    lv_obj_set_style_pad_ver(s_calendar_page.content, 4, 0);
    lv_obj_clear_flag(s_calendar_page.content, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    product_ui_calendar_view_init(s_calendar_page.content, product_ui_calendar_queue);
    if (product_ui_calendar_present() != 0) {
        product_ui_calendar_view_reset();
        product_ui_page_view_reset(&s_calendar_page);
        return -1;
    }
    return 0;
}

/**
 * @brief 保存对应视图滚动位置、归还下拉占用并解除 LVGL 状态，保留查询会话。
 * @note 创建准备失败也会调用此路径；视图已经回退时不会读取失效对象。
 */
static void product_ui_calendar_page_destroy(void)
{
    /* 准备失败且临时视图已经重置时，也会调用 destroy。 */
    if (s_calendar_page.root != NULL) {
        if (s_calendar.presentation.mode == PRODUCT_UI_CALENDAR_MONTH) {
            s_calendar.presentation.month_scroll = product_ui_calendar_view_scroll();
        } else if (s_calendar.presentation.mode == PRODUCT_UI_CALENDAR_DETAIL) {
            s_calendar.presentation.detail_scroll = product_ui_calendar_view_scroll();
        }
    }
    product_ui_control_center_release_pull(&s_calendar.holds_pull);
    product_ui_calendar_view_reset();
    product_ui_page_view_reset(&s_calendar_page);
    s_calendar.pending = PRODUCT_UI_CALENDAR_ACTION_NONE;
}

/**
 * @brief 推进公共 UI、消费结构动作，再按节奏更新可信今天及详情事件投影。
 * @param[in] elapsed_ms 本轮页面经过的毫秒数。
 */
static void product_ui_calendar_page_update(uint32_t elapsed_ms)
{
    product_ui_process(elapsed_ms);
    product_ui_calendar_process_action();
    if (s_calendar_page.root == NULL) {
        return;
    }
    if (elapsed_ms < PRODUCT_UI_CALENDAR_REFRESH_MS - s_calendar.elapsed_ms) {
        s_calendar.elapsed_ms += elapsed_ms;
        return;
    }
    s_calendar.elapsed_ms = 0U;
    if (product_ui_calendar_update_clock(0U)) {
        (void)product_ui_calendar_present();
    }
    product_ui_calendar_view_update(PRODUCT_UI_CALENDAR_REFRESH_MS);
}

static const page_ops_t s_product_ui_calendar_ops = {
    .create = product_ui_calendar_page_create,
    .destroy = product_ui_calendar_page_destroy,
    .update = product_ui_calendar_page_update,
    .flags = PAGE_FLAG_NO_GESTURE,
};
static page_desc_t s_product_ui_calendar_desc = {
    .name = "ProductCalendar", .ops = &s_product_ui_calendar_ops,
};

/**
 * @brief 注册静态万年历页面描述。
 */
int product_ui_calendar_register_page(int page_type)
{
    s_product_ui_calendar_desc.page_type = page_type;
    return page_manager_register(&s_product_ui_calendar_desc);
}

D_UI_MODULE_DEFINE(calendar, "日历", LV_SYMBOL_LIST,
                    product_ui_calendar_register_page, NULL);
#endif
