/** @file product_ui_pomodoro_page.c @brief 番茄钟命令及延后视图切换；后台计时与保存持续超出页面寿命。 */
#include "config/app_config.h"
#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)
#include "product_ui_pomodoro_page.h"
#include "product_ui_pomodoro_view.h"
#include "modules/applications/product_ui/product_ui_runtime.h"
#include "modules/applications/product_ui/runtime/service_registry/product_ui_service_registry.h"
#include "framework/d_ui_module.h"
#include "framework/d_page_manager.h"
#include <string.h>

static struct {
    product_ui_page_view_t page;
    product_ui_pomodoro_snapshot_t snapshot;
    product_ui_pomodoro_settings_t draft;
    product_ui_pomo_mode_e mode;
    product_ui_pomo_action_e pending;
    product_ui_pomo_action_e confirmed;
    product_ui_pomo_action_e leave_action;
    uint32_t command_generation;
    uint32_t confirmation_generation;
    uint32_t save_request;
    uint8_t field;
    uint8_t value;
    uint8_t holds_pull;
    uint8_t save_before_leave;
    uint8_t leave_dialog;
} s_page;

/**
 * @brief 在单槽为空时保存视图动作及其字段、数值和操作代次。
 * @param[in] action 页面动作编号。
 * @param[in] field 设置字段或已经确认的动作标记。
 * @param[in] value 设置目标值。
 * @param[in] generation 动作产生时的模型代次。
 */
static void product_ui_pomodoro_queue(product_ui_pomo_action_e action, uint8_t field,
    uint8_t value, uint32_t generation)
{
    if (s_page.pending != PRODUCT_UI_POMO_NONE) return;
    s_page.pending = action; s_page.field = field; s_page.command_generation = generation;
    s_page.value = value;
}

/**
 * @brief 点击导航返回时预约带当前模型代次的返回动作。
 * @param[in] event 导航按钮事件，仅处理 CLICKED。
 */
static void product_ui_pomodoro_back(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED)
        product_ui_pomodoro_queue(PRODUCT_UI_POMO_BACK, 0U, 0U, s_page.snapshot.model.generation);
}

/**
 * @brief 事务式创建目标子视图，成功后更新模式、草稿及下拉占用。
 * @param[in] mode 计时、设置或统计视图。
 * @return 成功返回 0，视图构建失败返回 -1 并保留当前模式。
 * @note 仅首次进入设置时从当前保存值建立草稿；页面持有草稿，视图只呈现。
 */
static int product_ui_pomodoro_show(product_ui_pomo_mode_e mode)
{
    if (product_ui_pomodoro_view_show(s_page.page.content, mode, &s_page.snapshot,
                                     product_ui_pomodoro_queue) != 0) return -1;
    if (mode == PRODUCT_UI_POMO_SETTINGS && s_page.mode != mode) s_page.draft = s_page.snapshot.model.saved.settings;
    s_page.mode = mode;
    if (mode == PRODUCT_UI_POMO_SETTINGS) product_ui_control_center_hold_pull(&s_page.holds_pull);
    else product_ui_control_center_release_pull(&s_page.holds_pull);
    lv_label_set_text(s_page.page.title, mode == PRODUCT_UI_POMO_SETTINGS ? "番茄设置" :
        mode == PRODUCT_UI_POMO_STATS ? "专注统计" : "番茄钟");
    product_ui_pomodoro_view_refresh(&s_page.snapshot, &s_page.draft);
    return 0;
}

/**
 * @brief 危险操作确认接受后，用原确认代次重新预约该动作。
 * @param[in] confirmed 非零表示接受。
 * @param[in] context 确认上下文，当前未使用。
 */
static void product_ui_pomodoro_confirmed(uint8_t confirmed, void *context)
{
    (void)context;
    if (confirmed && s_page.page.root != NULL)
        product_ui_pomodoro_queue(s_page.confirmed, 1U, 0U, s_page.confirmation_generation);
}

/**
 * @brief 只在设置模式中按字段范围修改本页草稿，不直接更新服务。
 * @param[in] action 增加、减少、切换或设置时长动作。
 * @param[in] field 七个设置字段下标。
 * @param[in] value 滑条提交的时长值，其他动作忽略。
 */
static void product_ui_pomodoro_edit(product_ui_pomo_action_e action, uint8_t field, uint8_t value)
{
    uint8_t *values[] = {&s_page.draft.focus_minutes, &s_page.draft.short_minutes,
        &s_page.draft.long_minutes, &s_page.draft.interval, &s_page.draft.auto_focus,
        &s_page.draft.auto_break, &s_page.draft.sound};
    static const uint8_t maxima[] = {120,30,60,8};
    if (s_page.mode != PRODUCT_UI_POMO_SETTINGS || field >= 7U) return;
    if (action == PRODUCT_UI_POMO_SET_DURATION && field < 3U && value >= 1U && value <= maxima[field])
        *values[field] = value;
    else if (action == PRODUCT_UI_POMO_TOGGLE && field >= 4U) *values[field] ^= 1U;
    else if (field < 4U && action == PRODUCT_UI_POMO_DECREASE && *values[field] > 1U) (*values[field])--;
    else if (field < 4U && action == PRODUCT_UI_POMO_INCREASE && *values[field] < maxima[field]) (*values[field])++;
}

/**
 * @brief 检查草稿与服务设置差异，或服务保存代次尚未确认。
 * @return 任一条件存在返回非零，否则为零。
 */
static uint8_t product_ui_pomodoro_unsaved(void)
{
    return memcmp(&s_page.draft, &s_page.snapshot.model.saved.settings,
                  sizeof(s_page.draft)) != 0 ||
        s_page.snapshot.persisted_revision != s_page.snapshot.model.saved_revision;
}

/**
 * @brief 把保存／放弃选择转为延后动作，并撤销离开对话框状态。
 * @param[in] choice 1 保存，2 放弃，其余取消离开。
 * @param[in] context 对话框上下文，当前未使用。
 */
static void product_ui_pomodoro_leave_choice(uint8_t choice, void *context)
{
    (void)context;
    if (!s_page.leave_dialog || !s_page.page.root || s_page.mode != PRODUCT_UI_POMO_SETTINGS) return;
    s_page.leave_dialog = 0U;
    if (choice == 1U || choice == 2U)
        product_ui_pomodoro_queue(choice == 1U ? PRODUCT_UI_POMO_SAVE : s_page.leave_action,
            1U, 0U, s_page.snapshot.model.generation);
    else s_page.leave_action = PRODUCT_UI_POMO_NONE;
}

/**
 * @brief 设置页有未保存内容时拦截返回或统计导航，并创建保存选择框。
 * @param[in] action 待执行的离开动作。
 * @return 已拦截并处理返回 1，无需拦截返回 0。
 * @note 选择框创建失败仍拦截本次离开并提示，不丢弃草稿。
 */
static uint8_t product_ui_pomodoro_guard_leave(product_ui_pomo_action_e action)
{
    if (s_page.mode != PRODUCT_UI_POMO_SETTINGS || s_page.field ||
        (action != PRODUCT_UI_POMO_BACK && action != PRODUCT_UI_POMO_STATS_OPEN) ||
        !product_ui_pomodoro_unsaved()) return 0U;
    s_page.leave_action = action;
    s_page.leave_dialog = 1U;
    if (!product_ui_save_dialog_show("保存设置？", "设置尚未保存，保存后离开？",
            product_ui_pomodoro_leave_choice, NULL)) {
        s_page.leave_dialog = 0U;
        s_page.leave_action = PRODUCT_UI_POMO_NONE;
        product_ui_toast_show("无法打开确认，请重试", 1500U);
    }
    return 1U;
}

/**
 * @brief 按草稿差异选择保存或重读重试，并记录成功受理后的离开条件。
 * @return 保存正在执行返回 BUSY，其余返回服务保存／重试受理结果。
 * @note 未读取的旧记录不能被未变默认值覆盖；服务即使先应用 RAM 草稿，也使用既有重试路径恢复受保护的初始记录。
 */
static d_err_t product_ui_pomodoro_save_draft(void)
{
    uint32_t request = 0U;
    d_err_t result;
    if (s_page.snapshot.saving) return D_ERR_BUSY;
    if (memcmp(&s_page.draft, &s_page.snapshot.model.saved.settings, sizeof(s_page.draft)) == 0 &&
        (s_page.snapshot.persisted_revision != s_page.snapshot.model.saved_revision ||
         s_page.snapshot.load_error == D_ERR_IO || s_page.snapshot.load_error == D_ERR_NOT_READY)) {
        /* 未成功读取的原记录不能被未变默认值覆盖。 */
        result = product_ui_pomodoro_retry_save(&request);
    } else {
        result = product_ui_pomodoro_save_settings(&s_page.draft, &request);
    /* 原记录恢复受阻时，服务可能已应用 RAM 草稿；同一保存按钮
     * 也请求既有恢复路径，不能用默认值绕过保护。 */
        if (request != 0U) result = product_ui_pomodoro_retry_save(&request);
    }
    if (result == D_OK) {
        s_page.save_request = request;
        s_page.save_before_leave = s_page.field != 0U;
    }
    return result;
}

/**
 * @brief 消费一个页面动作，执行必要确认、草稿修改、子视图切换或服务受理。
 * @param[in] action 本轮排队动作；其他参数从单槽页面状态读取。
 * @note 等待保存离开或离开选择期间不接纳其他动作；旧模型代次不能修改当前草稿或计时动作。
 */
static void product_ui_pomodoro_apply(product_ui_pomo_action_e action)
{
    d_err_t result = D_OK;
    uint32_t request;
    if (s_page.save_before_leave || s_page.leave_dialog) return;
    if (product_ui_pomodoro_guard_leave(action)) return;
    if ((action == PRODUCT_UI_POMO_END || action == PRODUCT_UI_POMO_CLEAR) && !s_page.field) {
        s_page.confirmed = action;
        s_page.confirmation_generation = s_page.command_generation;
        if (product_ui_danger_dialog_show(action == PRODUCT_UI_POMO_END ? "结束本轮" : "清空统计",
            action == PRODUCT_UI_POMO_END ? "未完成专注不计入统计，确定结束？" : "清空完成记录，当前计时继续。确定清空？",
            product_ui_pomodoro_confirmed, NULL) == NULL) product_ui_toast_show("无法打开确认，请重试", 1500U);
        return;
    }
    switch (action) {
    case PRODUCT_UI_POMO_PRIMARY:
        result = product_ui_pomodoro_execute(s_page.snapshot.model.state == PRODUCT_UI_POMODORO_RUNNING ?
            PRODUCT_UI_POMODORO_PAUSE : s_page.snapshot.model.state == PRODUCT_UI_POMODORO_PAUSED ?
            PRODUCT_UI_POMODORO_RESUME : PRODUCT_UI_POMODORO_START, s_page.command_generation);
        break;
    case PRODUCT_UI_POMO_SKIP:
    case PRODUCT_UI_POMO_END:
        result = product_ui_pomodoro_execute(action == PRODUCT_UI_POMO_SKIP ? PRODUCT_UI_POMODORO_SKIP :
            PRODUCT_UI_POMODORO_END, s_page.command_generation);
        break;
    case PRODUCT_UI_POMO_SETTINGS_OPEN: result = product_ui_pomodoro_show(PRODUCT_UI_POMO_SETTINGS); break;
    case PRODUCT_UI_POMO_STATS_OPEN: result = product_ui_pomodoro_show(PRODUCT_UI_POMO_STATS); break;
    case PRODUCT_UI_POMO_BACK:
        if (s_page.mode == PRODUCT_UI_POMO_TIMER) (void)product_ui_navigation_back();
        else result = product_ui_pomodoro_show(PRODUCT_UI_POMO_TIMER);
        break;
    case PRODUCT_UI_POMO_SAVE: result = product_ui_pomodoro_save_draft(); break;
    case PRODUCT_UI_POMO_CLEAR: result = product_ui_pomodoro_clear_stats(&request); break;
    case PRODUCT_UI_POMO_DECREASE:
    case PRODUCT_UI_POMO_INCREASE:
    case PRODUCT_UI_POMO_TOGGLE:
    case PRODUCT_UI_POMO_SET_DURATION:
        if (s_page.command_generation != s_page.snapshot.model.generation) result = D_ERR_BUSY;
        else product_ui_pomodoro_edit(action, s_page.field, s_page.value);
        break;
    default: break;
    }
    if (result != D_OK) product_ui_toast_show(result == D_ERR_BUSY ? "状态已变化，请重试" : "操作未完成，请重试", 1500U);
}

/**
 * @brief 等待实际保存确认后再执行原离开动作，错误时保留页面并提示。
 * @note 要求没有未保存内容且已存在确认记录；仅请求受理不允许提前离开。
 */
static void product_ui_pomodoro_finish_leave(void)
{
    const product_ui_pomodoro_snapshot_t *status = &s_page.snapshot;
    if (!s_page.save_before_leave) return;
    if (!status->storage_ready || status->quiescing) {
        s_page.save_before_leave = 0U;
        product_ui_toast_show("保存未完成，请重试", 1800U);
    } else if (!status->saving && !product_ui_pomodoro_unsaved() && status->has_record) {
        product_ui_pomo_action_e action = s_page.leave_action;
        s_page.save_before_leave = 0U;
        s_page.leave_action = PRODUCT_UI_POMO_NONE;
        s_page.field = 1U;
        product_ui_pomodoro_apply(action);
    } else if (!status->saving && status->storage_error != D_OK &&
        (status->completed_id == s_page.save_request ||
         product_ui_pomodoro_sequence_after(status->completed_id, s_page.save_request))) {
        s_page.save_before_leave = 0U;
        product_ui_toast_show("保存失败，请重试", 1800U);
    }
}

/**
 * @brief 创建番茄钟框架、计时视图及设置／统计导航，使用服务值初始化草稿。
 */
int product_ui_pomodoro_page_create(lv_obj_t *parent)
{
    product_ui_page_spec_t spec = {.title = "番茄钟", .back_title = "桌面", .show_back = 1U,
        .nav_back_cb = product_ui_pomodoro_back};
    lv_obj_t *settings;
    lv_obj_t *stats;
    memset(&s_page, 0, sizeof(s_page));
    product_ui_pomodoro_get_snapshot(&s_page.snapshot);
    s_page.draft = s_page.snapshot.model.saved.settings;
    if (product_ui_page_frame_create_ex(parent, &spec, &s_page.page) != 0) return -1;
    lv_obj_set_width(s_page.page.title, 140);
    if (product_ui_pomodoro_show(PRODUCT_UI_POMO_TIMER) != 0) return -1;
    settings = product_ui_pomodoro_view_nav_button(s_page.page.nav, "设置", PRODUCT_UI_POMO_SETTINGS_OPEN);
    stats = product_ui_pomodoro_view_nav_button(s_page.page.nav, "统计", PRODUCT_UI_POMO_STATS_OPEN);
    if (!settings || !stats) return -1;
    lv_obj_align(settings, LV_ALIGN_RIGHT_MID, -64, 0);
    lv_obj_align(stats, LV_ALIGN_RIGHT_MID, -12, 0);
    return 0;
}

/**
 * @brief 在 UI 线程预约返回计时子视图，由页面处理阶段执行。
 */
void product_ui_pomodoro_page_show_timer(void)
{
    if (s_page.page.root != NULL && s_page.mode != PRODUCT_UI_POMO_TIMER)
        product_ui_pomodoro_queue(PRODUCT_UI_POMO_BACK, 0U, 0U, s_page.snapshot.model.generation);
}

/**
 * @brief 刷新服务值，仅当用户尚无自有草稿时跟随迟到恢复的设置。
 */
static void product_ui_pomodoro_refresh_snapshot(void)
{
    uint8_t follows_saved = memcmp(&s_page.draft, &s_page.snapshot.model.saved.settings,
                                  sizeof(s_page.draft)) == 0;
    product_ui_pomodoro_get_snapshot(&s_page.snapshot);
    /* 页面打开期间可能收到迟到恢复的设置；只有用户没有自己的未保存草稿时才跟随。 */
    if (follows_saved) s_page.draft = s_page.snapshot.model.saved.settings;
}

/**
 * @brief 读取最新服务状态、确认当前完成事件并刷新当前子视图。
 */
static void product_ui_pomodoro_page_enter(void)
{
    product_ui_pomodoro_refresh_snapshot();
    product_ui_pomodoro_acknowledge(s_page.snapshot.model.event_id);
    product_ui_pomodoro_view_refresh(&s_page.snapshot, &s_page.draft);
}

/**
 * @brief 归还下拉占用、删除本页视图并清空页面状态，不停止后台计时。
 */
static void product_ui_pomodoro_page_destroy(void)
{
    product_ui_control_center_release_pull(&s_page.holds_pull);
    product_ui_pomodoro_view_destroy();
    product_ui_page_view_reset(&s_page.page);
    memset(&s_page, 0, sizeof(s_page));
}

/**
 * @brief 推进公共 UI，刷新快照并消费一个动作，再处理保存后离开及视图投影。
 * @param[in] elapsed_ms 本轮页面经过的毫秒数。
 */
static void product_ui_pomodoro_page_update(uint32_t elapsed_ms)
{
    product_ui_pomo_action_e action;
    product_ui_process(elapsed_ms);
    if (!s_page.page.root) return;
    product_ui_pomodoro_refresh_snapshot();
    action = s_page.pending;
    s_page.pending = PRODUCT_UI_POMO_NONE;
    if (action != PRODUCT_UI_POMO_NONE) product_ui_pomodoro_apply(action);
    if (!s_page.page.root) return;
    product_ui_pomodoro_refresh_snapshot();
    product_ui_pomodoro_finish_leave();
    if (!s_page.page.root) return;
    product_ui_pomodoro_view_refresh(&s_page.snapshot, &s_page.draft);
}

static const page_ops_t s_pomodoro_ops = {.create = product_ui_pomodoro_page_create,
    .enter = product_ui_pomodoro_page_enter, .destroy = product_ui_pomodoro_page_destroy,
    .update = product_ui_pomodoro_page_update, .flags = PAGE_FLAG_NO_GESTURE};
static page_desc_t s_pomodoro_page = {.name = "ProductPomodoro", .ops = &s_pomodoro_ops};
/**
 * @brief 注册静态番茄钟页面描述。
 */
int product_ui_pomodoro_register_page(int page_type)
{
    s_pomodoro_page.page_type = page_type;
    return page_manager_register(&s_pomodoro_page);
}
/**
 * @brief 初始化可选服务并发布实际就绪／错误，保留桌面可用。
 * @return 始终返回 D_OK；可选服务失败通过注册表及页面状态展示。
 */
static d_err_t product_ui_pomodoro_module_init(void)
{
    d_err_t result = product_ui_pomodoro_init();
    (void)product_ui_service_publish(PRODUCT_UI_SERVICE_POMODORO,
        result == D_OK ? PRODUCT_UI_SERVICE_READY : PRODUCT_UI_SERVICE_ERROR, 0U, result);
    /* 可选能力失败在页面内明确展示，桌面仍保持可用。 */
    return D_OK;
}
D_UI_MODULE_DEFINE(pomodoro, "番茄钟", LV_SYMBOL_LOOP,
    product_ui_pomodoro_register_page, product_ui_pomodoro_module_init);
#endif
